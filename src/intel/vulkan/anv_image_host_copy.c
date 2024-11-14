/* Copyright © 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <stdbool.h>

#include "anv_private.h"
#include "util/u_cpu_detect.h"
#include "util/u_debug.h"
#include "vk_util.h"

static inline VkOffset3D
vk_offset3d_to_el(enum isl_format format, VkOffset3D offset)
{
   const struct isl_format_layout *fmt_layout =
      isl_format_get_layout(format);
   return (VkOffset3D) {
      .x = offset.x / fmt_layout->bw,
      .y = offset.y / fmt_layout->bh,
      .z = offset.z / fmt_layout->bd,
   };
}

static inline VkExtent3D
vk_extent3d_to_el(enum isl_format format, VkExtent3D extent)
{
   const struct isl_format_layout *fmt_layout =
      isl_format_get_layout(format);
   return (VkExtent3D) {
      .width  = DIV_ROUND_UP(extent.width,  fmt_layout->bw),
      .height = DIV_ROUND_UP(extent.height, fmt_layout->bh),
      .depth  = DIV_ROUND_UP(extent.depth,  fmt_layout->bd),
   };
}

static void
anv_memcpy_image_memory(struct anv_device *device,
                        const struct isl_surf *surf,
                        const struct anv_image_binding *binding,
                        uint64_t binding_offset,
                        void *mem_ptr,
                        uint32_t level,
                        uint32_t base_img_array_layer,
                        uint32_t base_img_z_offset_px,
                        uint32_t array_layer,
                        uint32_t z_offset_px,
                        bool mem_to_img)
{
   uint64_t start_tile_B, end_tile_B;
   isl_surf_get_image_range_B_tile(surf, level,
                                   base_img_array_layer,
                                   base_img_z_offset_px,
                                   &start_tile_B, &end_tile_B);
   uint32_t array_pitch_B = isl_surf_get_array_pitch(surf);

   uint32_t img_depth_or_layer = MAX2(base_img_array_layer + array_layer,
                                      base_img_z_offset_px + z_offset_px);
   uint32_t mem_depth_or_layer = MAX2(z_offset_px, array_layer);

   void *img_ptr = binding->host_map + binding->map_delta + binding_offset;
   if (mem_to_img) {
      memcpy(img_ptr + start_tile_B + img_depth_or_layer * array_pitch_B,
             mem_ptr + mem_depth_or_layer * array_pitch_B,
             end_tile_B - start_tile_B);
   } else {
      memcpy(mem_ptr + mem_depth_or_layer * array_pitch_B,
             img_ptr + start_tile_B + img_depth_or_layer * array_pitch_B,
             end_tile_B - start_tile_B);
   }
}

static void
get_image_offset_el(const struct isl_surf *surf, unsigned level, unsigned z,
                    uint32_t *out_x0_el, uint32_t *out_y0_el)
{
   ASSERTED uint32_t z0_el, a0_el;
   if (surf->dim == ISL_SURF_DIM_3D) {
      isl_surf_get_image_offset_el(surf, level, 0, z,
                                   out_x0_el, out_y0_el, &z0_el, &a0_el);
   } else {
      isl_surf_get_image_offset_el(surf, level, z, 0,
                                   out_x0_el, out_y0_el, &z0_el, &a0_el);
   }
   assert(z0_el == 0 && a0_el == 0);
}

/* Compute extent parameters for use with tiled_memcpy functions.
 * xs are in units of bytes and ys are in units of strides.
 */
static inline void
tile_extents(const struct isl_surf *surf,
             const VkOffset3D *offset_el,
             const VkExtent3D *extent_el,
             unsigned level, int z,
             uint32_t *x1_B, uint32_t *x2_B,
             uint32_t *y1_el, uint32_t *y2_el)
{
   const struct isl_format_layout *fmtl = isl_format_get_layout(surf->format);
   const unsigned cpp = fmtl->bpb / 8;

   /* z contains offset->z */
   assert (z >= offset_el->z);

   unsigned x0_el, y0_el;
   get_image_offset_el(surf, level, z, &x0_el, &y0_el);

   *x1_B = (offset_el->x + x0_el) * cpp;
   *y1_el = offset_el->y + y0_el;
   *x2_B = (offset_el->x + extent_el->width + x0_el) * cpp;
   *y2_el = offset_el->y + extent_el->height + y0_el;
}

