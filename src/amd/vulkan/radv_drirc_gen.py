#!/usr/bin/env python3
# Copyright © 2026 Valve Corporation
# SPDX-License-Identifier: MIT

import argparse
import sys

def declare_options():
    from drirc_gen import DrircBool      as B
    from drirc_gen import DrircInt       as I
    from drirc_gen import DrircString    as S

    from drirc_gen import DrircSection   as Section

    debug_options = [
        # WSI options.
        B("vk_wsi_force_bgra8_unorm_first", False,
          "Force vkGetPhysicalDeviceSurfaceFormatsKHR to return VK_FORMAT_B8G8R8A8_UNORM as the first format"),
        B("vk_wsi_force_swapchain_to_current_extent", False,
          "Force VkSwapchainCreateInfoKHR::imageExtent to be VkSurfaceCapabilities2KHR::currentExtent"),
        B("vk_wsi_disable_unordered_submits", False,
          "Disable unordered WSI submits to workaround application synchronization bugs"),
        B("vk_x11_ignore_suboptimal", False,
          "Force the X11 WSI to never report VK_SUBOPTIMAL_KHR"),

        B("vk_zero_vram", False,
          "Initialize to zero all VRAM allocations",
          c_name="zero_vram"),
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
        B("radv_invariant_geom", False,
          "Mark geometry-affecting outputs as invariant",
          c_name="invariant_geom"),
        B("vk_lower_terminate_to_discard", False,
          "Lower terminate to discard (which is implicitly demote)",
          c_name="lower_terminate_to_discard"),
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
    ]

    performance_options = [
        # WSI options.
        B("adaptive_sync", True,
          "Adapt the monitor sync to the application performance (when possible)"),
        I("vk_x11_override_min_image_count", 0, 0, 999,
          "Override the VkSurfaceCapabilitiesKHR::minImageCount (0 = no override)"),
        B("vk_x11_strict_image_count", False,
          "Force the X11 WSI to create exactly the number of image specified by the application in VkSwapchainCreateInfoKHR::minImageCount"),
        B("vk_x11_ensure_min_image_count", False,
          "Force the X11 WSI to create at least the number of image specified by the driver in VkSurfaceCapabilitiesKHR::minImageCount"),
        B("vk_xwayland_wait_ready", False,
          "Wait for fences before submitting buffers to Xwayland"),

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
        B("radv_cooperative_matrix2_nv", False,
          "Expose VK_NV_cooperative_matrix2 on supported hardware.",
          c_name="cooperative_matrix2_nv"),
        B("radv_emulate_rt", False,
          "Expose RT extensions on GFX10 and below through software emulation.",
          c_name="emulate_rt"),
        B("radv_enable_float16_gfx8", False,
          "Expose float16 on GFX8, where it's supported but usually not beneficial.",
          c_name="enable_float16_gfx8"),
        B("vk_require_etc2", False,
          "Implement emulated ETC2 on HW that does not support it",
          c_name="require_etc2"),
        B("vk_require_astc", False,
          "Implement emulated ASTC on HW that does not support it",
          c_name="require_astc"),
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

    return [
        Section("Debugging", debug_options, c_name="debug"),
        Section("Performance", performance_options, c_name="performance"),
        Section("Features", features_options, c_name="features"),
        Section("Miscellaneous", misc_options, c_name="misc"),
    ]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-p', '--import-path', required=True)
    parser.add_argument('--drirc-src', required=True)
    parser.add_argument('--drirc-hdr', required=True)
    parser.add_argument('--validate', required=True)
    args = parser.parse_args()
    sys.path.insert(0, args.import_path)

    from drirc_gen import drirc_validate
    drirc_validate([args.validate], declare_options())

    from drirc_gen import drirc_generate
    drirc_generate(args.drirc_src, args.drirc_hdr, "radv", declare_options())

if __name__ == '__main__':
    main()
