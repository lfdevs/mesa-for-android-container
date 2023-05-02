/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
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

#include <amdgpu.h>
#include <assert.h>
#include <libsync.h>
#include <pthread.h>
#include <stdlib.h>
#include "drm-uapi/amdgpu_drm.h"

#include "util/os_time.h"
#include "util/u_memory.h"
#include "ac_debug.h"
#include "radv_amdgpu_bo.h"
#include "radv_amdgpu_cs.h"
#include "radv_amdgpu_winsys.h"
#include "radv_debug.h"
#include "radv_radeon_winsys.h"
#include "sid.h"
#include "vk_alloc.h"
#include "vk_drm_syncobj.h"
#include "vk_sync.h"
#include "vk_sync_dummy.h"

/* Maximum allowed total number of submitted IBs. */
#define RADV_MAX_IBS_PER_SUBMIT 192

enum { VIRTUAL_BUFFER_HASH_TABLE_SIZE = 1024 };

struct radv_amdgpu_ib {
   struct radeon_winsys_bo *bo;
   unsigned cdw;
};

struct radv_amdgpu_cs_ib_info {
   int64_t flags;
   uint64_t ib_mc_address;
   uint32_t size;
   enum amd_ip_type ip_type;
};

struct radv_amdgpu_cs {
   struct radeon_cmdbuf base;
   struct radv_amdgpu_winsys *ws;

   struct radv_amdgpu_cs_ib_info ib;

   struct radeon_winsys_bo *ib_buffer;
   uint8_t *ib_mapped;
   unsigned max_num_buffers;
   unsigned num_buffers;
   struct drm_amdgpu_bo_list_entry *handles;

   struct radv_amdgpu_ib *old_ib_buffers;
   unsigned num_old_ib_buffers;
   unsigned max_num_old_ib_buffers;
   unsigned *ib_size_ptr;
   VkResult status;
   struct radv_amdgpu_cs *chained_to;
   bool use_ib;
   bool is_secondary;

   int buffer_hash_table[1024];
   unsigned hw_ip;

   unsigned num_virtual_buffers;
   unsigned max_num_virtual_buffers;
   struct radeon_winsys_bo **virtual_buffers;
   int *virtual_buffer_hash_table;
};

struct radv_winsys_sem_counts {
   uint32_t syncobj_count;
   uint32_t timeline_syncobj_count;
   uint32_t *syncobj;
   uint64_t *points;
};

struct radv_winsys_sem_info {
   bool cs_emit_signal;
   bool cs_emit_wait;
   struct radv_winsys_sem_counts wait;
   struct radv_winsys_sem_counts signal;
};

static uint32_t radv_amdgpu_ctx_queue_syncobj(struct radv_amdgpu_ctx *ctx, unsigned ip,
                                              unsigned ring);

static inline struct radv_amdgpu_cs *
radv_amdgpu_cs(struct radeon_cmdbuf *base)
{
   return (struct radv_amdgpu_cs *)base;
}

static bool
ring_can_use_ib_bos(const struct radv_amdgpu_winsys *ws,
                    enum amd_ip_type ip_type)
{
   return ws->use_ib_bos && (ip_type == AMD_IP_GFX || ip_type == AMD_IP_COMPUTE);
}

struct radv_amdgpu_cs_request {
   /** Specify HW IP block type to which to send the IB. */
   unsigned ip_type;

   /** IP instance index if there are several IPs of the same type. */
   unsigned ip_instance;

   /**
    * Specify ring index of the IP. We could have several rings
    * in the same IP. E.g. 0 for SDMA0 and 1 for SDMA1.
    */
   uint32_t ring;

   /**
    * BO list handles used by this request.
    */
   struct drm_amdgpu_bo_list_entry *handles;
   uint32_t num_handles;

   /** Number of IBs to submit in the field ibs. */
   uint32_t number_of_ibs;

   /**
    * IBs to submit. Those IBs will be submitted together as single entity
    */
   struct radv_amdgpu_cs_ib_info *ibs;

   /**
    * The returned sequence number for the command submission
    */
   uint64_t seq_no;
};

static VkResult radv_amdgpu_cs_submit(struct radv_amdgpu_ctx *ctx,
                                      struct radv_amdgpu_cs_request *request,
                                      struct radv_winsys_sem_info *sem_info);

static void
radv_amdgpu_request_to_fence(struct radv_amdgpu_ctx *ctx, struct radv_amdgpu_fence *fence,
                             struct radv_amdgpu_cs_request *req)
{
   fence->fence.context = ctx->ctx;
   fence->fence.ip_type = req->ip_type;
   fence->fence.ip_instance = req->ip_instance;
   fence->fence.ring = req->ring;
   fence->fence.fence = req->seq_no;
}

static struct radv_amdgpu_cs_ib_info
radv_amdgpu_cs_ib_to_info(struct radv_amdgpu_cs *cs, struct radv_amdgpu_ib ib)
{
   struct radv_amdgpu_cs_ib_info info = {
      .flags = 0,
      .ip_type = cs->hw_ip,
      .ib_mc_address = radv_amdgpu_winsys_bo(ib.bo)->base.va,
      .size = ib.cdw,
   };
   return info;
}

static void
radv_amdgpu_cs_destroy(struct radeon_cmdbuf *rcs)
{
   struct radv_amdgpu_cs *cs = radv_amdgpu_cs(rcs);

   if (cs->ib_buffer)
      cs->ws->base.buffer_destroy(&cs->ws->base, cs->ib_buffer);

   for (unsigned i = 0; i < cs->num_old_ib_buffers; ++i)
      cs->ws->base.buffer_destroy(&cs->ws->base, cs->old_ib_buffers[i].bo);

   free(cs->old_ib_buffers);
   free(cs->virtual_buffers);
   free(cs->virtual_buffer_hash_table);
   free(cs->handles);
   free(cs);
}

static void
radv_amdgpu_init_cs(struct radv_amdgpu_cs *cs, enum amd_ip_type ip_type)
{
   for (int i = 0; i < ARRAY_SIZE(cs->buffer_hash_table); ++i)
      cs->buffer_hash_table[i] = -1;

   cs->hw_ip = ip_type;
}

static enum radeon_bo_domain
radv_amdgpu_cs_domain(const struct radeon_winsys *_ws)
{
   const struct radv_amdgpu_winsys *ws = (const struct radv_amdgpu_winsys *)_ws;

   bool enough_vram = ws->info.all_vram_visible ||
                      p_atomic_read_relaxed(&ws->allocated_vram_vis) * 2 <= (uint64_t)ws->info.vram_vis_size_kb * 1024;

   /* Bandwidth should be equivalent to at least PCIe 3.0 x8.
    * If there is no PCIe info, assume there is enough bandwidth.
    */
   bool enough_bandwidth = !ws->info.has_pcie_bandwidth_info || ws->info.pcie_bandwidth_mbps >= 8 * 0.985 * 1024;

   bool use_sam =
      (enough_vram && enough_bandwidth && ws->info.has_dedicated_vram && !(ws->perftest & RADV_PERFTEST_NO_SAM)) ||
      (ws->perftest & RADV_PERFTEST_SAM);
   return use_sam ? RADEON_DOMAIN_VRAM : RADEON_DOMAIN_GTT;
}

static VkResult
radv_amdgpu_cs_bo_create(struct radv_amdgpu_cs *cs, uint32_t ib_size)
{
   struct radeon_winsys *ws = &cs->ws->base;

   /* Avoid memcpy from VRAM when a secondary cmdbuf can't always rely on IB2. */
   const bool can_always_use_ib2 = cs->ws->info.gfx_level >= GFX8 && cs->hw_ip == AMD_IP_GFX;
   const bool avoid_vram = cs->is_secondary && !can_always_use_ib2;
   const enum radeon_bo_domain domain = avoid_vram ? RADEON_DOMAIN_GTT : radv_amdgpu_cs_domain(ws);
   const enum radeon_bo_flag gtt_wc_flag = avoid_vram ? 0 : RADEON_FLAG_GTT_WC;
   const enum radeon_bo_flag flags = RADEON_FLAG_CPU_ACCESS | RADEON_FLAG_NO_INTERPROCESS_SHARING |
                                     RADEON_FLAG_READ_ONLY | gtt_wc_flag;

   return ws->buffer_create(ws, ib_size, cs->ws->info.ib_alignment, domain, flags,
                            RADV_BO_PRIORITY_CS, 0, &cs->ib_buffer);
}

