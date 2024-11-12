/*
 * Copyright © 2022 Intel Corporation
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

#include <anv_private.h>

/* Sparse binding handling.
 *
 * There is one main structure passed around all over this file:
 *
 * - struct anv_sparse_binding_data: every resource (VkBuffer or VkImage) has
 *   a pointer to an instance of this structure. It contains the virtual
 *   memory address (VMA) used by the binding operations (which is different
 *   from the VMA used by the anv_bo it's bound to) and the VMA range size. We
 *   do not keep record of our our list of bindings (which ranges were bound
 *   to which buffers).
 */

__attribute__((format(printf, 1, 2)))
static void
sparse_debug(const char *format, ...)
{
   if (!INTEL_DEBUG(DEBUG_SPARSE))
      return;

   va_list args;
   va_start(args, format);
   vfprintf(stderr, format, args);
   va_end(args);
}

static void
dump_anv_vm_bind(struct anv_device *device,
                 const struct anv_vm_bind *bind)
{
  sparse_debug("[%s] ", bind->op == ANV_VM_BIND ? " bind " : "unbind");

   if (bind->bo)
      sparse_debug("bo:%04u ", bind->bo->gem_handle);
   else
      sparse_debug("bo:---- ");
   sparse_debug("address:%016"PRIx64" size:%08"PRIx64" "
                "mem_offset:%08"PRIx64"\n",
                bind->address, bind->size, bind->bo_offset);
}

static void
dump_anv_image(struct anv_image *i)
{
   if (!INTEL_DEBUG(DEBUG_SPARSE))
      return;

   sparse_debug("anv_image:\n");
   sparse_debug("- format: %d\n", i->vk.format);
   sparse_debug("- extent: [%d, %d, %d]\n",
                i->vk.extent.width, i->vk.extent.height, i->vk.extent.depth);
   sparse_debug("- mip_levels: %d array_layers: %d samples: %d\n",
                i->vk.mip_levels, i->vk.array_layers, i->vk.samples);
   sparse_debug("- n_planes: %d\n", i->n_planes);
   sparse_debug("- disjoint: %d\n", i->disjoint);
}

static void
dump_isl_surf(struct isl_surf *s)
{
   if (!INTEL_DEBUG(DEBUG_SPARSE))
      return;

   sparse_debug("isl_surf:\n");

   const char *dim_s = s->dim == ISL_SURF_DIM_1D ? "1D" :
                       s->dim == ISL_SURF_DIM_2D ? "2D" :
                       s->dim == ISL_SURF_DIM_3D ? "3D" :
                       "(ERROR)";
   sparse_debug("- dim: %s\n", dim_s);
   sparse_debug("- tiling: %d (%s)\n", s->tiling,
                isl_tiling_to_name(s->tiling));
   sparse_debug("- format: %s\n", isl_format_get_short_name(s->format));
   sparse_debug("- image_alignment_el: [%d, %d, %d]\n",
                s->image_alignment_el.w, s->image_alignment_el.h,
                s->image_alignment_el.d);
   sparse_debug("- logical_level0_px: [%d, %d, %d, %d]\n",
                s->logical_level0_px.w,
                s->logical_level0_px.h,
                s->logical_level0_px.d,
                s->logical_level0_px.a);
   sparse_debug("- phys_level0_sa: [%d, %d, %d, %d]\n",
                s->phys_level0_sa.w,
                s->phys_level0_sa.h,
                s->phys_level0_sa.d,
                s->phys_level0_sa.a);
   sparse_debug("- levels: %d samples: %d\n", s->levels, s->samples);
   sparse_debug("- size_B: %"PRIu64" alignment_B: %u\n",
                s->size_B, s->alignment_B);
   sparse_debug("- row_pitch_B: %u\n", s->row_pitch_B);
   sparse_debug("- array_pitch_el_rows: %u\n", s->array_pitch_el_rows);

   const struct isl_format_layout *layout = isl_format_get_layout(s->format);
   sparse_debug("- format layout:\n");
   sparse_debug("  - format:%d bpb:%d bw:%d bh:%d bd:%d\n",
                layout->format, layout->bpb, layout->bw, layout->bh,
                layout->bd);

   struct isl_tile_info tile_info;
   isl_surf_get_tile_info(s, &tile_info);

   sparse_debug("- tile info:\n");
   sparse_debug("  - format_bpb: %d\n", tile_info.format_bpb);
   sparse_debug("  - logical_extent_el: [%d, %d, %d, %d]\n",
                tile_info.logical_extent_el.w,
                tile_info.logical_extent_el.h,
                tile_info.logical_extent_el.d,
                tile_info.logical_extent_el.a);
   sparse_debug("  - phys_extent_B: [%d, %d]\n",
                tile_info.phys_extent_B.w,
                tile_info.phys_extent_B.h);
}

static VkOffset3D
vk_offset3d_px_to_el(const VkOffset3D offset_px,
                     const struct isl_format_layout *layout)
{
   return (VkOffset3D) {
      .x = offset_px.x / layout->bw,
      .y = offset_px.y / layout->bh,
      .z = offset_px.z / layout->bd,
   };
}

static VkOffset3D
vk_offset3d_el_to_px(const VkOffset3D offset_el,
                     const struct isl_format_layout *layout)
{
   return (VkOffset3D) {
      .x = offset_el.x * layout->bw,
      .y = offset_el.y * layout->bh,
      .z = offset_el.z * layout->bd,
   };
}

static VkExtent3D
vk_extent3d_px_to_el(const VkExtent3D extent_px,
                     const struct isl_format_layout *layout)
{
   return (VkExtent3D) {
      .width = extent_px.width / layout->bw,
      .height = extent_px.height / layout->bh,
      .depth = extent_px.depth / layout->bd,
   };
}

static VkExtent3D
vk_extent3d_el_to_px(const VkExtent3D extent_el,
                     const struct isl_format_layout *layout)
{
   return (VkExtent3D) {
      .width = extent_el.width * layout->bw,
      .height = extent_el.height * layout->bh,
      .depth = extent_el.depth * layout->bd,
   };
}

static bool
isl_tiling_supports_standard_block_shapes(enum isl_tiling tiling)
{
   return isl_tiling_is_64(tiling) ||
          tiling == ISL_TILING_ICL_Ys ||
          tiling == ISL_TILING_SKL_Ys;
}

static uint32_t
isl_calc_tile_size(struct isl_tile_info *tile_info)
{
   uint32_t tile_size = tile_info->phys_extent_B.w *
                        tile_info->phys_extent_B.h;
   assert(tile_size == 64 * 1024 || tile_size == 4096 || tile_size == 1);
   return tile_size;
}

