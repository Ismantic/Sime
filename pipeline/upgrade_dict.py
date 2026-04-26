#!/usr/bin/env python3
"""One-shot upgrade of sime.dict from the old (10-byte ArrayUnit) format
to the new (12-byte ArrayUnit, mmap-friendly) format.

Old DAT layout:    [sz: u32]  [unit×sz: label(1) eow(1) index(4) parent(4)]   = 4 + 10·sz
New DAT layout:    [sz: u32]  [unit×sz: label(1) eow(1) pad(2)  index(4) parent(4)] = 4 + 12·sz
Sections in new format are 4-byte aligned.
"""
import struct
import sys


def upgrade(in_path: str, out_path: str) -> None:
    with open(in_path, 'rb') as f:
        old = f.read()

    # Parse old header (5 × u32).
    if len(old) < 20:
        raise SystemExit("file too small")
    token_count, token_offset, sec0, sec1, total_size = struct.unpack_from('<5I', old, 0)
    if total_size != len(old):
        raise SystemExit(f"total_size mismatch: {total_size} vs {len(old)}")

    # The new file shares the exact same logical sections; only DAT byte
    # widths and inter-section padding change. Build new buffer in order.
    HEADER = 20
    out = bytearray(HEADER)

    # Token table (verbatim — char32 already 4-byte aligned).
    token_section_size = sec0 - token_offset
    new_token_offset = HEADER
    out += old[token_offset:token_offset + token_section_size]

    # Two DAT + side-table sections.
    new_section_offsets = []
    sec_starts_old = [sec0, sec1]
    sec_starts_old.append(total_size)  # sentinel for end of section 1

    for i in range(2):
        # Pad new buffer to 4-byte alignment.
        while len(out) % 4 != 0:
            out.append(0)
        new_section_offsets.append(len(out))

        s = sec_starts_old[i]
        e = sec_starts_old[i + 1]
        # DAT header
        sz = struct.unpack_from('<I', old, s)[0]
        out += struct.pack('<I', sz)
        s += 4
        # Expand units 10 -> 12 bytes (insert 2 zero bytes after eow).
        for u in range(sz):
            label = old[s]
            eow = old[s + 1]
            idx_parent = old[s + 2:s + 10]
            out += bytes([label, eow, 0, 0]) + idx_parent
            s += 10
        # Side table runs from s to e — verbatim copy.
        out += old[s:e]

    # Patch header.
    struct.pack_into('<5I', out, 0,
                     token_count,
                     new_token_offset,
                     new_section_offsets[0],
                     new_section_offsets[1],
                     len(out))

    with open(out_path, 'wb') as f:
        f.write(out)
    print(f"old: {len(old):>10} bytes")
    print(f"new: {len(out):>10} bytes (delta {len(out) - len(old):+d})")


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print("usage: upgrade_dict.py <old.dict> <new.dict>", file=sys.stderr)
        raise SystemExit(2)
    upgrade(sys.argv[1], sys.argv[2])
