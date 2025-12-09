/*
 * Copyright © 2016 Red Hat
 * based on intel anv code:
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RADV_META_H
#define RADV_META_H

#include "radv_buffer.h"
#include "radv_buffer_view.h"
#include "radv_cmd_buffer.h"
#include "radv_device.h"
#include "radv_device_memory.h"
#include "radv_entrypoints.h"
#include "radv_image.h"
#include "radv_image_view.h"
#include "radv_physical_device.h"
#include "radv_pipeline.h"
#include "radv_pipeline_compute.h"
#include "radv_pipeline_graphics.h"
#include "radv_queue.h"
#include "radv_shader.h"
#include "radv_shader_object.h"
#include "radv_sqtt.h"

#include "vk_render_pass.h"
#include "vk_shader_module.h"

#ifdef __cplusplus
extern "C" {
#endif

enum radv_meta_save_flags {
   RADV_META_SAVE_RENDER = (1 << 0),
   RADV_META_SAVE_CONSTANTS = (1 << 1),
   RADV_META_SAVE_DESCRIPTORS = (1 << 2),
   RADV_META_SAVE_GRAPHICS_PIPELINE = (1 << 3),
   RADV_META_SAVE_COMPUTE_PIPELINE = (1 << 4),
};

struct radv_meta_saved_state {
   uint32_t flags;

   struct radv_descriptor_set *old_descriptor_set0;
   bool old_descriptor_set0_valid;
   uint64_t old_descriptor_buffer_addr0;
   uint64_t old_descriptor_buffer0;

   struct radv_graphics_pipeline *old_graphics_pipeline;
   struct radv_compute_pipeline *old_compute_pipeline;
   struct radv_dynamic_state dynamic;

   struct radv_shader_object *old_shader_objs[MESA_VULKAN_SHADER_STAGES];

   char push_constants[MAX_PUSH_CONSTANTS_SIZE];

   struct radv_rendering_state render;

   unsigned active_emulated_pipeline_queries;
   unsigned active_emulated_prims_gen_queries;
   unsigned active_emulated_prims_xfb_queries;
   unsigned active_occlusion_queries;
};

enum radv_copy_flags {
   RADV_COPY_FLAGS_DEVICE_LOCAL = 1 << 0,
   RADV_COPY_FLAGS_SPARSE = 1 << 1,
};

extern const VkFormat radv_fs_key_format_exemplars[NUM_META_FS_KEYS];

enum radv_meta_object_key_type {
   RADV_META_OBJECT_KEY_NOOP = VK_META_OBJECT_KEY_DRIVER_OFFSET,
   RADV_META_OBJECT_KEY_BLIT,
   RADV_META_OBJECT_KEY_BLIT2D,
   RADV_META_OBJECT_KEY_BLIT2D_COLOR,
   RADV_META_OBJECT_KEY_BLIT2D_DEPTH,
   RADV_META_OBJECT_KEY_BLIT2D_STENCIL,
   RADV_META_OBJECT_KEY_BLIT2D_DEPTH_STENCIL,
   RADV_META_OBJECT_KEY_FILL_MEMORY,
   RADV_META_OBJECT_KEY_COPY_MEMORY,
   RADV_META_OBJECT_KEY_COPY_IMAGE_TO_BUFFER,
   RADV_META_OBJECT_KEY_COPY_BUFFER_TO_IMAGE,
   RADV_META_OBJECT_KEY_COPY_BUFFER_TO_IMAGE_R32G32B32,
   RADV_META_OBJECT_KEY_COPY_IMAGE,
   RADV_META_OBJECT_KEY_COPY_IMAGE_R32G32B32,
   RADV_META_OBJECT_KEY_COPY_VRS_HTILE,
   RADV_META_OBJECT_KEY_CLEAR_CS,
   RADV_META_OBJECT_KEY_CLEAR_CS_R32G32B32,
   RADV_META_OBJECT_KEY_CLEAR_COLOR,
   RADV_META_OBJECT_KEY_CLEAR_DS,
   RADV_META_OBJECT_KEY_CLEAR_HTILE,
   RADV_META_OBJECT_KEY_CLEAR_DCC_COMP_TO_SINGLE,
   RADV_META_OBJECT_KEY_CLEAR_HIZ,
   RADV_META_OBJECT_KEY_FAST_CLEAR_ELIMINATE,
   RADV_META_OBJECT_KEY_DCC_DECOMPRESS,
   RADV_META_OBJECT_KEY_DCC_RETILE,
   RADV_META_OBJECT_KEY_HTILE_EXPAND_GFX,
   RADV_META_OBJECT_KEY_HTILE_EXPAND_CS,
   RADV_META_OBJECT_KEY_FMASK_COPY,
   RADV_META_OBJECT_KEY_FMASK_EXPAND,
   RADV_META_OBJECT_KEY_FMASK_DECOMPRESS,
   RADV_META_OBJECT_KEY_RESOLVE_HW,
   RADV_META_OBJECT_KEY_RESOLVE_CS,
   RADV_META_OBJECT_KEY_RESOLVE_COLOR_CS,
   RADV_META_OBJECT_KEY_RESOLVE_DS_CS,
   RADV_META_OBJECT_KEY_RESOLVE_FS,
   RADV_META_OBJECT_KEY_RESOLVE_COLOR_FS,
   RADV_META_OBJECT_KEY_RESOLVE_DS_FS,
   RADV_META_OBJECT_KEY_DGC,
   RADV_META_OBJECT_KEY_QUERY,
   RADV_META_OBJECT_KEY_QUERY_OCCLUSION,
   RADV_META_OBJECT_KEY_QUERY_PIPELINE_STATS,
   RADV_META_OBJECT_KEY_QUERY_TFB,
   RADV_META_OBJECT_KEY_QUERY_TIMESTAMP,
   RADV_META_OBJECT_KEY_QUERY_PRIMS_GEN,
   RADV_META_OBJECT_KEY_QUERY_MESH_PRIMS_GEN,
   RADV_META_OBJECT_KEY_BVH_COPY,
   RADV_META_OBJECT_KEY_BVH_COPY_BLAS_ADDRS_GFX12,
   RADV_META_OBJECT_KEY_BVH_ENCODE,
   RADV_META_OBJECT_KEY_BVH_ENCODE_TRIANGLES_GFX12,
   RADV_META_OBJECT_KEY_BVH_UPDATE,
   RADV_META_OBJECT_KEY_BVH_HEADER,
};

VkResult radv_device_init_meta(struct radv_device *device);
void radv_device_finish_meta(struct radv_device *device);

VkResult radv_device_init_accel_struct_build_state(struct radv_device *device);
void radv_device_finish_accel_struct_build_state(struct radv_device *device);

void radv_meta_save(struct radv_meta_saved_state *saved_state, struct radv_cmd_buffer *cmd_buffer, uint32_t flags);

void radv_meta_restore(const struct radv_meta_saved_state *state, struct radv_cmd_buffer *cmd_buffer);

VkImageViewType radv_meta_get_view_type(const struct radv_image *image);

static inline VkFormat
radv_meta_get_96bit_channel_format(VkFormat format)
{
   switch (format) {
   case VK_FORMAT_R32G32B32_UINT:
      return VK_FORMAT_R32_UINT;
      break;
   case VK_FORMAT_R32G32B32_SINT:
      return VK_FORMAT_R32_SINT;
      break;
   case VK_FORMAT_R32G32B32_SFLOAT:
      return VK_FORMAT_R32_SFLOAT;
      break;
   default:
      UNREACHABLE("invalid R32G32B32 format");
   }
}

struct radv_meta_blit2d_surf {
   /** The size of an element in bytes. */
   uint8_t bs;
   VkFormat format;

   struct radv_image *image;
   unsigned level;
   unsigned layer;
   VkImageAspectFlags aspect_mask;
   VkImageLayout current_layout;
   bool disable_compression;
};