static const VkExtent3D block_shapes_2d_1sample[] = {
   /* 8 bits:   */ { .width = 256, .height = 256, .depth = 1 },
   /* 16 bits:  */ { .width = 256, .height = 128, .depth = 1 },
   /* 32 bits:  */ { .width = 128, .height = 128, .depth = 1 },
   /* 64 bits:  */ { .width = 128, .height =  64, .depth = 1 },
   /* 128 bits: */ { .width =  64, .height =  64, .depth = 1 },
};
static const VkExtent3D block_shapes_3d_1sample[] = {
   /* 8 bits:   */ { .width = 64, .height = 32, .depth = 32 },
   /* 16 bits:  */ { .width = 32, .height = 32, .depth = 32 },
   /* 32 bits:  */ { .width = 32, .height = 32, .depth = 16 },
   /* 64 bits:  */ { .width = 32, .height = 16, .depth = 16 },
   /* 128 bits: */ { .width = 16, .height = 16, .depth = 16 },
};
static const VkExtent3D block_shapes_2d_2samples[] = {
   /* 8 bits:   */ { .width = 128, .height = 256, .depth = 1 },
   /* 16 bits:  */ { .width = 128, .height = 128, .depth = 1 },
   /* 32 bits:  */ { .width =  64, .height = 128, .depth = 1 },
   /* 64 bits:  */ { .width =  64, .height =  64, .depth = 1 },
   /* 128 bits: */ { .width =  32, .height =  64, .depth = 1 },
};
static const VkExtent3D block_shapes_2d_4samples[] = {
   /* 8 bits:   */ { .width = 128, .height = 128, .depth = 1 },
   /* 16 bits:  */ { .width = 128, .height =  64, .depth = 1 },
   /* 32 bits:  */ { .width =  64, .height =  64, .depth = 1 },
   /* 64 bits:  */ { .width =  64, .height =  32, .depth = 1 },
   /* 128 bits: */ { .width =  32, .height =  32, .depth = 1 },
};
static const VkExtent3D block_shapes_2d_8samples[] = {
   /* 8 bits:   */ { .width = 64, .height = 128, .depth = 1 },
   /* 16 bits:  */ { .width = 64, .height =  64, .depth = 1 },
   /* 32 bits:  */ { .width = 32, .height =  64, .depth = 1 },
   /* 64 bits:  */ { .width = 32, .height =  32, .depth = 1 },
   /* 128 bits: */ { .width = 16, .height =  32, .depth = 1 },
};
static const VkExtent3D block_shapes_2d_16samples[] = {
   /* 8 bits:   */ { .width = 64, .height = 64, .depth = 1 },
   /* 16 bits:  */ { .width = 64, .height = 32, .depth = 1 },
   /* 32 bits:  */ { .width = 32, .height = 32, .depth = 1 },
   /* 64 bits:  */ { .width = 32, .height = 16, .depth = 1 },
   /* 128 bits: */ { .width = 16, .height = 16, .depth = 1 },
};

static VkExtent3D
anv_sparse_get_standard_image_block_shape(enum isl_format format,
                                          VkImageType image_type,
                                          VkSampleCountFlagBits samples,
                                          uint16_t texel_size)
{
   const struct isl_format_layout *layout = isl_format_get_layout(format);
   VkExtent3D block_shape = { .width = 0, .height = 0, .depth = 0 };

   int table_idx = ffs(texel_size) - 4;

   switch (samples) {
   case VK_SAMPLE_COUNT_1_BIT:
      switch (image_type) {
      case VK_IMAGE_TYPE_1D:
         /* 1D images don't have a standard block format. */
         assert(false);
         break;
      case VK_IMAGE_TYPE_2D:
         block_shape = block_shapes_2d_1sample[table_idx];
         break;
      case VK_IMAGE_TYPE_3D:
         block_shape = block_shapes_3d_1sample[table_idx];
         break;
      default:
         fprintf(stderr, "unexpected image_type %d\n", image_type);
         assert(false);
      }
      break;
   case VK_SAMPLE_COUNT_2_BIT:
      block_shape = block_shapes_2d_2samples[table_idx];
      break;
   case VK_SAMPLE_COUNT_4_BIT:
      block_shape = block_shapes_2d_4samples[table_idx];
      break;
   case VK_SAMPLE_COUNT_8_BIT:
      block_shape = block_shapes_2d_8samples[table_idx];
      break;
   case VK_SAMPLE_COUNT_16_BIT:
      block_shape = block_shapes_2d_16samples[table_idx];
      break;
   default:
      fprintf(stderr, "unexpected sample count: %d\n", samples);
      assert(false);
   }

   return vk_extent3d_el_to_px(block_shape, layout);
}

/* Adds "bind_op" to the list in "submit", while also trying to check if we
 * can just extend the last operation instead.
 */
static VkResult
anv_sparse_submission_add(struct anv_device *device,
                          struct anv_sparse_submission *submit,
                          struct anv_vm_bind *bind_op)
{
   struct anv_vm_bind *prev_bind = submit->binds_len == 0 ? NULL :
                                    &submit->binds[submit->binds_len - 1];

   if (prev_bind &&
       bind_op->op == prev_bind->op &&
       bind_op->bo == prev_bind->bo &&
       bind_op->address == prev_bind->address + prev_bind->size &&
       (bind_op->bo_offset == prev_bind->bo_offset + prev_bind->size ||
        prev_bind->bo == NULL)) {
      prev_bind->size += bind_op->size;
      return VK_SUCCESS;
   }

   if (submit->binds_len < submit->binds_capacity) {
      submit->binds[submit->binds_len++] = *bind_op;
      return VK_SUCCESS;
   }

   int new_capacity = MAX2(32, submit->binds_capacity * 2);
   struct anv_vm_bind *new_binds =
      vk_realloc(&device->vk.alloc, submit->binds,
                 new_capacity * sizeof(*new_binds), 8,
                 VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!new_binds)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   new_binds[submit->binds_len] = *bind_op;

   submit->binds = new_binds;
   submit->binds_len++;
   submit->binds_capacity = new_capacity;

   return VK_SUCCESS;
}

/* We really want to try to have all the page tables on as few BOs as possible
 * to benefit from cache locality and to keep the i915.ko relocation lists
 * small. On the other hand, we don't want to waste memory on unused space.
 */
#define ANV_TRTT_PAGE_TABLE_BO_SIZE (2 * 1024 * 1024)

