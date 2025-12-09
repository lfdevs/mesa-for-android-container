/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 */

#ifndef TU_LRZ_H
#define TU_LRZ_H

#include "tu_common.h"

enum tu_lrz_force_disable_mask {
   TU_LRZ_FORCE_DISABLE_LRZ = 1 << 0,
   TU_LRZ_FORCE_DISABLE_WRITE = 1 << 1,
   TU_LRZ_READS_DEST = 1 << 2,              /* Blend/logicop/colormask, etc */
};

enum tu_lrz_direction {
   TU_LRZ_UNKNOWN,
   /* Depth func less/less-than: */
   TU_LRZ_LESS,
   /* Depth func greater/greater-than: */
   TU_LRZ_GREATER,
};

struct tu_lrz_state
{
   /* Depth/Stencil image currently on use to do LRZ */
   const struct tu_image_view *image_view;
   VkClearValue depth_clear_value;
   /* If LRZ is in invalid state we cannot use it until depth is cleared */
   bool valid : 1;
   /* Being invalid at the very start means ew could e.g. skip the clearing. */
   bool valid_at_start: 1;

   /* Sticky for the RP duration */
   bool disable_write_for_rp : 1;

   /* Allows to temporary disable LRZ */
   bool enabled : 1;
   bool fast_clear : 1;
   bool gpu_dir_tracking : 1;
   bool force_late_z : 1;
   /* Continue using old LRZ state (LOAD_OP_LOAD of depth) */
   bool reuse_previous_state : 1;
   bool gpu_dir_set : 1;

   bool color_written_with_z_test : 1;
   bool has_lrz_write_with_skipped_color_writes : 1;

   bool store : 1;

   enum tu_lrz_direction prev_direction;
};

template <chip CHIP>
void
tu6_emit_lrz(struct tu_cmd_buffer *cmd, struct tu_cs *cs);

template <chip CHIP>
void
tu_disable_lrz(struct tu_cmd_buffer *cmd, struct tu_cs *cs,
               struct tu_image *image);

template <chip CHIP>
void
tu_disable_lrz_cpu(struct tu_device *device, struct tu_image *image);

template <chip CHIP>
void
tu_lrz_clear_depth_image(struct tu_cmd_buffer *cmd,
                         struct tu_image *image,
                         const VkClearDepthStencilValue *pDepthStencil,
                         uint32_t rangeCount,
                         const VkImageSubresourceRange *pRanges);

template <chip CHIP>
void
tu_lrz_begin_renderpass(struct tu_cmd_buffer *cmd);

template <chip CHIP>
void
tu_lrz_begin_resumed_renderpass(struct tu_cmd_buffer *cmd);

void
tu_lrz_begin_secondary_cmdbuf(struct tu_cmd_buffer *cmd);

void
tu_lrz_cb_begin(struct tu_cmd_buffer *cmd, struct tu_cs *cs);

template <chip CHIP>
void
tu_lrz_tiling_begin(struct tu_cmd_buffer *cmd, struct tu_cs *cs);

template <chip CHIP>
void
tu_lrz_after_bv(struct tu_cmd_buffer *cmd, struct tu_cs *cs);

template <chip CHIP>
void
tu_lrz_before_tiles(struct tu_cmd_buffer *cmd, struct tu_cs *cs, bool use_cb);

template <chip CHIP>
void
tu_lrz_before_tile(struct tu_cmd_buffer *cmd, struct tu_cs *cs);

template <chip CHIP>
void
tu_lrz_before_sysmem_br(struct tu_cmd_buffer *cmd, struct tu_cs *cs);

template <chip CHIP>
void
tu_lrz_tiling_end(struct tu_cmd_buffer *cmd, struct tu_cs *cs);

template <chip CHIP>
void
tu_lrz_sysmem_begin(struct tu_cmd_buffer *cmd, struct tu_cs *cs);

template <chip CHIP>
void
tu_lrz_sysmem_end(struct tu_cmd_buffer *cmd, struct tu_cs *cs);

template <chip CHIP>
void
tu_lrz_disable_during_renderpass(struct tu_cmd_buffer *cmd,
                                 const char *reason);

template <chip CHIP>
void
tu_lrz_flush_valid_during_renderpass(struct tu_cmd_buffer *cmd,
                                     struct tu_cs *cs);

void
tu_lrz_disable_write_for_rp(struct tu_cmd_buffer *cmd, const char *reason);

#endif /* TU_LRZ_H */