struct radv_meta_blit2d_buffer {
   uint64_t addr;
   uint64_t size;
   uint32_t offset;
   uint32_t pitch;
   VkFormat format;
   enum radv_copy_flags copy_flags;
};

void radv_gfx_copy_image(struct radv_cmd_buffer *cmd_buffer, struct radv_meta_blit2d_surf *src,
                         struct radv_meta_blit2d_surf *dst, const VkOffset3D *src_offset, const VkOffset3D *dst_offset,
                         const VkExtent3D *extent);

void radv_gfx_copy_memory_to_image(struct radv_cmd_buffer *cmd_buffer, struct radv_meta_blit2d_buffer *src,
                                   struct radv_meta_blit2d_surf *dst, const VkOffset3D *offset,
                                   const VkExtent3D *extent);

void radv_meta_image_to_buffer(struct radv_cmd_buffer *cmd_buffer, struct radv_meta_blit2d_surf *src,
                               struct radv_meta_blit2d_buffer *dst, const VkOffset3D *offset, const VkExtent3D *extent);

void radv_meta_buffer_to_image_cs(struct radv_cmd_buffer *cmd_buffer, struct radv_meta_blit2d_buffer *src,
                                  struct radv_meta_blit2d_surf *dst, const VkOffset3D *offset,
                                  const VkExtent3D *extent);

