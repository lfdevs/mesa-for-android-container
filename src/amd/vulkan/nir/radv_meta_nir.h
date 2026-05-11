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

struct radeon_surf;
enum amd_gfx_level;

nir_builder PRINTFLIKE(2, 3) radv_meta_nir_init_shader(mesa_shader_stage stage, const char *name, ...);

nir_shader *radv_meta_nir_build_vs_generate_vertices(void);
nir_shader *radv_meta_nir_build_fs_noop(void);

nir_def *radv_meta_nir_get_global_ids(nir_builder *b, unsigned num_components);

void radv_meta_nir_break_on_count(nir_builder *b, nir_variable *var, nir_def *count);

nir_shader *radv_meta_nir_build_fill_memory_shader(uint32_t bytes_per_invocation);
nir_shader *radv_meta_nir_build_copy_memory_shader(uint32_t bytes_per_invocation);

nir_shader *radv_meta_nir_build_blit_vertex_shader(void);
nir_shader *radv_meta_nir_build_blit_copy_fragment_shader(enum glsl_sampler_dim tex_dim);
nir_shader *radv_meta_nir_build_blit_copy_fragment_shader_depth(enum glsl_sampler_dim tex_dim);
nir_shader *radv_meta_nir_build_blit_copy_fragment_shader_stencil(enum glsl_sampler_dim tex_dim);

nir_shader *radv_meta_nir_build_itob_compute_shader(bool is_3d);
nir_shader *radv_meta_nir_build_btoi_compute_shader(bool is_3d);
nir_shader *radv_meta_nir_build_itoi_compute_shader(bool src_3d, bool dst_3d, int samples);
nir_shader *radv_meta_nir_build_cleari_compute_shader(bool is_3d, int samples);
nir_shader *radv_meta_nir_build_cleari_96bit_compute_shader(void);

typedef nir_def *(*radv_meta_nir_texel_fetch_build_func)(nir_builder *, uint32_t, nir_def *, bool, bool);
nir_def *radv_meta_nir_build_blit2d_texel_fetch(nir_builder *b, uint32_t binding, nir_def *tex_pos, bool is_3d,
                                                bool is_multisampled);
nir_def *radv_meta_nir_build_blit2d_buffer_fetch(nir_builder *b, uint32_t binding, nir_def *tex_pos, bool is_3d,
                                                 bool is_multisampled);

nir_shader *radv_meta_nir_build_blit2d_vertex_shader(void);
nir_shader *radv_meta_nir_build_blit2d_copy_fragment_shader(radv_meta_nir_texel_fetch_build_func txf_func,
                                                            const char *name, bool is_3d, bool is_multisampled);
nir_shader *radv_meta_nir_build_blit2d_copy_fragment_shader_depth(radv_meta_nir_texel_fetch_build_func txf_func,
                                                                  const char *name, bool is_3d, bool is_multisampled);
nir_shader *radv_meta_nir_build_blit2d_copy_fragment_shader_stencil(radv_meta_nir_texel_fetch_build_func txf_func,
                                                                    const char *name, bool is_3d, bool is_multisampled);
nir_shader *radv_meta_nir_build_blit2d_copy_fragment_shader_depth_stencil(radv_meta_nir_texel_fetch_build_func txf_func,
                                                                          const char *name, bool is_3d,
                                                                          bool is_multisampled);

void radv_meta_nir_build_clear_color_shaders(struct nir_shader **out_vs, struct nir_shader **out_fs,
                                             uint32_t frag_output);
void radv_meta_nir_build_clear_depthstencil_shaders(struct nir_shader **out_vs, struct nir_shader **out_fs,
                                                    bool unrestricted);
nir_shader *radv_meta_nir_build_clear_htile_mask_shader(void);
nir_shader *radv_meta_nir_build_clear_dcc_comp_to_single_shader(bool is_msaa);

nir_shader *radv_meta_nir_build_copy_vrs_htile_shader(enum amd_gfx_level gfx_level, uint32_t gb_addr_config,
                                                      const struct radeon_surf *surf);

nir_shader *radv_meta_nir_build_dcc_retile_compute_shader(enum amd_gfx_level gfx_level, uint32_t gb_addr_config,
                                                          const struct radeon_surf *surf);

nir_shader *radv_meta_nir_build_expand_depth_stencil_compute_shader(uint8_t samples);

nir_shader *radv_meta_nir_build_dcc_decompress_compute_shader(void);

nir_shader *radv_meta_nir_build_fmask_copy_compute_shader(int samples);

nir_shader *radv_meta_nir_build_fmask_expand_compute_shader(int samples);

enum radv_meta_resolve_compute_type {
   RADV_META_RESOLVE_COMPUTE_NORM,
   RADV_META_RESOLVE_COMPUTE_NORM_SRGB,
   RADV_META_RESOLVE_COMPUTE_INTEGER,
   RADV_META_RESOLVE_COMPUTE_FLOAT,
   RADV_META_RESOLVE_COMPUTE_COUNT,
};

nir_shader *radv_meta_nir_build_resolve_cs(bool use_fmask, enum radv_meta_resolve_compute_type type, int samples,
                                           VkImageAspectFlags aspects, VkResolveModeFlagBits resolve_mode);
nir_shader *radv_meta_nir_build_resolve_fs(bool use_fmask, int samples, bool is_integer, VkImageAspectFlags aspects,
                                           VkResolveModeFlagBits resolve_mode);

nir_shader *radv_meta_nir_build_clear_hiz_compute_shader(int samples);

nir_shader *radv_meta_nir_build_copy_memory_indirect_preprocess_cs(void);
nir_shader *radv_meta_nir_build_copy_memory_indirect_cs(void);

nir_shader *radv_meta_nir_build_copy_memory_to_image_indirect_preprocess_cs(void);
nir_shader *radv_meta_nir_build_copy_memory_to_image_indirect_cs(uint8_t fmt_block_width, uint8_t fmt_block_height,
                                                                 uint8_t fmt_block_depth, uint8_t fmt_element_size_B,
                                                                 bool is_3d);

nir_shader *radv_meta_nir_build_copy_memory_to_image_indirect_vs(uint8_t fmt_block_width, uint8_t fmt_block_height,
                                                                 uint8_t fmt_block_depth);
nir_shader *radv_meta_nir_build_copy_memory_to_image_indirect_fs(VkImageAspectFlags aspect_mask,
                                                                 uint8_t fmt_block_width, uint8_t fmt_block_height,
                                                                 uint8_t fmt_element_size_B);

#ifdef __cplusplus
}
#endif

#endif /* RADV_META_NIR_H */
