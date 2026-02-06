#!/usr/bin/env python3
"""
overlay_server.py

- Reads fixed-format data lines from a serial port:
    {MARK},frame,sx,sy,crc

  Example:
    D,12345,200,41,5A

  Where:
    MARK : default "D" (configurable)
    frame: 0..65535 (decimal)
    sx,sy: 0..255 (decimal)
    crc  : CRC-8 (ATM/poly=0x07, init=0x00) over bytes [frame_hi, frame_lo, sx, sy],
           printed as 2-digit hex (00..FF).

- Serves an OBS Browser Source overlay at:
    http://HOST:PORT/overlay.html

- Pushes the latest values to the overlay via WebSocket at:
    ws://HOST:PORT/ws

Dependencies:
  pip install pyserial aiohttp
"""

from __future__ import annotations

import argparse
import asyncio
import json
import re
import threading
import time
from dataclasses import dataclass
from typing import Optional, Tuple

import serial
from aiohttp import web, WSMsgType


# ---------- CRC-8 (ATM) ----------
def crc8_atm(data: bytes, poly: int = 0x07, init: int = 0x00) -> int:
    """
    CRC-8, poly=0x07, init=0x00, xorout=0x00, MSB-first.
    Matches the suggested Pico implementation.
    """
    crc = init & 0xFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) & 0xFF) ^ poly
            else:
                crc = (crc << 1) & 0xFF
    return crc & 0xFF


# ---------- Barcode bits (for recording) ----------
def make_bits(frame: int, sx: int, sy: int) -> str:
    """
    48 bits total:
      preamble 0xA5 (8)
      payload  frame_hi, frame_lo, sx, sy (32)
      crc8(payload) (8)
    """
    pre = 0xA5
    payload = bytes([(frame >> 8) & 0xFF, frame & 0xFF, sx & 0xFF, sy & 0xFF])
    c = crc8_atm(payload)
    full = bytes([pre]) + payload + bytes([c])
    return "".join(f"{b:08b}" for b in full)


# ---------- Latest state shared inside the aiohttp event loop ----------
@dataclass
class Latest:
    frame: int = 0
    sx: int = 128
    sy: int = 128
    bits: str = ""
    updated_at: float = 0.0


# ---------- HTML overlay ----------
def build_overlay_html() -> str:
    # Fixed-size canvas is simplest for OBS.
    # If you change these, also change OBS Browser Source width/height.
    return r"""<!doctype html>
<html>
<head>
  <meta charset="utf-8"/>
  <style>
    body { margin:0; background:transparent; overflow:hidden; }
    #wrap { width: 860px; height: 160px; position: relative; }
    canvas { display:block; }
    .dbg {
      position:absolute;
      left: 12px;
      top: 102px;
      font: 28px/1.1 ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", "Courier New", monospace;
      color: white;
      text-shadow: 0 0 6px rgba(0,0,0,0.95);
      user-select: none;
      pointer-events: none;
    }
  </style>
</head>
<body>
<div id="wrap">
  <canvas id="c" width="920" height="200"></canvas>
  <div class="dbg" id="dbg">waiting...</div>
</div>

<script>
const canvas = document.getElementById('c');
const ctx = canvas.getContext('2d', { alpha: true });
const dbg = document.getElementById('dbg');

function bitsToHex(bits) {
  const out = [];
  for (let i = 0; i + 7 < bits.length; i += 8) {
    const byte = parseInt(bits.slice(i, i + 8), 2);
    out.push(byte.toString(16).toUpperCase().padStart(2, '0'));
  }
  return out.join(' ');
}

// Draw a robust 1D binary barcode.
// bits: string of '0'/'1' (expected 48)
function draw(bits, frame, sx, sy) {
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  ctx.fillStyle = 'rgba(0,0,0,1.0)';
  ctx.fillRect(0, 0, canvas.width, canvas.height);

  const marginX = 12;
  const top = 18;
  const bitW = 14;   // increase if you want extra robustness
  const bitH = 62;
  const bitHzero = 20; // height of '0' bits
  const gap  = 2;

  // Guard pattern to make it easier to locate in post-processing:
  // white, gray, white
  function guard(x) {
    ctx.fillStyle = '#fff'; ctx.fillRect(x, top, bitW, bitH);
    ctx.fillStyle = '#888'; ctx.fillRect(x + (bitW+gap), top, bitW, bitH);
    ctx.fillStyle = '#fff'; ctx.fillRect(x + 2*(bitW+gap), top, bitW, bitH);
    return x + 3*(bitW+gap) + 10;
  }

  let x = marginX;
  x = guard(x);

  // Bits
  for (let i = 0; i < bits.length; i++) {
    ctx.fillStyle = '#fff';
    const bitHdraw = (bits[i] === '1') ? bitH : bitHzero;
    const bitTop = top + (bitH - bitHdraw);
    ctx.fillRect(x, bitTop, bitW, bitHdraw);
    x += bitW + gap;
  }

  x += 10;
  guard(x);

  // Debug text
  const hex = bitsToHex(bits);
  dbg.textContent =
    `frame=${frame}  sx=${sx}  sy=${sy}  bits=${bits.length}\n` +
    ` bits=${bits}\n` +
    `  hex=${hex}`;
}

// WebSocket connect / reconnect
function connect() {
  const wsProto = (location.protocol === 'https:') ? 'wss://' : 'ws://';
  const ws = new WebSocket(wsProto + location.host + '/ws');

  ws.onmessage = (ev) => {
    const msg = JSON.parse(ev.data);
    draw(msg.bits, msg.frame, msg.sx, msg.sy);
  };

  ws.onclose = () => setTimeout(connect, 500);
  ws.onerror = () => ws.close();
}

connect();
</script>
</body>
</html>
"""


