/*
 * XML DRI client-side driver configuration
 * Copyright (C) 2003 Felix Kuehling
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * FELIX KUEHLING, OR ANY OTHER CONTRIBUTORS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */
/**
 * \file driconf.h
 * \brief Pool of common options
 * \author Felix Kuehling
 *
 * This file defines macros that can be used to construct
 * driConfigOptions in the drivers.
 */

#ifndef __DRICONF_H
#define __DRICONF_H

#include "xmlconfig.h"

/*
 * generic macros
 */

/** \brief Names a section of related options to follow */
#define DRI_CONF_SECTION(text) { .desc = text, .info = { .type = DRI_SECTION } },
#define DRI_CONF_SECTION_END

/** \brief End an option description */
#define DRI_CONF_OPT_END },

/** \brief A verbal description (empty version) */
#define DRI_CONF_DESC(text) .desc = text,

/** \brief A verbal description of an enum value */
#define DRI_CONF_ENUM(_value,text) { .value = _value, .desc = text },

#define DRI_CONF_RANGE_I(min, max)              \
      .range = {                                \
         .start = { ._int = min },              \
         .end = { ._int = max },                \
      }                                         \

#define DRI_CONF_RANGE_U64(min, max)            \
      .range = {                                \
         .start = { ._uint64 = min },           \
         .end = { ._uint64 = max },             \
      }

#define DRI_CONF_RANGE_F(min, max)              \
      .range = {                                \
         .start = { ._float = min },            \
         .end = { ._float = max },              \
      }                                         \

/**
 * \brief A boolean option definition, with the default value passed in as a
 * string
 */

#define DRI_CONF_OPT_B(_name, def, _desc) {                     \
      .desc = _desc,                                            \
      .info = {                                                 \
         .name = #_name,                                        \
         .type = DRI_BOOL,                                      \
      },                                                        \
      .value = { ._bool = def },                                \
   },

#define DRI_CONF_OPT_I(_name, def, min, max, _desc) {           \
      .desc = _desc,                                            \
      .info = {                                                 \
         .name = #_name,                                        \
         .type = DRI_INT,                                       \
         DRI_CONF_RANGE_I(min, max),                            \
      },                                                        \
      .value = { ._int = def },                                 \
   },

#define DRI_CONF_OPT_U64(_name, def, min, max, _desc) {         \
      .desc = _desc,                                            \
      .info = {                                                 \
         .name = #_name,                                        \
         .type = DRI_UINT64,                                    \
         DRI_CONF_RANGE_U64(min, max),                          \
      },                                                        \
      .value = { ._uint64 = def },                                 \
   },

#define DRI_CONF_OPT_F(_name, def, min, max, _desc) {           \
      .desc = _desc,                                            \
      .info = {                                                 \
         .name = #_name,                                        \
         .type = DRI_FLOAT,                                     \
         DRI_CONF_RANGE_F(min, max),                            \
      },                                                        \
      .value = { ._float = def },                               \
   },

#define DRI_CONF_OPT_E(_name, def, min, max, _desc, values) {   \
      .desc = _desc,                                            \
      .info = {                                                 \
         .name = #_name,                                        \
         .type = DRI_ENUM,                                      \
         DRI_CONF_RANGE_I(min, max),                            \
      },                                                        \
      .value = { ._int = def },                                 \
      .enums = { values },                                      \
   },

#define DRI_CONF_OPT_S(_name, def, _desc) {                     \
      .desc = _desc,                                            \
      .info = {                                                 \
         .name = #_name,                                        \
         .type = DRI_STRING,                                    \
      },                                                        \
      .value = { ._string = #def },                             \
   },

#define DRI_CONF_OPT_S_NODEF(_name, _desc) {                    \
      .desc = _desc,                                            \
      .info = {                                                 \
         .name = #_name,                                        \
         .type = DRI_STRING,                                    \
      },                                                        \
      .value = { ._string = "" },                               \
   },

/**
 * \brief Debugging options
 */
#define DRI_CONF_SECTION_DEBUG DRI_CONF_SECTION("Debugging")

#define DRI_CONF_ALWAYS_FLUSH_BATCH(def) \
   DRI_CONF_OPT_B(always_flush_batch, def,                              \
                  "Enable flushing batchbuffer after each draw call")

#define DRI_CONF_ALWAYS_FLUSH_CACHE(def) \
   DRI_CONF_OPT_B(always_flush_cache, def, \
                  "Enable flushing GPU caches with each draw call")

#define DRI_CONF_DISABLE_THROTTLING(def) \
   DRI_CONF_OPT_B(disable_throttling, def, \
                  "Disable throttling on first batch after flush")

#define DRI_CONF_FORCE_GLSL_EXTENSIONS_WARN(def) \
   DRI_CONF_OPT_B(force_glsl_extensions_warn, def, \
                  "Force GLSL extension default behavior to 'warn'")

#define DRI_CONF_DISABLE_BLEND_FUNC_EXTENDED(def) \
   DRI_CONF_OPT_B(disable_blend_func_extended, def, \
                  "Disable dual source blending")

#define DRI_CONF_DISABLE_ARB_GPU_SHADER5(def) \
   DRI_CONF_OPT_B(disable_arb_gpu_shader5, def, \
                  "Disable GL_ARB_gpu_shader5")

#define DRI_CONF_DUAL_COLOR_BLEND_BY_LOCATION(def) \
   DRI_CONF_OPT_B(dual_color_blend_by_location, def, \
                  "Identify dual color blending sources by location rather than index")

#define DRI_CONF_DISABLE_GLSL_LINE_CONTINUATIONS(def) \
   DRI_CONF_OPT_B(disable_glsl_line_continuations, def, \
                  "Disable backslash-based line continuations in GLSL source")

#define DRI_CONF_DISABLE_UNIFORM_ARRAY_RESIZE(def) \
   DRI_CONF_OPT_B(disable_uniform_array_resize, def, \
                  "Disable the glsl optimisation that resizes uniform arrays")

#define DRI_CONF_ALIAS_SHADER_EXTENSION() \
   DRI_CONF_OPT_S_NODEF(alias_shader_extension, "Allow  alias for shader extensions")

