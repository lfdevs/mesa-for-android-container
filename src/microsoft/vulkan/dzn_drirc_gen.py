#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

import argparse
import sys


VALID_COMMON_VK_OPTIONS = {
    "force_vk_devicename",
}


def declare_options():
    import drirc_gen

    B = drirc_gen.DrircBool

    debug_options = [
        B("dzn_claim_wide_lines", False,
          "Claim wide line support",
          c_name="claim_wide_lines"),
        B("dzn_enable_8bit_loads_stores", False,
          "Enable VK_KHR_8bit_loads_stores",
          c_name="enable_8bit_loads_stores"),
        B("dzn_disable", False,
          "Fail instance creation",
          c_name="disable"),
    ]

    drirc_gen.add_common_vk_options(debug_options, [], [], valid_options=VALID_COMMON_VK_OPTIONS)

    return [drirc_gen.DrircSection("Debugging", debug_options, c_name="debug")]


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

    drirc_gen.drirc_generate(args.drirc_src, args.drirc_hdr, "dzn", options)


if __name__ == '__main__':
    main()
