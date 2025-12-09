/* Based on anv:
 * Copyright © 2015 Intel Corporation
 *
 * Copyright © 2016 Red Hat Inc.
 * Copyright © 2018 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RADV_META_NIR_H
#define RADV_META_NIR_H

#include "compiler/shader_enums.h"
#include "vulkan/vulkan_core.h"
#include "nir_defines.h"

#ifdef __cplusplus
extern "C" {
#endif

struct radv_device;
struct radeon_surf;

nir_builder PRINTFLIKE(3, 4)
   radv_meta_nir_init_shader(struct radv_device *dev, mesa_shader_stage stage, const char *name, ...);

nir_shader *radv_meta_nir_build_vs_generate_vertices(struct radv_device *dev);
nir_shader *radv_meta_nir_build_fs_noop(struct radv_device *dev);

nir_def *radv_meta_nir_get_global_ids(nir_builder *b, unsigned num_components);

void radv_meta_nir_break_on_count(nir_builder *b, nir_variable *var, nir_def *count);

nir_shader *radv_meta_nir_build_fill_memory_shader(struct radv_device *dev, uint32_t bytes_per_invocation);
nir_shader *radv_meta_nir_build_copy_memory_shader(struct radv_device *dev, uint32_t bytes_per_invocation);

nir_shader *radv_meta_nir_build_blit_vertex_shader(struct radv_device *dev);
nir_shader *radv_meta_nir_build_blit_copy_fragment_shader(struct radv_device *dev, enum glsl_sampler_dim tex_dim);
nir_shader *radv_meta_nir_build_blit_copy_fragment_shader_depth(struct radv_device *dev, enum glsl_sampler_dim tex_dim);
nir_shader *radv_meta_nir_build_blit_copy_fragment_shader_stencil(struct radv_device *dev,
                                                                  enum glsl_sampler_dim tex_dim);

nir_shader *radv_meta_nir_build_itob_compute_shader(struct radv_device *dev, bool is_3d);
nir_shader *radv_meta_nir_build_btoi_compute_shader(struct radv_device *dev, bool is_3d);
nir_shader *radv_meta_nir_build_btoi_r32g32b32_compute_shader(struct radv_device *dev);
nir_shader *radv_meta_nir_build_itoi_compute_shader(struct radv_device *dev, bool src_3d, bool dst_3d, int samples);
nir_shader *radv_meta_nir_build_itoi_r32g32b32_compute_shader(struct radv_device *dev);
nir_shader *radv_meta_nir_build_cleari_compute_shader(struct radv_device *dev, bool is_3d, int samples);
nir_shader *radv_meta_nir_build_cleari_r32g32b32_compute_shader(struct radv_device *dev);

typedef nir_def *(*radv_meta_nir_texel_fetch_build_func)(nir_builder *, uint32_t, nir_def *, bool, bool);
nir_def *radv_meta_nir_build_blit2d_texel_fetch(nir_builder *b, uint32_t binding, nir_def *tex_pos, bool is_3d,
                                                bool is_multisampled);
nir_def *radv_meta_nir_build_blit2d_buffer_fetch(nir_builder *b, uint32_t binding, nir_def *tex_pos, bool is_3d,
                                                 bool is_multisampled);

nir_shader *radv_meta_nir_build_blit2d_vertex_shader(struct radv_device *device);
nir_shader *radv_meta_nir_build_blit2d_copy_fragment_shader(struct radv_device *device,
                                                            radv_meta_nir_texel_fetch_build_func txf_func,
                                                            const char *name, bool is_3d, bool is_multisampled);
nir_shader *radv_meta_nir_build_blit2d_copy_fragment_shader_depth(struct radv_device *device,
                                                                  radv_meta_nir_texel_fetch_build_func txf_func,
                                                                  const char *name, bool is_3d, bool is_multisampled);
nir_shader *radv_meta_nir_build_blit2d_copy_fragment_shader_stencil(struct radv_device *device,
                                                                    radv_meta_nir_texel_fetch_build_func txf_func,
                                                                    const char *name, bool is_3d, bool is_multisampled);
nir_shader *radv_meta_nir_build_blit2d_copy_fragment_shader_depth_stencil(struct radv_device *device,
                                                                          radv_meta_nir_texel_fetch_build_func txf_func,
                                                                          const char *name, bool is_3d,
                                                                          bool is_multisampled);

void radv_meta_nir_build_clear_color_shaders(struct radv_device *dev, struct nir_shader **out_vs,
                                             struct nir_shader **out_fs, uint32_t frag_output);
void radv_meta_nir_build_clear_depthstencil_shaders(struct radv_device *dev, struct nir_shader **out_vs,
                                                    struct nir_shader **out_fs, bool unrestricted);
nir_shader *radv_meta_nir_build_clear_htile_mask_shader(struct radv_device *dev);
nir_shader *radv_meta_nir_build_clear_dcc_comp_to_single_shader(struct radv_device *dev, bool is_msaa);

nir_shader *radv_meta_nir_build_copy_vrs_htile_shader(struct radv_device *device, const struct radeon_surf *surf);

nir_shader *radv_meta_nir_build_dcc_retile_compute_shader(struct radv_device *dev, const struct radeon_surf *surf);

nir_shader *radv_meta_nir_build_expand_depth_stencil_compute_shader(struct radv_device *dev);

nir_shader *radv_meta_nir_build_dcc_decompress_compute_shader(struct radv_device *dev);

nir_shader *radv_meta_nir_build_fmask_copy_compute_shader(struct radv_device *dev, int samples);

nir_shader *radv_meta_nir_build_fmask_expand_compute_shader(struct radv_device *device, int samples);

enum radv_meta_resolve_type {
   RADV_META_DEPTH_RESOLVE,
   RADV_META_STENCIL_RESOLVE,
};

enum radv_meta_resolve_compute_type {
   RADV_META_RESOLVE_COMPUTE_NORM,
   RADV_META_RESOLVE_COMPUTE_NORM_SRGB,
   RADV_META_RESOLVE_COMPUTE_INTEGER,
   RADV_META_RESOLVE_COMPUTE_FLOAT,
   RADV_META_RESOLVE_COMPUTE_COUNT,
};

nir_shader *radv_meta_nir_build_resolve_compute_shader(struct radv_device *dev,
                                                       enum radv_meta_resolve_compute_type type, int samples);
nir_shader *radv_meta_nir_build_depth_stencil_resolve_compute_shader(struct radv_device *dev, int samples,
                                                                     enum radv_meta_resolve_type index,
                                                                     VkResolveModeFlagBits resolve_mode);
nir_shader *radv_meta_nir_build_resolve_fragment_shader(struct radv_device *dev, bool is_integer, int samples);
nir_shader *radv_meta_nir_build_depth_stencil_resolve_fragment_shader(struct radv_device *dev, int samples,
                                                                      enum radv_meta_resolve_type index,
                                                                      VkResolveModeFlagBits resolve_mode);

nir_shader *radv_meta_nir_build_resolve_fs(struct radv_device *dev);

nir_shader *radv_meta_nir_build_clear_hiz_compute_shader(struct radv_device *dev, int samples);

#ifdef __cplusplus
}
#endif

#endif /* RADV_META_NIR_H */
