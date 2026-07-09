#!/usr/bin/env python3
# Copyright 2019 Google LLC
# SPDX-License-Identifier: MIT

import argparse
import sys


VALID_COMMON_VK_OPTIONS = {
    "force_vk_devicename",
}


def declare_options():
    import drirc_gen

    B = drirc_gen.DrircBool

    debug_options = []

    performance_options = [
        B("venus_implicit_fencing", False,
          "Assume the virtio-gpu kernel driver supports implicit fencing",
          c_name="implicit_fencing"),
        B("venus_wsi_multi_plane_modifiers", False,
          "Enable support of multi-plane format modifiers for wsi images",
          c_name="enable_wsi_multi_plane_modifiers"),
    ]

    drirc_gen.add_common_vk_options(debug_options, [], [], valid_options=VALID_COMMON_VK_OPTIONS)
    drirc_gen.add_common_vk_wsi_options(debug_options, performance_options);

    return [
        drirc_gen.DrircSection("Performance", performance_options, c_name="performance"),
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

    options = declare_options()

    drirc_gen.drirc_validate([args.validate], options)

    drirc_gen.drirc_generate(args.drirc_src, args.drirc_hdr, "vn", options)


if __name__ == '__main__':
    main()
