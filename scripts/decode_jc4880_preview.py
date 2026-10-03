#!/usr/bin/env python3
"""Validate and render one JC4880 diagnostic log snapshot locally, without uploads."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import struct
import zlib

BEGIN = re.compile(r"Preview begin id=(\d+) width=(\d+) height=(\d+) bytes=(\d+) checksum=([0-9a-f]{8})")
CHUNK = re.compile(r"Preview chunk id=(\d+) offset=(\d+) data=([0-9a-f]+)")
END = re.compile(r"Preview end id=(\d+)")


def decode(log: str) -> tuple[int, int, bytes]:
    """Require contiguous bytes, an end marker, and the device's FNV checksum."""
    current = None
    data = bytearray()
    for line in log.splitlines():
        begin = BEGIN.search(line)
        if begin:
            ident, width, height, size = map(int, begin.groups()[:4])
            if width != 160 or height != 90 or size != width * height:
                raise ValueError("Unexpected preview geometry")
            current = (ident, width, height, size, int(begin.group(5), 16))
            data.clear()
            continue
        if current is None:
            continue
        chunk = CHUNK.search(line)
        if chunk:
            if int(chunk.group(1)) != current[0] or int(chunk.group(2)) != len(data):
                raise ValueError("Preview chunk missing, duplicated, or out of order")
            data.extend(bytes.fromhex(chunk.group(3)))
            if len(data) > current[3]:
                raise ValueError("Preview exceeds declared size")
        end = END.search(line)
        if end and int(end.group(1)) == current[0]:
            if len(data) != current[3]:
                raise ValueError("Incomplete preview")
            checksum = 2166136261
            for value in data:
                checksum = ((checksum ^ value) * 16777619) & 0xFFFFFFFF
            if checksum != current[4]:
                raise ValueError("Preview checksum mismatch")
            return current[1], current[2], bytes(data)
    raise ValueError("No complete preview found")


def png(width: int, height: int, pixels: bytes) -> bytes:
    def chunk(kind: bytes, payload: bytes) -> bytes:
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload))
    rows = b"".join(b"\0" + pixels[y * width:(y + 1) * width] for y in range(height))
    header = struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--output", type=Path, required=True, help="Private local output directory")
    args = parser.parse_args()
    width, height, pixels = decode(args.log.read_text(errors="replace"))
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "preview-raw.png").write_bytes(png(width, height, pixels))
    low, high = min(pixels), max(pixels)
    stretched = bytes(round((v - low) * 255 / (high - low)) for v in pixels) if high > low else pixels
    (args.output / "preview-contrast.png").write_bytes(png(width, height, stretched))
    info = {"width": width, "height": height, "min": low, "max": high,
            "mean": sum(pixels) / len(pixels), "checksum_verified": True,
            "contrast_preview": "Display-only min/max stretch; raw data remains unchanged."}
    (args.output / "preview.json").write_text(json.dumps(info, indent=2))
    print(json.dumps(info))


if __name__ == "__main__":
    main()
