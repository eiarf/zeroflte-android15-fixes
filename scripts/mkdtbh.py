#!/usr/bin/env python3
"""Build Samsung DTBH v2 dt.img for Exynos 7420 (SM-G920F / zeroflte) S-Boot."""

import argparse
import os
import struct
import sys


def parse_int(val: str) -> int:
    return int(val, 0)


def align_up(value: int, alignment: int) -> int:
    return ((value + alignment - 1) // alignment) * alignment


def build_dtbh_v2(
    dtb_paths: list[str],
    output_path: str,
    page_size: int = 2048,
    chip_id: int = 7420,
    platform_id: int = 0x50A6,
    subtype_id: int = 0x217584DA,
    hw_rev: int = 10,
    hw_rev_end: int = 11,
    space: int = 0x20,
) -> None:
    if not dtb_paths:
        raise ValueError("At least one .dtb input is required")

    num_dtbs = len(dtb_paths)
    # 12-byte DTBH header + 32-byte v2 table entry per DTB
    header_raw_len = 12 + num_dtbs * 32
    header_size = align_up(header_raw_len, page_size)

    entries = []
    blobs = []
    cur_offset = header_size

    for path in dtb_paths:
        with open(path, "rb") as f:
            raw = f.read()
        if len(raw) < 8 or raw[:4] != b"\xd0\x0d\xfe\xed":
            raise ValueError(f"{path} is not a valid FDT/DTB blob")
        padded_len = align_up(len(raw), page_size)
        padded = raw.ljust(padded_len, b"\x00")
        entries.append((
            chip_id,
            platform_id,
            subtype_id,
            hw_rev,
            hw_rev_end,
            cur_offset,
            padded_len,
            space,
        ))
        blobs.append(padded)
        cur_offset += padded_len

    hdr = bytearray(header_size)
    struct.pack_into("<4sII", hdr, 0, b"DTBH", 2, num_dtbs)
    for idx, entry in enumerate(entries):
        struct.pack_into("<8I", hdr, 12 + idx * 32, *entry)

    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
    with open(output_path, "wb") as out:
        out.write(hdr)
        for b in blobs:
            out.write(b)


def main() -> int:
    ap = argparse.ArgumentParser(description="Create Samsung DTBH v2 dt.img")
    ap.add_argument("-d", "--dtb", action="append", required=True, help="Input .dtb file(s)")
    ap.add_argument("-o", "--output", required=True, help="Output dt.img path")
    ap.add_argument("-s", "--page-size", type=parse_int, default=2048)
    ap.add_argument("--chip-id", type=parse_int, default=7420)
    ap.add_argument("--platform-id", type=parse_int, default=0x50A6)
    ap.add_argument("--subtype-id", type=parse_int, default=0x217584DA)
    ap.add_argument("--hw-rev", type=parse_int, default=10)
    ap.add_argument("--hw-rev-end", type=parse_int, default=11)
    ap.add_argument("--space", type=parse_int, default=0x20)
    args = ap.parse_args()

    build_dtbh_v2(
        dtb_paths=args.dtb,
        output_path=args.output,
        page_size=args.page_size,
        chip_id=args.chip_id,
        platform_id=args.platform_id,
        subtype_id=args.subtype_id,
        hw_rev=args.hw_rev,
        hw_rev_end=args.hw_rev_end,
        space=args.space,
    )
    print(f"Wrote DTBH v2 image: {args.output} ({os.path.getsize(args.output)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