#define DRI_CONF_ALLOW_VERTEX_TEXTURE_BIAS(def) \
   DRI_CONF_OPT_B(allow_vertex_texture_bias, def, \
                  "Allow GL2 vertex shaders to have access to texture2D/textureCube with bias variants")

#define DRI_CONF_FORCE_GLSL_VERSION(def) \
   DRI_CONF_OPT_I(force_glsl_version, def, 0, 999, \
                  "Force a default GLSL version for shaders that lack an explicit #version line")

#define DRI_CONF_ALLOW_EXTRA_PP_TOKENS(def) \
   DRI_CONF_OPT_B(allow_extra_pp_tokens, def, \
                  "Allow extra tokens at end of preprocessor directives.")

#define DRI_CONF_ALLOW_GLSL_EXTENSION_DIRECTIVE_MIDSHADER(def) \
   DRI_CONF_OPT_B(allow_glsl_extension_directive_midshader, def, \
                  "Allow GLSL #extension directives in the middle of shaders")

#define DRI_CONF_ALLOW_GLSL_120_SUBSET_IN_110(def) \
   DRI_CONF_OPT_B(allow_glsl_120_subset_in_110, def, \
                  "Allow a subset of GLSL 1.20 in GLSL 1.10 as needed by SPECviewperf13")

#define DRI_CONF_ALLOW_GLSL_BUILTIN_CONST_EXPRESSION(def) \
   DRI_CONF_OPT_B(allow_glsl_builtin_const_expression, def, \
                  "Allow builtins as part of constant expressions")

#define DRI_CONF_ALLOW_GLSL_RELAXED_ES(def) \
   DRI_CONF_OPT_B(allow_glsl_relaxed_es, def, \
                  "Allow some relaxation of GLSL ES shader restrictions")

#define DRI_CONF_ALLOW_GLSL_BUILTIN_VARIABLE_REDECLARATION(def) \
   DRI_CONF_OPT_B(allow_glsl_builtin_variable_redeclaration, def, \
                  "Allow GLSL built-in variables to be redeclared verbatim")

#define DRI_CONF_ALLOW_HIGHER_COMPAT_VERSION(def) \
   DRI_CONF_OPT_B(allow_higher_compat_version, def, \
                  "Allow a higher compat profile (version 3.1+) for apps that request it")

#define DRI_CONF_ALLOW_GLSL_COMPAT_SHADERS(def) \
   DRI_CONF_OPT_B(allow_glsl_compat_shaders, def, \
                  "Allow in GLSL: #version xxx compatibility")

#define DRI_CONF_FORCE_GLSL_ABS_SQRT(def) \
   DRI_CONF_OPT_B(force_glsl_abs_sqrt, def,                             \
                  "Force computing the absolute value for sqrt() and inversesqrt()")

#define DRI_CONF_GLSL_CORRECT_DERIVATIVES_AFTER_DISCARD(def) \
   DRI_CONF_OPT_B(glsl_correct_derivatives_after_discard, def, \
                  "Implicit and explicit derivatives after a discard behave as if the discard didn't happen")

#define DRI_CONF_GLSL_IGNORE_WRITE_TO_READONLY_VAR(def) \
   DRI_CONF_OPT_B(glsl_ignore_write_to_readonly_var, def, \
                  "Forces the GLSL compiler to ignore writes to readonly vars rather than throwing an error")

#define DRI_CONF_ALLOW_GLSL_CROSS_STAGE_INTERPOLATION_MISMATCH(def) \
   DRI_CONF_OPT_B(allow_glsl_cross_stage_interpolation_mismatch, def,   \
                  "Allow interpolation qualifier mismatch across shader stages")

#define DRI_CONF_DO_DCE_BEFORE_CLIP_CULL_ANALYSIS(def) \
   DRI_CONF_OPT_B(do_dce_before_clip_cull_analysis, def,   \
                  "Use dead code elimitation before checking for invalid Clip*/CullDistance variables usage.")

#define DRI_CONF_ALLOW_DRAW_OUT_OF_ORDER(def) \
   DRI_CONF_OPT_B(allow_draw_out_of_order, def, \
                  "Allow out-of-order draw optimizations. Set when Z fighting doesn't have to be accurate.")

#define DRI_CONF_GLTHREAD_NOP_CHECK_FRAMEBUFFER_STATUS(def) \
   DRI_CONF_OPT_B(glthread_nop_check_framebuffer_status, def, \
                  "glthread always returns GL_FRAMEBUFFER_COMPLETE to prevent synchronization.")

#define DRI_CONF_FORCE_GL_VENDOR() \
   DRI_CONF_OPT_S_NODEF(force_gl_vendor, "Override GPU vendor string.")

#define DRI_CONF_FORCE_GL_RENDERER() \
   DRI_CONF_OPT_S_NODEF(force_gl_renderer, "Override GPU renderer string.")

#define DRI_CONF_FORCE_COMPAT_PROFILE(def) \
   DRI_CONF_OPT_B(force_compat_profile, def, \
                  "Force an OpenGL compatibility context")

#define DRI_CONF_FORCE_COMPAT_SHADERS(def) \
   DRI_CONF_OPT_B(force_compat_shaders, def, \
                  "Force OpenGL compatibility shaders")

#define DRI_CONF_FORCE_DIRECT_GLX_CONTEXT(def) \
   DRI_CONF_OPT_B(force_direct_glx_context, def, \
                  "Force direct GLX context (even if indirect is requested)")

#define DRI_CONF_ALLOW_INVALID_GLX_DESTROY_WINDOW(def) \
   DRI_CONF_OPT_B(allow_invalid_glx_destroy_window, def, \
                  "Allow passing an invalid window into glXDestroyWindow")

#define DRI_CONF_KEEP_NATIVE_WINDOW_GLX_DRAWABLE(def) \
   DRI_CONF_OPT_B(keep_native_window_glx_drawable, def, \
                  "Keep GLX drawable created from native window when switch context")

#define DRI_CONF_OVERRIDE_VRAM_SIZE() \
   DRI_CONF_OPT_I(override_vram_size, -1, -1, 2147483647, \
                  "Override the VRAM size advertised to the application in MiB (-1 = default)")

#define DRI_CONF_FORCE_GL_MAP_BUFFER_SYNCHRONIZED(def) \
   DRI_CONF_OPT_B(force_gl_map_buffer_synchronized, def, "Override GL_MAP_UNSYNCHRONIZED_BIT.")