static struct radeon_cmdbuf *
radv_amdgpu_cs_create(struct radeon_winsys *ws, enum amd_ip_type ip_type, bool is_secondary)
{
   struct radv_amdgpu_cs *cs;
   uint32_t ib_pad_dw_mask = MAX2(3, radv_amdgpu_winsys(ws)->info.ib_pad_dw_mask[ip_type]);
   uint32_t ib_size = align(20 * 1024 * 4, ib_pad_dw_mask + 1);
   cs = calloc(1, sizeof(struct radv_amdgpu_cs));
   if (!cs)
      return NULL;

   cs->is_secondary = is_secondary;
   cs->ws = radv_amdgpu_winsys(ws);
   radv_amdgpu_init_cs(cs, ip_type);

   cs->use_ib = ring_can_use_ib_bos(cs->ws, ip_type);

   VkResult result = radv_amdgpu_cs_bo_create(cs, ib_size);
   if (result != VK_SUCCESS) {
      free(cs);
      return NULL;
   }

   cs->ib_mapped = ws->buffer_map(cs->ib_buffer);
   if (!cs->ib_mapped) {
      ws->buffer_destroy(ws, cs->ib_buffer);
      free(cs);
      return NULL;
   }

   cs->ib.ib_mc_address = radv_amdgpu_winsys_bo(cs->ib_buffer)->base.va;
   cs->base.buf = (uint32_t *)cs->ib_mapped;
   cs->base.max_dw = ib_size / 4 - 4;
   cs->ib.size = 0;
   cs->ib.ip_type = ip_type;

   if (cs->use_ib)
      cs->ib_size_ptr = &cs->ib.size;

   ws->cs_add_buffer(&cs->base, cs->ib_buffer);

   return &cs->base;
}

static uint32_t get_nop_packet(struct radv_amdgpu_cs *cs)
{
   switch(cs->hw_ip) {
   case AMDGPU_HW_IP_GFX:
   case AMDGPU_HW_IP_COMPUTE:
      return cs->ws->info.gfx_ib_pad_with_type2 ?  PKT2_NOP_PAD : PKT3_NOP_PAD;
   case AMDGPU_HW_IP_DMA:
      return cs->ws->info.gfx_level <= GFX6 ? 0xF0000000 : SDMA_NOP_PAD;
   case AMDGPU_HW_IP_UVD:
   case AMDGPU_HW_IP_UVD_ENC:
      return PKT2_NOP_PAD;
   case AMDGPU_HW_IP_VCN_DEC:
      return 0x81FF;
   default:
      unreachable("Unknown IP type");
   }
}

static void
radv_amdgpu_cs_add_old_ib_buffer(struct radv_amdgpu_cs *cs)
{
   if (cs->num_old_ib_buffers == cs->max_num_old_ib_buffers) {
      unsigned max_num_old_ib_buffers = MAX2(1, cs->max_num_old_ib_buffers * 2);
      struct radv_amdgpu_ib *old_ib_buffers =
         realloc(cs->old_ib_buffers, max_num_old_ib_buffers * sizeof(*old_ib_buffers));
      if (!old_ib_buffers) {
         cs->status = VK_ERROR_OUT_OF_HOST_MEMORY;
         return;
      }
      cs->max_num_old_ib_buffers = max_num_old_ib_buffers;
      cs->old_ib_buffers = old_ib_buffers;
   }

   cs->old_ib_buffers[cs->num_old_ib_buffers].bo = cs->ib_buffer;
   cs->old_ib_buffers[cs->num_old_ib_buffers++].cdw = cs->base.cdw;
}

static void
radv_amdgpu_cs_grow(struct radeon_cmdbuf *_cs, size_t min_size)
{
   struct radv_amdgpu_cs *cs = radv_amdgpu_cs(_cs);

   if (cs->status != VK_SUCCESS) {
      cs->base.cdw = 0;
      return;
   }

   enum amd_ip_type ip_type = cs->hw_ip;
   uint32_t ib_pad_dw_mask = MAX2(3, cs->ws->info.ib_pad_dw_mask[ip_type]);
   uint32_t nop_packet = get_nop_packet(cs);
   while (!cs->base.cdw || (cs->base.cdw & ib_pad_dw_mask) != ib_pad_dw_mask - 3)
      radeon_emit(&cs->base, nop_packet);

   if (cs->use_ib)
      *cs->ib_size_ptr |= cs->base.cdw + 4;

   radv_amdgpu_cs_add_old_ib_buffer(cs);
   if (cs->status != VK_SUCCESS)
      return;

   uint64_t ib_size = MAX2(min_size * 4 + 16, cs->base.max_dw * 4 * 2);

   /* max that fits in the chain size field. */
   ib_size = align(MIN2(ib_size, 0xfffff), ib_pad_dw_mask + 1);

   VkResult result = radv_amdgpu_cs_bo_create(cs, ib_size);

   if (result != VK_SUCCESS) {
      cs->base.cdw = 0;
      cs->status = VK_ERROR_OUT_OF_DEVICE_MEMORY;
      cs->ib_buffer = cs->old_ib_buffers[--cs->num_old_ib_buffers].bo;
   }

   cs->ib_mapped = cs->ws->base.buffer_map(cs->ib_buffer);
   if (!cs->ib_mapped) {
      cs->ws->base.buffer_destroy(&cs->ws->base, cs->ib_buffer);
      cs->base.cdw = 0;

      /* VK_ERROR_MEMORY_MAP_FAILED is not valid for vkEndCommandBuffer. */
      cs->status = VK_ERROR_OUT_OF_DEVICE_MEMORY;
      cs->ib_buffer = cs->old_ib_buffers[--cs->num_old_ib_buffers].bo;
   }

   cs->ws->base.cs_add_buffer(&cs->base, cs->ib_buffer);

   if (cs->use_ib) {
      radeon_emit(&cs->base, PKT3(PKT3_INDIRECT_BUFFER_CIK, 2, 0));
      radeon_emit(&cs->base, radv_amdgpu_winsys_bo(cs->ib_buffer)->base.va);
      radeon_emit(&cs->base, radv_amdgpu_winsys_bo(cs->ib_buffer)->base.va >> 32);
      radeon_emit(&cs->base, S_3F2_CHAIN(1) | S_3F2_VALID(1));

      cs->ib_size_ptr = cs->base.buf + cs->base.cdw - 1;
   } else {
      /* Pad the CS with NOP packets. */
      while (!cs->base.cdw || (cs->base.cdw & ib_pad_dw_mask))
         radeon_emit(&cs->base, nop_packet);
   }

   cs->base.buf = (uint32_t *)cs->ib_mapped;
   cs->base.cdw = 0;
   cs->base.max_dw = ib_size / 4 - 4;
}

static VkResult
radv_amdgpu_cs_finalize(struct radeon_cmdbuf *_cs)
{
   struct radv_amdgpu_cs *cs = radv_amdgpu_cs(_cs);
   enum amd_ip_type ip_type = cs->hw_ip;

   uint32_t ib_pad_dw_mask = MAX2(3, cs->ws->info.ib_pad_dw_mask[ip_type]);
   uint32_t nop_packet = get_nop_packet(cs);

   if (cs->use_ib) {
      /* Ensure that with the 4 dword reservation we subtract from max_dw we always
       * have 4 nops at the end for chaining.
       */
      while (!cs->base.cdw || (cs->base.cdw & ib_pad_dw_mask) != ib_pad_dw_mask - 3)
         radeon_emit(&cs->base, nop_packet);

      radeon_emit(&cs->base, nop_packet);
      radeon_emit(&cs->base, nop_packet);
      radeon_emit(&cs->base, nop_packet);
      radeon_emit(&cs->base, nop_packet);

      *cs->ib_size_ptr |= cs->base.cdw;
   } else {
      /* Pad the CS with NOP packets. */
      while (!cs->base.cdw || (cs->base.cdw & ib_pad_dw_mask))
         radeon_emit(&cs->base, nop_packet);

      /* Append the current (last) IB to the array of old IB buffers. */
      radv_amdgpu_cs_add_old_ib_buffer(cs);

      /* The ib field holds the first IB, set its size correctly. */
      cs->ib.size = cs->old_ib_buffers[0].cdw;

      /* Prevent freeing this BO twice. */
      cs->ib_buffer = NULL;
   }

   cs->chained_to = NULL;

   assert(cs->base.cdw <= cs->base.max_dw + 4);

   return cs->status;
}

static void
radv_amdgpu_cs_reset(struct radeon_cmdbuf *_cs)
{
   struct radv_amdgpu_cs *cs = radv_amdgpu_cs(_cs);
   cs->base.cdw = 0;
   cs->status = VK_SUCCESS;

   for (unsigned i = 0; i < cs->num_buffers; ++i) {
      unsigned hash = cs->handles[i].bo_handle & (ARRAY_SIZE(cs->buffer_hash_table) - 1);
      cs->buffer_hash_table[hash] = -1;
   }

   for (unsigned i = 0; i < cs->num_virtual_buffers; ++i) {
      unsigned hash =
         ((uintptr_t)cs->virtual_buffers[i] >> 6) & (VIRTUAL_BUFFER_HASH_TABLE_SIZE - 1);
      cs->virtual_buffer_hash_table[hash] = -1;
   }

   cs->num_buffers = 0;
   cs->num_virtual_buffers = 0;

   /* When the CS is finalized and IBs are not allowed, use last old IB. */
   assert(cs->ib_buffer || cs->num_old_ib_buffers);
   if (!cs->ib_buffer)
      cs->ib_buffer = cs->old_ib_buffers[--cs->num_old_ib_buffers].bo;

   cs->ws->base.cs_add_buffer(&cs->base, cs->ib_buffer);

   for (unsigned i = 0; i < cs->num_old_ib_buffers; ++i)
      cs->ws->base.buffer_destroy(&cs->ws->base, cs->old_ib_buffers[i].bo);

   cs->num_old_ib_buffers = 0;
   cs->ib.ib_mc_address = radv_amdgpu_winsys_bo(cs->ib_buffer)->base.va;

   cs->ib.size = 0;

   if (cs->use_ib)
      cs->ib_size_ptr = &cs->ib.size;
}