static void
anv_copy_image_memory(struct anv_device *device,
                      const struct isl_surf *surf,
                      const struct anv_image_binding *binding,
                      uint64_t binding_offset,
                      void *mem_ptr,
                      uint64_t mem_row_pitch_B,
                      uint64_t mem_height_pitch_B,
                      const VkOffset3D *offset_el,
                      const VkExtent3D *extent_el,
                      uint32_t level,
                      uint32_t base_img_array_layer,
                      uint32_t base_img_z_offset_px,
                      uint32_t array_layer,
                      uint32_t z_offset_px,
                      bool mem_to_img)
{
   const struct isl_format_layout *fmt_layout =
      isl_format_get_layout(surf->format);
   const uint32_t bs = fmt_layout->bpb / 8;
   void *img_ptr = binding->host_map + binding->map_delta + binding_offset;

   uint64_t start_tile_B, end_tile_B;
   isl_surf_get_image_range_B_tile(surf, level,
                                   base_img_array_layer + array_layer,
                                   base_img_z_offset_px + z_offset_px,
                                   &start_tile_B, &end_tile_B);

#ifdef SUPPORT_INTEL_INTEGRATED_GPUS
   const bool need_invalidate_flush =
      (binding->address.bo->flags & ANV_BO_ALLOC_HOST_COHERENT) == 0 &&
      device->physical->memory.need_flush;
   if (need_invalidate_flush && !mem_to_img)
      intel_invalidate_range(img_ptr + start_tile_B, end_tile_B - start_tile_B);
#endif

   uint32_t img_depth_or_layer = MAX2(base_img_array_layer + array_layer,
                                      base_img_z_offset_px + z_offset_px);
   uint32_t mem_depth_or_layer = MAX2(z_offset_px, array_layer);

   if (surf->tiling == ISL_TILING_LINEAR) {
      uint64_t img_col_offset = offset_el->x * bs;
      uint64_t row_copy_size = extent_el->width * bs;
      for (uint32_t h_el = 0; h_el < extent_el->height; h_el++) {
         uint64_t mem_row_offset =
            mem_height_pitch_B * mem_depth_or_layer +
            h_el * mem_row_pitch_B;
         uint64_t img_row = h_el + offset_el->y;
         uint64_t img_offset =
            start_tile_B + img_row * surf->row_pitch_B + img_col_offset;
         assert((img_offset + row_copy_size) <= binding->memory_range.size);

         if (mem_to_img)
            memcpy(img_ptr + img_offset, mem_ptr + mem_row_offset, row_copy_size);
         else
            memcpy(mem_ptr + mem_row_offset, img_ptr + img_offset, row_copy_size);
      }
   } else {
      uint32_t x1, x2, y1, y2;
      tile_extents(surf, offset_el, extent_el, level, img_depth_or_layer,
                   &x1, &x2, &y1, &y2);

      if (mem_to_img) {
         isl_memcpy_linear_to_tiled(x1, x2, y1, y2,
                                    img_ptr,
                                    mem_ptr + mem_height_pitch_B * mem_depth_or_layer,
                                    surf->row_pitch_B,
                                    mem_row_pitch_B,
                                    false,
                                    surf->tiling,
                                    ISL_MEMCPY);
      } else {
         isl_memcpy_tiled_to_linear(x1, x2, y1, y2,
                                    mem_ptr + mem_height_pitch_B * mem_depth_or_layer,
                                    img_ptr,
                                    mem_row_pitch_B,
                                    surf->row_pitch_B,
                                    false,
                                    surf->tiling,
#if defined(USE_SSE41)
                                    util_get_cpu_caps()->has_sse4_1 ?
                                    ISL_MEMCPY_STREAMING_LOAD :
#endif
                                    ISL_MEMCPY);
      }
   }

#ifdef SUPPORT_INTEL_INTEGRATED_GPUS
   if (need_invalidate_flush && mem_to_img)
      intel_flush_range(img_ptr + start_tile_B, end_tile_B - start_tile_B);
#endif
}

