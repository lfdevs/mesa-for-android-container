/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 */

#include "tu_image.h"

#include "fdl/fd6_format_table.h"
#include "common/freedreno_lrz.h"

#include "util/u_debug.h"
#include "util/format/u_format.h"
#include "vk_android.h"
#include "vk_debug_utils.h"
#include "vk_util.h"
#include "drm-uapi/drm_fourcc.h"
#include "vulkan/vulkan_core.h"

#include "fdl/freedreno_layout.h"

#include "tu_buffer.h"
#include "tu_cs.h"
#include "tu_descriptor_set.h"
#include "tu_device.h"
#include "tu_formats.h"
#include "tu_lrz.h"
#include "tu_rmv.h"
#include "tu_wsi.h"

uint32_t
tu6_plane_count(VkFormat format)
{
   switch (format) {
   case VK_FORMAT_D32_SFLOAT_S8_UINT:
      /* We do not support interleaved depth/stencil. Instead, we decompose to
       * a depth plane and a stencil plane.
       */
      return 2;

   default:
      return vk_format_get_plane_count(format);
   }
}

enum pipe_format
tu6_plane_format(VkFormat format, uint32_t plane)
{
   switch (format) {
   case VK_FORMAT_D32_SFLOAT_S8_UINT:
      /* See tu6_plane_count above */
      return !plane ? PIPE_FORMAT_Z32_FLOAT : PIPE_FORMAT_S8_UINT;

   case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
      /* The 0'th plane of this format has a different UBWC compression */
      return !plane ? PIPE_FORMAT_Y8_UNORM : PIPE_FORMAT_R8G8_UNORM;

   default:
      return vk_format_to_pipe_format(vk_format_get_plane_format(format, plane));
   }
}

uint32_t
tu6_plane_index(VkFormat format, VkImageAspectFlags aspect_mask)
{
   /* Must only be one aspect unless it's depth/stencil */
   assert(aspect_mask ==
             (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT) ||
          util_bitcount(aspect_mask) == 1);

   switch (aspect_mask) {
   default:
      assert(aspect_mask != VK_IMAGE_ASPECT_MEMORY_PLANE_3_BIT_EXT);
      return 0;

   case VK_IMAGE_ASPECT_PLANE_1_BIT:
   case VK_IMAGE_ASPECT_MEMORY_PLANE_1_BIT_EXT:
      return 1;

   case VK_IMAGE_ASPECT_PLANE_2_BIT:
   case VK_IMAGE_ASPECT_MEMORY_PLANE_2_BIT_EXT:
      return 2;

   case VK_IMAGE_ASPECT_STENCIL_BIT:
      return format == VK_FORMAT_D32_SFLOAT_S8_UINT;
   }
}

enum pipe_format
tu_format_for_aspect(enum pipe_format format, VkImageAspectFlags aspect_mask)
{
   switch (format) {
   case PIPE_FORMAT_Z24_UNORM_S8_UINT:
      /* VK_IMAGE_ASPECT_COLOR_BIT is used internally for blits (despite we
       * also incorrectly advertise VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT for
       * depth formats).  Return PIPE_FORMAT_Z24_UNORM_S8_UINT_AS_R8G8B8A8 in
       * this case.
       *
       * Otherwise, return the appropriate pipe format and let fdl6_view_init
       * take care of the rest.
       */
      if (aspect_mask & VK_IMAGE_ASPECT_COLOR_BIT)
         return PIPE_FORMAT_Z24_UNORM_S8_UINT_AS_R8G8B8A8;
      if (aspect_mask & VK_IMAGE_ASPECT_STENCIL_BIT) {
         if (aspect_mask & VK_IMAGE_ASPECT_DEPTH_BIT)
            return PIPE_FORMAT_Z24_UNORM_S8_UINT;
         else
            return PIPE_FORMAT_X24S8_UINT;
      } else {
         return PIPE_FORMAT_Z24X8_UNORM;
      }
   case PIPE_FORMAT_Z24X8_UNORM:
      if (aspect_mask & VK_IMAGE_ASPECT_COLOR_BIT)
         return PIPE_FORMAT_Z24_UNORM_S8_UINT_AS_R8G8B8A8;
      return PIPE_FORMAT_Z24X8_UNORM;
   default:
      return format;
   }
}

static bool
tu_is_r8g8(enum pipe_format format)
{
   return (util_format_get_blocksize(format) == 2) &&
          (util_format_get_nr_components(format) == 2);
}

static bool
tu_is_r8g8_compatible(enum pipe_format format)
{
   return (util_format_get_blocksize(format) == 2) &&
          !util_format_is_depth_or_stencil(format);
}

uint64_t
tu_layer_address(const struct fdl6_view *iview, uint32_t layer)
{
   return iview->base_addr + iview->layer_size * layer;
}

void
tu_cs_image_ref(struct tu_cs *cs, const struct fdl6_view *iview, uint32_t layer)
{
   tu_cs_emit(cs, A6XX_RB_MRT_PITCH(0, iview->pitch).value);
   tu_cs_emit(cs, iview->layer_size >> 6);
   tu_cs_emit_qw(cs, tu_layer_address(iview, layer));
}

void
tu_cs_image_stencil_ref(struct tu_cs *cs, const struct tu_image_view *iview, uint32_t layer)
{
   tu_cs_emit(cs, A6XX_RB_STENCIL_BUFFER_PITCH(iview->stencil_pitch).value);
   tu_cs_emit(cs, iview->stencil_layer_size >> 6);
   tu_cs_emit_qw(cs, iview->stencil_base_addr + iview->stencil_layer_size * layer);
}

void
tu_cs_image_depth_ref(struct tu_cs *cs, const struct tu_image_view *iview, uint32_t layer)
{
   tu_cs_emit(cs, A6XX_RB_DEPTH_BUFFER_PITCH(iview->depth_pitch).value);
   tu_cs_emit(cs, iview->depth_layer_size >> 6);
   tu_cs_emit_qw(cs, iview->depth_base_addr + iview->depth_layer_size * layer);
}

template <chip CHIP>
void
tu_cs_image_ref_2d(struct tu_cs *cs, const struct fdl6_view *iview, uint32_t layer, bool src)
{
   tu_cs_emit_qw(cs, iview->base_addr + iview->layer_size * layer);
   /* TPL1_A2D_SRC_TEXTURE_PITCH has shifted pitch field */
   if (src)
      tu_cs_emit(cs, TPL1_A2D_SRC_TEXTURE_PITCH(CHIP, .pitch = iview->pitch).value);
   else
      tu_cs_emit(cs, A6XX_RB_A2D_DEST_BUFFER_PITCH(iview->pitch).value);
}
TU_GENX(tu_cs_image_ref_2d);

void
tu_cs_image_flag_ref(struct tu_cs *cs, const struct fdl6_view *iview, uint32_t layer)
{
   tu_cs_emit_qw(cs, iview->ubwc_addr + iview->ubwc_layer_size * layer);
   tu_cs_emit(cs, iview->FLAG_BUFFER_PITCH);
}

