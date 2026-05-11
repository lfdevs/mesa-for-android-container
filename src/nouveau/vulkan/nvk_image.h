/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#ifndef NVK_IMAGE_H
#define NVK_IMAGE_H 1

#include "nvk_private.h"
#include "nvk_device_memory.h"

#include "vk_image.h"

#include "nil.h"

/* Because small images can end up with an array_stride_B that is less than
 * the sparse block size (in bytes), we have to set SINGLE_MIPTAIL_BIT when
 * advertising sparse properties to the client.  This means that we get one
 * single memory range for the miptail of the image.  For large images with
 * mipTailStartLod > 0, we have to deal with the array stride ourselves.
 *
 * We do this by returning NVK_MIP_TAIL_START_OFFSET as the image's
 * imageMipTailOffset.  We can then detect anything with that address as
 * being part of the miptail and re-map it accordingly.  The Vulkan spec
 * explicitly allows for this.
 *
 * From the Vulkan 1.3.279 spec:
 *
 *    "When VK_SPARSE_MEMORY_BIND_METADATA_BIT is present, the resourceOffset
 *    must have been derived explicitly from the imageMipTailOffset in the
 *    sparse resource properties returned for the metadata aspect. By
 *    manipulating the value returned for imageMipTailOffset, the
 *    resourceOffset does not have to correlate directly to a device virtual
 *    address offset, and may instead be whatever value makes it easiest for
 *    the implementation to derive the correct device virtual address."
 */
#define NVK_MIP_TAIL_START_OFFSET 0x6d74000000000000UL

struct nvk_device_memory;
struct nvk_physical_device;
struct nvk_queue;
struct nvkmd_mem;
struct nvkmd_va;

VkFormatFeatureFlags2
nvk_get_image_format_features(const struct nvk_physical_device *pdev,
                              VkFormat format, VkImageTiling tiling,
                              uint64_t drm_format_mod);

void
nvk_get_drm_format_modifier_properties_list(const struct nvk_physical_device *pdev,
                                            VkFormat vk_format,
                                            VkBaseOutStructure *ext);

uint32_t
nvk_image_max_dimension(const struct nv_device_info *info,
                        VkImageType image_type);

struct nvk_image_plane {
   struct nil_image nil;
   uint64_t addr;

   /** Reserved VA for sparse images, NULL otherwise. */
   struct nvkmd_va *va;

   /* Byte offset of the plane. Calculated and stashed at image create time
    * so that we don't have to constantly calculate it with every bind and
    * sparse bind, and so that we can respect explicit modifier offsets for
    * linear modifiers.
    */
   uint64_t plane_offset_B;

   /* Plane alignment in bytes. This isn't always the same as nil->align_B
    * because sparse and tiled images have a minimum.
    */
   uint32_t plane_align_B;

   /* Needed for EXT_Host_Image_Copy. We get GPU addresses from the API,
    * so we stash in the memory object and the offset in the plane to be able
    * to retrieve CPU addresses for host copies.
    */
   struct nvk_device_memory *host_mem;

   /* This is not the same as the plane offset above because this includes the
    * memory offset given by the client at bind time in VkBindImageMemoryInfo:
    * host_offset_B = plane_offset_B + memoryOffset
    */
   uint64_t host_offset_B;
};

struct nvk_zcull_plane {
   struct nil_zcull nil;
   uint64_t addr;
   uint64_t plane_offset_B;
};

struct nvk_image {
   struct vk_image vk;

   /** True if the planes are bound separately
    *
    * This is set based on VK_IMAGE_CREATE_DISJOINT_BIT
    */
   bool disjoint;

   /** True if this image uses separate depth/stencil
    *
    * In this case, stencil will be in planes[1].
    */
   bool separate_zs;

   uint8_t plane_count;
   struct nvk_image_plane planes[NVK_MAX_IMAGE_PLANES];

   struct nvk_zcull_plane zcull;

   /* In order to support D32_SFLOAT_S8_UINT, a temp area is
    * needed. The stencil plane can't be a copied using the DMA
    * engine in a single pass since it would need 8 components support.
    * Instead we allocate a 16-bit temp, that gets copied into, then
    * copied again down to the 8-bit result.
    */
   struct nvk_image_plane stencil_copy_temp;

