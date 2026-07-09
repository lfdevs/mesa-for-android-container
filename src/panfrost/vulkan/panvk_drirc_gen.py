#!/usr/bin/env python3
# Copyright © 2021 Collabora Ltd.
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
    U64 = drirc_gen.DrircUint64

    UINT64_MAX = (1 << 64) - 1

    misc_options = [
        U64("pan_compute_core_mask", UINT64_MAX, 0, UINT64_MAX,
            "Bitmask of shader cores that may be used for compute jobs. "
            "If unset, defaults to scheduling across all available cores.",
            c_name="compute_core_mask"),
        U64("pan_fragment_core_mask", UINT64_MAX, 0, UINT64_MAX,
            "Bitmask of shader cores that may be used for fragment jobs. "
            "If unset, defaults to scheduling across all available cores.",
            c_name="fragment_core_mask"),
        B("pan_enable_vertex_pipeline_stores_atomics", False,
          "Enable vertexPipelineStoresAndAtomics on v13+ (This cannot work on older "
          "generation because of speculative behaviors around vertices)",
          c_name="enable_vertex_pipeline_stores_atomics"),
        B("pan_force_enable_shader_atomics", False,
          "Enable fragmentStoresAndAtomics and vertexPipelineStoresAndAtomics on any "
          "architecture. (This may not work reliably and is for debug purposes only!)",
          c_name="force_enable_shader_atomics"),
    ]

    debug_options = []
    features_options = []
    performance_options = []

    drirc_gen.add_common_vk_options(debug_options, features_options, misc_options,
                                    valid_options=VALID_COMMON_VK_OPTIONS)
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

    drirc_gen.drirc_generate(args.drirc_src, args.drirc_hdr, "panvk", options)


if __name__ == '__main__':
    main()