void
tu_image_view_init(struct tu_device *device,
                   struct tu_image_view *iview,
                   const VkImageViewCreateInfo *pCreateInfo)
{
   VK_FROM_HANDLE(tu_image, image, pCreateInfo->image);
   const VkImageSubresourceRange *range = &pCreateInfo->subresourceRange;
   VkImageAspectFlags aspect_mask = pCreateInfo->subresourceRange.aspectMask;

   const struct VkSamplerYcbcrConversionInfo *ycbcr_conversion =
      vk_find_struct_const(pCreateInfo->pNext, SAMPLER_YCBCR_CONVERSION_INFO);
   const struct vk_ycbcr_conversion *conversion = ycbcr_conversion ?
      vk_ycbcr_conversion_from_handle(ycbcr_conversion->conversion) : NULL;

   vk_image_view_init(&device->vk, &iview->vk, pCreateInfo);
   assert(iview->vk.format != VK_FORMAT_UNDEFINED);

   iview->image = image;

   const struct fdl_layout *layouts[3];

   layouts[0] = &image->layout[tu6_plane_index(image->vk.format, aspect_mask)];

   enum pipe_format format;
   if (iview->vk.format == VK_FORMAT_D32_SFLOAT_S8_UINT)
      format = tu_aspects_to_plane(iview->vk.format, aspect_mask);
   else
      format = vk_format_to_pipe_format(iview->vk.format);

   if (image->vk.format == VK_FORMAT_G8_B8R8_2PLANE_420_UNORM &&
       aspect_mask == VK_IMAGE_ASPECT_PLANE_0_BIT) {
      if (iview->vk.format == VK_FORMAT_R8_UNORM) {
         /* The 0'th plane of this format has a different UBWC compression. */
         format = PIPE_FORMAT_Y8_UNORM;
      } else {
         /* If the user wants to reinterpret this plane, then they should've
          * set MUTABLE_FORMAT_BIT which should disable UBWC and tiling.
          */
         assert(!layouts[0]->ubwc);
      }
   }

   if (aspect_mask == VK_IMAGE_ASPECT_COLOR_BIT &&
       vk_format_get_plane_count(iview->vk.format) > 1) {
      layouts[1] = &image->layout[1];
      layouts[2] = &image->layout[2];
   }

   vk_component_mapping_to_pipe_swizzle(pCreateInfo->components,
                                        iview->swizzle);

   struct fdl_view_args args = {};
   args.iova = image->iova;
   args.base_array_layer = range->baseArrayLayer;
   args.base_miplevel = range->baseMipLevel;
   args.layer_count = vk_image_subresource_layer_count(&image->vk, range);
   args.level_count = vk_image_subresource_level_count(&image->vk, range);
   args.min_lod_clamp = iview->vk.min_lod;
   args.format = tu_format_for_aspect(format, aspect_mask);
   vk_component_mapping_to_pipe_swizzle(pCreateInfo->components, args.swiz);
   if (conversion) {
      unsigned char conversion_swiz[4], create_swiz[4];
      memcpy(create_swiz, args.swiz, sizeof(create_swiz));

      VkComponentMapping component = {
         .r = conversion->state.mapping[0],
         .g = conversion->state.mapping[1],
         .b = conversion->state.mapping[2],
         .a = conversion->state.mapping[3]
      };
      vk_component_mapping_to_pipe_swizzle(component, conversion_swiz);
      util_format_compose_swizzles(create_swiz, conversion_swiz, args.swiz);
   }

   switch (pCreateInfo->viewType) {
   case VK_IMAGE_VIEW_TYPE_1D:
   case VK_IMAGE_VIEW_TYPE_1D_ARRAY:
      args.type = FDL_VIEW_TYPE_1D;
      break;
   case VK_IMAGE_VIEW_TYPE_2D:
   case VK_IMAGE_VIEW_TYPE_2D_ARRAY:
      args.type = FDL_VIEW_TYPE_2D;
      break;
   case VK_IMAGE_VIEW_TYPE_CUBE:
   case VK_IMAGE_VIEW_TYPE_CUBE_ARRAY:
      args.type = FDL_VIEW_TYPE_CUBE;
      break;
   case VK_IMAGE_VIEW_TYPE_3D:
      args.type = FDL_VIEW_TYPE_3D;
      break;
   default:
      UNREACHABLE("unknown view type");
   }

   STATIC_ASSERT((unsigned)VK_CHROMA_LOCATION_COSITED_EVEN == (unsigned)FDL_CHROMA_LOCATION_COSITED_EVEN);
   STATIC_ASSERT((unsigned)VK_CHROMA_LOCATION_MIDPOINT == (unsigned)FDL_CHROMA_LOCATION_MIDPOINT);
   if (conversion) {
      args.chroma_offsets[0] = (enum fdl_chroma_location) conversion->state.chroma_offsets[0];
      args.chroma_offsets[1] = (enum fdl_chroma_location) conversion->state.chroma_offsets[1];
   }

   TU_CALLX(device, fdl6_view_init)(&iview->view, layouts, &args, device->use_z24uint_s8uint);

   if (image->vk.format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
      struct fdl_layout *layout = &image->layout[0];
      iview->depth_base_addr = image->iova +
         fdl_surface_offset(layout, range->baseMipLevel, range->baseArrayLayer);
      iview->depth_layer_size = fdl_layer_stride(layout, range->baseMipLevel);
      iview->depth_pitch = fdl_pitch(layout, range->baseMipLevel);

      layout = &image->layout[1];
      iview->stencil_base_addr = image->iova +
         fdl_surface_offset(layout, range->baseMipLevel, range->baseArrayLayer);
      iview->stencil_layer_size = fdl_layer_stride(layout, range->baseMipLevel);
      iview->stencil_pitch = fdl_pitch(layout, range->baseMipLevel);
   }
}

bool
tiling_possible(VkFormat format)
{
   if (format == VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM ||
       format == VK_FORMAT_G8B8G8R8_422_UNORM ||
       format == VK_FORMAT_B8G8R8G8_422_UNORM)
      return false;

   return true;
}

/* Checks if we should advertise UBWC support for the given usage.
 *
 * Used by both vkCreateImage and vkGetPhysicalDeviceFormatProperties2, so the
 * logical tu_device may be NULL.
 */
bool
ubwc_possible(struct tu_device *device,
              VkFormat format,
              VkImageType type,
              VkImageCreateFlags flags,
              VkImageUsageFlags usage,
              VkImageUsageFlags stencil_usage,
              const struct fd_dev_info *info,
              VkSampleCountFlagBits samples,
              uint32_t mip_levels,
              bool use_z24uint_s8uint)
{
   /* TODO: enable for a702 */
   if (info->props.is_a702)
      return false;

   /* UBWC isn't possible with sparse residency, because unbound blocks may
    * have leftover fast-clear data and therefore may show up as non-zero.
    * TODO: Enable UBWC if nonResidentStrict isn't enabled.
    */
   if (flags & VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT)
      return false;

   /* no UBWC with compressed formats, E5B9G9R9, S8_UINT
    * (S8_UINT because separate stencil doesn't have UBWC-enable bit)
    */
   if (vk_format_is_compressed(format) ||
       format == VK_FORMAT_E5B9G9R9_UFLOAT_PACK32 ||
       format == VK_FORMAT_S8_UINT)
      return false;

   /* In copy_format, we treat snorm as unorm to avoid clamping.  But snorm
    * and unorm are UBWC incompatible for special values such as all 0's or
    * all 1's prior to a740.  Disable UBWC for snorm.
    */
   if (vk_format_is_snorm(format) &&
       !info->props.ubwc_unorm_snorm_int_compatible)
      return false;

   if (!info->props.has_8bpp_ubwc &&
       vk_format_get_blocksizebits(format) == 8 &&
       vk_format_get_plane_count(format) == 1)
      return false;

   if (type == VK_IMAGE_TYPE_3D && mip_levels > 1) {
      if (device) {
         perf_debug(
            device,
            "Disabling UBWC for %s 3D image with mipmaps, but it should be "
            "possible to support.",
            util_format_name(vk_format_to_pipe_format(format)));
      }
      return false;
   }

   /* Disable UBWC for storage images when not supported.
    *
    * Prior to a7xx, storage images must be readonly or writeonly to use UBWC.
    * Freedreno can determine when this isn't the case and decompress the
    * image on-the-fly, but we don't know which image a binding corresponds to
    * and we can't change the descriptor so we can't do this.
    */
   if (((usage | stencil_usage) & VK_IMAGE_USAGE_STORAGE_BIT) &&
       !info->props.supports_uav_ubwc) {
      return false;
   }

   /* A690 seem to have broken UBWC for depth/stencil, it requires
    * depth flushing where we cannot realistically place it, like between
    * ordinary draw calls writing read/depth. WSL blob seem to use ubwc
    * sometimes for depth/stencil.
    */
   if (info->props.broken_ds_ubwc_quirk &&
       vk_format_is_depth_or_stencil(format))
      return false;

   /* We don't support compressing or decompressing on the CPU */
   if ((usage | stencil_usage) & VK_IMAGE_USAGE_HOST_TRANSFER_BIT_EXT) {
      return false;
   }

   /* Disable UBWC for D24S8 on A630 in some cases
    *
    * VK_IMAGE_ASPECT_STENCIL_BIT image view requires to be able to sample
    * from the stencil component as UINT, however no format allows this
    * on a630 (the special FMT6_Z24_UINT_S8_UINT format is missing)
    *
    * It must be sampled as FMT6_8_8_8_8_UINT, which is not UBWC-compatible
    *
    * If we wish to get the border colors correct without knowing the format
    * when creating the sampler, we also have to use the A630 workaround.
    */
   if (!use_z24uint_s8uint &&
       format == VK_FORMAT_D24_UNORM_S8_UINT &&
       (stencil_usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)))
      return false;

   if (!info->props.has_z24uint_s8uint &&
       (format == VK_FORMAT_D24_UNORM_S8_UINT ||
        format == VK_FORMAT_X8_D24_UNORM_PACK32) &&
       samples > VK_SAMPLE_COUNT_1_BIT) {
      return false;
   }

   return true;
}

