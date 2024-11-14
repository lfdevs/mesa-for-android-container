/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#ifndef NVK_CMD_BUFFER_H
#define NVK_CMD_BUFFER_H 1

#include "nvk_private.h"

#include "nv_push.h"
#include "nvk_cmd_pool.h"
#include "nvk_descriptor_set.h"
#include "nvk_image.h"
#include "nvk_shader.h"

#include "util/u_dynarray.h"

#include "vk_command_buffer.h"

#include <stdio.h>

struct nvk_buffer;
struct nvk_cbuf;
struct nvk_cmd_mem;
struct nvk_cmd_buffer;
struct nvk_cmd_pool;
struct nvk_image_view;
struct nvk_push_descriptor_set;
struct nvk_shader;
struct vk_shader;

/** Root descriptor table.  This gets pushed to the GPU directly */
struct nvk_root_descriptor_table {
   union {
      struct {
         uint32_t base_vertex;
         uint32_t base_instance;
         uint32_t draw_index;
         uint32_t view_index;
         struct nak_sample_location sample_locations[NVK_MAX_SAMPLES];
         struct nak_sample_mask sample_masks[NVK_MAX_SAMPLES];
      } draw;
      struct {
         uint32_t base_group[3];
         uint32_t group_count[3];
      } cs;
   };

   /* Client push constants */
   uint8_t push[NVK_MAX_PUSH_SIZE];

   /* Descriptor set addresses */
   struct nvk_buffer_address sets[NVK_MAX_SETS];

   /* For each descriptor set, the index in dynamic_buffers where that set's
    * the dynamic buffers start. This is maintained for every set, regardless
    * of whether or not anything is bound there.
    */
   uint8_t set_dynamic_buffer_start[NVK_MAX_SETS];

   /* Dynamic buffer bindings */
   union nvk_buffer_descriptor dynamic_buffers[NVK_MAX_DYNAMIC_BUFFERS];

   /* enfore alignment to 0x100 as needed pre pascal */
   uint8_t __padding[0x38];
};

/* helper macro for computing root descriptor byte offsets */
#define nvk_root_descriptor_offset(member)\
   offsetof(struct nvk_root_descriptor_table, member)

enum ENUM_PACKED nvk_descriptor_set_type {
   NVK_DESCRIPTOR_SET_TYPE_NONE,
   NVK_DESCRIPTOR_SET_TYPE_SET,
   NVK_DESCRIPTOR_SET_TYPE_PUSH,
   NVK_DESCRIPTOR_SET_TYPE_BUFFER,
};

struct nvk_descriptor_set_binding {
   enum nvk_descriptor_set_type type;
   struct nvk_descriptor_set *set;
   struct nvk_push_descriptor_set *push;
};

struct nvk_descriptor_state {
   alignas(16) char root[sizeof(struct nvk_root_descriptor_table)];
   void (*flush_root)(struct nvk_cmd_buffer *cmd,
                      struct nvk_descriptor_state *desc,
                      size_t offset, size_t size);

   struct nvk_descriptor_set_binding sets[NVK_MAX_SETS];
   uint32_t push_dirty;
};

#define nvk_descriptor_state_get_root(desc, member, dst) do { \
   const struct nvk_root_descriptor_table *root = \
      (const struct nvk_root_descriptor_table *)(desc)->root; \
   *dst = root->member; \
} while (0)

#define nvk_descriptor_state_get_root_array(desc, member, \
                                            start, count, dst) do { \
   const struct nvk_root_descriptor_table *root = \
      (const struct nvk_root_descriptor_table *)(desc)->root; \
   unsigned _start = start; \
   unsigned _count = count; \
   assert(_start + _count <= ARRAY_SIZE(root->member)); \
   for (unsigned i = 0; i < _count; i++) \
      (dst)[i] = root->member[i + _start]; \
} while (0)

#define nvk_descriptor_state_set_root(cmd, desc, member, src) do { \
   struct nvk_descriptor_state *_desc = (desc); \
   struct nvk_root_descriptor_table *root = \
      (struct nvk_root_descriptor_table *)_desc->root; \
   root->member = (src); \
   if (_desc->flush_root != NULL) { \
      size_t offset = (char *)&root->member - (char *)root; \
      _desc->flush_root((cmd), _desc, offset, sizeof(root->member)); \
   } \
} while (0)

#define nvk_descriptor_state_set_root_array(cmd, desc, member, \
                                            start, count, src) do { \
   struct nvk_descriptor_state *_desc = (desc); \
   struct nvk_root_descriptor_table *root = \
      (struct nvk_root_descriptor_table *)_desc->root; \
   unsigned _start = start; \
   unsigned _count = count; \
   assert(_start + _count <= ARRAY_SIZE(root->member)); \
   for (unsigned i = 0; i < _count; i++) \
      root->member[i + _start] = (src)[i]; \
   if (_desc->flush_root != NULL) { \
      size_t offset = (char *)&root->member[_start] - (char *)root; \
      _desc->flush_root((cmd), _desc, offset, \
                        _count * sizeof(root->member[0])); \
   } \
} while (0)

