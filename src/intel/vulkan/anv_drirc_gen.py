#!/usr/bin/env python3
# Copyright © 2026 Intel Corporation
# SPDX-License-Identifier: MIT

import argparse
import sys

VALID_COMMON_VK_OPTIONS = {
    "force_vk_vendor",
    "vk_lower_terminate_to_discard",
    "vk_require_astc",
}

def declare_options(android_version):
    import drirc_gen

    B = drirc_gen.DrircBool
    I = drirc_gen.DrircInt
    F = drirc_gen.DrircFloat
    U64 = drirc_gen.DrircUint64
    E = drirc_gen.DrircEnum
    EV = drirc_gen.DrircEnumValue

    debug_options = [
        # Workaround subgroups
        I("anv_assume_full_subgroups", 0, 0, 32,
          "Allow assuming full subgroups requirement even when it's not specified explicitly and set the given size",
          c_name="assume_full_subgroups"),
        B("anv_assume_full_subgroups_with_barrier", False,
          "Assume full subgroups requirement for compute shaders that use control barriers",
          c_name="assume_full_subgroups_with_barrier"),
        B("anv_assume_full_subgroups_with_shared_memory", False,
          "Allow assuming full subgroups requirement for shaders using shared memory even when it's not specified explicitly",
          c_name="assume_full_subgroups_with_shared_memory"),
        B("anv_brw_disable_subgroup_size_control", False,
          "Disable EXT_subgroup_size_control support when using brw compiler.",
          c_name="disable_subgroup_size_control"),
        B("anv_large_workgroup_non_coherent_image_workaround", False,
          "Fixup image coherency qualifier for certain shaders.",
          c_name="large_workgroup_non_coherent_image_workaround"),

        # Workaround various compiler related
        B("anv_emulate_read_without_format", android_version >= 35,
          "Emulate shaderStorageImageReadWithoutFormat with shader conversions",
          c_name="read_without_format_emu"),
        B("anv_sample_mask_out_opengl_behaviour", False,
          "Ignore sample mask out when having single sampled target",
          c_name="sample_mask_out_opengl_behaviour"),
        B("anv_disable_link_time_optimization", False,
          "Disable linking of graphics pipeline shaders",
          c_name="disable_lto"),
        F("lower_depth_range_rate", 1.0, 0.0, 1.0,
          "Lower depth range for fixing misrendering issues due to z coordinate float point interpolation accuracy",
          c_name="lower_depth_range_rate"),
        B("force_indirect_descriptors", False,
          "Use an indirection to access buffer/image/texture/sampler handles",
          c_name="force_indirect_descriptors"),
        B("limit_trig_input_range", False,
          "Limit trig input range to [-2p : 2p] to improve sin/cos calculation precision on Intel",
          c_name="limit_trig_input_range"),
        B("fp64_workaround_enabled", False,
          "Use softpf64 when the shader uses float64, but the device doesn't support that type",
          c_name="fp64_emu"),
        B("no_16bit", False,
          "Disable 16-bit instructions",
          c_name="no_16bit"),
        I("shader_spilling_rate", 11, 0, 100,
          "Speed up shader compilation by increasing number of spilled registers after ra_allocate failure",
          c_name="shader_spilling_rate"),
        B("anv_fs_sampler_undef_derivatives_workaround", False,
          "Fixes samplers in fragment shaders computing undefined values for derivatives with lanes disabled by control flow",
          c_name="fs_sampler_undef_derivatives_workaround"),

        # Workaround various driver
        B("always_flush_cache", False,
          "Enable flushing GPU caches with each draw call", c_name="always_flush_cache"),
        B("anv_force_filter_addr_rounding", False,
          "Force min/mag filter address rounding to be enabled even for NEAREST sampling",
          c_name="force_filter_addr_rounding"),
        B("anv_disable_fcv", False,
          "Disable FCV optimization",
          c_name="disable_fcv"),
        B("anv_enable_buffer_comp", False,
          "Enable CCS on buffers where possible",
          c_name="enable_buffer_comp"),
        B("anv_external_memory_implicit_sync", False,
          "Implicit sync on external BOs",
          c_name="external_memory_implicit_sync"),
        B("anv_fake_nonlocal_memory", False,
          "Present host-visible device-local memory types as non device-local",
          c_name="fake_nonlocal_mem"),
        B("anv_upper_bound_descriptor_pool_sampler", False,
          "Overallocate samplers in descriptor pools to workaround app bug",
          c_name="upper_bound_desc_pool_sampler"),
        B("anv_disable_drm_ccs_modifiers", False,
          "Disable DRM CCS modifier usage",
          c_name="disable_xe2_ccs_modifiers"),
        B("custom_border_colors_without_format", android_version == 0,
          "Enable custom border colors without format",
          c_name="custom_border_colors_without_format"),
        B("intel_sampler_route_to_lsc", False,
          "Specific toggle to enable sampler route to LSC",
          c_name="sampler_route_to_lsc"),
        B("intel_storage_cache_policy_wt", False,
          "Enable write-through cache policy for storage buffers/images",
          c_name="storage_l1_wt"),
        B("intel_tbimr", True,
          "Enable TBIMR tiled rendering",
          c_name="tbimr"),
        B("intel_te_distribution", True,
          "Enable tesselation distribution",
          c_name="te_distribution"),
        B("intel_vf_distribution", True,
          "Enable geometry distribution",
          c_name="vf_distribution"),
        B("anv_write_lookup_maps_unconditionally", False,
          "Unconditionally write lookup maps for BLAS update operation",
          c_name="write_lookup_maps_unconditionally"),
        B("anv_disable_hiz", False,
          "Disable HiZ (equivalent to INTEL_DEBUG=nohiz). Should only be used as a temporary solution",
          c_name="disable_hiz"),

        # Workaround command emission
        B("anv_barrier_post_untyped_clear_shader", False,
          "Insert pipeline barriers post clearing shader on untyped data",
          c_name="barrier_post_untyped_clear_shader"),
        B("anv_barrier_post_typed_clear_shader", False,
          "Insert pipeline barriers post clearing shader on typed data",
          c_name="barrier_post_typed_clear_shader"),
        B("intel_enable_wa_14018912822", False,
          "Workaround for using zero blend constants",
          c_name="wa_14018912822"),
        B("intel_enable_wa_14024015672_msaa", False,
          "Workaround for RHWO MSAA",
          c_name="wa_14024015672_msaa"),
    ]

    perf_options = [
        B("adaptive_sync", True,
          "Adapt the monitor sync to the application performance (when possible)",
          c_name="adaptive_sync"),
        I("generated_indirect_threshold", 4, 0, 0x7fffffff,
          "Indirect threshold count above which we start generating commands",
          c_name="generated_indirect_threshold"),
        I("generated_indirect_ring_threshold", 100, 0, 0x7fffffff,
          "Indirect threshold count above which we start generating commands in a ring buffer",
          c_name="generated_indirect_ring_threshold"),
        I("query_clear_with_blorp_threshold", 6, 0, 0x7fffffff,
          "Query threshold count above which query buffers are cleared with blorp",
          c_name="query_clear_with_blorp_threshold"),
        I("query_copy_with_shader_threshold", 6, 0, 0x7fffffff,
          "Query threshold count above which query copies are executed with a shader",
          c_name="query_copy_with_shader_threshold"),

        B("anv_disable_push_constant_alloc", True,
          "Disable push constant space allocations",
          c_name="disable_push_const_alloc"),
        I("anv_binding_table_block_size",
          4096, 1024, 128 * 1024,
          "Binding table block allocation size (3DSTATE_BINDING_TABLE_POOL_ALLOC)",
          c_name="bt_block_size"),
        B("anv_promote_cbv_to_push_buffers", False,
          "Promote CBV 64bit pointers in push constant data to push buffers",
          c_name="promote_cbv_push_buffer"),
        B("anv_state_cache_perf_fix", False,
          "Whether COMMON_SLICE_CHICKEN3 bit13 should be programmed to enable BTP+BTI RCC keying",
          c_name="state_cache_perf_fix"),
        B("anv_vf_component_packing", True,
          "Vertex fetching component packing",
          c_name="vf_comp_packing"),
        I("anv_enable_opt_divergent_atomics", 0, 0, 3,
          "Enable fusion of divergent atomics (see brw_divergent_atomics_flags)",
          c_name="opt_divergent_atomics"),
        I("anv_enable_opt_divergent_atomics_compute_only", 0, 0, 3,
          "Enable fusion of divergent atomics for compute shaders only (see brw_divergent_atomics_flags)",
          c_name="opt_divergent_atomics_compute_only"),
        B("intel_force_compute_surface_prefetch", True,
          "Enable binding table surface prefteching for compute shaders",
          c_name="cs_surface_prefetch"),
        B("intel_force_sampler_prefetch", False,
          "Enable binding table sampler prefteching",
          c_name="sampler_prefetch"),

        B("force_guc_low_latency", False,
          "Enable low latency GuC strategy.",
          c_name="guc_low_latency"),

        E("anv_stack_ids", 512, 256, 2048,
          [EV(256,  "256 stackids"),
           EV(512,  "512 stackids"),
           EV(1024, "1024 stackids"),
           EV(2048, "2048 stackids")],
          "Control the number stackIDs (i.e. number of unique rays in the RT subsytem)",
          c_name="stack_ids"),
        E("anv_rt_dispatch_timeout", 512, 64, 4096,
          [EV(64,    "64 clocks"),
           EV(128,   "128 clocks"),
           EV(192,   "192 clocks"),
           EV(256,   "256 clocks"),
           EV(384,   "384 clocks"),
           EV(512,   "512 clocks"),
           EV(640,   "640 clocks"),
           EV(768,   "768 clocks"),
           EV(896,   "896 clocks"),
           EV(1024,  "1024 clocks"),
           EV(1152,  "1152 clocks"),
           EV(1280,  "1280 clocks"),
           EV(1408,  "1408 clocks"),
           EV(1536,  "1536 clocks"),
           EV(1664,  "1664 clocks"),
           EV(1792,  "1792 clocks"),
           EV(1920,  "1920 clocks"),
           EV(2048,  "2048 clocks"),
           EV(4096,  "4096 clocks")],
          "Force BTD child dispatches if dispatches do not happen naturally for number of clocks equal to the programmed timeout counter",
          c_name="rt_dispatch_timeout"),
    ]

    feature_options = [
        B("fake_sparse", False,
          "Advertise support for sparse binding of textures regardless of real support",
          c_name="fake_sparse"),
        B("anv_enable_scratch_page", True,
          "Disables surface padding and suppresses all page faults, drops writes and returns zeros on reads.",
          c_name="scratch_page"),
        B("anv_enable_fully_covered", False,
          "Enable fullyCoveredFragmentShaderInputVariable (Alchemist and newer only).",
          c_name="fully_covered"),
        B("anv_fake_image_compression_control_xe2_plus", android_version >= 37,
          "Enable VK_EXT_image_compression_control with no actual effect",
          c_name= "fake_image_compression_control_xe2_plus"),
        B("compression_control_enabled", android_version >= 37,
          "Enable VK_EXT_image_compression_control support",
          c_name="compression_control_enabled"),
    ]

    misc_options = []

    drirc_gen.add_common_vk_options(debug_options, feature_options, misc_options,
                                    valid_options=VALID_COMMON_VK_OPTIONS,
                                    defaults={"vk_require_astc": android_version >= 34})
    drirc_gen.add_common_vk_wsi_options(debug_options, perf_options)

    return [drirc_gen.DrircSection("Debugging",   debug_options,   c_name="debug"),
            drirc_gen.DrircSection("Features",    feature_options, c_name="features"),
            drirc_gen.DrircSection("Performance", perf_options,    c_name="perf")]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-p', '--import-path', required=True)
    parser.add_argument('--drirc-src', required=True)
    parser.add_argument('--drirc-hdr', required=True)
    parser.add_argument('--android-ver', type=int, default=0, required=False)
    parser.add_argument('--validate', required=True)
    args = parser.parse_args()

    sys.path.insert(0, args.import_path)
    import drirc_gen

    options = declare_options(args.android_ver)

    drirc_gen.drirc_validate([args.validate], options)

    drirc_gen.drirc_generate(args.drirc_src, args.drirc_hdr, "anv", options)


if __name__ == '__main__':
    main()
