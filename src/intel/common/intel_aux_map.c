/*
 * Copyright (c) 2018 Intel Corporation
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

/**
 * The aux map provides a multi-level lookup of the main surface address which
 * ends up providing information about the auxiliary surface data, including
 * the address where the auxiliary data resides.
 *
 * The below sections depict address splitting and formats of table entries of
 * TGL platform. These may vary on other platforms.
 *
 * The 48-bit VMA (GPU) address of the main surface is split to do the address
 * lookup:
 *
 *  48 bit address of main surface
 * +--------+--------+--------+------+
 * | 47:36  | 35:24  | 23:16  | 15:0 |
 * | L3-idx | L2-idx | L1-idx | ...  |
 * +--------+--------+--------+------+
 *
 * The GFX_AUX_TABLE_BASE_ADDR points to a buffer. The L3 Table Entry is
 * located by indexing into this buffer as a uint64_t array using the L3-idx
 * value. The 64-bit L3 entry is defined as:
 *
 * +-------+-------------+------+---+
 * | 63:48 | 47:15       | 14:1 | 0 |
 * |  ...  | L2-tbl-addr | ...  | V |
 * +-------+-------------+------+---+
 *
 * If the `V` (valid) bit is set, then the L2-tbl-addr gives the address for
 * the level-2 table entries, with the lower address bits filled with zero.
 * The L2 Table Entry is located by indexing into this buffer as a uint64_t
 * array using the L2-idx value. The 64-bit L2 entry is similar to the L3
 * entry, except with 2 additional address bits:
 *
 * +-------+-------------+------+---+
 * | 63:48 | 47:13       | 12:1 | 0 |
 * |  ...  | L1-tbl-addr | ...  | V |
 * +-------+-------------+------+---+
 *
 * If the `V` bit is set, then the L1-tbl-addr gives the address for the
 * level-1 table entries, with the lower address bits filled with zero. The L1
 * Table Entry is located by indexing into this buffer as a uint64_t array
 * using the L1-idx value. The 64-bit L1 entry is defined as:
 *
 * +--------+------+-------+-------+-------+---------------+-----+---+
 * | 63:58  | 57   | 56:54 | 53:52 | 51:48 | 47:8          | 7:1 | 0 |
 * | Format | Y/Cr | Depth |  TM   |  ...  | aux-data-addr | ... | V |
 * +--------+------+-------+-------+-------+---------------+-----+---+
 *
 * Where:
 *  - Format: See `get_format_encoding`
 *  - Y/Cr: 0=Y(Luma), 1=Cr(Chroma)
 *  - (bit) Depth: See `get_bpp_encoding`
 *  - TM (Tile-mode): 0=Ys, 1=Y, 2=rsvd, 3=rsvd
 *  - aux-data-addr: VMA/GPU address for the aux-data
 *  - V: entry is valid
 */

#include "intel_aux_map.h"
#include "intel_gem.h"

#include "dev/intel_device_info.h"
#include "isl/isl.h"

#include "drm-uapi/i915_drm.h"
#include "util/list.h"
#include "util/ralloc.h"
#include "util/u_atomic.h"
#include "util/u_math.h"

#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

#define INTEL_AUX_MAP_FORMAT_BITS_MASK   0xfff0000000000000ull

static const bool aux_map_debug = false;

/**
 * Auxiliary surface mapping formats
 *
 * Several formats of AUX mapping exist. The supported formats
 * are designated by generation and granularity here. A device
 * can support more than one format, depending on Hardware, but
 * we expect only one of them of a device is needed. Otherwise,
 * we could need to change this enum into a bit map in such case
 * later.
 */
enum intel_aux_map_format {
   /**
    * 64KB granularity format on GFX12 devices
    */
   INTEL_AUX_MAP_GFX12_64KB = 0,

   /**
    * 1MB granularity format on GFX125 devices
    */
   INTEL_AUX_MAP_GFX125_1MB,

   INTEL_AUX_MAP_LAST,
};

/**
 * An incomplete description of AUX mapping formats
 *
 * Theoretically, many things can be different, depending on hardware
 * design like level of page tables, address splitting, format bits
 * etc. We only manage the known delta to simplify the implementation
 * this time.
 */
