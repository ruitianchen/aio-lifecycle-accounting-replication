"""Print or execute a compile-only Linux build; never run ANN/AIO executables."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
NATIVE = ROOT / 'src/native'

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--variant', choices=('C8', 'C9'), default='C9')
    parser.add_argument('--execute', action='store_true', help='compile and link only')
    parser.add_argument('--deps-prefix', type=Path, help='optional extracted Debian-package prefix')
    parser.add_argument('--output', type=Path, help='new build directory; no existing outputs overwritten')
    args = parser.parse_args()
    out = (args.output or ROOT / 'build/native' / args.variant).resolve()
    if args.execute and (sys.platform != 'linux' or not shutil.which('g++') or not shutil.which('ar')):
        parser.error('Execution requires Linux, g++, ar, and the libraries in docs/NATIVE_REPRODUCTION.md')
    if args.execute and out.exists():
        parser.error('Build output already exists. Choose a new --output; frozen inputs are never overwritten.')
    if args.execute:
        out.mkdir(parents=True)
    recipe = json.loads((NATIVE / 'build_commands.json').read_text(encoding='utf-8'))
    env = dict(os.environ)
    extra = ['-I' + str(NATIVE)]
    if args.deps_prefix:
        prefix = args.deps_prefix.resolve()
        inc = prefix / 'usr/include'
        lib = prefix / 'usr/lib/x86_64-linux-gnu'
        extra += ['-I' + str(inc), '-I' + str(inc / 'x86_64-linux-gnu'),
                  '-L' + str(lib), '-Wl,-rpath,' + str(lib)]
        env['LD_LIBRARY_PATH'] = str(lib) + (':' + env['LD_LIBRARY_PATH'] if env.get('LD_LIBRARY_PATH') else '')
    executed = []
    started = time.monotonic()
    for command in recipe['commands']:
        argv = []
        for item in command['argv']:
            item = item.replace('__ROOT__/build', str(out)).replace('__ROOT__', str(NATIVE))
            if args.variant == 'C8':
                for rel in ('driver.cpp', 'source/src/linux_aligned_file_reader.cpp'):
                    if item == str(NATIVE / rel):
                        item = str(ROOT / 'src/variants/C8' / rel)
            argv.append(item)
        if argv[0] == 'g++':
            argv[1:1] = extra
        if argv[0] not in ('g++', 'gcc', 'ar'):
            raise ValueError('Unsupported compile tool')
        if not args.execute:
            print(shlex.join(argv))
            continue
        print('Compiling ' + command['name'], flush=True)
        log = out / (command['name'] + '.log')
        with log.open('x', encoding='utf-8') as stream:
            proc = subprocess.run(argv, cwd=NATIVE, env=env, stdout=stream, stderr=subprocess.STDOUT)
        executed.append({'name': command['name'], 'returncode': proc.returncode})
        if proc.returncode:
            print(log.read_text(encoding='utf-8', errors='replace')[-6000:], file=sys.stderr)
            raise SystemExit('Compile/link failed: ' + command['name'])
    if args.execute:
        binaries = {name: hashlib.sha256((out / name).read_bytes()).hexdigest()
                    for name in ('overall', 'search', 'pq_oracle')}
        report = {'status': 'PASS_COMPILE_AND_LINK_ONLY', 'variant': args.variant,
                  'compiler': subprocess.check_output(['g++', '--version'], text=True).splitlines()[0],
                  'commands': executed, 'binary_sha256': binaries,
                  'elapsed_s': time.monotonic() - started, 'executables_run': 0,
                  'performance_qualification': False}
        (out / 'build_report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
        print(json.dumps(report, indent=2))

if __name__ == '__main__':
    main()
