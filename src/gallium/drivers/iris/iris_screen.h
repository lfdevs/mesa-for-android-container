/*
 * Copyright © 2017 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef IRIS_SCREEN_H
#define IRIS_SCREEN_H

#include "pipe/p_screen.h"
#include "state_tracker/drm_driver.h"
#include "util/slab.h"
#include "util/u_screen.h"
#include "intel/dev/gen_device_info.h"
#include "intel/isl/isl.h"
#include "iris_bufmgr.h"

struct iris_bo;

#define READ_ONCE(x) (*(volatile __typeof__(x) *)&(x))
#define WRITE_ONCE(x, v) *(volatile __typeof__(x) *)&(x) = (v)

#define IRIS_MAX_TEXTURE_SAMPLERS 32
#define IRIS_MAX_SOL_BUFFERS 4
#define IRIS_MAP_BUFFER_ALIGNMENT 64

struct iris_screen {
   struct pipe_screen base;

   /** Global slab allocator for iris_transfer_map objects */
   struct slab_parent_pool transfer_pool;

   /** drm device file descriptor */
   int fd;

   /** PCI ID for our GPU device */
   int pci_id;

   bool no_hw;

   /** Global program_string_id counter (see get_program_string_id()) */
   unsigned program_id;

   /** Precompile shaders at link time?  (Can be disabled for debugging.) */
   bool precompile;

   /** driconf options and application workarounds */
   struct {
      /** Dual color blend by location instead of index (for broken apps) */
      bool dual_color_blend_by_location;
   } driconf;

   unsigned subslice_total;

   struct gen_device_info devinfo;
   struct isl_device isl_dev;
   struct iris_bufmgr *bufmgr;
   struct brw_compiler *compiler;

   /**
    * A buffer containing nothing useful, for hardware workarounds that
    * require scratch writes or reads from some unimportant memory.
    */
   struct iris_bo *workaround_bo;
};

struct pipe_screen *
iris_screen_create(int fd, const struct pipe_screen_config *config);

boolean
iris_is_format_supported(struct pipe_screen *pscreen,
                         enum pipe_format format,
                         enum pipe_texture_target target,
                         unsigned sample_count,
                         unsigned storage_sample_count,
                         unsigned usage);

#endif
