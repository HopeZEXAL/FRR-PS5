#!/usr/bin/env python3
"""The layout of the game's compressed files (they start 0F F5 12 ED): sizes
and header words only, for working out how to unpack them on the PC.

  probe_compressed.py GAME_DIR
"""
import collections, struct, sys
from pathlib import Path

game = Path(sys.argv[1])
files = [p for p in sorted(game.rglob('*')) if p.is_file() and p.stat().st_size >= 64]
compressed = []
for p in files:
    with p.open('rb') as f:
        if f.read(4) == bytes.fromhex('0ff512ed'):
            compressed.append(p)
plain = len(files) - len(compressed)
print(f'files: {len(files)}, compressed: {len(compressed)}, not compressed: {plain}')
print('compressed by type:', dict(collections.Counter(p.suffix.lower() for p in compressed).most_common(12)))
print('not compressed by type:', dict(collections.Counter(p.suffix.lower() for p in files if p not in compressed).most_common(12)))
words = collections.Counter()
for p in compressed:
    words[p.read_bytes()[4:8].hex()] += 1
print('header word +4 values:', dict(words.most_common(5)))
flags = collections.Counter(p.read_bytes()[12:16].hex() for p in compressed)
print('header word +12 values:', dict(flags.most_common(5)))
# A few files in detail: size, header, and the size fields after it.
for p in sorted(compressed, key=lambda q: q.stat().st_size)[:3] + sorted(compressed, key=lambda q: -q.stat().st_size)[:2]:
    d = p.read_bytes()
    print(f'\n{p.suffix} {len(d)} bytes; header {d[:16].hex(" ", 4)}')
    print('  words after header:', [hex(struct.unpack_from(">I", d, 16 + 4 * i)[0]) for i in range(6)])
    print('  halfwords after header:', [hex(struct.unpack_from(">H", d, 16 + 2 * i)[0]) for i in range(8)])
    # Chains of big-endian 16-bit sizes from a few starting points.
    for start in (16, 20, 24, 28, 32):
        at, frames = start, 0
        while at + 2 <= len(d):
            size = struct.unpack_from('>H', d, at)[0]
            at += 2
            if size == 0:
                break
            at += size
            frames += 1
        print(f'  16-bit chain from +{start}: {frames} frames, ends at {at} (file {len(d)}){"  EXACT" if at == len(d) else ""}')
    # Blocks of [32-bit size][data] from the header.
    at, blocks = 16, []
    while at + 4 <= len(d) and len(blocks) < 400:
        size = struct.unpack_from('>I', d, at)[0]
        if size == 0 or at + 4 + size > len(d):
            break
        blocks.append(size)
        at += 4 + size
    print(f'  32-bit blocks from +16: {len(blocks)} (first {blocks[:4]}), end {at}{"  EXACT" if at == len(d) else ""}')
