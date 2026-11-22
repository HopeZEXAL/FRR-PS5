#!/usr/bin/env python3
"""How the shaders the game makes (a working shaders.pack) relate to the
shader containers in the game files. Prints only counts, sizes and offsets:
no shader bytes.

  analyse_shaders.py REFERENCE.pack GAME_DIR [UNPACKED_DIR]
GAME_DIR is the installed game folder (frr-ps5/game, with image/ and assets/).
"""
import collections, hashlib, struct, sys
from pathlib import Path


def pack_entries(path):
    data = Path(path).read_bytes()
    count = struct.unpack_from('<I', data, 12)[0]
    at, out = 16, []
    for _ in range(count):
        stage, mask, source, dxil, spirv = struct.unpack_from('<5I', data, at)
        at += 20
        out.append((stage, data[at:at + source]))
        at += source + dxil + spirv
    return out


def scan(games):
    found = {}  # bytes -> (file, offset)
    per_file = collections.Counter()
    for game in map(Path, games):
      for path in sorted(game.rglob('*')):
        if not path.is_file():
            continue
        data = path.read_bytes()
        if data[:4] == bytes.fromhex('0ff512ed'):
            continue  # compressed: scanned in its unpacked copy
        at = -1
        while True:
            at = data.find(b'\x10\x2a\x11', at + 1)
            if at < 0 or at + 36 > len(data):
                break
            if at % 4:
                continue
            flags, vs, ps = struct.unpack_from('>III', data, at)
            if vs < 36 or not ps or vs + ps > 0x100000 or at + vs + ps > len(data):
                continue
            blob = data[at:at + vs + ps]
            found.setdefault(blob, (path.relative_to(game).as_posix(), at))
            per_file[path.suffix.lower() or path.name] += 1
    return found, per_file


def be(blob, at):
    return struct.unpack_from('>I', blob, at)[0]


def main():
    reference = pack_entries(sys.argv[1])
    raw, per_file = scan(sys.argv[2:])
    print(f'reference shaders: {len(reference)}; containers in the game files: {len(raw)}')
    print('containers by file type:', dict(per_file.most_common(12)))
    by_physical = collections.defaultdict(list)
    by_size = collections.defaultdict(list)
    for blob in raw:
        vs = be(blob, 4)
        by_physical[(blob[0:4][3] & 1, blob[vs:])].append(blob)
        by_size[(blob[3] & 1, len(blob))].append(blob)
    kinds = collections.Counter()
    word_offsets = collections.Counter()
    header_fields = collections.Counter()
    sizes_changed = collections.Counter()
    sources = collections.Counter()
    examples = []
    for stage, source in reference:
        vertex = 1 if stage == 0 else 0
        if source in raw:
            kinds['exact', 'vertex' if vertex else 'pixel'] += 1
            sources[raw[source][0].split('/')[0]] += 1
            continue
        vs = be(source, 4)
        same_code = by_physical.get((vertex, source[vs:]), [])
        if same_code:
            # Same microcode: what differs in the container part?
            best = min(same_code, key=lambda b: (len(b) != len(source),
                                                  sum(x != y for x, y in zip(b, source))))
            kinds['same microcode', 'vertex' if vertex else 'pixel'] += 1
            if len(best) != len(source):
                sizes_changed[(len(source) - len(best))] += 1
            shader_offset = be(source, 0x18)
            diffs = sorted({i // 4 * 4 for i, (x, y) in enumerate(zip(best, source)) if x != y})
            for d in diffs:
                if d < 0x24:
                    header_fields[hex(d)] += 1
                else:
                    word_offsets[f'shader+{hex(d - shader_offset)}' if d >= shader_offset else f'tables+{hex(d)}'] += 1
            if len(examples) < 8:
                examples.append(f'{"vertex" if vertex else "pixel"} size {len(best)}->{len(source)} '
                                f'shader_offset {hex(be(best, 0x18))}->{hex(shader_offset)} '
                                f'differing words: {[hex(d) for d in diffs[:12]]}{"…" if len(diffs) > 12 else ""} '
                                f'from {raw[best][0]}')
            continue
        same = by_size.get((vertex, len(source)), [])
        kinds['no match' + (' (same size exists)' if same else ''), 'vertex' if vertex else 'pixel'] += 1
    print('reference shaders by match:')
    for (kind, stage), n in sorted(kinds.items()):
        print(f'  {stage:6} {kind}: {n}')
    print('exact matches come from:', dict(sources))
    print('header fields that differ (offset: count):', dict(header_fields.most_common(10)))
    print('other differing words (where: count):', dict(word_offsets.most_common(15)))
    print('size changes (bytes: count):', dict(sizes_changed.most_common(8)))
    for line in examples:
        print('  e.g.', line)


main()
