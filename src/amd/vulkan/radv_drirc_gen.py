#!/usr/bin/env python3
# Copyright © 2026 Valve Corporation
# SPDX-License-Identifier: MIT

import argparse
import sys

VALID_COMMON_VK_OPTIONS = {
    "force_vk_devicename",
    "vk_lower_terminate_to_discard",
    "vk_zero_vram",
    "vk_require_etc2",
    "vk_require_astc",
}

def declare_options():
    import drirc_gen

    B = drirc_gen.DrircBool
    I = drirc_gen.DrircInt
    S = drirc_gen.DrircString

    debug_options = [
        B("radv_disable_aniso_single_level", False,
          "Disable anisotropic filtering for single level images",
          c_name="disable_aniso_single_level"),
        B("radv_disable_dcc", False,
          "Disable DCC for color images on GFX8-GFX11.5",
          c_name="disable_dcc"),
        B("radv_disable_dcc_mips", False,
          "Disable DCC for color images with mips on GFX8-GFX11.5",
          c_name="disable_dcc_mips"),
        B("radv_disable_dcc_stores", False,
          "Disable DCC for color storage images on GFX10-GFX11.5",
          c_name="disable_dcc_stores"),
        B("radv_disable_shrink_image_store", False,
          "Disabling shrinking of image stores based on the format",
          c_name="disable_shrink_image_store"),
        B("radv_disable_sinking_load_input_fs", False,
          "Disable sinking load inputs for fragment shaders",
          c_name="disable_sinking_load_input_fs"),
        B("radv_disable_tc_compat_htile_general", False,
          "Disable TC-compat HTILE in GENERAL layout",
          c_name="disable_tc_compat_htile_general"),
        B("radv_disable_trunc_coord", False,
          "Disable TRUNC_COORD to use D3D10/11/12 point sampling behaviour. This has special behaviour for DXVK.",
          c_name="disable_trunc_coord"),
        B("radv_enable_mrt_output_nan_fixup", False,
          "Replace NaN outputs from fragment shaders with zeroes for floating point render target",
          c_name="enable_mrt_output_nan_fixup"),
        B("radv_flush_before_query_copy", False,
          "Wait for timestamps to be written before a query copy command",
          c_name="flush_before_query_copy"),
        B("radv_flush_before_timestamp_write", False,
          "Wait for previous commands to finish before writing timestamps",
          c_name="flush_before_timestamp_write"),
        B("radv_no_dynamic_bounds", False,
          "Disabling bounds checking for dynamic buffer descriptors",
          c_name="no_dynamic_bounds"),
        B("radv_split_fma", False,
          "Split application-provided fused multiply-add in geometry stages",
          c_name="split_fma"),
        B("radv_ssbo_non_uniform", False,
          "Always mark SSBO operations as non-uniform.",
          c_name="ssbo_non_uniform"),
        B("radv_tex_non_uniform", False,
          "Always mark texture sample operations as non-uniform.",
          c_name="tex_non_uniform"),
        B("radv_wait_for_vm_map_updates", False,
          "Wait for VM MAP updates at allocation time to mitigate use-before-alloc",
          c_name="wait_for_vm_map_updates"),
        B("radv_no_implicit_varying_subgroup_size", False,
          "Do not assume VK_PIPELINE_SHADER_STAGE_CREATE_ALLOW_VARYING_SUBGROUP_SIZE for SPIR-V 1.6.",
          c_name="no_implicit_varying_subgroup_size"),
        B("radv_rt_wave64", False,
          "Force wave64 in RT shaders",
          c_name="rt_wave64"),
        B("radv_hide_rebar_on_dgpu", False,
          "Hide resizable bar on dGPUs by exposing a fake carveout of 256MiB.",
          c_name="hide_rebar_on_dgpu"),
        S("radv_app_layer",
          description="Select an application layer.",
          c_name="app_layer"),
        I("radv_override_uniform_offset_alignment", 0, 0, 128,
          "Override the minUniformBufferOffsetAlignment exposed to the application. (0 = default)",
          c_name="override_uniform_offset_alignment"),
        B("radv_force_64_byte_sampled_image", False,
          "Force sampled images size to 64 bytes.",
          c_name="force_64_byte_sampled_image"),
        B("radv_force_nan_preserve_min_max", False,
          "Treat FMax/FMin/FClamp like NMax/NMin/NClamp.",
          c_name="force_nan_preserve_min_max"),
    ]

    performance_options = [
        B("radv_disable_ngg_gs", False,
          "Disable NGG GS on GFX10/GFX10.3.",
          c_name="disable_ngg_gs"),
        B("radv_enable_unified_heap_on_apu", False,
          "Enable an unified heap with DEVICE_LOCAL on integrated GPUs",
          c_name="enable_unified_heap_on_apu"),
        B("radv_report_llvm9_version_string", False,
          "Report LLVM 9.0.1 for games that apply shader workarounds if missing (for ACO only)",
          c_name="report_llvm9_version_string"),
        B("radv_prefer_2d_swizzle_for_3d_storage", False,
          "Prefer 2D swizzle mode for 3D storage images.",
          c_name="prefer_2d_swizzle_for_3d_storage"),
        S("radv_gfx12_hiz_wa",
          description="Choose the specific HiZ workaround to apply on GFX12 (RDNA4). Accepted values are: disabled, partial or full",
          c_name="gfx12_hiz_wa"),
    ]

    features_options = [
        B("radv_device_coherent_memory", False,
          "Expose VK_AMD_device_coherent_memory on GFX12 (RDNA4).",
          c_name="device_coherent_memory"),
        B("radv_cooperative_matrix2_nv", False,
          "Expose VK_NV_cooperative_matrix2 on supported hardware.",
          c_name="cooperative_matrix2_nv"),
        B("radv_emulate_rt", False,
          "Expose RT extensions on GFX10 and below through software emulation.",
          c_name="emulate_rt"),
        B("radv_enable_float16_gfx8", False,
          "Expose float16 on GFX8, where it's supported but usually not beneficial.",
          c_name="enable_float16_gfx8"),
    ]

    misc_options = [
        B("radv_clear_lds", False,
          "Clear LDS at the end of shaders. Might decrease performance.",
          c_name="clear_lds"),
        I("override_vram_size", -1, -1, 2147483647,
          "Override the VRAM size advertised to the application in MiB (-1 = default)",
          c_name="override_vram_size"),

        # Overrides for forcing re-compilation of pipelines when
        # RADV_BUILD_ID_OVERRIDE is enabled. These need to be bumped every
        # time a compiler bugfix is backported (up to 8 shader versions are
        # supported).
        I("radv_override_graphics_shader_version", 0, 0, 7,
          "Override the shader version of graphics pipelines to force re-compilation. (0 = default)",
          c_name="override_graphics_shader_version"),
        I("radv_override_compute_shader_version", 0, 0, 7,
          "Override the shader version of compute pipelines to force re-compilation. (0 = default)",
          c_name="override_compute_shader_version"),
        I("radv_override_ray_tracing_shader_version", 0, 0, 7,
          "Override the shader version of ray tracing pipelines to force re-compilation. (0 = default)",
          c_name="override_ray_tracing_shader_version"),
    ]

    drirc_gen.add_common_vk_options(debug_options, features_options, misc_options,
                                    valid_options=VALID_COMMON_VK_OPTIONS)
    drirc_gen.add_common_vk_wsi_options(debug_options, performance_options)

    return [
        drirc_gen.DrircSection("Debugging", debug_options, c_name="debug"),
        drirc_gen.DrircSection("Performance", performance_options, c_name="performance"),
        drirc_gen.DrircSection("Features", features_options, c_name="features"),
        drirc_gen.DrircSection("Miscellaneous", misc_options, c_name="misc"),
    ]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-p', '--import-path', required=True)
    parser.add_argument('--drirc-src')
    parser.add_argument('--drirc-hdr')
    parser.add_argument('--rst')
    parser.add_argument('--validate', required=True)
    args = parser.parse_args()

    if (args.drirc_src is None) != (args.drirc_hdr is None):
        parser.error("`--drirc-src` and `--drirc-hdr` can only be used together")

    sys.path.insert(0, args.import_path)
    import drirc_gen

    drirc_gen.drirc_validate([args.validate], declare_options())

    if args.drirc_src and args.drirc_hdr:
        drirc_gen.drirc_generate(args.drirc_src, args.drirc_hdr, "radv", declare_options())
    if args.rst:
        drirc_gen.drirc_generate_rst(args.rst, declare_options())

if __name__ == '__main__':
    main()
