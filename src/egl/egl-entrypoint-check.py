#!/usr/bin/env python

import argparse

PREFIX = 'EGL_ENTRYPOINT('
SUFFIX = ')'

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('header')
    args = parser.parse_args()

    with open(args.header) as header:
        lines = header.readlines()

    entrypoints = []
    for line in lines:
        line = line.strip()
        if line.startswith(PREFIX):
            assert line.endswith(SUFFIX)
            entrypoints.append(line[len(PREFIX):-len(SUFFIX)])

    print('Checking EGL API entrypoints are sorted')

    for i, _ in enumerate(entrypoints):
        # Can't compare the first one with the previous
        if i == 0:
            continue
        if entrypoints[i - 1] > entrypoints[i]:
            print('ERROR: ' + entrypoints[i] + ' should come before ' + entrypoints[i - 1])
            exit(1)

    print('All good :)')

if __name__ == '__main__':
    main()