static uint64_t
calc_mem_row_pitch_B(const struct isl_surf *surf,
                     uint64_t api_row_length_px,
                     const VkExtent3D *extent_px)
{
   const struct isl_format_layout *fmt_layout =
      isl_format_get_layout(surf->format);
   const uint32_t bs = fmt_layout->bpb / 8;

   return api_row_length_px != 0 ?
      (bs * DIV_ROUND_UP(api_row_length_px, fmt_layout->bw)) :
      (bs * DIV_ROUND_UP(extent_px->width, fmt_layout->bw));
}

static uint64_t
calc_mem_height_pitch_B(const struct isl_surf *surf,
                        uint64_t row_pitch_B,
                        uint64_t api_height_px,
                        const VkExtent3D *extent_px)
{
   const struct isl_format_layout *fmt_layout =
      isl_format_get_layout(surf->format);

   return api_height_px != 0 ?
      (row_pitch_B * DIV_ROUND_UP(api_height_px, fmt_layout->bh)) :
      (row_pitch_B * DIV_ROUND_UP(extent_px->height, fmt_layout->bh));
}

VkResult
anv_CopyMemoryToImageEXT(
    VkDevice                                    _device,
    const VkCopyMemoryToImageInfoEXT*           pCopyMemoryToImageInfo)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_image, image, pCopyMemoryToImageInfo->dstImage);

   for (uint32_t r = 0; r < pCopyMemoryToImageInfo->regionCount; r++) {
      const VkMemoryToImageCopyEXT *region =
         &pCopyMemoryToImageInfo->pRegions[r];
      const uint32_t plane =
         anv_image_aspect_to_plane(image, region->imageSubresource.aspectMask);
      const struct anv_surface *anv_surf =
         &image->planes[plane].primary_surface;
      const struct isl_surf *surf = &anv_surf->isl;
      const struct anv_image_binding *binding =
         &image->bindings[anv_surf->memory_range.binding];

      assert(binding->host_map != NULL);

      /* Memory distance between each row */
      uint64_t mem_row_pitch_B =
         calc_mem_row_pitch_B(surf, region->memoryRowLength,
                              &region->imageExtent);
      /* Memory distance between each slice (1 3D level or 1 array layer) */
      uint64_t mem_height_pitch_B =
         calc_mem_height_pitch_B(surf, mem_row_pitch_B,
                                 region->memoryImageHeight,
                                 &region->imageExtent);

      VkOffset3D offset_el =
         vk_offset3d_to_el(surf->format, region->imageOffset);
      VkExtent3D extent_el =
         vk_extent3d_to_el(surf->format, region->imageExtent);

      for (uint32_t a = 0; a < region->imageSubresource.layerCount; a++) {
         for (uint32_t z = 0; z < region->imageExtent.depth; z++) {
            if ((pCopyMemoryToImageInfo->flags &
                 VK_HOST_IMAGE_COPY_MEMCPY_EXT) &&
                anv_image_can_host_memcpy(image)) {
               anv_memcpy_image_memory(device, surf, binding,
                                       anv_surf->memory_range.offset,
                                       (void *)region->pHostPointer,
                                       region->imageSubresource.mipLevel,
                                       region->imageSubresource.baseArrayLayer,
                                       region->imageOffset.z,
                                       a, z, true /* mem_to_img */);
            } else {
               anv_copy_image_memory(device, surf,
                                     binding, anv_surf->memory_range.offset,
                                     (void *)region->pHostPointer,
                                     mem_row_pitch_B,
                                     mem_height_pitch_B,
                                     &offset_el,
                                     &extent_el,
                                     region->imageSubresource.mipLevel,
                                     region->imageSubresource.baseArrayLayer,
                                     region->imageOffset.z,
                                     a, z, true /* mem_to_img */);
            }
         }
      }
   }

   return VK_SUCCESS;
}