struct nvk_attachment {
   VkFormat vk_format;
   struct nvk_image_view *iview;

   VkResolveModeFlagBits resolve_mode;
   struct nvk_image_view *resolve_iview;

   /* Needed to track the value of storeOp in case we need to copy images for
    * the DRM_FORMAT_MOD_LINEAR case */
   VkAttachmentStoreOp store_op;
};

struct nvk_rendering_state {
   VkRenderingFlagBits flags;

   VkRect2D area;
   uint32_t layer_count;
   uint32_t view_mask;
   uint32_t samples;

   uint32_t color_att_count;
   struct nvk_attachment color_att[NVK_MAX_RTS];
   struct nvk_attachment depth_att;
   struct nvk_attachment stencil_att;
   struct nvk_attachment fsr_att;

   bool all_linear;
};

struct nvk_graphics_state {
   struct nvk_rendering_state render;
   struct nvk_descriptor_state descriptors;

   VkShaderStageFlags shaders_dirty;
   struct nvk_shader *shaders[MESA_SHADER_MESH + 1];

   struct nvk_cbuf_group {
      uint16_t dirty;
      struct nvk_cbuf cbufs[16];
   } cbuf_groups[5];

   /* Used for meta save/restore */
   struct nvk_addr_range vb0;

   /* Needed by vk_command_buffer::dynamic_graphics_state */
   struct vk_vertex_input_state _dynamic_vi;
   struct vk_sample_locations_state _dynamic_sl;
};

struct nvk_compute_state {
   struct nvk_descriptor_state descriptors;
   struct nvk_shader *shader;
};

struct nvk_cmd_push {
   void *map;
   uint64_t addr;
   uint32_t range;
   bool no_prefetch;
};

struct nvk_cmd_buffer {
   struct vk_command_buffer vk;

   struct nvk_cmd_state {
      uint64_t descriptor_buffers[NVK_MAX_SETS];
      struct nvk_graphics_state gfx;
      struct nvk_compute_state cs;
   } state;

   /** List of nvk_cmd_mem
    *
    * This list exists entirely for ownership tracking.  Everything in here
    * must also be in pushes or bo_refs if it is to be referenced by this
    * command buffer.
    */
   struct list_head owned_mem;
   struct list_head owned_gart_mem;

   struct nvk_cmd_mem *upload_mem;
   uint32_t upload_offset;

   struct nvk_cmd_mem *cond_render_gart_mem;
   uint32_t cond_render_gart_offset;

   struct nvk_cmd_mem *push_mem;
   uint32_t *push_mem_limit;
   struct nv_push push;

   /** Array of struct nvk_cmd_push
    *
    * This acts both as a BO reference as well as provides a range in the
    * buffer to use as a pushbuf.
    */
   struct util_dynarray pushes;

   uint64_t tls_space_needed;
};

VK_DEFINE_HANDLE_CASTS(nvk_cmd_buffer, vk.base, VkCommandBuffer,
                       VK_OBJECT_TYPE_COMMAND_BUFFER)

extern const struct vk_command_buffer_ops nvk_cmd_buffer_ops;

static inline struct nvk_device *
nvk_cmd_buffer_device(struct nvk_cmd_buffer *cmd)
{
   return (struct nvk_device *)cmd->vk.base.device;
}

static inline struct nvk_cmd_pool *
nvk_cmd_buffer_pool(struct nvk_cmd_buffer *cmd)
{
   return (struct nvk_cmd_pool *)cmd->vk.pool;
}

void nvk_cmd_buffer_new_push(struct nvk_cmd_buffer *cmd);

#define NVK_CMD_BUFFER_MAX_PUSH 512

static inline struct nv_push *
nvk_cmd_buffer_push(struct nvk_cmd_buffer *cmd, uint32_t dw_count)
{
   assert(dw_count <= NVK_CMD_BUFFER_MAX_PUSH);

   /* Compare to the actual limit on our push bo */
   if (unlikely(cmd->push.end + dw_count > cmd->push_mem_limit))
      nvk_cmd_buffer_new_push(cmd);

   cmd->push.limit = cmd->push.end + dw_count;
   
   return &cmd->push;
}

void
nvk_cmd_buffer_push_indirect(struct nvk_cmd_buffer *cmd,
                             uint64_t addr, uint32_t dw_count);

void nvk_cmd_buffer_begin_graphics(struct nvk_cmd_buffer *cmd,
                                   const VkCommandBufferBeginInfo *pBeginInfo);
void nvk_cmd_buffer_begin_compute(struct nvk_cmd_buffer *cmd,
                                  const VkCommandBufferBeginInfo *pBeginInfo);

void nvk_cmd_invalidate_graphics_state(struct nvk_cmd_buffer *cmd);
void nvk_cmd_invalidate_compute_state(struct nvk_cmd_buffer *cmd);

