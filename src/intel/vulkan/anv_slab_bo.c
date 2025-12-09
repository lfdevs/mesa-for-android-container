/* Copyright © 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "anv_slab_bo.h"

enum anv_bo_slab_heap {
   ANV_BO_SLAB_HEAP_CACHED_COHERENT_CAPTURE, /* main usage is batch buffers but other buffers also matches */
   ANV_BO_SLAB_HEAP_DYNAMIC_VISIBLE_POOL,
   ANV_BO_SLAB_HEAP_DESCRIPTOR_POOL,
   ANV_BO_SLAB_HEAP_SMEM_CACHED_COHERENT,
   ANV_BO_SLAB_HEAP_SMEM_CACHED_INCOHERENT,
   ANV_BO_SLAB_HEAP_SMEM_COHERENT,
   ANV_BO_SLAB_HEAP_COMPRESSED, /* used by integrated and discrete GPUs */
   ANV_BO_SLAB_HEAP_LMEM_SMEM,
   ANV_BO_SLAB_HEAP_LMEM_ONLY,
   ANV_BO_SLAB_NOT_SUPPORTED,
};

struct anv_slab {
   struct pb_slab base;

   /** The BO representing the entire slab */
   struct anv_bo *bo;

   /** Array of anv_bo structs representing BOs allocated out of this slab */
   struct anv_bo *entries;
};

static enum anv_bo_slab_heap
anv_bo_alloc_flags_to_slab_heap(struct anv_device *device,
                                enum anv_bo_alloc_flags alloc_flags)
{
   enum anv_bo_alloc_flags not_supported = ANV_BO_ALLOC_32BIT_ADDRESS |
                                           ANV_BO_ALLOC_EXTERNAL |
                                           ANV_BO_ALLOC_CAPTURE |
                                           ANV_BO_ALLOC_FIXED_ADDRESS |
                                           ANV_BO_ALLOC_CLIENT_VISIBLE_ADDRESS |
                                           ANV_BO_ALLOC_DESCRIPTOR_POOL |
                                           ANV_BO_ALLOC_LOCAL_MEM_CPU_VISIBLE |
                                           ANV_BO_ALLOC_SCANOUT |
                                           ANV_BO_ALLOC_PROTECTED |
                                           ANV_BO_ALLOC_DYNAMIC_VISIBLE_POOL |
                                           ANV_BO_ALLOC_IMPORTED |
                                           ANV_BO_ALLOC_SLAB_PARENT;

   if (device->info->kmd_type == INTEL_KMD_TYPE_I915) {
      not_supported |= (ANV_BO_ALLOC_IMPLICIT_SYNC |
                        ANV_BO_ALLOC_IMPLICIT_WRITE);
   }

   if (alloc_flags == ANV_BO_ALLOC_BATCH_BUFFER_FLAGS ||
       alloc_flags == ANV_BO_ALLOC_BATCH_BUFFER_INTERNAL_FLAGS)
      return ANV_BO_SLAB_HEAP_CACHED_COHERENT_CAPTURE;

   if (alloc_flags == ANV_BO_ALLOC_DYNAMIC_VISIBLE_POOL_FLAGS)
      return ANV_BO_SLAB_HEAP_DYNAMIC_VISIBLE_POOL;

   if (alloc_flags == ANV_BO_ALLOC_DESCRIPTOR_POOL_FLAGS)
      return ANV_BO_SLAB_HEAP_DESCRIPTOR_POOL;

   if (alloc_flags & not_supported)
      return ANV_BO_SLAB_NOT_SUPPORTED;

   if (alloc_flags & ANV_BO_ALLOC_COMPRESSED)
      return ANV_BO_SLAB_HEAP_COMPRESSED;

   if (anv_physical_device_has_vram(device->physical)) {
      if (alloc_flags & ANV_BO_ALLOC_NO_LOCAL_MEM)
         return ANV_BO_SLAB_HEAP_SMEM_CACHED_COHERENT;
      if (alloc_flags & (ANV_BO_ALLOC_MAPPED | ANV_BO_ALLOC_LOCAL_MEM_CPU_VISIBLE))
         return ANV_BO_SLAB_HEAP_LMEM_SMEM;
      return ANV_BO_SLAB_HEAP_LMEM_ONLY;
   }

