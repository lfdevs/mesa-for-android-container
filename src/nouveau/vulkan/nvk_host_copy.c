/*
 * Copyright © 2024 Valve Corp.
 * SPDX-License-Identifier: MIT
 */

#include "nvk_device.h"
#include "nvk_device_memory.h"
#include "nvk_entrypoints.h"
#include "nvk_format.h"
#include "nvk_image.h"

#include "vk_format.h"

static struct nil_Offset4D_Pixels
vk_to_nil_offset(const struct vk_image *image, VkOffset3D offset,
                 uint32_t base_array_layer)
{
   const VkOffset3D sanitized_offset =
      vk_image_sanitize_offset(image, offset);
   return (struct nil_Offset4D_Pixels) {
      .x = sanitized_offset.x,
      .y = sanitized_offset.y,
      .z = sanitized_offset.z,
      .a = base_array_layer
   };
}

static struct nil_Extent4D_Pixels
vk_to_nil_extent(const struct vk_image *image, VkExtent3D extent,
                 uint32_t array_layers)
{
   const VkExtent3D sanitized_extent =
      vk_image_sanitize_extent(image, extent);
   return (struct nil_Extent4D_Pixels) {
      .width      = sanitized_extent.width,
      .height     = sanitized_extent.height,
      .depth      = sanitized_extent.depth,
      .array_len  = array_layers,
   };
}

static void
memcpy2d(void *dst, size_t dst_stride_B,
         const void *src, size_t src_stride_B,
         size_t width_B, size_t height)
{
   if (dst_stride_B == width_B && src_stride_B == width_B) {
      memcpy(dst, src, width_B * height);
   } else {
      for (uint32_t y = 0; y < height; y++) {
         memcpy(dst, src, width_B);
         dst += dst_stride_B;
         src += src_stride_B;
      }
   }
}

static VkResult
nvk_image_plane_map(const struct nvk_image_plane *plane,
                    enum nvkmd_mem_map_flags map_flags,
                    void **map_out)
{
   struct nvk_device_memory *host_mem = plane->host_mem;
   VkResult result;

   result = nvkmd_mem_map(host_mem->mem, &host_mem->vk.base,
                          map_flags, NULL, map_out);
   if (result != VK_SUCCESS)
      return result;

   *map_out += plane->host_offset;

   return VK_SUCCESS;
}

static void
nvk_image_plane_unmap(const struct nvk_image_plane *plane)
{
   nvkmd_mem_unmap(plane->host_mem->mem, 0);
}

