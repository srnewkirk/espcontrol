"""Reject incomplete/corrupt camera preview logs before interpreting their pixels."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("preview", Path(__file__).resolve().parents[2] / "scripts/decode_jc4880_preview.py")
preview = importlib.util.module_from_spec(spec)
spec.loader.exec_module(preview)


class PreviewTest(unittest.TestCase):
    def setUp(self):
        self.pixels = bytes(range(256)) * 56 + bytes(range(64))
        checksum = 2166136261
        for value in self.pixels:
            checksum = ((checksum ^ value) * 16777619) & 0xFFFFFFFF
        self.lines = [f"[I] Preview begin id=42 width=160 height=90 bytes=14400 checksum={checksum:08x}"]
        self.lines += [f"[I] Preview chunk id=42 offset={offset} data={self.pixels[offset:offset+128].hex()}" for offset in range(0, len(self.pixels), 128)]
        self.lines += ["[I] Preview end id=42"]

    def test_round_trip(self):
        self.assertEqual(preview.decode("\n".join(self.lines)), (160, 90, self.pixels))

    def test_dropped_chunk(self):
        del self.lines[3]
        with self.assertRaisesRegex(ValueError, "missing"):
            preview.decode("\n".join(self.lines))

    def test_corruption(self):
        self.lines[1] = self.lines[1].replace("data=00", "data=01")
        with self.assertRaisesRegex(ValueError, "checksum"):
            preview.decode("\n".join(self.lines))

    def test_no_end(self):
        with self.assertRaisesRegex(ValueError, "complete"):
            preview.decode("\n".join(self.lines[:-1]))


if __name__ == "__main__":
    unittest.main()