void radv_meta_image_to_image_cs(struct radv_cmd_buffer *cmd_buffer, struct radv_meta_blit2d_surf *src,
                                 struct radv_meta_blit2d_surf *dst, const VkOffset3D *src_offset,
                                 const VkOffset3D *dst_offset, const VkExtent3D *extent);

void radv_meta_clear_image_cs(struct radv_cmd_buffer *cmd_buffer, struct radv_meta_blit2d_surf *dst,
                              const VkClearColorValue *clear_color);

void radv_expand_depth_stencil(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image,
                               const VkImageSubresourceRange *subresourceRange,
                               struct radv_sample_locations_state *sample_locs);
void radv_decompress_dcc(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image,
                         const VkImageSubresourceRange *subresourceRange);
void radv_retile_dcc(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image);

void radv_fast_clear_eliminate(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image,
                               const VkImageSubresourceRange *subresourceRange);
void radv_fmask_decompress(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image,
                           const VkImageSubresourceRange *subresourceRange);
void radv_fmask_color_expand(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image,
                             const VkImageSubresourceRange *subresourceRange);

void radv_copy_vrs_htile(struct radv_cmd_buffer *cmd_buffer, struct radv_image_view *vrs_iview, const VkRect2D *rect,
                         struct radv_image *dst_image, uint64_t htile_va, bool read_htile_value);

bool radv_can_use_fmask_copy(struct radv_cmd_buffer *cmd_buffer, const struct radv_image *src_image,
                             const struct radv_image *dst_image, const VkOffset3D *src_offset,
                             const VkOffset3D *dst_offset, const VkExtent3D *extent);

void radv_fmask_copy(struct radv_cmd_buffer *cmd_buffer, struct radv_meta_blit2d_surf *src,
                     struct radv_meta_blit2d_surf *dst);

void radv_meta_resolve_compute_image(struct radv_cmd_buffer *cmd_buffer, struct radv_image *src_image,
                                     VkFormat src_format, VkImageLayout src_image_layout, struct radv_image *dst_image,
                                     VkFormat dst_format, VkImageLayout dst_image_layout,
                                     const VkImageResolve2 *region);

void radv_meta_resolve_fragment_image(struct radv_cmd_buffer *cmd_buffer, struct radv_image *src_image,
                                      VkFormat src_format, VkImageLayout src_image_layout, struct radv_image *dst_image,
                                      VkFormat dst_format, VkImageLayout dst_image_layout,
                                      const VkImageResolve2 *region);

void radv_decompress_resolve_rendering_src(struct radv_cmd_buffer *cmd_buffer);

void radv_decompress_resolve_src(struct radv_cmd_buffer *cmd_buffer, struct radv_image *src_image,
                                 VkImageLayout src_image_layout, const VkImageResolve2 *region);

