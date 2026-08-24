/*
 * Copyright © 2023 Imagination Technologies Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <vulkan/vulkan_core.h>

#include "hwdef/rogue_hw_utils.h"
#include "pvr_bo.h"
#include "pvr_border.h"
#include "pvr_device.h"
#include "pvr_device_info.h"
#include "pvr_formats.h"
#include "pvr_macros.h"
#include "pvr_physical_device.h"
#include "pvr_sampler.h"
#include "util/bitset.h"
#include "util/format/u_format.h"
#include "util/format/u_formats.h"
#include "util/log.h"
#include "util/macros.h"
#include "vk_format.h"
#include "vk_log.h"
#include "vk_sampler.h"

struct pvr_border_color_table_value {
   uint8_t value[16];
} PACKED;
static_assert(sizeof(struct pvr_border_color_table_value) ==
                 4 * sizeof(uint32_t),
              "struct pvr_border_color_table_value must be 4 x u32");

struct pvr_border_color_table_entry {
   struct pvr_border_color_table_value values[PVR_TEX_FORMAT_COUNT];
   struct pvr_border_color_table_value compressed_values[PVR_TEX_FORMAT_COUNT];
} PACKED;

/* FIXME: Replace all instances of uint32_t with ROGUE_TEXSTATE_FORMAT or
 * ROGUE_TEXSTATE_FORMAT_COMPRESSED after the pvr_common cleanup is complete.
 */

struct pvr_tex_format_description {
   enum pipe_format pipe_format_int;
   enum pipe_format pipe_format_float;
};

struct pvr_tex_format_compressed_description {
   uint32_t tex_format;
   enum pipe_format pipe_format;
   uint32_t tex_format_simple;
};

#define FORMAT(tex_fmt, pipe_fmt_int, pipe_fmt_float) \
   [ROGUE_TEXSTATE_FORMAT_##tex_fmt] = {                    \
      .desc = {                                             \
         .pipe_format_int = PIPE_FORMAT_##pipe_fmt_int,     \
         .pipe_format_float = PIPE_FORMAT_##pipe_fmt_float, \
      },                                                    \
      .present = true,                                      \
   }

static const struct pvr_tex_format_table_entry {
   struct pvr_tex_format_description desc;
   bool present;
} pvr_tex_format_table[PVR_TEX_FORMAT_COUNT] = {
   /*   0 */ FORMAT(U8, R8_UINT, R8_UNORM),
   /*   1 */ FORMAT(S8, R8_SINT, R8_SNORM),
   /*   2 */ FORMAT(A4R4G4B4, A4R4G4B4_UINT, A4R4G4B4_UNORM),
   /*   4 */ FORMAT(A1R5G5B5, A1R5G5B5_UINT, B5G5R5A1_UNORM),
   /*   5 */ FORMAT(R5G6B5, R5G6B5_UINT, B5G6R5_UNORM),
   /*   7 */ FORMAT(U8U8, R8G8_UINT, R8G8_UNORM),
   /*   8 */ FORMAT(S8S8, R8G8_SINT, R8G8_SNORM),
   /*   9 */ FORMAT(U16, R16_UINT, R16_UNORM),
   /*  10 */ FORMAT(S16, R16_SINT, R16_SNORM),
   /*  11 */ FORMAT(F16, NONE, R16_FLOAT),
   /*  12 */ FORMAT(U8U8U8U8, R8G8B8A8_UINT, R8G8B8A8_UNORM),
   /*  13 */ FORMAT(S8S8S8S8, R8G8B8A8_SINT, R8G8B8A8_SNORM),
   /*  14 */ FORMAT(A2R10B10G10, R10G10B10A2_UINT, R10G10B10A2_UNORM),
   /*  15 */ FORMAT(U16U16, R16G16_UINT, R16G16_UNORM),
   /*  16 */ FORMAT(S16S16, R16G16_SINT, R16G16_SNORM),
   /*  17 */ FORMAT(F16F16, NONE, R16G16_FLOAT),
   /*  18 */ FORMAT(F32, NONE, R32_FLOAT),
   /*  21 */ FORMAT(X8U24, NONE, Z24X8_UNORM),
   /*  22 */ FORMAT(ST8U24, Z24_UNORM_S8_UINT, Z24_UNORM_S8_UINT),
   /*  23 */ FORMAT(U8X24, X24S8_UINT, NONE),
   /*  24 */ FORMAT(U32, R32_UINT, R32_UNORM),
   /*  25 */ FORMAT(S32, R32_SINT, R32_SNORM),
   /*  26 */ FORMAT(SE9995, NONE, R9G9B9E5_FLOAT),
   /*  28 */ FORMAT(F16F16F16F16, NONE, R16G16B16A16_FLOAT),
   /*  29 */ FORMAT(U16U16U16U16, R16G16B16A16_UINT, R16G16B16A16_UNORM),
   /*  30 */ FORMAT(S16S16S16S16, R16G16B16A16_SINT, R16G16B16A16_SNORM),
   /*  32 */ FORMAT(U16U16U16, R16G16B16_UINT, R16G16B16_UNORM),
   /*  33 */ FORMAT(S16S16S16, R16G16B16_SINT, R16G16B16_SNORM),
   /*  34 */ FORMAT(F32F32, NONE, R32G32_FLOAT),
   /*  35 */ FORMAT(U32U32, R32G32_UINT, R32G32_UNORM),
   /*  36 */ FORMAT(S32S32, R32G32_SINT, R32G32_SNORM),
   /*  37 */ FORMAT(X24U8F32, Z32_FLOAT_S8X24_UINT, Z32_FLOAT_S8X24_UINT),
   /*  38 */ FORMAT(X24X8F32, NONE, Z32_FLOAT_S8X24_UINT),
   /*  39 */ FORMAT(X24G8X32, X32_S8X24_UINT, NONE),
   /*  58 */ FORMAT(U8U8U8, R8G8B8_UINT, R8G8B8_UNORM),
   /*  61 */ FORMAT(F32F32F32F32, NONE, R32G32B32A32_FLOAT),
   /*  62 */ FORMAT(U32U32U32U32, R32G32B32A32_UINT, R32G32B32A32_UNORM),
   /*  63 */ FORMAT(S32S32S32S32, R32G32B32A32_SINT, R32G32B32A32_SNORM),
   /*  64 */ FORMAT(F32F32F32, NONE, R32G32B32_FLOAT),
   /*  65 */ FORMAT(U32U32U32, R32G32B32_UINT, R32G32B32_UNORM),
   /*  66 */ FORMAT(S32S32S32, R32G32B32_SINT, R32G32B32_SNORM),
   /*  88 */ FORMAT(F10F11F11, NONE, R11G11B10_FLOAT),
};

