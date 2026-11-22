#!/usr/bin/env python3
"""Compare two shaders.pack files by the shaders they hold (no shader data is
printed, only counts and, for differences, each shader's stage, size and
SHA-256 prefix).

  compare_packs.py REFERENCE.pack NEW.pack
"""
import hashlib, struct, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / 'frr'))
from shader_pack_format import validate_shader_pack  # noqa: E402


def entries(path):
    data = Path(path).read_bytes()
    validate_shader_pack(data)
    count = struct.unpack_from('<I', data, 12)[0]
    at, out = 16, {}
    for _ in range(count):
        stage, mask, source, dxil, spirv = struct.unpack_from('<5I', data, at)
        at += 20
        src = data[at:at + source]
        at += source + dxil + spirv
        out[(stage, hashlib.sha256(src).hexdigest())] = (len(src), mask, spirv)
    return out


reference, new = entries(sys.argv[1]), entries(sys.argv[2])
missing = sorted(set(reference) - set(new))
extra = sorted(set(new) - set(reference))
print(f'reference: {len(reference)} shaders, new: {len(new)} shaders, in both: {len(set(reference) & set(new))}')
print(f'missing from new: {len(missing)}, only in new: {len(extra)}')
for stage, digest in missing[:50]:
    size, mask, _ = reference[(stage, digest)]
    print(f'  missing {"vertex" if stage == 0 else "pixel"} bytes={size} mask={mask} sha256={digest[:16]}')