static VkResult
trtt_make_page_table_bo(struct anv_device *device, struct anv_bo **bo)
{
   VkResult result;
   struct anv_trtt *trtt = &device->trtt;

   result = anv_device_alloc_bo(device, "trtt-page-table",
                                ANV_TRTT_PAGE_TABLE_BO_SIZE,
                                ANV_BO_ALLOC_INTERNAL,
                                0 /* explicit_address */, bo);
   if (result != VK_SUCCESS)
      return result;

   if (trtt->num_page_table_bos < trtt->page_table_bos_capacity) {
      trtt->page_table_bos[trtt->num_page_table_bos++] = *bo;
   } else {

      int new_capacity = MAX2(8, trtt->page_table_bos_capacity * 2);
      struct anv_bo **new_page_table_bos =
         vk_realloc(&device->vk.alloc, trtt->page_table_bos,
                    new_capacity * sizeof(*trtt->page_table_bos), 8,
                    VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
      if (!new_page_table_bos) {
         anv_device_release_bo(device, *bo);
         return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
      }

      new_page_table_bos[trtt->num_page_table_bos] = *bo;

      trtt->page_table_bos = new_page_table_bos;
      trtt->page_table_bos_capacity = new_capacity;
      trtt->num_page_table_bos++;
   }

   trtt->cur_page_table_bo = *bo;
   trtt->next_page_table_bo_offset = 0;

   sparse_debug("new number of page table BOs: %d\n",
                trtt->num_page_table_bos);

   return VK_SUCCESS;
}

static VkResult
trtt_get_page_table_bo(struct anv_device *device, struct anv_bo **bo,
                       uint64_t *bo_addr)
{
   struct anv_trtt *trtt = &device->trtt;
   VkResult result;

   if (!trtt->cur_page_table_bo) {
      result = trtt_make_page_table_bo(device, bo);
      if (result != VK_SUCCESS)
         return result;
   }

   *bo = trtt->cur_page_table_bo;
   *bo_addr = trtt->cur_page_table_bo->offset +
              trtt->next_page_table_bo_offset;

   trtt->next_page_table_bo_offset += 4096;
   if (trtt->next_page_table_bo_offset >= ANV_TRTT_PAGE_TABLE_BO_SIZE)
      trtt->cur_page_table_bo = NULL;

   return VK_SUCCESS;
}

/* For L3 and L2 pages, null and invalid entries are indicated by bits 1 and 0
 * respectively. For L1 entries, the hardware compares the addresses against
 * what we program to the GFX_TRTT_NULL and GFX_TRTT_INVAL registers.
 */
#define ANV_TRTT_L3L2_NULL_ENTRY (1 << 1)
#define ANV_TRTT_L3L2_INVALID_ENTRY (1 << 0)

static void
anv_trtt_bind_list_add_entry(struct util_dynarray *binds, uint64_t pte_addr,
                             uint64_t entry_addr)
{
   struct anv_trtt_bind b = {
      .pte_addr = pte_addr,
      .entry_addr = entry_addr,
   };
   util_dynarray_append(binds, struct anv_trtt_bind, b);
}

/* Adds elements to the anv_trtt_bind structs passed. This doesn't write the
 * entries to the HW yet.
 */
static VkResult
anv_trtt_bind_add(struct anv_device *device,
                  uint64_t trtt_addr, uint64_t dest_addr,
                  struct util_dynarray *l3l2_binds,
                  struct util_dynarray *l1_binds)
{
   VkResult result = VK_SUCCESS;
   struct anv_trtt *trtt = &device->trtt;
   bool is_null_bind = dest_addr == ANV_TRTT_L1_NULL_TILE_VAL;

   int l3_index = (trtt_addr >> 35) & 0x1FF;
   int l2_index = (trtt_addr >> 26) & 0x1FF;
   int l1_index = (trtt_addr >> 16) & 0x3FF;

   uint64_t l2_addr = trtt->l3_mirror[l3_index];
   if (l2_addr == ANV_TRTT_L3L2_NULL_ENTRY && is_null_bind) {
      return VK_SUCCESS;
   } else if (l2_addr == 0 || l2_addr == ANV_TRTT_L3L2_NULL_ENTRY) {
      if (is_null_bind) {
         trtt->l3_mirror[l3_index] = ANV_TRTT_L3L2_NULL_ENTRY;

         anv_trtt_bind_list_add_entry(l3l2_binds, trtt->l3_addr +
                                      l3_index * sizeof(uint64_t),
                                      ANV_TRTT_L3L2_NULL_ENTRY);

         return VK_SUCCESS;
      }

      struct anv_bo *l2_bo;
      result = trtt_get_page_table_bo(device, &l2_bo, &l2_addr);
      if (result != VK_SUCCESS)
         return result;

      trtt->l3_mirror[l3_index] = l2_addr;

      anv_trtt_bind_list_add_entry(l3l2_binds, trtt->l3_addr +
                                   l3_index * sizeof(uint64_t), l2_addr);

      /* We have just created a new L2 table. Other resources may already have
       * been pointing to this L2 table relying on the fact that it was marked
       * as NULL, so now we need to mark every one of its entries as NULL in
       * order to preserve behavior for those entries.
       */
      if (!util_dynarray_ensure_cap(l3l2_binds,
            l3l2_binds->capacity + 512 * sizeof(struct anv_trtt_bind)))
         return VK_ERROR_OUT_OF_HOST_MEMORY;

      for (int i = 0; i < 512; i++) {
         if (i != l2_index) {
            trtt->l2_mirror[l3_index * 512 + i] = ANV_TRTT_L3L2_NULL_ENTRY;
            anv_trtt_bind_list_add_entry(l3l2_binds,
                                         l2_addr + i * sizeof(uint64_t),
                                         ANV_TRTT_L3L2_NULL_ENTRY);
         }
      }
   }
   assert(l2_addr != 0 && l2_addr != ANV_TRTT_L3L2_NULL_ENTRY);

   /* The first page in the l2_mirror corresponds to l3_index=0 and so on. */
   uint64_t l1_addr = trtt->l2_mirror[l3_index * 512 + l2_index];
   if (l1_addr == ANV_TRTT_L3L2_NULL_ENTRY && is_null_bind) {
      return VK_SUCCESS;
   } else if (l1_addr == 0 || l1_addr == ANV_TRTT_L3L2_NULL_ENTRY) {
      if (is_null_bind) {
         trtt->l2_mirror[l3_index * 512 + l2_index] =
            ANV_TRTT_L3L2_NULL_ENTRY;

         anv_trtt_bind_list_add_entry(l3l2_binds,
                                      l2_addr + l2_index * sizeof(uint64_t),
                                      ANV_TRTT_L3L2_NULL_ENTRY);

         return VK_SUCCESS;
      }

      struct anv_bo *l1_bo;
      result = trtt_get_page_table_bo(device, &l1_bo, &l1_addr);
      if (result != VK_SUCCESS)
         return result;

      trtt->l2_mirror[l3_index * 512 + l2_index] = l1_addr;

      anv_trtt_bind_list_add_entry(l3l2_binds,
                                   l2_addr + l2_index * sizeof(uint64_t),
                                   l1_addr);
   }
   assert(l1_addr != 0 && l1_addr != ANV_TRTT_L3L2_NULL_ENTRY);

   anv_trtt_bind_list_add_entry(l1_binds,
                                l1_addr + l1_index * sizeof(uint32_t),
                                dest_addr);

   return VK_SUCCESS;
}

VkResult
anv_sparse_trtt_garbage_collect_batches(struct anv_device *device,
                                        bool wait_completion)
{
   struct anv_trtt *trtt = &device->trtt;

   uint64_t last_value;
   if (!wait_completion) {
      VkResult result =
         vk_sync_get_value(&device->vk, trtt->timeline, &last_value);
      if (result != VK_SUCCESS)
         return result;

      /* Valgrind doesn't know that drmSyncobjQuery writes to 'last_value' on
       * success.
       */
      VG(VALGRIND_MAKE_MEM_DEFINED(&last_value, sizeof(last_value)));
   } else {
      last_value = trtt->timeline_val;
   }

   list_for_each_entry_safe(struct anv_trtt_submission, submit,
                            &trtt->in_flight_batches, link) {
      if (submit->base.signal.signal_value <= last_value) {
         list_del(&submit->link);
         anv_async_submit_fini(&submit->base);
         vk_free(&device->vk.alloc, submit);
         continue;
      }

      if (!wait_completion)
         break;

      VkResult result = vk_sync_wait(
         &device->vk,
         submit->base.signal.sync,
         submit->base.signal.signal_value,
         VK_SYNC_WAIT_COMPLETE,
         os_time_get_absolute_timeout(OS_TIMEOUT_INFINITE));
      if (result == VK_SUCCESS) {
         list_del(&submit->link);
         anv_async_submit_fini(&submit->base);
         vk_free(&device->vk.alloc, submit);
         continue;
      }

      /* If the wait failed but the caller wanted completion, return the
       * error.
       */
      return result;
   }

   return VK_SUCCESS;
}

/* On success, this function initializes 'submit' and submits it, but doesn't
 * wait or free it. This allows the caller to submit multiple queues at the
 * same time before starting to wait for anything to complete.
 * If the function fails, the caller doesn't need to wait or fini anything,
 * just whatever other submissions may have succeeded in the past.
 */
static VkResult
anv_trtt_first_bind_init_queue(struct anv_queue *queue,
                               struct anv_async_submit *submit,
                               bool init_l3_table, struct anv_bo *l3_bo)
{
   struct anv_device *device = queue->device;
   struct anv_trtt *trtt = &device->trtt;
   VkResult result;

   result = anv_async_submit_init(submit, queue, &device->batch_bo_pool,
                                  false, true);
   if (result != VK_SUCCESS)
      return result;

   result = anv_genX(device->info, init_trtt_context_state)(submit);
   if (result != VK_SUCCESS)
      goto out_submit_fini;

   /* We only need to do this once, so pick the first queue. */
   if (init_l3_table) {
      struct anv_trtt_bind l3l2_binds_data[512];
      struct util_dynarray l3l2_binds;
      util_dynarray_init_from_stack(&l3l2_binds, l3l2_binds_data,
                                    sizeof(l3l2_binds_data));

      for (int entry = 0; entry < 512; entry++) {
         trtt->l3_mirror[entry] = ANV_TRTT_L3L2_NULL_ENTRY;
         anv_trtt_bind_list_add_entry(&l3l2_binds,
                                      trtt->l3_addr +
                                      entry * sizeof(uint64_t),
                                      ANV_TRTT_L3L2_NULL_ENTRY);
      }

      anv_genX(device->info, write_trtt_entries)(
         submit, l3l2_binds.data,
         util_dynarray_num_elements(&l3l2_binds, struct anv_trtt_bind),
         NULL, 0);

      result = anv_reloc_list_add_bo(&submit->relocs, l3_bo);
      if (result != VK_SUCCESS)
         goto out_submit_fini;
   }

   anv_genX(device->info, async_submit_end)(submit);

   result = device->kmd_backend->queue_exec_async(submit, 0, NULL, 1,
                                                  &submit->signal);
   if (result != VK_SUCCESS)
      goto out_submit_fini;

   /* If we succeed, it's our caller that's going to call
    * anv_async_submit_fini(). We do this so we can start waiting for the
    * submissions only after all the submissions are submitted.
    */
   return VK_SUCCESS;

out_submit_fini:
   /* If we fail, undo everything this function has done so the caller has
    * nothing to free.
    */
   anv_async_submit_fini(submit);
   return result;
}

/* There are lots of applications that request for sparse binding to be
 * enabled but never use it, so we choose to delay the initialization of TR-TT
 * until the moment we know we're going to need it.
 */
static VkResult
anv_trtt_first_bind_init(struct anv_device *device)
{
   struct anv_trtt *trtt = &device->trtt;
   VkResult result = VK_SUCCESS;

   /* TR-TT submission needs a queue even when the API entry point doesn't
    * provide one, such as resource creation. We pick this queue from the user
    * created queues at init_device_state() under anv_CreateDevice.
    *
    * It is technically possible for the user to create sparse resources even
    * when they don't have a sparse queue: they won't be able to bind the
    * resource but they should still be able to use the resource and rely on
    * its unbound behavior. We haven't spotted any real world application or
    * even test suite that exercises this behavior.
    *
    * For now let's just print an error message and return, which means that
    * resource creation will succeed but the behavior will be undefined if the
    * resource is used, which goes against our claim that we support the
    * sparseResidencyNonResidentStrict property.
    *
    * TODO: be fully spec-compliant here. Maybe have a device-internal queue
    * independent of the application's queues for the TR-TT operations.
    */
   if (unlikely(!trtt->queue)) {
      static bool warned = false;
      if (unlikely(!warned)) {
         fprintf(stderr, "FIXME: application has created a sparse resource "
                 "but no queues capable of binding sparse resources were "
                 "created. Using these resources will result in undefined "
                 "behavior.\n");
         warned = true;
      }
      return VK_SUCCESS;
   }

   simple_mtx_lock(&trtt->mutex);

   /* This means we have already initialized the first bind. */
   if (likely(trtt->l3_addr)) {
      simple_mtx_unlock(&trtt->mutex);
      return VK_SUCCESS;
   }

   struct anv_async_submit submits[device->queue_count];

   struct anv_bo *l3_bo;
   result = trtt_get_page_table_bo(device, &l3_bo, &trtt->l3_addr);
   if (result != VK_SUCCESS)
      goto out;

   trtt->l3_mirror = vk_zalloc(&device->vk.alloc, 4096, 8,
                                VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!trtt->l3_mirror) {
      result = vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto out;
   }

   /* L3 has 512 entries, so we can have up to 512 L2 tables. */
   trtt->l2_mirror = vk_zalloc(&device->vk.alloc, 512 * 4096, 8,
                               VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!trtt->l2_mirror) {
      vk_free(&device->vk.alloc, trtt->l3_mirror);
      result = vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto out;
   }

   int n_submits;
   for (n_submits = 0; n_submits < device->queue_count; n_submits++) {
      result = anv_trtt_first_bind_init_queue(&device->queues[n_submits],
                                              &submits[n_submits],
                                              n_submits == 0, l3_bo);
      if (result != VK_SUCCESS)
         break;
   }

   for (uint32_t i = 0; i < n_submits; i++) {
      anv_async_submit_wait(&submits[i]);
      anv_async_submit_fini(&submits[i]);
   }

out:
   if (result != VK_SUCCESS)
      trtt->l3_addr = 0;

   simple_mtx_unlock(&trtt->mutex);
   return result;
}

static VkResult
anv_sparse_bind_trtt(struct anv_device *device,
                     struct anv_sparse_submission *sparse_submit)
{
   struct anv_trtt *trtt = &device->trtt;
   VkResult result;

   /* See the same check at anv_trtt_first_bind_init(). */
   if (unlikely(!trtt->queue))
      return VK_SUCCESS;

   if (!sparse_submit->queue)
      sparse_submit->queue = trtt->queue;

   struct anv_trtt_submission *submit =
      vk_zalloc(&device->vk.alloc, sizeof(*submit), 8,
                VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (submit == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   result = anv_async_submit_init(&submit->base, sparse_submit->queue,
                                  &device->batch_bo_pool,
                                  false, false);
   if (result != VK_SUCCESS)
      goto out_async;

   simple_mtx_lock(&trtt->mutex);

   /* Do this so we can avoid reallocs later. */
   int l1_binds_capacity = 0;
   for (int b = 0; b < sparse_submit->binds_len; b++) {
      assert(sparse_submit->binds[b].size % (64 * 1024) == 0);
      int pages = sparse_submit->binds[b].size / (64 * 1024);
      l1_binds_capacity += pages;
   }

   /* Turn a series of virtual address maps, into a list of L3/L2/L1 TRTT page
    * table updates.
    */

   /* These are arrays of struct anv_trtt_bind. */
   struct util_dynarray l3l2_binds = {};
   struct util_dynarray l1_binds;

   if (l1_binds_capacity <= 32) {
      size_t alloc_size = l1_binds_capacity * sizeof(struct anv_trtt_bind);
      struct anv_trtt_bind *ptr = alloca(alloc_size);
      util_dynarray_init_from_stack(&l1_binds, ptr, alloc_size);
   } else {
      util_dynarray_init(&l1_binds, NULL);
      if (!util_dynarray_ensure_cap(&l1_binds,
            l1_binds_capacity * sizeof(struct anv_trtt_bind)))
         goto out_dynarrays;
   }

   for (int b = 0; b < sparse_submit->binds_len; b++) {
      struct anv_vm_bind *vm_bind = &sparse_submit->binds[b];
      for (uint64_t i = 0; i < vm_bind->size; i += 64 * 1024) {
         uint64_t trtt_addr = vm_bind->address + i;
         uint64_t dest_addr =
            (vm_bind->op == ANV_VM_BIND && vm_bind->bo) ?
               vm_bind->bo->offset + vm_bind->bo_offset + i :
               ANV_TRTT_L1_NULL_TILE_VAL;

         result = anv_trtt_bind_add(device, trtt_addr, dest_addr,
                                    &l3l2_binds, &l1_binds);
         if (result != VK_SUCCESS)
            goto out_dynarrays;
      }
   }

   /* Convert the L3/L2/L1 TRTT page table updates in anv_trtt_bind elements
    * into MI commands.
    */
   uint32_t n_l3l2_binds =
      util_dynarray_num_elements(&l3l2_binds, struct anv_trtt_bind);
   uint32_t n_l1_binds =
      util_dynarray_num_elements(&l1_binds, struct anv_trtt_bind);
   sparse_debug("trtt_binds: num_vm_binds:%02d l3l2:%04d l1:%04d\n",
                sparse_submit->binds_len, n_l3l2_binds, n_l1_binds);

   /* This is not an error, the application is simply trying to reset state
    * that was already there. */
   if (n_l3l2_binds == 0 && n_l1_binds == 0)
      goto out_dynarrays;

   anv_genX(device->info, write_trtt_entries)(&submit->base,
                                              l3l2_binds.data, n_l3l2_binds,
                                              l1_binds.data, n_l1_binds);

   util_dynarray_fini(&l1_binds);
   util_dynarray_fini(&l3l2_binds);

   anv_genX(device->info, async_submit_end)(&submit->base);

   if (submit->base.batch.status != VK_SUCCESS) {
      result = submit->base.batch.status;
      goto out_add_bind;
   }

   /* Add all the BOs backing TRTT page tables to the reloc list. */
   if (device->physical->uses_relocs) {
      for (int i = 0; i < trtt->num_page_table_bos; i++) {
         result = anv_reloc_list_add_bo(&submit->base.relocs,
                                        trtt->page_table_bos[i]);
         if (result != VK_SUCCESS)
            goto out_add_bind;
      }
   }

   anv_sparse_trtt_garbage_collect_batches(device, false);

   submit->base.signal = (struct vk_sync_signal) {
      .sync = trtt->timeline,
      .signal_value = ++trtt->timeline_val,
   };

   result =
      device->kmd_backend->queue_exec_async(&submit->base,
                                            sparse_submit->wait_count,
                                            sparse_submit->waits,
                                            sparse_submit->signal_count,
                                            sparse_submit->signals);
   if (result != VK_SUCCESS) {
      trtt->timeline_val--;
      goto out_add_bind;
   }

   list_addtail(&submit->link, &trtt->in_flight_batches);

   simple_mtx_unlock(&trtt->mutex);

   ANV_RMV(vm_binds, device, sparse_submit->binds, sparse_submit->binds_len);

   return VK_SUCCESS;

 out_dynarrays:
   util_dynarray_fini(&l1_binds);
   util_dynarray_fini(&l3l2_binds);
 out_add_bind:
   simple_mtx_unlock(&trtt->mutex);
   anv_async_submit_fini(&submit->base);
 out_async:
   vk_free(&device->vk.alloc, submit);
   return result;
}

static VkResult
anv_sparse_bind_vm_bind(struct anv_device *device,
                        struct anv_sparse_submission *submit)
{
   struct anv_queue *queue = submit->queue;

   VkResult result = device->kmd_backend->vm_bind(device, submit,
                                                  ANV_VM_BIND_FLAG_NONE);
   if (!queue) {
      assert(submit->wait_count == 0 && submit->signal_count == 0 &&
             submit->binds_len == 1);
      return result;
   }

   if (result == VK_ERROR_OUT_OF_HOST_MEMORY) {
      /* If we get this, the system is under memory pressure. First we
       * manually wait for all our dependency syncobjs hoping that some memory
       * will be released while we wait, then we try to issue each bind
       * operation in a single ioctl as it requires less Kernel memory and so
       * we may be able to move things forward, although slowly, while also
       * waiting for each operation to complete before issuing the next.
       * Performance isn't a concern at this point: we're just trying to move
       * progress forward without crashing until whatever is eating too much
       * memory goes away.
       */

      result = vk_sync_wait_many(&device->vk, submit->wait_count,
                                 submit->waits, VK_SYNC_WAIT_COMPLETE,
                                 INT64_MAX);
      if (result != VK_SUCCESS)
         return vk_queue_set_lost(&queue->vk, "vk_sync_wait_many failed");

      struct vk_sync *sync;
      result = vk_sync_create(&device->vk,
                              &device->physical->sync_syncobj_type,
                              VK_SYNC_IS_TIMELINE, 0 /* initial_value */,
                              &sync);
      if (result != VK_SUCCESS)
         return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

      for (int b = 0; b < submit->binds_len; b++) {
         struct vk_sync_signal sync_signal = {
            .sync = sync,
            .signal_value = b + 1,
         };
         struct anv_sparse_submission s = {
            .queue = submit->queue,
            .binds = &submit->binds[b],
            .binds_len = 1,
            .binds_capacity = 1,
            .wait_count = 0,
            .signal_count = 1,
            .waits = NULL,
            .signals = &sync_signal,
         };
         result = device->kmd_backend->vm_bind(device, &s,
                                               ANV_VM_BIND_FLAG_NONE);
         if (result != VK_SUCCESS) {
            vk_sync_destroy(&device->vk, sync);
            return vk_error(device, result); /* Well, at least we tried... */
         }

         result = vk_sync_wait(&device->vk, sync, sync_signal.signal_value,
                               VK_SYNC_WAIT_COMPLETE, UINT64_MAX);
         if (result != VK_SUCCESS) {
            vk_sync_destroy(&device->vk, sync);
            return vk_queue_set_lost(&queue->vk, "vk_sync_wait failed");
         }
      }

      vk_sync_destroy(&device->vk, sync);

      for (uint32_t i = 0; i < submit->signal_count; i++) {
         struct vk_sync_signal *s = &submit->signals[i];
         result = vk_sync_signal(&device->vk, s->sync, s->signal_value);
         if (result != VK_SUCCESS)
            return vk_queue_set_lost(&queue->vk, "vk_sync_signal failed");
      }
   }

   return VK_SUCCESS;
}

VkResult
anv_sparse_bind(struct anv_device *device,
                struct anv_sparse_submission *submit)
{
   if (INTEL_DEBUG(DEBUG_SPARSE)) {
      for (int b = 0; b < submit->binds_len; b++)
         dump_anv_vm_bind(device, &submit->binds[b]);
   }

   return device->physical->sparse_type == ANV_SPARSE_TYPE_TRTT ?
            anv_sparse_bind_trtt(device, submit) :
            anv_sparse_bind_vm_bind(device, submit);
}

VkResult
anv_init_sparse_bindings(struct anv_device *device,
                         uint64_t size_,
                         struct anv_sparse_binding_data *sparse,
                         enum anv_bo_alloc_flags alloc_flags,
                         uint64_t client_address,
                         struct anv_address *out_address)
{
   VkResult result;
   uint64_t size = align64(size_, ANV_SPARSE_BLOCK_SIZE);

   if (device->physical->sparse_type == ANV_SPARSE_TYPE_TRTT)
      alloc_flags |= ANV_BO_ALLOC_TRTT;

   sparse->address = anv_vma_alloc(device, size, ANV_SPARSE_BLOCK_SIZE,
                                   alloc_flags,
                                   intel_48b_address(client_address),
                                   &sparse->vma_heap);
   sparse->size = size;

   out_address->bo = NULL;
   out_address->offset = sparse->address;

   if (device->physical->sparse_type == ANV_SPARSE_TYPE_TRTT) {
      result = anv_trtt_first_bind_init(device);
      if (result != VK_SUCCESS)
         goto out_vma_free;
   } else {
      struct anv_vm_bind bind = {
         .bo = NULL, /* That's a NULL binding. */
         .address = sparse->address,
         .bo_offset = 0,
         .size = size,
         .op = ANV_VM_BIND,
      };
      struct anv_sparse_submission submit = {
         .queue = NULL,
         .binds = &bind,
         .binds_len = 1,
         .binds_capacity = 1,
         .wait_count = 0,
         .signal_count = 0,
      };
      result = anv_sparse_bind(device, &submit);
      if (result != VK_SUCCESS)
         goto out_vma_free;
   }

   p_atomic_inc(&device->num_sparse_resources);
   return VK_SUCCESS;

out_vma_free:
   anv_vma_free(device, sparse->vma_heap, sparse->address, sparse->size);
   return result;

}

void
anv_free_sparse_bindings(struct anv_device *device,
                         struct anv_sparse_binding_data *sparse)
{
   if (!sparse->address)
      return;

   sparse_debug("%s: address:0x%016"PRIx64" size:0x%08"PRIx64"\n",
                __func__, sparse->address, sparse->size);

   p_atomic_dec(&device->num_sparse_resources);

   struct anv_vm_bind unbind = {
      .bo = 0,
      .address = sparse->address,
      .bo_offset = 0,
      .size = sparse->size,
      .op = ANV_VM_UNBIND,
   };
   struct anv_sparse_submission submit = {
      .queue = NULL,
      .binds = &unbind,
      .binds_len = 1,
      .binds_capacity = 1,
      .wait_count = 0,
      .signal_count = 0,
   };
   VkResult res = anv_sparse_bind(device, &submit);

   /* Our callers don't have a way to signal failure to the upper layers, so
    * just keep the vma if we fail to unbind it. Still, let's have an
    * assertion because this really shouldn't be happening.
    */
   assert(res == VK_SUCCESS);
   if (res != VK_SUCCESS)
      return;

   anv_vma_free(device, sparse->vma_heap, sparse->address, sparse->size);
}

static VkExtent3D
anv_sparse_calc_block_shape(struct anv_physical_device *pdevice,
                            struct isl_surf *surf,
                            const struct isl_tile_info *tile_info)
{
   const struct isl_format_layout *layout =
      isl_format_get_layout(surf->format);

   VkExtent3D block_shape_el = {
      .width = tile_info->logical_extent_el.width,
      .height = tile_info->logical_extent_el.height,
      .depth = tile_info->logical_extent_el.depth,
   };
   VkExtent3D block_shape_px = vk_extent3d_el_to_px(block_shape_el, layout);

   assert(surf->tiling != ISL_TILING_LINEAR);

   return block_shape_px;
}

VkSparseImageFormatProperties
anv_sparse_calc_image_format_properties(struct anv_physical_device *pdevice,
                                        VkImageAspectFlags aspect,
                                        VkImageType vk_image_type,
                                        VkSampleCountFlagBits vk_samples,
                                        struct isl_surf *surf)
{
   const struct isl_format_layout *isl_layout =
      isl_format_get_layout(surf->format);
   struct isl_tile_info tile_info;
   isl_surf_get_tile_info(surf, &tile_info);
   const int bpb = isl_layout->bpb;
   assert(bpb == 8 || bpb == 16 || bpb == 32 || bpb == 64 ||bpb == 128);

   VkExtent3D granularity = anv_sparse_calc_block_shape(pdevice, surf,
                                                        &tile_info);
   bool is_standard = false;
   bool is_known_nonstandard_format = false;

   /* We shouldn't be able to reach this function with a 1D image. */
   assert(vk_image_type != VK_IMAGE_TYPE_1D);

   VkExtent3D std_shape =
      anv_sparse_get_standard_image_block_shape(surf->format,
                                                vk_image_type, vk_samples,
                                                bpb);
   /* YUV formats don't work with Tile64, which is required if we want to
    * claim standard block shapes. The spec requires us to support all
    * non-compressed color formats that non-sparse supports, so we can't just
    * say YUV formats are not supported by Sparse. So we end supporting this
    * format and anv_sparse_calc_miptail_properties() will say that everything
    * is part of the miptail.
    *
    * For more details on the hardware restriction, please check
    * isl_gfx125_filter_tiling().
    */
   if (pdevice->info.verx10 >= 125 && isl_format_is_yuv(surf->format))
      is_known_nonstandard_format = true;

   /* The standard block shapes (and by extension, the tiling formats they
    * require) are simply incompatible with getting a 2D view of a 3D image.
    */
   if (surf->usage & ISL_SURF_USAGE_2D_3D_COMPATIBLE_BIT)
      is_known_nonstandard_format = true;

   is_standard = granularity.width == std_shape.width &&
                 granularity.height == std_shape.height &&
                 granularity.depth == std_shape.depth;

   /* TODO: dEQP seems to care about the block shapes being standard even for
    * the cases where is_known_nonstandard_format is true. Luckily as of today
    * all of those cases are NotSupported but sooner or later we may end up
    * getting a failure.
    * Notice that in practice we report these cases as having the mip tail
    * starting on mip level 0, so the reported block shapes are irrelevant
    * since non-opaque binds are not supported. Still, dEQP seems to care.
    */
   assert(is_standard || is_known_nonstandard_format);
   assert(!(is_standard && is_known_nonstandard_format));

   bool wrong_block_size = isl_calc_tile_size(&tile_info) !=
                           ANV_SPARSE_BLOCK_SIZE;

   return (VkSparseImageFormatProperties) {
      .aspectMask = aspect,
      .imageGranularity = granularity,
      .flags = ((is_standard || is_known_nonstandard_format) ? 0 :
                  VK_SPARSE_IMAGE_FORMAT_NONSTANDARD_BLOCK_SIZE_BIT) |
               (wrong_block_size ? VK_SPARSE_IMAGE_FORMAT_SINGLE_MIPTAIL_BIT :
                  0),
   };
}

/* The miptail is supposed to be this region where the tiniest mip levels
 * are squished together in one single page, which should save us some memory.
 * It's a hardware feature which our hardware supports on certain tiling
 * formats - the ones we always want to use for sparse resources.
 *
 * For sparse, the main feature of the miptail is that it only supports opaque
 * binds, so you either bind the whole miptail or you bind nothing at all,
 * there are no subresources inside it to separately bind. While the idea is
 * that the miptail as reported by sparse should match what our hardware does,
 * in practice we can say in our sparse functions that certain mip levels are
 * part of the miptail while from the point of view of our hardwared they
 * aren't.
 *
 * If we detect we're using the sparse-friendly tiling formats and ISL
 * supports miptails for them, we can just trust the miptail level set by ISL
 * and things can proceed as The Spec intended.
 *
 * However, if that's not the case, we have to go on a best-effort policy. We
 * could simply declare that every mip level is part of the miptail and be
 * done, but since that kinda defeats the purpose of Sparse we try to find
 * what level we really should be reporting as the first miptail level based
 * on the alignments of the surface subresources.
 */
void
anv_sparse_calc_miptail_properties(struct anv_device *device,
                                   struct anv_image *image,
                                   VkImageAspectFlags vk_aspect,
                                   uint32_t *imageMipTailFirstLod,
                                   VkDeviceSize *imageMipTailSize,
                                   VkDeviceSize *imageMipTailOffset,
                                   VkDeviceSize *imageMipTailStride)
{
   const uint32_t plane = anv_image_aspect_to_plane(image, vk_aspect);
   struct isl_surf *surf = &image->planes[plane].primary_surface.isl;
   uint64_t binding_plane_offset =
      image->planes[plane].primary_surface.memory_range.offset;
   struct isl_tile_info tile_info;
   isl_surf_get_tile_info(surf, &tile_info);
   uint64_t layer1_offset;
   uint32_t x_off, y_off;

   /* Treat the whole thing as a single miptail. We should have already
    * reported this image as VK_SPARSE_IMAGE_FORMAT_SINGLE_MIPTAIL_BIT.
    *
    * In theory we could try to make ISL massage the alignments so that we
    * could at least claim mip level 0 to be not part of the miptail, but
    * that could end up wasting a lot of memory, so it's better to do
    * nothing and focus our efforts into making things use the appropriate
    * tiling formats that give us the standard block shapes.
    */
   if (isl_calc_tile_size(&tile_info) != ANV_SPARSE_BLOCK_SIZE)
      goto out_everything_is_miptail;

   assert(surf->tiling != ISL_TILING_LINEAR);

   if (image->vk.array_layers == 1) {
      layer1_offset = surf->size_B;
   } else {
      isl_surf_get_image_offset_B_tile_sa(surf, 0, 1, 0, &layer1_offset,
                                          &x_off, &y_off);
      if (x_off || y_off)
         goto out_everything_is_miptail;
   }
   assert(layer1_offset % ANV_SPARSE_BLOCK_SIZE == 0);

   /* We could try to do better here, but there's not really any point since
    * we should be supporting the appropriate tiling formats everywhere.
    */
   if (!isl_tiling_supports_standard_block_shapes(surf->tiling))
      goto out_everything_is_miptail;

   int miptail_first_level = surf->miptail_start_level;
   if (miptail_first_level >= image->vk.mip_levels)
      goto out_no_miptail;

   uint64_t miptail_offset = 0;
   isl_surf_get_image_offset_B_tile_sa(surf, miptail_first_level, 0, 0,
                                       &miptail_offset,
                                       &x_off, &y_off);
   assert(x_off == 0 && y_off == 0);
   assert(miptail_offset % ANV_SPARSE_BLOCK_SIZE == 0);

   *imageMipTailFirstLod = miptail_first_level;
   *imageMipTailSize = ANV_SPARSE_BLOCK_SIZE;
   *imageMipTailOffset = binding_plane_offset + miptail_offset;
   *imageMipTailStride = layer1_offset;
   goto out_debug;

out_no_miptail:
   *imageMipTailFirstLod = image->vk.mip_levels;
   *imageMipTailSize = 0;
   *imageMipTailOffset = 0;
   *imageMipTailStride = 0;
   goto out_debug;

out_everything_is_miptail:
   *imageMipTailFirstLod = 0;
   *imageMipTailSize = surf->size_B;
   *imageMipTailOffset = binding_plane_offset;
   *imageMipTailStride = 0;

out_debug:
   sparse_debug("miptail first_lod:%d size:%"PRIu64" offset:%"PRIu64" "
                "stride:%"PRIu64"\n",
                *imageMipTailFirstLod, *imageMipTailSize,
                *imageMipTailOffset, *imageMipTailStride);
}

static struct anv_vm_bind
vk_bind_to_anv_vm_bind(struct anv_sparse_binding_data *sparse,
                       const struct VkSparseMemoryBind *vk_bind)
{
   struct anv_vm_bind anv_bind = {
      .bo = NULL,
      .address = sparse->address + vk_bind->resourceOffset,
      .bo_offset = 0,
      .size = vk_bind->size,
      .op = ANV_VM_BIND,
   };

   assert(vk_bind->size);
   assert(vk_bind->resourceOffset + vk_bind->size <= sparse->size);

   if (vk_bind->memory != VK_NULL_HANDLE) {
      anv_bind.bo = anv_device_memory_from_handle(vk_bind->memory)->bo;
      anv_bind.bo_offset = vk_bind->memoryOffset,
      assert(vk_bind->memoryOffset + vk_bind->size <= anv_bind.bo->size);
   }

   return anv_bind;
}

static VkResult
anv_sparse_bind_resource_memory(struct anv_device *device,
                                struct anv_sparse_binding_data *sparse,
                                uint64_t resource_size,
                                const VkSparseMemoryBind *vk_bind,
                                struct anv_sparse_submission *submit)
{
   struct anv_vm_bind bind = vk_bind_to_anv_vm_bind(sparse, vk_bind);
   uint64_t rem = vk_bind->size % ANV_SPARSE_BLOCK_SIZE;

   if (rem != 0) {
      if (vk_bind->resourceOffset + vk_bind->size == resource_size)
         bind.size += ANV_SPARSE_BLOCK_SIZE - rem;
      else
         return vk_error(device, VK_ERROR_VALIDATION_FAILED_EXT);
   }

   return anv_sparse_submission_add(device, submit, &bind);
}

VkResult
anv_sparse_bind_buffer(struct anv_device *device,
                       struct anv_buffer *buffer,
                       const VkSparseMemoryBind *vk_bind,
                       struct anv_sparse_submission *submit)
{
   return anv_sparse_bind_resource_memory(device, &buffer->sparse_data,
                                          buffer->vk.size,
                                          vk_bind, submit);
}

VkResult
anv_sparse_bind_image_opaque(struct anv_device *device,
                             struct anv_image *image,
                             const VkSparseMemoryBind *vk_bind,
                             struct anv_sparse_submission *submit)
{
   struct anv_image_binding *b =
      &image->bindings[ANV_IMAGE_MEMORY_BINDING_MAIN];
   assert(!image->disjoint);

   if (INTEL_DEBUG(DEBUG_SPARSE)) {
      sparse_debug("%s:\n", __func__);
      dump_anv_image(image);
      u_foreach_bit(b, image->vk.aspects) {
         VkImageAspectFlagBits aspect = 1 << b;
         const uint32_t plane = anv_image_aspect_to_plane(image, aspect);
         struct isl_surf *surf = &image->planes[plane].primary_surface.isl;
         sparse_debug("aspect 0x%x (plane %d):\n", aspect, plane);
         dump_isl_surf(surf);
      }
      sparse_debug("\n");
   }

   return anv_sparse_bind_resource_memory(device, &b->sparse_data,
                                          b->memory_range.size,
                                          vk_bind, submit);
}

VkResult
anv_sparse_bind_image_memory(struct anv_queue *queue,
                             struct anv_image *image,
                             const VkSparseImageMemoryBind *bind,
                             struct anv_sparse_submission *submit)
{
   struct anv_device *device = queue->device;
   VkImageAspectFlags aspect = bind->subresource.aspectMask;
   uint32_t mip_level = bind->subresource.mipLevel;
   uint32_t array_layer = bind->subresource.arrayLayer;

   assert(!(bind->flags & VK_SPARSE_MEMORY_BIND_METADATA_BIT));

   struct anv_image_binding *img_binding = image->disjoint ?
      &image->bindings[anv_image_aspect_to_binding(image, aspect)] :
      &image->bindings[ANV_IMAGE_MEMORY_BINDING_MAIN];
   struct anv_sparse_binding_data *sparse_data = &img_binding->sparse_data;

   const uint32_t plane = anv_image_aspect_to_plane(image, aspect);
   struct isl_surf *surf = &image->planes[plane].primary_surface.isl;
   uint64_t binding_plane_offset =
      image->planes[plane].primary_surface.memory_range.offset;
   const struct isl_format_layout *layout =
      isl_format_get_layout(surf->format);
   struct isl_tile_info tile_info;
   isl_surf_get_tile_info(surf, &tile_info);

   if (INTEL_DEBUG(DEBUG_SPARSE)) {
      sparse_debug("%s:\n", __func__);
      sparse_debug("mip_level:%d array_layer:%d\n", mip_level, array_layer);
      sparse_debug("aspect:0x%x plane:%d\n", aspect, plane);
      sparse_debug("binding offset: [%d, %d, %d] extent: [%d, %d, %d]\n",
                   bind->offset.x, bind->offset.y, bind->offset.z,
                   bind->extent.width, bind->extent.height,
                   bind->extent.depth);
      dump_anv_image(image);
      dump_isl_surf(surf);
      sparse_debug("\n");
   }

   VkExtent3D block_shape_px =
      anv_sparse_calc_block_shape(device->physical, surf, &tile_info);
   VkExtent3D block_shape_el = vk_extent3d_px_to_el(block_shape_px, layout);

   /* Both bind->offset and bind->extent are in pixel units. */
   VkOffset3D bind_offset_el = vk_offset3d_px_to_el(bind->offset, layout);

   /* The spec says we only really need to align if for a given coordinate
    * offset + extent equals the corresponding dimensions of the image
    * subresource, but all the other non-aligned usage is invalid, so just
    * align everything.
    */
   VkExtent3D bind_extent_px = {
      .width = ALIGN_NPOT(bind->extent.width, block_shape_px.width),
      .height = ALIGN_NPOT(bind->extent.height, block_shape_px.height),
      .depth = ALIGN_NPOT(bind->extent.depth, block_shape_px.depth),
   };
   VkExtent3D bind_extent_el = vk_extent3d_px_to_el(bind_extent_px, layout);

   /* Nothing that has a tile_size different than ANV_SPARSE_BLOCK_SIZE should
    * be reaching here, as these cases should be treated as "everything is
    * part of the miptail" (see anv_sparse_calc_miptail_properties()).
    */
   assert(isl_calc_tile_size(&tile_info) == ANV_SPARSE_BLOCK_SIZE);

   /* How many blocks are necessary to form a whole line on this image? */
   const uint32_t blocks_per_line = surf->row_pitch_B / (layout->bpb / 8) /
                                    block_shape_el.width;
   /* The loop below will try to bind a whole line of blocks at a time as
    * they're guaranteed to be contiguous, so we calculate how many blocks
    * that is and how big is each block to figure the bind size of a whole
    * line.
    */
   uint64_t line_bind_size_in_blocks = bind_extent_el.width /
                                       block_shape_el.width;
   uint64_t line_bind_size = line_bind_size_in_blocks * ANV_SPARSE_BLOCK_SIZE;
   assert(line_bind_size_in_blocks != 0);
   assert(line_bind_size != 0);

   uint64_t memory_offset = bind->memoryOffset;
   for (uint32_t z = bind_offset_el.z;
        z < bind_offset_el.z + bind_extent_el.depth;
        z += block_shape_el.depth) {
      uint64_t subresource_offset_B;
      uint32_t subresource_x_offset, subresource_y_offset;
      isl_surf_get_image_offset_B_tile_sa(surf, mip_level, array_layer, z,
                                          &subresource_offset_B,
                                          &subresource_x_offset,
                                          &subresource_y_offset);
      assert(subresource_x_offset == 0 && subresource_y_offset == 0);
      assert(subresource_offset_B % ANV_SPARSE_BLOCK_SIZE == 0);

      for (uint32_t y = bind_offset_el.y;
           y < bind_offset_el.y + bind_extent_el.height;
           y+= block_shape_el.height) {
         uint32_t line_block_offset = y / block_shape_el.height *
                                      blocks_per_line;
         uint64_t line_start_B = subresource_offset_B +
                                 line_block_offset * ANV_SPARSE_BLOCK_SIZE;
         uint64_t bind_offset_B = line_start_B +
                                  (bind_offset_el.x / block_shape_el.width) *
                                  ANV_SPARSE_BLOCK_SIZE;

         VkSparseMemoryBind opaque_bind = {
            .resourceOffset = binding_plane_offset + bind_offset_B,
            .size = line_bind_size,
            .memory = bind->memory,
            .memoryOffset = memory_offset,
            .flags = bind->flags,
         };

         memory_offset += line_bind_size;

         assert(line_start_B % ANV_SPARSE_BLOCK_SIZE == 0);
         assert(opaque_bind.resourceOffset % ANV_SPARSE_BLOCK_SIZE == 0);
         assert(opaque_bind.size % ANV_SPARSE_BLOCK_SIZE == 0);

         struct anv_vm_bind anv_bind = vk_bind_to_anv_vm_bind(sparse_data,
                                                              &opaque_bind);
         VkResult result = anv_sparse_submission_add(device, submit,
                                                     &anv_bind);
         if (result != VK_SUCCESS)
            return result;
      }
   }

   return VK_SUCCESS;
}

VkResult
anv_sparse_image_check_support(struct anv_physical_device *pdevice,
                               VkImageCreateFlags flags,
                               VkImageTiling tiling,
                               VkSampleCountFlagBits samples,
                               VkImageType type,
                               VkFormat vk_format)
{
   assert(flags & VK_IMAGE_CREATE_SPARSE_BINDING_BIT);

   /* The spec says:
    *   "A sparse image created using VK_IMAGE_CREATE_SPARSE_BINDING_BIT (but
    *    not VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT) supports all formats that
    *    non-sparse usage supports, and supports both VK_IMAGE_TILING_OPTIMAL
    *    and VK_IMAGE_TILING_LINEAR tiling."
    */
   if (!(flags & VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT))
      return VK_SUCCESS;

   if (type == VK_IMAGE_TYPE_1D)
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   /* From here on, these are the rules:
    *   "A sparse image created using VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT
    *    supports all non-compressed color formats with power-of-two element
    *    size that non-sparse usage supports. Additional formats may also be
    *    supported and can be queried via
    *    vkGetPhysicalDeviceSparseImageFormatProperties.
    *    VK_IMAGE_TILING_LINEAR tiling is not supported."
    */

   /* We choose not to support sparse residency on emulated compressed
    * formats due to the additional image plane. It would make the
    * implementation extremely complicated.
    */
   if (anv_is_format_emulated(pdevice, vk_format))
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   /* While the spec itself says linear is not supported (see above), deqp-vk
    * tries anyway to create linear sparse images, so we have to check for it.
    * This is also said in VUID-VkImageCreateInfo-tiling-04121:
    *   "If tiling is VK_IMAGE_TILING_LINEAR, flags must not contain
    *    VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT"
    */
   if (tiling == VK_IMAGE_TILING_LINEAR)
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   if ((samples & VK_SAMPLE_COUNT_2_BIT &&
        !pdevice->vk.supported_features.sparseResidency2Samples) ||
       (samples & VK_SAMPLE_COUNT_4_BIT &&
        !pdevice->vk.supported_features.sparseResidency4Samples) ||
       (samples & VK_SAMPLE_COUNT_8_BIT &&
        !pdevice->vk.supported_features.sparseResidency8Samples) ||
       (samples & VK_SAMPLE_COUNT_16_BIT &&
        !pdevice->vk.supported_features.sparseResidency16Samples) ||
       samples & VK_SAMPLE_COUNT_32_BIT ||
       samples & VK_SAMPLE_COUNT_64_BIT)
      return VK_ERROR_FEATURE_NOT_PRESENT;

   /* While the Vulkan spec allows us to support depth/stencil sparse images
    * everywhere, sometimes we're not able to have them with the tiling
    * formats that give us the standard block shapes. Having standard block
    * shapes is higher priority than supporting depth/stencil sparse images.
    *
    * Please see ISL's filter_tiling() functions for accurate explanations on
    * why depth/stencil images are not always supported with the tiling
    * formats we want. But in short: depth/stencil support in our HW is
    * limited to 2D and we can't build a 2D view of a 3D image with these
    * tiling formats due to the address swizzling being different.
    */
   VkImageAspectFlags aspects = vk_format_aspects(vk_format);
   if (aspects & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
      /* For multi-sampled images, the image layouts for color and
       * depth/stencil are different, and only the color layout is compatible
       * with the standard block shapes.
       */
      if (samples != VK_SAMPLE_COUNT_1_BIT)
         return VK_ERROR_FORMAT_NOT_SUPPORTED;

      /* For 125+, isl_gfx125_filter_tiling() claims 3D is not supported.
       * For the previous platforms, isl_gfx6_filter_tiling() says only 2D is
       * supported.
       */
      if (pdevice->info.verx10 >= 125) {
         if (type == VK_IMAGE_TYPE_3D)
            return VK_ERROR_FORMAT_NOT_SUPPORTED;
      } else {
         if (type != VK_IMAGE_TYPE_2D)
            return VK_ERROR_FORMAT_NOT_SUPPORTED;
      }
   }

   const struct anv_format *anv_format = anv_get_format(vk_format);
   if (!anv_format)
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   for (int p = 0; p < anv_format->n_planes; p++) {
      enum isl_format isl_format = anv_format->planes[p].isl_format;

      if (isl_format == ISL_FORMAT_UNSUPPORTED)
         return VK_ERROR_FORMAT_NOT_SUPPORTED;

      const struct isl_format_layout *isl_layout =
         isl_format_get_layout(isl_format);

      /* As quoted above, we only need to support the power-of-two formats.
       * The problem with the non-power-of-two formats is that we need an
       * integer number of pixels to fit into a sparse block, so we'd need the
       * sparse block sizes to be, for example, 192k for 24bpp.
       *
       * TODO: add support for these formats.
       */
      if (isl_layout->bpb != 8 && isl_layout->bpb != 16 &&
          isl_layout->bpb != 32 && isl_layout->bpb != 64 &&
          isl_layout->bpb != 128)
         return VK_ERROR_FORMAT_NOT_SUPPORTED;

      /* ISL_TILING_64_XE2_BIT's block shapes are not always Vulkan's standard
       * block shapes, so exclude what's non-standard.
       */
      if (pdevice->info.ver == 20) {
         switch (samples) {
         case VK_SAMPLE_COUNT_2_BIT:
            if (isl_layout->bpb == 128)
               return VK_ERROR_FORMAT_NOT_SUPPORTED;
            break;
         case VK_SAMPLE_COUNT_8_BIT:
             if (isl_layout->bpb == 8 || isl_layout->bpb == 32)
               return VK_ERROR_FORMAT_NOT_SUPPORTED;
            break;
         case VK_SAMPLE_COUNT_16_BIT:
            if (isl_layout->bpb == 64)
               return VK_ERROR_FORMAT_NOT_SUPPORTED;
            break;
         default:
            break;
         }
      }
   }

   /* These YUV formats are considered by Vulkan to be compressed 2x1 blocks.
    * We don't need to support them since they're compressed. On Gfx12 we
    * can't even have Tile64 for them. Once we do support these formats we'll
    * have to report the correct block shapes because dEQP cares about them,
    * and we'll have to adjust for the fact that ISL treats these as 16bpp 1x1
    * blocks instead of 32bpp 2x1 compressed blocks (as block shapes are
    * reported in units of compressed blocks).
    */
   if (vk_format == VK_FORMAT_G8B8G8R8_422_UNORM ||
       vk_format == VK_FORMAT_B8G8R8G8_422_UNORM)
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   return VK_SUCCESS;
}
