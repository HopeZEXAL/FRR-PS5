#!/usr/bin/env python3
"""Free Riders Recompiled for PS5: the builder.

Makes the title (PPSA48220/) and the console's data folder (frr-ps5/) from
your own Sonic Free Riders disc image. Nothing from the game is in this
builder: the game code is recompiled from your disc's default.xex, here, and
the game data is copied from your disc.

  build.py DISC OUTPUT [--jobs N] [--keep-work]

DISC is a .iso (or a folder with the disc's files). OUTPUT gets:
  OUTPUT/PPSA48220/   install it as a homebrew title
  OUTPUT/frr-ps5/     copy it to /data/frr-ps5 on the console
Progress goes to stdout as "STEP n/8 text" and "PERCENT p" lines.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import tomllib
from concurrent.futures import ThreadPoolExecutor, as_completed

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / 'scripts' / 'frr'))
WINDOWS = os.name == 'nt'
EXE = '.exe' if WINDOWS else ''
STEPS = 8
# No console window for each program when the builder runs from the window.
NO_WINDOW = {'creationflags': subprocess.CREATE_NO_WINDOW} if WINDOWS else {}


def step(n, text):
    print(f'STEP {n}/{STEPS} {text}', flush=True)


def percent(p):
    print(f'PERCENT {p:.1f}', flush=True)


def tool(name):
    """A program of the builder (tools/), or on PATH while developing."""
    for folder in (HERE / 'tools', HERE / 'tools' / 'llvm' / 'bin'):
        path = folder / (name + EXE)
        if path.is_file():
            return str(path)
    found = shutil.which(name) or (shutil.which(name + '-18') if not WINDOWS else None)
    if not found:
        raise SystemExit(f'missing tool: {name}')
    return found


def run(args, log, **kw):
    with open(log, 'a', encoding='utf-8', errors='replace') as out:
        out.write('$ ' + ' '.join(str(a) for a in args)[:4000] + '\n')
        out.flush()
        try:
            result = subprocess.run([str(a) for a in args], stdout=out, stderr=subprocess.STDOUT, **NO_WINDOW, **kw)
        except OSError as error:
            raise SystemExit(f'{Path(args[0]).name} could not start: {error}')
    if result.returncode != 0:
        raise SystemExit(f'{Path(args[0]).name} failed (exit {result.returncode}); see {log}')


def run_progress(args, log):
    """frr_prep install: PROGRESS lines turned into PERCENT."""
    with open(log, 'a', encoding='utf-8', errors='replace') as out:
        out.write('$ ' + ' '.join(str(a) for a in args) + '\n')
        process = subprocess.Popen([str(a) for a in args], stdout=subprocess.PIPE, stderr=out, text=True, **NO_WINDOW)
        stages = {'checking': 0, 'copying': 1, 'decoding': 2, 'finishing': 3}
        for line in process.stdout:
            out.write(line)
            parts = line.split()
            if len(parts) == 3 and parts[0] == 'PROGRESS':
                percent((stages.get(parts[1], 0) * 100 + int(parts[2])) / 4)
        if process.wait() != 0:
            raise SystemExit(f'installing the game data failed; see {log}')


# ---- 3. the game code: XEX analysis and XenonRecomp (scripts/prepare_recomp.py) ----
def generate_code(xex, recomp, log):
    import prepare_recomp as pr
    import analyse_switches
    from analyse_game import PATTERNS, find_unique_aligned
    from generate_diagnostic import generate

    expected = json.loads((HERE / 'config/source.json').read_text())['default_xex']['sha256']
    with open(xex, 'rb') as source:
        if hashlib.file_digest(source, 'sha256').hexdigest() != expected:
            raise SystemExit('This default.xex is not the supported Sonic Free Riders (Xbox 360) release.')
    recomp.mkdir(parents=True)
    run([tool('sfr_image_dump'), xex, recomp / 'image'], log, timeout=300)
    image = (recomp / 'image/image.bin').read_bytes()
    tables, rejected, bctr_sites = analyse_switches.analyse(image)
    tables = [t for t in tables if t['base'] not in pr.AUDITED_CTR_BRANCHES]
    table_functions, unplaced = analyse_switches.table_functions(image, tables)
    (recomp / 'switches.toml').write_text(analyse_switches.to_toml(tables))
    document = tomllib.loads((HERE / 'config/freeriders.toml').read_text())
    config = document['main']
    midasm_hooks = document.get('midasm_hook', [])
    config['functions'] = config.get('functions', []) + [
        {'address': f['address'], 'size': f['size']} for f in table_functions]
    text_base, text_size = 0x82210000, 0x8BC4EC
    code = image[text_base - 0x82000000:text_base - 0x82000000 + text_size]
    for key, signature in PATTERNS.items():
        if find_unique_aligned(code, bytes.fromhex(signature), text_base) != config[key]:
            raise SystemExit(f'helper address differs from configuration: {key}')
    shutil.copyfile(xex, recomp / 'default.xex')
    config.update(file_path='default.xex', out_directory_path='ppc', switch_table_file_path='switches.toml')
    (recomp / 'recomp.toml').write_text('[main]\n' + ''.join(
        f'{key} = {pr.toml_value(value)}\n' for key, value in config.items()) + ''.join(
        '\n[[midasm_hook]]\n' + ''.join(f'{key} = {pr.toml_value(value)}\n' for key, value in hook.items())
        for hook in midasm_hooks))
    (recomp / 'ppc').mkdir()
    with open(recomp / 'recompile.log', 'w') as out:
        result = subprocess.run([tool('XenonRecomp'), str(recomp / 'recomp.toml'), str(HERE / 'include/ppc_context.h')],
                                stdout=out, stderr=subprocess.STDOUT, timeout=1800, **NO_WINDOW)
    if result.returncode != 0:
        raise SystemExit(f'XenonRecomp failed; see {recomp / "recompile.log"}')
    generate(recomp / 'ppc', recomp / 'recompile.log', recomp / 'diagnostic', recomp / 'switches.toml')
    (recomp / 'default.xex').unlink()


# ---- 3. the game's shaders, translated for Vulkan (shaders.pack) ----
def translate_shaders(game, work, data, log):
    """frr_shaders finds the shaders in the installed game and translates them
    with the game's own code; pack_shaders.py packs them as the PS5 reads them."""
    tools = HERE / 'tools'
    dxc = tools / 'dxc' / ('dxc.exe' if WINDOWS else 'dxc-linux')
    env = dict(os.environ, SFR_SHADER_TRANSLATOR=tool('shader_translate'), SFR_DXC_SPIRV=str(dxc),
               SFR_SHADER_COMMON=str(HERE / 'include/xenos/shader_common.h'))
    if not WINDOWS:
        env['SFR_DXC_LIBRARY'] = str(tools / 'dxc')
    # Most of the game's files are compressed; the shaders are found in
    # unpacked copies (the first half of the step's bar).
    unpacked = work / 'unpacked'
    with open(log, 'a', encoding='utf-8', errors='replace') as out:
        args = [tool('frr_prep'), 'unpack', str(game), str(unpacked)]
        out.write('$ ' + ' '.join(args) + '\n')
        process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=out, text=True, **NO_WINDOW)
        for line in process.stdout:
            parts = line.split()
            if len(parts) == 3 and parts[0] == 'PROGRESS':
                percent(int(parts[1]) * 50 / max(1, int(parts[2])))
            else:
                out.write(line)
        if process.wait() not in (0, 5):
            raise SystemExit(f'unpacking the game files failed; see {log}')
    with open(log, 'a', encoding='utf-8', errors='replace') as out:
        args = [tool('frr_shaders'), str(work), str(game), str(unpacked)]
        out.write('$ ' + ' '.join(args) + '\n')
        process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=out, text=True, env=env, **NO_WINDOW)
        for line in process.stdout:
            parts = line.split()
            if len(parts) == 3 and parts[0] == 'PROGRESS':
                percent(50 + int(parts[1]) * 50 / max(1, int(parts[2])))
            else:
                out.write(line)
        if process.wait() != 0:
            raise SystemExit(f'translating the shaders failed; see {log}')
    shutil.rmtree(unpacked, ignore_errors=True)
    run([sys.executable, HERE / 'scripts/frr/pack_shaders.py', '--cache', work / 'runtime',
         '--output', data / 'shaders.pack'], log)