static void
radv_amdgpu_cs_unchain(struct radeon_cmdbuf *cs)
{
   struct radv_amdgpu_cs *acs = radv_amdgpu_cs(cs);

   if (!acs->chained_to)
      return;

   assert(cs->cdw <= cs->max_dw + 4);
   assert(get_nop_packet(acs) == PKT3_NOP_PAD); /* Other shouldn't chain. */

   acs->chained_to = NULL;
   cs->buf[cs->cdw - 4] = PKT3_NOP_PAD;
   cs->buf[cs->cdw - 3] = PKT3_NOP_PAD;
   cs->buf[cs->cdw - 2] = PKT3_NOP_PAD;
   cs->buf[cs->cdw - 1] = PKT3_NOP_PAD;
}

static bool
radv_amdgpu_cs_chain(struct radeon_cmdbuf *cs, struct radeon_cmdbuf *next_cs, bool pre_ena)
{
   /* Chains together two CS (command stream) objects by editing
    * the end of the first CS to add a command that jumps to the
    * second CS.
    *
    * After this, it is enough to submit the first CS to the GPU
    * and not necessary to submit the second CS because it is already
    * executed by the first.
    */

   struct radv_amdgpu_cs *acs = radv_amdgpu_cs(cs);
   struct radv_amdgpu_cs *next_acs = radv_amdgpu_cs(next_cs);

   /* Only some HW IP types have packets that we can use for chaining. */
   if (!acs->use_ib)
      return false;

   assert(cs->cdw <= cs->max_dw + 4);
   assert(get_nop_packet(acs) == PKT3_NOP_PAD); /* Other shouldn't chain. */

   acs->chained_to = next_acs;

   cs->buf[cs->cdw - 4] = PKT3(PKT3_INDIRECT_BUFFER_CIK, 2, 0);
   cs->buf[cs->cdw - 3] = next_acs->ib.ib_mc_address;
   cs->buf[cs->cdw - 2] = next_acs->ib.ib_mc_address >> 32;
   cs->buf[cs->cdw - 1] =
      S_3F2_CHAIN(1) | S_3F2_VALID(1) | S_3F2_PRE_ENA(pre_ena) | next_acs->ib.size;

   return true;
}

static int
radv_amdgpu_cs_find_buffer(struct radv_amdgpu_cs *cs, uint32_t bo)
{
   unsigned hash = bo & (ARRAY_SIZE(cs->buffer_hash_table) - 1);
   int index = cs->buffer_hash_table[hash];

   if (index == -1)
      return -1;

   if (cs->handles[index].bo_handle == bo)
      return index;

   for (unsigned i = 0; i < cs->num_buffers; ++i) {
      if (cs->handles[i].bo_handle == bo) {
         cs->buffer_hash_table[hash] = i;
         return i;
      }
   }

   return -1;
}

static void
radv_amdgpu_cs_add_buffer_internal(struct radv_amdgpu_cs *cs, uint32_t bo, uint8_t priority)
{
   unsigned hash;
   int index = radv_amdgpu_cs_find_buffer(cs, bo);

   if (index != -1)
      return;

   if (cs->num_buffers == cs->max_num_buffers) {
      unsigned new_count = MAX2(1, cs->max_num_buffers * 2);
      struct drm_amdgpu_bo_list_entry *new_entries =
         realloc(cs->handles, new_count * sizeof(struct drm_amdgpu_bo_list_entry));
      if (new_entries) {
         cs->max_num_buffers = new_count;
         cs->handles = new_entries;
      } else {
         cs->status = VK_ERROR_OUT_OF_HOST_MEMORY;
         return;
      }
   }

   cs->handles[cs->num_buffers].bo_handle = bo;
   cs->handles[cs->num_buffers].bo_priority = priority;

   hash = bo & (ARRAY_SIZE(cs->buffer_hash_table) - 1);
   cs->buffer_hash_table[hash] = cs->num_buffers;

   ++cs->num_buffers;
}

static void
radv_amdgpu_cs_add_virtual_buffer(struct radeon_cmdbuf *_cs, struct radeon_winsys_bo *bo)
{
   struct radv_amdgpu_cs *cs = radv_amdgpu_cs(_cs);
   unsigned hash = ((uintptr_t)bo >> 6) & (VIRTUAL_BUFFER_HASH_TABLE_SIZE - 1);

   if (!cs->virtual_buffer_hash_table) {
      int *virtual_buffer_hash_table = malloc(VIRTUAL_BUFFER_HASH_TABLE_SIZE * sizeof(int));
      if (!virtual_buffer_hash_table) {
         cs->status = VK_ERROR_OUT_OF_HOST_MEMORY;
         return;
      }
      cs->virtual_buffer_hash_table = virtual_buffer_hash_table;

      for (int i = 0; i < VIRTUAL_BUFFER_HASH_TABLE_SIZE; ++i)
         cs->virtual_buffer_hash_table[i] = -1;
   }

   if (cs->virtual_buffer_hash_table[hash] >= 0) {
      int idx = cs->virtual_buffer_hash_table[hash];
      if (cs->virtual_buffers[idx] == bo) {
         return;
      }
      for (unsigned i = 0; i < cs->num_virtual_buffers; ++i) {
         if (cs->virtual_buffers[i] == bo) {
            cs->virtual_buffer_hash_table[hash] = i;
            return;
         }
      }
   }

   if (cs->max_num_virtual_buffers <= cs->num_virtual_buffers) {
      unsigned max_num_virtual_buffers = MAX2(2, cs->max_num_virtual_buffers * 2);
      struct radeon_winsys_bo **virtual_buffers =
         realloc(cs->virtual_buffers, sizeof(struct radeon_winsys_bo *) * max_num_virtual_buffers);
      if (!virtual_buffers) {
         cs->status = VK_ERROR_OUT_OF_HOST_MEMORY;
         return;
      }
      cs->max_num_virtual_buffers = max_num_virtual_buffers;
      cs->virtual_buffers = virtual_buffers;
   }

   cs->virtual_buffers[cs->num_virtual_buffers] = bo;

   cs->virtual_buffer_hash_table[hash] = cs->num_virtual_buffers;
   ++cs->num_virtual_buffers;
}

static void
radv_amdgpu_cs_add_buffer(struct radeon_cmdbuf *_cs, struct radeon_winsys_bo *_bo)
{
   struct radv_amdgpu_cs *cs = radv_amdgpu_cs(_cs);
   struct radv_amdgpu_winsys_bo *bo = radv_amdgpu_winsys_bo(_bo);

   if (cs->status != VK_SUCCESS)
      return;

   if (bo->is_virtual) {
      radv_amdgpu_cs_add_virtual_buffer(_cs, _bo);
      return;
   }

   radv_amdgpu_cs_add_buffer_internal(cs, bo->bo_handle, bo->priority);
}

static void
radv_amdgpu_cs_execute_secondary(struct radeon_cmdbuf *_parent, struct radeon_cmdbuf *_child,
                                 bool allow_ib2)
{
   struct radv_amdgpu_cs *parent = radv_amdgpu_cs(_parent);
   struct radv_amdgpu_cs *child = radv_amdgpu_cs(_child);
   struct radv_amdgpu_winsys *ws = parent->ws;
   bool use_ib2 = parent->use_ib && allow_ib2;

   if (parent->status != VK_SUCCESS || child->status != VK_SUCCESS)
      return;

   for (unsigned i = 0; i < child->num_buffers; ++i) {
      radv_amdgpu_cs_add_buffer_internal(parent, child->handles[i].bo_handle,
                                         child->handles[i].bo_priority);
   }

   for (unsigned i = 0; i < child->num_virtual_buffers; ++i) {
      radv_amdgpu_cs_add_buffer(&parent->base, child->virtual_buffers[i]);
   }

   if (use_ib2) {
      if (parent->base.cdw + 4 > parent->base.max_dw)
         radv_amdgpu_cs_grow(&parent->base, 4);

      /* Not setting the CHAIN bit will launch an IB2. */
      radeon_emit(&parent->base, PKT3(PKT3_INDIRECT_BUFFER_CIK, 2, 0));
      radeon_emit(&parent->base, child->ib.ib_mc_address);
      radeon_emit(&parent->base, child->ib.ib_mc_address >> 32);
      radeon_emit(&parent->base, child->ib.size);
   } else {
      assert(parent->use_ib == child->use_ib);

      /* Grow the current CS and copy the contents of the secondary CS. */
      for (unsigned i = 0; i < child->num_old_ib_buffers; i++) {
         struct radv_amdgpu_ib *ib = &child->old_ib_buffers[i];
         uint8_t *mapped;

         if (parent->base.cdw + ib->cdw > parent->base.max_dw)
            radv_amdgpu_cs_grow(&parent->base, ib->cdw);

         mapped = ws->base.buffer_map(ib->bo);
         if (!mapped) {
            parent->status = VK_ERROR_OUT_OF_HOST_MEMORY;
            return;
         }

         /* Copy the IB data without the original chain link. */
         memcpy(parent->base.buf + parent->base.cdw, mapped, 4 * ib->cdw);
         parent->base.cdw += ib->cdw;
      }

      /* When the parent and child can both use IBs, the current (last)
       * IB is not part of old_ib_buffers, take care of that here.
       */
      if (child->ib_buffer) {
         if (parent->base.cdw + child->base.cdw > parent->base.max_dw)
            radv_amdgpu_cs_grow(&parent->base, child->base.cdw);

         memcpy(parent->base.buf + parent->base.cdw, child->base.buf, 4 * child->base.cdw);
         parent->base.cdw += child->base.cdw;
      }
   }
}

