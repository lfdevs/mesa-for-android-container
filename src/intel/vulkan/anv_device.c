/*
 * Copyright © 2015 Intel Corporation
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

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <fcntl.h>
#include "drm-uapi/drm_fourcc.h"
#include "drm-uapi/drm.h"
#include <xf86drm.h>

#include "anv_private.h"
#include "anv_measure.h"
#include "util/u_debug.h"
#include "util/os_file.h"
#include "util/os_misc.h"
#include "util/u_atomic.h"
#if DETECT_OS_ANDROID
#include "util/u_gralloc/u_gralloc.h"
#endif
#include "util/u_string.h"
#include "vk_common_entrypoints.h"
#include "vk_util.h"
#include "vk_deferred_operation.h"
#include "vk_drm_syncobj.h"
#include "common/intel_aux_map.h"
#include "common/intel_common.h"
#include "common/intel_debug_identifier.h"

#include "i915/anv_device.h"
#include "xe/anv_device.h"

#include "genxml/gen7_pack.h"
#include "genxml/genX_bits.h"

static void
anv_device_init_border_colors(struct anv_device *device)
{
   static const struct gfx8_border_color border_colors[] = {
      [VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK] =  { .float32 = { 0.0, 0.0, 0.0, 0.0 } },
      [VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK] =       { .float32 = { 0.0, 0.0, 0.0, 1.0 } },
      [VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE] =       { .float32 = { 1.0, 1.0, 1.0, 1.0 } },
      [VK_BORDER_COLOR_INT_TRANSPARENT_BLACK] =    { .uint32 = { 0, 0, 0, 0 } },
      [VK_BORDER_COLOR_INT_OPAQUE_BLACK] =         { .uint32 = { 0, 0, 0, 1 } },
      [VK_BORDER_COLOR_INT_OPAQUE_WHITE] =         { .uint32 = { 1, 1, 1, 1 } },
   };

   device->border_colors =
      anv_state_pool_emit_data(&device->dynamic_state_pool,
                               sizeof(border_colors), 64, border_colors);
}

static VkResult
anv_device_init_trivial_batch(struct anv_device *device)
{
   VkResult result = anv_device_alloc_bo(device, "trivial-batch", 4096,
                                         ANV_BO_ALLOC_MAPPED |
                                         ANV_BO_ALLOC_HOST_COHERENT |
                                         ANV_BO_ALLOC_INTERNAL |
                                         ANV_BO_ALLOC_CAPTURE,
                                         0 /* explicit_address */,
                                         &device->trivial_batch_bo);
   if (result != VK_SUCCESS)
      return result;

   struct anv_batch batch = {
      .start = device->trivial_batch_bo->map,
      .next = device->trivial_batch_bo->map,
      .end = device->trivial_batch_bo->map + 4096,
   };

   anv_batch_emit(&batch, GFX7_MI_BATCH_BUFFER_END, bbe);
   anv_batch_emit(&batch, GFX7_MI_NOOP, noop);

   return VK_SUCCESS;
}

static bool
get_bo_from_pool(struct intel_batch_decode_bo *ret,
                 struct anv_block_pool *pool,
                 uint64_t address)
{
   anv_block_pool_foreach_bo(bo, pool) {
      uint64_t bo_address = intel_48b_address(bo->offset);
      if (address >= bo_address && address < (bo_address + bo->size)) {
         *ret = (struct intel_batch_decode_bo) {
            .addr = bo_address,
            .size = bo->size,
            .map = bo->map,
         };
         return true;
      }
   }
   return false;
}

/* Finding a buffer for batch decoding */
static struct intel_batch_decode_bo
decode_get_bo(void *v_batch, bool ppgtt, uint64_t address)
{
   struct anv_device *device = v_batch;
   struct intel_batch_decode_bo ret_bo = {};

   assert(ppgtt);

   if (get_bo_from_pool(&ret_bo, &device->dynamic_state_pool.block_pool, address))
      return ret_bo;
   if (get_bo_from_pool(&ret_bo, &device->instruction_state_pool.block_pool, address))
      return ret_bo;
   if (get_bo_from_pool(&ret_bo, &device->binding_table_pool.block_pool, address))
      return ret_bo;
   if (get_bo_from_pool(&ret_bo, &device->scratch_surface_state_pool.block_pool, address))
      return ret_bo;
   if (device->physical->indirect_descriptors &&
       get_bo_from_pool(&ret_bo, &device->bindless_surface_state_pool.block_pool, address))
      return ret_bo;
   if (get_bo_from_pool(&ret_bo, &device->internal_surface_state_pool.block_pool, address))
      return ret_bo;
   if (device->physical->indirect_descriptors &&
       get_bo_from_pool(&ret_bo, &device->indirect_push_descriptor_pool.block_pool, address))
      return ret_bo;
   if (device->info->has_aux_map &&
       get_bo_from_pool(&ret_bo, &device->aux_tt_pool.block_pool, address))
      return ret_bo;

   if (!device->cmd_buffer_being_decoded)
      return (struct intel_batch_decode_bo) { };

   struct anv_batch_bo **bbo;
   u_vector_foreach(bbo, &device->cmd_buffer_being_decoded->seen_bbos) {
      /* The decoder zeroes out the top 16 bits, so we need to as well */
      uint64_t bo_address = (*bbo)->bo->offset & (~0ull >> 16);

      if (address >= bo_address && address < bo_address + (*bbo)->bo->size) {
         return (struct intel_batch_decode_bo) {
            .addr = bo_address,
            .size = (*bbo)->bo->size,
            .map = (*bbo)->bo->map,
         };
      }

      uint32_t dep_words = (*bbo)->relocs.dep_words;
      BITSET_WORD *deps = (*bbo)->relocs.deps;
      for (uint32_t w = 0; w < dep_words; w++) {
         BITSET_WORD mask = deps[w];
         while (mask) {
            int i = u_bit_scan(&mask);
            uint32_t gem_handle = w * BITSET_WORDBITS + i;
            struct anv_bo *bo = anv_device_lookup_bo(device, gem_handle);
            assert(bo->refcount > 0);
            bo_address = bo->offset & (~0ull >> 16);
            if (address >= bo_address && address < bo_address + bo->size) {
               return (struct intel_batch_decode_bo) {
                  .addr = bo_address,
                  .size = bo->size,
                  .map = bo->map,
               };
            }
         }
      }
   }

   return (struct intel_batch_decode_bo) { };
}

struct intel_aux_map_buffer {
   struct intel_buffer base;
   struct anv_state state;
};

static struct intel_buffer *
intel_aux_map_buffer_alloc(void *driver_ctx, uint32_t size)
{
   struct intel_aux_map_buffer *buf = malloc(sizeof(struct intel_aux_map_buffer));
   if (!buf)
      return NULL;

   struct anv_device *device = (struct anv_device*)driver_ctx;

   struct anv_state_pool *pool = &device->aux_tt_pool;
   buf->state = anv_state_pool_alloc(pool, size, size);

   buf->base.gpu = pool->block_pool.bo->offset + buf->state.offset;
   buf->base.gpu_end = buf->base.gpu + buf->state.alloc_size;
   buf->base.map = buf->state.map;
   buf->base.driver_bo = &buf->state;
   return &buf->base;
}

static void
intel_aux_map_buffer_free(void *driver_ctx, struct intel_buffer *buffer)
{
   struct intel_aux_map_buffer *buf = (struct intel_aux_map_buffer*)buffer;
   struct anv_device *device = (struct anv_device*)driver_ctx;
   struct anv_state_pool *pool = &device->aux_tt_pool;
   anv_state_pool_free(pool, buf->state);
   free(buf);
}

static struct intel_mapped_pinned_buffer_alloc aux_map_allocator = {
   .alloc = intel_aux_map_buffer_alloc,
   .free = intel_aux_map_buffer_free,
};

static VkResult
anv_device_setup_context_or_vm(struct anv_device *device,
                               const VkDeviceCreateInfo *pCreateInfo,
                               const uint32_t num_queues)
{
   switch (device->info->kmd_type) {
   case INTEL_KMD_TYPE_I915:
      return anv_i915_device_setup_context(device, pCreateInfo, num_queues);
   case INTEL_KMD_TYPE_XE:
      return anv_xe_device_setup_vm(device);
   default:
      unreachable("Missing");
      return VK_ERROR_UNKNOWN;
   }
}

static bool
anv_device_destroy_context_or_vm(struct anv_device *device)
{
   switch (device->info->kmd_type) {
   case INTEL_KMD_TYPE_I915:
      if (device->physical->has_vm_control)
         return anv_i915_device_destroy_vm(device);
      else
         return intel_gem_destroy_context(device->fd, device->context_id);
   case INTEL_KMD_TYPE_XE:
      return anv_xe_device_destroy_vm(device);
   default:
      unreachable("Missing");
      return false;
   }
}

static VkResult
anv_device_init_trtt(struct anv_device *device)
{
   if (device->physical->sparse_type != ANV_SPARSE_TYPE_TRTT ||
       !device->vk.enabled_features.sparseBinding)
      return VK_SUCCESS;

   struct anv_trtt *trtt = &device->trtt;

   VkResult result =
      vk_sync_create(&device->vk,
                     &device->physical->sync_syncobj_type,
                     VK_SYNC_IS_TIMELINE,
                     0 /* initial_value */,
                     &trtt->timeline);
   if (result != VK_SUCCESS)
      return result;

   simple_mtx_init(&trtt->mutex, mtx_plain);

   list_inithead(&trtt->in_flight_batches);

   return VK_SUCCESS;
}

static void
anv_device_finish_trtt(struct anv_device *device)
{
   if (device->physical->sparse_type != ANV_SPARSE_TYPE_TRTT ||
       !device->vk.enabled_features.sparseBinding)
      return;

   struct anv_trtt *trtt = &device->trtt;

   anv_sparse_trtt_garbage_collect_batches(device, true);

   vk_sync_destroy(&device->vk, trtt->timeline);

   simple_mtx_destroy(&trtt->mutex);

   vk_free(&device->vk.alloc, trtt->l3_mirror);
   vk_free(&device->vk.alloc, trtt->l2_mirror);

   for (int i = 0; i < trtt->num_page_table_bos; i++)
      anv_device_release_bo(device, trtt->page_table_bos[i]);

   vk_free(&device->vk.alloc, trtt->page_table_bos);
}

