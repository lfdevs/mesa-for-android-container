/*
 * Copyright 2003 VMware, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _INTEL_INIT_H_
#define _INTEL_INIT_H_

#include <stdbool.h>
#include <sys/time.h>

#include <GL/internal/dri_interface.h>

#include "dri_util.h"
#include "intel_bufmgr.h"
#include "common/gen_device_info.h"
#include "i915_drm.h"
#include "xmlconfig.h"

struct intel_screen
{
   int deviceID;
   struct gen_device_info devinfo;

   __DRIscreen *driScrnPriv;

   uint64_t max_gtt_map_object_size;

   bool no_hw;

   bool hw_has_swizzling;

   int hw_has_timestamp;

   /**
    * Does the kernel support resource streamer?
    */
   bool has_resource_streamer;

   /**
    * Does the kernel support context reset notifications?
    */
   bool has_context_reset_notification;

   /**
    * Does the kernel support features such as pipelined register access to
    * specific registers?
    */
   unsigned kernel_features;
#define KERNEL_ALLOWS_SOL_OFFSET_WRITES             (1<<0)
#define KERNEL_ALLOWS_PREDICATE_WRITES              (1<<1)
#define KERNEL_ALLOWS_MI_MATH_AND_LRR               (1<<2)
#define KERNEL_ALLOWS_HSW_SCRATCH1_AND_ROW_CHICKEN3 (1<<3)
#define KERNEL_ALLOWS_COMPUTE_DISPATCH              (1<<4)

   dri_bufmgr *bufmgr;

   /**
    * A unique ID for shader programs.
    */
   unsigned program_id;

   int winsys_msaa_samples_override;

   struct brw_compiler *compiler;

   /**
   * Configuration cache with default values for all contexts
   */
   driOptionCache optionCache;

   /**
    * Version of the command parser reported by the
    * I915_PARAM_CMD_PARSER_VERSION parameter
    */
   int cmd_parser_version;

   /**
    * Number of subslices reported by the I915_PARAM_SUBSLICE_TOTAL parameter
    */
   int subslice_total;

   /**
    * Number of EUs reported by the I915_PARAM_EU_TOTAL parameter
    */
   int eu_total;
};

extern void intelDestroyContext(__DRIcontext * driContextPriv);

extern GLboolean intelUnbindContext(__DRIcontext * driContextPriv);

PUBLIC const __DRIextension **__driDriverGetExtensions_i965(void);
extern const __DRI2fenceExtension intelFenceExtension;

extern GLboolean
intelMakeCurrent(__DRIcontext * driContextPriv,
                 __DRIdrawable * driDrawPriv,
                 __DRIdrawable * driReadPriv);

double get_time(void);
void aub_dump_bmp(struct gl_context *ctx);

const int*
intel_supported_msaa_modes(const struct intel_screen  *screen);

static inline bool
can_do_pipelined_register_writes(const struct intel_screen *screen)
{
   return screen->kernel_features & KERNEL_ALLOWS_SOL_OFFSET_WRITES;
}

static inline bool
can_do_hsw_l3_atomics(const struct intel_screen *screen)
{
   return screen->kernel_features & KERNEL_ALLOWS_HSW_SCRATCH1_AND_ROW_CHICKEN3;
}

static inline bool
can_do_mi_math_and_lrr(const struct intel_screen *screen)
{
   return screen->kernel_features & KERNEL_ALLOWS_MI_MATH_AND_LRR;
}

static inline bool
can_do_compute_dispatch(const struct intel_screen *screen)
{
   return screen->kernel_features & KERNEL_ALLOWS_COMPUTE_DISPATCH;
}

static inline bool
can_do_predicate_writes(const struct intel_screen *screen)
{
   return screen->kernel_features & KERNEL_ALLOWS_PREDICATE_WRITES;
}

#endif
