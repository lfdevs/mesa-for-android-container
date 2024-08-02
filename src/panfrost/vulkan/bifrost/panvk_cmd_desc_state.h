/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_CMD_DESC_STATE_H
#define PANVK_CMD_DESC_STATE_H

#ifndef PAN_ARCH
#error "PAN_ARCH must be defined"
#endif

#include <stdint.h>

#include "genxml/gen_macros.h"

#include "panvk_descriptor_set.h"
#include "panvk_macros.h"
#include "panvk_shader.h"

#include "vk_command_buffer.h"

#include "pan_pool.h"

struct panvk_shader_desc_state {
   mali_ptr tables[PANVK_BIFROST_DESC_TABLE_COUNT];
   mali_ptr img_attrib_table;
   mali_ptr dyn_ssbos;
};

struct panvk_descriptor_state {
   const struct panvk_descriptor_set *sets[MAX_SETS];
   struct panvk_descriptor_set *push_sets[MAX_SETS];

   uint32_t dyn_buf_offsets[MAX_SETS][MAX_DYNAMIC_BUFFERS];
};

void panvk_per_arch(cmd_desc_state_reset)(
   struct panvk_descriptor_state *gfx_desc_state,
   struct panvk_descriptor_state *compute_desc_state);

void panvk_per_arch(cmd_desc_state_cleanup)(
   struct vk_command_buffer *cmdbuf,
   struct panvk_descriptor_state *gfx_desc_state,
   struct panvk_descriptor_state *compute_desc_state);

void panvk_per_arch(cmd_desc_state_bind_sets)(
   struct panvk_descriptor_state *desc_state, VkPipelineLayout layout,
   uint32_t first_set, uint32_t desc_set_count,
   const VkDescriptorSet *desc_sets, uint32_t dyn_offset_count,
   const uint32_t *dyn_offsets);

struct panvk_descriptor_set *panvk_per_arch(cmd_push_descriptors)(
   struct vk_command_buffer *cmdbuf, struct panvk_descriptor_state *desc_state,
   uint32_t set);

void panvk_per_arch(cmd_prepare_dyn_ssbos)(
   struct pan_pool *desc_pool, const struct panvk_descriptor_state *desc_state,
   const struct panvk_shader *shader,
   struct panvk_shader_desc_state *shader_desc_state);

void panvk_per_arch(cmd_prepare_shader_desc_tables)(
   struct pan_pool *desc_pool, const struct panvk_descriptor_state *desc_state,
   const struct panvk_shader *shader,
   struct panvk_shader_desc_state *shader_desc_state);

void panvk_per_arch(cmd_prepare_push_descs)(
   struct pan_pool *desc_pool, struct panvk_descriptor_state *desc_state,
   uint32_t used_set_mask);

#endif