#define DRI_CONF_FORCE_GL_DEPTH_COMPONENT_TYPE_INT(def) \
   DRI_CONF_OPT_B(force_gl_depth_component_type_int, def, "Override GL_DEPTH_COMPONENT type from unsigned short to unsigned int")

#define DRI_CONF_TRANSCODE_ETC(def) \
   DRI_CONF_OPT_B(transcode_etc, def, "Transcode ETC formats to DXTC if unsupported")

#define DRI_CONF_TRANSCODE_ASTC(def) \
   DRI_CONF_OPT_B(transcode_astc, def, "Transcode ASTC formats to DXTC if unsupported")

#define DRI_CONF_ALLOW_COMPRESSED_FALLBACK(def) \
   DRI_CONF_OPT_B(allow_compressed_fallback, def, "Allow fallback to uncompressed formats for unsupported compressed formats")

#define DRI_CONF_MESA_EXTENSION_OVERRIDE() \
   DRI_CONF_OPT_S_NODEF(mesa_extension_override, \
                  "Allow enabling/disabling a list of extensions")

#define DRI_CONF_GLX_EXTENSION_OVERRIDE() \
   DRI_CONF_OPT_S_NODEF(glx_extension_override, \
                  "Allow enabling/disabling a list of GLX extensions")

#define DRI_CONF_INDIRECT_GL_EXTENSION_OVERRIDE() \
   DRI_CONF_OPT_S_NODEF(indirect_gl_extension_override, \
                  "Allow enabling/disabling a list of indirect-GL extensions")

#define DRI_CONF_FORCE_PROTECTED_CONTENT_CHECK(def) \
   DRI_CONF_OPT_B(force_protected_content_check, def, \
                  "Reject image import if protected_content attribute doesn't match")

#define DRI_CONF_IGNORE_MAP_UNSYNCHRONIZED(def) \
   DRI_CONF_OPT_B(ignore_map_unsynchronized, def, \
                  "Ignore GL_MAP_UNSYNCHRONIZED_BIT, workaround for games that use it incorrectly")

#define DRI_CONF_VK_DONT_CARE_AS_LOAD(def) \
   DRI_CONF_OPT_B(vk_dont_care_as_load, def, \
                  "Treat VK_ATTACHMENT_LOAD_OP_DONT_CARE as LOAD_OP_LOAD, workaround on tiler GPUs for games that confuse these two load ops")

#define DRI_CONF_VK_LOWER_TERMINATE_TO_DISCARD(def) \
   DRI_CONF_OPT_B(vk_lower_terminate_to_discard, def, \
                  "Lower terminate to discard (which is implicitly demote)")

#define DRI_CONF_LIMIT_TRIG_INPUT_RANGE(def) \
   DRI_CONF_OPT_B(limit_trig_input_range, def, \
                  "Limit trig input range to [-2p : 2p] to improve sin/cos calculation precision on Intel")

#define DRI_CONF_NO_16BIT(def) \
   DRI_CONF_OPT_B(no_16bit, def, \
                  "Disable 16-bit instructions")

#define DRI_CONF_IGNORE_DISCARD_FRAMEBUFFER(def) \
   DRI_CONF_OPT_B(ignore_discard_framebuffer, def, \
                  "Ignore glDiscardFramebuffer/glInvalidateFramebuffer, workaround for games that use it incorrectly")

#define DRI_CONF_FORCE_VK_VENDOR() \
   DRI_CONF_OPT_I(force_vk_vendor, 0, -1, 2147483647, "Override GPU vendor id")

#define DRI_CONF_FAKE_SPARSE(def) \
   DRI_CONF_OPT_B(fake_sparse, def, \
                  "Advertise support for sparse binding of textures regardless of real support")

#define DRI_CONFIG_INTEL_TBIMR(def) \
   DRI_CONF_OPT_B(intel_tbimr, def, "Enable TBIMR tiled rendering")

#define DRI_CONFIG_INTEL_VF_DISTRIBUTION(def) \
   DRI_CONF_OPT_B(intel_vf_distribution, def, "Enable geometry distribution")

#define DRI_CONFIG_INTEL_TE_DISTRIBUTION(def) \
   DRI_CONF_OPT_B(intel_te_distribution, def, "Enable tesselation distribution")

#define DRI_CONFIG_INTEL_STORAGE_CACHE_POLICY_WT(def) \
   DRI_CONF_OPT_B(intel_storage_cache_policy_wt, def, "Enable write-through cache policy for storage buffers/images.")

#define DRI_CONF_INTEL_ENABLE_WA_14018912822(def) \
   DRI_CONF_OPT_B(intel_enable_wa_14018912822, def, \
                  "Intel workaround for using zero blend constants")

#define DRI_CONF_INTEL_SAMPLER_ROUTE_TO_LSC(def) \
   DRI_CONF_OPT_B(intel_sampler_route_to_lsc, def, \
                  "Intel specific toggle to enable sampler route to LSC")

#define DRI_CONF_INTEL_DISABLE_THREADED_CONTEXT(def) \
   DRI_CONF_OPT_B(intel_disable_threaded_context, def, "Disable threaded context")

#define DRI_CONF_VK_REQUIRE_ETC2(def) \
  DRI_CONF_OPT_B(vk_require_etc2, def, \
                 "Implement emulated ETC2 on HW that does not support it")

#define DRI_CONF_VK_REQUIRE_ASTC(def) \
   DRI_CONF_OPT_B(vk_require_astc, def, \
                  "Implement emulated ASTC on HW that does not support it")

/**
 * \brief Image quality-related options
 */
#define DRI_CONF_SECTION_QUALITY DRI_CONF_SECTION("Image Quality")

#define DRI_CONF_PRECISE_TRIG(def) \
   DRI_CONF_OPT_B(precise_trig, def, \
                  "Prefer accuracy over performance in trig functions")

#define DRI_CONF_PP_CELSHADE(def) \
   DRI_CONF_OPT_E(pp_celshade, def, 0, 1, \
                  "A post-processing filter to cel-shade the output", \
                  { 0 } )

#define DRI_CONF_PP_NORED(def) \
   DRI_CONF_OPT_E(pp_nored, def, 0, 1, \
                  "A post-processing filter to remove the red channel", \
                  { 0 } )

