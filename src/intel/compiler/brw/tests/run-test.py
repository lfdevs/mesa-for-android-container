#!/usr/bin/env python3

import argparse
import difflib
import errno
import os
import pathlib
import subprocess
import sys

# The meson version handles windows paths better, but if it's not available
# fall back to shlex
try:
    from meson.mesonlib import split_args
except ImportError:
    from shlex import split as split_args

parser = argparse.ArgumentParser()
parser.add_argument('--gentool',
                    help='path to gentool binary')
parser.add_argument('--gen_name',
                    help='3-letter platform name (as understood by gentool)')
parser.add_argument('--gen_folder',
                    type=pathlib.Path,
                    help='name of the folder for the generation')
args = parser.parse_args()

wrapper = os.environ.get('MESON_EXE_WRAPPER')
if wrapper is not None:
    gentool = split_args(wrapper) + [args.gentool]
else:
    gentool = [args.gentool]

if not args.gen_folder.is_dir():
    print('Test files path does not exist or is not a directory.',
          file=sys.stderr)
    exit(99)

success = True

for asm_file in args.gen_folder.glob('*.asm'):
    expected_file = asm_file.stem + '.expected'
    expected_path = args.gen_folder / expected_file

    try:
        command = gentool + [
            'asm',
            '-p', args.gen_name,
            asm_file.as_posix(),
        ]
        raw = subprocess.check_output(command, timeout=5)
    except OSError as e:
        if e.errno == errno.ENOEXEC:
            print('Skipping due to inability to run host binaries.',
                  file=sys.stderr)
            exit(77)
        raise

    # gentool asm writes raw binary instructions.  The expected files hold
    # hex dumps with 16 bytes per line (matching the pre-xe/xe 128-bit
    # instruction size), so convert on the fly.
    lines_after = []
    for i in range(0, len(raw), 16):
        chunk = raw[i:i+16]
        lines_after.append(' '.join('%02x' % b for b in chunk) + '\n')

    with expected_path.open() as f:
        lines_before = f.readlines()

    diff = ''.join(difflib.unified_diff(lines_before, lines_after,
                                        expected_file, asm_file.stem + '.out'))

    if diff:
        print('Output comparison for {}:'.format(asm_file.name))
        print(diff)
        success = False
    else:
        print('{} : PASS'.format(asm_file.name))

if not success:
    exit(1)