static unsigned
radv_amdgpu_add_cs_to_bo_list(struct radv_amdgpu_cs *cs, struct drm_amdgpu_bo_list_entry *handles,
                              unsigned num_handles)
{
   if (!cs->num_buffers)
      return num_handles;

   if (num_handles == 0 && !cs->num_virtual_buffers) {
      memcpy(handles, cs->handles, cs->num_buffers * sizeof(struct drm_amdgpu_bo_list_entry));
      return cs->num_buffers;
   }

   int unique_bo_so_far = num_handles;
   for (unsigned j = 0; j < cs->num_buffers; ++j) {
      bool found = false;
      for (unsigned k = 0; k < unique_bo_so_far; ++k) {
         if (handles[k].bo_handle == cs->handles[j].bo_handle) {
            found = true;
            break;
         }
      }
      if (!found) {
         handles[num_handles] = cs->handles[j];
         ++num_handles;
      }
   }
   for (unsigned j = 0; j < cs->num_virtual_buffers; ++j) {
      struct radv_amdgpu_winsys_bo *virtual_bo = radv_amdgpu_winsys_bo(cs->virtual_buffers[j]);
      for (unsigned k = 0; k < virtual_bo->bo_count; ++k) {
         struct radv_amdgpu_winsys_bo *bo = virtual_bo->bos[k];
         bool found = false;
         for (unsigned m = 0; m < num_handles; ++m) {
            if (handles[m].bo_handle == bo->bo_handle) {
               found = true;
               break;
            }
         }
         if (!found) {
            handles[num_handles].bo_handle = bo->bo_handle;
            handles[num_handles].bo_priority = bo->priority;
            ++num_handles;
         }
      }
   }

   return num_handles;
}

static VkResult
radv_amdgpu_get_bo_list(struct radv_amdgpu_winsys *ws, struct radeon_cmdbuf **cs_array,
                        unsigned count, struct radv_amdgpu_winsys_bo **extra_bo_array,
                        unsigned num_extra_bo, struct radeon_cmdbuf **extra_cs_array,
                        unsigned num_extra_cs, unsigned *rnum_handles,
                        struct drm_amdgpu_bo_list_entry **rhandles)
{
   struct drm_amdgpu_bo_list_entry *handles = NULL;
   unsigned num_handles = 0;

   if (ws->debug_all_bos) {
      handles = malloc(sizeof(handles[0]) * ws->global_bo_list.count);
      if (!handles) {
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }

      for (uint32_t i = 0; i < ws->global_bo_list.count; i++) {
         handles[i].bo_handle = ws->global_bo_list.bos[i]->bo_handle;
         handles[i].bo_priority = ws->global_bo_list.bos[i]->priority;
         num_handles++;
      }
   } else if (count == 1 && !num_extra_bo && !num_extra_cs &&
              !radv_amdgpu_cs(cs_array[0])->num_virtual_buffers &&
              !radv_amdgpu_cs(cs_array[0])->chained_to && !ws->global_bo_list.count) {
      struct radv_amdgpu_cs *cs = (struct radv_amdgpu_cs *)cs_array[0];
      if (cs->num_buffers == 0)
         return VK_SUCCESS;

      handles = malloc(sizeof(handles[0]) * cs->num_buffers);
      if (!handles)
         return VK_ERROR_OUT_OF_HOST_MEMORY;

      memcpy(handles, cs->handles, sizeof(handles[0]) * cs->num_buffers);
      num_handles = cs->num_buffers;
   } else {
      unsigned total_buffer_count = num_extra_bo;
      num_handles = num_extra_bo;
      for (unsigned i = 0; i < count; ++i) {
         struct radv_amdgpu_cs *start_cs = (struct radv_amdgpu_cs *)cs_array[i];
         for (struct radv_amdgpu_cs *cs = start_cs; cs; cs = cs->chained_to) {
            total_buffer_count += cs->num_buffers;
            for (unsigned j = 0; j < cs->num_virtual_buffers; ++j)
               total_buffer_count += radv_amdgpu_winsys_bo(cs->virtual_buffers[j])->bo_count;
         }
      }

      if (num_extra_cs) {
         for (unsigned i = 0; i < num_extra_cs; ++i)
            total_buffer_count += ((struct radv_amdgpu_cs *)extra_cs_array[i])->num_buffers;
      }

      total_buffer_count += ws->global_bo_list.count;

      if (total_buffer_count == 0)
         return VK_SUCCESS;

      handles = malloc(sizeof(handles[0]) * total_buffer_count);
      if (!handles)
         return VK_ERROR_OUT_OF_HOST_MEMORY;

      for (unsigned i = 0; i < num_extra_bo; i++) {
         handles[i].bo_handle = extra_bo_array[i]->bo_handle;
         handles[i].bo_priority = extra_bo_array[i]->priority;
      }

      for (unsigned i = 0; i < count + num_extra_cs; ++i) {
         struct radv_amdgpu_cs *start_cs = i >= count
                                              ? (struct radv_amdgpu_cs *)extra_cs_array[i - count]
                                              : (struct radv_amdgpu_cs *)cs_array[i];

         for (struct radv_amdgpu_cs *cs = start_cs; cs; cs = cs->chained_to)
            num_handles = radv_amdgpu_add_cs_to_bo_list(cs, handles, num_handles);
      }

      unsigned unique_bo_so_far = num_handles;
      for (unsigned i = 0; i < ws->global_bo_list.count; ++i) {
         struct radv_amdgpu_winsys_bo *bo = ws->global_bo_list.bos[i];
         bool found = false;
         for (unsigned j = 0; j < unique_bo_so_far; ++j) {
            if (bo->bo_handle == handles[j].bo_handle) {
               found = true;
               break;
            }
         }
         if (!found) {
            handles[num_handles].bo_handle = bo->bo_handle;
            handles[num_handles].bo_priority = bo->priority;
            ++num_handles;
         }
      }
   }

   *rhandles = handles;
   *rnum_handles = num_handles;

   return VK_SUCCESS;
}

static void
radv_assign_last_submit(struct radv_amdgpu_ctx *ctx, struct radv_amdgpu_cs_request *request)
{
   radv_amdgpu_request_to_fence(ctx, &ctx->last_submission[request->ip_type][request->ring],
                                request);
}