   if ((alloc_flags & ANV_BO_ALLOC_HOST_CACHED_COHERENT) == ANV_BO_ALLOC_HOST_CACHED_COHERENT)
      return ANV_BO_SLAB_HEAP_SMEM_CACHED_COHERENT;
   if (alloc_flags & ANV_BO_ALLOC_HOST_CACHED)
      return ANV_BO_SLAB_HEAP_SMEM_CACHED_INCOHERENT;
   return ANV_BO_SLAB_HEAP_SMEM_COHERENT;
}

/* Return the power of two size of a slab entry matching the input size. */
static unsigned
get_slab_pot_entry_size(struct anv_device *device, unsigned size)
{
   unsigned entry_size = util_next_power_of_two(size);
   unsigned min_entry_size = 1 << device->bo_slabs[0].min_order;

   return MAX2(entry_size, min_entry_size);
}

static struct pb_slabs *
get_slabs(struct anv_device *device, uint64_t size)
{
   const unsigned num_slab_allocator = ARRAY_SIZE(device->bo_slabs);

   for (unsigned i = 0; i < num_slab_allocator; i++) {
      struct pb_slabs *slabs = &device->bo_slabs[i];

      if (size <= (1ull << (slabs->min_order + slabs->num_orders - 1)))
         return slabs;
   }

   UNREACHABLE("should have found a valid slab for this size");
   return NULL;
}

static inline bool
anv_slab_bo_is_disabled(struct anv_device *device)
{
   return device->bo_slabs[0].num_heaps == 0;
}

struct anv_bo *
anv_slab_bo_alloc(struct anv_device *device, const char *name, uint64_t requested_size,
                  uint32_t alignment, enum anv_bo_alloc_flags alloc_flags)
{
   if (anv_slab_bo_is_disabled(device))
      return NULL;

   const enum anv_bo_slab_heap slab_heap = anv_bo_alloc_flags_to_slab_heap(device, alloc_flags);
   if (slab_heap == ANV_BO_SLAB_NOT_SUPPORTED)
      return NULL;

   /* Don't always use slab if AUX_TT_ALIGNED is required and AUX alignment is
    * >= 1MB, enabling this causes a high memory consumption that causes out
    * of memory when running several parallel GPU applications.
    */
   if ((alloc_flags & ANV_BO_ALLOC_AUX_TT_ALIGNED) &&
       (intel_aux_map_get_alignment(device->aux_map_ctx) >= 1024 * 1024) &&
       (requested_size < (1024 * 1024 / 2)))
       return NULL;

   const unsigned num_slab_allocator = ARRAY_SIZE(device->bo_slabs);
   struct pb_slabs *last_slab = &device->bo_slabs[num_slab_allocator - 1];
   const uint64_t max_slab_entry_size = BITFIELD64_BIT(last_slab->min_order + last_slab->num_orders - 1);

   if (requested_size > max_slab_entry_size)
      return NULL;

   uint64_t alloc_size = MAX2(alignment, requested_size);
   alloc_size = get_slab_pot_entry_size(device, alloc_size);

   if (alloc_size > max_slab_entry_size)
         return NULL;

   struct pb_slabs *slabs = get_slabs(device, alloc_size);
   struct pb_slab_entry *entry = pb_slab_alloc(slabs, alloc_size, slab_heap);
   if (!entry) {
      /* Clean up and try again... */
      pb_slabs_reclaim(slabs);

      entry = pb_slab_alloc(slabs, alloc_size, slab_heap);
   }
   if (!entry)
      return NULL;

   struct anv_bo *bo = container_of(entry, struct anv_bo, slab_entry);
   bo->name = name;
   bo->refcount = 1;
   bo->size = requested_size;
   bo->alloc_flags = alloc_flags;
   bo->flags = device->kmd_backend->bo_alloc_flags_to_bo_flags(device, alloc_flags);

   assert(bo->flags == bo->slab_parent->flags);
   assert((intel_48b_address(bo->offset) & (alignment - 1)) == 0);

   if (alloc_flags & ANV_BO_ALLOC_MAPPED) {
      if (anv_device_map_bo(device, bo, 0, bo->size, NULL, &bo->map) != VK_SUCCESS) {
         anv_slab_bo_free(device, bo);
         return NULL;
      }
   }

   return bo;
}