#undef FORMAT

#define FORMAT(tex_fmt, pipe_fmt, tex_fmt_simple) \
   [ROGUE_TEXSTATE_FORMAT_COMPRESSED_##tex_fmt] = {                   \
      .desc = {                                                       \
         .tex_format = ROGUE_TEXSTATE_FORMAT_COMPRESSED_##tex_fmt,    \
         .pipe_format = PIPE_FORMAT_##pipe_fmt,                       \
         .tex_format_simple = ROGUE_TEXSTATE_FORMAT_##tex_fmt_simple, \
      },                                                              \
      .present = true,                                                \
   }

static const struct pvr_tex_format_compressed_table_entry {
   struct pvr_tex_format_compressed_description desc;
   bool present;
} pvr_tex_format_compressed_table[PVR_TEX_FORMAT_COUNT] = {
   /*  68 */ FORMAT(ETC2_RGB, ETC2_RGB8, U8U8U8U8),
   /*  69 */ FORMAT(ETC2A_RGBA, ETC2_RGBA8, U8U8U8U8),
   /*  70 */ FORMAT(ETC2_PUNCHTHROUGHA, ETC2_RGB8A1, U8U8U8U8),
   /*  71 */ FORMAT(EAC_R11_UNSIGNED, ETC2_R11_UNORM, U16U16U16U16),
   /*  72 */ FORMAT(EAC_R11_SIGNED, ETC2_R11_SNORM, S16S16S16S16),
   /*  73 */ FORMAT(EAC_RG11_UNSIGNED, ETC2_RG11_UNORM, U16U16U16U16),
   /*  74 */ FORMAT(EAC_RG11_SIGNED, ETC2_RG11_SNORM, S16S16S16S16),
};

#undef FORMAT

static inline bool tex_format_is_supported(const uint32_t tex_format)
{
   return tex_format < ARRAY_SIZE(pvr_tex_format_table) &&
          pvr_tex_format_table[tex_format].present;
}

static inline const struct pvr_tex_format_description *
get_tex_format_description(const uint32_t tex_format)
{
   assert(tex_format_is_supported(tex_format));
   return &pvr_tex_format_table[tex_format].desc;
}

