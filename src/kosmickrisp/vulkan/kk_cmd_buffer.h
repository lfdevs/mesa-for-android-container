/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef KK_CMD_BUFFER_H
#define KK_CMD_BUFFER_H 1

#include "kk_private.h"

#include "kk_descriptor_set.h"
#include "kk_image.h"
#include "kk_nir_lower_vbo.h"
#include "kk_shader.h"

#include "kosmickrisp/bridge/mtl_types.h"

#include "util/u_dynarray.h"

#include "vk_command_buffer.h"

#include <stdio.h>

struct kk_query_pool;

struct kk_root_descriptor_table {
   struct kk_bo *root_buffer;

   union {
      struct {
         /* Vertex input state */
         uint32_t buffer_strides[KK_MAX_VBUFS];
         uint64_t attrib_base[KK_MAX_ATTRIBS];
         uint32_t attrib_clamps[KK_MAX_ATTRIBS];
         float blend_constant[4];
         uint32_t draw_id;
      } draw;
      struct {
         uint32_t base_group[3];
      } cs;
   };

   /* Client push constants */
   uint8_t push[KK_MAX_PUSH_SIZE];

   /* Descriptor set base addresses */
   uint64_t sets[KK_MAX_SETS];

   /* Dynamic buffer bindings */
   struct kk_buffer_address dynamic_buffers[KK_MAX_DYNAMIC_BUFFERS];

   /* Start index in dynamic_buffers where each set starts */
   uint8_t set_dynamic_buffer_start[KK_MAX_SETS];
};

struct kk_descriptor_state {
   bool root_dirty;
   struct kk_root_descriptor_table root;

   uint32_t set_sizes[KK_MAX_SETS];
   struct kk_descriptor_set *sets[KK_MAX_SETS];

   uint32_t push_dirty;
   struct kk_push_descriptor_set *push[KK_MAX_SETS];
};

struct kk_attachment {
   VkFormat vk_format;
   struct kk_image_view *iview;

   VkResolveModeFlagBits resolve_mode;
   struct kk_image_view *resolve_iview;

   /* Needed to track the value of storeOp in case we need to copy images for
    * the DRM_FORMAT_MOD_LINEAR case */
   VkAttachmentStoreOp store_op;
};

struct kk_rendering_state {
   VkRenderingFlagBits flags;

   VkRect2D area;
   uint32_t layer_count;
   uint32_t view_mask;
   uint32_t samples;

   uint32_t color_att_count;
   struct kk_attachment color_att[KK_MAX_RTS];
   struct kk_attachment depth_att;
   struct kk_attachment stencil_att;
   struct kk_attachment fsr_att;
};

/* Dirty tracking bits for state not tracked by vk_dynamic_graphics_state or
 * shaders_dirty.
 */
enum kk_dirty {
   KK_DIRTY_INDEX = BITFIELD_BIT(0),
   KK_DIRTY_VB = BITFIELD_BIT(1),
   KK_DIRTY_OCCLUSION = BITFIELD_BIT(2),
   KK_DIRTY_PROVOKING = BITFIELD_BIT(3),
   KK_DIRTY_VARYINGS = BITFIELD_BIT(4),
   KK_DIRTY_PIPELINE = BITFIELD_BIT(5),
};

struct kk_graphics_state {
   struct kk_rendering_state render;
   struct kk_descriptor_state descriptors;

   mtl_render_pipeline_state *pipeline_state;
   mtl_depth_stencil_state *depth_stencil_state;
   mtl_render_pass_descriptor *render_pass_descriptor;
   bool is_depth_stencil_dynamic;
   bool is_cull_front_and_back;
   bool restart_disabled;

   enum mtl_primitive_type primitive_type;
   enum mesa_prim prim;
   enum kk_dirty dirty;

   struct {
      enum mtl_visibility_result_mode mode;

      /* If enabled, index of the current occlusion query in the occlusion heap.
       * There can only be one active at a time (hardware constraint).
       */
      uint16_t index;
   } occlusion;

   /* Index buffer */
   struct {
      mtl_buffer *handle;
      uint32_t size;
      uint32_t offset;
      uint32_t restart;
      uint8_t bytes_per_index;
   } index;