void
anv_slab_bo_free(struct anv_device *device, struct anv_bo *bo)
{
   assert(bo->slab_parent);

   if (bo->map) {
      anv_device_unmap_bo(device, bo, bo->map, bo->size, false /* replace */);
      bo->map = NULL;
   }

   bo->refcount = 0;
   pb_slab_free(get_slabs(device, bo->size), &bo->slab_entry);
}

static unsigned heap_max_get(struct anv_device *device)
{
   unsigned ret;

   if (anv_physical_device_has_vram(device->physical))
      ret = ANV_BO_SLAB_HEAP_LMEM_ONLY;
   else
      ret = device->info->verx10 >= 200 ? ANV_BO_SLAB_HEAP_COMPRESSED :
                                          ANV_BO_SLAB_HEAP_SMEM_COHERENT;

   return (ret + 1);
}

static bool
anv_can_reclaim_slab(void *priv, struct pb_slab_entry *entry)
{
   struct anv_bo *bo = container_of(entry, struct anv_bo, slab_entry);

   return p_atomic_read(&bo->refcount) == 0;
}

static struct pb_slab *
anv_slab_alloc(void *priv,
               unsigned heap,
               unsigned entry_size,
               unsigned group_index)
{
   struct anv_device *device = priv;
   struct anv_slab *slab = calloc(1, sizeof(struct anv_slab));

   if (!slab)
      return NULL;

   const enum anv_bo_slab_heap bo_slab_heap = heap;
   enum anv_bo_alloc_flags alloc_flags = ANV_BO_ALLOC_SLAB_PARENT;

   switch (bo_slab_heap) {
   case ANV_BO_SLAB_HEAP_SMEM_CACHED_COHERENT:
      alloc_flags |= ANV_BO_ALLOC_HOST_CACHED_COHERENT |
                     ANV_BO_ALLOC_NO_LOCAL_MEM;
      break;
   case ANV_BO_SLAB_HEAP_SMEM_CACHED_INCOHERENT:
      alloc_flags |= ANV_BO_ALLOC_HOST_CACHED |
                     ANV_BO_ALLOC_NO_LOCAL_MEM;
      break;
   case ANV_BO_SLAB_HEAP_SMEM_COHERENT:
      alloc_flags |= ANV_BO_ALLOC_HOST_COHERENT |
                     ANV_BO_ALLOC_NO_LOCAL_MEM;
      break;
   case ANV_BO_SLAB_HEAP_COMPRESSED:
      alloc_flags |= ANV_BO_ALLOC_COMPRESSED;
      break;
   case ANV_BO_SLAB_HEAP_LMEM_SMEM:
      alloc_flags |= ANV_BO_ALLOC_MAPPED |
                     ANV_BO_ALLOC_HOST_COHERENT;
      break;
   case ANV_BO_SLAB_HEAP_LMEM_ONLY:
      break;
   case ANV_BO_SLAB_HEAP_CACHED_COHERENT_CAPTURE:
      alloc_flags |= ANV_BO_ALLOC_BATCH_BUFFER_FLAGS;
      break;
   case ANV_BO_SLAB_HEAP_DYNAMIC_VISIBLE_POOL:
      alloc_flags |= ANV_BO_ALLOC_DYNAMIC_VISIBLE_POOL_FLAGS;
      break;
   case ANV_BO_SLAB_HEAP_DESCRIPTOR_POOL:
      alloc_flags |= ANV_BO_ALLOC_DESCRIPTOR_POOL_FLAGS;
      break;
   default:
      UNREACHABLE("Missing");
      return NULL;
   }

   struct pb_slabs *slabs = get_slabs(device, entry_size);

   entry_size = MAX2(entry_size, 1ULL << slabs->min_order);
   if (!util_is_power_of_two_nonzero(entry_size))
      entry_size = util_next_power_of_two(entry_size);

   unsigned slab_parent_size = entry_size * 8;
   /* allocate at least a 2MB buffer, this allows KMD to enable THP for this bo */
   slab_parent_size = MAX2(slab_parent_size, 2 * 1024 * 1024);

   VkResult result;
   result = anv_device_alloc_bo(device, "slab_parent", slab_parent_size, alloc_flags,
                                0, &slab->bo);
   if (result != VK_SUCCESS)
      goto error_alloc_bo;

   slab_parent_size = slab->bo->size = slab->bo->actual_size;
   slab->base.num_entries = slab_parent_size / entry_size;
   slab->base.num_free = slab->base.num_entries;
   slab->base.group_index = group_index;
   slab->base.entry_size = entry_size;
   slab->entries = calloc(slab->base.num_entries, sizeof(*slab->entries));
   if (!slab->entries)
      goto error_alloc_entries;

   list_inithead(&slab->base.free);

   for (unsigned i = 0; i < slab->base.num_entries; i++) {
      struct anv_bo *bo = &slab->entries[i];
      uint64_t offset = intel_48b_address(slab->bo->offset);

      offset += ((uint64_t)i * entry_size);

      bo->name = "slab_child";
      bo->gem_handle = slab->bo->gem_handle;
      bo->refcount = 0;
      bo->offset = intel_canonical_address(offset);
      bo->size = entry_size;
      bo->actual_size = entry_size;
      bo->alloc_flags = alloc_flags;
      bo->vma_heap = slab->bo->vma_heap;
      bo->slab_parent = slab->bo;
      bo->slab_entry.slab = &slab->base;

      list_addtail(&bo->slab_entry.head, &slab->base.free);
   }

   return &slab->base;

error_alloc_entries:
   anv_device_release_bo(device, slab->bo);
error_alloc_bo:
   free(slab);

   return NULL;
}