static inline bool tex_format_compressed_is_supported(const uint32_t tex_format)
{
   return tex_format < ARRAY_SIZE(pvr_tex_format_compressed_table) &&
          pvr_tex_format_compressed_table[tex_format].present;
}

static inline const struct pvr_tex_format_compressed_description *
get_tex_format_compressed_description(const uint32_t tex_format)
{
   assert(tex_format_compressed_is_supported(tex_format));
   return &pvr_tex_format_compressed_table[tex_format].desc;
}

static inline void pvr_border_color_table_pack_single(
   struct pvr_border_color_table_value *const dst,
   const union pipe_color_union *const color,
   const struct pvr_tex_format_description *const pvr_tex_fmt_desc,
   const bool is_int,
   const struct pvr_device_info *const dev_info)
{
   enum pipe_format pipe_format = is_int ? pvr_tex_fmt_desc->pipe_format_int
                                         : pvr_tex_fmt_desc->pipe_format_float;

   if (pipe_format == PIPE_FORMAT_NONE)
      return;

   memset(dst->value, 0, sizeof(dst->value));

   if (util_format_is_depth_or_stencil(pipe_format)) {
      if (is_int) {
         const uint8_t s_color[4] = {
            color->ui[0],
            color->ui[1],
            color->ui[2],
            color->ui[3],
         };

         util_format_pack_s_8uint(pipe_format, dst->value, s_color, 1);
      } else {
         util_format_pack_z_float(pipe_format, dst->value, color->f, 1);
      }
   } else {
      if (PVR_HAS_FEATURE(dev_info, tpu_border_colour_enhanced)) {
         if (pipe_format == PIPE_FORMAT_R9G9B9E5_FLOAT)
            pipe_format = PIPE_FORMAT_R16G16B16A16_FLOAT;
      }

      util_format_pack_rgba(pipe_format, dst->value, color, 1);
   }
}

static inline void pvr_border_color_table_pack_single_compressed(
   struct pvr_border_color_table_value *const dst,
   const union pipe_color_union *const color,
   const struct pvr_tex_format_compressed_description *const pvr_tex_fmt_desc,
   const bool is_int,
   const struct pvr_device_info *const dev_info)
{
   if (PVR_HAS_FEATURE(dev_info, tpu_border_colour_enhanced)) {
      const struct pvr_tex_format_description *pvr_tex_fmt_desc_simple =
         get_tex_format_description(pvr_tex_fmt_desc->tex_format_simple);

      pvr_border_color_table_pack_single(dst,
                                         color,
                                         pvr_tex_fmt_desc_simple,
                                         is_int,
                                         dev_info);
      return;
   }

   memset(dst->value, 0, sizeof(dst->value));

   pvr_finishme("Devices without tpu_border_colour_enhanced require entries "
                "for compressed formats to be stored in the table "
                "pre-compressed.");
}

static int32_t
pvr_border_color_table_alloc_entry(struct pvr_border_color_table *const table)
{
   /* BITSET_FFS() returns a 1-indexed position or 0 if no bits are set. */
   int32_t index = BITSET_FFS(table->unused_entries);
   if (!index--)
      return -1;

   assert(index >= PVR_BORDER_COLOR_TABLE_NR_BUILTIN_ENTRIES);

   BITSET_CLEAR(table->unused_entries, index);

   return index;
}

static void
pvr_border_color_table_free_entry(struct pvr_border_color_table *const table,
                                  const uint32_t index)
{
   assert(pvr_border_color_table_is_index_valid(table, index));
   BITSET_SET(table->unused_entries, index);
}

/** Attempt to invert a swizzle.
 *
 * If @param swz contains multiple channels with the same swizzle, this
 * operation will fail and return false. The @param dst should be preloaded
 * with suitable defaults (@var PIPE_SWIZZLE_0 or @var PIPE_SWIZZLE_1) for
 * channels with no source.
 *
 * For a given swizzle S, this function produces an inverse swizzle S' such
 * that for a given input color C:
 *
 *    C * S => C'
 *    C' * S' => C"
 *
 * The unswizzled color C" is a subset of the input color C, where channels not
 * contained in C' (because they weren't included as outputs in S) are set to
 * the defaults specified in S' as described above.
 *
 * @param swz The swizzle to invert
 * @param dst Output
 * @return true if the swizzle is invertible and the operation succeeded.
 */
static bool pvr_invert_swizzle(const unsigned char swz[4], unsigned char dst[4])
{
   bool found[4] = { false };
   unsigned i, c;

   for (i = 0; i < 4; i++) {
      c = swz[i];

      if (c > PIPE_SWIZZLE_W)
         continue;

      if (found[c])
         return false;

      dst[c] = i;
      found[c] = true;
   }

   return true;
}

