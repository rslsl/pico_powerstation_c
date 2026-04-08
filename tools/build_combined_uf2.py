#!/usr/bin/env python3
"""Build a combined RP2040 flash image from multiple BIN inputs and convert it to UF2.

Usage:
    build_combined_uf2.py <picotool> <output.uf2> <family_id_hex> <base_addr_hex>
                          <bin1> <offset1_hex> [<bin2> <offset2_hex> ...]

Example:
    build_combined_uf2.py picotool.exe combined.uf2 0xE48BFF56 0x10000000 \
        loader.bin 0x0 slot_a.bin 0x20000
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path


def _parse_int(text: str) -> int:
    return int(text, 0)


def main() -> int:
    if len(sys.argv) < 7 or ((len(sys.argv) - 5) % 2) != 0:
        print(__doc__.strip(), file=sys.stderr)
        return 1

    picotool = Path(sys.argv[1])
    output_uf2 = Path(sys.argv[2])
    family_id = _parse_int(sys.argv[3])
    base_addr = _parse_int(sys.argv[4])

    inputs: list[tuple[Path, int]] = []
    for i in range(5, len(sys.argv), 2):
        inputs.append((Path(sys.argv[i]), _parse_int(sys.argv[i + 1])))

    if not picotool.exists():
        raise FileNotFoundError(f"picotool not found: {picotool}")
    if not inputs:
        raise ValueError("at least one BIN input is required")

    spans: list[tuple[Path, int, bytes]] = []
    image_end = 0
    for path, offset in inputs:
        data = path.read_bytes()
        spans.append((path, offset, data))
        image_end = max(image_end, offset + len(data))

    flash_image = bytearray(b"\xFF" * image_end)
    for path, offset, data in spans:
        end = offset + len(data)
        existing = flash_image[offset:end]
        if any(b != 0xFF for b in existing):
            raise ValueError(f"image overlap detected for {path} at 0x{offset:X}")
        flash_image[offset:end] = data

    output_uf2.parent.mkdir(parents=True, exist_ok=True)
    output_bin = output_uf2.with_suffix(".flash.bin")
    output_bin.write_bytes(flash_image)

    cmd = [
        str(picotool),
        "uf2",
        "convert",
        str(output_bin),
        str(output_uf2),
        "-o",
        hex(base_addr),
        "--family",
        hex(family_id),
    ]
    subprocess.run(cmd, check=True)

    print(f"combined bin: {output_bin} ({len(flash_image)} bytes)")
    print(f"combined uf2: {output_uf2}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