VkResult
anv_CopyImageToMemoryEXT(
    VkDevice                                    _device,
    const VkCopyImageToMemoryInfoEXT*           pCopyImageToMemoryInfo)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_image, image, pCopyImageToMemoryInfo->srcImage);

   for (uint32_t r = 0; r < pCopyImageToMemoryInfo->regionCount; r++) {
      const VkImageToMemoryCopyEXT *region =
         &pCopyImageToMemoryInfo->pRegions[r];
      const uint32_t plane =
         anv_image_aspect_to_plane(image, region->imageSubresource.aspectMask);
      const struct anv_surface *anv_surf =
         &image->planes[plane].primary_surface;
      const struct isl_surf *surf = &anv_surf->isl;
      const struct anv_image_binding *binding =
         &image->bindings[anv_surf->memory_range.binding];

      assert(binding->host_map != NULL);

      VkOffset3D offset_el =
         vk_offset3d_to_el(surf->format, region->imageOffset);
      VkExtent3D extent_el =
         vk_extent3d_to_el(surf->format, region->imageExtent);

      /* Memory distance between each row */
      uint64_t mem_row_pitch_B =
         calc_mem_row_pitch_B(surf, region->memoryRowLength,
                              &region->imageExtent);
      /* Memory distance between each slice (1 3D level or 1 array layer) */
      uint64_t mem_height_pitch_B =
         calc_mem_height_pitch_B(surf, mem_row_pitch_B,
                                 region->memoryImageHeight,
                                 &region->imageExtent);

      for (uint32_t a = 0; a < region->imageSubresource.layerCount; a++) {
         for (uint32_t z = 0; z < region->imageExtent.depth; z++) {
            if ((pCopyImageToMemoryInfo->flags &
                 VK_HOST_IMAGE_COPY_MEMCPY_EXT) &&
                anv_image_can_host_memcpy(image)) {
               anv_memcpy_image_memory(device, surf, binding,
                                       anv_surf->memory_range.offset,
                                       region->pHostPointer,
                                       region->imageSubresource.mipLevel,
                                       region->imageSubresource.baseArrayLayer,
                                       region->imageOffset.z,
                                       a, z, false /* mem_to_img */);
            } else {
               anv_copy_image_memory(device, surf,
                                     binding, anv_surf->memory_range.offset,
                                     region->pHostPointer,
                                     mem_row_pitch_B,
                                     mem_height_pitch_B,
                                     &offset_el,
                                     &extent_el,
                                     region->imageSubresource.mipLevel,
                                     region->imageSubresource.baseArrayLayer,
                                     region->imageOffset.z,
                                     a, z, false /* mem_to_img */);
            }
         }
      }
   }

   return VK_SUCCESS;
}

