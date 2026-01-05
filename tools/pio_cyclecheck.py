#!/usr/bin/env python3
"""
RP2040 PIO cycle counter / tiny simulator.

- Counts cycles exactly as: 1 cycle per instruction + optional delay [0..31].
- Supports a small subset of PIO ops that are common in timing work:
  set, jmp, out, in, wait, nop, push, irq
- Models wrap/wrap_target.
- Can simulate with a simple pin waveform (level over time).
- Can report "watched" signals: e.g., pindirs transitions.

Notes / limitations:
- This does NOT model side-set, pin outputs, or shift direction config in detail.
  (For timing verification, pindirs/watch points are usually enough.)
- 'wait' stalling is modeled cycle-by-cycle using the provided pin waveform.
"""

from __future__ import annotations

import argparse
import re
from dataclasses import dataclass, field
from typing import Optional, List, Tuple, Dict


# ---------- Parser ----------

LABEL_RE = re.compile(r"^\s*(?:public\s+)?([A-Za-z_][\w.]*)\s*:\s*(.*)$")
DIR_RE = re.compile(r"^\s*\.(\w+)")
DELAY_RE = re.compile(r"\[(\d+)\]\s*$")


def strip_comment(line: str) -> str:
    # PIO asm uses ';' for comments
    return line.split(";", 1)[0]


def parse_int(tok: str) -> int:
    tok = tok.strip()
    if tok.lower().startswith("0x"):
        return int(tok, 16)
    if tok.lower().startswith("0b"):
        return int(tok, 2)
    return int(tok, 10)


@dataclass
class Instr:
    pc: int
    op: str
    args: str
    delay: int
    raw: str
    lineno: int


@dataclass
class Program:
    instrs: List[Instr]
    labels: Dict[str, int]
    wrap_target: int
    wrap: int

    def pc_of(self, label: str) -> int:
        if label not in self.labels:
            raise KeyError(f"unknown label: {label}")
        return self.labels[label]


def parse_pio(text: str) -> Program:
    instrs: List[Instr] = []
    labels: Dict[str, int] = {}

    pc = 0
    wrap_target = 0
    wrap = -1
    pending_wrap_target = False

    for lineno, raw in enumerate(text.splitlines(), start=1):
        line = strip_comment(raw).rstrip()
        if not line.strip():
            continue

        mdir = DIR_RE.match(line)
        if mdir:
            d = mdir.group(1)
            if d == "wrap_target":
                pending_wrap_target = True
            elif d == "wrap":
                # .wrap marks the previous instruction as wrap point
                wrap = pc - 1 if pc > 0 else 0
            # ignore other directives (.program, etc.)
            continue

        mlabel = LABEL_RE.match(line)
        if mlabel:
            name = mlabel.group(1)
            rest = mlabel.group(2).strip()
            labels[name] = pc
            if pending_wrap_target:
                wrap_target = pc
                pending_wrap_target = False
            if not rest:
                continue
            line = rest  # label: instr on same line

        delay = 0
        md = DELAY_RE.search(line)
        if md:
            delay = int(md.group(1))
            line = line[: md.start()].rstrip()

        parts = line.strip().split(None, 1)
        op = parts[0]
        args = parts[1] if len(parts) > 1 else ""
        instrs.append(Instr(pc=pc, op=op, args=args, delay=delay, raw=line.strip(), lineno=lineno))
        pc += 1

    if wrap < 0:
        wrap = len(instrs) - 1

    return Program(instrs=instrs, labels=labels, wrap_target=wrap_target, wrap=wrap)


# ---------- Waveform / State / Simulator ----------

def u32(v: int) -> int:
    return v & 0xFFFFFFFF


@dataclass
class Waveform:
    """
    segments: [(duration_cycles, level), ...]
    After end, holds last level. If empty, uses default.
    """
    segments: List[Tuple[int, int]] = field(default_factory=list)
    default: int = 0

    def value_at(self, t: int) -> int:
        if not self.segments:
            return self.default
        r = t
        for dur, lvl in self.segments:
            if r < dur:
                return lvl
            r -= dur
        return self.segments[-1][1]