struct aux_format_info {
   /**
    * Granularity of main surface in compression. It must be power of 2.
    */
   uint64_t main_page_size;
   /**
    * The ratio of main surface to an AUX entry.
    */
   uint64_t main_to_aux_ratio;
   /**
    * Page size of level 1 page table. It must be power of 2.
    */
   uint64_t l1_page_size;
   /**
    * Mask of index bits of level 1 page table in address splitting.
    */
   uint64_t l1_index_mask;
   /**
    * Offset of index bits of level 1 page table in address splitting.
    */
   uint64_t l1_index_offset;
};

static const struct aux_format_info aux_formats[] = {
   [INTEL_AUX_MAP_GFX12_64KB] = {
      .main_page_size = 64 * 1024,
      .main_to_aux_ratio = 256,
      .l1_page_size = 8 * 1024,
      .l1_index_mask = 0xff,
      .l1_index_offset = 16,
   },
   [INTEL_AUX_MAP_GFX125_1MB] = {
      .main_page_size = 1024 * 1024,
      .main_to_aux_ratio = 256,
      .l1_page_size = 2 * 1024,
      .l1_index_mask = 0xf,
      .l1_index_offset = 20,
   },
};

struct aux_map_buffer {
   struct list_head link;
   struct intel_buffer *buffer;
};

struct intel_aux_map_context {
   void *driver_ctx;
   pthread_mutex_t mutex;
   struct intel_mapped_pinned_buffer_alloc *buffer_alloc;
   uint32_t num_buffers;
   struct list_head buffers;
   uint64_t level3_base_addr;
   uint64_t *level3_map;
   uint32_t tail_offset, tail_remaining;
   uint32_t state_num;
   const struct aux_format_info *format;
};

static inline uint64_t
get_page_mask(const uint64_t page_size)
{
   return page_size - 1;
}

static inline uint64_t
get_meta_page_size(const struct aux_format_info *info)
{
   return info->main_page_size / info->main_to_aux_ratio;
}

static inline uint64_t
get_index(const uint64_t main_address,
      const uint64_t index_mask, const uint64_t index_offset)
{
   return (main_address >> index_offset) & index_mask;
}

uint64_t
intel_aux_get_meta_address_mask(struct intel_aux_map_context *ctx)
{
   return ~get_page_mask(get_meta_page_size(ctx->format)) << 16 >> 16;
}

uint64_t
intel_aux_get_main_to_aux_ratio(struct intel_aux_map_context *ctx)
{
   return ctx->format->main_to_aux_ratio;
}

static const struct aux_format_info *
get_format(enum intel_aux_map_format format)
{

   assert(format < INTEL_AUX_MAP_LAST);
   assert(ARRAY_SIZE(aux_formats) == INTEL_AUX_MAP_LAST);
   return &aux_formats[format];
}

static enum intel_aux_map_format
select_format(const struct intel_device_info *devinfo)
{
   if (devinfo->verx10 >= 125)
      return INTEL_AUX_MAP_GFX125_1MB;
   else if (devinfo->verx10 == 120)
      return INTEL_AUX_MAP_GFX12_64KB;
   else
      return INTEL_AUX_MAP_LAST;
}

static bool
add_buffer(struct intel_aux_map_context *ctx)
{
   struct aux_map_buffer *buf = ralloc(ctx, struct aux_map_buffer);
   if (!buf)
      return false;

   const uint32_t size = 0x100000;
   buf->buffer = ctx->buffer_alloc->alloc(ctx->driver_ctx, size);
   if (!buf->buffer) {
      ralloc_free(buf);
      return false;
   }

   assert(buf->buffer->map != NULL);

   list_addtail(&buf->link, &ctx->buffers);
   ctx->tail_offset = 0;
   ctx->tail_remaining = size;
   p_atomic_inc(&ctx->num_buffers);

   return true;
}

static void
advance_current_pos(struct intel_aux_map_context *ctx, uint32_t size)
{
   assert(ctx->tail_remaining >= size);
   ctx->tail_remaining -= size;
   ctx->tail_offset += size;
}