void nvk_cmd_bind_shaders(struct vk_command_buffer *vk_cmd,
                          uint32_t stage_count,
                          const gl_shader_stage *stages,
                          struct vk_shader ** const shaders);

void nvk_cmd_bind_graphics_shader(struct nvk_cmd_buffer *cmd,
                                  const gl_shader_stage stage,
                                  struct nvk_shader *shader);

void nvk_cmd_bind_compute_shader(struct nvk_cmd_buffer *cmd,
                                 struct nvk_shader *shader);

void nvk_cmd_dirty_cbufs_for_descriptors(struct nvk_cmd_buffer *cmd,
                                         VkShaderStageFlags stages,
                                         uint32_t sets_start, uint32_t sets_end,
                                         uint32_t dyn_start, uint32_t dyn_end);
void nvk_cmd_bind_vertex_buffer(struct nvk_cmd_buffer *cmd, uint32_t vb_idx,
                                struct nvk_addr_range addr_range);

static inline struct nvk_descriptor_state *
nvk_get_descriptors_state(struct nvk_cmd_buffer *cmd,
                          VkPipelineBindPoint bind_point)
{
   switch (bind_point) {
   case VK_PIPELINE_BIND_POINT_GRAPHICS:
      return &cmd->state.gfx.descriptors;
   case VK_PIPELINE_BIND_POINT_COMPUTE:
      return &cmd->state.cs.descriptors;
   default:
      unreachable("Unhandled bind point");
   }
}

static inline struct nvk_descriptor_state *
nvk_get_descriptor_state_for_stages(struct nvk_cmd_buffer *cmd,
                                    VkShaderStageFlags stages)
{
   if (stages & VK_SHADER_STAGE_COMPUTE_BIT) {
      assert(stages == VK_SHADER_STAGE_COMPUTE_BIT);
      return &cmd->state.cs.descriptors;
   } else if (stages & NVK_SHADER_STAGE_GRAPHICS_BITS) {
      assert(!(stages & ~NVK_SHADER_STAGE_GRAPHICS_BITS));
      return &cmd->state.gfx.descriptors;
   } else {
      unreachable("Unknown shader stage");
   }
}

VkResult nvk_cmd_buffer_upload_alloc(struct nvk_cmd_buffer *cmd,
                                     uint32_t size, uint32_t alignment,
                                     uint64_t *addr, void **ptr);

VkResult nvk_cmd_buffer_upload_data(struct nvk_cmd_buffer *cmd,
                                    const void *data, uint32_t size,
                                    uint32_t alignment, uint64_t *addr);

VkResult nvk_cmd_buffer_cond_render_alloc(struct nvk_cmd_buffer *cmd,
					  uint64_t *addr);

void nvk_cmd_flush_wait_dep(struct nvk_cmd_buffer *cmd,
                            const VkDependencyInfo *dep,
                            bool wait);

void nvk_cmd_invalidate_deps(struct nvk_cmd_buffer *cmd,
                             uint32_t dep_count,
                             const VkDependencyInfo *deps);

void
nvk_cmd_buffer_flush_push_descriptors(struct nvk_cmd_buffer *cmd,
                                      struct nvk_descriptor_state *desc);

bool
nvk_cmd_buffer_get_cbuf_addr(struct nvk_cmd_buffer *cmd,
                             const struct nvk_descriptor_state *desc,
                             const struct nvk_shader *shader,
                             const struct nvk_cbuf *cbuf,
                             struct nvk_buffer_address *addr_out);
uint64_t
nvk_cmd_buffer_get_cbuf_descriptor_addr(struct nvk_cmd_buffer *cmd,
                                        const struct nvk_descriptor_state *desc,
                                        const struct nvk_cbuf *cbuf);

VkResult nvk_cmd_flush_cs_qmd(struct nvk_cmd_buffer *cmd,
                              uint32_t global_size[3],
                              uint64_t *qmd_addr_out,
                              uint64_t *root_desc_addr_out);

void nvk_cmd_flush_gfx_dynamic_state(struct nvk_cmd_buffer *cmd);
void nvk_cmd_flush_gfx_shaders(struct nvk_cmd_buffer *cmd);
void nvk_cmd_flush_gfx_cbufs(struct nvk_cmd_buffer *cmd);

void nvk_cmd_dispatch_shader(struct nvk_cmd_buffer *cmd,
                             struct nvk_shader *shader,
                             const void *push_data, size_t push_size,
                             uint32_t groupCountX,
                             uint32_t groupCountY,
                             uint32_t groupCountZ);

void nvk_meta_resolve_rendering(struct nvk_cmd_buffer *cmd,
                                const VkRenderingInfo *pRenderingInfo);

void nvk_cmd_buffer_dump(struct nvk_cmd_buffer *cmd, FILE *fp);

void nvk_linear_render_copy(struct nvk_cmd_buffer *cmd,
                            const struct nvk_image_view *iview,
                            VkRect2D copy_rect,
                            bool copy_to_tiled_shadow);

#endif