VkResult anv_CreateDevice(
    VkPhysicalDevice                            physicalDevice,
    const VkDeviceCreateInfo*                   pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDevice*                                   pDevice)
{
   ANV_FROM_HANDLE(anv_physical_device, physical_device, physicalDevice);
   VkResult result;
   struct anv_device *device;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);

   /* Check requested queues and fail if we are requested to create any
    * queues with flags we don't support.
    */
   assert(pCreateInfo->queueCreateInfoCount > 0);
   for (uint32_t i = 0; i < pCreateInfo->queueCreateInfoCount; i++) {
      if (pCreateInfo->pQueueCreateInfos[i].flags & ~VK_DEVICE_QUEUE_CREATE_PROTECTED_BIT)
         return vk_error(physical_device, VK_ERROR_INITIALIZATION_FAILED);
   }

   device = vk_zalloc2(&physical_device->instance->vk.alloc, pAllocator,
                       sizeof(*device), 8,
                       VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!device)
      return vk_error(physical_device, VK_ERROR_OUT_OF_HOST_MEMORY);

   struct vk_device_dispatch_table dispatch_table;

   bool override_initial_entrypoints = true;
   if (physical_device->instance->vk.app_info.app_name &&
       !strcmp(physical_device->instance->vk.app_info.app_name, "HITMAN3.exe")) {
      vk_device_dispatch_table_from_entrypoints(&dispatch_table,
                                                &anv_hitman3_device_entrypoints,
                                                true);
      override_initial_entrypoints = false;
   }
   if (physical_device->info.ver < 12 &&
       physical_device->instance->vk.app_info.app_name &&
       !strcmp(physical_device->instance->vk.app_info.app_name, "DOOM 64")) {
      vk_device_dispatch_table_from_entrypoints(&dispatch_table,
                                                &anv_doom64_device_entrypoints,
                                                true);
      override_initial_entrypoints = false;
   }
#if DETECT_OS_ANDROID
   vk_device_dispatch_table_from_entrypoints(&dispatch_table,
                                             &anv_android_device_entrypoints,
                                             true);
   override_initial_entrypoints = false;