static bool
align_and_verify_space(struct intel_aux_map_context *ctx, uint32_t size,
                       uint32_t align)
{
   if (ctx->tail_remaining < size)
      return false;

   struct aux_map_buffer *tail =
      list_last_entry(&ctx->buffers, struct aux_map_buffer, link);
   uint64_t gpu = tail->buffer->gpu + ctx->tail_offset;
   uint64_t aligned = align64(gpu, align);

   if ((aligned - gpu) + size > ctx->tail_remaining) {
      return false;
   } else {
      if (aligned - gpu > 0)
         advance_current_pos(ctx, aligned - gpu);
      return true;
   }
}

static void
get_current_pos(struct intel_aux_map_context *ctx, uint64_t *gpu, uint64_t **map)
{
   assert(!list_is_empty(&ctx->buffers));
   struct aux_map_buffer *tail =
      list_last_entry(&ctx->buffers, struct aux_map_buffer, link);
   if (gpu)
      *gpu = tail->buffer->gpu + ctx->tail_offset;
   if (map)
      *map = (uint64_t*)((uint8_t*)tail->buffer->map + ctx->tail_offset);
}

static bool
add_sub_table(struct intel_aux_map_context *ctx, uint32_t size,
              uint32_t align, uint64_t *gpu, uint64_t **map)
{
   if (!align_and_verify_space(ctx, size, align)) {
      if (!add_buffer(ctx))
         return false;
      UNUSED bool aligned = align_and_verify_space(ctx, size, align);
      assert(aligned);
   }
   get_current_pos(ctx, gpu, map);
   memset(*map, 0, size);
   advance_current_pos(ctx, size);
   return true;
}

uint32_t
intel_aux_map_get_state_num(struct intel_aux_map_context *ctx)
{
   return p_atomic_read(&ctx->state_num);
}

struct intel_aux_map_context *
intel_aux_map_init(void *driver_ctx,
                   struct intel_mapped_pinned_buffer_alloc *buffer_alloc,
                   const struct intel_device_info *devinfo)
{
   struct intel_aux_map_context *ctx;

   enum intel_aux_map_format format = select_format(devinfo);
   if (format == INTEL_AUX_MAP_LAST)
      return NULL;

   ctx = ralloc(NULL, struct intel_aux_map_context);
   if (!ctx)
      return NULL;

   if (pthread_mutex_init(&ctx->mutex, NULL))
      return NULL;

   ctx->format = get_format(format);
   ctx->driver_ctx = driver_ctx;
   ctx->buffer_alloc = buffer_alloc;
   ctx->num_buffers = 0;
   list_inithead(&ctx->buffers);
   ctx->tail_offset = 0;
   ctx->tail_remaining = 0;
   ctx->state_num = 0;

   if (add_sub_table(ctx, 32 * 1024, 32 * 1024, &ctx->level3_base_addr,
                     &ctx->level3_map)) {
      if (aux_map_debug)
         fprintf(stderr, "AUX-MAP L3: 0x%"PRIx64", map=%p\n",
                 ctx->level3_base_addr, ctx->level3_map);
      p_atomic_inc(&ctx->state_num);
      return ctx;
   } else {
      ralloc_free(ctx);
      return NULL;
   }
}

void
intel_aux_map_finish(struct intel_aux_map_context *ctx)
{
   if (!ctx)
      return;

   pthread_mutex_destroy(&ctx->mutex);
   list_for_each_entry_safe(struct aux_map_buffer, buf, &ctx->buffers, link) {
      ctx->buffer_alloc->free(ctx->driver_ctx, buf->buffer);
      list_del(&buf->link);
      p_atomic_dec(&ctx->num_buffers);
      ralloc_free(buf);
   }

   ralloc_free(ctx);
}

uint64_t
intel_aux_map_get_base(struct intel_aux_map_context *ctx)
{
   /**
    * This get initialized in intel_aux_map_init, and never changes, so there is
    * no need to lock the mutex.
    */
   return ctx->level3_base_addr;
}

static struct aux_map_buffer *
find_buffer(struct intel_aux_map_context *ctx, uint64_t addr)
{
   list_for_each_entry(struct aux_map_buffer, buf, &ctx->buffers, link) {
      if (buf->buffer->gpu <= addr && buf->buffer->gpu_end > addr) {
         return buf;
      }
   }
   return NULL;
}