static inline void pvr_border_color_swizzle_to_tex_format(
   union pipe_color_union *const color,
   const VkFormat vk_format,
   const struct pvr_tex_format_description *const pvr_tex_fmt_desc,
   bool is_int)
{
   const enum pipe_format tex_pipe_format =
      is_int ? pvr_tex_fmt_desc->pipe_format_int
             : pvr_tex_fmt_desc->pipe_format_float;

   const enum pipe_format color_format = vk_format_to_pipe_format(vk_format);
   const struct util_format_description *const color_format_desc =
      util_format_description(color_format);
   const struct util_format_description *const tex_format_desc =
      util_format_description(tex_pipe_format);

   union pipe_color_union swizzled_color;
   unsigned char composed_swizzle[4];
   unsigned char color_unswizzle[4] = {
      PIPE_SWIZZLE_0,
      PIPE_SWIZZLE_0,
      PIPE_SWIZZLE_0,
      PIPE_SWIZZLE_1,
   };
   const unsigned char *tpu_swizzle;

   ASSERTED bool invert_succeeded;

   if (color_format_desc->format == tex_pipe_format)
      return;

   /* Some format pairs (e.g. UNORM vs SRGB) fail the above test but still don't
    * require a re-swizzle.
    */
   if (memcmp(color_format_desc->swizzle,
              tex_format_desc->swizzle,
              sizeof(color_format_desc->swizzle)) == 0) {
      return;
   }

   mesa_logd("Mismatched border pipe formats: vk=%s, tex=%s",
             color_format_desc->short_name,
             tex_format_desc->short_name);

   tpu_swizzle = pvr_get_format_swizzle_for_tpu(color_format_desc);

   /* Any supported format for which this operation is necessary must have an
    * invertible swizzle.
    */
   invert_succeeded = pvr_invert_swizzle(tpu_swizzle, color_unswizzle);
   assert(invert_succeeded);

   util_format_compose_swizzles(color_unswizzle,
                                tex_format_desc->swizzle,
                                composed_swizzle);

   mesa_logd("Applying swizzle: %u%u%u%u",
             composed_swizzle[0],
             composed_swizzle[1],
             composed_swizzle[2],
             composed_swizzle[3]);

   util_format_apply_color_swizzle(&swizzled_color,
                                   color,
                                   composed_swizzle,
                                   is_int);

   *color = swizzled_color;
}

static void
pvr_border_color_table_fill_entry(struct pvr_border_color_table *const table,
                                  const uint32_t index,
                                  const VkFormat vk_format,
                                  const union pipe_color_union *const color,
                                  const bool is_int,
                                  const struct pvr_device_info *const dev_info)
{
   struct pvr_border_color_table_entry *const entries = table->table->bo->map;
   struct pvr_border_color_table_entry *const entry = &entries[index];

   for (enum ROGUE_TEXSTATE_FORMAT tex_format = 0;
        tex_format < PVR_TEX_FORMAT_COUNT;
        tex_format++) {
      if (tex_format_is_supported(tex_format)) {
         const struct pvr_tex_format_description *const pvr_tex_fmt_desc =
            get_tex_format_description(tex_format);
         union pipe_color_union swizzled_color = *color;

         if (vk_format_is_color(vk_format)) {
            pvr_border_color_swizzle_to_tex_format(&swizzled_color,
                                                   vk_format,
                                                   pvr_tex_fmt_desc,
                                                   is_int);
         }

         pvr_border_color_table_pack_single(
            &entry->values[tex_format],
            &swizzled_color,
            pvr_tex_fmt_desc,
            is_int,
            dev_info);
      }
   }

   for (enum ROGUE_TEXSTATE_FORMAT_COMPRESSED tex_format = 0;
        tex_format < PVR_TEX_FORMAT_COUNT;
        tex_format++) {
      if (tex_format_compressed_is_supported(tex_format))
         pvr_border_color_table_pack_single_compressed(
            &entry->compressed_values[tex_format],
            color,
            get_tex_format_compressed_description(tex_format),
            is_int,
            dev_info);
   }
}