#endif
   if (physical_device->instance->vk.trace_mode & VK_TRACE_MODE_RMV) {
      vk_device_dispatch_table_from_entrypoints(&dispatch_table,
                                                &anv_rmv_device_entrypoints,
                                                true);
      override_initial_entrypoints = false;
   }
   vk_device_dispatch_table_from_entrypoints(&dispatch_table,
      anv_genX(&physical_device->info, device_entrypoints),
      override_initial_entrypoints);
   vk_device_dispatch_table_from_entrypoints(&dispatch_table,
      &anv_device_entrypoints, false);
   vk_device_dispatch_table_from_entrypoints(&dispatch_table,
      &wsi_device_entrypoints, false);


   result = vk_device_init(&device->vk, &physical_device->vk,
                           &dispatch_table, pCreateInfo, pAllocator);
   if (result != VK_SUCCESS)
      goto fail_alloc;

   if (INTEL_DEBUG(DEBUG_BATCH | DEBUG_BATCH_STATS)) {
      for (unsigned i = 0; i < physical_device->queue.family_count; i++) {
         struct intel_batch_decode_ctx *decoder = &device->decoder[i];

         const unsigned decode_flags = INTEL_BATCH_DECODE_DEFAULT_FLAGS;

         intel_batch_decode_ctx_init_brw(decoder,
                                         &physical_device->compiler->isa,
                                         &physical_device->info,
                                         stderr, decode_flags, NULL,
                                         decode_get_bo, NULL, device);
         intel_batch_stats_reset(decoder);

         decoder->engine = physical_device->queue.families[i].engine_class;
         decoder->dynamic_base = physical_device->va.dynamic_state_pool.addr;
         decoder->surface_base = physical_device->va.internal_surface_state_pool.addr;
         decoder->instruction_base = physical_device->va.instruction_state_pool.addr;
      }
   }

   anv_device_set_physical(device, physical_device);
   device->kmd_backend = anv_kmd_backend_get(device->info->kmd_type);

   /* XXX(chadv): Can we dup() physicalDevice->fd here? */
   device->fd = open(physical_device->path, O_RDWR | O_CLOEXEC);
   if (device->fd == -1) {
      result = vk_error(device, VK_ERROR_INITIALIZATION_FAILED);
      goto fail_device;
   }

   switch (device->info->kmd_type) {
   case INTEL_KMD_TYPE_I915:
      device->vk.check_status = anv_i915_device_check_status;
      break;
   case INTEL_KMD_TYPE_XE:
      device->vk.check_status = anv_xe_device_check_status;
      break;
   default:
      unreachable("Missing");
   }

   device->vk.command_buffer_ops = &anv_cmd_buffer_ops;
   device->vk.create_sync_for_memory = anv_create_sync_for_memory;
   if (physical_device->info.kmd_type == INTEL_KMD_TYPE_I915)
      device->vk.create_sync_for_memory = anv_create_sync_for_memory;
   vk_device_set_drm_fd(&device->vk, device->fd);

   uint32_t num_queues = 0;
   for (uint32_t i = 0; i < pCreateInfo->queueCreateInfoCount; i++)
      num_queues += pCreateInfo->pQueueCreateInfos[i].queueCount;

   result = anv_device_setup_context_or_vm(device, pCreateInfo, num_queues);
   if (result != VK_SUCCESS)
      goto fail_fd;

   device->queues =
      vk_zalloc(&device->vk.alloc, num_queues * sizeof(*device->queues), 8,
                VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (device->queues == NULL) {
      result = vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto fail_context_id;
   }

   if (pthread_mutex_init(&device->vma_mutex, NULL) != 0) {
      result = vk_error(device, VK_ERROR_INITIALIZATION_FAILED);
      goto fail_queues_alloc;
   }

   /* keep the page with address zero out of the allocator */
   util_vma_heap_init(&device->vma_lo,
                      device->physical->va.low_heap.addr,
                      device->physical->va.low_heap.size);

   util_vma_heap_init(&device->vma_hi,
                      device->physical->va.high_heap.addr,
                      device->physical->va.high_heap.size);

   if (device->physical->indirect_descriptors) {
      util_vma_heap_init(&device->vma_desc,
                         device->physical->va.indirect_descriptor_pool.addr,
                         device->physical->va.indirect_descriptor_pool.size);
   } else {
      util_vma_heap_init(&device->vma_desc,
                         device->physical->va.bindless_surface_state_pool.addr,
                         device->physical->va.bindless_surface_state_pool.size);
   }

   /* Always initialized because the the memory types point to this and they
    * are on the physical device.
    */
   util_vma_heap_init(&device->vma_dynamic_visible,
                      device->physical->va.dynamic_visible_pool.addr,
                      device->physical->va.dynamic_visible_pool.size);
   util_vma_heap_init(&device->vma_trtt,
                      device->physical->va.trtt.addr,
                      device->physical->va.trtt.size);

   list_inithead(&device->memory_objects);
   list_inithead(&device->image_private_objects);

   if (pthread_mutex_init(&device->mutex, NULL) != 0) {
      result = vk_error(device, VK_ERROR_INITIALIZATION_FAILED);
      goto fail_vmas;
   }

   pthread_condattr_t condattr;
   if (pthread_condattr_init(&condattr) != 0) {
      result = vk_error(device, VK_ERROR_INITIALIZATION_FAILED);
      goto fail_mutex;
   }
   if (pthread_condattr_setclock(&condattr, CLOCK_MONOTONIC) != 0) {
      pthread_condattr_destroy(&condattr);
      result = vk_error(device, VK_ERROR_INITIALIZATION_FAILED);
      goto fail_mutex;
   }
   if (pthread_cond_init(&device->queue_submit, &condattr) != 0) {
      pthread_condattr_destroy(&condattr);
      result = vk_error(device, VK_ERROR_INITIALIZATION_FAILED);
      goto fail_mutex;
   }
   pthread_condattr_destroy(&condattr);

   if (physical_device->instance->vk.trace_mode & VK_TRACE_MODE_RMV)
      anv_memory_trace_init(device);

   result = anv_bo_cache_init(&device->bo_cache, device);
   if (result != VK_SUCCESS)
      goto fail_queue_cond;

   anv_bo_pool_init(&device->batch_bo_pool, device, "batch",
                    ANV_BO_ALLOC_MAPPED |
                    ANV_BO_ALLOC_HOST_CACHED_COHERENT |
                    ANV_BO_ALLOC_CAPTURE);
   if (device->vk.enabled_extensions.KHR_acceleration_structure) {
      anv_bo_pool_init(&device->bvh_bo_pool, device, "bvh build",
                       0 /* alloc_flags */);
   }

   /* Because scratch is also relative to General State Base Address, we leave
    * the base address 0 and start the pool memory at an offset.  This way we
    * get the correct offsets in the anv_states that get allocated from it.
    */
   result = anv_state_pool_init(&device->general_state_pool, device,
                                &(struct anv_state_pool_params) {
                                   .name         = "general pool",
                                   .base_address = 0,
                                   .start_offset = device->physical->va.general_state_pool.addr,
                                   .block_size   = 16384,
                                   .max_size     = device->physical->va.general_state_pool.size
                                });
   if (result != VK_SUCCESS)
      goto fail_batch_bo_pool;

   result = anv_state_pool_init(&device->dynamic_state_pool, device,
                                &(struct anv_state_pool_params) {
                                   .name         = "dynamic pool",
                                   .base_address = device->physical->va.dynamic_state_pool.addr,
                                   .block_size   = 16384,
                                   .max_size     = device->physical->va.dynamic_state_pool.size,
                                });
   if (result != VK_SUCCESS)
      goto fail_general_state_pool;

   /* The border color pointer is limited to 24 bits, so we need to make
    * sure that any such color used at any point in the program doesn't
    * exceed that limit.
    * We achieve that by reserving all the custom border colors we support
    * right off the bat, so they are close to the base address.
    */
   result = anv_state_reserved_array_pool_init(&device->custom_border_colors,
                                               &device->dynamic_state_pool,
                                               MAX_CUSTOM_BORDER_COLORS,
                                               sizeof(struct gfx8_border_color), 64);
   if (result != VK_SUCCESS)
      goto fail_dynamic_state_pool;

   result = anv_state_pool_init(&device->instruction_state_pool, device,
                                &(struct anv_state_pool_params) {
                                   .name         = "instruction pool",
                                   .base_address = device->physical->va.instruction_state_pool.addr,
                                   .block_size   = 16384,
                                   .max_size     = device->physical->va.instruction_state_pool.size,
                                });
   if (result != VK_SUCCESS)
      goto fail_custom_border_color_pool;

   if (device->info->verx10 >= 125) {
      /* Put the scratch surface states at the beginning of the internal
       * surface state pool.
       */
      result = anv_state_pool_init(&device->scratch_surface_state_pool, device,
                                   &(struct anv_state_pool_params) {
                                      .name         = "scratch surface state pool",
                                      .base_address = device->physical->va.scratch_surface_state_pool.addr,
                                      .block_size   = 4096,
                                      .max_size     = device->physical->va.scratch_surface_state_pool.size,
                                   });
      if (result != VK_SUCCESS)
         goto fail_instruction_state_pool;

      result = anv_state_pool_init(&device->internal_surface_state_pool, device,
                                   &(struct anv_state_pool_params) {
                                      .name         = "internal surface state pool",
                                      .base_address = device->physical->va.internal_surface_state_pool.addr,
                                      .start_offset = device->physical->va.scratch_surface_state_pool.size,
                                      .block_size   = 4096,
                                      .max_size     = device->physical->va.internal_surface_state_pool.size,
                                   });
   } else {
      result = anv_state_pool_init(&device->internal_surface_state_pool, device,
                                   &(struct anv_state_pool_params) {
                                      .name         = "internal surface state pool",
                                      .base_address = device->physical->va.internal_surface_state_pool.addr,
                                      .block_size   = 4096,
                                      .max_size     = device->physical->va.internal_surface_state_pool.size,
                                   });
   }
   if (result != VK_SUCCESS)
      goto fail_scratch_surface_state_pool;

   if (device->physical->indirect_descriptors) {
      result = anv_state_pool_init(&device->bindless_surface_state_pool, device,
                                   &(struct anv_state_pool_params) {
                                      .name         = "bindless surface state pool",
                                      .base_address = device->physical->va.bindless_surface_state_pool.addr,
                                      .block_size   = 4096,
                                      .max_size     = device->physical->va.bindless_surface_state_pool.size,
                                   });
      if (result != VK_SUCCESS)
         goto fail_internal_surface_state_pool;
   }

   if (device->info->verx10 >= 125) {
      /* We're using 3DSTATE_BINDING_TABLE_POOL_ALLOC to give the binding
       * table its own base address separately from surface state base.
       */
      result = anv_state_pool_init(&device->binding_table_pool, device,
                                   &(struct anv_state_pool_params) {
                                      .name         = "binding table pool",
                                      .base_address = device->physical->va.binding_table_pool.addr,
                                      .block_size   = BINDING_TABLE_POOL_BLOCK_SIZE,
                                      .max_size     = device->physical->va.binding_table_pool.size,
                                   });
   } else {
      /* The binding table should be in front of the surface states in virtual
       * address space so that all surface states can be express as relative
       * offsets from the binding table location.
       */
      assert(device->physical->va.binding_table_pool.addr <
             device->physical->va.internal_surface_state_pool.addr);
      int64_t bt_pool_offset = (int64_t)device->physical->va.binding_table_pool.addr -
                               (int64_t)device->physical->va.internal_surface_state_pool.addr;
      assert(INT32_MIN < bt_pool_offset && bt_pool_offset < 0);
      result = anv_state_pool_init(&device->binding_table_pool, device,
                                   &(struct anv_state_pool_params) {
                                      .name         = "binding table pool",
                                      .base_address = device->physical->va.internal_surface_state_pool.addr,
                                      .start_offset = bt_pool_offset,
                                      .block_size   = BINDING_TABLE_POOL_BLOCK_SIZE,
                                      .max_size     = device->physical->va.internal_surface_state_pool.size,
                                   });
   }
   if (result != VK_SUCCESS)
      goto fail_bindless_surface_state_pool;

   if (device->physical->indirect_descriptors) {
      result = anv_state_pool_init(&device->indirect_push_descriptor_pool, device,
                                   &(struct anv_state_pool_params) {
                                      .name         = "indirect push descriptor pool",
                                      .base_address = device->physical->va.indirect_push_descriptor_pool.addr,
                                      .block_size   = 4096,
                                      .max_size     = device->physical->va.indirect_push_descriptor_pool.size,
                                   });
      if (result != VK_SUCCESS)
         goto fail_binding_table_pool;
   }

   if (device->vk.enabled_extensions.EXT_descriptor_buffer &&
       device->info->verx10 >= 125) {
      /* On Gfx12.5+ because of the bindless stages (Mesh, Task, RT), the only
       * way we can wire push descriptors is through the bindless heap. This
       * state pool is a 1Gb carve out of the 4Gb HW heap.
       */
      result = anv_state_pool_init(&device->push_descriptor_buffer_pool, device,
                                   &(struct anv_state_pool_params) {
                                      .name         = "push descriptor buffer state pool",
                                      .base_address = device->physical->va.push_descriptor_buffer_pool.addr,
                                      .block_size   = 4096,
                                      .max_size     = device->physical->va.push_descriptor_buffer_pool.size,
                                   });
      if (result != VK_SUCCESS)
         goto fail_indirect_push_descriptor_pool;
   }

   if (device->info->has_aux_map) {
      result = anv_state_pool_init(&device->aux_tt_pool, device,
                                   &(struct anv_state_pool_params) {
                                      .name         = "aux-tt pool",
                                      .base_address = device->physical->va.aux_tt_pool.addr,
                                      .block_size   = 16384,
                                      .max_size     = device->physical->va.aux_tt_pool.size,
                                   });
      if (result != VK_SUCCESS)
         goto fail_push_descriptor_buffer_pool;

      device->aux_map_ctx = intel_aux_map_init(device, &aux_map_allocator,
                                               &physical_device->info);
      if (!device->aux_map_ctx)
         goto fail_aux_tt_pool;
   }

   result = anv_device_alloc_bo(device, "workaround", 8192,
                                ANV_BO_ALLOC_CAPTURE |
                                ANV_BO_ALLOC_HOST_COHERENT |
                                ANV_BO_ALLOC_MAPPED |
                                ANV_BO_ALLOC_INTERNAL,
                                0 /* explicit_address */,
                                &device->workaround_bo);
   if (result != VK_SUCCESS)
      goto fail_surface_aux_map_pool;

   if (intel_needs_workaround(device->info, 14019708328)) {
      result = anv_device_alloc_bo(device, "dummy_aux", 4096,
                                   0 /* alloc_flags */,
                                   0 /* explicit_address */,
                                   &device->dummy_aux_bo);
      if (result != VK_SUCCESS)
         goto fail_workaround_bo;

      device->isl_dev.dummy_aux_address = device->dummy_aux_bo->offset;
   }

   struct anv_address wa_addr = (struct anv_address) {
      .bo = device->workaround_bo,
   };

   wa_addr = anv_address_add_aligned(wa_addr,
                                     intel_debug_write_identifiers(
                                        device->workaround_bo->map,
                                        device->workaround_bo->size,
                                        "Anv"), 32);

   device->rt_uuid_addr = wa_addr;
   memcpy(device->rt_uuid_addr.bo->map + device->rt_uuid_addr.offset,
          physical_device->rt_uuid,
          sizeof(physical_device->rt_uuid));

   /* Make sure the workaround address is the last one in the workaround BO,
    * so that writes never overwrite other bits of data stored in the
    * workaround BO.
    */
   wa_addr = anv_address_add_aligned(wa_addr,
                                     sizeof(physical_device->rt_uuid), 64);
   device->workaround_address = wa_addr;

   /* Make sure we don't over the allocated BO. */
   assert(device->workaround_address.offset < device->workaround_bo->size);
   /* We also need 64B (maximum GRF size) from the workaround address (see
    * TBIMR workaround)
    */
   assert((device->workaround_bo->size -
           device->workaround_address.offset) >= 64);

   device->workarounds.doom64_images = NULL;


   device->debug_frame_desc =
      intel_debug_get_identifier_block(device->workaround_bo->map,
                                       device->workaround_bo->size,
                                       INTEL_DEBUG_BLOCK_TYPE_FRAME);

   if (device->vk.enabled_extensions.KHR_ray_query) {
      uint32_t ray_queries_size =
         align(brw_rt_ray_queries_hw_stacks_size(device->info), 4096);

      result = anv_device_alloc_bo(device, "ray queries",
                                   ray_queries_size,
                                   ANV_BO_ALLOC_INTERNAL,
                                   0 /* explicit_address */,
                                   &device->ray_query_bo);
      if (result != VK_SUCCESS)
         goto fail_dummy_aux_bo;
   }

   result = anv_device_init_trivial_batch(device);
   if (result != VK_SUCCESS)
      goto fail_ray_query_bo;

   /* Emit the CPS states before running the initialization batch as those
    * structures are referenced.
    */
   if (device->info->ver >= 12) {
      uint32_t n_cps_states = 3 * 3; /* All combinaisons of X by Y CP sizes (1, 2, 4) */

      if (device->info->has_coarse_pixel_primitive_and_cb)
         n_cps_states *= 5 * 5; /* 5 combiners by 2 operators */

      n_cps_states += 1; /* Disable CPS */

       /* Each of the combinaison must be replicated on all viewports */
      n_cps_states *= MAX_VIEWPORTS;

      device->cps_states =
         anv_state_pool_alloc(&device->dynamic_state_pool,
                              n_cps_states * CPS_STATE_length(device->info) * 4,
                              32);
      if (device->cps_states.map == NULL)
         goto fail_trivial_batch;

      anv_genX(device->info, init_cps_device_state)(device);
   }

   if (device->physical->indirect_descriptors) {
      /* Allocate a null surface state at surface state offset 0. This makes
       * NULL descriptor handling trivial because we can just memset
       * structures to zero and they have a valid descriptor.
       */
      device->null_surface_state =
         anv_state_pool_alloc(&device->bindless_surface_state_pool,
                              device->isl_dev.ss.size,
                              device->isl_dev.ss.align);
      isl_null_fill_state(&device->isl_dev, device->null_surface_state.map,
                          .size = isl_extent3d(1, 1, 1) /* This shouldn't matter */);
      assert(device->null_surface_state.offset == 0);
   } else {
      /* When using direct descriptors, those can hold the null surface state
       * directly. We still need a null surface for the binding table entries
       * though but this one can live anywhere the internal surface state
       * pool.
       */
      device->null_surface_state =
         anv_state_pool_alloc(&device->internal_surface_state_pool,
                              device->isl_dev.ss.size,
                              device->isl_dev.ss.align);
      isl_null_fill_state(&device->isl_dev, device->null_surface_state.map,
                          .size = isl_extent3d(1, 1, 1) /* This shouldn't matter */);
   }

   isl_null_fill_state(&device->isl_dev, &device->host_null_surface_state,
                       .size = isl_extent3d(1, 1, 1) /* This shouldn't matter */);

   anv_scratch_pool_init(device, &device->scratch_pool, false);
   anv_scratch_pool_init(device, &device->protected_scratch_pool, true);

   /* TODO(RT): Do we want some sort of data structure for this? */
   memset(device->rt_scratch_bos, 0, sizeof(device->rt_scratch_bos));

   if (ANV_SUPPORT_RT && device->info->has_ray_tracing) {
      /* The docs say to always allocate 128KB per DSS */
      const uint32_t btd_fifo_bo_size =
         128 * 1024 * intel_device_info_dual_subslice_id_bound(device->info);
      result = anv_device_alloc_bo(device,
                                   "rt-btd-fifo",
                                   btd_fifo_bo_size,
                                   ANV_BO_ALLOC_INTERNAL,
                                   0 /* explicit_address */,
                                   &device->btd_fifo_bo);
      if (result != VK_SUCCESS)
         goto fail_trivial_batch_bo_and_scratch_pool;
   }

   struct vk_pipeline_cache_create_info pcc_info = { .weak_ref = true, };
   device->vk.mem_cache =
      vk_pipeline_cache_create(&device->vk, &pcc_info, NULL);
   if (!device->vk.mem_cache) {
      result = vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto fail_btd_fifo_bo;
   }

   /* Internal shaders need their own pipeline cache because, unlike the rest
    * of ANV, it won't work at all without the cache. It depends on it for
    * shaders to remain resident while it runs. Therefore, we need a special
    * cache just for BLORP/RT that's forced to always be enabled.
    */
   struct vk_pipeline_cache_create_info internal_pcc_info = {
      .force_enable = true,
      .weak_ref = false,
   };
   device->internal_cache =
      vk_pipeline_cache_create(&device->vk, &internal_pcc_info, NULL);
   if (device->internal_cache == NULL) {
      result = vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto fail_default_pipeline_cache;
   }

   /* The device (currently is ICL/TGL) does not have float64 support. */
   if (!device->info->has_64bit_float &&
      device->physical->instance->fp64_workaround_enabled)
      anv_load_fp64_shader(device);

   if (INTEL_DEBUG(DEBUG_SHADER_PRINT)) {
      result = anv_device_print_init(device);
      if (result != VK_SUCCESS)
         goto fail_internal_cache;
   }

#if DETECT_OS_ANDROID
   device->u_gralloc = u_gralloc_create(U_GRALLOC_TYPE_AUTO);
#endif

   device->robust_buffer_access =
      device->vk.enabled_features.robustBufferAccess ||
      device->vk.enabled_features.nullDescriptor;

   device->breakpoint = anv_state_pool_alloc(&device->dynamic_state_pool, 4,
                                             4);
   p_atomic_set(&device->draw_call_count, 0);

   /* Create a separate command pool for companion RCS command buffer. */
   if (device->info->verx10 >= 125) {
      VkCommandPoolCreateInfo pool_info = {
         .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
         .queueFamilyIndex =
             anv_get_first_render_queue_index(device->physical),
      };

      result = vk_common_CreateCommandPool(anv_device_to_handle(device),
                                           &pool_info, NULL,
                                           &device->companion_rcs_cmd_pool);
      if (result != VK_SUCCESS) {
         goto fail_print;
      }
   }

   result = anv_device_init_trtt(device);
   if (result != VK_SUCCESS)
      goto fail_companion_cmd_pool;

   result = anv_device_init_rt_shaders(device);
   if (result != VK_SUCCESS) {
      result = vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto fail_trtt;
   }

   anv_device_init_blorp(device);

   anv_device_init_border_colors(device);

   anv_device_init_internal_kernels(device);

   anv_device_init_astc_emu(device);

   anv_device_perf_init(device);

   anv_device_init_embedded_samplers(device);

   BITSET_ONES(device->gfx_dirty_state);
   BITSET_CLEAR(device->gfx_dirty_state, ANV_GFX_STATE_INDEX_BUFFER);
   BITSET_CLEAR(device->gfx_dirty_state, ANV_GFX_STATE_SO_DECL_LIST);
   if (device->info->ver < 11)
      BITSET_CLEAR(device->gfx_dirty_state, ANV_GFX_STATE_VF_SGVS_2);
   if (device->info->ver < 12) {
      BITSET_CLEAR(device->gfx_dirty_state, ANV_GFX_STATE_PRIMITIVE_REPLICATION);
      BITSET_CLEAR(device->gfx_dirty_state, ANV_GFX_STATE_DEPTH_BOUNDS);
   }
   if (!device->vk.enabled_extensions.EXT_sample_locations)
      BITSET_CLEAR(device->gfx_dirty_state, ANV_GFX_STATE_SAMPLE_PATTERN);
   if (!device->vk.enabled_extensions.KHR_fragment_shading_rate)
      BITSET_CLEAR(device->gfx_dirty_state, ANV_GFX_STATE_CPS);
   if (!device->vk.enabled_extensions.EXT_mesh_shader) {
      BITSET_CLEAR(device->gfx_dirty_state, ANV_GFX_STATE_SBE_MESH);
      BITSET_CLEAR(device->gfx_dirty_state, ANV_GFX_STATE_CLIP_MESH);
      BITSET_CLEAR(device->gfx_dirty_state, ANV_GFX_STATE_MESH_CONTROL);
      BITSET_CLEAR(device->gfx_dirty_state, ANV_GFX_STATE_MESH_SHADER);
      BITSET_CLEAR(device->gfx_dirty_state, ANV_GFX_STATE_MESH_DISTRIB);
      BITSET_CLEAR(device->gfx_dirty_state, ANV_GFX_STATE_TASK_CONTROL);
      BITSET_CLEAR(device->gfx_dirty_state, ANV_GFX_STATE_TASK_SHADER);
      BITSET_CLEAR(device->gfx_dirty_state, ANV_GFX_STATE_TASK_REDISTRIB);
   }
   if (!intel_needs_workaround(device->info, 18019816803))
      BITSET_CLEAR(device->gfx_dirty_state, ANV_GFX_STATE_WA_18019816803);
   if (!intel_needs_workaround(device->info, 14018283232))
      BITSET_CLEAR(device->gfx_dirty_state, ANV_GFX_STATE_WA_14018283232);
   if (device->info->ver > 9)
      BITSET_CLEAR(device->gfx_dirty_state, ANV_GFX_STATE_PMA_FIX);

   device->queue_count = 0;
   for (uint32_t i = 0; i < pCreateInfo->queueCreateInfoCount; i++) {
      const VkDeviceQueueCreateInfo *queueCreateInfo =
         &pCreateInfo->pQueueCreateInfos[i];

      for (uint32_t j = 0; j < queueCreateInfo->queueCount; j++) {
         result = anv_queue_init(device, &device->queues[device->queue_count],
                                 queueCreateInfo, j);
         if (result != VK_SUCCESS)
            goto fail_queues;

         device->queue_count++;
      }
   }

   anv_device_utrace_init(device);

   result = anv_genX(device->info, init_device_state)(device);
   if (result != VK_SUCCESS)
      goto fail_utrace;

   *pDevice = anv_device_to_handle(device);

   return VK_SUCCESS;

 fail_utrace:
   anv_device_utrace_finish(device);
 fail_queues:
   for (uint32_t i = 0; i < device->queue_count; i++)
      anv_queue_finish(&device->queues[i]);
   anv_device_finish_embedded_samplers(device);
   anv_device_finish_blorp(device);
   anv_device_finish_astc_emu(device);
   anv_device_finish_internal_kernels(device);
   anv_device_finish_rt_shaders(device);
 fail_trtt:
   anv_device_finish_trtt(device);
 fail_companion_cmd_pool:
   if (device->info->verx10 >= 125) {
      vk_common_DestroyCommandPool(anv_device_to_handle(device),
                                   device->companion_rcs_cmd_pool, NULL);
   }
 fail_print:
   if (INTEL_DEBUG(DEBUG_SHADER_PRINT))
      anv_device_print_fini(device);
 fail_internal_cache:
   vk_pipeline_cache_destroy(device->internal_cache, NULL);
 fail_default_pipeline_cache:
   vk_pipeline_cache_destroy(device->vk.mem_cache, NULL);
 fail_btd_fifo_bo:
   if (ANV_SUPPORT_RT && device->info->has_ray_tracing)
      anv_device_release_bo(device, device->btd_fifo_bo);
 fail_trivial_batch_bo_and_scratch_pool:
   anv_scratch_pool_finish(device, &device->scratch_pool);
   anv_scratch_pool_finish(device, &device->protected_scratch_pool);
 fail_trivial_batch:
   anv_device_release_bo(device, device->trivial_batch_bo);
 fail_ray_query_bo:
   if (device->ray_query_bo)
      anv_device_release_bo(device, device->ray_query_bo);
 fail_dummy_aux_bo:
   if (device->dummy_aux_bo)
      anv_device_release_bo(device, device->dummy_aux_bo);
 fail_workaround_bo:
   anv_device_release_bo(device, device->workaround_bo);
 fail_surface_aux_map_pool:
   if (device->info->has_aux_map) {
      intel_aux_map_finish(device->aux_map_ctx);
      device->aux_map_ctx = NULL;
   }
 fail_aux_tt_pool:
   if (device->info->has_aux_map)
      anv_state_pool_finish(&device->aux_tt_pool);
 fail_push_descriptor_buffer_pool:
   if (device->vk.enabled_extensions.EXT_descriptor_buffer &&
       device->info->verx10 >= 125)
      anv_state_pool_finish(&device->push_descriptor_buffer_pool);
 fail_indirect_push_descriptor_pool:
   if (device->physical->indirect_descriptors)
      anv_state_pool_finish(&device->indirect_push_descriptor_pool);
 fail_binding_table_pool:
   anv_state_pool_finish(&device->binding_table_pool);
 fail_bindless_surface_state_pool:
   if (device->physical->indirect_descriptors)
      anv_state_pool_finish(&device->bindless_surface_state_pool);
 fail_internal_surface_state_pool:
   anv_state_pool_finish(&device->internal_surface_state_pool);
 fail_scratch_surface_state_pool:
   if (device->info->verx10 >= 125)
      anv_state_pool_finish(&device->scratch_surface_state_pool);
 fail_instruction_state_pool:
   anv_state_pool_finish(&device->instruction_state_pool);
 fail_custom_border_color_pool:
   anv_state_reserved_array_pool_finish(&device->custom_border_colors);
 fail_dynamic_state_pool:
   anv_state_pool_finish(&device->dynamic_state_pool);
 fail_general_state_pool:
   anv_state_pool_finish(&device->general_state_pool);
 fail_batch_bo_pool:
   if (device->vk.enabled_extensions.KHR_acceleration_structure)
      anv_bo_pool_finish(&device->bvh_bo_pool);
   anv_bo_pool_finish(&device->batch_bo_pool);
   anv_bo_cache_finish(&device->bo_cache);
 fail_queue_cond:
   pthread_cond_destroy(&device->queue_submit);
 fail_mutex:
   pthread_mutex_destroy(&device->mutex);
 fail_vmas:
   util_vma_heap_finish(&device->vma_trtt);
   util_vma_heap_finish(&device->vma_dynamic_visible);
   util_vma_heap_finish(&device->vma_desc);
   util_vma_heap_finish(&device->vma_hi);
   util_vma_heap_finish(&device->vma_lo);
   pthread_mutex_destroy(&device->vma_mutex);
 fail_queues_alloc:
   vk_free(&device->vk.alloc, device->queues);
 fail_context_id:
   anv_device_destroy_context_or_vm(device);
 fail_fd:
   close(device->fd);
 fail_device:
   vk_device_finish(&device->vk);
 fail_alloc:
   vk_free(&device->vk.alloc, device);

   return result;
}