static VkResult
radv_amdgpu_winsys_cs_submit_internal(
   struct radv_amdgpu_ctx *ctx, int queue_idx, struct radv_winsys_sem_info *sem_info,
   struct radeon_cmdbuf **cs_array, unsigned cs_count, struct radeon_cmdbuf **initial_preamble_cs,
   unsigned initial_preamble_count, struct radeon_cmdbuf **continue_preamble_cs,
   unsigned continue_preamble_count, struct radeon_cmdbuf **postamble_cs, unsigned postamble_count,
   bool uses_shadow_regs)
{
   VkResult result;

   /* Last CS is "the gang leader", its IP type determines which fence to signal. */
   struct radv_amdgpu_cs *last_cs = radv_amdgpu_cs(cs_array[cs_count - 1]);
   struct radv_amdgpu_winsys *ws = last_cs->ws;

   struct drm_amdgpu_bo_list_entry *handles = NULL;
   unsigned num_handles = 0;

   unsigned num_extra_cs = initial_preamble_count + continue_preamble_count + postamble_count;
   unsigned extra_cs_idx = 0;

   STACK_ARRAY(struct radeon_cmdbuf *, extra_cs, num_extra_cs);

   for (unsigned i = 0; i < initial_preamble_count; i++)
      extra_cs[extra_cs_idx++] = initial_preamble_cs[i];
   for (unsigned i = 0; i < continue_preamble_count; i++)
      extra_cs[extra_cs_idx++] = continue_preamble_cs[i];
   for (unsigned i = 0; i < postamble_count; i++)
      extra_cs[extra_cs_idx++] = postamble_cs[i];

   u_rwlock_rdlock(&ws->global_bo_list.lock);
   result = radv_amdgpu_get_bo_list(ws, &cs_array[0], cs_count, NULL, 0, extra_cs, num_extra_cs,
                                    &num_handles, &handles);
   if (result != VK_SUCCESS)
      goto fail;

   /* Configure the CS request. */
   const uint8_t *max_ib_per_ip = ws->info.max_submitted_ibs;
   struct radv_amdgpu_cs_ib_info ibs[RADV_MAX_IBS_PER_SUBMIT];
   struct radv_amdgpu_cs_request request = {
      .ip_type = last_cs->hw_ip,
      .ip_instance = 0,
      .ring = queue_idx,
      .handles = handles,
      .num_handles = num_handles,
      .ibs = ibs,
      .number_of_ibs = 0, /* set below */
   };

   assert(cs_count);
   assert(MAX2(initial_preamble_count, continue_preamble_count) + postamble_count <
          RADV_MAX_IBS_PER_SUBMIT);

   for (unsigned cs_idx = 0, cs_ib_idx = 0; cs_idx < cs_count;) {
      struct radeon_cmdbuf **preambles = cs_idx ? continue_preamble_cs : initial_preamble_cs;
      const unsigned preamble_count = cs_idx ? continue_preamble_count : initial_preamble_count;
      const unsigned ib_per_submit = RADV_MAX_IBS_PER_SUBMIT - preamble_count - postamble_count;
      unsigned num_submitted_ibs = 0;
      unsigned ibs_per_ip[AMD_NUM_IP_TYPES] = {0};

      /* Copy preambles to the submission. */
      for (unsigned i = 0; i < preamble_count; ++i) {
         /* Assume that the full preamble fits into 1 IB. */
         struct radv_amdgpu_cs *cs = radv_amdgpu_cs(preambles[i]);
         assert(cs->num_old_ib_buffers <= 1);
         ibs[num_submitted_ibs++] = cs->ib;
         ibs_per_ip[cs->hw_ip]++;
      }

      for (unsigned i = 0; i < ib_per_submit && cs_idx < cs_count; ++i) {
         struct radv_amdgpu_cs *cs = radv_amdgpu_cs(cs_array[cs_idx]);
         struct radv_amdgpu_cs_ib_info ib;

         if (cs_ib_idx == 0) {
            /* Make sure the whole CS fits into the same submission. */
            unsigned cs_num_ib = cs->use_ib ? 1 : cs->num_old_ib_buffers;
            if (i + cs_num_ib > ib_per_submit ||
                ibs_per_ip[cs->hw_ip] + cs_num_ib > max_ib_per_ip[cs->hw_ip])
               break;

            if (cs->hw_ip != request.ip_type) {
               /* Found a "follower" CS in a gang submission.
                * Make sure to submit this together with its "leader", the next CS.
                * We rely on the caller to order each "follower" before its "leader."
                */
               assert(cs_idx != cs_count - 1);
               struct radv_amdgpu_cs *next_cs = radv_amdgpu_cs(cs_array[cs_idx + 1]);
               assert(next_cs->hw_ip == request.ip_type);
               unsigned next_cs_num_ib = next_cs->use_ib ? 1 : next_cs->num_old_ib_buffers;
               if (i + cs_num_ib + next_cs_num_ib > ib_per_submit ||
                   ibs_per_ip[next_cs->hw_ip] + next_cs_num_ib > max_ib_per_ip[next_cs->hw_ip])
                  break;
            }
         }

         /* When can use IBs, we only need to submit the main IB of this CS,
          * because everything else is chained to the first IB.
          * Otherwise we must submit all IBs in the old_ib_buffers array.
          */
         if (cs->use_ib) {
            ib = cs->ib;
            cs_idx++;
         } else {
            assert(cs_ib_idx < cs->num_old_ib_buffers);
            ib = radv_amdgpu_cs_ib_to_info(cs, cs->old_ib_buffers[cs_ib_idx++]);

            if (cs_ib_idx == cs->num_old_ib_buffers) {
               cs_idx++;
               cs_ib_idx = 0;
            }
         }

         if (uses_shadow_regs && ib.ip_type == AMDGPU_HW_IP_GFX)
            ib.flags |= AMDGPU_IB_FLAG_PREEMPT;

         ibs[num_submitted_ibs++] = ib;
         ibs_per_ip[cs->hw_ip]++;
      }

      assert(num_submitted_ibs > preamble_count);

      /* Copy postambles to the submission. */
      for (unsigned i = 0; i < postamble_count; ++i) {
         /* Assume that the full postamble fits into 1 IB. */
         struct radv_amdgpu_cs *cs = radv_amdgpu_cs(postamble_cs[i]);
         assert(cs->num_old_ib_buffers <= 1);
         ibs[num_submitted_ibs++] = cs->ib;
         ibs_per_ip[cs->hw_ip]++;
      }

      /* Submit the CS. */
      request.number_of_ibs = num_submitted_ibs;
      result = radv_amdgpu_cs_submit(ctx, &request, sem_info);
      if (result != VK_SUCCESS)
         goto fail;
   }

   free(request.handles);

   if (result != VK_SUCCESS)
      goto fail;

   radv_assign_last_submit(ctx, &request);

fail:
   STACK_ARRAY_FINISH(extra_cs);
   u_rwlock_rdunlock(&ws->global_bo_list.lock);
   return result;
}

static VkResult
radv_amdgpu_cs_submit_zero(struct radv_amdgpu_ctx *ctx, enum amd_ip_type ip_type, int queue_idx,
                           struct radv_winsys_sem_info *sem_info)
{
   unsigned hw_ip = ip_type;
   unsigned queue_syncobj = radv_amdgpu_ctx_queue_syncobj(ctx, hw_ip, queue_idx);
   int ret;

   if (!queue_syncobj)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   if (sem_info->wait.syncobj_count || sem_info->wait.timeline_syncobj_count) {
      int fd;
      ret = amdgpu_cs_syncobj_export_sync_file(ctx->ws->dev, queue_syncobj, &fd);
      if (ret < 0)
         return VK_ERROR_DEVICE_LOST;

      for (unsigned i = 0; i < sem_info->wait.syncobj_count; ++i) {
         int fd2;
         ret = amdgpu_cs_syncobj_export_sync_file(ctx->ws->dev, sem_info->wait.syncobj[i], &fd2);
         if (ret < 0) {
            close(fd);
            return VK_ERROR_DEVICE_LOST;
         }

         sync_accumulate("radv", &fd, fd2);
         close(fd2);
      }
      for (unsigned i = 0; i < sem_info->wait.timeline_syncobj_count; ++i) {
         int fd2;
         ret = amdgpu_cs_syncobj_export_sync_file2(
            ctx->ws->dev, sem_info->wait.syncobj[i + sem_info->wait.syncobj_count],
            sem_info->wait.points[i], 0, &fd2);
         if (ret < 0) {
            /* This works around a kernel bug where the fence isn't copied if it is already
             * signalled. Since it is already signalled it is totally fine to not wait on it.
             *
             * kernel patch: https://patchwork.freedesktop.org/patch/465583/ */
            uint64_t point;
            ret = amdgpu_cs_syncobj_query2(
               ctx->ws->dev, &sem_info->wait.syncobj[i + sem_info->wait.syncobj_count], &point, 1,
               0);
            if (!ret && point >= sem_info->wait.points[i])
               continue;

            close(fd);
            return VK_ERROR_DEVICE_LOST;
         }

         sync_accumulate("radv", &fd, fd2);
         close(fd2);
      }
      ret = amdgpu_cs_syncobj_import_sync_file(ctx->ws->dev, queue_syncobj, fd);
      close(fd);
      if (ret < 0)
         return VK_ERROR_DEVICE_LOST;

      ctx->queue_syncobj_wait[hw_ip][queue_idx] = true;
   }

   for (unsigned i = 0; i < sem_info->signal.syncobj_count; ++i) {
      uint32_t dst_handle = sem_info->signal.syncobj[i];
      uint32_t src_handle = queue_syncobj;

      if (ctx->ws->info.has_timeline_syncobj) {
         ret = amdgpu_cs_syncobj_transfer(ctx->ws->dev, dst_handle, 0, src_handle, 0, 0);
         if (ret < 0)
            return VK_ERROR_DEVICE_LOST;
      } else {
         int fd;
         ret = amdgpu_cs_syncobj_export_sync_file(ctx->ws->dev, src_handle, &fd);
         if (ret < 0)
            return VK_ERROR_DEVICE_LOST;

         ret = amdgpu_cs_syncobj_import_sync_file(ctx->ws->dev, dst_handle, fd);
         close(fd);
         if (ret < 0)
            return VK_ERROR_DEVICE_LOST;
      }
   }
   for (unsigned i = 0; i < sem_info->signal.timeline_syncobj_count; ++i) {
      ret = amdgpu_cs_syncobj_transfer(ctx->ws->dev,
                                       sem_info->signal.syncobj[i + sem_info->signal.syncobj_count],
                                       sem_info->signal.points[i], queue_syncobj, 0, 0);
      if (ret < 0)
         return VK_ERROR_DEVICE_LOST;
   }
   return VK_SUCCESS;
}