/* R8G8 have a different block width/height and height alignment from other
 * formats that would normally be compatible (like R16), and so if we are
 * trying to, for example, sample R16 as R8G8 we need to demote to linear.
 */
static bool
format_list_reinterprets_r8g8_r16(enum pipe_format format, const VkImageFormatListCreateInfo *fmt_list)
{
   /* Check if it's actually a 2-cpp color format. */
   if (!tu_is_r8g8_compatible(format))
      return false;

   /* If there's no format list, then the app may reinterpret to any compatible
    * format.
    */
   if (!fmt_list || !fmt_list->viewFormatCount)
      return true;

   bool has_r8g8 = false;
   bool has_non_r8g8 = false;
   for (uint32_t i = 0; i < fmt_list->viewFormatCount; i++) {
      enum pipe_format format =
         vk_format_to_pipe_format(fmt_list->pViewFormats[i]);
      if (tu_is_r8g8(format))
         has_r8g8 = true;
      else
         has_non_r8g8 = true;
   }
   return has_r8g8 && has_non_r8g8;
}

static bool
format_list_has_swaps(const VkImageFormatListCreateInfo *fmt_list)
{
   /* If there's no format list, then the app may reinterpret to any compatible
    * format, and presumably one would have the swap set.
    */
   if (!fmt_list || !fmt_list->viewFormatCount)
      return true;

   for (uint32_t i = 0; i < fmt_list->viewFormatCount; i++) {
      enum pipe_format format =
         vk_format_to_pipe_format(fmt_list->pViewFormats[i]);

      if (tu6_format_texture(format, TILE6_LINEAR, false).swap)
         return true;
   }
   return false;
}

static bool
format_list_has_uncompressed_format(
   const VkImageFormatListCreateInfo *fmt_list)
{
   if (!fmt_list || !fmt_list->viewFormatCount)
      return true;

   for (uint32_t i = 0; i < fmt_list->viewFormatCount; i++) {
      if (!vk_format_is_compressed(fmt_list->pViewFormats[i]))
         return true;
   }
   return false;
}

template <chip CHIP>
VkResult
tu_image_update_layout(struct tu_device *device, struct tu_image *image,
                       uint64_t modifier, const VkSubresourceLayout *plane_layouts)
{
   enum a6xx_tile_mode tile_mode = TILE6_3;
#if DETECT_OS_LINUX || DETECT_OS_BSD
   image->vk.drm_format_mod = modifier;
#endif

   if (modifier == DRM_FORMAT_MOD_LINEAR) {
      image->force_linear_tile = true;
   }

   if (image->force_linear_tile) {
      tile_mode = TILE6_LINEAR;
      image->ubwc_enabled = false;
   }

   /* Whether a view of the image with an R8G8 format could be made. */
   bool has_r8g8 = tu_is_r8g8(vk_format_to_pipe_format(image->vk.format));

   /* With AHB, we could be asked to create an image with VK_IMAGE_TILING_LINEAR
    * but gralloc doesn't know this.  So if we are explicitly told that it is
    * UBWC, then override how the image was created.
    */
   bool force_ubwc = false;
   if (modifier == DRM_FORMAT_MOD_QCOM_COMPRESSED) {
      assert(!image->force_linear_tile);
      image->ubwc_enabled = true;
      force_ubwc = true;
   }

   /* R8G8 images have a special tiled layout which we don't implement yet in
    * fdl6_memcpy, fall back to linear.
    */
   if (has_r8g8 && tile_mode == TILE6_3 &&
       (image->vk.usage & VK_IMAGE_USAGE_HOST_TRANSFER_BIT_EXT)) {
      tile_mode = TILE6_LINEAR;
   }

   /* We cannot support sparse residency with linear images, it should've been
    * rejected.
    */
   assert(!(image->vk.create_flags & VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT) ||
          tile_mode == TILE6_3);

   for (uint32_t i = 0; i < tu6_plane_count(image->vk.format); i++) {
      struct fdl_layout *layout = &image->layout[i];
      enum pipe_format format = tu6_plane_format(image->vk.format, i);
      uint32_t width0 = vk_format_get_plane_width(image->vk.format, i, image->vk.extent.width);
      uint32_t height0 = vk_format_get_plane_height(image->vk.format, i, image->vk.extent.height);

      if (i == 1 && image->vk.format == VK_FORMAT_D32_SFLOAT_S8_UINT)
         /* no UBWC for separate stencil */
         image->ubwc_enabled = false;

      struct fdl_explicit_layout plane_layout;

      if (plane_layouts) {
         /* only expect simple 2D images for now */
         if (image->vk.mip_levels != 1 ||
            image->vk.array_layers != 1 ||
            image->vk.extent.depth != 1)
            return vk_error(device, VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT);

         plane_layout.offset = plane_layouts[i].offset;
         plane_layout.pitch = plane_layouts[i].rowPitch;
         /* note: use plane_layouts[0].arrayPitch to support array formats */
      }

      layout->tile_mode = tile_mode;
      layout->ubwc = image->ubwc_enabled;

      struct fdl_image_params params = {
         .format = format,
         .nr_samples = image->vk.samples,
         .width0 = width0,
         .height0 = height0,
         .depth0 = image->vk.extent.depth,
         .mip_levels = image->vk.mip_levels,
         .array_size = image->vk.array_layers,
         .tile_mode = tile_mode,
         .ubwc = image->ubwc_enabled,
         .force_ubwc = force_ubwc,
         .is_3d = image->vk.image_type == VK_IMAGE_TYPE_3D,
         .is_mutable = image->is_mutable,
         .sparse = image->vk.create_flags &
            VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT,
         .force_disable_linear_fallback = image->force_disable_linear_fallback,
      };

      if (!fdl6_layout_image(layout, &device->physical_device->dev_info,
                             &params, plane_layouts ? &plane_layout : NULL)) {
         assert(plane_layouts); /* can only fail with explicit layout */
         return vk_error(device, VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT);
      }

      if (TU_DEBUG(LAYOUT))
         fdl_dump_layout(layout);

      /* fdl6_layout can't take explicit offset without explicit pitch
       * add offset manually for extra layouts for planes
       */
      if (!plane_layouts && i > 0) {
         uint32_t offset = ALIGN_POT(image->total_size, 4096);
         for (int i = 0; i < image->vk.mip_levels; i++) {
            layout->slices[i].offset += offset;
            layout->ubwc_slices[i].offset += offset;
         }
         layout->size += offset;
      }

      image->total_size = MAX2(image->total_size, layout->size);
   }

   image->max_tile_w_constraint_fdm = ~0;
   image->max_tile_h_constraint_fdm = ~0;

   const struct util_format_description *desc = util_format_description(image->layout[0].format);
   if (util_format_has_depth(desc) && device->use_lrz) {
      /* If FDM offset is enabled, then the LRZ image will be shifted over. We
       * have to overallocate it, but we have no idea how large the tiles it's
       * used with will be. Try to calculate a maximum size of tile that would
       * still let us do LRZ fast clears that we'll use to inform tiling setup
       * later once we know the rest of the images.  We'll fall back to
       * allocating for the device's maximum tile size if we can't ensure
       * LRZ fast clears.
       */
      struct fdl_lrz_fdm_extra_size extra_size = { 0, 0 };
      if (image->vk.create_flags &
          VK_IMAGE_CREATE_FRAGMENT_DENSITY_MAP_OFFSET_BIT_EXT) {
         extra_size = fdl6_lrz_get_max_fdm_extra_size<CHIP>(
            device->physical_device->info, image->layout[0].width0,
            image->layout[0].height0, image->vk.samples,
            image->vk.array_layers);

         image->max_tile_w_constraint_fdm = extra_size.extra_width;
         image->max_tile_h_constraint_fdm = extra_size.extra_height;
      }

      fdl6_lrz_layout_init<CHIP>(&image->lrz_layout, &image->layout[0],
                                 extra_size.extra_width, extra_size.extra_height,
                                 device->physical_device->info,
                                 image->total_size, image->vk.array_layers);

      image->total_size += image->lrz_layout.lrz_total_size;
   } else {
      image->lrz_layout.lrz_height = 0;
      image->lrz_layout.lrz_total_size = 0;
   }

   return VK_SUCCESS;
}
TU_GENX(tu_image_update_layout);