void anv_DestroyDevice(
    VkDevice                                    _device,
    const VkAllocationCallbacks*                pAllocator)
{
   ANV_FROM_HANDLE(anv_device, device, _device);

   if (!device)
      return;

#if DETECT_OS_ANDROID
   u_gralloc_destroy(&device->u_gralloc);
#endif

   anv_memory_trace_finish(device);

   struct anv_physical_device *pdevice = device->physical;

   /* Do TRTT batch garbage collection before destroying queues. */
   anv_device_finish_trtt(device);

   anv_device_utrace_finish(device);

   for (uint32_t i = 0; i < device->queue_count; i++)
      anv_queue_finish(&device->queues[i]);
   vk_free(&device->vk.alloc, device->queues);

   anv_device_finish_blorp(device);

   anv_device_finish_rt_shaders(device);

   anv_device_finish_astc_emu(device);

   anv_device_finish_internal_kernels(device);

   if (INTEL_DEBUG(DEBUG_SHADER_PRINT))
      anv_device_print_fini(device);

   vk_pipeline_cache_destroy(device->internal_cache, NULL);
   vk_pipeline_cache_destroy(device->vk.mem_cache, NULL);

   anv_device_finish_embedded_samplers(device);

   if (ANV_SUPPORT_RT && device->info->has_ray_tracing)
      anv_device_release_bo(device, device->btd_fifo_bo);

   if (device->info->verx10 >= 125) {
      vk_common_DestroyCommandPool(anv_device_to_handle(device),
                                   device->companion_rcs_cmd_pool, NULL);
   }

   anv_state_reserved_array_pool_finish(&device->custom_border_colors);
#ifdef HAVE_VALGRIND
   /* We only need to free these to prevent valgrind errors.  The backing
    * BO will go away in a couple of lines so we don't actually leak.
    */
   anv_state_pool_free(&device->dynamic_state_pool, device->border_colors);
   anv_state_pool_free(&device->dynamic_state_pool, device->slice_hash);
   anv_state_pool_free(&device->dynamic_state_pool, device->cps_states);
   anv_state_pool_free(&device->dynamic_state_pool, device->breakpoint);
#endif

   for (unsigned i = 0; i < ARRAY_SIZE(device->rt_scratch_bos); i++) {
      if (device->rt_scratch_bos[i] != NULL)
         anv_device_release_bo(device, device->rt_scratch_bos[i]);
   }

   anv_scratch_pool_finish(device, &device->scratch_pool);
   anv_scratch_pool_finish(device, &device->protected_scratch_pool);

   if (device->vk.enabled_extensions.KHR_ray_query) {
      for (unsigned i = 0; i < ARRAY_SIZE(device->ray_query_shadow_bos); i++) {
         if (device->ray_query_shadow_bos[i] != NULL)
            anv_device_release_bo(device, device->ray_query_shadow_bos[i]);
      }
      anv_device_release_bo(device, device->ray_query_bo);
   }
   anv_device_release_bo(device, device->workaround_bo);
   if (device->dummy_aux_bo)
      anv_device_release_bo(device, device->dummy_aux_bo);
   anv_device_release_bo(device, device->trivial_batch_bo);

   if (device->info->has_aux_map) {
      intel_aux_map_finish(device->aux_map_ctx);
      device->aux_map_ctx = NULL;
      anv_state_pool_finish(&device->aux_tt_pool);
   }
   if (device->vk.enabled_extensions.EXT_descriptor_buffer &&
       device->info->verx10 >= 125)
      anv_state_pool_finish(&device->push_descriptor_buffer_pool);
   if (device->physical->indirect_descriptors)
      anv_state_pool_finish(&device->indirect_push_descriptor_pool);
   anv_state_pool_finish(&device->binding_table_pool);
   if (device->info->verx10 >= 125)
      anv_state_pool_finish(&device->scratch_surface_state_pool);
   anv_state_pool_finish(&device->internal_surface_state_pool);
   if (device->physical->indirect_descriptors)
      anv_state_pool_finish(&device->bindless_surface_state_pool);
   anv_state_pool_finish(&device->instruction_state_pool);
   anv_state_pool_finish(&device->dynamic_state_pool);
   anv_state_pool_finish(&device->general_state_pool);

   if (device->vk.enabled_extensions.KHR_acceleration_structure)
      anv_bo_pool_finish(&device->bvh_bo_pool);
   anv_bo_pool_finish(&device->batch_bo_pool);

   anv_bo_cache_finish(&device->bo_cache);

   util_vma_heap_finish(&device->vma_trtt);
   util_vma_heap_finish(&device->vma_dynamic_visible);
   util_vma_heap_finish(&device->vma_desc);
   util_vma_heap_finish(&device->vma_hi);
   util_vma_heap_finish(&device->vma_lo);
   pthread_mutex_destroy(&device->vma_mutex);

   pthread_cond_destroy(&device->queue_submit);
   pthread_mutex_destroy(&device->mutex);

   ralloc_free(device->fp64_nir);

   anv_device_destroy_context_or_vm(device);

   if (INTEL_DEBUG(DEBUG_BATCH | DEBUG_BATCH_STATS)) {
      for (unsigned i = 0; i < pdevice->queue.family_count; i++) {
         if (INTEL_DEBUG(DEBUG_BATCH_STATS))
            intel_batch_print_stats(&device->decoder[i]);
         intel_batch_decode_ctx_finish(&device->decoder[i]);
      }
   }

   close(device->fd);

   vk_device_finish(&device->vk);
   vk_free(&device->vk.alloc, device);
}

