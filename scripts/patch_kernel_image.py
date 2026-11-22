#!/usr/bin/env python3
"""Patch or verify ARM64 kernel Image text_offset for Samsung S-Boot (0x205000)."""

import argparse
import struct
import sys


ARM64_MAGIC = b"ARMd"
DEFAULT_SBOOT_TEXT_OFFSET = 0x205000


def parse_int(val: str) -> int:
    return int(val, 0)


def main() -> int:
    ap = argparse.ArgumentParser(description="Patch ARM64 Image text_offset for S-Boot")
    ap.add_argument("image", help="Path to arch/arm64/boot/Image")
    ap.add_argument("--text-offset", type=parse_int, default=DEFAULT_SBOOT_TEXT_OFFSET)
    ap.add_argument("--verify", action="store_true", help="Only verify header without writing")
    args = ap.parse_args()

    with open(args.image, "rb" if args.verify else "r+b") as f:
        hdr = f.read(64)
        if len(hdr) < 64:
            print(f"error: {args.image} is too small ({len(hdr)} bytes)", file=sys.stderr)
            return 1
        if hdr[0x38:0x3C] != ARM64_MAGIC:
            print(
                f"error: {args.image} missing ARM64 magic 'ARMd' at 0x38 (got {hdr[0x38:0x3C]!r})",
                file=sys.stderr,
            )
            return 1
        (cur_offset,) = struct.unpack_from("<Q", hdr, 0x08)
        (img_size,) = struct.unpack_from("<Q", hdr, 0x10)
        if args.verify:
            if cur_offset != args.text_offset:
                print(
                    f"error: {args.image} text_offset is 0x{cur_offset:x}, expected 0x{args.text_offset:x}",
                    file=sys.stderr,
                )
                return 1
            print(f"OK: {args.image} ARM64 text_offset=0x{cur_offset:x} image_size=0x{img_size:x}")
            return 0

        if cur_offset != args.text_offset:
            f.seek(0x08)
            f.write(struct.pack("<Q", args.text_offset))
            print(
                f"Patched {args.image} text_offset: 0x{cur_offset:x} -> 0x{args.text_offset:x}"
            )
        else:
            print(f"Already patched: {args.image} text_offset=0x{cur_offset:x}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
