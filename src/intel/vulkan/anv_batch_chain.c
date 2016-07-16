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
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "anv_private.h"

#include "genxml/gen7_pack.h"
#include "genxml/gen8_pack.h"

/** \file anv_batch_chain.c
 *
 * This file contains functions related to anv_cmd_buffer as a data
 * structure.  This involves everything required to create and destroy
 * the actual batch buffers as well as link them together and handle
 * relocations and surface state.  It specifically does *not* contain any
 * handling of actual vkCmd calls beyond vkCmdExecuteCommands.
 */

/*-----------------------------------------------------------------------*
 * Functions related to anv_reloc_list
 *-----------------------------------------------------------------------*/

static VkResult
anv_reloc_list_init_clone(struct anv_reloc_list *list,
                          const VkAllocationCallbacks *alloc,
                          const struct anv_reloc_list *other_list)
{
   if (other_list) {
      list->num_relocs = other_list->num_relocs;
      list->array_length = other_list->array_length;
   } else {
      list->num_relocs = 0;
      list->array_length = 256;
   }

   list->relocs =
      anv_alloc(alloc, list->array_length * sizeof(*list->relocs), 8,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

   if (list->relocs == NULL)
      return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

   list->reloc_bos =
      anv_alloc(alloc, list->array_length * sizeof(*list->reloc_bos), 8,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

   if (list->reloc_bos == NULL) {
      anv_free(alloc, list->relocs);
      return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   if (other_list) {
      memcpy(list->relocs, other_list->relocs,
             list->array_length * sizeof(*list->relocs));
      memcpy(list->reloc_bos, other_list->reloc_bos,
             list->array_length * sizeof(*list->reloc_bos));
   }

   return VK_SUCCESS;
}

VkResult
anv_reloc_list_init(struct anv_reloc_list *list,
                    const VkAllocationCallbacks *alloc)
{
   return anv_reloc_list_init_clone(list, alloc, NULL);
}

void
anv_reloc_list_finish(struct anv_reloc_list *list,
                      const VkAllocationCallbacks *alloc)
{
   anv_free(alloc, list->relocs);
   anv_free(alloc, list->reloc_bos);
}

static VkResult
anv_reloc_list_grow(struct anv_reloc_list *list,
                    const VkAllocationCallbacks *alloc,
                    size_t num_additional_relocs)
{
   if (list->num_relocs + num_additional_relocs <= list->array_length)
      return VK_SUCCESS;

   size_t new_length = list->array_length * 2;
   while (new_length < list->num_relocs + num_additional_relocs)
      new_length *= 2;

   struct drm_i915_gem_relocation_entry *new_relocs =
      anv_alloc(alloc, new_length * sizeof(*list->relocs), 8,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (new_relocs == NULL)
      return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

   struct anv_bo **new_reloc_bos =
      anv_alloc(alloc, new_length * sizeof(*list->reloc_bos), 8,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (new_reloc_bos == NULL) {
      anv_free(alloc, new_relocs);
      return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   memcpy(new_relocs, list->relocs, list->num_relocs * sizeof(*list->relocs));
   memcpy(new_reloc_bos, list->reloc_bos,
          list->num_relocs * sizeof(*list->reloc_bos));

   anv_free(alloc, list->relocs);
   anv_free(alloc, list->reloc_bos);

   list->array_length = new_length;
   list->relocs = new_relocs;
   list->reloc_bos = new_reloc_bos;

   return VK_SUCCESS;
}

uint64_t
anv_reloc_list_add(struct anv_reloc_list *list,
                   const VkAllocationCallbacks *alloc,
                   uint32_t offset, struct anv_bo *target_bo, uint32_t delta)
{
   struct drm_i915_gem_relocation_entry *entry;
   int index;

   const uint32_t domain =
      target_bo->is_winsys_bo ? I915_GEM_DOMAIN_RENDER : 0;

   anv_reloc_list_grow(list, alloc, 1);
   /* TODO: Handle failure */

   /* XXX: Can we use I915_EXEC_HANDLE_LUT? */
   index = list->num_relocs++;
   list->reloc_bos[index] = target_bo;
   entry = &list->relocs[index];
   entry->target_handle = target_bo->gem_handle;
   entry->delta = delta;
   entry->offset = offset;
   entry->presumed_offset = target_bo->offset;
   entry->read_domains = domain;
   entry->write_domain = domain;
   VG(VALGRIND_CHECK_MEM_IS_DEFINED(entry, sizeof(*entry)));

   return target_bo->offset + delta;
}

static void
anv_reloc_list_append(struct anv_reloc_list *list,
                      const VkAllocationCallbacks *alloc,
                      struct anv_reloc_list *other, uint32_t offset)
{
   anv_reloc_list_grow(list, alloc, other->num_relocs);
   /* TODO: Handle failure */

   memcpy(&list->relocs[list->num_relocs], &other->relocs[0],
          other->num_relocs * sizeof(other->relocs[0]));
   memcpy(&list->reloc_bos[list->num_relocs], &other->reloc_bos[0],
          other->num_relocs * sizeof(other->reloc_bos[0]));

   for (uint32_t i = 0; i < other->num_relocs; i++)
      list->relocs[i + list->num_relocs].offset += offset;

   list->num_relocs += other->num_relocs;
}

/*-----------------------------------------------------------------------*
 * Functions related to anv_batch
 *-----------------------------------------------------------------------*/

void *
anv_batch_emit_dwords(struct anv_batch *batch, int num_dwords)
{
   if (batch->next + num_dwords * 4 > batch->end)
      batch->extend_cb(batch, batch->user_data);

   void *p = batch->next;

   batch->next += num_dwords * 4;
   assert(batch->next <= batch->end);

   return p;
}

uint64_t
anv_batch_emit_reloc(struct anv_batch *batch,
                     void *location, struct anv_bo *bo, uint32_t delta)
{
   return anv_reloc_list_add(batch->relocs, batch->alloc,
                             location - batch->start, bo, delta);
}

void
anv_batch_emit_batch(struct anv_batch *batch, struct anv_batch *other)
{
   uint32_t size, offset;

   size = other->next - other->start;
   assert(size % 4 == 0);

   if (batch->next + size > batch->end)
      batch->extend_cb(batch, batch->user_data);

   assert(batch->next + size <= batch->end);

   VG(VALGRIND_CHECK_MEM_IS_DEFINED(other->start, size));
   memcpy(batch->next, other->start, size);

   offset = batch->next - batch->start;
   anv_reloc_list_append(batch->relocs, batch->alloc,
                         other->relocs, offset);

   batch->next += size;
}

/*-----------------------------------------------------------------------*
 * Functions related to anv_batch_bo
 *-----------------------------------------------------------------------*/

static VkResult
anv_batch_bo_create(struct anv_cmd_buffer *cmd_buffer,
                    struct anv_batch_bo **bbo_out)
{
   VkResult result;

   struct anv_batch_bo *bbo = anv_alloc(&cmd_buffer->pool->alloc, sizeof(*bbo),
                                        8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (bbo == NULL)
      return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

   result = anv_bo_pool_alloc(&cmd_buffer->device->batch_bo_pool, &bbo->bo,
                              ANV_CMD_BUFFER_BATCH_SIZE);
   if (result != VK_SUCCESS)
      goto fail_alloc;

   result = anv_reloc_list_init(&bbo->relocs, &cmd_buffer->pool->alloc);
   if (result != VK_SUCCESS)
      goto fail_bo_alloc;

   *bbo_out = bbo;

   return VK_SUCCESS;

 fail_bo_alloc:
   anv_bo_pool_free(&cmd_buffer->device->batch_bo_pool, &bbo->bo);
 fail_alloc:
   anv_free(&cmd_buffer->pool->alloc, bbo);

   return result;
}

static VkResult
anv_batch_bo_clone(struct anv_cmd_buffer *cmd_buffer,
                   const struct anv_batch_bo *other_bbo,
                   struct anv_batch_bo **bbo_out)
{
   VkResult result;

   struct anv_batch_bo *bbo = anv_alloc(&cmd_buffer->pool->alloc, sizeof(*bbo),
                                        8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (bbo == NULL)
      return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

   result = anv_bo_pool_alloc(&cmd_buffer->device->batch_bo_pool, &bbo->bo,
                              other_bbo->bo.size);
   if (result != VK_SUCCESS)
      goto fail_alloc;

   result = anv_reloc_list_init_clone(&bbo->relocs, &cmd_buffer->pool->alloc,
                                      &other_bbo->relocs);
   if (result != VK_SUCCESS)
      goto fail_bo_alloc;

   bbo->length = other_bbo->length;
   memcpy(bbo->bo.map, other_bbo->bo.map, other_bbo->length);

   bbo->last_ss_pool_bo_offset = other_bbo->last_ss_pool_bo_offset;

   *bbo_out = bbo;

   return VK_SUCCESS;

 fail_bo_alloc:
   anv_bo_pool_free(&cmd_buffer->device->batch_bo_pool, &bbo->bo);
 fail_alloc:
   anv_free(&cmd_buffer->pool->alloc, bbo);

   return result;
}

static void
anv_batch_bo_start(struct anv_batch_bo *bbo, struct anv_batch *batch,
                   size_t batch_padding)
{
   batch->next = batch->start = bbo->bo.map;
   batch->end = bbo->bo.map + bbo->bo.size - batch_padding;
   batch->relocs = &bbo->relocs;
   bbo->last_ss_pool_bo_offset = 0;
   bbo->relocs.num_relocs = 0;
}

static void
anv_batch_bo_continue(struct anv_batch_bo *bbo, struct anv_batch *batch,
                      size_t batch_padding)
{
   batch->start = bbo->bo.map;
   batch->next = bbo->bo.map + bbo->length;
   batch->end = bbo->bo.map + bbo->bo.size - batch_padding;
   batch->relocs = &bbo->relocs;
}

static void
anv_batch_bo_finish(struct anv_batch_bo *bbo, struct anv_batch *batch)
{
   assert(batch->start == bbo->bo.map);
   bbo->length = batch->next - batch->start;
   VG(VALGRIND_CHECK_MEM_IS_DEFINED(batch->start, bbo->length));
}

static VkResult
anv_batch_bo_grow(struct anv_cmd_buffer *cmd_buffer, struct anv_batch_bo *bbo,
                  struct anv_batch *batch, size_t aditional,
                  size_t batch_padding)
{
   assert(batch->start == bbo->bo.map);
   bbo->length = batch->next - batch->start;

   size_t new_size = bbo->bo.size;
   while (new_size <= bbo->length + aditional + batch_padding)
      new_size *= 2;

   if (new_size == bbo->bo.size)
      return VK_SUCCESS;

   struct anv_bo new_bo;
   VkResult result = anv_bo_pool_alloc(&cmd_buffer->device->batch_bo_pool,
                                       &new_bo, new_size);
   if (result != VK_SUCCESS)
      return result;

   memcpy(new_bo.map, bbo->bo.map, bbo->length);

   anv_bo_pool_free(&cmd_buffer->device->batch_bo_pool, &bbo->bo);

   bbo->bo = new_bo;
   anv_batch_bo_continue(bbo, batch, batch_padding);

   return VK_SUCCESS;
}

static void
anv_batch_bo_destroy(struct anv_batch_bo *bbo,
                     struct anv_cmd_buffer *cmd_buffer)
{
   anv_reloc_list_finish(&bbo->relocs, &cmd_buffer->pool->alloc);
   anv_bo_pool_free(&cmd_buffer->device->batch_bo_pool, &bbo->bo);
   anv_free(&cmd_buffer->pool->alloc, bbo);
}

static VkResult
anv_batch_bo_list_clone(const struct list_head *list,
                        struct anv_cmd_buffer *cmd_buffer,
                        struct list_head *new_list)
{
   VkResult result = VK_SUCCESS;

   list_inithead(new_list);

   struct anv_batch_bo *prev_bbo = NULL;
   list_for_each_entry(struct anv_batch_bo, bbo, list, link) {
      struct anv_batch_bo *new_bbo = NULL;
      result = anv_batch_bo_clone(cmd_buffer, bbo, &new_bbo);
      if (result != VK_SUCCESS)
         break;
      list_addtail(&new_bbo->link, new_list);

      if (prev_bbo) {
         /* As we clone this list of batch_bo's, they chain one to the
          * other using MI_BATCH_BUFFER_START commands.  We need to fix up
          * those relocations as we go.  Fortunately, this is pretty easy
          * as it will always be the last relocation in the list.
          */
         uint32_t last_idx = prev_bbo->relocs.num_relocs - 1;
         assert(prev_bbo->relocs.reloc_bos[last_idx] == &bbo->bo);
         prev_bbo->relocs.reloc_bos[last_idx] = &new_bbo->bo;
      }

      prev_bbo = new_bbo;
   }

   if (result != VK_SUCCESS) {
      list_for_each_entry_safe(struct anv_batch_bo, bbo, new_list, link)
         anv_batch_bo_destroy(bbo, cmd_buffer);
   }

   return result;
}

/*-----------------------------------------------------------------------*
 * Functions related to anv_batch_bo
 *-----------------------------------------------------------------------*/

static inline struct anv_batch_bo *
anv_cmd_buffer_current_batch_bo(struct anv_cmd_buffer *cmd_buffer)
{
   return LIST_ENTRY(struct anv_batch_bo, cmd_buffer->batch_bos.prev, link);
}

struct anv_address
anv_cmd_buffer_surface_base_address(struct anv_cmd_buffer *cmd_buffer)
{
   return (struct anv_address) {
      .bo = &cmd_buffer->device->surface_state_block_pool.bo,
      .offset = *(int32_t *)anv_vector_head(&cmd_buffer->bt_blocks),
   };
}

static void
emit_batch_buffer_start(struct anv_cmd_buffer *cmd_buffer,
                        struct anv_bo *bo, uint32_t offset)
{
   /* In gen8+ the address field grew to two dwords to accomodate 48 bit
    * offsets. The high 16 bits are in the last dword, so we can use the gen8
    * version in either case, as long as we set the instruction length in the
    * header accordingly.  This means that we always emit three dwords here
    * and all the padding and adjustment we do in this file works for all
    * gens.
    */

   const uint32_t gen7_length =
      GEN7_MI_BATCH_BUFFER_START_length - GEN7_MI_BATCH_BUFFER_START_length_bias;
   const uint32_t gen8_length =
      GEN8_MI_BATCH_BUFFER_START_length - GEN8_MI_BATCH_BUFFER_START_length_bias;

   anv_batch_emit(&cmd_buffer->batch, GEN8_MI_BATCH_BUFFER_START, bbs) {
      bbs.DWordLength               = cmd_buffer->device->info.gen < 8 ?
                                      gen7_length : gen8_length;
      bbs._2ndLevelBatchBuffer      = _1stlevelbatch;
      bbs.AddressSpaceIndicator     = ASI_PPGTT;
      bbs.BatchBufferStartAddress   = (struct anv_address) { bo, offset };
   }
}

static void
cmd_buffer_chain_to_batch_bo(struct anv_cmd_buffer *cmd_buffer,
                             struct anv_batch_bo *bbo)
{
   struct anv_batch *batch = &cmd_buffer->batch;
   struct anv_batch_bo *current_bbo =
      anv_cmd_buffer_current_batch_bo(cmd_buffer);

   /* We set the end of the batch a little short so we would be sure we
    * have room for the chaining command.  Since we're about to emit the
    * chaining command, let's set it back where it should go.
    */
   batch->end += GEN8_MI_BATCH_BUFFER_START_length * 4;
   assert(batch->end == current_bbo->bo.map + current_bbo->bo.size);

   emit_batch_buffer_start(cmd_buffer, &bbo->bo, 0);

   anv_batch_bo_finish(current_bbo, batch);
}

static VkResult
anv_cmd_buffer_chain_batch(struct anv_batch *batch, void *_data)
{
   struct anv_cmd_buffer *cmd_buffer = _data;
   struct anv_batch_bo *new_bbo;

   VkResult result = anv_batch_bo_create(cmd_buffer, &new_bbo);
   if (result != VK_SUCCESS)
      return result;

   struct anv_batch_bo **seen_bbo = anv_vector_add(&cmd_buffer->seen_bbos);
   if (seen_bbo == NULL) {
      anv_batch_bo_destroy(new_bbo, cmd_buffer);
      return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);
   }
   *seen_bbo = new_bbo;

   cmd_buffer_chain_to_batch_bo(cmd_buffer, new_bbo);

   list_addtail(&new_bbo->link, &cmd_buffer->batch_bos);

   anv_batch_bo_start(new_bbo, batch, GEN8_MI_BATCH_BUFFER_START_length * 4);

   return VK_SUCCESS;
}

static VkResult
anv_cmd_buffer_grow_batch(struct anv_batch *batch, void *_data)
{
   struct anv_cmd_buffer *cmd_buffer = _data;
   struct anv_batch_bo *bbo = anv_cmd_buffer_current_batch_bo(cmd_buffer);

   anv_batch_bo_grow(cmd_buffer, bbo, &cmd_buffer->batch, 4096,
                     GEN8_MI_BATCH_BUFFER_START_length * 4);

   return VK_SUCCESS;
}

struct anv_state
anv_cmd_buffer_alloc_binding_table(struct anv_cmd_buffer *cmd_buffer,
                                   uint32_t entries, uint32_t *state_offset)
{
   struct anv_block_pool *block_pool =
       &cmd_buffer->device->surface_state_block_pool;
   int32_t *bt_block = anv_vector_head(&cmd_buffer->bt_blocks);
   struct anv_state state;

   state.alloc_size = align_u32(entries * 4, 32);

   if (cmd_buffer->bt_next + state.alloc_size > block_pool->block_size)
      return (struct anv_state) { 0 };

   state.offset = cmd_buffer->bt_next;
   state.map = block_pool->map + *bt_block + state.offset;

   cmd_buffer->bt_next += state.alloc_size;

   assert(*bt_block < 0);
   *state_offset = -(*bt_block);

   return state;
}

struct anv_state
anv_cmd_buffer_alloc_surface_state(struct anv_cmd_buffer *cmd_buffer)
{
   return anv_state_stream_alloc(&cmd_buffer->surface_state_stream, 64, 64);
}

struct anv_state
anv_cmd_buffer_alloc_dynamic_state(struct anv_cmd_buffer *cmd_buffer,
                                   uint32_t size, uint32_t alignment)
{
   return anv_state_stream_alloc(&cmd_buffer->dynamic_state_stream,
                                 size, alignment);
}

VkResult
anv_cmd_buffer_new_binding_table_block(struct anv_cmd_buffer *cmd_buffer)
{
   struct anv_block_pool *block_pool =
       &cmd_buffer->device->surface_state_block_pool;

   int32_t *offset = anv_vector_add(&cmd_buffer->bt_blocks);
   if (offset == NULL)
      return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

   *offset = anv_block_pool_alloc_back(block_pool);
   cmd_buffer->bt_next = 0;

   return VK_SUCCESS;
}

VkResult
anv_cmd_buffer_init_batch_bo_chain(struct anv_cmd_buffer *cmd_buffer)
{
   struct anv_batch_bo *batch_bo;
   VkResult result;

   list_inithead(&cmd_buffer->batch_bos);

   result = anv_batch_bo_create(cmd_buffer, &batch_bo);
   if (result != VK_SUCCESS)
      return result;

   list_addtail(&batch_bo->link, &cmd_buffer->batch_bos);

   cmd_buffer->batch.alloc = &cmd_buffer->pool->alloc;
   cmd_buffer->batch.user_data = cmd_buffer;

   if (cmd_buffer->device->can_chain_batches) {
      cmd_buffer->batch.extend_cb = anv_cmd_buffer_chain_batch;
   } else {
      cmd_buffer->batch.extend_cb = anv_cmd_buffer_grow_batch;
   }

   anv_batch_bo_start(batch_bo, &cmd_buffer->batch,
                      GEN8_MI_BATCH_BUFFER_START_length * 4);

   int success = anv_vector_init(&cmd_buffer->seen_bbos,
                                 sizeof(struct anv_bo *),
                                 8 * sizeof(struct anv_bo *));
   if (!success)
      goto fail_batch_bo;

   *(struct anv_batch_bo **)anv_vector_add(&cmd_buffer->seen_bbos) = batch_bo;

   success = anv_vector_init(&cmd_buffer->bt_blocks, sizeof(int32_t),
                             8 * sizeof(int32_t));
   if (!success)
      goto fail_seen_bbos;

   result = anv_reloc_list_init(&cmd_buffer->surface_relocs,
                                &cmd_buffer->pool->alloc);
   if (result != VK_SUCCESS)
      goto fail_bt_blocks;

   anv_cmd_buffer_new_binding_table_block(cmd_buffer);

   cmd_buffer->execbuf2.objects = NULL;
   cmd_buffer->execbuf2.bos = NULL;
   cmd_buffer->execbuf2.array_length = 0;

   return VK_SUCCESS;

 fail_bt_blocks:
   anv_vector_finish(&cmd_buffer->bt_blocks);
 fail_seen_bbos:
   anv_vector_finish(&cmd_buffer->seen_bbos);
 fail_batch_bo:
   anv_batch_bo_destroy(batch_bo, cmd_buffer);

   return result;
}

void
anv_cmd_buffer_fini_batch_bo_chain(struct anv_cmd_buffer *cmd_buffer)
{
   int32_t *bt_block;
   anv_vector_foreach(bt_block, &cmd_buffer->bt_blocks) {
      anv_block_pool_free(&cmd_buffer->device->surface_state_block_pool,
                          *bt_block);
   }
   anv_vector_finish(&cmd_buffer->bt_blocks);

   anv_reloc_list_finish(&cmd_buffer->surface_relocs, &cmd_buffer->pool->alloc);

   anv_vector_finish(&cmd_buffer->seen_bbos);

   /* Destroy all of the batch buffers */
   list_for_each_entry_safe(struct anv_batch_bo, bbo,
                            &cmd_buffer->batch_bos, link) {
      anv_batch_bo_destroy(bbo, cmd_buffer);
   }

   anv_free(&cmd_buffer->pool->alloc, cmd_buffer->execbuf2.objects);
   anv_free(&cmd_buffer->pool->alloc, cmd_buffer->execbuf2.bos);
}

void
anv_cmd_buffer_reset_batch_bo_chain(struct anv_cmd_buffer *cmd_buffer)
{
   /* Delete all but the first batch bo */
   assert(!list_empty(&cmd_buffer->batch_bos));
   while (cmd_buffer->batch_bos.next != cmd_buffer->batch_bos.prev) {
      struct anv_batch_bo *bbo = anv_cmd_buffer_current_batch_bo(cmd_buffer);
      list_del(&bbo->link);
      anv_batch_bo_destroy(bbo, cmd_buffer);
   }
   assert(!list_empty(&cmd_buffer->batch_bos));

   anv_batch_bo_start(anv_cmd_buffer_current_batch_bo(cmd_buffer),
                      &cmd_buffer->batch,
                      GEN8_MI_BATCH_BUFFER_START_length * 4);

   while (anv_vector_length(&cmd_buffer->bt_blocks) > 1) {
      int32_t *bt_block = anv_vector_remove(&cmd_buffer->bt_blocks);
      anv_block_pool_free(&cmd_buffer->device->surface_state_block_pool,
                          *bt_block);
   }
   assert(anv_vector_length(&cmd_buffer->bt_blocks) == 1);
   cmd_buffer->bt_next = 0;

   cmd_buffer->surface_relocs.num_relocs = 0;

   /* Reset the list of seen buffers */
   cmd_buffer->seen_bbos.head = 0;
   cmd_buffer->seen_bbos.tail = 0;

   *(struct anv_batch_bo **)anv_vector_add(&cmd_buffer->seen_bbos) =
      anv_cmd_buffer_current_batch_bo(cmd_buffer);
}

void
anv_cmd_buffer_end_batch_buffer(struct anv_cmd_buffer *cmd_buffer)
{
   struct anv_batch_bo *batch_bo = anv_cmd_buffer_current_batch_bo(cmd_buffer);

   if (cmd_buffer->level == VK_COMMAND_BUFFER_LEVEL_PRIMARY) {
      /* When we start a batch buffer, we subtract a certain amount of
       * padding from the end to ensure that we always have room to emit a
       * BATCH_BUFFER_START to chain to the next BO.  We need to remove
       * that padding before we end the batch; otherwise, we may end up
       * with our BATCH_BUFFER_END in another BO.
       */
      cmd_buffer->batch.end += GEN8_MI_BATCH_BUFFER_START_length * 4;
      assert(cmd_buffer->batch.end == batch_bo->bo.map + batch_bo->bo.size);

      anv_batch_emit(&cmd_buffer->batch, GEN7_MI_BATCH_BUFFER_END, bbe);

      /* Round batch up to an even number of dwords. */
      if ((cmd_buffer->batch.next - cmd_buffer->batch.start) & 4)
         anv_batch_emit(&cmd_buffer->batch, GEN7_MI_NOOP, noop);

      cmd_buffer->exec_mode = ANV_CMD_BUFFER_EXEC_MODE_PRIMARY;
   }

   anv_batch_bo_finish(batch_bo, &cmd_buffer->batch);

   if (cmd_buffer->level == VK_COMMAND_BUFFER_LEVEL_SECONDARY) {
      /* If this is a secondary command buffer, we need to determine the
       * mode in which it will be executed with vkExecuteCommands.  We
       * determine this statically here so that this stays in sync with the
       * actual ExecuteCommands implementation.
       */
      if (!cmd_buffer->device->can_chain_batches) {
         cmd_buffer->exec_mode = ANV_CMD_BUFFER_EXEC_MODE_GROW_AND_EMIT;
      } else if ((cmd_buffer->batch_bos.next == cmd_buffer->batch_bos.prev) &&
          (batch_bo->length < ANV_CMD_BUFFER_BATCH_SIZE / 2)) {
         /* If the secondary has exactly one batch buffer in its list *and*
          * that batch buffer is less than half of the maximum size, we're
          * probably better of simply copying it into our batch.
          */
         cmd_buffer->exec_mode = ANV_CMD_BUFFER_EXEC_MODE_EMIT;
      } else if (!(cmd_buffer->usage_flags &
                   VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT)) {
         cmd_buffer->exec_mode = ANV_CMD_BUFFER_EXEC_MODE_CHAIN;

         /* When we chain, we need to add an MI_BATCH_BUFFER_START command
          * with its relocation.  In order to handle this we'll increment here
          * so we can unconditionally decrement right before adding the
          * MI_BATCH_BUFFER_START command.
          */
         batch_bo->relocs.num_relocs++;
         cmd_buffer->batch.next += GEN8_MI_BATCH_BUFFER_START_length * 4;
      } else {
         cmd_buffer->exec_mode = ANV_CMD_BUFFER_EXEC_MODE_COPY_AND_CHAIN;
      }
   }
}

static inline VkResult
anv_cmd_buffer_add_seen_bbos(struct anv_cmd_buffer *cmd_buffer,
                             struct list_head *list)
{
   list_for_each_entry(struct anv_batch_bo, bbo, list, link) {
      struct anv_batch_bo **bbo_ptr = anv_vector_add(&cmd_buffer->seen_bbos);
      if (bbo_ptr == NULL)
         return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

      *bbo_ptr = bbo;
   }

   return VK_SUCCESS;
}

void
anv_cmd_buffer_add_secondary(struct anv_cmd_buffer *primary,
                             struct anv_cmd_buffer *secondary)
{
   switch (secondary->exec_mode) {
   case ANV_CMD_BUFFER_EXEC_MODE_EMIT:
      anv_batch_emit_batch(&primary->batch, &secondary->batch);
      anv_cmd_buffer_emit_state_base_address(primary);
      break;
   case ANV_CMD_BUFFER_EXEC_MODE_GROW_AND_EMIT: {
      struct anv_batch_bo *bbo = anv_cmd_buffer_current_batch_bo(primary);
      unsigned length = secondary->batch.end - secondary->batch.start;
      anv_batch_bo_grow(primary, bbo, &primary->batch, length,
                        GEN8_MI_BATCH_BUFFER_START_length * 4);
      anv_batch_emit_batch(&primary->batch, &secondary->batch);
      anv_cmd_buffer_emit_state_base_address(primary);
      break;
   }
   case ANV_CMD_BUFFER_EXEC_MODE_CHAIN: {
      struct anv_batch_bo *first_bbo =
         list_first_entry(&secondary->batch_bos, struct anv_batch_bo, link);
      struct anv_batch_bo *last_bbo =
         list_last_entry(&secondary->batch_bos, struct anv_batch_bo, link);

      emit_batch_buffer_start(primary, &first_bbo->bo, 0);

      struct anv_batch_bo *this_bbo = anv_cmd_buffer_current_batch_bo(primary);
      assert(primary->batch.start == this_bbo->bo.map);
      uint32_t offset = primary->batch.next - primary->batch.start;
      const uint32_t inst_size = GEN8_MI_BATCH_BUFFER_START_length * 4;

      /* Roll back the previous MI_BATCH_BUFFER_START and its relocation so we
       * can emit a new command and relocation for the current splice.  In
       * order to handle the initial-use case, we incremented next and
       * num_relocs in end_batch_buffer() so we can alyways just subtract
       * here.
       */
      last_bbo->relocs.num_relocs--;
      secondary->batch.next -= inst_size;
      emit_batch_buffer_start(secondary, &this_bbo->bo, offset);
      anv_cmd_buffer_add_seen_bbos(primary, &secondary->batch_bos);

      /* After patching up the secondary buffer, we need to clflush the
       * modified instruction in case we're on a !llc platform. We use a
       * little loop to handle the case where the instruction crosses a cache
       * line boundary.
       */
      if (!primary->device->info.has_llc) {
         void *inst = secondary->batch.next - inst_size;
         void *p = (void *) (((uintptr_t) inst) & ~CACHELINE_MASK);
         __builtin_ia32_mfence();
         while (p < secondary->batch.next) {
            __builtin_ia32_clflush(p);
            p += CACHELINE_SIZE;
         }
      }

      anv_cmd_buffer_emit_state_base_address(primary);
      break;
   }
   case ANV_CMD_BUFFER_EXEC_MODE_COPY_AND_CHAIN: {
      struct list_head copy_list;
      VkResult result = anv_batch_bo_list_clone(&secondary->batch_bos,
                                                secondary,
                                                &copy_list);
      if (result != VK_SUCCESS)
         return; /* FIXME */

      anv_cmd_buffer_add_seen_bbos(primary, &copy_list);

      struct anv_batch_bo *first_bbo =
         list_first_entry(&copy_list, struct anv_batch_bo, link);
      struct anv_batch_bo *last_bbo =
         list_last_entry(&copy_list, struct anv_batch_bo, link);

      cmd_buffer_chain_to_batch_bo(primary, first_bbo);

      list_splicetail(&copy_list, &primary->batch_bos);

      anv_batch_bo_continue(last_bbo, &primary->batch,
                            GEN8_MI_BATCH_BUFFER_START_length * 4);

      anv_cmd_buffer_emit_state_base_address(primary);
      break;
   }
   default:
      assert(!"Invalid execution mode");
   }

   anv_reloc_list_append(&primary->surface_relocs, &primary->pool->alloc,
                         &secondary->surface_relocs, 0);
}

static VkResult
anv_cmd_buffer_add_bo(struct anv_cmd_buffer *cmd_buffer,
                      struct anv_bo *bo,
                      struct anv_reloc_list *relocs)
{
   struct drm_i915_gem_exec_object2 *obj = NULL;

   if (bo->index < cmd_buffer->execbuf2.bo_count &&
       cmd_buffer->execbuf2.bos[bo->index] == bo)
      obj = &cmd_buffer->execbuf2.objects[bo->index];

   if (obj == NULL) {
      /* We've never seen this one before.  Add it to the list and assign
       * an id that we can use later.
       */
      if (cmd_buffer->execbuf2.bo_count >= cmd_buffer->execbuf2.array_length) {
         uint32_t new_len = cmd_buffer->execbuf2.objects ?
                            cmd_buffer->execbuf2.array_length * 2 : 64;

         struct drm_i915_gem_exec_object2 *new_objects =
            anv_alloc(&cmd_buffer->pool->alloc, new_len * sizeof(*new_objects),
                      8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
         if (new_objects == NULL)
            return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

         struct anv_bo **new_bos =
            anv_alloc(&cmd_buffer->pool->alloc, new_len * sizeof(*new_bos),
                      8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
         if (new_bos == NULL) {
            anv_free(&cmd_buffer->pool->alloc, new_objects);
            return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);
         }

         if (cmd_buffer->execbuf2.objects) {
            memcpy(new_objects, cmd_buffer->execbuf2.objects,
                   cmd_buffer->execbuf2.bo_count * sizeof(*new_objects));
            memcpy(new_bos, cmd_buffer->execbuf2.bos,
                   cmd_buffer->execbuf2.bo_count * sizeof(*new_bos));
         }

         cmd_buffer->execbuf2.objects = new_objects;
         cmd_buffer->execbuf2.bos = new_bos;
         cmd_buffer->execbuf2.array_length = new_len;
      }

      assert(cmd_buffer->execbuf2.bo_count < cmd_buffer->execbuf2.array_length);

      bo->index = cmd_buffer->execbuf2.bo_count++;
      obj = &cmd_buffer->execbuf2.objects[bo->index];
      cmd_buffer->execbuf2.bos[bo->index] = bo;

      obj->handle = bo->gem_handle;
      obj->relocation_count = 0;
      obj->relocs_ptr = 0;
      obj->alignment = 0;
      obj->offset = bo->offset;
      obj->flags = bo->is_winsys_bo ? EXEC_OBJECT_WRITE : 0;
      obj->rsvd1 = 0;
      obj->rsvd2 = 0;
   }

   if (relocs != NULL && obj->relocation_count == 0) {
      /* This is the first time we've ever seen a list of relocations for
       * this BO.  Go ahead and set the relocations and then walk the list
       * of relocations and add them all.
       */
      obj->relocation_count = relocs->num_relocs;
      obj->relocs_ptr = (uintptr_t) relocs->relocs;

      for (size_t i = 0; i < relocs->num_relocs; i++) {
         /* A quick sanity check on relocations */
         assert(relocs->relocs[i].offset < bo->size);
         anv_cmd_buffer_add_bo(cmd_buffer, relocs->reloc_bos[i], NULL);
      }
   }

   return VK_SUCCESS;
}

static void
anv_cmd_buffer_process_relocs(struct anv_cmd_buffer *cmd_buffer,
                              struct anv_reloc_list *list)
{
   struct anv_bo *bo;

   /* If the kernel supports I915_EXEC_NO_RELOC, it will compare offset in
    * struct drm_i915_gem_exec_object2 against the bos current offset and if
    * all bos haven't moved it will skip relocation processing alltogether.
    * If I915_EXEC_NO_RELOC is not supported, the kernel ignores the incoming
    * value of offset so we can set it either way.  For that to work we need
    * to make sure all relocs use the same presumed offset.
    */

   for (size_t i = 0; i < list->num_relocs; i++) {
      bo = list->reloc_bos[i];
      if (bo->offset != list->relocs[i].presumed_offset)
         cmd_buffer->execbuf2.need_reloc = true;

      list->relocs[i].target_handle = bo->index;
   }
}

static uint64_t
read_reloc(const struct anv_device *device, const void *p)
{
   if (device->info.gen >= 8)
      return *(uint64_t *)p;
   else
      return *(uint32_t *)p;
}

static void
write_reloc(const struct anv_device *device, void *p, uint64_t v)
{
   if (device->info.gen >= 8)
      *(uint64_t *)p = v;
   else
      *(uint32_t *)p = v;
}

static void
adjust_relocations_from_block_pool(struct anv_block_pool *pool,
                                   struct anv_reloc_list *relocs)
{
   for (size_t i = 0; i < relocs->num_relocs; i++) {
      /* In general, we don't know how stale the relocated value is.  It
       * may have been used last time or it may not.  Since we don't want
       * to stomp it while the GPU may be accessing it, we haven't updated
       * it anywhere else in the code.  Instead, we just set the presumed
       * offset to what it is now based on the delta and the data in the
       * block pool.  Then the kernel will update it for us if needed.
       */
      assert(relocs->relocs[i].offset < pool->state.end);
      const void *p = pool->map + relocs->relocs[i].offset;

      /* We're reading back the relocated value from potentially incoherent
       * memory here. However, any change to the value will be from the kernel
       * writing out relocations, which will keep the CPU cache up to date.
       */
      relocs->relocs[i].presumed_offset =
         read_reloc(pool->device, p) - relocs->relocs[i].delta;

      /* All of the relocations from this block pool to other BO's should
       * have been emitted relative to the surface block pool center.  We
       * need to add the center offset to make them relative to the
       * beginning of the actual GEM bo.
       */
      relocs->relocs[i].offset += pool->center_bo_offset;
   }
}

static void
adjust_relocations_to_block_pool(struct anv_block_pool *pool,
                                 struct anv_bo *from_bo,
                                 struct anv_reloc_list *relocs,
                                 uint32_t *last_pool_center_bo_offset)
{
   assert(*last_pool_center_bo_offset <= pool->center_bo_offset);
   uint32_t delta = pool->center_bo_offset - *last_pool_center_bo_offset;

   /* When we initially emit relocations into a block pool, we don't
    * actually know what the final center_bo_offset will be so we just emit
    * it as if center_bo_offset == 0.  Now that we know what the center
    * offset is, we need to walk the list of relocations and adjust any
    * relocations that point to the pool bo with the correct offset.
    */
   for (size_t i = 0; i < relocs->num_relocs; i++) {
      if (relocs->reloc_bos[i] == &pool->bo) {
         /* Adjust the delta value in the relocation to correctly
          * correspond to the new delta.  Initially, this value may have
          * been negative (if treated as unsigned), but we trust in
          * uint32_t roll-over to fix that for us at this point.
          */
         relocs->relocs[i].delta += delta;

         /* Since the delta has changed, we need to update the actual
          * relocated value with the new presumed value.  This function
          * should only be called on batch buffers, so we know it isn't in
          * use by the GPU at the moment.
          */
         assert(relocs->relocs[i].offset < from_bo->size);
         write_reloc(pool->device, from_bo->map + relocs->relocs[i].offset,
                     relocs->relocs[i].presumed_offset +
                     relocs->relocs[i].delta);
      }
   }

   *last_pool_center_bo_offset = pool->center_bo_offset;
}

void
anv_cmd_buffer_prepare_execbuf(struct anv_cmd_buffer *cmd_buffer)
{
   struct anv_batch *batch = &cmd_buffer->batch;
   struct anv_block_pool *ss_pool =
      &cmd_buffer->device->surface_state_block_pool;

   cmd_buffer->execbuf2.bo_count = 0;
   cmd_buffer->execbuf2.need_reloc = false;

   adjust_relocations_from_block_pool(ss_pool, &cmd_buffer->surface_relocs);
   anv_cmd_buffer_add_bo(cmd_buffer, &ss_pool->bo, &cmd_buffer->surface_relocs);

   /* First, we walk over all of the bos we've seen and add them and their
    * relocations to the validate list.
    */
   struct anv_batch_bo **bbo;
   anv_vector_foreach(bbo, &cmd_buffer->seen_bbos) {
      adjust_relocations_to_block_pool(ss_pool, &(*bbo)->bo, &(*bbo)->relocs,
                                       &(*bbo)->last_ss_pool_bo_offset);

      anv_cmd_buffer_add_bo(cmd_buffer, &(*bbo)->bo, &(*bbo)->relocs);
   }

   struct anv_batch_bo *first_batch_bo =
      list_first_entry(&cmd_buffer->batch_bos, struct anv_batch_bo, link);

   /* The kernel requires that the last entry in the validation list be the
    * batch buffer to execute.  We can simply swap the element
    * corresponding to the first batch_bo in the chain with the last
    * element in the list.
    */
   if (first_batch_bo->bo.index != cmd_buffer->execbuf2.bo_count - 1) {
      uint32_t idx = first_batch_bo->bo.index;
      uint32_t last_idx = cmd_buffer->execbuf2.bo_count - 1;

      struct drm_i915_gem_exec_object2 tmp_obj =
         cmd_buffer->execbuf2.objects[idx];
      assert(cmd_buffer->execbuf2.bos[idx] == &first_batch_bo->bo);

      cmd_buffer->execbuf2.objects[idx] = cmd_buffer->execbuf2.objects[last_idx];
      cmd_buffer->execbuf2.bos[idx] = cmd_buffer->execbuf2.bos[last_idx];
      cmd_buffer->execbuf2.bos[idx]->index = idx;

      cmd_buffer->execbuf2.objects[last_idx] = tmp_obj;
      cmd_buffer->execbuf2.bos[last_idx] = &first_batch_bo->bo;
      first_batch_bo->bo.index = last_idx;
   }

   /* Now we go through and fixup all of the relocation lists to point to
    * the correct indices in the object array.  We have to do this after we
    * reorder the list above as some of the indices may have changed.
    */
   anv_vector_foreach(bbo, &cmd_buffer->seen_bbos)
      anv_cmd_buffer_process_relocs(cmd_buffer, &(*bbo)->relocs);

   anv_cmd_buffer_process_relocs(cmd_buffer, &cmd_buffer->surface_relocs);

   if (!cmd_buffer->device->info.has_llc) {
      __builtin_ia32_mfence();
      anv_vector_foreach(bbo, &cmd_buffer->seen_bbos) {
         for (uint32_t i = 0; i < (*bbo)->length; i += CACHELINE_SIZE)
            __builtin_ia32_clflush((*bbo)->bo.map + i);
      }
   }

   cmd_buffer->execbuf2.execbuf = (struct drm_i915_gem_execbuffer2) {
      .buffers_ptr = (uintptr_t) cmd_buffer->execbuf2.objects,
      .buffer_count = cmd_buffer->execbuf2.bo_count,
      .batch_start_offset = 0,
      .batch_len = batch->next - batch->start,
      .cliprects_ptr = 0,
      .num_cliprects = 0,
      .DR1 = 0,
      .DR4 = 0,
      .flags = I915_EXEC_HANDLE_LUT | I915_EXEC_RENDER |
               I915_EXEC_CONSTANTS_REL_GENERAL,
      .rsvd1 = cmd_buffer->device->context_id,
      .rsvd2 = 0,
   };

   if (!cmd_buffer->execbuf2.need_reloc)
      cmd_buffer->execbuf2.execbuf.flags |= I915_EXEC_NO_RELOC;
}