/* Return true if all formats in the format list can support UBWC.
 */
static bool
format_list_ubwc_possible(struct tu_device *dev,
                          const VkImageFormatListCreateInfo *fmt_list,
                          const VkImageCreateInfo *create_info)
{
   /* If there is no format list, we may have to assume that a
    * UBWC-incompatible format may be used.
    * TODO: limit based on compatiblity class
    */
   if (!fmt_list || !fmt_list->viewFormatCount)
      return false;

   for (uint32_t i = 0; i < fmt_list->viewFormatCount; i++) {
      if (!ubwc_possible(dev, fmt_list->pViewFormats[i],
                         create_info->imageType, create_info->flags,
                         create_info->usage, create_info->usage,
                         dev->physical_device->info, create_info->samples,
                         create_info->mipLevels, dev->use_z24uint_s8uint))
         return false;
   }

   return true;
}

VkResult
tu_image_init(struct tu_device *device, struct tu_image *image,
              const VkImageCreateInfo *pCreateInfo)
{
   image->ubwc_enabled = true;

   /* use linear tiling if requested */
   if (pCreateInfo->tiling == VK_IMAGE_TILING_LINEAR) {
      image->force_linear_tile = true;
   }

   /* Force linear tiling for formats with "fake" optimalTilingFeatures */
   if (!tiling_possible(image->vk.format)) {
      image->force_linear_tile = true;
   }

   /* No sense in tiling a 1D image, you'd just waste space and cache locality. */
   if (pCreateInfo->imageType == VK_IMAGE_TYPE_1D) {
      image->force_linear_tile = true;
   }

   /* Fragment density maps are sampled on the CPU and we don't support
    * sampling tiled images on the CPU or UBWC at the moment.
    */
   if (pCreateInfo->usage & VK_IMAGE_USAGE_FRAGMENT_DENSITY_MAP_BIT_EXT) {
      image->force_linear_tile = true;
   }

   /* Force linear tiling for HIC usage with swapped formats. Because tiled
    * images are stored without the swap, we would have to apply the swap when
    * copying on the CPU, which for some formats is tricky.
    *
    * TODO: should we add a fast path for BGRA8 and allow tiling for it?
    */
   if ((pCreateInfo->usage & VK_IMAGE_USAGE_HOST_TRANSFER_BIT_EXT) &&
       fd6_color_swap(vk_format_to_pipe_format(image->vk.format),
                                               TILE6_LINEAR, false) != WZYX)
      image->force_linear_tile = true;

   /* Some kind of HW limitation. */
   if (pCreateInfo->usage &
          VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR &&
       image->vk.extent.width < 16) {
      image->force_linear_tile = true;
   }

   if (image->force_linear_tile ||
       !ubwc_possible(device, image->vk.format, pCreateInfo->imageType,
                      pCreateInfo->flags, pCreateInfo->usage,
                      image->vk.stencil_usage,
                      device->physical_device->info, pCreateInfo->samples,
                      pCreateInfo->mipLevels, device->use_z24uint_s8uint))
      image->ubwc_enabled = false;

   /* Mutable images can be reinterpreted as any other compatible format.
    * This is a problem with UBWC (compression for different formats is different),
    * but also tiling ("swap" affects how tiled formats are stored in memory)
    * Depth and stencil formats cannot be reintepreted as another format, and
    * cannot be linear with sysmem rendering, so don't fall back for those.
    *
    * TODO:
    * - if the fmt_list contains only formats which are swapped, but compatible
    *   with each other (B8G8R8A8_UNORM and B8G8R8A8_UINT for example), then
    *   tiling is still possible
    */
   if ((pCreateInfo->flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT) &&
       !vk_format_is_depth_or_stencil(image->vk.format)) {
      const VkImageFormatListCreateInfo *fmt_list =
         vk_find_struct_const(pCreateInfo->pNext, IMAGE_FORMAT_LIST_CREATE_INFO);
      if (!tu6_mutable_format_list_ubwc_compatible(device->physical_device->info,
                                                   fmt_list)) {
         bool mutable_ubwc_fc = device->physical_device->info->props.ubwc_all_formats_compatible;

         /* NV12 uses a special compression scheme for the Y channel which
          * doesn't support reinterpretation. We have to fall back to linear
          * always.
          */
         if (pCreateInfo->format == VK_FORMAT_G8_B8R8_2PLANE_420_UNORM) {
            if (image->ubwc_enabled) {
               perf_debug(
                  device,
                  "Disabling UBWC and tiling on %dx%d %s resource due to mutable formats "
                  "(fmt list %s)",
                  image->vk.extent.width, image->vk.extent.height,
                  util_format_name(vk_format_to_pipe_format(image->vk.format)),
                  fmt_list ? "present" : "missing");
            }
            image->ubwc_enabled = false;
            image->force_linear_tile = true;
         } else if (!mutable_ubwc_fc) {
            if (image->ubwc_enabled) {
               if (fmt_list && fmt_list->viewFormatCount == 2) {
                  perf_debug(
                     device,
                     "Disabling UBWC on %dx%d %s resource due to mutable formats "
                     "(fmt list %s, %s)",
                     image->vk.extent.width, image->vk.extent.height,
                     util_format_name(vk_format_to_pipe_format(image->vk.format)),
                     util_format_name(vk_format_to_pipe_format(fmt_list->pViewFormats[0])),
                     util_format_name(vk_format_to_pipe_format(fmt_list->pViewFormats[1])));
               } else {
                  perf_debug(
                     device,
                     "Disabling UBWC on %dx%d %s resource due to mutable formats "
                     "(fmt list %s)",
                     image->vk.extent.width, image->vk.extent.height,
                     util_format_name(vk_format_to_pipe_format(image->vk.format)),
                     fmt_list ? "present" : "missing");
               }
               image->ubwc_enabled = false;
            }

            bool r8g8_r16 = format_list_reinterprets_r8g8_r16(vk_format_to_pipe_format(image->vk.format), fmt_list);
            bool fmt_list_has_swaps = format_list_has_swaps(fmt_list);

            if (r8g8_r16 || fmt_list_has_swaps) {
               image->ubwc_enabled = false;
               image->force_linear_tile = true;
            }
         } else {
            image->is_mutable = true;
            if (!format_list_ubwc_possible(device, fmt_list, pCreateInfo))
               image->ubwc_enabled = false;
         }

         /* If the threshold of the linear mipmap fallback for compressed
          * format is reached at a different mipmap level than the
          * size-compatible non-compressed formats the image can be viewed as,
          * then we have to disable the fallback. Otherwise, for some levels,
          * texels would be read from the wrong locations due to the tiling
          * mismatch.
          * NOTE: Prop driver falls back to LINEAR in this case.
          */
         if (!device->physical_device->info->props
                 .supports_linear_mipmap_threshold_in_blocks &&
             vk_format_is_compressed(image->vk.format) &&
             pCreateInfo->usage &
                VK_IMAGE_CREATE_BLOCK_TEXEL_VIEW_COMPATIBLE_BIT &&
             format_list_has_uncompressed_format(fmt_list)) {
            image->force_disable_linear_fallback = true;
         }
      }
   }

   if (TU_DEBUG(NOUBWC)) {
      image->ubwc_enabled = false;
   }

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_CreateImage(VkDevice _device,
               const VkImageCreateInfo *pCreateInfo,
               const VkAllocationCallbacks *alloc,
               VkImage *pImage)
{
   uint64_t modifier = DRM_FORMAT_MOD_INVALID;
   const VkSubresourceLayout *plane_layouts = NULL;
   VkResult result;

   VK_FROM_HANDLE(tu_device, device, _device);

#ifdef TU_USE_WSI_PLATFORM
   /* Ignore swapchain creation info on Android. Since we don't have an
    * implementation in Mesa, we're guaranteed to access an Android object
    * incorrectly.
    */
   const VkImageSwapchainCreateInfoKHR *swapchain_info =
      vk_find_struct_const(pCreateInfo->pNext, IMAGE_SWAPCHAIN_CREATE_INFO_KHR);
   if (swapchain_info && swapchain_info->swapchain != VK_NULL_HANDLE) {
      return wsi_common_create_swapchain_image(device->physical_device->vk.wsi_device,
                                               pCreateInfo,
                                               swapchain_info->swapchain,
                                               pImage);
   }
#endif

   struct tu_image *image = (struct tu_image *)
      vk_image_create(&device->vk, pCreateInfo, alloc, sizeof(*image));

   if (!image)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   if (pCreateInfo->tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT) {
      const VkImageDrmFormatModifierListCreateInfoEXT *mod_info =
         vk_find_struct_const(pCreateInfo->pNext,
                              IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT);
      const VkImageDrmFormatModifierExplicitCreateInfoEXT *drm_explicit_info =
         vk_find_struct_const(pCreateInfo->pNext,
                              IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT);

      assert(mod_info || drm_explicit_info);

      if (mod_info) {
         modifier = DRM_FORMAT_MOD_LINEAR;
         for (unsigned i = 0; i < mod_info->drmFormatModifierCount; i++) {
            if (mod_info->pDrmFormatModifiers[i] == DRM_FORMAT_MOD_QCOM_COMPRESSED)
               modifier = DRM_FORMAT_MOD_QCOM_COMPRESSED;
         }
      } else {
         modifier = drm_explicit_info->drmFormatModifier;
         assert(modifier == DRM_FORMAT_MOD_LINEAR ||
                modifier == DRM_FORMAT_MOD_QCOM_COMPRESSED);
         plane_layouts = drm_explicit_info->pPlaneLayouts;
      }
   } else {
      const struct wsi_image_create_info *wsi_info =
         vk_find_struct_const(pCreateInfo->pNext, WSI_IMAGE_CREATE_INFO_MESA);
      if (wsi_info && wsi_info->scanout)
         modifier = DRM_FORMAT_MOD_LINEAR;
   }

   /* This section is removed by the optimizer for non-ANDROID builds */
   VkImageDrmFormatModifierExplicitCreateInfoEXT eci;
   VkSubresourceLayout a_plane_layouts[TU_MAX_PLANE_COUNT];
   if (vk_image_is_android_native_buffer(&image->vk)) {
      result = vk_android_get_anb_layout(
         pCreateInfo, &eci, a_plane_layouts, TU_MAX_PLANE_COUNT);
      if (result != VK_SUCCESS)
         goto fail;

      plane_layouts = a_plane_layouts;
      modifier = eci.drmFormatModifier;
   }

   result = tu_image_init(device, image, pCreateInfo);
   if (result != VK_SUCCESS)
      goto fail;

   /* This section is removed by the optimizer for non-ANDROID builds */
   if (vk_image_is_android_hardware_buffer(&image->vk)) {
      /* At this time, an AHB handle is not yet provided.
       * Image layout will be filled up during vkBindImageMemory2
       */
      *pImage = tu_image_to_handle(image);
      return VK_SUCCESS;
   }

   result = TU_CALLX(device, tu_image_update_layout)(device, image, modifier,
                                                    plane_layouts);
   if (result != VK_SUCCESS)
      goto fail;

   /* This section is removed by the optimizer for non-ANDROID builds */
   if (vk_image_is_android_native_buffer(&image->vk)) {
      result = vk_android_import_anb(&device->vk, pCreateInfo, alloc,
                                     &image->vk);
      if (result != VK_SUCCESS)
         goto fail;
   }

   if (pCreateInfo->flags & VK_IMAGE_CREATE_SPARSE_BINDING_BIT) {
      struct tu_instance *instance = device->physical_device->instance;
      BITMASK_ENUM(tu_sparse_vma_flags) flags = 0;

      uint64_t client_address = 0;
      if (pCreateInfo->flags & VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT)
         flags |= TU_SPARSE_VMA_MAP_ZERO;
      if (pCreateInfo->flags & VK_IMAGE_CREATE_DESCRIPTOR_BUFFER_CAPTURE_REPLAY_BIT_EXT)
         flags |= TU_SPARSE_VMA_REPLAYABLE;

      const VkOpaqueCaptureDescriptorDataCreateInfoEXT *replay_info =
         vk_find_struct_const(pCreateInfo->pNext,
                              OPAQUE_CAPTURE_DESCRIPTOR_DATA_CREATE_INFO_EXT);
      if (replay_info && replay_info->opaqueCaptureDescriptorData) {
         flags |= TU_SPARSE_VMA_REPLAYABLE;
         client_address =
            *(const uint64_t *)replay_info->opaqueCaptureDescriptorData;
      }

      result = tu_sparse_vma_init(device, &image->vk.base, &image->vma,
                                  &image->iova, flags, image->total_size,
                                  client_address);

      if (result != VK_SUCCESS)
         goto fail;

      vk_address_binding_report(&instance->vk, &image->vk.base,
                                image->iova, image->total_size,
                                VK_DEVICE_ADDRESS_BINDING_TYPE_BIND_EXT);
   }

   TU_RMV(image_create, device, image);

#ifdef HAVE_PERFETTO
   tu_perfetto_log_create_image(device, image);
#endif

   *pImage = tu_image_to_handle(image);

   return VK_SUCCESS;
fail:
   vk_image_destroy(&device->vk, alloc, &image->vk);
   return result;
}

VKAPI_ATTR void VKAPI_CALL
tu_DestroyImage(VkDevice _device,
                VkImage _image,
                const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(tu_device, device, _device);
   VK_FROM_HANDLE(tu_image, image, _image);
   struct tu_instance *instance = device->physical_device->instance;

   if (!image)
      return;

   TU_RMV(image_destroy, device, image);

#ifdef HAVE_PERFETTO
   tu_perfetto_log_destroy_image(device, image);
#endif

   if (image->vk.create_flags & VK_IMAGE_CREATE_SPARSE_BINDING_BIT) {
      tu_sparse_vma_finish(device, &image->vma);
   }

   if (image->iova)
      vk_address_binding_report(&instance->vk, &image->vk.base,
                                image->iova, image->total_size,
                                VK_DEVICE_ADDRESS_BINDING_TYPE_UNBIND_EXT);

   vk_image_destroy(&device->vk, pAllocator, &image->vk);
}

static VkResult
tu_image_bind(struct tu_device *device,
              const VkBindImageMemoryInfo *bind_info)
{
   struct tu_instance *instance = device->physical_device->instance;
   VK_FROM_HANDLE(tu_image, image, bind_info->image);
   VK_FROM_HANDLE(tu_device_memory, mem, bind_info->memory);
   uint64_t offset = bind_info->memoryOffset;
   VkResult result;

   if (!mem) {
#if DETECT_OS_ANDROID
      /* TODO handle VkNativeBufferANDROID */
      UNREACHABLE("VkBindImageMemoryInfo with no memory");
#else
      const VkBindImageMemorySwapchainInfoKHR *swapchain_info =
         vk_find_struct_const(bind_info->pNext,
                              BIND_IMAGE_MEMORY_SWAPCHAIN_INFO_KHR);
      assert(swapchain_info && swapchain_info->swapchain != VK_NULL_HANDLE);
      mem = tu_device_memory_from_handle(wsi_common_get_memory(
         swapchain_info->swapchain, swapchain_info->imageIndex));
      /* memoryOffset is ignored when VkBindImageMemorySwapchainInfoKHR is
       * present, so we follow common wsi to set the offset to 0 here.
       */
      offset = 0;
#endif
   }

   assert(mem);
   if (vk_image_is_android_hardware_buffer(&image->vk)) {
      VkImageDrmFormatModifierExplicitCreateInfoEXT eci;
      VkSubresourceLayout a_plane_layouts[TU_MAX_PLANE_COUNT];
      result = vk_android_get_ahb_layout(mem->vk.ahardware_buffer, &eci,
                                         a_plane_layouts, TU_MAX_PLANE_COUNT);
      if (result != VK_SUCCESS)
         return result;

      result = TU_CALLX(device, tu_image_update_layout)(
         device, image, eci.drmFormatModifier, a_plane_layouts);
      if (result != VK_SUCCESS)
         return result;
   }
   image->mem = mem;
   image->mem_offset = offset;
   image->iova = mem->iova + offset;

   if (image->vk.usage & (VK_IMAGE_USAGE_FRAGMENT_DENSITY_MAP_BIT_EXT |
                          VK_IMAGE_USAGE_HOST_TRANSFER_BIT_EXT)) {
      assert(mem->bo); /* Transient images cannot have these usages */
      if (!mem->bo->map) {
         result = tu_bo_map(device, mem->bo, NULL);
         if (result != VK_SUCCESS)
            return result;
      }

      image->map = (char *) mem->bo->map + offset;
   } else {
      image->map = NULL;
   }
#ifdef HAVE_PERFETTO
   tu_perfetto_log_bind_image(device, image);
#endif

   TU_RMV(image_bind, device, image);

   vk_address_binding_report(&instance->vk, &image->vk.base, image->iova,
                             image->total_size,
                             VK_DEVICE_ADDRESS_BINDING_TYPE_BIND_EXT);

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_BindImageMemory2(VkDevice _device,
                    uint32_t bindInfoCount,
                    const VkBindImageMemoryInfo *pBindInfos)
{
   VK_FROM_HANDLE(tu_device, device, _device);
   VkResult result = VK_SUCCESS;

   for (uint32_t i = 0; i < bindInfoCount; i++) {
      const VkBindMemoryStatus *bind_status =
         vk_find_struct_const(&pBindInfos[i], BIND_MEMORY_STATUS);
      VkResult bind_result = tu_image_bind(device, &pBindInfos[i]);
      if (bind_status)
         *bind_status->pResult = bind_result;
      if (bind_result != VK_SUCCESS)
         result = bind_result;
   }

   return result;
}

static void
tu_get_image_memory_requirements(struct tu_device *dev, struct tu_image *image,
                                 VkMemoryRequirements2 *pMemoryRequirements)
{
   uint32_t alignment = image->layout[0].base_align;
   if (image->vk.create_flags & VK_IMAGE_CREATE_SPARSE_BINDING_BIT)
      alignment = MAX2(alignment, os_page_size);
   if (image->vk.create_flags & VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT)
      alignment = 65536;

   /* Only expose the lazy memory type for images with TRANSIENT_ATTACHMENT
    * usage.
    */
   uint32_t type_count =
      (image->vk.usage & VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT) ?
      dev->physical_device->memory.type_count :
      dev->physical_device->memory.non_lazy_type_count;

   pMemoryRequirements->memoryRequirements = (VkMemoryRequirements) {
      /* Due to how we fake the sparse tile size, the real size may not be
       * aligned. CTS doesn't like this, and real apps may also be surprised,
       * so we align it.
       */
      .size = align64(image->total_size, alignment),
      .alignment = alignment,
      .memoryTypeBits = (1 << type_count) - 1,
   };

   vk_foreach_struct(ext, pMemoryRequirements->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS: {
         VkMemoryDedicatedRequirements *req =
            (VkMemoryDedicatedRequirements *) ext;
         req->requiresDedicatedAllocation =
            image->vk.external_handle_types != 0;
         req->prefersDedicatedAllocation = req->requiresDedicatedAllocation;
         break;
      }
      default:
         break;
      }
   }
}


static VkSparseImageFormatProperties
tu_fill_sparse_image_fmt_props(VkImageAspectFlags aspects,
                               const enum pipe_format format,
                               VkSampleCountFlags samples)
{
   uint32_t width, height;
   fdl_get_sparse_block_size(format, samples, &width, &height);

   VkSparseImageFormatProperties sparse_format_props = {
      .aspectMask = aspects,
      .imageGranularity = {
         .width = width * util_format_get_blockwidth(format),
         .height = height * util_format_get_blockheight(format),
         .depth = 1,
      },
      .flags = 0,
   };

   return sparse_format_props;
}

VKAPI_ATTR void VKAPI_CALL
tu_GetPhysicalDeviceSparseImageFormatProperties2(
    VkPhysicalDevice physicalDevice,
    const VkPhysicalDeviceSparseImageFormatInfo2* pFormatInfo,
    uint32_t *pPropertyCount,
    VkSparseImageFormatProperties2 *pProperties)
{
   VkResult result;

   /* Check if the given format info is valid first before returning sparse
    * props.  The easiest way to do this is to just call
    * tu_GetPhysicalDeviceImageFormatProperties2()
    */
   const VkPhysicalDeviceImageFormatInfo2 img_fmt_info = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
      .format = pFormatInfo->format,
      .type = pFormatInfo->type,
      .tiling = pFormatInfo->tiling,
      .usage = pFormatInfo->usage,
      .flags = VK_IMAGE_CREATE_SPARSE_BINDING_BIT |
               VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT,
   };

   VkImageFormatProperties2 img_fmt_props2 = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,
      .pNext = NULL,
   };

   result = tu_GetPhysicalDeviceImageFormatProperties2(physicalDevice,
                                                       &img_fmt_info,
                                                       &img_fmt_props2);
   if (result != VK_SUCCESS) {
      *pPropertyCount = 0;
      return;
   }

   const VkImageFormatProperties *props = &img_fmt_props2.imageFormatProperties;
   if (!(pFormatInfo->samples & props->sampleCounts)) {
      *pPropertyCount = 0;
      return;
   }

   /* We should already reject non-2D images */
   assert(pFormatInfo->type == VK_IMAGE_TYPE_2D);

   VK_OUTARRAY_MAKE_TYPED(VkSparseImageFormatProperties2, out,
                          pProperties, pPropertyCount);

   VkImageAspectFlags aspects = vk_format_aspects(pFormatInfo->format);
   const enum pipe_format pipe_format =
      vk_format_to_pipe_format(pFormatInfo->format);

   if (pFormatInfo->format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
      u_foreach_bit (aspect, aspects) {
         enum pipe_format aspect_format =
            tu6_plane_format(pFormatInfo->format, aspect);
         vk_outarray_append_typed(VkSparseImageFormatProperties2, &out, props) {
            props->properties =
               tu_fill_sparse_image_fmt_props(aspect, aspect_format,
                                              pFormatInfo->samples);
         }
      }
   } else {
      vk_outarray_append_typed(VkSparseImageFormatProperties2, &out, props) {
         props->properties = tu_fill_sparse_image_fmt_props(aspects, pipe_format,
                                                            pFormatInfo->samples);
      }
   }
}

