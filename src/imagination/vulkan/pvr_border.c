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
         pvr_get_tex_format_description(pvr_tex_fmt_desc->tex_format_simple);

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

static void
pvr_border_color_table_fill_entry(struct pvr_border_color_table *const table,
                                  const uint32_t index,
                                  const union pipe_color_union *const color,
                                  const bool is_int,
                                  const struct pvr_device_info *const dev_info)
{
   struct pvr_border_color_table_entry *const entries = table->table->bo->map;
   struct pvr_border_color_table_entry *const entry = &entries[index];

   pvr_foreach_supported_tex_format (tex_format, desc) {
      pvr_border_color_table_pack_single(&entry->values[tex_format],
                                         color,
                                         desc,
                                         is_int,
                                         dev_info);
   }

   pvr_foreach_supported_tex_format_compressed (tex_format, desc) {
      pvr_border_color_table_pack_single_compressed(
         &entry->compressed_values[tex_format],
         color,
         desc,
         is_int,
         dev_info);
   }
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
   const enum pipe_format color_format,
   const struct pvr_tex_format_description *const pvr_tex_fmt_desc,
   bool is_int)
{
   const enum pipe_format tex_pipe_format =
      is_int ? pvr_tex_fmt_desc->pipe_format_int
             : pvr_tex_fmt_desc->pipe_format_float;

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

VkResult
pvr_border_color_table_init(struct pvr_device *const device)
{
   struct pvr_border_color_table *table = device->border_color_table =
      vk_zalloc(&device->vk.alloc, sizeof(struct pvr_border_color_table), 8,
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

void
pvr_border_color_table_finish(struct pvr_device *const device)
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

static inline void pvr_border_color_table_set_custom_entry(
   struct pvr_border_color_table *const table,
   const uint32_t index,
   const VkFormat vk_format,
   const union pipe_color_union *const color,
   const bool is_int,
   const struct pvr_device_info *const dev_info)
{
   struct pvr_border_color_table_entry *const entries = table->table->bo->map;
   struct pvr_border_color_table_entry *const entry = &entries[index];

   const enum pipe_format format = vk_format_to_pipe_format(vk_format);
   uint32_t tex_format = pvr_get_tex_format(vk_format);

   assert(tex_format != ROGUE_TEXSTATE_FORMAT_INVALID);

   if (util_format_is_compressed(format)) {
      const struct pvr_tex_format_compressed_description *const pvr_tex_fmt_desc =
         pvr_get_tex_format_compressed_description(tex_format);

      pvr_border_color_table_pack_single_compressed(
         &entry->compressed_values[tex_format],
         color,
         pvr_tex_fmt_desc,
         is_int,
         dev_info);
   } else {
      const struct pvr_tex_format_description *const pvr_tex_fmt_desc =
         pvr_get_tex_format_description(tex_format);
      union pipe_color_union swizzled_color = *color;

      if (util_format_is_depth_or_stencil(format)) {
         VkImageAspectFlags aspect_mask;

         if (is_int)
            aspect_mask = VK_IMAGE_ASPECT_STENCIL_BIT;
         else
            aspect_mask = VK_IMAGE_ASPECT_DEPTH_BIT;

         /* Write the border color entry at the index of the texture
          * format relative to the depth-only or stencil-only compoment
          * associated with this Vulkan format.
          */
         tex_format = pvr_get_tex_format_aspect(vk_format, aspect_mask);
         assert(tex_format != ROGUE_TEXSTATE_FORMAT_INVALID);
      }

      pvr_border_color_swizzle_to_tex_format(&swizzled_color,
                                             format,
                                             pvr_tex_fmt_desc,
                                             is_int);

      pvr_border_color_table_pack_single(&entry->values[tex_format],
                                         &swizzled_color,
                                         pvr_tex_fmt_desc,
                                         is_int,
                                         dev_info);
   }
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

   pvr_border_color_table_set_custom_entry(
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

VkResult pvr_border_color_table_get_or_create_entry(
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

void pvr_border_color_table_release_entry(
   struct pvr_border_color_table *const table,
   const uint32_t index)
{
   if (index < PVR_BORDER_COLOR_TABLE_NR_BUILTIN_ENTRIES)
      return;

   pvr_border_color_table_free_entry(table, index);
}