#define DRI_CONF_PP_NOGREEN(def) \
   DRI_CONF_OPT_E(pp_nogreen, def, 0, 1, \
                  "A post-processing filter to remove the green channel", \
                  { 0 } )

#define DRI_CONF_PP_NOBLUE(def) \
   DRI_CONF_OPT_E(pp_noblue, def, 0, 1, \
                  "A post-processing filter to remove the blue channel", \
                  { 0 } )

#define DRI_CONF_PP_JIMENEZMLAA(def,min,max) \
   DRI_CONF_OPT_I(pp_jimenezmlaa, def, min, max, \
                  "Morphological anti-aliasing based on Jimenez' MLAA. 0 to disable, 8 for default quality")

#define DRI_CONF_PP_JIMENEZMLAA_COLOR(def,min,max) \
   DRI_CONF_OPT_I(pp_jimenezmlaa_color, def, min, max, \
                  "Morphological anti-aliasing based on Jimenez' MLAA. 0 to disable, 8 for default quality. Color version, usable with 2d GL apps")

#define DRI_CONF_PP_LOWER_DEPTH_RANGE_RATE() \
   DRI_CONF_OPT_F(lower_depth_range_rate, 1.0, 0.0, 1.0, \
                  "Lower depth range for fixing misrendering issues due to z coordinate float point interpolation accuracy")

/**
 * \brief Performance-related options
 */
#define DRI_CONF_SECTION_PERFORMANCE DRI_CONF_SECTION("Performance")

#define DRI_CONF_VBLANK_NEVER 0
#define DRI_CONF_VBLANK_DEF_INTERVAL_0 1
#define DRI_CONF_VBLANK_DEF_INTERVAL_1 2
#define DRI_CONF_VBLANK_ALWAYS_SYNC 3
#define DRI_CONF_VBLANK_MODE(def) \
   DRI_CONF_OPT_E(vblank_mode, def, 0, 3, \
                  "Synchronization with vertical refresh (swap intervals)", \
                  DRI_CONF_ENUM(0,"Never synchronize with vertical refresh, ignore application's choice") \
                  DRI_CONF_ENUM(1,"Initial swap interval 0, obey application's choice") \
                  DRI_CONF_ENUM(2,"Initial swap interval 1, obey application's choice") \
                  DRI_CONF_ENUM(3,"Always synchronize with vertical refresh, application chooses the minimum swap interval"))

#define DRI_CONF_ADAPTIVE_SYNC(def) \
   DRI_CONF_OPT_B(adaptive_sync,def, \
                  "Adapt the monitor sync to the application performance (when possible)")

#define DRI_CONF_BLOCK_ON_DEPLETED_BUFFERS(def) \
   DRI_CONF_OPT_B(block_on_depleted_buffers, def, \
                  "Block clients using buffer backpressure until new buffer is available to reduce latency")

#define DRI_CONF_VK_WSI_FORCE_BGRA8_UNORM_FIRST(def) \
   DRI_CONF_OPT_B(vk_wsi_force_bgra8_unorm_first, def, \
                  "Force vkGetPhysicalDeviceSurfaceFormatsKHR to return VK_FORMAT_B8G8R8A8_UNORM as the first format")

#define DRI_CONF_VK_WSI_FORCE_SWAPCHAIN_TO_CURRENT_EXTENT(def) \
   DRI_CONF_OPT_B(vk_wsi_force_swapchain_to_current_extent, def, \
                  "Force VkSwapchainCreateInfoKHR::imageExtent to be VkSurfaceCapabilities2KHR::currentExtent")

#define DRI_CONF_VK_WSI_DISABLE_UNORDERED_SUBMITS(def) \
   DRI_CONF_OPT_B(vk_wsi_disable_unordered_submits, def, \
                  "Disable unordered WSI submits to workaround application synchronization bugs")

#define DRI_CONF_VK_X11_OVERRIDE_MIN_IMAGE_COUNT(def) \
   DRI_CONF_OPT_I(vk_x11_override_min_image_count, def, 0, 999, \
                  "Override the VkSurfaceCapabilitiesKHR::minImageCount (0 = no override)")

#define DRI_CONF_VK_X11_STRICT_IMAGE_COUNT(def) \
   DRI_CONF_OPT_B(vk_x11_strict_image_count, def, \
                  "Force the X11 WSI to create exactly the number of image specified by the application in VkSwapchainCreateInfoKHR::minImageCount")

#define DRI_CONF_VK_X11_ENSURE_MIN_IMAGE_COUNT(def) \
   DRI_CONF_OPT_B(vk_x11_ensure_min_image_count, def, \
                  "Force the X11 WSI to create at least the number of image specified by the driver in VkSurfaceCapabilitiesKHR::minImageCount")

#define DRI_CONF_VK_X11_IGNORE_SUBOPTIMAL(def) \
   DRI_CONF_OPT_B(vk_x11_ignore_suboptimal, def, \
                  "Force the X11 WSI to never report VK_SUBOPTIMAL_KHR")

#define DRI_CONF_VK_XWAYLAND_WAIT_READY(def) \
   DRI_CONF_OPT_B(vk_xwayland_wait_ready, def, \
                  "Wait for fences before submitting buffers to Xwayland")

#define DRI_CONF_MESA_GLTHREAD_DRIVER(def) \
   DRI_CONF_OPT_B(mesa_glthread_driver, def, \
                  "Enable offloading GL driver work to a separate thread")

#define DRI_CONF_MESA_NO_ERROR(def) \
   DRI_CONF_OPT_B(mesa_no_error, def, \
                  "Disable GL driver error checking")

#define DRI_CONF_SHADER_SPILLING_RATE(def) \
   DRI_CONF_OPT_I(shader_spilling_rate, def, 0, 100, \
                  "Speed up shader compilation by increasing number of spilled registers after ra_allocate failure")
/**
 * \brief Miscellaneous configuration options
 */
#define DRI_CONF_SECTION_MISCELLANEOUS DRI_CONF_SECTION("Miscellaneous")

#define DRI_CONF_ALWAYS_HAVE_DEPTH_BUFFER(def) \
   DRI_CONF_OPT_B(always_have_depth_buffer, def, \
                  "Create all visuals with a depth buffer")

