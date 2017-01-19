/*
 * Copyright © 2011 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#pragma once

#include <stdint.h>
#include "blorp/blorp.h"
#include "compiler/glsl/list.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Enum for keeping track of the fast clear state of a buffer associated with
 * a miptree.
 *
 * Fast clear works by deferring the memory writes that would be used to clear
 * the buffer, so that instead of performing them at the time of the clear
 * operation, the hardware automatically performs them at the time that the
 * buffer is later accessed for rendering.  The MCS buffer keeps track of
 * which regions of the buffer still have pending clear writes.
 *
 * This enum keeps track of the driver's knowledge of pending fast clears in
 * the MCS buffer.
 *
 * MCS buffers only exist on Gen7+.
 */
enum intel_fast_clear_state
{
   /**
    * No deferred clears are pending for this miptree, and the contents of the
    * color buffer are entirely correct.  An MCS buffer may or may not exist
    * for this miptree.  If it does exist, it is entirely in the "no deferred
    * clears pending" state.  If it does not exist, it will be created the
    * first time a fast color clear is executed.
    *
    * In this state, the color buffer can be used for purposes other than
    * rendering without needing a render target resolve.
    *
    * Since there is no such thing as a "fast color clear resolve" for MSAA
    * buffers, an MSAA buffer will never be in this state.
    */
   INTEL_FAST_CLEAR_STATE_RESOLVED,

   /**
    * An MCS buffer exists for this miptree, and deferred clears are pending
    * for some regions of the color buffer, as indicated by the MCS buffer.
    * The contents of the color buffer are only correct for the regions where
    * the MCS buffer doesn't indicate a deferred clear.
    *
    * If a single-sample buffer is in this state, a render target resolve must
    * be performed before it can be used for purposes other than rendering.
    */
   INTEL_FAST_CLEAR_STATE_UNRESOLVED,

   /**
    * An MCS buffer exists for this miptree, and deferred clears are pending
    * for the entire color buffer, and the contents of the MCS buffer reflect
    * this.  The contents of the color buffer are undefined.
    *
    * If a single-sample buffer is in this state, a render target resolve must
    * be performed before it can be used for purposes other than rendering.
    *
    * If the client attempts to clear a buffer which is already in this state,
    * the clear can be safely skipped, since the buffer is already clear.
    */
   INTEL_FAST_CLEAR_STATE_CLEAR,
};

/**
 * \brief Map of miptree slices to needed resolves.
 *
 * The map is implemented as a linear doubly-linked list.
 *
 * In the intel_resolve_map*() functions, the \c head argument is not
 * inspected for its data. It only serves as an anchor for the list.
 *
 * \par Design Discussion
 *
 *     There are two possible ways to record which miptree slices need
 *     resolves. 1) Maintain a flag for every miptree slice in the texture,
 *     likely in intel_mipmap_level::slice, or 2) maintain a list of only
 *     those slices that need a resolve.
 *
 *     Immediately before drawing, a full depth resolve performed on each
 *     enabled depth texture. If design 1 were chosen, then at each draw call
 *     it would be necessary to iterate over each miptree slice of each
 *     enabled depth texture in order to query if each slice needed a resolve.
 *     In the worst case, this would require 2^16 iterations: 16 texture
 *     units, 16 miplevels, and 256 depth layers (assuming maximums for OpenGL
 *     2.1).
 *
 *     By choosing design 2, the number of iterations is exactly the minimum
 *     necessary.
 */
struct intel_resolve_map {
   struct exec_node link;

   uint32_t level;
   uint32_t layer;

   union {
      enum blorp_hiz_op need;
      enum intel_fast_clear_state fast_clear_state;
   };
};

void
intel_resolve_map_set(struct exec_list *resolve_map,
                      uint32_t level,
                      uint32_t layer,
                      unsigned new_state);

const struct intel_resolve_map *
intel_resolve_map_find_any(const struct exec_list *resolve_map,
                           uint32_t start_level, uint32_t num_levels,
                           uint32_t start_layer, uint32_t num_layers);

static inline const struct intel_resolve_map *
intel_resolve_map_const_get(const struct exec_list *resolve_map,
                            uint32_t level,
                            uint32_t layer)
{
   return intel_resolve_map_find_any(resolve_map, level, 1, layer, 1);
}

static inline struct intel_resolve_map *
intel_resolve_map_get(struct exec_list *resolve_map,
		      uint32_t level,
		      uint32_t layer)
{
   return (struct intel_resolve_map *)intel_resolve_map_find_any(
                                         resolve_map, level, 1, layer, 1);
}

void
intel_resolve_map_remove(struct intel_resolve_map *resolve_map);

void
intel_resolve_map_clear(struct exec_list *resolve_map);

#ifdef __cplusplus
} /* extern "C" */
#endif

