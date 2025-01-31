#!/usr/bin/env python3
# Copyright © 2025 Intel Corporation
# SPDX-License-Identifier: MIT

from __future__ import annotations
import argparse
import binascii


def main() -> None:
    p = argparse.ArgumentParser()

    p.add_argument('--output', dest='output', action='store',
                   help='Output file', required=True)
    p.add_argument('--prefix', action='store',
                   help='Prefix string to use', required=True)
    p.add_argument('inputs', metavar='SPIRV', nargs='+')

    args = p.parse_args()

    for f in args.inputs:
        with open(f, 'rb') as fin:
            with open(args.output, 'w') as fout:
                fout.write("#pragma one\n")

                fout.write("const uint32_t {0}[] = {{".format(args.prefix))

                count = 0
                while True:
                    dword = fin.read(4)
                    if not dword:
                        break
                    if count % 8 == 0:
                        fout.write("\n   ")
                    fout.write('{:#x}, '.format(int.from_bytes(dword, byteorder='little', signed=False)))
                    count += 1

                fout.write("\n")
                fout.write("};\n")


if __name__ == '__main__':
    main()