@dataclass
class State:
    pc: int
    cycles: int = 0
    x: int = 0
    y: int = 0
    osr: int = 0
    isr: int = 0
    pindirs: int = 0


@dataclass
class Event:
    start: int
    end: int
    pc: int
    instr: str
    pin: int
    x: int
    y: int
    pindirs: int
    note: str = ""


def step(prog: Program, st: State, wave: Waveform) -> Event:
    # wrap handling (pc beyond wrap => wrap_target)
    if st.pc > prog.wrap:
        st.pc = prog.wrap_target

    ins = prog.instrs[st.pc]
    note = ""

    # instruction start time (cycle)
    start = st.cycles
    pin = wave.value_at(start)

    # wait can stall before "real execution"
    if ins.op == "wait":
        # wait <level> pin <n>
        parts = ins.args.split()
        if len(parts) < 3:
            raise ValueError(f"bad wait args @L{ins.lineno}: {ins.args}")
        level = int(parts[0])

        stalled = 0
        # each cycle, wait re-checks the pin
        while wave.value_at(st.cycles + stalled) != level:
            stalled += 1
            if stalled > 2_000_000:
                raise RuntimeError("wait stalled too long (check waveform)")
        st.cycles += stalled
        start = st.cycles
        pin = wave.value_at(start)
        note = f"stalled {stalled} cycle(s)"

    op = ins.op
    args = ins.args.strip()
    advance_pc = True

    if op == "set":
        m = re.match(r"(\w+)\s*,\s*(.+)$", args)
        if not m:
            raise ValueError(f"bad set args @L{ins.lineno}: {args}")
        dest, val_s = m.group(1), m.group(2)
        imm = parse_int(val_s)
        if dest == "x":
            st.x = u32(imm)
        elif dest == "y":
            st.y = u32(imm)
        elif dest == "pindirs":
            st.pindirs = imm & 1
            note = note or f"pindirs={st.pindirs}"
        else:
            note = note or f"(ignored set {dest})"

    elif op == "out":
        m = re.match(r"(\w+)\s*,\s*(\d+)$", args)
        if not m:
            raise ValueError(f"bad out args @L{ins.lineno}: {args}")
        dest, count = m.group(1), int(m.group(2))
        mask = 0xFFFFFFFF if count >= 32 else ((1 << count) - 1)
        bits = st.osr & mask

        st.osr = u32(st.osr >> count) if count < 32 else 0

        if dest == "x":
            st.x = u32(bits)
        elif dest == "pindirs":
            st.pindirs = bits & 1
            note = note or f"pindirs={st.pindirs} (out bit)"
        else:
            note = note or f"(ignored out {dest})"

    elif op == "in":
        # in <src>, <count> : timing-only => ignore
        pass

    elif op == "jmp":
        parts = args.split()
        if len(parts) == 1:
            cond = None
            target = parts[0]
        elif len(parts) == 2:
            cond, target = parts
        else:
            raise ValueError(f"bad jmp args @L{ins.lineno}: {args}")

        take = False

        if cond is None:
            take = True
        elif cond == "pin":
            take = (pin == 1)
        elif cond == "!pin":
            take = (pin == 0)
        elif cond == "!x":
            take = (st.x == 0)
        elif cond == "!y":
            take = (st.y == 0)
        elif cond == "x--":
            prev = st.x
            st.x = u32(st.x - 1)
            if prev == 0 and not note:
                note = "WARNING: x underflow (x was 0 before x--)"
            take = (st.x != 0)
        elif cond == "y--":
            prev = st.y
            st.y = u32(st.y - 1)
            if prev == 0 and not note:
                note = "WARNING: y underflow (y was 0 before y--)"
            take = (st.y != 0)
        else:
            note = note or f"WARNING: unsupported jmp cond '{cond}'"

        if take:
            if target not in prog.labels:
                raise KeyError(f"unknown label '{target}' (jmp @L{ins.lineno})")
            st.pc = prog.labels[target]
            advance_pc = False

    elif op == "nop":
        pass

    elif op == "push":
        note = note or "push"

    elif op == "irq":
        note = note or "irq"

    elif op == "wait":
        # already handled stall; nothing else
        pass

    else:
        note = note or f"WARNING: unsupported op '{op}'"

    # execute cost: 1 + delay cycles (after any wait-stall)
    st.cycles += 1 + ins.delay
    end = st.cycles

    if advance_pc:
        st.pc += 1
        if st.pc > prog.wrap:
            st.pc = prog.wrap_target

    return Event(
        start=start, end=end, pc=ins.pc, instr=ins.raw + (f" [{ins.delay}]" if ins.delay else ""),
        pin=pin, x=st.x, y=st.y, pindirs=st.pindirs, note=note
    )


