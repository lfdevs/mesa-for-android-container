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

#define DRI_CONF_ALLOW_GLSL_EMBEDDED_STRUCTURE_DECLARATIONS(def) \
   DRI_CONF_OPT_B(allow_glsl_embedded_structure_declarations, def, \
                  "Allow embedded structure declarations again in GLSL 1.20+")

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

#define DRI_CONF_FORCE_EXPLICIT_UNIFORM_LOC_ZERO() \
   DRI_CONF_OPT_S_NODEF(force_explicit_uniform_loc_zero, "Forces an explicit uniform location of zero for the uniform.")

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

#define DRI_CONF_GLX_CLEAR_CONTEXT_RESET_ISOLATION_BIT(def) \
   DRI_CONF_OPT_B(glx_clear_context_reset_isolation_bit, def, "Clear context reset isolation bit before creating context")

#define DRI_CONF_INDIRECT_GL_EXTENSION_OVERRIDE() \
   DRI_CONF_OPT_S_NODEF(indirect_gl_extension_override, \
                  "Allow enabling/disabling a list of indirect-GL extensions")

#define DRI_CONF_FORCE_PROTECTED_CONTENT_CHECK(def) \
   DRI_CONF_OPT_B(force_protected_content_check, def, \
                  "Reject image import if protected_content attribute doesn't match")

#define DRI_CONF_IGNORE_MAP_UNSYNCHRONIZED(def) \
   DRI_CONF_OPT_B(ignore_map_unsynchronized, def, \
                  "Ignore GL_MAP_UNSYNCHRONIZED_BIT, workaround for games that use it incorrectly")

#define DRI_CONF_ZERO_INVALIDATED_BUFFERS(def) \
   DRI_CONF_OPT_B(zero_invalidated_buffers, def, \
                  "Zero memory returned by glMapBufferRange with GL_MAP_INVALIDATE_*_BIT, workaround for games that rely on the undefined contents being zero")

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

#define DRI_CONFIG_INTEL_TBIMR(def) \
   DRI_CONF_OPT_B(intel_tbimr, def, "Enable TBIMR tiled rendering")

#define DRI_CONFIG_INTEL_FORCE_COMPUTE_SURFACE_PREFETCH(def) \
   DRI_CONF_OPT_B(intel_force_compute_surface_prefetch, def, \
                  "Enable binding table surface prefteching for compute shaders")

#define DRI_CONFIG_INTEL_FORCE_SAMPLER_PREFETCH(def) \
   DRI_CONF_OPT_B(intel_force_sampler_prefetch, def, \
                  "Enable binding table sampler prefteching")

#define DRI_CONFIG_INTEL_VF_DISTRIBUTION(def) \
   DRI_CONF_OPT_B(intel_vf_distribution, def, "Enable geometry distribution")

#define DRI_CONFIG_INTEL_TE_DISTRIBUTION(def) \
   DRI_CONF_OPT_B(intel_te_distribution, def, "Enable tesselation distribution")

#define DRI_CONFIG_INTEL_STORAGE_CACHE_POLICY_WT(def) \
   DRI_CONF_OPT_B(intel_storage_cache_policy_wt, def, "Enable write-through cache policy for storage buffers/images.")

#define DRI_CONF_INTEL_ENABLE_WA_14018912822(def) \
   DRI_CONF_OPT_B(intel_enable_wa_14018912822, def, \
                  "Intel workaround for using zero blend constants")

#define DRI_CONF_INTEL_ENABLE_WA_14024015672_MSAA(def) \
   DRI_CONF_OPT_B(intel_enable_wa_14024015672_msaa, def, \
                  "Intel workaround for RHWO MSAA")

#define DRI_CONF_INTEL_SAMPLER_ROUTE_TO_LSC(def) \
   DRI_CONF_OPT_B(intel_sampler_route_to_lsc, def, \
                  "Intel specific toggle to enable sampler route to LSC")

#define DRI_CONF_INTEL_DISABLE_THREADED_CONTEXT(def) \
   DRI_CONF_OPT_B(intel_disable_threaded_context, def, "Disable threaded context")

/**
 * \brief Image quality-related options
 */
#define DRI_CONF_SECTION_QUALITY DRI_CONF_SECTION("Image Quality")

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

#define DRI_CONF_ALLOW_RGB16_CONFIGS(def) \
   DRI_CONF_OPT_B(allow_rgb16_configs, def, \
                  "Allow exposure of visuals and fbconfigs with rgb16 and rgba16 formats")

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

#define DRI_CONF_HEAP_MEMORY_PERCENT(def) \
   DRI_CONF_OPT_F(heap_memory_percent, def, 0.0, 1.0, \
                  "Percentage of total system memory to report as gpu heap memory (0 = driver default)")

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

#define DRI_CONF_WGL_REQUIRE_GDI_COMPAT(def) \
   DRI_CONF_OPT_B(wgl_require_gdi_compat, def, \
                  "Require all pixel formats to have PFD_SUPPORT_GDI flag")

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


/**
 * \brief Asahi specific configuration options
 */
#define DRI_CONF_ASAHI_NO_FP16(def) \
   DRI_CONF_OPT_B(asahi_no_fp16, def, \
                  "Disable 16-bit float support")

#endif