static uint64_t *
get_u64_entry_ptr(struct intel_aux_map_context *ctx, uint64_t addr)
{
   struct aux_map_buffer *buf = find_buffer(ctx, addr);
   assert(buf);
   uintptr_t map_offset = addr - buf->buffer->gpu;
   return (uint64_t*)((uint8_t*)buf->buffer->map + map_offset);
}

static uint8_t
get_bpp_encoding(enum isl_format format)
{
   if (isl_format_is_yuv(format)) {
      switch (format) {
      case ISL_FORMAT_YCRCB_NORMAL:
      case ISL_FORMAT_YCRCB_SWAPY:
      case ISL_FORMAT_PLANAR_420_8: return 3;
      case ISL_FORMAT_PLANAR_420_12: return 2;
      case ISL_FORMAT_PLANAR_420_10: return 1;
      case ISL_FORMAT_PLANAR_420_16: return 0;
      default:
         unreachable("Unsupported format!");
         return 0;
      }
   } else {
      switch (isl_format_get_layout(format)->bpb) {
      case 16:  return 0;
      case 8:   return 4;
      case 32:  return 5;
      case 64:  return 6;
      case 128: return 7;
      default:
         unreachable("Unsupported bpp!");
         return 0;
      }
   }
}

#define INTEL_AUX_MAP_ENTRY_Y_TILED_BIT  (0x1ull << 52)

uint64_t
intel_aux_map_format_bits(enum isl_tiling tiling, enum isl_format format,
                          uint8_t plane)
{
   /* gfx12.5+ uses tile-4 rather than y-tiling, and gfx12.5+ also uses
    * compression info from the surface state and ignores the aux-map format
    * bits metadata.
    */
   if (!isl_tiling_is_any_y(tiling))
      return 0;

   if (aux_map_debug)
      fprintf(stderr, "AUX-MAP entry %s, bpp_enc=%d\n",
              isl_format_get_name(format),
              isl_format_get_aux_map_encoding(format));

   uint64_t format_bits =
      ((uint64_t)isl_format_get_aux_map_encoding(format) << 58) |
      ((uint64_t)(plane > 0) << 57) |
      ((uint64_t)get_bpp_encoding(format) << 54) |
      INTEL_AUX_MAP_ENTRY_Y_TILED_BIT;

   assert((format_bits & INTEL_AUX_MAP_FORMAT_BITS_MASK) == format_bits);

   return format_bits;
}

uint64_t
intel_aux_map_format_bits_for_isl_surf(const struct isl_surf *isl_surf)
{
   assert(!isl_format_is_planar(isl_surf->format));
   return intel_aux_map_format_bits(isl_surf->tiling, isl_surf->format, 0);
}