   /* The hardware doesn't support rendering to linear images except
    * under certain conditions, so to support DRM_FORMAT_MOD_LINEAR
    * rendering in the general case, we need to keep a tiled copy, which would
    * be used to fake support if the conditions aren't satisfied.
    * 
    * In order to avoid needlessly paying the memory cost of the tiled shadows
    * even if they aren't needed (imports for texturing or rendering with the
    * conditions satisfied), we hold a mutex at image create time to guard the
    * memory object and defer the memory allocation till draw time, where we
    * check if it's needed per-plane and selectively allocate memory for it.
    */
   simple_mtx_t tiled_shadow_mutex;
   struct nvk_image_plane linear_tiled_shadows[NVK_MAX_IMAGE_PLANES];
   struct nvkmd_mem *linear_tiled_shadow_mem[NVK_MAX_IMAGE_PLANES];

   /* This indicates that we would like to compress the image and would prefer
    * larger pages and a dedicated allocation.
    */
   bool can_compress;
   /* This indicates that we actually have compressed the image. This is set at
    * bind time.
    */
   bool is_compressed;

   /* Total image size and alignment for all planes combined, used by
    * GetImageMemoryRequirements(). Set to 0 for disjoint images as size and
    * alignment have to be calculated per plane in that case.
    */
   uint64_t image_size_B;
   uint32_t image_align_B;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(nvk_image, vk.base, VkImage, VK_OBJECT_TYPE_IMAGE)

static inline uint64_t
nvk_image_plane_base_address(const struct nvk_image_plane *plane)
{
   return plane->addr;
}

static inline uint64_t
nvk_image_base_address(const struct nvk_image *image, uint8_t plane)
{
   return nvk_image_plane_base_address(&image->planes[plane]);
}

static inline uint64_t
nvk_image_size_B(const struct nvk_image *image, uint8_t plane)
{
   return image->planes[plane].nil.size_B;
}

static inline uint8_t
nvk_image_aspects_to_plane(const struct nvk_image *image,
                           VkImageAspectFlags aspectMask)
{
   /* Memory planes are only allowed for memory operations */
   assert(!(aspectMask & (VK_IMAGE_ASPECT_MEMORY_PLANE_0_BIT_EXT |
                          VK_IMAGE_ASPECT_MEMORY_PLANE_1_BIT_EXT |
                          VK_IMAGE_ASPECT_MEMORY_PLANE_2_BIT_EXT |
                          VK_IMAGE_ASPECT_MEMORY_PLANE_3_BIT_EXT)));

   /* Verify that the aspects are actually in the image */
   assert(!(aspectMask & ~image->vk.aspects));

   /* Must only be one aspect unless it's depth/stencil */
   assert(aspectMask == (VK_IMAGE_ASPECT_DEPTH_BIT |
                         VK_IMAGE_ASPECT_STENCIL_BIT) ||
          util_bitcount(aspectMask) == 1);

   if (image->separate_zs) {
      assert(util_bitcount(aspectMask) == 1);
      switch(aspectMask) {
      case VK_IMAGE_ASPECT_DEPTH_BIT: return 0;
      case VK_IMAGE_ASPECT_STENCIL_BIT: return 1;
      default: UNREACHABLE("Not a depth/stencil aspect");
      }
   }

   switch(aspectMask) {
   case VK_IMAGE_ASPECT_PLANE_1_BIT: return 1;
   case VK_IMAGE_ASPECT_PLANE_2_BIT: return 2;
   default: return 0;
   }
}

static inline uint8_t
nvk_image_memory_aspects_to_plane(ASSERTED const struct nvk_image *image,
                                  VkImageAspectFlags aspectMask)
{
   if (aspectMask & (VK_IMAGE_ASPECT_MEMORY_PLANE_0_BIT_EXT |
                     VK_IMAGE_ASPECT_MEMORY_PLANE_1_BIT_EXT |
                     VK_IMAGE_ASPECT_MEMORY_PLANE_2_BIT_EXT |
                     VK_IMAGE_ASPECT_MEMORY_PLANE_3_BIT_EXT)) {
      /* We don't support DRM format modifiers on anything but single-plane
       * color at the moment.
       */
      assert(aspectMask == VK_IMAGE_ASPECT_MEMORY_PLANE_0_BIT_EXT);
      return 0;
   } else {
      return nvk_image_aspects_to_plane(image, aspectMask);
   }
}

VkResult nvk_queue_image_bind(struct nvk_queue *queue,
                              const VkSparseImageMemoryBindInfo *bind_info);

VkResult nvk_queue_image_opaque_bind(struct nvk_queue *queue,
                                     const VkSparseImageOpaqueMemoryBindInfo *bind_info);

#endif