static VkResult
radv_amdgpu_winsys_cs_submit(struct radeon_winsys_ctx *_ctx,
                             const struct radv_winsys_submit_info *submit, uint32_t wait_count,
                             const struct vk_sync_wait *waits, uint32_t signal_count,
                             const struct vk_sync_signal *signals)
{
   struct radv_amdgpu_ctx *ctx = radv_amdgpu_ctx(_ctx);
   struct radv_amdgpu_winsys *ws = ctx->ws;
   VkResult result;
   unsigned wait_idx = 0, signal_idx = 0;

   STACK_ARRAY(uint64_t, wait_points, wait_count);
   STACK_ARRAY(uint32_t, wait_syncobj, wait_count);
   STACK_ARRAY(uint64_t, signal_points, signal_count);
   STACK_ARRAY(uint32_t, signal_syncobj, signal_count);

   if (!wait_points || !wait_syncobj || !signal_points || !signal_syncobj) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto out;
   }

   for (uint32_t i = 0; i < wait_count; ++i) {
      if (waits[i].sync->type == &vk_sync_dummy_type)
         continue;

      assert(waits[i].sync->type == &ws->syncobj_sync_type);
      wait_syncobj[wait_idx] = ((struct vk_drm_syncobj *)waits[i].sync)->syncobj;
      wait_points[wait_idx] = waits[i].wait_value;
      ++wait_idx;
   }

   for (uint32_t i = 0; i < signal_count; ++i) {
      if (signals[i].sync->type == &vk_sync_dummy_type)
         continue;

      assert(signals[i].sync->type == &ws->syncobj_sync_type);
      signal_syncobj[signal_idx] = ((struct vk_drm_syncobj *)signals[i].sync)->syncobj;
      signal_points[signal_idx] = signals[i].signal_value;
      ++signal_idx;
   }

   assert(signal_idx <= signal_count);
   assert(wait_idx <= wait_count);

   const uint32_t wait_timeline_syncobj_count =
      (ws->syncobj_sync_type.features & VK_SYNC_FEATURE_TIMELINE) ? wait_idx : 0;
   const uint32_t signal_timeline_syncobj_count =
      (ws->syncobj_sync_type.features & VK_SYNC_FEATURE_TIMELINE) ? signal_idx : 0;

   struct radv_winsys_sem_info sem_info = {
      .wait =
         {
            .points = wait_points,
            .syncobj = wait_syncobj,
            .timeline_syncobj_count = wait_timeline_syncobj_count,
            .syncobj_count = wait_idx - wait_timeline_syncobj_count,
         },
      .signal =
         {
            .points = signal_points,
            .syncobj = signal_syncobj,
            .timeline_syncobj_count = signal_timeline_syncobj_count,
            .syncobj_count = signal_idx - signal_timeline_syncobj_count,
         },
      .cs_emit_wait = true,
      .cs_emit_signal = true,
   };

   if (!submit->cs_count) {
      result = radv_amdgpu_cs_submit_zero(ctx, submit->ip_type, submit->queue_index, &sem_info);
   } else {
      result = radv_amdgpu_winsys_cs_submit_internal(
         ctx, submit->queue_index, &sem_info, submit->cs_array, submit->cs_count,
         submit->initial_preamble_cs, submit->initial_preamble_count, submit->continue_preamble_cs,
         submit->continue_preamble_count, submit->postamble_cs, submit->postamble_count,
         submit->uses_shadow_regs);
   }

out:
   STACK_ARRAY_FINISH(wait_points);
   STACK_ARRAY_FINISH(wait_syncobj);
   STACK_ARRAY_FINISH(signal_points);
   STACK_ARRAY_FINISH(signal_syncobj);
   return result;
}

static void *
radv_amdgpu_winsys_get_cpu_addr(void *_cs, uint64_t addr)
{
   struct radv_amdgpu_cs *cs = (struct radv_amdgpu_cs *)_cs;
   void *ret = NULL;

   if (!cs->ib_buffer)
      return NULL;
   for (unsigned i = 0; i <= cs->num_old_ib_buffers; ++i) {
      struct radv_amdgpu_winsys_bo *bo;

      bo = (struct radv_amdgpu_winsys_bo *)(i == cs->num_old_ib_buffers ? cs->ib_buffer
                                                                        : cs->old_ib_buffers[i].bo);
      if (addr >= bo->base.va && addr - bo->base.va < bo->size) {
         if (amdgpu_bo_cpu_map(bo->bo, &ret) == 0)
            return (char *)ret + (addr - bo->base.va);
      }
   }
   u_rwlock_rdlock(&cs->ws->global_bo_list.lock);
   for (uint32_t i = 0; i < cs->ws->global_bo_list.count; i++) {
      struct radv_amdgpu_winsys_bo *bo = cs->ws->global_bo_list.bos[i];
      if (addr >= bo->base.va && addr - bo->base.va < bo->size) {
         if (amdgpu_bo_cpu_map(bo->bo, &ret) == 0) {
            u_rwlock_rdunlock(&cs->ws->global_bo_list.lock);
            return (char *)ret + (addr - bo->base.va);
         }
      }
   }
   u_rwlock_rdunlock(&cs->ws->global_bo_list.lock);

   return ret;
}

static void
radv_amdgpu_winsys_cs_dump(struct radeon_cmdbuf *_cs, FILE *file, const int *trace_ids,
                           int trace_id_count)
{
   struct radv_amdgpu_cs *cs = (struct radv_amdgpu_cs *)_cs;
   void *ib = radv_amdgpu_winsys_get_cpu_addr(cs, cs->ib.ib_mc_address);
   int num_dw = cs->base.cdw;
   assert(ib);
   ac_parse_ib(file, ib, num_dw, trace_ids, trace_id_count, "main IB", cs->ws->info.gfx_level,
               cs->ws->info.family, radv_amdgpu_winsys_get_cpu_addr, cs);
}

static uint32_t
radv_to_amdgpu_priority(enum radeon_ctx_priority radv_priority)
{
   switch (radv_priority) {
   case RADEON_CTX_PRIORITY_REALTIME:
      return AMDGPU_CTX_PRIORITY_VERY_HIGH;
   case RADEON_CTX_PRIORITY_HIGH:
      return AMDGPU_CTX_PRIORITY_HIGH;
   case RADEON_CTX_PRIORITY_MEDIUM:
      return AMDGPU_CTX_PRIORITY_NORMAL;
   case RADEON_CTX_PRIORITY_LOW:
      return AMDGPU_CTX_PRIORITY_LOW;
   default:
      unreachable("Invalid context priority");
   }
}

static VkResult
radv_amdgpu_ctx_create(struct radeon_winsys *_ws, enum radeon_ctx_priority priority,
                       struct radeon_winsys_ctx **rctx)
{
   struct radv_amdgpu_winsys *ws = radv_amdgpu_winsys(_ws);
   struct radv_amdgpu_ctx *ctx = CALLOC_STRUCT(radv_amdgpu_ctx);
   uint32_t amdgpu_priority = radv_to_amdgpu_priority(priority);
   VkResult result;
   int r;

   if (!ctx)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   r = amdgpu_cs_ctx_create2(ws->dev, amdgpu_priority, &ctx->ctx);
   if (r && r == -EACCES) {
      result = VK_ERROR_NOT_PERMITTED_KHR;
      goto fail_create;
   } else if (r) {
      fprintf(stderr, "radv/amdgpu: radv_amdgpu_cs_ctx_create2 failed. (%i)\n", r);
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto fail_create;
   }
   ctx->ws = ws;

   assert(AMDGPU_HW_IP_NUM * MAX_RINGS_PER_TYPE * 4 * sizeof(uint64_t) <= 4096);
   result = ws->base.buffer_create(&ws->base, 4096, 8, RADEON_DOMAIN_GTT,
                                   RADEON_FLAG_CPU_ACCESS | RADEON_FLAG_NO_INTERPROCESS_SHARING,
                                   RADV_BO_PRIORITY_CS, 0, &ctx->fence_bo);
   if (result != VK_SUCCESS) {
      goto fail_alloc;
   }

   *rctx = (struct radeon_winsys_ctx *)ctx;
   return VK_SUCCESS;

fail_alloc:
   amdgpu_cs_ctx_free(ctx->ctx);
fail_create:
   FREE(ctx);
   return result;
}

static void
radv_amdgpu_ctx_destroy(struct radeon_winsys_ctx *rwctx)
{
   struct radv_amdgpu_ctx *ctx = (struct radv_amdgpu_ctx *)rwctx;

   for (unsigned ip = 0; ip <= AMDGPU_HW_IP_NUM; ++ip) {
      for (unsigned ring = 0; ring < MAX_RINGS_PER_TYPE; ++ring) {
         if (ctx->queue_syncobj[ip][ring])
            amdgpu_cs_destroy_syncobj(ctx->ws->dev, ctx->queue_syncobj[ip][ring]);
      }
   }

   ctx->ws->base.buffer_destroy(&ctx->ws->base, ctx->fence_bo);
   amdgpu_cs_ctx_free(ctx->ctx);
   FREE(ctx);
}

