#!/usr/bin/python3
#
# Copyright 2025 Valve Corporation
# SPDX-License-Identifier: MIT

import argparse
import csv
import unittest
import sys
import subprocess
import os
import shlex
from unidecode import unidecode

def normalize(x):
    return unidecode(x.lower())

def name(row):
    return normalize(row[0])

def username(row):
    return normalize(row[2])

def find_person(x):
    x = normalize(x)

    filename = 'people.csv'
    path = os.path.join(os.path.dirname(os.path.realpath(__file__)), filename)
    with open(path, 'r') as f:
        people = list(csv.reader(f, skipinitialspace=True))

        # First, try to exactly match username
        for row in people:
            if username(row) == x:
                return row

        # Next, try to exactly match fullname
        for row in people:
            if name(row) == x:
                return row

        # Now we get fuzzy. Try to match a first name.
        candidates = [r for r in people if name(r).split(' ')[0] == x]
        if len(candidates) == 1:
            return candidates[0]

        # Or a last name?
        candidates = [r for r in people if x in name(r).split(' ')]
        if len(candidates) == 1:
            return candidates[0]

    # Well, frick.
    return None

# Self-test... is it even worth find a unit test framework for this?
TEST_CASES = {
    'gfxstrand': 'faith.ekstrand@collabora.com',
    'Faith': 'faith.ekstrand@collabora.com',
    'faith': 'faith.ekstrand@collabora.com',
    'alyssa': 'alyssa.rosenzweig@intel.com',
    'briano': 'ivan.briano@intel.com',
    'schurmann': 'daniel@schuermann.dev',
    'Schürmann': 'daniel@schuermann.dev',
}

for test in TEST_CASES:
    a, b = find_person(test), TEST_CASES[test]
    if a is None or a[1] != b:
        print(test, a, b)
    assert(a is not None and a[1] == b)

# Now the tool itself
if __name__ == "__main__":
    parser = argparse.ArgumentParser(
                        prog='rb',
                        description='Add review trailers')
    parser.add_argument('person', nargs='+', help="Reviewer's username, first name, or full name")
    parser.add_argument('-a', '--ack', action='store_true', help="Apply an acked-by tag")
    parser.add_argument('-d', '--dry-run', action='store_true',
                        help="Print trailer without applying")
    parser.add_argument('-r', '--rebase', nargs='?',
                        help="Rebase on the specified branch applying tags to all commits")
    args = parser.parse_args()

    # If we are rebasing, let git reinvoke this script with the rebase related
    # arguments stripped.
    if args.rebase is not None:
        relevant_args = [sys.argv[0]]
        if args.ack:
            relevant_args.append("--ack")

        relevant_args += args.person

        cmd = f"python3 {' '.join(relevant_args)}"
        rebase = ['git', 'rebase', '--exec', cmd, args.rebase]

        if args.dry_run:
            print(' '.join([shlex.quote(s) for s in rebase]))
            returncode = 0
        else:
            returncode = subprocess.run(rebase).returncode

        sys.exit(returncode)

    for p in args.person:
        person = find_person(p)
        if person is None:
            print(f'Could not uniquely identify {p}, skipping')

        trailer = 'Acked-by' if args.ack else 'Reviewed-by'
        trailer = f'{trailer}: {person[0]} <{person[1]}>'

        if args.dry_run:
            print(trailer)
            continue

        diff_index = subprocess.run("git diff-index --quiet --cached HEAD --".split(" "))
        if diff_index.returncode != 0:
            print("You have staged changes.")
            print("Please commit before applying review tags.")
            sys.exit(1)

        env = os.environ.copy()
        env['GIT_EDITOR'] = f'git interpret-trailers --trailer  "{trailer}" --in-place'
        subprocess.run(["git", "commit", "--amend"], env=env)
