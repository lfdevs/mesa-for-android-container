#!/usr/bin/env python3
# Copyright 2024 Valve Corporation
# Copyright 2024 Alyssa Rosenzweig
# SPDX-License-Identifier: MIT

import argparse
import sys

VALID_COMMON_VK_OPTIONS = {
    "force_vk_devicename",
    "force_vk_vendor",
    "heap_memory_percent",
}

def declare_options():
    import drirc_gen

    B = drirc_gen.DrircBool

    misc_options = [
        B("hk_disable_border_emulation", False,
          "Disable custom border colour emulation",
          c_name="disable_border_emulation"),
        B("hk_fake_minmax", False,
          "Fake support for min/max filtering",
          c_name="fake_minmax"),
        B("hk_image_view_min_lod", False,
          "Emulate VK_EXT_image_view_min_lod (conformant but slower)",
          c_name="image_view_min_lod"),
        B("hk_enable_vertex_pipeline_stores_atomics", False,
          "Enable vertexPipelineStoresAndAtomics",
          c_name="enable_vertex_pipeline_stores_atomics"),
    ]

    debug_options = []
    features_options = []
    performance_options = []

    drirc_gen.add_common_vk_options(debug_options, features_options, misc_options,
                                    valid_options=VALID_COMMON_VK_OPTIONS,
                                    # Use 1/2 of total size to avoid swapping
                                    defaults={"heap_memory_percent": 0.5})
    drirc_gen.add_common_vk_wsi_options(debug_options, performance_options)

    return [
        drirc_gen.DrircSection("Debugging", debug_options, c_name="debug"),
        drirc_gen.DrircSection("Performance", performance_options, c_name="performance"),
        drirc_gen.DrircSection("Miscellaneous", misc_options, c_name="misc"),
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

    drirc_gen.drirc_generate(args.drirc_src, args.drirc_hdr, "hk", options)


if __name__ == '__main__':
    main()
