#!/usr/bin/env python3
"""Lists --defsym=NAME=0 for undefined *weak* dynamic symbols of a linked PIE
that no import stub exports.

lld 18 has no -z nodynamic-undefined-weak, so the weak references Mesa's
dispatch tables make to entry points RADV leaves unimplemented become dynamic
imports, which the title converter cannot resolve. In a static link they mean
NULL (Mesa then uses the common runtime's entry point, or reports the command
unsupported); binding them to 0 restores that. Strong undefined symbols and
weak ones a stub really exports are left alone.
"""
import subprocess, sys

def symbols(readelf, path, dynamic):
    out = subprocess.run([readelf, '--dyn-syms' if dynamic else '-s', '-W', path],
                         capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 8 and parts[0].endswith(':'):
            yield parts  # Num Value Size Type Bind Vis Ndx Name

readelf, elf, *stubs = sys.argv[1:]
exported = set()
for stub in stubs:
    for p in symbols(readelf, stub, True):
        if p[6] != 'UND':
            exported.add(p[7].split('@')[0])
names = sorted({p[7].split('@')[0] for p in symbols(readelf, elf, True)
                if p[6] == 'UND' and p[4] == 'WEAK' and p[7].split('@')[0] not in exported})
strong_missing = sorted({p[7].split('@')[0] for p in symbols(readelf, elf, True)
                         if p[6] == 'UND' and p[4] == 'GLOBAL' and p[7].split('@')[0] not in exported})
print(f'weak_zero: {len(names)} weak references bound to 0; '
      f'{len(strong_missing)} strong undefined not in any stub: {" ".join(strong_missing[:20])}',
      file=sys.stderr)
for name in names:
    print(f'--defsym={name}=0')