#define DRI_CONF_GLSL_ZERO_INIT(def) \
   DRI_CONF_OPT_B(glsl_zero_init, def, \
                  "Force uninitialized variables to default to zero")

#define DRI_CONF_VS_POSITION_ALWAYS_INVARIANT(def) \
   DRI_CONF_OPT_B(vs_position_always_invariant, def, \
                  "Force the vertex shader's gl_Position output to be considered 'invariant'")

#define DRI_CONF_VS_POSITION_ALWAYS_PRECISE(def) \
   DRI_CONF_OPT_B(vs_position_always_precise, def, \
                  "Force the vertex shader's gl_Position output to be considered 'precise'")

#define DRI_CONF_ALLOW_RGB10_CONFIGS(def) \
   DRI_CONF_OPT_B(allow_rgb10_configs, def, \
                  "Allow exposure of visuals and fbconfigs with rgb10a2 formats")

#define DRI_CONF_ALLOW_RGB565_CONFIGS(def) \
   DRI_CONF_OPT_B(allow_rgb565_configs, def, \
                  "Allow exposure of visuals and fbconfigs with rgb565 formats")

#define DRI_CONF_FORCE_INTEGER_TEX_NEAREST(def) \
   DRI_CONF_OPT_B(force_integer_tex_nearest, def, \
                  "Force integer textures to use nearest filtering")

/* The GL spec does not allow this but wine has translation bug:
   https://bugs.winehq.org/show_bug.cgi?id=54787
*/
#define DRI_CONF_ALLOW_MULTISAMPLED_COPYTEXIMAGE(def) \
   DRI_CONF_OPT_B(allow_multisampled_copyteximage, def, \
                  "Allow CopyTexSubImage and other to copy sampled framebuffer")

#define DRI_CONF_VERTEX_PROGRAM_DEFAULT_OUT(def) \
   DRI_CONF_OPT_B(vertex_program_default_out, def, \
                  "Initialize outputs of vertex program to a default value vec4(0, 0, 0, 1)")

#define DRI_CONF_CUSTOM_BORDER_COLORS_WITHOUT_FORMAT(def) \
   DRI_CONF_OPT_B(custom_border_colors_without_format, def, \
                  "Enable custom border colors without format")

#define DRI_CONF_NO_FP16(def) \
   DRI_CONF_OPT_B(no_fp16, def, \
                  "Disable 16-bit float support")

#define DRI_CONF_VK_ZERO_VRAM(def) \
   DRI_CONF_OPT_B(vk_zero_vram, def, \
                  "Initialize to zero all VRAM allocations")

/**
 * \brief Initialization configuration options
 */
#define DRI_CONF_SECTION_INITIALIZATION DRI_CONF_SECTION("Initialization")

#define DRI_CONF_DEVICE_ID_PATH_TAG() \
   DRI_CONF_OPT_S_NODEF(device_id, "Define the graphic device to use if possible")

#define DRI_CONF_DRI_DRIVER() \
   DRI_CONF_OPT_S_NODEF(dri_driver, "Override the DRI driver to load")

#define DRI_CONF_V3D_NONMSAA_TEXTURE_SIZE_LIMIT(def) \
   DRI_CONF_OPT_B(v3d_nonmsaa_texture_size_limit, def, \
                  "Report the non-MSAA-only texture size limit")

/**
 * \brief wgl specific configuration options
 */

#define DRI_CONF_WGL_FRAME_LATENCY(def) \
   DRI_CONF_OPT_I(wgl_frame_latency, def, 1, 16, \
                  "Override default maximum frame latency")

#define DRI_CONF_WGL_SWAP_INTERVAL(def) \
   DRI_CONF_OPT_I(wgl_swap_interval, def, 1, 4, \
                  "Override default swap interval")

/**
 * \brief virgl specific configuration options
 */

#define DRI_CONF_GLES_EMULATE_BGRA(def) \
   DRI_CONF_OPT_B(gles_emulate_bgra, def, \
                  "On GLES emulate BGRA formats by using a swizzled RGBA format")

#define DRI_CONF_GLES_APPLY_BGRA_DEST_SWIZZLE(def) \
   DRI_CONF_OPT_B(gles_apply_bgra_dest_swizzle, def, \
                  "When the BGRA formats are emulated by using swizzled RGBA formats on GLES apply the swizzle when writing")

#define DRI_CONF_GLES_SAMPLES_PASSED_VALUE(def, minimum, maximum) \
   DRI_CONF_OPT_I(gles_samples_passed_value, def, minimum, maximum, \
                  "GL_SAMPLES_PASSED value when emulated by GL_ANY_SAMPLES_PASSED")

#define DRI_CONF_FORMAT_L8_SRGB_ENABLE_READBACK(def) \
   DRI_CONF_OPT_B(format_l8_srgb_enable_readback, def, \
                  "Force-enable reading back L8_SRGB textures")

#define DRI_CONF_VIRGL_SHADER_SYNC(def) \
   DRI_CONF_OPT_B(virgl_shader_sync, def, \
                  "Make shader compilation synchronous")

/**
 * \brief freedreno specific configuration options
 */

#define DRI_CONF_DISABLE_CONSERVATIVE_LRZ(def) \
   DRI_CONF_OPT_B(disable_conservative_lrz, def, \
                  "Disable conservative LRZ")

#define DRI_CONF_DISABLE_EXPLICIT_SYNC_HEURISTIC(def) \
   DRI_CONF_OPT_B(disable_explicit_sync_heuristic, def, \
                  "Disable Explicit-sync heuristic")

/**
 * \brief panfrost specific configuration options
 */

#define DRI_CONF_PAN_COMPUTE_CORE_MASK(def) \
   DRI_CONF_OPT_U64(pan_compute_core_mask, def, 0, UINT64_MAX, \
                    "Bitmask of shader cores that may be used for compute jobs. If unset, defaults to scheduling across all available cores.")

#define DRI_CONF_PAN_FRAGMENT_CORE_MASK(def) \
   DRI_CONF_OPT_U64(pan_fragment_core_mask, def, 0, UINT64_MAX, \
                    "Bitmask of shader cores that may be used for fragment jobs. If unset, defaults to scheduling across all available cores.")