static void
get_aux_entry(struct intel_aux_map_context *ctx, uint64_t address,
              uint32_t *l1_index_out, uint64_t *l1_entry_addr_out,
              uint64_t **l1_entry_map_out)
{
   uint32_t l3_index = (address >> 36) & 0xfff;
   uint64_t *l3_entry = &ctx->level3_map[l3_index];

   uint64_t *l2_map;
   if ((*l3_entry & INTEL_AUX_MAP_ENTRY_VALID_BIT) == 0) {
      uint64_t l2_gpu;
      if (add_sub_table(ctx, 32 * 1024, 32 * 1024, &l2_gpu, &l2_map)) {
         if (aux_map_debug)
            fprintf(stderr, "AUX-MAP L3[0x%x]: 0x%"PRIx64", map=%p\n",
                    l3_index, l2_gpu, l2_map);
      } else {
         unreachable("Failed to add L2 Aux-Map Page Table!");
      }
      *l3_entry = (l2_gpu & 0xffffffff8000ULL) | 1;
   } else {
      uint64_t l2_addr = intel_canonical_address(*l3_entry & ~0x7fffULL);
      l2_map = get_u64_entry_ptr(ctx, l2_addr);
   }
   uint32_t l2_index = (address >> 24) & 0xfff;
   uint64_t *l2_entry = &l2_map[l2_index];
   uint64_t l1_page_size = ctx->format->l1_page_size;
   uint64_t l1_addr, *l1_map;
   if ((*l2_entry & INTEL_AUX_MAP_ENTRY_VALID_BIT) == 0) {
      if (add_sub_table(ctx, l1_page_size, l1_page_size, &l1_addr, &l1_map)) {
         if (aux_map_debug)
            fprintf(stderr, "AUX-MAP L2[0x%x]: 0x%"PRIx64", map=%p\n",
                    l2_index, l1_addr, l1_map);
      } else {
         unreachable("Failed to add L1 Aux-Map Page Table!");
      }
      *l2_entry = (l1_addr & ~get_page_mask(l1_page_size)) | 1;
   } else {
      l1_addr = intel_canonical_address(
            *l2_entry & ~get_page_mask(l1_page_size));
      l1_map = get_u64_entry_ptr(ctx, l1_addr);
   }
   uint32_t l1_index = get_index(address, ctx->format->l1_index_mask,
         ctx->format->l1_index_offset);
   if (l1_index_out)
      *l1_index_out = l1_index;
   if (l1_entry_addr_out)
      *l1_entry_addr_out = l1_addr + l1_index * sizeof(*l1_map);
   if (l1_entry_map_out)
      *l1_entry_map_out = &l1_map[l1_index];
}

static void
add_mapping(struct intel_aux_map_context *ctx, uint64_t address,
            uint64_t aux_address, uint64_t format_bits,
            bool *state_changed)
{
   if (aux_map_debug)
      fprintf(stderr, "AUX-MAP 0x%"PRIx64" => 0x%"PRIx64"\n", address,
              aux_address);

   uint32_t l1_index;
   uint64_t *l1_entry;
   get_aux_entry(ctx, address, &l1_index, NULL, &l1_entry);

   const uint64_t l1_data =
      (aux_address & intel_aux_get_meta_address_mask(ctx)) |
      format_bits |
      INTEL_AUX_MAP_ENTRY_VALID_BIT;

   const uint64_t current_l1_data = *l1_entry;
   if ((current_l1_data & INTEL_AUX_MAP_ENTRY_VALID_BIT) == 0) {
      assert((aux_address & 0xffULL) == 0);
      if (aux_map_debug)
         fprintf(stderr, "AUX-MAP L1[0x%x] 0x%"PRIx64" -> 0x%"PRIx64"\n",
                 l1_index, current_l1_data, l1_data);
      /**
       * We use non-zero bits in 63:1 to indicate the entry had been filled
       * previously. If these bits are non-zero and they don't exactly match
       * what we want to program into the entry, then we must force the
       * aux-map tables to be flushed.
       */
      if (current_l1_data != 0 && \
          (current_l1_data | INTEL_AUX_MAP_ENTRY_VALID_BIT) != l1_data)
         *state_changed = true;
      *l1_entry = l1_data;
   } else {
      if (aux_map_debug)
         fprintf(stderr, "AUX-MAP L1[0x%x] is already marked valid!\n",
                 l1_index);
      assert(*l1_entry == l1_data);
   }
}

uint64_t *
intel_aux_map_get_entry(struct intel_aux_map_context *ctx,
                        uint64_t address,
                        uint64_t *entry_address)
{
   pthread_mutex_lock(&ctx->mutex);
   uint64_t *l1_entry_map;
   get_aux_entry(ctx, address, NULL, entry_address, &l1_entry_map);
   pthread_mutex_unlock(&ctx->mutex);

   return l1_entry_map;
}

void
intel_aux_map_add_mapping(struct intel_aux_map_context *ctx, uint64_t address,
                          uint64_t aux_address, uint64_t main_size_B,
                          uint64_t format_bits)
{
   bool state_changed = false;
   pthread_mutex_lock(&ctx->mutex);
   uint64_t map_addr = address;
   uint64_t dest_aux_addr = aux_address;
   uint64_t main_page_size = ctx->format->main_page_size;
   assert((address & get_page_mask(main_page_size)) == 0);
   uint64_t aux_page_size = get_meta_page_size(ctx->format);
   assert((aux_address & get_page_mask(aux_page_size)) == 0);
   while (map_addr - address < main_size_B) {
      add_mapping(ctx, map_addr, dest_aux_addr, format_bits, &state_changed);
      map_addr += main_page_size;
      dest_aux_addr += aux_page_size;
   }
   pthread_mutex_unlock(&ctx->mutex);
   if (state_changed)
      p_atomic_inc(&ctx->state_num);
}