# ---- 5. compiling for the PS5 (as compile_one.sh) ----
def clang_args(include):
    sdk = HERE / 'runtime/sdk'
    return ['-target', 'x86_64-sie-ps5', '-fvisibility-nodllstorageclass=default',
            '-isystem', sdk / 'include/c++/v1', '-isystem', sdk / 'include',
            '-fno-stack-protector', '-fno-plt', '-femulated-tls', '-fdenormal-fp-math=ieee',
            '-std=gnu++20', '-O3', '-DNDEBUG', '-DSFR_DIRECT_MEMORY=1', '-D_LIBCPP_ENABLE_EXPERIMENTAL',
            '-fno-strict-aliasing', '-ffp-model=strict', '-fexceptions', '-fcxx-exceptions', '-w',
            '-ffunction-sections', '-fdata-sections', '-include', HERE / 'include/compat/atomic_ref_compat.h',
            '-I', HERE / 'include/frr', '-I', include, '-I', HERE / 'include/simde']


def compile_code(sources, include, objects, jobs, log):
    objects.mkdir(parents=True, exist_ok=True)
    clang = tool('clang')
    base = clang_args(include)
    # The biggest files first, so the last ones to finish are small.
    sources = sorted(sources, key=lambda s: -s.stat().st_size)
    done = 0

    def one(source):
        out = objects / (source.stem + '.o')
        result = subprocess.run([clang, *map(str, base), '-c', str(source), '-o', str(out)],
                                capture_output=True, text=True, **NO_WINDOW)
        return source, out, result

    outputs = []
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        for future in as_completed([pool.submit(one, s) for s in sources]):
            source, out, result = future.result()
            if result.returncode != 0:
                with open(log, 'a') as f:
                    f.write(f'{source}\n{result.stdout}{result.stderr}\n')
                raise SystemExit(f'compiling {source.name} failed; see {log}')
            outputs.append(out)
            done += 1
            percent(done * 100 / len(sources))
    return outputs


