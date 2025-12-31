# !/usr/bin/env python3
# """RP2040 PIO assembly code formatter tool."""
import argparse
import re
from pathlib import Path


INDENT = ' ' * 4
LABEL_REGEX = re.compile(r'^\s*[A-Za-z_][\w.]*:\s*$')
COMMENT_COLUMN = 44 # コメントを揃える列

is_inside_wrapped_block = False


def is_directive(line: str) -> bool:
    return line.strip().startswith('.')


def is_label(line: str) -> bool:
    return bool(LABEL_REGEX.match(line))


def normalize_instruction(code: str) -> str:
    code = code.strip()
    code = re.sub(r'\s+', ' ', code)
    code = re.sub(r'\s*,\s*', ', ', code)
    return code


def format_line(line: str) -> str:
    raw = line.rstrip("\n")
    if not raw.strip():
        return ""

    if ".wrap_target" in raw:
        global is_inside_wrapped_block
        is_inside_wrapped_block = True

    # コメント行
    if raw.lstrip().startswith(';'):
        return ' ' * COMMENT_COLUMN + raw.strip() if is_inside_wrapped_block else raw.strip()

    # コードとコメントを分離
    if ';' in raw:
        code_part, comment_part = raw.split(';', 1)
        code_part = code_part.rstrip()
        comment = ';' + comment_part
    else:
        code_part, comment = raw.rstrip(), ''

    if is_directive(code_part) or is_label(code_part):
        code = code_part.strip()
    else:
        code = INDENT + normalize_instruction(code_part)

    if not comment:
        return code.rstrip()

    if len(code) < COMMENT_COLUMN:
        pad = ' ' * (COMMENT_COLUMN - len(code))
        return f"{code}{pad}{comment}"
    else:
        return f"{code}  {comment}"


def format_text(text: str) -> str:
    out_lines = [format_line(line) for line in text.splitlines()]
    return "\n".join(out_lines).rstrip() + "\n"


def main() -> int:
    ap = argparse.ArgumentParser(description="RP2040 PIO assembly code formatter tool.")
    ap.add_argument("file", type=Path, help="Path to the PIO assembly file to format.")
    ap.add_argument("--check", action="store_true", help="Check if the file is already formatted.")
    args = ap.parse_args()

    src = args.file.read_text(encoding="utf-8")
    dst = format_text(src)

    if args.check:
        return 0 if src == dst else 1

    if src != dst:
        args.file.write_text(dst, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
