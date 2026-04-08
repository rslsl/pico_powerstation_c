#!/usr/bin/env python3
"""Combine two or more UF2 files into a single UF2 image.

Usage: combine_uf2.py <input1.uf2> <input2.uf2> [... <inputN.uf2>] <output.uf2>

UF2 format: each block is 512 bytes.  This script simply concatenates
all blocks, validating the magic numbers and updating the total block
count in each block header so the result is a valid UF2 file.
"""

import struct
import sys

UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END    = 0x0AB16F30
BLOCK_SIZE       = 512


def read_uf2_blocks(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) % BLOCK_SIZE != 0:
        raise ValueError(f"{path}: size {len(data)} is not a multiple of {BLOCK_SIZE}")
    blocks = []
    for off in range(0, len(data), BLOCK_SIZE):
        block = bytearray(data[off:off + BLOCK_SIZE])
        magic0, magic1 = struct.unpack_from("<II", block, 0)
        magic_end, = struct.unpack_from("<I", block, BLOCK_SIZE - 4)
        if magic0 != UF2_MAGIC_START0 or magic1 != UF2_MAGIC_START1:
            raise ValueError(f"{path}: bad start magic at offset {off}")
        if magic_end != UF2_MAGIC_END:
            raise ValueError(f"{path}: bad end magic at offset {off}")
        blocks.append(block)
    return blocks


def main():
    if len(sys.argv) < 4:
        print(__doc__.strip(), file=sys.stderr)
        sys.exit(1)

    inputs = sys.argv[1:-1]
    output = sys.argv[-1]

    all_blocks = []
    for path in inputs:
        blocks = read_uf2_blocks(path)
        print(f"  {path}: {len(blocks)} blocks")
        all_blocks.extend(blocks)

    total = len(all_blocks)
    for idx, block in enumerate(all_blocks):
        # UF2 header: [magic0:0][magic1:4][flags:8][targetAddr:12][payloadSize:16]
        #             [blockNo:20][numBlocks:24][familyID:28][data:32..507][magicEnd:508]
        struct.pack_into("<I", block, 20, idx)      # blockNo
        struct.pack_into("<I", block, 24, total)     # numBlocks

    with open(output, "wb") as f:
        for block in all_blocks:
            f.write(block)

    print(f"  -> {output}: {total} blocks ({total * BLOCK_SIZE} bytes)")


if __name__ == "__main__":
    main()