VkResult anv_EnumerateInstanceLayerProperties(
    uint32_t*                                   pPropertyCount,
    VkLayerProperties*                          pProperties)
{
   if (pProperties == NULL) {
      *pPropertyCount = 0;
      return VK_SUCCESS;
   }

   /* None supported at this time */
   return vk_error(NULL, VK_ERROR_LAYER_NOT_PRESENT);
}

VkResult
anv_device_wait(struct anv_device *device, struct anv_bo *bo,
                int64_t timeout)
{
   int ret = anv_gem_wait(device, bo->gem_handle, &timeout);
   if (ret == -1 && errno == ETIME) {
      return VK_TIMEOUT;
   } else if (ret == -1) {
      /* We don't know the real error. */
      return vk_device_set_lost(&device->vk, "gem wait failed: %m");
   } else {
      return VK_SUCCESS;
   }
}

static struct util_vma_heap *
anv_vma_heap_for_flags(struct anv_device *device,
                       enum anv_bo_alloc_flags alloc_flags)
{
   if (alloc_flags & ANV_BO_ALLOC_TRTT)
      return &device->vma_trtt;

   if (alloc_flags & ANV_BO_ALLOC_32BIT_ADDRESS)
      return &device->vma_lo;

   if (alloc_flags & ANV_BO_ALLOC_DESCRIPTOR_POOL)
      return &device->vma_desc;