# ---- 6. linking and signing (as link_game.sh) ----
def link_title(objects, work, log):
    r = HERE / 'runtime'
    lib = r / 'sdk/lib'
    flags = json.loads((r / 'link/radv_link.json').read_text())
    lld = tool('ld.lld')
    stubs = sorted((r / 'stubs').glob('*.so'))
    sdk_so = sorted(lib.glob('*.so'))
    elf = work / 'llvm-pie.elf'

    def link(extra):
        # The arguments go in a response file: with the weak symbols they are
        # far longer than a Windows command line may be (32767 characters).
        # lld reads it GNU-style, so paths use forward slashes, quoted.
        arguments = ['-m', 'elf_x86_64', '-pie', '--eh-frame-hdr', '-z', 'max-page-size=0x4000',
             '-mllvm', '-emulated-tls',
             '-T', r / 'link/ps5-pie-unwind.ld', '-L', r / 'link', '--eh-frame-hdr', *flags['flags'],
             '--version-script', r / 'link/radv-platform-local.map',
             '--version-script', r / 'link/app-symbols.map', '--exclude-libs=ALL',
             '-e', '_start', '-o', elf, *extra,
             r / 'obj/app_crt.o', r / 'obj/app_cpp_runtime.o',
             '--whole-archive', r / 'lib/libfrr_runtime.a', '--no-whole-archive', *objects,
             r / 'lib/libSDL2.a', *stubs, lib / 'libc++experimental.a',
             '-L', lib, '--whole-archive', r / 'lib/libvulkan_radeon.ps5.a', '--no-whole-archive',
             '--start-group', lib / 'libc++.a', lib / 'libc++abi.a', lib / 'libunwind.a',
             r / 'lib/libclang_rt.builtins-x86_64.a', lib / 'libps5platform.a', '--end-group',
             '--as-needed', *sdk_so, '--hash-style=gnu']
        response = work / 'link.rsp'
        response.write_text('\n'.join('"' + str(a).replace('\\', '/').replace('"', '\\"') + '"' for a in arguments) + '\n',
                            encoding='utf-8')
        run([lld, f'@{response}'], log)

    link([])
    weak = subprocess.run([sys.executable, HERE / 'scripts/weak_zero.py', tool('llvm-readelf'), elf, *stubs, *sdk_so],
                          capture_output=True, text=True, check=True, **NO_WINDOW).stdout.split()
    link(weak)
    native = tool('ps5-native-tool')
    eboot_elf = work / 'eboot.elf'
    run([native, 'link', '--in', elf, '--out', eboot_elf, '--stub-dir', lib,
         *sum((['--stub', s] for s in stubs), []),
         '--module-sdk', '0x02000009', '--companion-sdk', '0x08050001', '--file-name', 'eboot.elf'], log)
    return eboot_elf