VkResult pvr_arch_border_color_table_init(struct pvr_device *const device)
{
   struct pvr_border_color_table *table = device->border_color_table =
      vk_zalloc(&device->vk.alloc,
                sizeof(struct pvr_border_color_table),
                8,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!table)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   const struct pvr_device_info *const dev_info = &device->pdevice->dev_info;
   const uint32_t cache_line_size = pvr_get_slc_cache_line_size(dev_info);
   const uint32_t table_size = sizeof(struct pvr_border_color_table_entry) *
                               PVR_BORDER_COLOR_TABLE_NR_ENTRIES;

   VkResult result;

   /* Initialize to ones so ffs can be used to find unused entries. */
   BITSET_ONES(table->unused_entries);

   result = pvr_bo_alloc(device,
                         device->heaps.general_heap,
                         table_size,
                         cache_line_size,
                         PVR_BO_ALLOC_FLAG_CPU_MAPPED,
                         &table->table);
   if (result != VK_SUCCESS)
      goto err_out;

   BITSET_CLEAR_RANGE_INSIDE_WORD(table->unused_entries,
                                  0,
                                  PVR_BORDER_COLOR_TABLE_NR_BUILTIN_ENTRIES -
                                     1);

   for (uint32_t i = 0; i < PVR_BORDER_COLOR_TABLE_NR_BUILTIN_ENTRIES; i++) {
      const VkClearColorValue color = vk_border_color_value(i);
      const bool is_int = vk_border_color_is_int(i);

      pvr_border_color_table_fill_entry(table,
                                        i,
                                        VK_FORMAT_UNDEFINED,
                                        (const union pipe_color_union *)&color,
                                        is_int,
                                        dev_info);
   }

   pvr_bo_cpu_unmap(device, table->table);

   return VK_SUCCESS;

err_out:
   vk_free(&device->vk.alloc, table);

   return result;
}

void pvr_arch_border_color_table_finish(struct pvr_device *const device)
{
#if MESA_DEBUG
   BITSET_SET_RANGE_INSIDE_WORD(device->border_color_table->unused_entries,
                                0,
                                PVR_BORDER_COLOR_TABLE_NR_BUILTIN_ENTRIES - 1);
   BITSET_NOT(device->border_color_table->unused_entries);
   assert(BITSET_IS_EMPTY(device->border_color_table->unused_entries));
#endif

   pvr_bo_free(device, device->border_color_table->table);
   vk_free(&device->vk.alloc, device->border_color_table);
}

static VkResult pvr_border_color_table_create_custom_entry(
   struct pvr_device *const device,
   const struct pvr_sampler *const sampler,
   struct pvr_border_color_table *const table,
   uint32_t *const index_out)
{
   const bool is_int = vk_border_color_is_int(sampler->vk.border_color);
   const VkClearColorValue color = sampler->vk.border_color_value;
   const VkFormat vk_format = sampler->vk.format;
   const bool map_table = !table->table->bo->map;
   VkResult result;
   int32_t index;

   assert(vk_format != VK_FORMAT_UNDEFINED);

   index = pvr_border_color_table_alloc_entry(table);
   if (index < 0)
      goto err_out;

   if (map_table) {
      result = pvr_bo_cpu_map_unchanged(device, table->table);
      if (result != VK_SUCCESS)
         goto err_free_entry;
   }

   pvr_border_color_table_fill_entry(
      table,
      index,
      vk_format,
      (const union pipe_color_union *)&color,
      is_int,
      &device->pdevice->dev_info);

   if (map_table)
      pvr_bo_cpu_unmap(device, table->table);

   *index_out = index;

   return VK_SUCCESS;

err_free_entry:
   pvr_border_color_table_free_entry(table, index);

err_out:
   return vk_errorf(sampler,
                    VK_ERROR_OUT_OF_DEVICE_MEMORY,
                    "Failed to allocate border color table entry");
}

VkResult pvr_arch_border_color_table_get_or_create_entry(
   struct pvr_device *const device,
   const struct pvr_sampler *const sampler,
   struct pvr_border_color_table *const table,
   uint32_t *const index_out)
{
   const VkBorderColor vk_type = sampler->vk.border_color;

   if (vk_type <= PVR_BORDER_COLOR_TABLE_NR_BUILTIN_ENTRIES) {
      *index_out = vk_type;
      return VK_SUCCESS;
   }

   return pvr_border_color_table_create_custom_entry(device,
                                                     sampler,
                                                     table,
                                                     index_out);
}

void pvr_arch_border_color_table_release_entry(
   struct pvr_border_color_table *const table,
   const uint32_t index)
{
   if (index < PVR_BORDER_COLOR_TABLE_NR_BUILTIN_ENTRIES)
      return;

   pvr_border_color_table_free_entry(table, index);
}