# ---------- Serial line parsing ----------
def build_line_regex(mark: str) -> re.Pattern:
    # Strict: MARK,frame,sx,sy,CRC(2 hex)
    # Example: D,12345,200,41,5A
    m = re.escape(mark)
    return re.compile(rf"^{m},(\d+),(\d+),(\d+),([0-9A-Fa-f]{{2}})$")


def parse_data_line(
    line: str, mark: str, line_re: re.Pattern
) -> Optional[Tuple[int, int, int]]:
    s = line.strip()
    m = line_re.match(s)
    if not m:
        return None

    frame = int(m.group(1))
    sx = int(m.group(2))
    sy = int(m.group(3))
    crc_in = int(m.group(4), 16)

    if not (0 <= frame <= 65535 and 0 <= sx <= 255 and 0 <= sy <= 255):
        return None

    payload = bytes([(frame >> 8) & 0xFF, frame & 0xFF, sx & 0xFF, sy & 0xFF])
    if crc8_atm(payload) != crc_in:
        return None

    return frame, sx, sy


# ---------- Serial thread ----------
def serial_reader_thread(
    port: str,
    baud: int,
    mark: str,
    loop: asyncio.AbstractEventLoop,
    q: asyncio.Queue,
    stop_flag: threading.Event,
) -> None:
    line_re = build_line_regex(mark)
    ser = serial.Serial(port, baudrate=baud, timeout=0.5)
    try:
        while not stop_flag.is_set():
            raw = ser.readline()
            if not raw:
                continue

            # Decode permissively; we only care about strict "D,...." lines anyway.
            line = raw.decode("utf-8", errors="ignore")
            parsed = parse_data_line(line, mark, line_re)
            if parsed is None:
                continue

            frame, sx, sy = parsed

            # Thread-safe enqueue into asyncio loop.
            def _put_nowait():
                try:
                    q.put_nowait((frame, sx, sy))
                except asyncio.QueueFull:
                    # Drop if overloaded; better than blocking the serial thread.
                    pass

            loop.call_soon_threadsafe(_put_nowait)

    finally:
        try:
            ser.close()
        except Exception:
            pass


# ---------- aiohttp handlers ----------
async def overlay_handler(request: web.Request) -> web.Response:
    return web.Response(text=request.app["overlay_html"], content_type="text/html")


async def ws_handler(request: web.Request) -> web.WebSocketResponse:
    ws = web.WebSocketResponse(heartbeat=15.0)
    await ws.prepare(request)

    clients: set = request.app["clients"]
    clients.add(ws)

    # Send latest immediately
    latest: Latest = request.app["latest"]
    await ws.send_str(
        json.dumps(
            {
                "frame": latest.frame,
                "sx": latest.sx,
                "sy": latest.sy,
                "bits": latest.bits,
            }
        )
    )

    try:
        async for msg in ws:
            if msg.type == WSMsgType.ERROR:
                break
    finally:
        clients.discard(ws)

    return ws


async def broadcaster(app: web.Application) -> None:
    q: asyncio.Queue = app["queue"]
    latest: Latest = app["latest"]
    clients: set = app["clients"]

    while True:
        frame, sx, sy = await q.get()
        latest.frame = frame
        latest.sx = sx
        latest.sy = sy
        latest.bits = make_bits(frame, sx, sy)
        latest.updated_at = time.time()

        payload = json.dumps({"frame": frame, "sx": sx, "sy": sy, "bits": latest.bits})
        dead = []
        for ws in clients:
            try:
                await ws.send_str(payload)
            except Exception:
                dead.append(ws)
        for ws in dead:
            clients.discard(ws)


async def main() -> None:
    ap = argparse.ArgumentParser(
        description="Serial -> OBS overlay (barcode + debug text)"
    )
    ap.add_argument(
        "--serial", required=True, help="macOS: /dev/cu.usbmodemXXXX  (or /dev/tty.*)"
    )
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument(
        "--mark", default="D", help="fixed marker at line start (default: D)"
    )
    ap.add_argument(
        "--host", default="127.0.0.1", help="bind host (use 0.0.0.0 for LAN)"
    )
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument(
        "--queue-size", type=int, default=256, help="async queue size (default: 256)"
    )
    args = ap.parse_args()

    app = web.Application()
    app["clients"] = set()
    app["queue"] = asyncio.Queue(maxsize=args.queue_size)
    app["overlay_html"] = build_overlay_html()
    app["latest"] = Latest(
        frame=0, sx=128, sy=128, bits=make_bits(0, 128, 128), updated_at=time.time()
    )

    app.router.add_get("/overlay.html", overlay_handler)
    app.router.add_get("/ws", ws_handler)

    # Start serial reader thread
    loop = asyncio.get_running_loop()
    stop_flag = threading.Event()
    t = threading.Thread(
        target=serial_reader_thread,
        args=(args.serial, args.baud, args.mark, loop, app["queue"], stop_flag),
        daemon=True,
    )
    t.start()

    # Start web server
    runner = web.AppRunner(app)
    await runner.setup()
    site = web.TCPSite(runner, args.host, args.port)
    await site.start()

    # Start broadcaster task
    asyncio.create_task(broadcaster(app))

    print("Overlay server running.")
    print("OBS Browser Source URL:")
    print(f"  http://{args.host}:{args.port}/overlay.html")
    print("")
    print("Expected serial data line format:")
    print(f"  {args.mark},frame,sx,sy,crc")
    print("Example:")
    print(f"  {args.mark},12345,200,41,5A")

    try:
        while True:
            await asyncio.sleep(3600)
    except (KeyboardInterrupt, SystemExit):
        pass
    finally:
        stop_flag.set()
        await runner.cleanup()


if __name__ == "__main__":
    asyncio.run(main())