def parse_waveform(s: str) -> Waveform:
    """
    "0:20,1:10,0:5"  => 20 cycles low, 10 cycles high, 5 cycles low.
    """
    s = s.strip()
    if not s:
        return Waveform(default=0)
    segs: List[Tuple[int, int]] = []
    for item in s.split(","):
        item = item.strip()
        if not item:
            continue
        lvl_s, dur_s = item.split(":")
        lvl = int(lvl_s.strip())
        dur = int(dur_s.strip())
        segs.append((dur, lvl))
    return Waveform(segments=segs, default=segs[-1][1] if segs else 0)


# ---------- CLI / Reports ----------

def fmt_time(cycles: int, clock_hz: float) -> str:
    us = cycles / clock_hz * 1e6
    return f"{cycles} cyc ({us:.3f} us)"


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("file", help="PIO asm file")
    ap.add_argument("--clock", type=float, default=16_000_000, help="PIO clock Hz (default: 16MHz)")
    ap.add_argument("--entry", default=None, help="entry label (default: first label)")
    ap.add_argument("--osr", default="0", help="initial OSR value (e.g., 0xAAAAAAAA)")
    ap.add_argument("--pin", default="", help="pin waveform: '0:20,1:10,0:5' in cycles")
    ap.add_argument("--max_cycles", type=int, default=500, help="stop after this many cycles")
    ap.add_argument("--watch", default="pindirs", help="watch signal transitions: pindirs|none")
    ap.add_argument("--trace", action="store_true", help="print full trace")
    args = ap.parse_args()

    text = open(args.file, "r", encoding="utf-8").read()
    prog = parse_pio(text)

    entry = args.entry
    if entry is None:
        # pick the first label by increasing pc
        entry = sorted(prog.labels.items(), key=lambda kv: kv[1])[0][0]

    wave = parse_waveform(args.pin)
    st = State(pc=prog.pc_of(entry), osr=parse_int(args.osr))

    events: List[Event] = []
    watched: List[Tuple[int, int]] = []  # (cycle, pindirs)
    last_pindirs = st.pindirs

    while st.cycles < args.max_cycles:
        ev = step(prog, st, wave)
        events.append(ev)

        if args.watch == "pindirs":
            if st.pindirs != last_pindirs:
                watched.append((ev.start, st.pindirs))
                last_pindirs = st.pindirs

        if args.trace:
            print(f"{fmt_time(ev.start, args.clock):>18}  pc={ev.pc:02d}  {ev.instr:<30}  "
                  f"x={ev.x:08X} y={ev.y:08X} pin={ev.pin} pindirs={ev.pindirs}  {ev.note}")

        # crude stop condition: if we hit wrap_target again and have some history, break
        if len(events) > 5 and st.pc == prog.wrap_target and st.cycles > 0 and not args.trace:
            # don't break too aggressively; let max_cycles control normally
            pass

    print("\n=== Summary ===")
    print(f"entry      : {entry}")
    print(f"clock      : {args.clock:g} Hz")
    print(f"simulated  : {fmt_time(st.cycles, args.clock)}")

    if args.watch == "pindirs":
        print("\n=== pindirs transitions (cycle / us) ===")
        if not watched:
            print("(no transitions)")
        else:
            prev = None
            for t, v in watched:
                us = t / args.clock * 1e6
                if prev is None:
                    print(f"t={t:6d}  ({us:9.3f} us)  pindirs={v}")
                else:
                    dt = t - prev[0]
                    dus = dt / args.clock * 1e6
                    print(f"t={t:6d}  ({us:9.3f} us)  pindirs={v}   (+{dt} cyc / {dus:.3f} us)")
                prev = (t, v)


if __name__ == "__main__":
    main()
