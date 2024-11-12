/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#ifndef NVK_EDB_BVIEW_CACHE_H
#define NVK_EDB_BVIEW_CACHE_H 1

#include "nvk_private.h"

#include "nvk_descriptor_types.h"

#include "util/format/u_formats.h"

struct hash_table;
struct nvk_device;

/** A cache of VK_EXT_descriptor_buffer BufferViews
 *
 * VK_EXT_descriptor_buffer effectively removes the concept of a VkBufferView
 * object.  Instead of allocating a view object and passing that into
 * vkGetDescriptorEXT() like you do for image views, typed buffers work more
 * like untyped UBOs or SSBOs and you just pass a base address, size (in
 * bytes) and format to vkGetDescriptorEXT().  On NVIDIA hardware, this is
 * annoying because it means we no longer have an object to help us manage the
 * life cycle of the descriptor on the heap.
 *
 * The solution is nvk_edb_bview_cache.  This cache stores enough typed buffer
 * descriptors to cover the entire address space.  For each buffer format, we
 * allocate 512 4 GiB buffer views, spaced at 2 GiB intervals.  This ensures
 * that every client buffer view will live entirely inside one of these views.
 * The descriptor we return from vkGetDescriptorEXT() contains the descriptor
 * index to the HW descriptor as well as an offset and size (both in surface
 * elements) and the alpha value to expect for OOB writes.
 *
 * For RGB32 formats, we place 3 3 GiB buffer views every 1.5 GiB in the
 * address space.  We need 3 per chunk because RGB32 buffer views only have a
 * minimum alignment of 4B but the offsetting we do in the shader is in terms
 * of surface elements.  For offsetting by 1 or 2 components, we need a
 * different view.  The reason why it's 3 GiB instead of 4 GiB is because
 * Ampere reduced the maximum size of an RGB32 buffer view to 3 GiB.
 *
 * In nvk_nir_lower_descriptors(), we lower all texture or image buffer access
 * to an access through one of these HW descriptors.  Bounds checkinig is done
 * in software and the offset is applied to ensure that we only ever read from
 * the memory range specified by the client.  The HW descriptor only exists to
 * help with format conversion.
 */
struct nvk_edb_bview_cache {
   struct hash_table *cache;
};

VkResult
nvk_edb_bview_cache_init(struct nvk_device *dev,
                         struct nvk_edb_bview_cache *cache);

/* It's safe to call this function on a zeroed nvk_edb_bview_cache */
void
nvk_edb_bview_cache_finish(struct nvk_device *dev,
                           struct nvk_edb_bview_cache *cache);

struct nvk_edb_buffer_view_descriptor
nvk_edb_bview_cache_get_descriptor(struct nvk_device *dev,
                                   struct nvk_edb_bview_cache *cache,
                                   uint64_t base_addr, uint64_t size_B,
                                   enum pipe_format format);

#endif /* NVK_EDB_BVIEW_CACHE_H */