/**
 * We mark the leaf entry as invalid, but we don't attempt to cleanup the
 * other levels of translation mappings. Since we attempt to re-use VMA
 * ranges, hopefully this will not lead to unbounded growth of the translation
 * tables.
 */
static void
remove_mapping(struct intel_aux_map_context *ctx, uint64_t address,
               bool *state_changed)
{
   uint32_t l3_index = (address >> 36) & 0xfff;
   uint64_t *l3_entry = &ctx->level3_map[l3_index];

   uint64_t *l2_map;
   if ((*l3_entry & INTEL_AUX_MAP_ENTRY_VALID_BIT) == 0) {
      return;
   } else {
      uint64_t l2_addr = intel_canonical_address(*l3_entry & ~0x7fffULL);
      l2_map = get_u64_entry_ptr(ctx, l2_addr);
   }
   uint32_t l2_index = (address >> 24) & 0xfff;
   uint64_t *l2_entry = &l2_map[l2_index];

   uint64_t *l1_map;
   if ((*l2_entry & INTEL_AUX_MAP_ENTRY_VALID_BIT) == 0) {
      return;
   } else {
      uint64_t l1_addr = intel_canonical_address(
            *l2_entry & ~get_page_mask(ctx->format->l1_page_size));
      l1_map = get_u64_entry_ptr(ctx, l1_addr);
   }
   uint32_t l1_index = get_index(address, ctx->format->l1_index_mask,
         ctx->format->l1_index_offset);
   uint64_t *l1_entry = &l1_map[l1_index];

   const uint64_t current_l1_data = *l1_entry;
   const uint64_t l1_data = current_l1_data & ~1ull;

   if ((current_l1_data & INTEL_AUX_MAP_ENTRY_VALID_BIT) == 0) {
      return;
   } else {
      if (aux_map_debug)
         fprintf(stderr, "AUX-MAP [0x%x][0x%x][0x%x] L1 entry removed!\n",
                 l3_index, l2_index, l1_index);
      /**
       * We use non-zero bits in 63:1 to indicate the entry had been filled
       * previously. In the unlikely event that these are all zero, we force a
       * flush of the aux-map tables.
       */
      if (unlikely(l1_data == 0))
         *state_changed = true;
      *l1_entry = l1_data;
   }
}

void
intel_aux_map_unmap_range(struct intel_aux_map_context *ctx, uint64_t address,
                          uint64_t size)
{
   bool state_changed = false;
   pthread_mutex_lock(&ctx->mutex);
   if (aux_map_debug)
      fprintf(stderr, "AUX-MAP remove 0x%"PRIx64"-0x%"PRIx64"\n", address,
              address + size);

   uint64_t map_addr = address;
   uint64_t main_page_size = ctx->format->main_page_size;
   assert((address & get_page_mask(main_page_size)) == 0);
   while (map_addr - address < size) {
      remove_mapping(ctx, map_addr, &state_changed);
      map_addr += main_page_size;
   }
   pthread_mutex_unlock(&ctx->mutex);
   if (state_changed)
      p_atomic_inc(&ctx->state_num);
}

uint32_t
intel_aux_map_get_num_buffers(struct intel_aux_map_context *ctx)
{
   return p_atomic_read(&ctx->num_buffers);
}

void
intel_aux_map_fill_bos(struct intel_aux_map_context *ctx, void **driver_bos,
                       uint32_t max_bos)
{
   assert(p_atomic_read(&ctx->num_buffers) >= max_bos);
   uint32_t i = 0;
   list_for_each_entry(struct aux_map_buffer, buf, &ctx->buffers, link) {
      if (i >= max_bos)
         return;
      driver_bos[i++] = buf->buffer->driver_bo;
   }
}