   if (alloc_flags & ANV_BO_ALLOC_DYNAMIC_VISIBLE_POOL)
      return &device->vma_dynamic_visible;

   return &device->vma_hi;
}

uint64_t
anv_vma_alloc(struct anv_device *device,
              uint64_t size, uint64_t align,
              enum anv_bo_alloc_flags alloc_flags,
              uint64_t client_address,
              struct util_vma_heap **out_vma_heap)
{
   pthread_mutex_lock(&device->vma_mutex);

   uint64_t addr = 0;
   *out_vma_heap = anv_vma_heap_for_flags(device, alloc_flags);

   if (alloc_flags & ANV_BO_ALLOC_CLIENT_VISIBLE_ADDRESS) {
      assert(*out_vma_heap == &device->vma_hi ||
             *out_vma_heap == &device->vma_dynamic_visible ||
             *out_vma_heap == &device->vma_trtt);

      if (client_address) {
         if (util_vma_heap_alloc_addr(*out_vma_heap,
                                      client_address, size)) {
            addr = client_address;
         }
      } else {
         (*out_vma_heap)->alloc_high = false;
         addr = util_vma_heap_alloc(*out_vma_heap, size, align);
         (*out_vma_heap)->alloc_high = true;
      }
      /* We don't want to fall back to other heaps */
      goto done;
   }

   assert(client_address == 0);

   addr = util_vma_heap_alloc(*out_vma_heap, size, align);

done:
   pthread_mutex_unlock(&device->vma_mutex);

   assert(addr == intel_48b_address(addr));
   return intel_canonical_address(addr);
}

void
anv_vma_free(struct anv_device *device,
             struct util_vma_heap *vma_heap,
             uint64_t address, uint64_t size)
{
   assert(vma_heap == &device->vma_lo ||
          vma_heap == &device->vma_hi ||
          vma_heap == &device->vma_desc ||
          vma_heap == &device->vma_dynamic_visible ||
          vma_heap == &device->vma_trtt);

   const uint64_t addr_48b = intel_48b_address(address);

   pthread_mutex_lock(&device->vma_mutex);

   util_vma_heap_free(vma_heap, addr_48b, size);

   pthread_mutex_unlock(&device->vma_mutex);
}