def default_jobs():
    """One compiler per processor, as far as memory allows (a compiler of the
    biggest files peaks at about half a gigabyte)."""
    cpus = os.cpu_count() or 2
    try:
        if WINDOWS:
            import ctypes

            class Status(ctypes.Structure):
                _fields_ = [('length', ctypes.c_ulong), ('load', ctypes.c_ulong), ('total', ctypes.c_ulonglong),
                            ('available', ctypes.c_ulonglong), ('page_total', ctypes.c_ulonglong),
                            ('page_available', ctypes.c_ulonglong), ('virtual_total', ctypes.c_ulonglong),
                            ('virtual_available', ctypes.c_ulonglong), ('extended', ctypes.c_ulonglong)]
            status = Status(); status.length = ctypes.sizeof(Status)
            ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status))
            available = status.available
        else:
            available = os.sysconf('SC_AVPHYS_PAGES') * os.sysconf('SC_PAGE_SIZE')
        return max(1, min(cpus, int(available // (768 << 20))))
    except Exception:
        return max(1, cpus // 2)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('disc', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--jobs', type=int, default=0, help='compilers at once (default: by processors and memory)')
    parser.add_argument('--keep-work', action='store_true')
    parser.add_argument('--skip-data', action='store_true', help='make the title only')
    args = parser.parse_args()
    args.jobs = args.jobs or default_jobs()
    disc, output = args.disc.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    work = output / 'frr-build-work'
    if work.exists():
        shutil.rmtree(work)
    work.mkdir()
    log = output / 'frr-build.log'
    log.write_text(f'Free Riders Recompiled for PS5 builder, {time.ctime()}, {args.jobs} compilers at once\n')
    started = time.time()
    prep = tool('frr_prep')

    step(1, 'Checking the disc image')
    run([prep, 'check', disc], log)
    run([prep, 'xex', disc, work / 'default.xex'], log)

    step(2, 'Preparing the game data')
    data = output / 'frr-ps5'
    if not args.skip_data:
        if (data / 'game').exists():
            shutil.rmtree(data / 'game')
        data.mkdir(exist_ok=True)
        run_progress([prep, 'install', disc, data / 'game'], log)

    step(3, "Translating the game's shaders")
    if (data / 'game').is_dir():
        translate_shaders(data / 'game', work / 'shaders', data, log)

    step(4, 'Recompiling the game code (a few minutes)')
    recomp = work / 'recomp'
    generate_code(work / 'default.xex', recomp, log)

    step(5, 'Optimising the game code')
    locals_dir = work / 'register-locals'
    run([sys.executable, HERE / 'scripts/frr/register_locals.py', '--classes', 'ctr,xer,cr,nonargument,nonvolatile',
         '--loop-heads', '--image', recomp / 'image/image.bin', '--report', work / 'register-locals.json',
         recomp / 'diagnostic', locals_dir], log)

    step(6, 'Compiling for the PS5 (the longest step)')
    diag = recomp / 'diagnostic'
    sources = sorted(locals_dir.glob('ppc_recomp.*.cpp')) + [diag / 'ppc_func_mapping.cpp', diag / 'imports.cpp']
    objects = compile_code(sources, diag, work / 'obj', args.jobs, log)

    step(7, 'Linking and signing the title')
    eboot_elf = link_title(objects, work, log)
    title = output / 'PPSA48220'
    if title.exists():
        shutil.rmtree(title)
    shutil.copytree(HERE / 'title/PPSA48220', title)
    run([tool('ps5-native-tool'), 'self', '--sign', '--in', eboot_elf, '--out', title / 'eboot.bin',
         '--magic', '0x1D3D154F'], log)
    run([tool('ps5-native-tool'), 'self', '--inspect', '--file', title / 'eboot.bin'], log)

    step(8, 'Finishing')
    if not args.keep_work:
        shutil.rmtree(work, ignore_errors=True)
    minutes = (time.time() - started) / 60
    with open(log, 'a', encoding='utf-8') as out:
        out.write(f'DONE {time.ctime()}, {minutes:.1f} minutes\n')
    print(f'DONE {minutes:.1f} minutes', flush=True)
    print(f'Copy {title} to your homebrew folder and {data} to /data/frr-ps5 on the console.', flush=True)


if __name__ == '__main__':
    try:
        main()
    except SystemExit as stop:
        if stop.code not in (None, 0) and not isinstance(stop.code, int):
            print(f'ERROR {stop.code}', flush=True)
            try:
                with open(Path(sys.argv[2]).resolve() / 'frr-build.log', 'a', encoding='utf-8') as out:
                    out.write(f'ERROR {stop.code}\n')
            except (OSError, IndexError):
                pass
        raise