   /* Vertex buffers */
   struct {
      struct kk_addr_range addr_range[KK_MAX_VBUFS];
      mtl_buffer *handles[KK_MAX_VBUFS];
      uint32_t attribs_read;
      /* Required to understand maximum size of index buffer if primitive is
       * triangle fans */
      uint32_t max_vertices;
   } vb;

   /* Needed by vk_command_buffer::dynamic_graphics_state */
   struct vk_vertex_input_state _dynamic_vi;
   struct vk_sample_locations_state _dynamic_sl;
};

struct kk_compute_state {
   struct kk_descriptor_state descriptors;
   mtl_compute_pipeline_state *pipeline_state;
   struct mtl_size local_size;
   enum kk_dirty dirty;
};

struct kk_encoder;

struct kk_cmd_buffer {
   struct vk_command_buffer vk;

   struct kk_encoder *encoder;
   void *drawable;

   struct {
      struct kk_graphics_state gfx;
      struct kk_compute_state cs;
   } state;

   /* Owned large BOs */
   struct util_dynarray large_bos;
};

VK_DEFINE_HANDLE_CASTS(kk_cmd_buffer, vk.base, VkCommandBuffer,
                       VK_OBJECT_TYPE_COMMAND_BUFFER)

extern const struct vk_command_buffer_ops kk_cmd_buffer_ops;

static inline struct kk_device *
kk_cmd_buffer_device(struct kk_cmd_buffer *cmd)
{
   return (struct kk_device *)cmd->vk.base.device;
}

static inline struct kk_cmd_pool *
kk_cmd_buffer_pool(struct kk_cmd_buffer *cmd)
{
   return (struct kk_cmd_pool *)cmd->vk.pool;
}

static inline struct kk_descriptor_state *
kk_get_descriptors_state(struct kk_cmd_buffer *cmd,
                         VkPipelineBindPoint bind_point)
{
   switch (bind_point) {
   case VK_PIPELINE_BIND_POINT_GRAPHICS:
      return &cmd->state.gfx.descriptors;
   case VK_PIPELINE_BIND_POINT_COMPUTE:
      return &cmd->state.cs.descriptors;
   default:
      UNREACHABLE("Unhandled bind point");
   }
};

void kk_cmd_release_resources(struct kk_device *dev, struct kk_cmd_buffer *cmd);

static void
kk_cmd_buffer_dirty_all_gfx(struct kk_cmd_buffer *cmd)
{
   /* Ensure we flush all graphics state */
   vk_dynamic_graphics_state_dirty_all(&cmd->vk.dynamic_graphics_state);
   cmd->state.gfx.dirty = ~0u;
}

void kk_cmd_release_dynamic_ds_state(struct kk_cmd_buffer *cmd);

mtl_depth_stencil_state *
kk_compile_depth_stencil_state(struct kk_device *device,
                               const struct vk_depth_stencil_state *ds,
                               bool has_depth, bool has_stencil);

void kk_meta_resolve_rendering(struct kk_cmd_buffer *cmd,
                               const VkRenderingInfo *pRenderingInfo);

void kk_cmd_buffer_write_descriptor_buffer(struct kk_cmd_buffer *cmd,
                                           struct kk_descriptor_state *desc,
                                           size_t size, size_t offset);

/* Allocates temporary buffer that will be released once the command buffer has
 * completed */
struct kk_bo *kk_cmd_allocate_buffer(struct kk_cmd_buffer *cmd, size_t size_B,
                                     size_t alignment_B);

struct kk_pool {
   mtl_buffer *handle;
   uint64_t gpu;
   void *cpu;
};
struct kk_pool kk_pool_upload(struct kk_cmd_buffer *cmd, void *data,
                              size_t size_B, size_t alignment_B);

uint64_t kk_upload_descriptor_root(struct kk_cmd_buffer *cmd,
                                   VkPipelineBindPoint bind_point);

void kk_cmd_buffer_flush_push_descriptors(struct kk_cmd_buffer *cmd,
                                          struct kk_descriptor_state *desc);

void kk_cmd_write(struct kk_cmd_buffer *cmd, mtl_buffer *buffer, uint64_t addr,
                  uint64_t value);

void kk_cmd_dispatch_pipeline(struct kk_cmd_buffer *cmd,
                              mtl_compute_encoder *encoder,
                              mtl_compute_pipeline_state *pipeline,
                              const void *push_data, size_t push_size,
                              uint32_t groupCountX, uint32_t groupCountY,
                              uint32_t groupCountZ);

#endif
