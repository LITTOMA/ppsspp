#!/usr/bin/env python3
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 5:
        print("usage: bin2c.py input output.c output.h symbol", file=sys.stderr)
        return 2

    src = Path(sys.argv[1])
    out_c = Path(sys.argv[2])
    out_h = Path(sys.argv[3])
    symbol = sys.argv[4]
    data = src.read_bytes()

    out_h.write_text(
        f"#pragma once\n"
        f"#include <stddef.h>\n\n"
        f"extern const unsigned char {symbol}[];\n"
        f"extern const unsigned int {symbol}_size;\n",
        encoding="ascii",
    )

    rows = []
    for i in range(0, len(data), 12):
        rows.append("\t" + ", ".join(f"0x{b:02x}" for b in data[i:i + 12]))

    out_c.write_text(
        f"#include \"{out_h.name}\"\n\n"
        f"const unsigned char {symbol}[] __attribute__((aligned(4))) = {{\n"
        + ",\n".join(rows)
        + ("\n" if rows else "")
        + f"}};\n"
        f"const unsigned int {symbol}_size = {len(data)};\n",
        encoding="ascii",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
