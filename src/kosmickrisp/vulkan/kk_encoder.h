/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef KK_ENCODER_H
#define KK_ENCODER_H 1

#include "kosmickrisp/bridge/mtl_types.h"

#include "util/u_dynarray.h"

#include "vulkan/vulkan.h"

struct kk_queue;
struct kk_cmd_buffer;

enum kk_encoder_type {
   KK_ENC_NONE = 0,
   KK_ENC_RENDER = BITFIELD_BIT(0),
   KK_ENC_COMPUTE = BITFIELD_BIT(1),
   KK_ENC_BLIT = BITFIELD_BIT(2),
   KK_ENC_ALL = (KK_ENC_RENDER | KK_ENC_COMPUTE | KK_ENC_BLIT),
   KK_ENC_COUNT = 3u,
};

struct kk_encoder_internal {
   mtl_command_buffer *cmd_buffer;
   mtl_command_encoder *encoder;

   /* Used to know if we need to make heaps resident again */
   uint32_t user_heap_hash;

   /* Need to track last used to we can converge at submission */
   enum kk_encoder_type last_used;

   /* Used to synchronize between passes inside the same command buffer */
   struct util_dynarray fences;
   /* Tracks if we need to wait on the last fence present in fences at the start
    * of the pass */
   bool wait_fence;
};

struct kk_copy_query_pool_results_info {
   uint64_t availability;
   uint64_t results;
   uint64_t indices;
   uint64_t dst_addr;
   uint64_t dst_stride;
   uint32_t first_query;
   VkQueryResultFlagBits flags;
   uint16_t reports_per_query;
   uint32_t query_count;
};

struct kk_encoder {
   mtl_device *dev;
   struct kk_encoder_internal main;
   /* Compute only for pre gfx required work */
   struct kk_encoder_internal pre_gfx;

   /* Used to synchronize between main and pre_gfx encoders */
   mtl_event *event;
   uint64_t event_value;
   /* Track what values pre_gfx must wait/signal before starting the encoding */
   uint64_t wait_value_pre_gfx;
   uint64_t signal_value_pre_gfx;

   /* uint64_t pairs with first being the address, second being the value to
    * write */
   struct util_dynarray imm_writes;
   /* Array of kk_copy_quer_pool_results_info structs */
   struct util_dynarray copy_query_pool_result_infos;
};

/* Allocates encoder and initialises/creates all resources required to start
 * recording commands into the multiple encoders */
VkResult kk_encoder_init(mtl_device *device, struct kk_queue *queue,
                         struct kk_encoder **encoder);

/* Submits all command buffers and releases encoder memory. Requires all command
 * buffers in the encoder to be linked to the last one used so the post
 * execution callback is called once all are done */
void kk_encoder_submit(struct kk_encoder *encoder);

mtl_render_encoder *
kk_encoder_start_render(struct kk_cmd_buffer *cmd,
                        mtl_render_pass_descriptor *descriptor,
                        uint32_t view_mask);

mtl_compute_encoder *kk_encoder_start_compute(struct kk_cmd_buffer *cmd);

mtl_compute_encoder *kk_encoder_start_blit(struct kk_cmd_buffer *cmd);

/* Ends encoding on all command buffers */
void kk_encoder_end(struct kk_cmd_buffer *cmd);

/* Creates a fence and signals it inside the encoder, then ends encoding */
void kk_encoder_signal_fence_and_end(struct kk_cmd_buffer *cmd);

mtl_render_encoder *kk_render_encoder(struct kk_cmd_buffer *cmd);

mtl_compute_encoder *kk_compute_encoder(struct kk_cmd_buffer *cmd);

mtl_blit_encoder *kk_blit_encoder(struct kk_cmd_buffer *cmd);

struct kk_encoder_internal *kk_encoder_get_internal(struct kk_encoder *encoder,
                                                    enum kk_encoder_type type);

void upload_queue_writes(struct kk_cmd_buffer *cmd);

void kk_encoder_render_triangle_fan_indirect(struct kk_cmd_buffer *cmd,
                                             mtl_buffer *indirect,
                                             uint64_t offset);

void kk_encoder_render_triangle_fan_indexed_indirect(struct kk_cmd_buffer *cmd,
                                                     mtl_buffer *indirect,
                                                     uint64_t offset,
                                                     bool increase_el_size);

#endif /* KK_ENCODER_H */