#define DRI_CONF_PAN_ENABLE_VERTEX_PIPELINE_STORES_ATOMICS(def) \
   DRI_CONF_OPT_B(pan_enable_vertex_pipeline_stores_atomics, def, \
                  "Enable vertexPipelineStoresAndAtomics on v13+ (This cannot work on older generation because of speculative behaviors around vertices)")

#define DRI_CONF_PAN_FORCE_ENABLE_SHADER_ATOMICS(def) \
   DRI_CONF_OPT_B(pan_force_enable_shader_atomics, def, \
                  "Enable fragmentStoresAndAtomics and vertexPipelineStoresAndAtomics on any architecture. (This may not work reliably and is for debug purposes only!)")

/**
 * \brief Turnip specific configuration options
 */

#define DRI_CONF_TU_DONT_RESERVE_DESCRIPTOR_SET(def) \
   DRI_CONF_OPT_B(tu_dont_reserve_descriptor_set, def, \
                  "Don't internally reserve one of the HW descriptor sets for descriptor set dynamic offset support, this frees up an extra descriptor set at the cost of that feature")

#define DRI_CONF_TU_ALLOW_OOB_INDIRECT_UBO_LOADS(def) \
   DRI_CONF_OPT_B(tu_allow_oob_indirect_ubo_loads, def, \
                  "Some D3D11 games rely on out-of-bounds indirect UBO loads to return real values from underlying bound descriptor, this prevents us from lowering indirectly accessed UBOs to consts")

#define DRI_CONF_TU_DISABLE_D24S8_BORDER_COLOR_WORKAROUND(def) \
   DRI_CONF_OPT_B(tu_disable_d24s8_border_color_workaround, def, \
                  "Use UBWC for D24S8 images with VK_IMAGE_USAGE_SAMPLED_BIT when customBorderColorWithoutFormat is enabled")

#define DRI_CONF_TU_USE_TEX_COORD_ROUND_NEAREST_EVEN_MODE(def) \
   DRI_CONF_OPT_B(tu_use_tex_coord_round_nearest_even_mode, def, \
                  "Use D3D-compliant round-to-nearest-even mode for texture coordinates")

#define DRI_CONF_TU_IGNORE_FRAG_DEPTH_DIRECTION(def) \
   DRI_CONF_OPT_B(tu_ignore_frag_depth_direction, def, \
                  "Ignore direction specified for gl_FragDepth output")

#define DRI_CONF_TU_ENABLE_SOFTFLOAT32(def) \
   DRI_CONF_OPT_B(tu_enable_softfloat32, def, \
                  "Enable softfloat emulation for float32 denormals")

/**
 * \brief Honeykrisp specific configuration options
 */

#define DRI_CONF_HK_DISABLE_BORDER_EMULATION(def) \
   DRI_CONF_OPT_B(hk_disable_border_emulation, def, \
                  "Disable custom border colour emulation")

#define DRI_CONF_HK_FAKE_MINMAX(def) \
   DRI_CONF_OPT_B(hk_fake_minmax, def, \
                  "Fake support for min/max filtering")

#define DRI_CONF_HK_IMAGE_VIEW_MIN_LOD(def) \
   DRI_CONF_OPT_B(hk_image_view_min_lod, def, \
                  "Emulate VK_EXT_image_view_min_lod (conformant but slower)")

/**
 * \brief venus specific configuration options
 */
#define DRI_CONF_VENUS_IMPLICIT_FENCING(def) \
   DRI_CONF_OPT_B(venus_implicit_fencing, def, \
                  "Assume the virtio-gpu kernel driver supports implicit fencing")

#define DRI_CONF_VENUS_WSI_MULTI_PLANE_MODIFIERS(def) \
   DRI_CONF_OPT_B(venus_wsi_multi_plane_modifiers, def, \
                  "Enable support of multi-plane format modifiers for wsi images")

/**
 * \brief RADV specific configuration options
 */

#define DRI_CONF_RADV_REPORT_LLVM9_VERSION_STRING(def) \
   DRI_CONF_OPT_B(radv_report_llvm9_version_string, def, \
                  "Report LLVM 9.0.1 for games that apply shader workarounds if missing (for ACO only)")

#define DRI_CONF_RADV_ENABLE_MRT_OUTPUT_NAN_FIXUP(def) \
   DRI_CONF_OPT_B(radv_enable_mrt_output_nan_fixup, def, \
                  "Replace NaN outputs from fragment shaders with zeroes for floating point render target")

#define DRI_CONF_RADV_NO_DYNAMIC_BOUNDS(def) \
   DRI_CONF_OPT_B(radv_no_dynamic_bounds, def, \
                  "Disabling bounds checking for dynamic buffer descriptors")

#define DRI_CONF_RADV_DISABLE_SHRINK_IMAGE_STORE(def) \
   DRI_CONF_OPT_B(radv_disable_shrink_image_store, def, \
                  "Disabling shrinking of image stores based on the format")

#define DRI_CONF_RADV_OVERRIDE_UNIFORM_OFFSET_ALIGNMENT(def) \
   DRI_CONF_OPT_I(radv_override_uniform_offset_alignment, def, 0, 128, \
                  "Override the minUniformBufferOffsetAlignment exposed to the application. (0 = default)")

#define DRI_CONF_RADV_ZERO_VRAM(def) \
   DRI_CONF_OPT_B(radv_zero_vram, def, \
                  "Initialize to zero all VRAM allocations")

#define DRI_CONF_RADV_INVARIANT_GEOM(def) \
   DRI_CONF_OPT_B(radv_invariant_geom, def, \
                  "Mark geometry-affecting outputs as invariant")

#define DRI_CONF_RADV_SPLIT_FMA(def) \
   DRI_CONF_OPT_B(radv_split_fma, def, \
                  "Split application-provided fused multiply-add in geometry stages")

#define DRI_CONF_RADV_DISABLE_TC_COMPAT_HTILE_GENERAL(def) \
   DRI_CONF_OPT_B(radv_disable_tc_compat_htile_general, def, \
                  "Disable TC-compat HTILE in GENERAL layout")

#define DRI_CONF_RADV_DISABLE_DCC(def) \
   DRI_CONF_OPT_B(radv_disable_dcc, def, \
                  "Disable DCC for color images on GFX8-GFX11.5")

#define DRI_CONF_RADV_DISABLE_DCC_MIPS(def) \
   DRI_CONF_OPT_B(radv_disable_dcc_mips, def, \
                  "Disable DCC for color images with mips on GFX8-GFX11.5")