VkResult anv_AllocateMemory(
    VkDevice                                    _device,
    const VkMemoryAllocateInfo*                 pAllocateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDeviceMemory*                             pMem)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   struct anv_physical_device *pdevice = device->physical;
   struct anv_device_memory *mem;
   VkResult result = VK_SUCCESS;

   assert(pAllocateInfo->sType == VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);

   VkDeviceSize aligned_alloc_size =
      align64(pAllocateInfo->allocationSize, 4096);

   assert(pAllocateInfo->memoryTypeIndex < pdevice->memory.type_count);
   const struct anv_memory_type *mem_type =
      &pdevice->memory.types[pAllocateInfo->memoryTypeIndex];
   assert(mem_type->heapIndex < pdevice->memory.heap_count);
   struct anv_memory_heap *mem_heap =
      &pdevice->memory.heaps[mem_type->heapIndex];

   if (aligned_alloc_size > mem_heap->size)
      return vk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);

   uint64_t mem_heap_used = p_atomic_read(&mem_heap->used);
   if (mem_heap_used + aligned_alloc_size > mem_heap->size)
      return vk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);

   mem = vk_device_memory_create(&device->vk, pAllocateInfo,
                                 pAllocator, sizeof(*mem));
   if (mem == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   mem->type = mem_type;
   mem->map = NULL;
   mem->map_size = 0;
   mem->map_delta = 0;

   enum anv_bo_alloc_flags alloc_flags = 0;

   const VkImportMemoryFdInfoKHR *fd_info = NULL;
   const VkMemoryDedicatedAllocateInfo *dedicated_info = NULL;
   const struct wsi_memory_allocate_info *wsi_info = NULL;
   uint64_t client_address = 0;

   vk_foreach_struct_const(ext, pAllocateInfo->pNext) {
      /* VK_STRUCTURE_TYPE_WSI_MEMORY_ALLOCATE_INFO_MESA isn't a real enum
       * value, so use cast to avoid compiler warn
       */
      switch ((uint32_t)ext->sType) {
      case VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO:
      case VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID:
      case VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT:
      case VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR:
      case VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO:
         /* handled by vk_device_memory_create */
         break;

      case VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR:
         fd_info = (void *)ext;
         break;

      case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO:
         dedicated_info = (void *)ext;
         break;

      case VK_STRUCTURE_TYPE_MEMORY_OPAQUE_CAPTURE_ADDRESS_ALLOCATE_INFO: {
         const VkMemoryOpaqueCaptureAddressAllocateInfo *addr_info =
            (const VkMemoryOpaqueCaptureAddressAllocateInfo *)ext;
         client_address = addr_info->opaqueCaptureAddress;
         break;
      }

      case VK_STRUCTURE_TYPE_WSI_MEMORY_ALLOCATE_INFO_MESA:
         wsi_info = (void *)ext;
         break;

      default:
         vk_debug_ignored_stype(ext->sType);
         break;
      }
   }

   /* If i915 reported a mappable/non_mappable vram regions and the
    * application want lmem mappable, then we need to use the
    * I915_GEM_CREATE_EXT_FLAG_NEEDS_CPU_ACCESS flag to create our BO.
    */
   if (pdevice->vram_mappable.size > 0 &&
       pdevice->vram_non_mappable.size > 0 &&
       (mem_type->propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) &&
       (mem_type->propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
      alloc_flags |= ANV_BO_ALLOC_LOCAL_MEM_CPU_VISIBLE;

   if (!mem_heap->is_local_mem)
      alloc_flags |= ANV_BO_ALLOC_NO_LOCAL_MEM;

   if (mem->vk.alloc_flags & VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT)
      alloc_flags |= ANV_BO_ALLOC_CLIENT_VISIBLE_ADDRESS;

   if (mem_type->propertyFlags & VK_MEMORY_PROPERTY_PROTECTED_BIT)
      alloc_flags |= ANV_BO_ALLOC_PROTECTED;

   /* For now, always allocated AUX-TT aligned memory, regardless of dedicated
    * allocations. An application can for example, suballocate a large
    * VkDeviceMemory and try to bind an image created with a CCS modifier. In
    * that case we cannot disable CCS if the alignment doesn´t meet the AUX-TT
    * requirements, so we need to ensure both the VkDeviceMemory and the
    * alignment reported through vkGetImageMemoryRequirements() meet the
    * AUX-TT requirement.
    *
    * Allocations with the special dynamic_visible mem type are for things like
    * descriptor buffers, so AUX-TT alignment is not needed here.
    */
   if (device->info->has_aux_map && !mem_type->dynamic_visible)
      alloc_flags |= ANV_BO_ALLOC_AUX_TT_ALIGNED;

   /* If the allocation is not dedicated nor a host pointer, allocate
    * additional CCS space.
    *
    * Allocations with the special dynamic_visible mem type are for things like
    * descriptor buffers, which don't need any compression.
    */
   if (device->physical->alloc_aux_tt_mem &&
       dedicated_info == NULL &&
       mem->vk.host_ptr == NULL &&
       !mem_type->dynamic_visible)
      alloc_flags |= ANV_BO_ALLOC_AUX_CCS;

   /* TODO: Android, ChromeOS and other applications may need another way to
    * allocate buffers that can be scanout to display but it should pretty
    * easy to catch those as Xe KMD driver will print warnings in dmesg when
    * scanning buffers allocated without proper flag set.
    */
   if (wsi_info)
      alloc_flags |= ANV_BO_ALLOC_SCANOUT;

   /* Anything imported or exported is EXTERNAL */
   if (mem->vk.export_handle_types || mem->vk.import_handle_type) {
      alloc_flags |= ANV_BO_ALLOC_EXTERNAL;

      /* wsi has its own way of synchronizing with the compositor */
      if (pdevice->instance->external_memory_implicit_sync &&
          !wsi_info && dedicated_info &&
          dedicated_info->image != VK_NULL_HANDLE) {
         ANV_FROM_HANDLE(anv_image, image, dedicated_info->image);

         /* Apply implicit sync to be compatible with clients relying on
          * implicit fencing. This matches the behavior in iris i915_batch
          * submit. An example client is VA-API (iHD), so only dedicated
          * image scenario has to be covered.
          */
         alloc_flags |= ANV_BO_ALLOC_IMPLICIT_SYNC;

         /* For color attachment, apply IMPLICIT_WRITE so a client on the
          * consumer side relying on implicit fencing can have a fence to
          * wait for render complete.
          */
         if (image->vk.usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
            alloc_flags |= ANV_BO_ALLOC_IMPLICIT_WRITE;
      }
   }

   /* TODO: Disabling compression on external bos will cause problems once we
    * have a modifier that supports compression (Xe2+).
    */
   if (!(alloc_flags & ANV_BO_ALLOC_EXTERNAL) && mem_type->compressed)
      alloc_flags |= ANV_BO_ALLOC_COMPRESSED;

   if (mem_type->dynamic_visible)
      alloc_flags |= ANV_BO_ALLOC_DYNAMIC_VISIBLE_POOL;

   if (mem->vk.ahardware_buffer) {
      result = anv_import_ahw_memory(_device, mem);
      if (result != VK_SUCCESS)
         goto fail;

      goto success;
   }

   /* The Vulkan spec permits handleType to be 0, in which case the struct is
    * ignored.
    */
   if (fd_info && fd_info->handleType) {
      /* At the moment, we support only the below handle types. */
      assert(fd_info->handleType ==
               VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT ||
             fd_info->handleType ==
               VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);

      result = anv_device_import_bo(device, fd_info->fd, alloc_flags,
                                    client_address, &mem->bo);
      if (result != VK_SUCCESS)
         goto fail;

      /* For security purposes, we reject importing the bo if it's smaller
       * than the requested allocation size.  This prevents a malicious client
       * from passing a buffer to a trusted client, lying about the size, and
       * telling the trusted client to try and texture from an image that goes
       * out-of-bounds.  This sort of thing could lead to GPU hangs or worse
       * in the trusted client.  The trusted client can protect itself against
       * this sort of attack but only if it can trust the buffer size.
       */
      if (mem->bo->size < aligned_alloc_size) {
         result = vk_errorf(device, VK_ERROR_INVALID_EXTERNAL_HANDLE,
                            "aligned allocationSize too large for "
                            "VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT: "
                            "%"PRIu64"B > %"PRIu64"B",
                            aligned_alloc_size, mem->bo->size);
         anv_device_release_bo(device, mem->bo);
         goto fail;
      }

      /* From the Vulkan spec:
       *
       *    "Importing memory from a file descriptor transfers ownership of
       *    the file descriptor from the application to the Vulkan
       *    implementation. The application must not perform any operations on
       *    the file descriptor after a successful import."
       *
       * If the import fails, we leave the file descriptor open.
       */
      close(fd_info->fd);
      goto success;
   }

   if (mem->vk.host_ptr) {
      if (mem->vk.import_handle_type ==
          VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_MAPPED_FOREIGN_MEMORY_BIT_EXT) {
         result = vk_error(device, VK_ERROR_INVALID_EXTERNAL_HANDLE);
         goto fail;
      }

      assert(mem->vk.import_handle_type ==
             VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT);

      result = anv_device_import_bo_from_host_ptr(device,
                                                  mem->vk.host_ptr,
                                                  mem->vk.size,
                                                  alloc_flags,
                                                  client_address,
                                                  &mem->bo);
      if (result != VK_SUCCESS)
         goto fail;

      goto success;
   }

   if (alloc_flags & (ANV_BO_ALLOC_EXTERNAL | ANV_BO_ALLOC_SCANOUT)) {
      alloc_flags |= ANV_BO_ALLOC_HOST_COHERENT;
   } else if (mem_type->propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
      if (mem_type->propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
         alloc_flags |= ANV_BO_ALLOC_HOST_COHERENT;
      if (mem_type->propertyFlags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT)
         alloc_flags |= ANV_BO_ALLOC_HOST_CACHED;
   } else {
      /* Required to set some host mode to have a valid pat index set */
      alloc_flags |= ANV_BO_ALLOC_HOST_COHERENT;
   }

   /* Regular allocate (not importing memory). */

   result = anv_device_alloc_bo(device, "user", pAllocateInfo->allocationSize,
                                alloc_flags, client_address, &mem->bo);
   if (result != VK_SUCCESS)
      goto fail;

   if (dedicated_info && dedicated_info->image != VK_NULL_HANDLE) {
      ANV_FROM_HANDLE(anv_image, image, dedicated_info->image);

      /* Some legacy (non-modifiers) consumers need the tiling to be set on
       * the BO.  In this case, we have a dedicated allocation.
       */
      if (image->vk.wsi_legacy_scanout) {
         const struct isl_surf *surf = &image->planes[0].primary_surface.isl;
         result = anv_device_set_bo_tiling(device, mem->bo,
                                           surf->row_pitch_B,
                                           surf->tiling);
         if (result != VK_SUCCESS) {
            anv_device_release_bo(device, mem->bo);
            goto fail;
         }
      }
   }

 success:
   mem_heap_used = p_atomic_add_return(&mem_heap->used, mem->bo->size);
   if (mem_heap_used > mem_heap->size) {
      p_atomic_add(&mem_heap->used, -mem->bo->size);
      anv_device_release_bo(device, mem->bo);
      result = vk_errorf(device, VK_ERROR_OUT_OF_DEVICE_MEMORY,
                         "Out of heap memory");
      goto fail;
   }

   pthread_mutex_lock(&device->mutex);
   list_addtail(&mem->link, &device->memory_objects);
   pthread_mutex_unlock(&device->mutex);

   ANV_RMV(heap_create, device, mem, false, 0);

   *pMem = anv_device_memory_to_handle(mem);

   return VK_SUCCESS;

 fail:
   vk_device_memory_destroy(&device->vk, pAllocator, &mem->vk);

   return result;
}

VkResult anv_GetMemoryFdKHR(
    VkDevice                                    device_h,
    const VkMemoryGetFdInfoKHR*                 pGetFdInfo,
    int*                                        pFd)
{
   ANV_FROM_HANDLE(anv_device, dev, device_h);
   ANV_FROM_HANDLE(anv_device_memory, mem, pGetFdInfo->memory);

   assert(pGetFdInfo->sType == VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR);

   assert(pGetFdInfo->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT ||
          pGetFdInfo->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);

   return anv_device_export_bo(dev, mem->bo, pFd);
}

VkResult anv_GetMemoryFdPropertiesKHR(
    VkDevice                                    _device,
    VkExternalMemoryHandleTypeFlagBits          handleType,
    int                                         fd,
    VkMemoryFdPropertiesKHR*                    pMemoryFdProperties)
{
   ANV_FROM_HANDLE(anv_device, device, _device);

   switch (handleType) {
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT:
      /* dma-buf can be imported as any memory type */
      pMemoryFdProperties->memoryTypeBits =
         (1 << device->physical->memory.type_count) - 1;
      return VK_SUCCESS;

   default:
      /* The valid usage section for this function says:
       *
       *    "handleType must not be one of the handle types defined as
       *    opaque."
       *
       * So opaque handle types fall into the default "unsupported" case.
       */
      return vk_error(device, VK_ERROR_INVALID_EXTERNAL_HANDLE);
   }
}

VkResult anv_GetMemoryHostPointerPropertiesEXT(
   VkDevice                                    _device,
   VkExternalMemoryHandleTypeFlagBits          handleType,
   const void*                                 pHostPointer,
   VkMemoryHostPointerPropertiesEXT*           pMemoryHostPointerProperties)
{
   ANV_FROM_HANDLE(anv_device, device, _device);

   assert(pMemoryHostPointerProperties->sType ==
          VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT);

   switch (handleType) {
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT:
      /* Host memory can be imported as any memory type. */
      pMemoryHostPointerProperties->memoryTypeBits =
         (1ull << device->physical->memory.type_count) - 1;

      return VK_SUCCESS;

   default:
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;
   }
}

void anv_FreeMemory(
    VkDevice                                    _device,
    VkDeviceMemory                              _mem,
    const VkAllocationCallbacks*                pAllocator)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_device_memory, mem, _mem);

   if (mem == NULL)
      return;

   pthread_mutex_lock(&device->mutex);
   list_del(&mem->link);
   pthread_mutex_unlock(&device->mutex);

   if (mem->map) {
      const VkMemoryUnmapInfoKHR unmap = {
         .sType = VK_STRUCTURE_TYPE_MEMORY_UNMAP_INFO_KHR,
         .memory = _mem,
      };
      anv_UnmapMemory2KHR(_device, &unmap);
   }

   p_atomic_add(&device->physical->memory.heaps[mem->type->heapIndex].used,
                -mem->bo->size);

   anv_device_release_bo(device, mem->bo);

   ANV_RMV(resource_destroy, device, mem);

   vk_device_memory_destroy(&device->vk, pAllocator, &mem->vk);
}

VkResult anv_MapMemory2KHR(
    VkDevice                                    _device,
    const VkMemoryMapInfoKHR*                   pMemoryMapInfo,
    void**                                      ppData)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_device_memory, mem, pMemoryMapInfo->memory);

   if (mem == NULL) {
      *ppData = NULL;
      return VK_SUCCESS;
   }

   if (mem->vk.host_ptr) {
      *ppData = mem->vk.host_ptr + pMemoryMapInfo->offset;
      return VK_SUCCESS;
   }

   /* From the Vulkan spec version 1.0.32 docs for MapMemory:
    *
    *  * memory must have been created with a memory type that reports
    *    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
    */
   if (!(mem->type->propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
      return vk_errorf(device, VK_ERROR_MEMORY_MAP_FAILED,
                       "Memory object not mappable.");
   }

   assert(pMemoryMapInfo->size > 0);
   const VkDeviceSize offset = pMemoryMapInfo->offset;
   const VkDeviceSize size =
      vk_device_memory_range(&mem->vk, pMemoryMapInfo->offset,
                                       pMemoryMapInfo->size);

   if (size != (size_t)size) {
      return vk_errorf(device, VK_ERROR_MEMORY_MAP_FAILED,
                       "requested size 0x%"PRIx64" does not fit in %u bits",
                       size, (unsigned)(sizeof(size_t) * 8));
   }

   /* From the Vulkan 1.2.194 spec:
    *
    *    "memory must not be currently host mapped"
    */
   if (mem->map != NULL) {
      return vk_errorf(device, VK_ERROR_MEMORY_MAP_FAILED,
                       "Memory object already mapped.");
   }

   void *placed_addr = NULL;
   if (pMemoryMapInfo->flags & VK_MEMORY_MAP_PLACED_BIT_EXT) {
      const VkMemoryMapPlacedInfoEXT *placed_info =
         vk_find_struct_const(pMemoryMapInfo->pNext, MEMORY_MAP_PLACED_INFO_EXT);
      assert(placed_info != NULL);
      placed_addr = placed_info->pPlacedAddress;
   }

   uint64_t map_offset, map_size;
   anv_sanitize_map_params(device, offset, size, &map_offset, &map_size);

   void *map;
   VkResult result = anv_device_map_bo(device, mem->bo, map_offset,
                                       map_size, placed_addr, &map);
   if (result != VK_SUCCESS)
      return result;

   mem->map = map;
   mem->map_size = map_size;
   mem->map_delta = (offset - map_offset);
   *ppData = mem->map + mem->map_delta;

   return VK_SUCCESS;
}

VkResult anv_UnmapMemory2KHR(
    VkDevice                                    _device,
    const VkMemoryUnmapInfoKHR*                 pMemoryUnmapInfo)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_device_memory, mem, pMemoryUnmapInfo->memory);

   if (mem == NULL || mem->vk.host_ptr)
      return VK_SUCCESS;

   VkResult result =
      anv_device_unmap_bo(device, mem->bo, mem->map, mem->map_size,
                          pMemoryUnmapInfo->flags & VK_MEMORY_UNMAP_RESERVE_BIT_EXT);
   if (result != VK_SUCCESS)
      return result;

   mem->map = NULL;
   mem->map_size = 0;
   mem->map_delta = 0;

   return VK_SUCCESS;
}

