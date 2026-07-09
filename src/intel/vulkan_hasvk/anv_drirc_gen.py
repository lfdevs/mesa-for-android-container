#!/usr/bin/env python3
# Copyright © 2026 Intel Corporation
# SPDX-License-Identifier: MIT

import argparse
import sys

VALID_COMMON_VK_OPTIONS = {
    "force_vk_devicename",
    "vk_lower_terminate_to_discard",
}

def declare_options():
    import drirc_gen

    B = drirc_gen.DrircBool
    I = drirc_gen.DrircInt
    F = drirc_gen.DrircFloat

    debug_options = [
        B("always_flush_cache", False,
          "Enable flushing GPU caches with each draw call",
          c_name="always_flush_cache"),
        B("limit_trig_input_range", False,
          "Limit trig input range to [-2p : 2p] to improve sin/cos calculation precision",
          c_name="limit_trig_input_range"),
    ]

    performance_options = [
        I("anv_assume_full_subgroups", 0, 0, 32,
          "Allow assuming full subgroups requirement even when it's not specified explicitly and set the given size",
          c_name="assume_full_subgroups"),
        B("anv_sample_mask_out_opengl_behaviour", False,
          "Ignore sample mask out when having single sampled target",
          c_name="sample_mask_out_opengl_behaviour"),
        B("no_16bit", False,
          "Disable 16-bit instructions",
          c_name="no_16bit"),
        B("hasvk_report_vk_1_3_version", False,
          "Report Vulkan 1.3 API version",
          c_name="report_vk_1_3_version"),
    ]

    quality_options = [
        F("lower_depth_range_rate", 1.0, 0.0, 1.0,
          "Lower depth range for fixing misrendering issues due to z coordinate float point interpolation accuracy",
          c_name="lower_depth_range_rate"),
    ]

    misc_options = []

    drirc_gen.add_common_vk_options(debug_options, [], misc_options,
                                    valid_options=VALID_COMMON_VK_OPTIONS)
    drirc_gen.add_common_vk_wsi_options(debug_options, performance_options);

    return [
        drirc_gen.DrircSection("Debugging", debug_options, c_name="debug"),
        drirc_gen.DrircSection("Performance", performance_options, c_name="performance"),
        drirc_gen.DrircSection("Quality", quality_options, c_name="quality"),
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

    drirc_gen.drirc_generate(args.drirc_src, args.drirc_hdr, "hasvk", options)


if __name__ == '__main__':
    main()