static VkSparseImageMemoryRequirements
tu_fill_sparse_image_memory_reqs(const struct fdl_layout *layout,
                                 VkImageAspectFlags aspects)
{
   VkSparseImageFormatProperties sparse_format_props =
      tu_fill_sparse_image_fmt_props(aspects,
                                     layout->format,
                                     layout->nr_samples);

   VkSparseImageMemoryRequirements sparse_memory_reqs = {
      .formatProperties = sparse_format_props,
      .imageMipTailFirstLod = layout->mip_tail_first_lod,
      .imageMipTailSize = fdl_sparse_miptail_size(layout),
      .imageMipTailOffset = fdl_sparse_miptail_offset(layout),
      .imageMipTailStride = layout->layer_size,
   };

   return sparse_memory_reqs;
}

static void
tu_get_image_sparse_memory_requirements(
   struct tu_device *dev,
   struct tu_image *image,
   uint32_t *pSparseMemoryRequirementCount,
   VkSparseImageMemoryRequirements2 *pMemoryRequirements)
{
   VK_OUTARRAY_MAKE_TYPED(VkSparseImageMemoryRequirements2, out,
                          pMemoryRequirements, pSparseMemoryRequirementCount);

   /* From the Vulkan 1.3.279 spec:
    *
    *    "The sparse image must have been created using the
    *    VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT flag to retrieve valid sparse
    *    image memory requirements."
    */
   if (!(image->vk.create_flags & VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT))
      return;

   if (image->vk.format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
      u_foreach_bit (aspect, image->vk.aspects) {
         const struct fdl_layout *layout =
            &image->layout[tu6_plane_index(image->vk.format, aspect)];
         vk_outarray_append_typed(VkSparseImageMemoryRequirements2, &out, reqs) {
            reqs->memoryRequirements =
               tu_fill_sparse_image_memory_reqs(layout, aspect);
         };
      }
   } else {
      vk_outarray_append_typed(VkSparseImageMemoryRequirements2, &out, reqs) {
         reqs->memoryRequirements =
            tu_fill_sparse_image_memory_reqs(&image->layout[0],
                                             image->vk.aspects);
      };
   }
}