static VkResult
nvk_copy_memory_to_image(struct nvk_image *dst,
                         const VkMemoryToImageCopyEXT *info,
                         bool use_memcpy)
{
   VkResult result;

   struct vk_image_buffer_layout buffer_layout =
      vk_memory_to_image_copy_layout(&dst->vk, info);

   const VkImageAspectFlagBits aspects = info->imageSubresource.aspectMask;
   const uint8_t plane = nvk_image_aspects_to_plane(dst, aspects);
   const struct nvk_image_plane *dst_plane = &dst->planes[plane];

   const uint32_t layer_count =
      vk_image_subresource_layer_count(&dst->vk, &info->imageSubresource);
   const struct nil_Extent4D_Pixels extent_px =
      vk_to_nil_extent(&dst->vk, info->imageExtent, layer_count);
   const struct nil_Extent4D_Bytes extent_B =
      nil_extent4d_px_to_B(extent_px, dst_plane->nil.format,
                           dst_plane->nil.sample_layout);

   const struct nil_Offset4D_Pixels offset_px =
      vk_to_nil_offset(&dst->vk, info->imageOffset,
                       info->imageSubresource.baseArrayLayer);
   struct nil_Offset4D_Bytes offset_B =
      nil_offset4d_px_to_B(offset_px, dst_plane->nil.format,
                           dst_plane->nil.sample_layout);

   const uint32_t dst_miplevel = info->imageSubresource.mipLevel;
   const struct nil_image_level *dst_level =
      &dst_plane->nil.levels[dst_miplevel];

   const void *src_ptr = info->pHostPointer;

   void *dst_ptr;
   result = nvk_image_plane_map(dst_plane, NVKMD_MEM_MAP_WR, &dst_ptr);
   if (result != VK_SUCCESS)
      return result;

   /* Take into account the miplevel and array layer */
   dst_ptr += dst_level->offset_B;
   dst_ptr += offset_B.a * dst_plane->nil.array_stride_B;
   offset_B.a = 0;

   if (use_memcpy) {
      const uint64_t layer_size_B =
         nil_image_level_layer_size_B(&dst_plane->nil, dst_miplevel);
      for (unsigned a = 0; a < layer_count; a++) {
         memcpy(dst_ptr, src_ptr, layer_size_B);

         src_ptr += layer_size_B;
         dst_ptr += dst_plane->nil.array_stride_B;
      }
   } else if (dst_level->tiling.gob_type == NIL_GOB_TYPE_LINEAR) {
      assert(layer_count == 1);
      memcpy2d(dst_ptr + offset_B.x + offset_B.y * dst_level->row_stride_B,
               dst_level->row_stride_B,
               src_ptr,
               buffer_layout.row_stride_B,
               extent_B.width,
               extent_B.height);
   } else {
      const struct nil_Extent4D_Pixels level_extent_px =
         nil_image_level_extent_px(&dst_plane->nil, dst_miplevel);
      struct nil_Extent4D_Bytes level_extent_B =
         nil_extent4d_px_to_B(level_extent_px, dst_plane->nil.format,
                              dst_plane->nil.sample_layout);
      level_extent_B.array_len = 1;

      /* The copy works one array layer at a time */
      assert(offset_B.a == 0);
      struct nil_Extent4D_Bytes copy_extent_B = extent_B;
      copy_extent_B.array_len = 1;

      for (unsigned a = 0; a < layer_count; a++) {
         nil_copy_linear_to_tiled(dst_ptr,
                                  level_extent_B,
                                  src_ptr,
                                  buffer_layout.row_stride_B,
                                  buffer_layout.image_stride_B,
                                  offset_B,
                                  copy_extent_B,
                                  &dst_level->tiling);

         src_ptr += buffer_layout.image_stride_B;
         dst_ptr += dst_plane->nil.array_stride_B;
      }
   }

   nvk_image_plane_unmap(dst_plane);

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
nvk_CopyMemoryToImageEXT(VkDevice _device,
                         const VkCopyMemoryToImageInfoEXT *info)
{
   VK_FROM_HANDLE(nvk_image, dst_image, info->dstImage);

   VkResult result;

   /* From the EXT spec:
    * VK_HOST_IMAGE_COPY_MEMCPY_EXT specifies that no memory layout swizzling is
    * to be applied during data copy. For copies between memory and images, this
    * flag indicates that image data in host memory is swizzled in exactly the
    * same way as the image data on the device. Using this flag indicates that
    * the implementations may use a simple memory copy to transfer the data
    * between the host memory and the device memory. The format of the swizzled
    * data in host memory is platform dependent and is not defined in this
    * specification.
    */
   const bool use_memcpy = info->flags &
      VK_HOST_IMAGE_COPY_MEMCPY_EXT;

   for (unsigned r = 0; r < info->regionCount; r++) {
      result = nvk_copy_memory_to_image(dst_image, &info->pRegions[r],
                                        use_memcpy);
      if (result != VK_SUCCESS)
         return result;
   }

   return result;
}

static VkResult
nvk_copy_image_to_memory(struct nvk_image *src,
                         const VkImageToMemoryCopyEXT *info,
                         bool use_memcpy)
{
   VkResult result;

   struct vk_image_buffer_layout buffer_layout =
      vk_image_to_memory_copy_layout(&src->vk, info);

   const VkImageAspectFlagBits aspects = info->imageSubresource.aspectMask;
   const uint8_t plane = nvk_image_aspects_to_plane(src, aspects);
   struct nvk_image_plane *src_plane = &src->planes[plane];

   const uint32_t layer_count =
      vk_image_subresource_layer_count(&src->vk, &info->imageSubresource);
   const struct nil_Extent4D_Pixels extent_px =
      vk_to_nil_extent(&src->vk, info->imageExtent, layer_count);
   const struct nil_Extent4D_Bytes extent_B =
      nil_extent4d_px_to_B(extent_px, src_plane->nil.format,
                           src_plane->nil.sample_layout);

   const struct nil_Offset4D_Pixels offset_px =
      vk_to_nil_offset(&src->vk, info->imageOffset,
                       info->imageSubresource.baseArrayLayer);
   struct nil_Offset4D_Bytes offset_B =
      nil_offset4d_px_to_B(offset_px, src_plane->nil.format,
                           src_plane->nil.sample_layout);

   const uint32_t src_miplevel = info->imageSubresource.mipLevel;
   const struct nil_image_level *src_level =
      &src_plane->nil.levels[src_miplevel];

   void *dst_ptr = info->pHostPointer;

   const void *src_ptr;
   result = nvk_image_plane_map(src_plane, NVKMD_MEM_MAP_RD, (void **)&src_ptr);
   if (result != VK_SUCCESS)
      return result;

   /* Take into account the miplevel and array layer */
   src_ptr += src_level->offset_B;
   src_ptr += offset_B.a * src_plane->nil.array_stride_B;
   offset_B.a = 0;

   if (use_memcpy) {
      const uint64_t layer_size_B =
         nil_image_level_layer_size_B(&src_plane->nil, src_miplevel);
      for (unsigned a = 0; a < layer_count; a++) {
         memcpy(dst_ptr, src_ptr, layer_size_B);

         src_ptr += src_plane->nil.array_stride_B;
         dst_ptr += layer_size_B;
      }
   } else if (src_level->tiling.gob_type == NIL_GOB_TYPE_LINEAR) {
      assert(layer_count == 1);
      memcpy2d(dst_ptr,
               buffer_layout.row_stride_B,
               src_ptr + offset_B.x + offset_B.y * src_level->row_stride_B,
               src_level->row_stride_B,
               extent_B.width,
               extent_B.height);
   } else {
      const struct nil_Extent4D_Pixels level_extent_px =
         nil_image_level_extent_px(&src_plane->nil, src_miplevel);
      struct nil_Extent4D_Bytes level_extent_B =
         nil_extent4d_px_to_B(level_extent_px, src_plane->nil.format,
                              src_plane->nil.sample_layout);
      level_extent_B.array_len = 1;

      /* The copy works one array layer at a time */
      assert(offset_B.a == 0);
      struct nil_Extent4D_Bytes copy_extent_B = extent_B;
      copy_extent_B.array_len = 1;

      for (unsigned a = 0; a < layer_count; a++) {
         nil_copy_tiled_to_linear(dst_ptr,
                                  buffer_layout.row_stride_B,
                                  buffer_layout.image_stride_B,
                                  src_ptr,
                                  level_extent_B,
                                  offset_B,
                                  copy_extent_B,
                                  &src_level->tiling);

         src_ptr += src_plane->nil.array_stride_B;
         dst_ptr += buffer_layout.image_stride_B;
      }
   }

   nvk_image_plane_unmap(src_plane);

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
nvk_CopyImageToMemoryEXT(VkDevice _device,
                         const VkCopyImageToMemoryInfoEXT *info)
{
   VK_FROM_HANDLE(nvk_image, image, info->srcImage);

   VkResult result;

   const bool use_memcpy = info->flags &
      VK_HOST_IMAGE_COPY_MEMCPY_EXT;

   for (unsigned r = 0; r < info->regionCount; r++) {
      result = nvk_copy_image_to_memory(image, &info->pRegions[r],
                                        use_memcpy);
      if (result != VK_SUCCESS)
         return result;
   }

   return result;
}

static VkResult
nvk_copy_image_to_image(struct nvk_device *device,
                        struct nvk_image *src,
                        struct nvk_image *dst,
                        const VkImageCopy2 *info)
{
   VkResult result;

   const VkImageAspectFlagBits src_aspects =
      info->srcSubresource.aspectMask;
   const uint8_t src_plane = nvk_image_aspects_to_plane(src, src_aspects);
   struct nvk_image_plane *src_img_plane = &src->planes[src_plane];

   const VkImageAspectFlagBits dst_aspects =
      info->dstSubresource.aspectMask;
   const uint8_t dst_plane = nvk_image_aspects_to_plane(dst, dst_aspects);
   struct nvk_image_plane *dst_img_plane = &dst->planes[dst_plane];

   /* From the Vulkan 1.3.217 spec:
    *
    *    "When copying between compressed and uncompressed formats the
    *    extent members represent the texel dimensions of the source image
    *    and not the destination."
    */
   const uint32_t src_layer_count =
      vk_image_subresource_layer_count(&src->vk, &info->srcSubresource);
   const struct nil_Extent4D_Pixels src_extent_px =
      vk_to_nil_extent(&src->vk, info->extent, src_layer_count);
   struct nil_Extent4D_Bytes src_extent_B =
      nil_extent4d_px_to_B(src_extent_px, src_img_plane->nil.format,
                           src_img_plane->nil.sample_layout);

   const struct nil_Offset4D_Pixels src_offset_px =
      vk_to_nil_offset(&src->vk, info->srcOffset,
                       info->srcSubresource.baseArrayLayer);
   struct nil_Offset4D_Bytes src_offset_B =
      nil_offset4d_px_to_B(src_offset_px, src_img_plane->nil.format,
                           src_img_plane->nil.sample_layout);

   const uint32_t dst_layer_count =
      vk_image_subresource_layer_count(&dst->vk, &info->dstSubresource);
   const struct nil_Extent4D_Pixels dst_extent_px =
      vk_to_nil_extent(&dst->vk, info->extent, dst_layer_count);
   // The source format is here in case of compressed images (see comment above)
   struct nil_Extent4D_Bytes dst_extent_B =
      nil_extent4d_px_to_B(dst_extent_px, src_img_plane->nil.format,
                           dst_img_plane->nil.sample_layout);

   const struct nil_Offset4D_Pixels dst_offset_px =
      vk_to_nil_offset(&dst->vk, info->dstOffset,
                       info->dstSubresource.baseArrayLayer);
   struct nil_Offset4D_Bytes dst_offset_B =
      nil_offset4d_px_to_B(dst_offset_px, dst_img_plane->nil.format,
                           dst_img_plane->nil.sample_layout);

   const uint32_t src_miplevel = info->srcSubresource.mipLevel;
   const struct nil_image_level *src_level =
      &src_img_plane->nil.levels[src_miplevel];

   const uint32_t dst_miplevel = info->dstSubresource.mipLevel;
   const struct nil_image_level *dst_level =
      &dst_img_plane->nil.levels[dst_miplevel];

   const void *src_ptr;
   result = nvk_image_plane_map(src_img_plane, NVKMD_MEM_MAP_RD,
                                (void **)&src_ptr);
   if (result != VK_SUCCESS)
      return result;

   /* Take into account the miplevel and array layer */
   src_ptr += src_level->offset_B;
   src_ptr += src_offset_B.a * src_img_plane->nil.array_stride_B;
   src_offset_B.a = 0;

   void *dst_ptr;
   result = nvk_image_plane_map(dst_img_plane, NVKMD_MEM_MAP_WR, &dst_ptr);
   if (result != VK_SUCCESS)
      return result;

   /* Take into account the miplevel and array layer */
   dst_ptr += dst_level->offset_B;
   dst_ptr += dst_offset_B.a * dst_img_plane->nil.array_stride_B;
   dst_offset_B.a = 0;

   if (src_level->tiling.gob_type == NIL_GOB_TYPE_LINEAR) {
      assert(src_img_plane->nil.dim == NIL_IMAGE_DIM_2D);
      assert(src_img_plane->nil.extent_px.array_len == 1);
      assert(src_extent_px.depth == 1 && src_extent_px.array_len == 1);
   }

   if (dst_level->tiling.gob_type == NIL_GOB_TYPE_LINEAR) {
      assert(dst_img_plane->nil.dim == NIL_IMAGE_DIM_2D);
      assert(dst_img_plane->nil.extent_px.array_len == 1);
      assert(dst_extent_px.depth == 1 && dst_extent_px.array_len == 1);
   }

   if (src_level->tiling.gob_type == NIL_GOB_TYPE_LINEAR &&
       dst_level->tiling.gob_type == NIL_GOB_TYPE_LINEAR) {
      memcpy2d(dst_ptr + dst_offset_B.x +
                  dst_offset_B.y * dst_level->row_stride_B,
               dst_level->row_stride_B,
               src_ptr + src_offset_B.x +
                  src_offset_B.y * src_level->row_stride_B,
               src_level->row_stride_B,
               src_extent_B.width,
               src_extent_B.height);
   } else if (src_level->tiling.gob_type == NIL_GOB_TYPE_LINEAR) {
      const struct nil_Extent4D_Pixels dst_level_extent_px =
         nil_image_level_extent_px(&dst_img_plane->nil, dst_miplevel);
      const struct nil_Extent4D_Bytes dst_level_extent_B =
         nil_extent4d_px_to_B(dst_level_extent_px, dst_img_plane->nil.format,
                              dst_img_plane->nil.sample_layout);

      nil_copy_linear_to_tiled(dst_ptr,
                               dst_level_extent_B,
                               src_ptr + src_offset_B.x +
                                  src_offset_B.y * src_level->row_stride_B,
                               src_level->row_stride_B,
                               0, /* No array slices */
                               dst_offset_B,
                               dst_extent_B,
                               &dst_level->tiling);
   } else if (dst_level->tiling.gob_type == NIL_GOB_TYPE_LINEAR) {
      const struct nil_Extent4D_Pixels src_level_extent_px =
         nil_image_level_extent_px(&src_img_plane->nil, src_miplevel);
      const struct nil_Extent4D_Bytes src_level_extent_B =
         nil_extent4d_px_to_B(src_level_extent_px, src_img_plane->nil.format,
                              src_img_plane->nil.sample_layout);

      nil_copy_tiled_to_linear(dst_ptr + dst_offset_B.x +
                                 dst_offset_B.y * dst_level->row_stride_B,
                               dst_level->row_stride_B,
                               0, /* No array slices */
                               src_ptr,
                               src_level_extent_B,
                               src_offset_B,
                               src_extent_B,
                               &src_level->tiling);
   } else {
      assert(src_extent_B.width == dst_extent_B.width);
      assert(src_extent_B.height == dst_extent_B.height);
      const uint32_t tmp_row_stride_B = src_extent_B.width;
      const uint32_t tmp_layer_stride_B = src_extent_B.width *
                                          src_extent_B.height;

      assert(src_extent_B.depth * src_extent_B.array_len ==
             dst_extent_B.depth * dst_extent_B.array_len);
      const size_t tmp_size_B =
         src_extent_B.depth * src_extent_B.array_len * tmp_layer_stride_B;
      void *tmp_mem = vk_alloc(&device->vk.alloc, tmp_size_B, 8,
                               VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);

      const struct nil_Extent4D_Pixels src_level_extent_px =
         nil_image_level_extent_px(&src_img_plane->nil, src_miplevel);
      struct nil_Extent4D_Bytes src_level_extent_B =
         nil_extent4d_px_to_B(src_level_extent_px, src_img_plane->nil.format,
                              src_img_plane->nil.sample_layout);
      src_level_extent_B.array_len = 1;

      /* The copy works one array layer at a time */
      assert(src_offset_B.a == 0);
      struct nil_Extent4D_Bytes src_copy_extent_B = src_extent_B;
      src_copy_extent_B.array_len = 1;

      void *tmp_dst = tmp_mem;
      for (unsigned a = 0; a < src_layer_count; a++) {
         nil_copy_tiled_to_linear(tmp_dst,
                                  tmp_row_stride_B,
                                  tmp_layer_stride_B,
                                  src_ptr,
                                  src_level_extent_B,
                                  src_offset_B,
                                  src_copy_extent_B,
                                  &src_level->tiling);

         src_ptr += src_img_plane->nil.array_stride_B;
         tmp_dst += tmp_layer_stride_B;
      }

      const struct nil_Extent4D_Pixels dst_level_extent_px =
         nil_image_level_extent_px(&dst_img_plane->nil, dst_miplevel);
      struct nil_Extent4D_Bytes dst_level_extent_B =
         nil_extent4d_px_to_B(dst_level_extent_px, dst_img_plane->nil.format,
                              dst_img_plane->nil.sample_layout);
      dst_level_extent_B.array_len = 1;

      /* The copy works one array layer at a time */
      assert(dst_offset_B.a == 0);
      struct nil_Extent4D_Bytes dst_copy_extent_B = dst_extent_B;
      dst_copy_extent_B.array_len = 1;

      void *tmp_src = tmp_mem;
      for (unsigned a = 0; a < dst_layer_count; a++) {
         nil_copy_linear_to_tiled(dst_ptr,
                                  dst_level_extent_B,
                                  tmp_src,
                                  tmp_row_stride_B,
                                  tmp_layer_stride_B,
                                  dst_offset_B,
                                  dst_copy_extent_B,
                                  &dst_level->tiling);

         tmp_src += tmp_layer_stride_B;
         dst_ptr += dst_img_plane->nil.array_stride_B;
      }

      vk_free(&device->vk.alloc, tmp_mem);
   }

   nvk_image_plane_unmap(src_img_plane);
   nvk_image_plane_unmap(dst_img_plane);

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
nvk_CopyImageToImageEXT(VkDevice _device,
                        const VkCopyImageToImageInfoEXT *pCopyImageToImageInfo)
{
   VK_FROM_HANDLE(nvk_device, device, _device);
   VK_FROM_HANDLE(nvk_image, src, pCopyImageToImageInfo->srcImage);
   VK_FROM_HANDLE(nvk_image, dst, pCopyImageToImageInfo->dstImage);

   VkResult result;

   for (unsigned r = 0; r < pCopyImageToImageInfo->regionCount; r++) {
      result = nvk_copy_image_to_image(device, src, dst,
                                       pCopyImageToImageInfo->pRegions + r);
      if (result != VK_SUCCESS)
         return result;
   }

   return result;
}


VKAPI_ATTR VkResult VKAPI_CALL
nvk_TransitionImageLayoutEXT(VkDevice device,
                             uint32_t transitionCount,
                             const VkHostImageLayoutTransitionInfoEXT *transitions)
{
   /* Nothing to do here */
   return VK_SUCCESS;
}
