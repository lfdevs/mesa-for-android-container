/*
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RADV_PIPELINE_LAYOUT_H
#define RADV_PIPELINE_LAYOUT_H

#include "util/mesa-blake3.h"

#include "radv_constants.h"

#include "vk_object.h"

struct radv_device;

struct radv_pipeline_layout {
   struct vk_object_base base;
   struct {
      struct radv_descriptor_set_layout *layout;
      uint32_t dynamic_offset_start;
   } set[MAX_SETS];

   uint32_t num_sets;
   uint32_t dynamic_offset_count;
   uint16_t dynamic_shader_stages;

   bool independent_sets;

   blake3_hash hash;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(radv_pipeline_layout, base, VkPipelineLayout, VK_OBJECT_TYPE_PIPELINE_LAYOUT)

void radv_pipeline_layout_init(struct radv_device *device, struct radv_pipeline_layout *layout, bool independent_sets);
void radv_pipeline_layout_finish(struct radv_device *device, struct radv_pipeline_layout *layout);

void radv_pipeline_layout_add_set(struct radv_pipeline_layout *layout, uint32_t set_idx,
                                  struct radv_descriptor_set_layout *set_layout);
void radv_pipeline_layout_hash(struct radv_pipeline_layout *layout);

#endif /* RADV_PIPELINE_LAYOUT_H */