static uint32_t
radv_amdgpu_ctx_queue_syncobj(struct radv_amdgpu_ctx *ctx, unsigned ip, unsigned ring)
{
   uint32_t *syncobj = &ctx->queue_syncobj[ip][ring];
   if (!*syncobj) {
      amdgpu_cs_create_syncobj2(ctx->ws->dev, DRM_SYNCOBJ_CREATE_SIGNALED, syncobj);
   }
   return *syncobj;
}

static bool
radv_amdgpu_ctx_wait_idle(struct radeon_winsys_ctx *rwctx, enum amd_ip_type ip_type, int ring_index)
{
   struct radv_amdgpu_ctx *ctx = (struct radv_amdgpu_ctx *)rwctx;

   if (ctx->last_submission[ip_type][ring_index].fence.fence) {
      uint32_t expired;
      int ret = amdgpu_cs_query_fence_status(&ctx->last_submission[ip_type][ring_index].fence,
                                             1000000000ull, 0, &expired);

      if (ret || !expired)
         return false;
   }

   return true;
}

static enum radv_reset_status
radv_amdgpu_ctx_query_reset_status(struct radeon_winsys_ctx *rwctx)
{
   int ret;
   struct radv_amdgpu_ctx *ctx = (struct radv_amdgpu_ctx *)rwctx;

   if (ctx->ws->info.drm_minor >= 24) {
      uint64_t flags;
      ret = amdgpu_cs_query_reset_state2(ctx->ctx, &flags);

      if (ret) {
         fprintf(stderr, "radv/amdgpu: amdgpu_cs_query_reset_state2 failed. (%i)\n", ret);
	 return RADV_NO_RESET;
      }

      if (flags & AMDGPU_CTX_QUERY2_FLAGS_RESET) {
         if (flags & AMDGPU_CTX_QUERY2_FLAGS_GUILTY) {
            /* Some job from this context once caused a GPU hang */
            return RADV_GUILTY_CONTEXT_RESET;
         } else {
            /* Some job from other context caused a GPU hang */
            return RADV_INNOCENT_CONTEXT_RESET;
         }
      }
   } else {
      uint32_t result, hangs;

      ret = amdgpu_cs_query_reset_state(ctx->ctx, &result, &hangs);
      if (ret) {
         fprintf(stderr, "radv/amdgpu: amdgpu_cs_query_reset_state failed. (%i)\n", ret);
         return RADV_NO_RESET;
      }

      switch (result) {
      case AMDGPU_CTX_GUILTY_RESET:
         return RADV_GUILTY_CONTEXT_RESET;
      case AMDGPU_CTX_INNOCENT_RESET:
         return RADV_INNOCENT_CONTEXT_RESET;
      case AMDGPU_CTX_UNKNOWN_RESET:
         return RADV_UNKNOWN_CONTEXT_RESET;
      }
   }

   return RADV_NO_RESET;
}

static uint32_t
radv_to_amdgpu_pstate(enum radeon_ctx_pstate radv_pstate)
{
   switch (radv_pstate) {
   case RADEON_CTX_PSTATE_NONE:
      return AMDGPU_CTX_STABLE_PSTATE_NONE;
   case RADEON_CTX_PSTATE_STANDARD:
      return AMDGPU_CTX_STABLE_PSTATE_STANDARD;
   case RADEON_CTX_PSTATE_MIN_SCLK:
      return AMDGPU_CTX_STABLE_PSTATE_MIN_SCLK;
   case RADEON_CTX_PSTATE_MIN_MCLK:
      return AMDGPU_CTX_STABLE_PSTATE_MIN_MCLK;
   case RADEON_CTX_PSTATE_PEAK:
      return AMDGPU_CTX_STABLE_PSTATE_PEAK;
   default:
      unreachable("Invalid pstate");
   }
}

static int
radv_amdgpu_ctx_set_pstate(struct radeon_winsys_ctx *rwctx, enum radeon_ctx_pstate pstate)
{
   struct radv_amdgpu_ctx *ctx = (struct radv_amdgpu_ctx *)rwctx;
   uint32_t new_pstate = radv_to_amdgpu_pstate(pstate);
   uint32_t current_pstate = 0;
   int r;

   r = amdgpu_cs_ctx_stable_pstate(ctx->ctx, AMDGPU_CTX_OP_GET_STABLE_PSTATE, 0, &current_pstate);
   if (r) {
      fprintf(stderr, "radv/amdgpu: failed to get current pstate\n");
      return r;
   }

   /* Do not try to set a new pstate when the current one is already what we want. Otherwise, the
    * kernel might return -EBUSY if we have multiple AMDGPU contexts in flight.
    */
   if (current_pstate == new_pstate)
      return 0;

   r = amdgpu_cs_ctx_stable_pstate(ctx->ctx, AMDGPU_CTX_OP_SET_STABLE_PSTATE, new_pstate, NULL);
   if (r) {
      fprintf(stderr, "radv/amdgpu: failed to set new pstate\n");
      return r;
   }

   return 0;
}

static void *
radv_amdgpu_cs_alloc_syncobj_chunk(struct radv_winsys_sem_counts *counts, uint32_t queue_syncobj,
                                   struct drm_amdgpu_cs_chunk *chunk, int chunk_id)
{
   unsigned count = counts->syncobj_count + (queue_syncobj ? 1 : 0);
   struct drm_amdgpu_cs_chunk_sem *syncobj =
      malloc(sizeof(struct drm_amdgpu_cs_chunk_sem) * count);
   if (!syncobj)
      return NULL;

   for (unsigned i = 0; i < counts->syncobj_count; i++) {
      struct drm_amdgpu_cs_chunk_sem *sem = &syncobj[i];
      sem->handle = counts->syncobj[i];
   }

   if (queue_syncobj)
      syncobj[counts->syncobj_count].handle = queue_syncobj;

   chunk->chunk_id = chunk_id;
   chunk->length_dw = sizeof(struct drm_amdgpu_cs_chunk_sem) / 4 * count;
   chunk->chunk_data = (uint64_t)(uintptr_t)syncobj;
   return syncobj;
}

static void *
radv_amdgpu_cs_alloc_timeline_syncobj_chunk(struct radv_winsys_sem_counts *counts,
                                            uint32_t queue_syncobj,
                                            struct drm_amdgpu_cs_chunk *chunk, int chunk_id)
{
   uint32_t count =
      counts->syncobj_count + counts->timeline_syncobj_count + (queue_syncobj ? 1 : 0);
   struct drm_amdgpu_cs_chunk_syncobj *syncobj =
      malloc(sizeof(struct drm_amdgpu_cs_chunk_syncobj) * count);
   if (!syncobj)
      return NULL;

   for (unsigned i = 0; i < counts->syncobj_count; i++) {
      struct drm_amdgpu_cs_chunk_syncobj *sem = &syncobj[i];
      sem->handle = counts->syncobj[i];
      sem->flags = 0;
      sem->point = 0;
   }

   for (unsigned i = 0; i < counts->timeline_syncobj_count; i++) {
      struct drm_amdgpu_cs_chunk_syncobj *sem = &syncobj[i + counts->syncobj_count];
      sem->handle = counts->syncobj[i + counts->syncobj_count];
      sem->flags = DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT;
      sem->point = counts->points[i];
   }

   if (queue_syncobj) {
      syncobj[count - 1].handle = queue_syncobj;
      syncobj[count - 1].flags = 0;
      syncobj[count - 1].point = 0;
   }

   chunk->chunk_id = chunk_id;
   chunk->length_dw = sizeof(struct drm_amdgpu_cs_chunk_syncobj) / 4 * count;
   chunk->chunk_data = (uint64_t)(uintptr_t)syncobj;
   return syncobj;
}

static bool
radv_amdgpu_cs_has_user_fence(struct radv_amdgpu_cs_request *request)
{
   return request->ip_type != AMDGPU_HW_IP_UVD &&
          request->ip_type != AMDGPU_HW_IP_VCE &&
          request->ip_type != AMDGPU_HW_IP_UVD_ENC &&
          request->ip_type != AMDGPU_HW_IP_VCN_DEC &&
          request->ip_type != AMDGPU_HW_IP_VCN_ENC &&
          request->ip_type != AMDGPU_HW_IP_VCN_JPEG;
}

static VkResult
radv_amdgpu_cs_submit(struct radv_amdgpu_ctx *ctx, struct radv_amdgpu_cs_request *request,
                      struct radv_winsys_sem_info *sem_info)
{
   int r;
   int num_chunks;
   int size;
   struct drm_amdgpu_cs_chunk *chunks;
   struct drm_amdgpu_cs_chunk_data *chunk_data;
   bool use_bo_list_create = ctx->ws->info.drm_minor < 27;
   struct drm_amdgpu_bo_list_in bo_list_in;
   void *wait_syncobj = NULL, *signal_syncobj = NULL;
   int i;
   uint32_t bo_list = 0;
   VkResult result = VK_SUCCESS;
   bool has_user_fence = radv_amdgpu_cs_has_user_fence(request);
   uint32_t queue_syncobj = radv_amdgpu_ctx_queue_syncobj(ctx, request->ip_type, request->ring);
   bool *queue_syncobj_wait = &ctx->queue_syncobj_wait[request->ip_type][request->ring];

