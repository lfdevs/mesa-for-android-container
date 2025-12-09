#!/usr/bin/env python3

import argparse
import subprocess
import os


def run(cmd, extra_env):
    env = os.environ | extra_env
    env['MESA_SHADER_CACHE_DISABLE'] = '1'
    subprocess.run(cmd, env=env, shell=True)


def dump(args):
    extra_env = {'IR3_SHADER_BISECT_DUMP_IDS_PATH': args.output}
    run(args.cmd, extra_env)


def was_good():
    while True:
        response = input('Was the previous run [g]ood or [b]ad? ')

        if response in ('g', 'b'):
            return response == 'g'


def bisect(args):
    with open(args.input, 'r') as f:
        ids = [l.strip() for l in f.readlines()]

    ids.sort()

    while len(ids) > 1:
        lo_id = 0
        hi_id = len(ids) // 2
        lo = ids[lo_id]
        hi = ids[hi_id]
        extra_env = {
            'IR3_SHADER_BISECT_LO': str(lo),
            'IR3_SHADER_BISECT_HI': str(hi)
        }
        run(args.cmd, extra_env)

        if was_good():
            del ids[lo_id:hi_id + 1]
        else:
            del ids[hi_id:]

    print(ids)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(required=True)

    gather_parser = subparsers.add_parser('dump-ids')
    gather_parser.add_argument('-o', '--output', required=True)
    gather_parser.add_argument('cmd')
    gather_parser.set_defaults(func=dump)

    bisect_parser = subparsers.add_parser('bisect')
    bisect_parser.add_argument('-i', '--input', required=True)
    bisect_parser.add_argument('cmd')
    bisect_parser.set_defaults(func=bisect)

    args = parser.parse_args()
    args.func(args)