VkResult
anv_CopyImageToImageEXT(
    VkDevice                                    _device,
    const VkCopyImageToImageInfoEXT*            pCopyImageToImageInfo)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_image, src_image, pCopyImageToImageInfo->srcImage);
   ANV_FROM_HANDLE(anv_image, dst_image, pCopyImageToImageInfo->dstImage);

   /* Work with a tile's worth of data */
   void *tmp_map = vk_alloc(&device->vk.alloc, 4096, 8,
                            VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (tmp_map == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   for (uint32_t r = 0; r < pCopyImageToImageInfo->regionCount; r++) {
      const VkImageCopy2 *region = &pCopyImageToImageInfo->pRegions[r];

      const uint32_t src_plane =
         anv_image_aspect_to_plane(src_image,
                                   region->srcSubresource.aspectMask);
      const uint32_t dst_plane =
         anv_image_aspect_to_plane(dst_image,
                                   region->srcSubresource.aspectMask);
      const struct anv_surface *src_anv_surf =
         &src_image->planes[src_plane].primary_surface;
      const struct anv_surface *dst_anv_surf =
         &dst_image->planes[dst_plane].primary_surface;
      const struct isl_surf *src_surf = &src_anv_surf->isl;
      const struct isl_surf *dst_surf = &dst_anv_surf->isl;
      const struct anv_image_binding *src_binding =
         &src_image->bindings[src_anv_surf->memory_range.binding];
      const struct anv_image_binding *dst_binding =
         &dst_image->bindings[dst_anv_surf->memory_range.binding];

      struct isl_tile_info src_tile;
      struct isl_tile_info dst_tile;

      isl_surf_get_tile_info(src_surf, &src_tile);
      isl_surf_get_tile_info(dst_surf, &dst_tile);

      uint32_t tile_width_B;
      uint32_t tile_width_el, tile_height_el;
      if (src_tile.phys_extent_B.w > dst_tile.phys_extent_B.w) {
         tile_width_B   = src_tile.phys_extent_B.w;
         tile_width_el  = src_tile.logical_extent_el.w;
         tile_height_el = src_tile.logical_extent_el.h;
      } else {
         tile_width_B   = dst_tile.phys_extent_B.w;
         tile_width_el  = dst_tile.logical_extent_el.w;
         tile_height_el = dst_tile.logical_extent_el.h;
      }

      /* There is no requirement that the extent be aligned to the texel block
       * size.
       */
      VkOffset3D src_offset_el =
         vk_offset3d_to_el(src_surf->format, region->srcOffset);
      VkOffset3D dst_offset_el =
         vk_offset3d_to_el(src_surf->format, region->dstOffset);
      VkExtent3D extent_el =
         vk_extent3d_to_el(src_surf->format, region->extent);

      /* linear-to-linear case */
      if (tile_width_el == 1 && tile_height_el == 1) {
         tile_width_el = MIN2(4096 / (src_tile.format_bpb / 8),
                              extent_el.width);
         tile_height_el = 4096 / (tile_width_el * (src_tile.format_bpb / 8));
         tile_width_B = tile_width_el * src_tile.format_bpb / 8;
      }

      for (uint32_t a = 0; a < region->srcSubresource.layerCount; a++) {
         for (uint32_t z = 0; z < region->extent.depth; z++) {
            for (uint32_t y_el = 0; y_el < extent_el.height; y_el += tile_height_el) {
               for (uint32_t x_el = 0; x_el < extent_el.width; x_el += tile_width_el) {
                  VkOffset3D src_offset = {
                     .x = src_offset_el.x + x_el,
                     .y = src_offset_el.y + y_el,
                  };
                  VkOffset3D dst_offset = {
                     .x = dst_offset_el.x + x_el,
                     .y = dst_offset_el.y + y_el,
                  };
                  VkExtent3D extent = {
                     .width  = MIN2(extent_el.width - x_el, tile_width_el),
                     .height = MIN2(extent_el.height - y_el, tile_height_el),
                     .depth  = 1,
                  };

                  anv_copy_image_memory(device, src_surf,
                                        src_binding,
                                        src_anv_surf->memory_range.offset,
                                        tmp_map,
                                        tile_width_B, 0,
                                        &src_offset, &extent,
                                        region->srcSubresource.mipLevel,
                                        region->srcSubresource.baseArrayLayer,
                                        region->srcOffset.z,
                                        a, z,
                                        false /* mem_to_img */);
                  anv_copy_image_memory(device, dst_surf,
                                        dst_binding,
                                        dst_anv_surf->memory_range.offset,
                                        tmp_map,
                                        tile_width_B, 0,
                                        &dst_offset, &extent,
                                        region->dstSubresource.mipLevel,
                                        region->dstSubresource.baseArrayLayer,
                                        region->dstOffset.z,
                                        a, z,
                                        true /* mem_to_img */);
               }
            }
         }
      }
   }

   vk_free(&device->vk.alloc, tmp_map);

   return VK_SUCCESS;
}

VkResult
anv_TransitionImageLayoutEXT(
    VkDevice                                    device,
    uint32_t                                    transitionCount,
    const VkHostImageLayoutTransitionInfoEXT*   pTransitions)
{
   /* Our layout transitions are mostly about resolving the auxiliary surface
    * into the main surface. Since we disable the auxiliary surface, there is
    * nothing here for us to do.
    */
   return VK_SUCCESS;
}