   if (!queue_syncobj)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   size = request->number_of_ibs + 1 + (has_user_fence ? 1 : 0) + (!use_bo_list_create ? 1 : 0) + 3;

   chunks = malloc(sizeof(chunks[0]) * size);
   if (!chunks)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   size = request->number_of_ibs + (has_user_fence ? 1 : 0);

   chunk_data = malloc(sizeof(chunk_data[0]) * size);
   if (!chunk_data) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto error_out;
   }

   num_chunks = request->number_of_ibs;
   for (i = 0; i < request->number_of_ibs; i++) {
      struct radv_amdgpu_cs_ib_info *ib;
      chunks[i].chunk_id = AMDGPU_CHUNK_ID_IB;
      chunks[i].length_dw = sizeof(struct drm_amdgpu_cs_chunk_ib) / 4;
      chunks[i].chunk_data = (uint64_t)(uintptr_t)&chunk_data[i];

      ib = &request->ibs[i];
      assert(ib->ib_mc_address && ib->ib_mc_address % ctx->ws->info.ib_alignment == 0);
      assert(ib->size);

      chunk_data[i].ib_data._pad = 0;
      chunk_data[i].ib_data.va_start = ib->ib_mc_address;
      chunk_data[i].ib_data.ib_bytes = ib->size * 4;
      chunk_data[i].ib_data.ip_type = ib->ip_type;
      chunk_data[i].ib_data.ip_instance = request->ip_instance;
      chunk_data[i].ib_data.ring = request->ring;
      chunk_data[i].ib_data.flags = ib->flags;
   }

   assert(chunk_data[request->number_of_ibs - 1].ib_data.ip_type == request->ip_type);

   if (has_user_fence) {
      i = num_chunks++;
      chunks[i].chunk_id = AMDGPU_CHUNK_ID_FENCE;
      chunks[i].length_dw = sizeof(struct drm_amdgpu_cs_chunk_fence) / 4;
      chunks[i].chunk_data = (uint64_t)(uintptr_t)&chunk_data[i];

      struct amdgpu_cs_fence_info fence_info;
      fence_info.handle = radv_amdgpu_winsys_bo(ctx->fence_bo)->bo;
      /* Need to reserve 4 QWORD for user fence:
       *   QWORD[0]: completed fence
       *   QWORD[1]: preempted fence
       *   QWORD[2]: reset fence
       *   QWORD[3]: preempted then reset
       */
      fence_info.offset = (request->ip_type * MAX_RINGS_PER_TYPE + request->ring) * 4;
      amdgpu_cs_chunk_fence_info_to_data(&fence_info, &chunk_data[i]);
   }

   if (sem_info->cs_emit_wait && (sem_info->wait.timeline_syncobj_count ||
                                  sem_info->wait.syncobj_count || *queue_syncobj_wait)) {

      if (ctx->ws->info.has_timeline_syncobj) {
         wait_syncobj = radv_amdgpu_cs_alloc_timeline_syncobj_chunk(
            &sem_info->wait, queue_syncobj, &chunks[num_chunks],
            AMDGPU_CHUNK_ID_SYNCOBJ_TIMELINE_WAIT);
      } else {
         wait_syncobj = radv_amdgpu_cs_alloc_syncobj_chunk(
            &sem_info->wait, queue_syncobj, &chunks[num_chunks], AMDGPU_CHUNK_ID_SYNCOBJ_IN);
      }
      if (!wait_syncobj) {
         result = VK_ERROR_OUT_OF_HOST_MEMORY;
         goto error_out;
      }
      num_chunks++;

      sem_info->cs_emit_wait = false;
      *queue_syncobj_wait = false;
   }

   if (sem_info->cs_emit_signal) {
      if (ctx->ws->info.has_timeline_syncobj) {
         signal_syncobj = radv_amdgpu_cs_alloc_timeline_syncobj_chunk(
            &sem_info->signal, queue_syncobj, &chunks[num_chunks],
            AMDGPU_CHUNK_ID_SYNCOBJ_TIMELINE_SIGNAL);
      } else {
         signal_syncobj = radv_amdgpu_cs_alloc_syncobj_chunk(
            &sem_info->signal, queue_syncobj, &chunks[num_chunks], AMDGPU_CHUNK_ID_SYNCOBJ_OUT);
      }
      if (!signal_syncobj) {
         result = VK_ERROR_OUT_OF_HOST_MEMORY;
         goto error_out;
      }
      num_chunks++;
   }

   if (use_bo_list_create) {
      /* Legacy path creating the buffer list handle and passing it
       * to the CS ioctl.
       */
      r = amdgpu_bo_list_create_raw(ctx->ws->dev, request->num_handles,
                                    request->handles, &bo_list);
      if (r) {
         if (r == -ENOMEM) {
            fprintf(stderr, "radv/amdgpu: Not enough memory for buffer list creation.\n");
            result = VK_ERROR_OUT_OF_HOST_MEMORY;
         } else {
            fprintf(stderr, "radv/amdgpu: buffer list creation failed (%d).\n", r);
            result = VK_ERROR_UNKNOWN;
         }
         goto error_out;
      }
   } else {
      /* Standard path passing the buffer list via the CS ioctl. */
      bo_list_in.operation = ~0;
      bo_list_in.list_handle = ~0;
      bo_list_in.bo_number = request->num_handles;
      bo_list_in.bo_info_size = sizeof(struct drm_amdgpu_bo_list_entry);
      bo_list_in.bo_info_ptr = (uint64_t)(uintptr_t)request->handles;

      chunks[num_chunks].chunk_id = AMDGPU_CHUNK_ID_BO_HANDLES;
      chunks[num_chunks].length_dw = sizeof(struct drm_amdgpu_bo_list_in) / 4;
      chunks[num_chunks].chunk_data = (uintptr_t)&bo_list_in;
      num_chunks++;
   }

   /* The kernel returns -ENOMEM with many parallel processes using GDS such as test suites quite
    * often, but it eventually succeeds after enough attempts. This happens frequently with dEQP
    * using NGG streamout.
    */
   uint64_t abs_timeout_ns = os_time_get_absolute_timeout(1000000000ull); /* 1s */

   r = 0;
   do {
      /* Wait 1 ms and try again. */
      if (r == -ENOMEM)
         os_time_sleep(1000);

      r = amdgpu_cs_submit_raw2(ctx->ws->dev, ctx->ctx, bo_list, num_chunks, chunks, &request->seq_no);
   } while (r == -ENOMEM && os_time_get_nano() < abs_timeout_ns);

   if (r) {
      if (r == -ENOMEM) {
         fprintf(stderr, "radv/amdgpu: Not enough memory for command submission.\n");
         result = VK_ERROR_OUT_OF_HOST_MEMORY;
      } else if (r == -ECANCELED) {
         fprintf(stderr, "radv/amdgpu: The CS has been cancelled because the context is lost.\n");
         result = VK_ERROR_DEVICE_LOST;
      } else {
         fprintf(stderr,
                 "radv/amdgpu: The CS has been rejected, "
                 "see dmesg for more information (%i).\n",
                 r);
         result = VK_ERROR_UNKNOWN;
      }
   }

   if (bo_list)
      amdgpu_bo_list_destroy_raw(ctx->ws->dev, bo_list);

error_out:
   free(chunks);
   free(chunk_data);
   free(wait_syncobj);
   free(signal_syncobj);
   return result;
}

void
radv_amdgpu_cs_init_functions(struct radv_amdgpu_winsys *ws)
{
   ws->base.ctx_create = radv_amdgpu_ctx_create;
   ws->base.ctx_destroy = radv_amdgpu_ctx_destroy;
   ws->base.ctx_wait_idle = radv_amdgpu_ctx_wait_idle;
   ws->base.ctx_set_pstate = radv_amdgpu_ctx_set_pstate;
   ws->base.ctx_query_reset_status = radv_amdgpu_ctx_query_reset_status;
   ws->base.cs_domain = radv_amdgpu_cs_domain;
   ws->base.cs_create = radv_amdgpu_cs_create;
   ws->base.cs_destroy = radv_amdgpu_cs_destroy;
   ws->base.cs_grow = radv_amdgpu_cs_grow;
   ws->base.cs_finalize = radv_amdgpu_cs_finalize;
   ws->base.cs_reset = radv_amdgpu_cs_reset;
   ws->base.cs_chain = radv_amdgpu_cs_chain;
   ws->base.cs_unchain = radv_amdgpu_cs_unchain;
   ws->base.cs_add_buffer = radv_amdgpu_cs_add_buffer;
   ws->base.cs_execute_secondary = radv_amdgpu_cs_execute_secondary;
   ws->base.cs_submit = radv_amdgpu_winsys_cs_submit;
   ws->base.cs_dump = radv_amdgpu_winsys_cs_dump;
}
