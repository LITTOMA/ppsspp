#!/usr/bin/env python3
import argparse
import struct
import wave
import zlib


PNG_SIG = b"\x89PNG\r\n\x1a\n"


def paeth(a, b, c):
    p = a + b - c
    pa = abs(p - a)
    pb = abs(p - b)
    pc = abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def read_png_rgba(path):
    data = open(path, "rb").read()
    if not data.startswith(PNG_SIG):
        raise ValueError(f"{path} is not a PNG")

    pos = len(PNG_SIG)
    width = height = bit_depth = color_type = interlace = None
    idat = bytearray()

    while pos < len(data):
        length = struct.unpack(">I", data[pos:pos + 4])[0]
        ctype = data[pos + 4:pos + 8]
        payload = data[pos + 8:pos + 8 + length]
        pos += 12 + length

        if ctype == b"IHDR":
            width, height, bit_depth, color_type, _compression, _filter, interlace = struct.unpack(">IIBBBBB", payload)
        elif ctype == b"IDAT":
            idat.extend(payload)
        elif ctype == b"IEND":
            break

    if bit_depth != 8 or interlace != 0 or color_type not in (2, 6):
        raise ValueError(f"Unsupported PNG format in {path}: bit={bit_depth} color={color_type} interlace={interlace}")

    channels = 4 if color_type == 6 else 3
    bpp = channels
    stride = width * channels
    raw = zlib.decompress(bytes(idat))
    rows = []
    prev = bytearray(stride)
    offset = 0

    for _y in range(height):
        filter_type = raw[offset]
        offset += 1
        row = bytearray(raw[offset:offset + stride])
        offset += stride

        for i in range(stride):
            left = row[i - bpp] if i >= bpp else 0
            up = prev[i]
            up_left = prev[i - bpp] if i >= bpp else 0
            if filter_type == 1:
                row[i] = (row[i] + left) & 0xFF
            elif filter_type == 2:
                row[i] = (row[i] + up) & 0xFF
            elif filter_type == 3:
                row[i] = (row[i] + ((left + up) >> 1)) & 0xFF
            elif filter_type == 4:
                row[i] = (row[i] + paeth(left, up, up_left)) & 0xFF
            elif filter_type != 0:
                raise ValueError(f"Unsupported PNG row filter {filter_type}")

        if color_type == 2:
            rgba = bytearray(width * 4)
            for x in range(width):
                rgba[x * 4:x * 4 + 4] = row[x * 3:x * 3 + 3] + b"\xFF"
            rows.append(rgba)
        else:
            rows.append(row)
        prev = row

    return width, height, rows


def write_png_rgba(path, width, height, rows):
    def chunk(ctype, payload):
        return (
            struct.pack(">I", len(payload)) +
            ctype +
            payload +
            struct.pack(">I", zlib.crc32(ctype + payload) & 0xFFFFFFFF)
        )

    raw = bytearray()
    for row in rows:
        raw.append(0)
        raw.extend(row)

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    out = bytearray(PNG_SIG)
    out.extend(chunk(b"IHDR", ihdr))
    out.extend(chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
    out.extend(chunk(b"IEND", b""))
    open(path, "wb").write(out)


FONT = {
    "P": ["11110", "10001", "10001", "11110", "10000", "10000", "10000"],
    "S": ["01111", "10000", "10000", "01110", "00001", "00001", "11110"],
}


def blend_pixel(row, x, color):
    r, g, b, a = color
    i = x * 4
    inv = 255 - a
    row[i + 0] = (r * a + row[i + 0] * inv) // 255
    row[i + 1] = (g * a + row[i + 1] * inv) // 255
    row[i + 2] = (b * a + row[i + 2] * inv) // 255
    row[i + 3] = 255


def draw_rect(rows, x, y, w, h, color):
    for yy in range(max(0, y), min(len(rows), y + h)):
        row = rows[yy]
        for xx in range(max(0, x), min(len(row) // 4, x + w)):
            blend_pixel(row, xx, color)


def draw_text(rows, text, x, y, scale, color):
    cursor = x
    for ch in text:
        glyph = FONT.get(ch.upper())
        if glyph is None:
            cursor += 6 * scale
            continue
        for gy, bits in enumerate(glyph):
            for gx, bit in enumerate(bits):
                if bit == "1":
                    draw_rect(rows, cursor + gx * scale, y + gy * scale, scale, scale, color)
        cursor += 6 * scale


def resize_nearest(src_rows, src_w, src_h, dst_w, dst_h):
    out = []
    for y in range(dst_h):
        sy = min(src_h - 1, (y * src_h) // dst_h)
        row = bytearray(dst_w * 4)
        src_row = src_rows[sy]
        for x in range(dst_w):
            sx = min(src_w - 1, (x * src_w) // dst_w)
            row[x * 4:x * 4 + 4] = src_row[sx * 4:sx * 4 + 4]
        out.append(row)
    return out


def composite(rows, src_rows, dst_x, dst_y):
    for y, src_row in enumerate(src_rows):
        yy = dst_y + y
        if yy < 0 or yy >= len(rows):
            continue
        dst_row = rows[yy]
        for x in range(len(src_row) // 4):
            xx = dst_x + x
            if xx < 0 or xx >= len(dst_row) // 4:
                continue
            color = src_row[x * 4:x * 4 + 4]
            blend_pixel(dst_row, xx, color)


def make_banner(icon_path, output_path):
    icon_w, icon_h, icon_rows = read_png_rgba(icon_path)
    width, height = 256, 128
    rows = []

    for y in range(height):
        t = y / max(1, height - 1)
        r = int(18 + 8 * t)
        g = int(52 + 18 * t)
        b = int(78 + 30 * t)
        row = bytearray()
        for _x in range(width):
            row.extend((r, g, b, 255))
        rows.append(row)

    draw_rect(rows, 0, 0, width, 4, (65, 170, 205, 255))
    draw_rect(rows, 0, height - 4, width, 4, (12, 30, 48, 255))
    draw_rect(rows, 24, 20, 88, 88, (9, 24, 38, 110))

    icon_size = 78
    icon = resize_nearest(icon_rows, icon_w, icon_h, icon_size, icon_size)
    composite(rows, icon, 29, 25)

    draw_text(rows, "PPSSPP", 121, 42, 4, (245, 250, 255, 245))
    draw_rect(rows, 120, 80, 140, 2, (65, 170, 205, 190))

    write_png_rgba(output_path, width, height, rows)


def make_silent_wav(path):
    sample_rate = 32728
    frames = sample_rate // 2
    with wave.open(path, "wb") as wav:
        wav.setnchannels(2)
        wav.setsampwidth(2)
        wav.setframerate(sample_rate)
        wav.writeframes(b"\x00\x00\x00\x00" * frames)


def main():
    parser = argparse.ArgumentParser(description="Generate PPSSPP 3DS CIA banner inputs.")
    parser.add_argument("--icon", required=True)
    parser.add_argument("--banner-png", required=True)
    parser.add_argument("--silent-wav", required=True)
    args = parser.parse_args()

    make_banner(args.icon, args.banner_png)
    make_silent_wav(args.silent_wav)


if __name__ == "__main__":
    main()