#define DRI_CONF_RADV_DISABLE_DCC_STORES(def) \
   DRI_CONF_OPT_B(radv_disable_dcc_stores, def, \
                  "Disable DCC for color storage images on GFX10-GFX11.5")

#define DRI_CONF_RADV_DISABLE_ANISO_SINGLE_LEVEL(def) \
  DRI_CONF_OPT_B(radv_disable_aniso_single_level, def, \
                 "Disable anisotropic filtering for single level images")

#define DRI_CONF_RADV_DISABLE_TRUNC_COORD(def) \
  DRI_CONF_OPT_B(radv_disable_trunc_coord, def, \
                 "Disable TRUNC_COORD to use D3D10/11/12 point sampling behaviour. This has special behaviour for DXVK.")

#define DRI_CONF_RADV_DISABLE_SINKING_LOAD_INPUT_FS(def) \
   DRI_CONF_OPT_B(radv_disable_sinking_load_input_fs, def, \
                  "Disable sinking load inputs for fragment shaders")

#define DRI_CONF_RADV_DISABLE_DEPTH_STORAGE(def) \
  DRI_CONF_OPT_B(radv_disable_depth_storage, def, \
                 "Hides support for storage access to depth formats")

#define DRI_CONF_RADV_FLUSH_BEFORE_QUERY_COPY(def) \
  DRI_CONF_OPT_B( \
      radv_flush_before_query_copy, def, \
      "Wait for timestamps to be written before a query copy command")

#define DRI_CONF_RADV_ENABLE_UNIFIED_HEAP_ON_APU(def) \
   DRI_CONF_OPT_B(radv_enable_unified_heap_on_apu, def, \
                  "Enable an unified heap with DEVICE_LOCAL on integrated GPUs")

#define DRI_CONF_RADV_TEX_NON_UNIFORM(def) \
   DRI_CONF_OPT_B(radv_tex_non_uniform, def, \
                  "Always mark texture sample operations as non-uniform.")

#define DRI_CONF_RADV_SSBO_NON_UNIFORM(def) \
   DRI_CONF_OPT_B(radv_ssbo_non_uniform, def, \
                  "Always mark SSBO operations as non-uniform.")

#define DRI_CONF_RADV_FLUSH_BEFORE_TIMESTAMP_WRITE(def) \
   DRI_CONF_OPT_B(radv_flush_before_timestamp_write, def, \
                  "Wait for previous commands to finish before writing timestamps")

#define DRI_CONF_RADV_RT_WAVE64(def) \
   DRI_CONF_OPT_B(radv_rt_wave64, def, \
                  "Force wave64 in RT shaders")

#define DRI_CONF_RADV_WAIT_FOR_VM_MAP_UPDATES(def) \
   DRI_CONF_OPT_B(radv_wait_for_vm_map_updates, def, \
                  "Wait for VM MAP updates at allocation time to mitigate use-before-alloc")

#define DRI_CONF_RADV_NO_IMPLICIT_VARYING_SUBGROUP_SIZE(def) \
   DRI_CONF_OPT_B(radv_no_implicit_varying_subgroup_size, def, \
                  "Do not assume VK_PIPELINE_SHADER_STAGE_CREATE_ALLOW_VARYING_SUBGROUP_SIZE for SPIR-V 1.6.")

/**
 * Overrides for forcing re-compilation of pipelines when RADV_BUILD_ID_OVERRIDE is enabled.
 * These need to be bumped every time a compiler bugfix is backported (up to 8 shader
 * versions are supported).
 */
#define DRI_CONF_RADV_OVERRIDE_GRAPHICS_SHADER_VERSION(def) \
   DRI_CONF_OPT_I(radv_override_graphics_shader_version, def, 0, 7, \
                  "Override the shader version of graphics pipelines to force re-compilation. (0 = default)")

#define DRI_CONF_RADV_OVERRIDE_COMPUTE_SHADER_VERSION(def) \
   DRI_CONF_OPT_I(radv_override_compute_shader_version, def, 0, 7, \
                  "Override the shader version of compute pipelines to force re-compilation. (0 = default)")

#define DRI_CONF_RADV_OVERRIDE_RAY_TRACING_SHADER_VERSION(def) \
   DRI_CONF_OPT_I(radv_override_ray_tracing_shader_version, def, 0, 7, \
                  "Override the shader version of ray tracing pipelines to force re-compilation. (0 = default)")

#define DRI_CONF_RADV_APP_LAYER() DRI_CONF_OPT_S_NODEF(radv_app_layer, "Select an application layer.")

#define DRI_CONF_RADV_CLEAR_LDS(def) \
   DRI_CONF_OPT_B(radv_clear_lds, def, "Clear LDS at the end of shaders. Might decrease performance.")

#define DRI_CONF_RADV_DISABLE_NGG_GS(def) \
   DRI_CONF_OPT_B(radv_disable_ngg_gs, def, "Disable NGG GS on GFX10/GFX10.3.")

#define DRI_CONF_RADV_EMULATE_RT(def) \
   DRI_CONF_OPT_B(radv_emulate_rt, def, \
                  "Expose RT extensions on GFX10 and below through software emulation.")

#define DRI_CONF_RADV_ENABLE_FLOAT16_GFX8(def) \
   DRI_CONF_OPT_B(radv_enable_float16_gfx8, def, \
                  "Expose float16 on GFX8, where it's supported but usually not beneficial.")

#define DRI_CONF_RADV_COOPERATIVE_MATRIX2_NV(def) \
   DRI_CONF_OPT_B(radv_cooperative_matrix2_nv, def, \
                  "Expose VK_NV_cooperative_matrix2 on supported hardware.")

#define DRI_CONF_RADV_GFX12_HIZ_WA() \
   DRI_CONF_OPT_S_NODEF(radv_gfx12_hiz_wa, \
                        "Choose the specific HiZ workaround to apply on GFX12 (RDNA4). " \
                        "Accepted values are: disabled, partial or full")

#define DRI_CONF_RADV_HIDE_REBAR_ON_DGPU(def) \
   DRI_CONF_OPT_B(radv_hide_rebar_on_dgpu, def, \
                  "Hide resizable bar on dGPUs by exposing a fake carveout of 256MiB.")

