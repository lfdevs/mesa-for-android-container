#!/usr/bin/env python3
# Copyright © 2026 Valve Corporation
# SPDX-License-Identifier: MIT

import argparse
import sys

VALID_COMMON_VK_OPTIONS = {
    "force_vk_devicename",
    "force_vk_vendor",
    "vk_zero_vram",
    "heap_memory_percent",
}

def declare_options():
    import drirc_gen

    B = drirc_gen.DrircBool
    I = drirc_gen.DrircInt
    S = drirc_gen.DrircString

    debug_options = [
        S("nvk_app_layer",
          description="Select an application layer",
          c_name="app_layer"),
    ]

    performance_options = []
    features_options = []
    misc_options = [
        # Overrides for forcing re-compilation of pipelines when
        # NVK_BUILD_ID_OVERRIDE is enabled. These need to be bumped every
        # time a compiler bugfix is backported (up to 256 shader versions are
        # supported).
        I("nvk_override_graphics_shader_version", 0, 0, 255,
          "Override graphics shader version to force recompilation when NVK_BUILD_ID_OVERRIDE is enabled.",
          c_name="override_graphics_shader_version"),
        I("nvk_override_compute_shader_version", 0, 0, 255,
          "Override compute shader version to force recompilation when NVK_BUILD_ID_OVERRIDE is enabled.",
          c_name="override_compute_shader_version"),

        B("nvk_ssbo_align_4b", False, c_name="ssbo_align_4b"),
    ]

    drirc_gen.add_common_vk_options(debug_options, features_options, misc_options,
                                    valid_options=VALID_COMMON_VK_OPTIONS,
                                    defaults={"heap_memory_percent": 0.75})
    drirc_gen.add_common_vk_wsi_options(debug_options, performance_options)

    return [
        drirc_gen.DrircSection("Debugging", debug_options, c_name="debug"),
        drirc_gen.DrircSection("Performance", performance_options, c_name="performance"),
        drirc_gen.DrircSection("Misc", misc_options, c_name="misc"),
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
    drirc_gen.drirc_generate(args.drirc_src, args.drirc_hdr, "nvk", declare_options())

if __name__ == '__main__':
    main()