static void
anv_slab_free(void *priv, struct pb_slab *pslab)
{
   struct anv_device *device = priv;
   struct anv_slab *slab = (void *)pslab;

   anv_device_release_bo(device, slab->bo);

   free(slab->entries);
   free(slab);
}

bool
anv_slab_bo_init(struct anv_device *device)
{
   const unsigned num_slab_allocator = ARRAY_SIZE(device->bo_slabs);
   unsigned min_slab_order = 8;/* 256 bytes */
   const unsigned max_slab_order = 20;/* 1 MB (slab size = 2 MB) */
   unsigned num_slab_orders_per_allocator = (max_slab_order - min_slab_order) /
                                            num_slab_allocator;

   if (unlikely(device->physical->instance->debug & ANV_DEBUG_NO_SLAB))
      return true;

   /* feature requirement */
   if (!device->info->has_mmap_offset || !device->info->has_partial_mmap_offset)
      return true;

   /* Divide the size order range among slab managers. */
   for (unsigned i = 0; i < num_slab_allocator; i++) {
      const unsigned min_order = min_slab_order;
      const unsigned max_order = MIN2(min_order + num_slab_orders_per_allocator,
                                      max_slab_order);

      if (!pb_slabs_init(&device->bo_slabs[i], min_order, max_order,
                         heap_max_get(device), false, device,
                         anv_can_reclaim_slab,
                         anv_slab_alloc,
                         anv_slab_free)) {
         goto error_slabs_init;
      }
      min_slab_order = max_order + 1;
   }

   return true;

error_slabs_init:
   for (unsigned i = 0; i < num_slab_allocator; i++) {
      if (!device->bo_slabs[i].groups)
         break;

      pb_slabs_deinit(&device->bo_slabs[i]);
   }

   return false;
}

void
anv_slab_bo_deinit(struct anv_device *device)
{
   const unsigned num_slab_allocator = ARRAY_SIZE(device->bo_slabs);

   if (anv_slab_bo_is_disabled(device))
      return;

   for (int i = 0; i < num_slab_allocator; i++) {
      if (device->bo_slabs[i].groups)
         pb_slabs_deinit(&device->bo_slabs[i]);
   }
}
