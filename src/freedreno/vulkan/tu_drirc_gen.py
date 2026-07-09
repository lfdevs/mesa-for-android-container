#!/usr/bin/env python3
# Copyright © 2026 Intel Corporation
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
    I = drirc_gen.DrircInt
    E = drirc_gen.DrircEnum
    F = drirc_gen.DrircFloat
    S = drirc_gen.DrircString
    EV = drirc_gen.DrircEnumValue

    debug_options = [
        B("tu_dont_care_as_load", False,
          "Treat VK_ATTACHMENT_LOAD_OP_DONT_CARE as LOAD_OP_LOAD, workaround on tiler GPUs for games that confuse these two load ops",
          c_name="dont_care_as_load")
    ]

    perf_options = [
        B("tu_allow_concurrent_binning", False,
          "Allow concurrent binning on A7XX+, the CB is disabled by default because it regresses performance on desktop games",
          c_name="allow_concurrent_binning"),
        S("tu_autotune_algorithm",
          description="Set the preferred autotune algorithm",
          c_name="autotune_algo"),
    ]

    misc_options = [
        # Conservative LRZ (default true) invalidates LRZ on draws with
        # blend and depth-write enabled, because this can lead to incorrect
        # rendering.  Driconf can be used to disable conservative LRZ for
        # games which do not have the problematic sequence of draws *and*
        # suffer a performance loss with conservative LRZ.
        B("disable_conservative_lrz", False,
          "Disable conservative LRZ",
          c_name="disable_conservative_lrz"),

        B("tu_dont_reserve_descriptor_set", False,
          "Don't internally reserve one of the HW descriptor sets for descriptor set dynamic offset support, this frees up an extra descriptor set at the cost of that feature",
          c_name="dont_reserve_descriptor_set"),

        # Allow out of bounds UBO access by disabling lowering of UBO loads for
        # indirect access, which rely on the UBO bounds specified in the shader,
        # rather than the bound UBO size which isn't known until draw time.
        #
        # See: https://github.com/doitsujin/dxvk/issues/3861
        B("tu_allow_oob_indirect_ubo_loads", False,
          "Some D3D11 games rely on out-of-bounds indirect UBO loads to return real values from underlying bound descriptor, this prevents us from lowering indirectly accessed UBOs to consts",
          c_name="allow_oob_indirect_ubo_loads"),

        # The hardware doesn't support Vulkan's stencil swizzling rules for
        # custom border colors. Vulkan requires stencil to be sampled as the red
        # component, but hardware samples it as the green component. Without
        # customBorderColorWithoutFormat we can work around this issue without
        # perf loss, but with customBorderColorWithoutFormat we have to disable
        # UBWC for D24S8 images with USAGE_SAMPLED set.
        # However, VkPhysicalDeviceMaintenance5Properties.depthStencilSwizzleOneSupport
        # forbids this state combination when false. It was added after the HW
        # deficiency was discovered, and we want to work around apps that aren't
        # aware of this.
        B("tu_enable_d24s8_border_color_workaround", False,
          "Disable UBWC for D24S8 images with VK_IMAGE_USAGE_SAMPLED_BIT when customBorderColorWithoutFormat is enabled",
          c_name="enable_d24s8_border_color_workaround"),

        # When D24S8 is used without enable_d24s8_border_color_workaround, the
        # fast border color HW feature results in an incorrect color being used.
        # However, we want to enable fast border colors for apps that are known
        # not to use border colors with D24S8, such as DXVK and vkd3d-proton.
        B("tu_enable_fast_border_color_for_undefined_formats", False,
          "Enables fast border color HW feature for VK_FORMAT_UNDEFINED sampler formats.",
          c_name="enable_fast_border_color_for_undefined_formats"),

        B("tu_use_tex_coord_round_nearest_even_mode", False,
          "Use D3D-compliant round-to-nearest-even mode for texture coordinates",
          c_name="use_tex_coord_round_nearest_even_mode"),
        B("tu_ignore_frag_depth_direction", False,
          "Ignore direction specified for gl_FragDepth output",
          c_name="ignore_frag_depth_direction"),
        B("tu_enable_softfloat32", False,
          "Enable software emulation of float32 denorms, which is required for D3D12 SM6.2 support but is not encouraged for native Vulkan apps",
          c_name="enable_softfloat32"),
        B("tu_emulate_alpha_to_coverage", False,
          "Enable emulation of alpha-to-coverage in the shader, which provides improved visual quality for many games at the cost of some performance",
          c_name="emulate_alpha_to_coverage"),
        B("tu_override_uncached_as_cache_coherent", False,
          "Replaces uncached-host allocations with cached-coherent-host when possible. Only useful under x86 emulation where memory accesses tend to be atomic",
          c_name="override_uncached_as_cache_coherent"),
        B("tu_restrict_subgroup_size_64", False,
          "Restrict subgroup size to 64 (instead of a max of 128) to work around games assuming desktop GPU 32/64 sizes",
          c_name="restrict_subgroup_size_64"),
        B("tu_emulate_second_queue", False,
          "Provide a second queue for applications that require it, like the Android framework",
          c_name="emulate_second_queue"),

        I("tu_override_graphics_shader_version", 0, 0, 255,
          "Override graphics shader version to force recompilation when TU_BUILD_ID_OVERRIDE is enabled.",
          c_name="override_graphics_shader_version"),
        I("tu_override_compute_shader_version", 0, 0, 255,
          "Override compute shader version to force recompilation when TU_BUILD_ID_OVERRIDE is enabled.",
          c_name="override_compute_shader_version"),

        B("tu_enable_texel_buffer_emulation", False,
          "Emulate texel buffers to allow a higher limit for elements that is in line with what some D3D12 games expect",
          c_name="enable_texel_buffer_emulation"),
        B("tu_enable_ssbo_emulation", False,
          "Emulate SSBOs to allow a higher limit for elements that is in line with what some D3D12 games expect",
          c_name="enable_ssbo_emulation"),
    ]

    features_options = []

    drirc_gen.add_common_vk_options(debug_options, features_options, misc_options, valid_options=VALID_COMMON_VK_OPTIONS)
    drirc_gen.add_common_vk_wsi_options(debug_options, perf_options)

    return [drirc_gen.DrircSection("Debugging", debug_options,   c_name="debug"),
            drirc_gen.DrircSection(
                "Miscellaneous", misc_options, c_name="misc"),
            drirc_gen.DrircSection("Performance",   perf_options,    c_name="perf")]


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

    drirc_gen.drirc_generate(args.drirc_src, args.drirc_hdr, "turnip", options)


if __name__ == '__main__':
    main()
