#!/usr/bin/env python3
# Copyright © 2026 Valve Corporation
# SPDX-License-Identifier: MIT

import argparse
import sys

VALID_COMMON_VK_OPTIONS = {
    "force_vk_devicename",
}


def declare_options():
    import drirc_gen

    debug_options = []
    features_options = []
    misc_options = []

    drirc_gen.add_common_vk_options(debug_options, features_options, misc_options,
                                    valid_options=VALID_COMMON_VK_OPTIONS)

    return [
        drirc_gen.DrircSection("Debugging", debug_options, c_name="debug"),
    ]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-p', '--import-path', required=True)
    parser.add_argument('--drirc-src', required=True)
    parser.add_argument('--drirc-hdr', required=True)
    parser.add_argument('--validate', required=True)
    args = parser.parse_args()

    sys.path.insert(0, args.import_path)
    import drirc_gen

    drirc_gen.drirc_validate([args.validate], declare_options())
    drirc_gen.drirc_generate(args.drirc_src, args.drirc_hdr, "lvp", declare_options())


if __name__ == '__main__':
    main()