uint32_t radv_clear_cmask(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image,
                          const VkImageSubresourceRange *range, uint32_t value);
uint32_t radv_clear_fmask(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image,
                          const VkImageSubresourceRange *range, uint32_t value);
uint32_t radv_clear_dcc(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image,
                        const VkImageSubresourceRange *range, uint32_t value);
uint32_t radv_clear_htile(struct radv_cmd_buffer *cmd_buffer, const struct radv_image *image,
                          const VkImageSubresourceRange *range, uint32_t value, bool is_clear);

void radv_clear_hiz(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image, const VkImageSubresourceRange *range,
                    uint32_t value);

void radv_update_memory_cp(struct radv_cmd_buffer *cmd_buffer, uint64_t va, const void *data, uint64_t size);

void radv_update_memory(struct radv_cmd_buffer *cmd_buffer, uint64_t va, uint64_t size, const void *data,
                        enum radv_copy_flags dst_copy_flags);

void radv_meta_decode_etc(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image, VkImageLayout layout,
                          const VkImageSubresourceLayers *subresource, VkOffset3D offset, VkExtent3D extent);
void radv_meta_decode_astc(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image, VkImageLayout layout,
                           const VkImageSubresourceLayers *subresource, VkOffset3D offset, VkExtent3D extent);

uint32_t radv_fill_buffer(struct radv_cmd_buffer *cmd_buffer, struct radeon_winsys_bo *bo, uint64_t va, uint64_t size,
                          uint32_t value);

uint32_t radv_fill_memory(struct radv_cmd_buffer *cmd_buffer, uint64_t va, uint64_t size, uint32_t value,
                          enum radv_copy_flags copy_flags);

uint32_t radv_fill_image(struct radv_cmd_buffer *cmd_buffer, const struct radv_image *image, uint64_t offset,
                         uint64_t size, uint32_t value);

void radv_copy_memory(struct radv_cmd_buffer *cmd_buffer, uint64_t src_va, uint64_t dst_va, uint64_t size,
                      enum radv_copy_flags src_copy_flags, enum radv_copy_flags dst_copy_flags);

void radv_cmd_buffer_clear_attachment(struct radv_cmd_buffer *cmd_buffer, const VkClearAttachment *attachment);

void radv_cmd_buffer_clear_rendering(struct radv_cmd_buffer *cmd_buffer, const VkRenderingInfo *render_info);

void radv_cmd_buffer_resolve_rendering(struct radv_cmd_buffer *cmd_buffer);

void radv_meta_resolve_depth_stencil_cs(struct radv_cmd_buffer *cmd_buffer, struct radv_image *src_image,
                                        VkFormat src_format, VkImageLayout src_image_layout,
                                        struct radv_image *dst_image, VkFormat dst_format,
                                        VkImageLayout dst_image_layout, VkResolveModeFlagBits resolve_mode,
                                        const VkImageResolve2 *region);

void radv_meta_resolve_depth_stencil_fs(struct radv_cmd_buffer *cmd_buffer, struct radv_image *src_image,
                                        VkFormat src_format, VkImageLayout src_image_layout,
                                        struct radv_image *dst_image, VkFormat dst_format,
                                        VkImageLayout dst_image_layout, VkResolveModeFlagBits resolve_mode,
                                        const VkImageResolve2 *region, uint32_t view_mask);

VkResult radv_meta_get_noop_pipeline_layout(struct radv_device *device, VkPipelineLayout *layout_out);

void radv_meta_bind_descriptors(struct radv_cmd_buffer *cmd_buffer, VkPipelineBindPoint bind_point,
                                VkPipelineLayout _layout, uint32_t num_descriptors,
                                const VkDescriptorGetInfoEXT *descriptors);

enum radv_copy_flags radv_get_copy_flags_from_bo(const struct radeon_winsys_bo *bo);

#ifdef __cplusplus
}
#endif

#endif /* RADV_META_H */
