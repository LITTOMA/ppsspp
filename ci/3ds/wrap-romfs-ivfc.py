#!/usr/bin/env python3
import hashlib
import struct
import sys
from pathlib import Path


BLOCK_SIZE_LOG2 = 12
BLOCK_SIZE = 1 << BLOCK_SIZE_LOG2
IVFC_HEADER_SIZE = 0x5C
IVFC_HEADER_PADDED_SIZE = 0x60
IVFC_MAGIC = 0x00010000


def align(value: int, alignment: int) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def pad(data: bytes, alignment: int) -> bytes:
    return data + b"\0" * (align(len(data), alignment) - len(data))


def hash_blocks(data: bytes) -> bytes:
    padded = pad(data, BLOCK_SIZE)
    return b"".join(
        hashlib.sha256(padded[offset:offset + BLOCK_SIZE]).digest()
        for offset in range(0, len(padded), BLOCK_SIZE)
    )


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {Path(sys.argv[0]).name} input.level3.romfs output.ivfc.romfs", file=sys.stderr)
        return 2

    input_path = Path(sys.argv[1])
    output_path = Path(sys.argv[2])
    level3 = input_path.read_bytes()

    if len(level3) < 0x28 or level3[:4] != b"\x28\x00\x00\x00":
        print(f"{input_path} does not look like a Level 3 RomFS image", file=sys.stderr)
        return 1

    level2 = hash_blocks(level3)
    level1 = hash_blocks(level2)
    master = hash_blocks(level1)

    level3_offset = align(IVFC_HEADER_PADDED_SIZE + len(master), BLOCK_SIZE)
    level1_offset = align(level3_offset + len(level3), BLOCK_SIZE)
    level2_offset = align(level1_offset + len(level1), BLOCK_SIZE)

    header = struct.pack(
        "<4sIIQQIIQQIIQQIIII",
        b"IVFC",
        IVFC_MAGIC,
        len(master),
        level1_offset,
        len(level1),
        BLOCK_SIZE_LOG2,
        0,
        level2_offset,
        len(level2),
        BLOCK_SIZE_LOG2,
        0,
        level3_offset,
        len(level3),
        BLOCK_SIZE_LOG2,
        0,
        0,
        0,
    )
    if len(header) != IVFC_HEADER_SIZE:
        print(f"internal error: IVFC header is {len(header):#x} bytes", file=sys.stderr)
        return 1

    image = bytearray()
    image += header
    image += b"\0" * (IVFC_HEADER_PADDED_SIZE - len(image))
    image += master
    image += b"\0" * (level3_offset - len(image))
    image += level3
    image += b"\0" * (level1_offset - len(image))
    image += level1
    image += b"\0" * (level2_offset - len(image))
    image += level2

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(image)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