VkResult anv_FlushMappedMemoryRanges(
    VkDevice                                    _device,
    uint32_t                                    memoryRangeCount,
    const VkMappedMemoryRange*                  pMemoryRanges)
{
#ifdef SUPPORT_INTEL_INTEGRATED_GPUS
   ANV_FROM_HANDLE(anv_device, device, _device);

   if (!device->physical->memory.need_flush)
      return VK_SUCCESS;

   /* Make sure the writes we're flushing have landed. */
   __builtin_ia32_mfence();

   for (uint32_t i = 0; i < memoryRangeCount; i++) {
      ANV_FROM_HANDLE(anv_device_memory, mem, pMemoryRanges[i].memory);
      if (mem->type->propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
         continue;

      uint64_t map_offset = pMemoryRanges[i].offset + mem->map_delta;
      if (map_offset >= mem->map_size)
         continue;

      intel_flush_range(mem->map + map_offset,
                        MIN2(pMemoryRanges[i].size,
                             mem->map_size - map_offset));
   }
#endif
   return VK_SUCCESS;
}

VkResult anv_InvalidateMappedMemoryRanges(
    VkDevice                                    _device,
    uint32_t                                    memoryRangeCount,
    const VkMappedMemoryRange*                  pMemoryRanges)
{
#ifdef SUPPORT_INTEL_INTEGRATED_GPUS
   ANV_FROM_HANDLE(anv_device, device, _device);

   if (!device->physical->memory.need_flush)
      return VK_SUCCESS;

   for (uint32_t i = 0; i < memoryRangeCount; i++) {
      ANV_FROM_HANDLE(anv_device_memory, mem, pMemoryRanges[i].memory);
      if (mem->type->propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
         continue;

      uint64_t map_offset = pMemoryRanges[i].offset + mem->map_delta;
      if (map_offset >= mem->map_size)
         continue;

      intel_invalidate_range(mem->map + map_offset,
                             MIN2(pMemoryRanges[i].size,
                                  mem->map_size - map_offset));
   }

   /* Make sure no reads get moved up above the invalidate. */
   __builtin_ia32_mfence();
#endif
   return VK_SUCCESS;
}

void anv_GetDeviceMemoryCommitment(
    VkDevice                                    device,
    VkDeviceMemory                              memory,
    VkDeviceSize*                               pCommittedMemoryInBytes)
{
   *pCommittedMemoryInBytes = 0;
}

static inline clockid_t
anv_get_default_cpu_clock_id(void)
{
#ifdef CLOCK_MONOTONIC_RAW
   return CLOCK_MONOTONIC_RAW;
#else
   return CLOCK_MONOTONIC;
#endif
}

static inline clockid_t
vk_time_domain_to_clockid(VkTimeDomainKHR domain)
{
   switch (domain) {
#ifdef CLOCK_MONOTONIC_RAW
   case VK_TIME_DOMAIN_CLOCK_MONOTONIC_RAW_KHR:
      return CLOCK_MONOTONIC_RAW;
#endif
   case VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR:
      return CLOCK_MONOTONIC;
   default:
      unreachable("Missing");
      return CLOCK_MONOTONIC;
   }
}

static inline bool
is_cpu_time_domain(VkTimeDomainKHR domain)
{
   return domain == VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR ||
          domain == VK_TIME_DOMAIN_CLOCK_MONOTONIC_RAW_KHR;
}

static inline bool
is_gpu_time_domain(VkTimeDomainKHR domain)
{
   return domain == VK_TIME_DOMAIN_DEVICE_KHR;
}

VkResult anv_GetCalibratedTimestampsKHR(
   VkDevice                                     _device,
   uint32_t                                     timestampCount,
   const VkCalibratedTimestampInfoKHR           *pTimestampInfos,
   uint64_t                                     *pTimestamps,
   uint64_t                                     *pMaxDeviation)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   const uint64_t timestamp_frequency = device->info->timestamp_frequency;
   const uint64_t device_period = DIV_ROUND_UP(1000000000, timestamp_frequency);
   uint32_t d, increment;
   uint64_t begin, end;
   uint64_t max_clock_period = 0;
   const enum intel_kmd_type kmd_type = device->physical->info.kmd_type;
   const bool has_correlate_timestamp = kmd_type == INTEL_KMD_TYPE_XE;
   clockid_t cpu_clock_id = -1;

   begin = end = vk_clock_gettime(anv_get_default_cpu_clock_id());

   for (d = 0, increment = 1; d < timestampCount; d += increment) {
      const VkTimeDomainKHR current = pTimestampInfos[d].timeDomain;
      /* If we have a request pattern like this :
       * - domain0 = VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR or VK_TIME_DOMAIN_CLOCK_MONOTONIC_RAW_KHR
       * - domain1 = VK_TIME_DOMAIN_DEVICE_KHR
       * - domain2 = domain0 (optional)
       *
       * We can combine all of those into a single ioctl for maximum accuracy.
       */
      if (has_correlate_timestamp && (d + 1) < timestampCount) {
         const VkTimeDomainKHR next = pTimestampInfos[d + 1].timeDomain;

         if ((is_cpu_time_domain(current) && is_gpu_time_domain(next)) ||
             (is_gpu_time_domain(current) && is_cpu_time_domain(next))) {
            /* We'll consume at least 2 elements. */
            increment = 2;

            if (is_cpu_time_domain(current))
               cpu_clock_id = vk_time_domain_to_clockid(current);
            else
               cpu_clock_id = vk_time_domain_to_clockid(next);

            uint64_t cpu_timestamp, gpu_timestamp, cpu_delta_timestamp, cpu_end_timestamp;
            if (!intel_gem_read_correlate_cpu_gpu_timestamp(device->fd,
                                                            kmd_type,
                                                            INTEL_ENGINE_CLASS_RENDER,
                                                            0 /* engine_instance */,
                                                            cpu_clock_id,
                                                            &cpu_timestamp,
                                                            &gpu_timestamp,
                                                            &cpu_delta_timestamp))
               return vk_device_set_lost(&device->vk, "Failed to read correlate timestamp %m");

            cpu_end_timestamp = cpu_timestamp + cpu_delta_timestamp;
            if (is_cpu_time_domain(current)) {
               pTimestamps[d] = cpu_timestamp;
               pTimestamps[d + 1] = gpu_timestamp;
            } else {
               pTimestamps[d] = gpu_timestamp;
               pTimestamps[d + 1] = cpu_end_timestamp;
            }
            max_clock_period = MAX2(max_clock_period, device_period);

            /* If we can consume a third element */
            if ((d + 2) < timestampCount &&
                is_cpu_time_domain(current) &&
                current == pTimestampInfos[d + 2].timeDomain) {
               pTimestamps[d + 2] = cpu_end_timestamp;
               increment++;
            }

            /* If we're the first element, we can replace begin */
            if (d == 0 && cpu_clock_id == anv_get_default_cpu_clock_id())
               begin = cpu_timestamp;

            /* If we're in the same clock domain as begin/end. We can set the end. */
            if (cpu_clock_id == anv_get_default_cpu_clock_id())
               end = cpu_end_timestamp;

            continue;
         }
      }

      /* fallback to regular method */
      increment = 1;
      switch (current) {
      case VK_TIME_DOMAIN_DEVICE_KHR:
         if (!intel_gem_read_render_timestamp(device->fd,
                                              device->info->kmd_type,
                                              &pTimestamps[d])) {
            return vk_device_set_lost(&device->vk, "Failed to read the "
                                      "TIMESTAMP register: %m");
         }
         max_clock_period = MAX2(max_clock_period, device_period);
         break;
      case VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR:
         pTimestamps[d] = vk_clock_gettime(CLOCK_MONOTONIC);
         max_clock_period = MAX2(max_clock_period, 1);
         break;

#ifdef CLOCK_MONOTONIC_RAW
      case VK_TIME_DOMAIN_CLOCK_MONOTONIC_RAW_KHR:
         pTimestamps[d] = begin;
         break;
#endif
      default:
         pTimestamps[d] = 0;
         break;
      }
   }

   /* If last timestamp was not get with has_correlate_timestamp method or
    * if it was but last cpu clock is not the default one, get time again
    */
   if (increment == 1 || cpu_clock_id != anv_get_default_cpu_clock_id())
      end = vk_clock_gettime(anv_get_default_cpu_clock_id());

   *pMaxDeviation = vk_time_max_deviation(begin, end, max_clock_period);

   return VK_SUCCESS;
}

const struct intel_device_info_pat_entry *
anv_device_get_pat_entry(struct anv_device *device,
                         enum anv_bo_alloc_flags alloc_flags)
{
   if (alloc_flags & ANV_BO_ALLOC_IMPORTED)
      return &device->info->pat.cached_coherent;

   if (alloc_flags & ANV_BO_ALLOC_COMPRESSED)
      return &device->info->pat.compressed;

   /* PAT indexes has no actual effect in DG2 and DG1, smem caches will always
    * be snopped by GPU and lmem will always be WC.
    * This might change in future discrete platforms.
    */
   if (anv_physical_device_has_vram(device->physical)) {
      if (alloc_flags & ANV_BO_ALLOC_NO_LOCAL_MEM)
         return &device->info->pat.cached_coherent;
      return &device->info->pat.writecombining;
   }

   /* Integrated platforms handling only */
   if ((alloc_flags & (ANV_BO_ALLOC_HOST_CACHED_COHERENT)) == ANV_BO_ALLOC_HOST_CACHED_COHERENT)
      return &device->info->pat.cached_coherent;
   else if (alloc_flags & (ANV_BO_ALLOC_EXTERNAL | ANV_BO_ALLOC_SCANOUT))
      return &device->info->pat.scanout;
   else if (alloc_flags & ANV_BO_ALLOC_HOST_CACHED)
      return &device->info->pat.writeback_incoherent;
   else
      return &device->info->pat.writecombining;
}