VKAPI_ATTR void VKAPI_CALL
tu_GetImageMemoryRequirements2(VkDevice _device,
                               const VkImageMemoryRequirementsInfo2 *pInfo,
                               VkMemoryRequirements2 *pMemoryRequirements)
{
   VK_FROM_HANDLE(tu_device, device, _device);
   VK_FROM_HANDLE(tu_image, image, pInfo->image);

   tu_get_image_memory_requirements(device, image, pMemoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL
tu_GetImageSparseMemoryRequirements2(
   VkDevice _device,
   const VkImageSparseMemoryRequirementsInfo2 *pInfo,
   uint32_t *pSparseMemoryRequirementCount,
   VkSparseImageMemoryRequirements2 *pSparseMemoryRequirements)
{
   VK_FROM_HANDLE(tu_device, device, _device);
   VK_FROM_HANDLE(tu_image, image, pInfo->image);

   tu_get_image_sparse_memory_requirements(device, image,
                                           pSparseMemoryRequirementCount,
                                           pSparseMemoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL
tu_GetDeviceImageMemoryRequirements(
   VkDevice _device,
   const VkDeviceImageMemoryRequirements *pInfo,
   VkMemoryRequirements2 *pMemoryRequirements)
{
   VK_FROM_HANDLE(tu_device, device, _device);

   struct tu_image image = {0};

   vk_image_init(&device->vk, &image.vk, pInfo->pCreateInfo);
   tu_image_init(device, &image, pInfo->pCreateInfo);
   TU_CALLX(device, tu_image_update_layout)(device, &image, DRM_FORMAT_MOD_INVALID, NULL);

   tu_get_image_memory_requirements(device, &image, pMemoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL
tu_GetDeviceImageSparseMemoryRequirements(
    VkDevice _device,
    const VkDeviceImageMemoryRequirements *pInfo,
    uint32_t *pSparseMemoryRequirementCount,
    VkSparseImageMemoryRequirements2 *pSparseMemoryRequirements)
{
   VK_FROM_HANDLE(tu_device, device, _device);

   struct tu_image image = {0};

   vk_image_init(&device->vk, &image.vk, pInfo->pCreateInfo);
   tu_image_init(device, &image, pInfo->pCreateInfo);
   TU_CALLX(device, tu_image_update_layout)(device, &image, DRM_FORMAT_MOD_INVALID, NULL);

   tu_get_image_sparse_memory_requirements(device, &image,
                                           pSparseMemoryRequirementCount,
                                           pSparseMemoryRequirements);
}

static void
tu_get_image_subresource_layout(struct tu_image *image,
                                const VkImageSubresource2KHR *pSubresource,
                                VkSubresourceLayout2KHR *pLayout)
{
   struct fdl_layout *layout =
      &image->layout[tu6_plane_index(image->vk.format,
                                     pSubresource->imageSubresource.aspectMask)];
   const struct fdl_slice *slice = layout->slices +
      pSubresource->imageSubresource.mipLevel;

   pLayout->subresourceLayout.offset =
      fdl_surface_offset(layout, pSubresource->imageSubresource.mipLevel,
                         pSubresource->imageSubresource.arrayLayer);
   pLayout->subresourceLayout.rowPitch =
      fdl_pitch(layout, pSubresource->imageSubresource.mipLevel);
   pLayout->subresourceLayout.arrayPitch =
      fdl_layer_stride(layout, pSubresource->imageSubresource.mipLevel);
   pLayout->subresourceLayout.depthPitch = slice->size0;
   pLayout->subresourceLayout.size = slice->size0 * layout->depth0;

   VkSubresourceHostMemcpySizeEXT *memcpy_size =
      vk_find_struct(pLayout, SUBRESOURCE_HOST_MEMCPY_SIZE_EXT);
   if (memcpy_size) {
      memcpy_size->size = slice->size0;
   }

   if (fdl_ubwc_enabled(layout, pSubresource->imageSubresource.mipLevel)) {
      /* UBWC starts at offset 0 */
      pLayout->subresourceLayout.offset = 0;
      /* UBWC scanout won't match what the kernel wants if we have levels/layers */
      assert(image->vk.mip_levels == 1 && image->vk.array_layers == 1);
   }
}

VKAPI_ATTR void VKAPI_CALL
tu_GetImageSubresourceLayout2KHR(VkDevice _device,
                                 VkImage _image,
                                 const VkImageSubresource2KHR *pSubresource,
                                 VkSubresourceLayout2KHR *pLayout)
{
   VK_FROM_HANDLE(tu_image, image, _image);

   tu_get_image_subresource_layout(image, pSubresource, pLayout);
}

VKAPI_ATTR void VKAPI_CALL
tu_GetDeviceImageSubresourceLayoutKHR(VkDevice _device,
                                      const VkDeviceImageSubresourceInfoKHR *pInfo,
                                      VkSubresourceLayout2KHR *pLayout)
{
   VK_FROM_HANDLE(tu_device, device, _device);

   struct tu_image image = {0};

   vk_image_init(&device->vk, &image.vk, pInfo->pCreateInfo);
   tu_image_init(device, &image, pInfo->pCreateInfo);
   TU_CALLX(device, tu_image_update_layout)(device, &image, DRM_FORMAT_MOD_INVALID, NULL);

   tu_get_image_subresource_layout(&image, pInfo->pSubresource, pLayout);
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_CreateImageView(VkDevice _device,
                   const VkImageViewCreateInfo *pCreateInfo,
                   const VkAllocationCallbacks *pAllocator,
                   VkImageView *pView)
{
   VK_FROM_HANDLE(tu_device, device, _device);
   struct tu_image_view *view;

   view = (struct tu_image_view *) vk_object_alloc(
      &device->vk, pAllocator, sizeof(*view), VK_OBJECT_TYPE_IMAGE_VIEW);
   if (view == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   tu_image_view_init(device, view, pCreateInfo);

   *pView = tu_image_view_to_handle(view);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
tu_DestroyImageView(VkDevice _device,
                    VkImageView _iview,
                    const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(tu_device, device, _device);
   VK_FROM_HANDLE(tu_image_view, iview, _iview);

   if (!iview)
      return;

   vk_object_free(&device->vk, pAllocator, iview);
}

/* Impelements the operations described in "Fragment Density Map Operations."
 */
void
tu_fragment_density_map_sample(const struct tu_image_view *fdm,
                               int32_t x, int32_t y,
                               uint32_t width, uint32_t height,
                               uint32_t layer,
                               struct tu_frag_area *area)
{
   assert(fdm->image->layout[0].tile_mode == TILE6_LINEAR);

   uint32_t fdm_shift_x = util_logbase2_ceil(DIV_ROUND_UP(width, fdm->vk.extent.width));
   uint32_t fdm_shift_y = util_logbase2_ceil(DIV_ROUND_UP(height, fdm->vk.extent.height));

   fdm_shift_x = CLAMP(fdm_shift_x, MIN_FDM_TEXEL_SIZE_LOG2, MAX_FDM_TEXEL_SIZE_LOG2);
   fdm_shift_y = CLAMP(fdm_shift_y, MIN_FDM_TEXEL_SIZE_LOG2, MAX_FDM_TEXEL_SIZE_LOG2);

   int32_t i = x >> fdm_shift_x;
   int32_t j = y >> fdm_shift_y;

   i = CLAMP(i, 0, fdm->vk.extent.width - 1);
   j = CLAMP(j, 0, fdm->vk.extent.height - 1);

   unsigned cpp = fdm->image->layout[0].cpp;
   unsigned pitch = fdm->view.pitch;

   void *pixel = (char *)fdm->image->map + fdm->view.offset + fdm->view.layer_size * layer + cpp * i + pitch * j;
   float density_src[4], density[4];
   util_format_unpack_rgba(fdm->view.format, density_src, pixel, 1);
   pipe_swizzle_4f(density, density_src, fdm->swizzle);
   area->width = 1.0f / density[0];
   area->height = 1.0f / density[1];
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_GetImageOpaqueCaptureDescriptorDataEXT(VkDevice device,
                                          const VkImageCaptureDescriptorDataInfoEXT *pInfo,
                                          void *pData)
{
   VK_FROM_HANDLE(tu_image, image, pInfo->image);

   /* Save the image iova so that when replaying sparse images have a
    * consistent iova and therefore consistent descriptor contents.
    */
   *(uint64_t *)pData = image->iova;
   return VK_SUCCESS;
}

/* The native macrotile size is 4K, and the page size is also 4K, so the
 * most natural thing would be to expose 4K tiles. But that isn't compatible
 * with D3D requirements, so we have to emulate 64K "sparse tiles" on the
 * native 4K macrotiles.
 *
 * Each "sparse tile" contains macrotiles in the natural linear order when
 * viewed in terms of the (bank swizzled) image coordinates. We have to do
 * this in order to guarantee that aliasing tiles in different images works.
 * For example, if cpp=16, then the UBWC 4K macrotile is 16x16 pixels, while
 * the sparse 64K tile is 64x64 pixels. That means each sparse tile contains
 * 4x4 macrotiles. Then the 64K tile at (0, 0) is mapped like this to the
 * image:
 * 
 *      |--16px---|
 *  -   -----------------------------------------------
 *  |   |         |         |         |         |
 * 16px | Tile 0  | Tile 1  | Tile 2  | Tile 3  | . . . 
 *  |   |         |         |         |         |
 *  -   -----------------------------------------------
 *      |         |         |         |         |
 *      | Tile 4  | Tile 5  | Tile 6  | Tile 7  | . . .
 *      |         |         |         |         |
 *      -----------------------------------------------
 *      |         |         |         |         |
 *      | Tile 8  | Tile 9  | Tile 10 | Tile 11 | . . .
 *      |         |         |         |         |
 *      -----------------------------------------------
 *      |         |         |         |         |
 *      | Tile 12 | Tile 13 | Tile 14 | Tile 15 | . . .
 *      |         |         |         |         |
 *      -----------------------------------------------
 *      |    .    |    .    |    .    |   .     | .
 *      |    .    |    .    |    .    |   .     |   .
 *      |    .    |    .    |    .    |   .     |     .
 *
 * One tricky case is when the stride isn't aligned to the sparse tile width,
 * or the height isn't aligned to the sparse height: at the bottom or left
 * edges there may be macrotiles inside the sparse tile that overhang the
 * image and don't have any corresponding backing memory, and we have to skip
 * mapping/unmapping those.
 *
 * When doing the mapping, we have to be aware of bank swizzling. It may
 * reorder macrotiles inside a sparse tile, or it may reorder sparse tiles, or
 * both, depending on the highest bank bit, bank swizzling levels, and
 * alignment. We cannot ignore the bank swizzling even in the first case,
 * where it only reorders macrotiles inside the sparse tile, to ensure that
 * aliasing (i.e. remapping the same sparse tile into a different image) works
 * because different images have different alignments and therefore different
 * bank swizzling.
 */

void
tu_bind_sparse_image(struct tu_device *device, void *submit,
                     struct tu_image *image,
                     const VkSparseImageMemoryBind *bind)
{
   VK_FROM_HANDLE(tu_device_memory, mem, bind->memory);
   const struct fdl_layout *layout =
      &image->layout[tu6_plane_index(image->vk.format,
                                     bind->subresource.aspectMask)];
   struct tu_bo *bo = mem ? mem->bo : NULL;
   uint64_t bo_offset = mem ? bind->memoryOffset : 0;
   uint32_t sparse_width, sparse_height;
   uint32_t macrotile_width, macrotile_height;
   fdl_get_sparse_block_size(layout->format,
                             layout->nr_samples,
                             &sparse_width, &sparse_height);
   fdl6_get_ubwc_macrotile_size(layout,
                                &macrotile_width, &macrotile_height);
   assert(sparse_width % macrotile_width == 0);
   assert(sparse_height % macrotile_height == 0);

   uint32_t blockwidth = util_format_get_blockwidth(layout->format);
   uint32_t blockheight = util_format_get_blockheight(layout->format);
   uint32_t x_start = bind->offset.x / blockwidth;
   uint32_t x_end = DIV_ROUND_UP(bind->offset.x + bind->extent.width,
                                 blockwidth);
   uint32_t y_start = bind->offset.y / blockheight;
   uint32_t y_end = DIV_ROUND_UP(bind->offset.y + bind->extent.height,
                                 blockheight);
   uint32_t cpp = layout->cpp;
   uint32_t pitch = fdl_pitch(layout, bind->subresource.mipLevel);
   uint64_t image_offset = fdl_surface_offset(layout,
                                              bind->subresource.mipLevel,
                                              bind->subresource.arrayLayer);
   uint32_t bank_mask = fdl6_get_bank_mask(layout,
                                           bind->subresource.mipLevel,
                                           &device->physical_device->ubwc_config);
   uint32_t bank_shift =
      fdl6_get_bank_shift(&device->physical_device->ubwc_config);

   /* Our y offset is in pixels */
   bank_mask *= macrotile_height;
   bank_shift -= util_logbase2(macrotile_height);

   uint64_t prev_image_offset = 0;
   uint64_t prev_bo_offset = 0;
   uint64_t bind_range = 0;

   for (unsigned sy = y_start; sy < y_end; sy += sparse_height) {
      for (unsigned sx = x_start; sx < x_end; sx += sparse_width,
           bo_offset += 65536) {
         uint64_t row_bo_offset = bo_offset;
         for (unsigned ty = sy; ty < MIN2(sy + sparse_height, y_end);
              ty += macrotile_height,
              row_bo_offset += sparse_width * macrotile_height * cpp) {
            uint64_t row_image_offset = image_offset + pitch * ty;
            uint32_t x_swizzle = (ty & bank_mask) << bank_shift;
            uint64_t column_bo_offset = row_bo_offset;
            for (unsigned tx = sx; tx < MIN2(sx + sparse_width, x_end);
                 tx += macrotile_width, column_bo_offset += 4096) {
               uint64_t image_offset =
                  ((tx * macrotile_height * cpp) ^ x_swizzle) + row_image_offset;

               /* Try to combine consecutive binds. In most cases, depending
                * on the x_swizzle, we should be able to map the whole row of
                * the sparse tile at once.
                */
               if (!bind_range) {
                  prev_image_offset = image_offset;
                  prev_bo_offset = bo ? column_bo_offset : 0;
                  bind_range = 4096;
               } else if (prev_image_offset + bind_range == image_offset &&
                          (!bo || prev_bo_offset + bind_range == bo_offset)) {
                  bind_range += 4096;
               } else {
                  tu_submit_add_bind(device, submit, &image->vma,
                                     prev_image_offset, bo, prev_bo_offset,
                                     bind_range);
                  prev_image_offset = image_offset;
                  prev_bo_offset = bo ? column_bo_offset : 0;
                  bind_range = 4096;
               }
            }
         }
      }
   }

   if (bind_range) {
      tu_submit_add_bind(device, submit, &image->vma, prev_image_offset, bo,
                         prev_bo_offset, bind_range);
   }
}