/**
 * \brief ANV specific configuration options
 */

#define DRI_CONF_ANV_ASSUME_FULL_SUBGROUPS(def) \
   DRI_CONF_OPT_I(anv_assume_full_subgroups, def, 0, 32, \
                  "Allow assuming full subgroups requirement even when it's not specified explicitly and set the given size")

#define DRI_CONF_ANV_ASSUME_FULL_SUBGROUPS_WITH_BARRIER(def) \
   DRI_CONF_OPT_B(anv_assume_full_subgroups_with_barrier, def, \
                  "Assume full subgroups requirement for compute shaders that use control barriers")

#define DRI_CONF_ANV_ASSUME_FULL_SUBGROUPS_WITH_SHARED_MEMORY(def) \
   DRI_CONF_OPT_B(anv_assume_full_subgroups_with_shared_memory, def, \
                  "Allow assuming full subgroups requirement for shaders using shared memory even when it's not specified explicitly")

#define DRI_CONF_ANV_EMULATE_READ_WITHOUT_FORMAT(def) \
   DRI_CONF_OPT_B(anv_emulate_read_without_format, def, \
                  "Emulate shaderStorageImageReadWithoutFormat with shader conversions")

#define DRI_CONF_ANV_SAMPLE_MASK_OUT_OPENGL_BEHAVIOUR(def) \
   DRI_CONF_OPT_B(anv_sample_mask_out_opengl_behaviour, def, \
                  "Ignore sample mask out when having single sampled target")

#define DRI_CONF_ANV_FORCE_FILTER_ADDR_ROUNDING(def) \
   DRI_CONF_OPT_B(anv_force_filter_addr_rounding, def, \
                  "Force min/mag filter address rounding to be enabled even for NEAREST sampling")

#define DRI_CONF_ANV_FP64_WORKAROUND_ENABLED(def) \
   DRI_CONF_OPT_B(fp64_workaround_enabled, def, \
                  "Use softpf64 when the shader uses float64, but the device doesn't support that type")

#define DRI_CONF_ANV_GENERATED_INDIRECT_THRESHOLD(def) \
   DRI_CONF_OPT_I(generated_indirect_threshold, def, 0, INT32_MAX, \
                  "Indirect threshold count above which we start generating commands")

#define DRI_CONF_ANV_GENERATED_INDIRECT_RING_THRESHOLD(def) \
   DRI_CONF_OPT_I(generated_indirect_ring_threshold, def, 0, INT32_MAX, \
                  "Indirect threshold count above which we start generating commands in a ring buffer")

#define DRI_CONF_ANV_QUERY_CLEAR_WITH_BLORP_THRESHOLD(def) \
   DRI_CONF_OPT_I(query_clear_with_blorp_threshold, def, 0, INT32_MAX, \
                  "Query threshold count above which query buffers are cleared with blorp")

#define DRI_CONF_ANV_QUERY_COPY_WITH_SHADER_THRESHOLD(def) \
   DRI_CONF_OPT_I(query_copy_with_shader_threshold, def, 0, INT32_MAX, \
                  "Query threshold count above which query copies are executed with a shader")

#define DRI_CONF_ANV_FORCE_INDIRECT_DESCRIPTORS(def) \
   DRI_CONF_OPT_B(force_indirect_descriptors, def, \
                  "Use an indirection to access buffer/image/texture/sampler handles")

#define DRI_CONF_ANV_DISABLE_FCV(def) \
   DRI_CONF_OPT_B(anv_disable_fcv, def, \
                  "Disable FCV optimization")

#define DRI_CONF_ANV_ENABLE_BUFFER_COMP(def) \
   DRI_CONF_OPT_B(anv_enable_buffer_comp, def, \
                  "Enable CCS on buffers where possible")

#define DRI_CONF_ANV_EXTERNAL_MEMORY_IMPLICIT_SYNC(def) \
   DRI_CONF_OPT_B(anv_external_memory_implicit_sync, def, "Implicit sync on external BOs")

#define DRI_CONF_ANV_COMPRESSION_CONTROL_ENABLED(def) \
   DRI_CONF_OPT_B(compression_control_enabled, def, "Enable VK_EXT_image_compression_control support")

#define DRI_CONF_ANV_FAKE_NONLOCAL_MEMORY(def) \
   DRI_CONF_OPT_B(anv_fake_nonlocal_memory, def, \
                  "Present host-visible device-local memory types as non device-local")

#define DRI_CONF_ANV_UPPER_BOUND_DESCRIPTOR_POOL_SAMPLER(def) \
   DRI_CONF_OPT_B(anv_upper_bound_descriptor_pool_sampler, def, \
                  "Overallocate samplers in descriptor pools to workaround app bug")

#define DRI_CONF_ANV_VF_COMPONENT_PACKING(def) \
   DRI_CONF_OPT_B(anv_vf_component_packing, def, \
                  "Vertex fetching component packing")

#define DRI_CONF_ANV_LARGE_WORKGROUP_NON_COHERENT_IMAGE_WORKAROUND(def) \
   DRI_CONF_OPT_B(anv_large_workgroup_non_coherent_image_workaround, def, \
                  "Fixup image coherency qualifier for certain shaders.")

#define DRI_CONF_ANV_FORCE_GUC_LOW_LATENCY(def) \
   DRI_CONF_OPT_B(force_guc_low_latency, def, \
                  "Enable low latency GuC strategy.")

/**
 * \brief HASVK specific configuration options
 */

#define DRI_CONF_HASVK_OVERRIDE_API_VERSION(def) \
   DRI_CONF_OPT_B(hasvk_report_vk_1_3_version, def, \
                  "Override intel_hasvk API version")

/**
 * \brief DZN specific configuration options
 */

#define DRI_CONF_DZN_CLAIM_WIDE_LINES(def) \
   DRI_CONF_OPT_B(dzn_claim_wide_lines, def, "Claim wide line support")

#define DRI_CONF_DZN_ENABLE_8BIT_LOADS_STORES(def) \
   DRI_CONF_OPT_B(dzn_enable_8bit_loads_stores, def, "Enable VK_KHR_8bit_loads_stores")

#define DRI_CONF_DZN_DISABLE(def) \
   DRI_CONF_OPT_B(dzn_disable, def, "Fail instance creation")

#endif
