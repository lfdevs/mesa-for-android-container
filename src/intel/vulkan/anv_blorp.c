/*
 * Copyright © 2016 Intel Corporation
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

#include "anv_private.h"
#include "compiler/brw/brw_disasm.h"
#include "genxml/gen80_pack.h"

static bool
lookup_blorp_shader(struct blorp_batch *batch,
                    const void *key, uint32_t key_size,
                    uint32_t *kernel_out, void *prog_data_out)
{
   struct blorp_context *blorp = batch->blorp;
   struct anv_device *device = blorp->driver_ctx;

   struct anv_shader_internal *bin =
      anv_device_search_for_kernel(device, device->internal_cache,
                                   key, key_size, NULL);
   if (!bin)
      return false;

   /* The cache already has a reference and it's not going anywhere so there
    * is no need to hold a second reference.
    */
   anv_shader_internal_unref(device, bin);

   *kernel_out = bin->kernel.offset;
   *(const struct brw_stage_prog_data **)prog_data_out = bin->prog_data;

   return true;
}

static bool
upload_blorp_shader(struct blorp_batch *batch, uint32_t stage,
                    const void *key, uint32_t key_size,
                    const void *kernel, uint32_t kernel_size,
                    const void *prog_data,
                    uint32_t prog_data_size,
                    uint32_t *kernel_out, void *prog_data_out)
{
   struct blorp_context *blorp = batch->blorp;
   struct anv_device *device = blorp->driver_ctx;

   struct anv_pipeline_bind_map empty_bind_map = {};
   struct anv_push_descriptor_info empty_push_desc_info = {};
   struct anv_shader_upload_params upload_params = {
      .stage               = stage,
      .key_data            = key,
      .key_size            = key_size,
      .kernel_data         = kernel,
      .kernel_size         = kernel_size,
      .prog_data           = prog_data,
      .prog_data_size      = prog_data_size,
      .bind_map            = &empty_bind_map,
      .push_desc_info      = &empty_push_desc_info,
   };

   struct anv_shader_internal *bin =
      anv_device_upload_kernel(device, device->internal_cache, &upload_params);

   if (!bin)
      return false;

   /* The cache already has a reference and it's not going anywhere so there
    * is no need to hold a second reference.
    */
   anv_shader_internal_unref(device, bin);

   if (INTEL_DEBUG(DEBUG_SHADERS_LINENO)) {
      /* shader hash is zero in this context */
      if (!intel_shader_dump_filter) {
         brw_disassemble_with_lineno(&device->physical->compiler->isa,
                                     stage, -1, 0, kernel, 0,
                                     bin->kernel.offset, stderr);
      }
   }

   *kernel_out = bin->kernel.offset;
   *(const struct brw_stage_prog_data **)prog_data_out = bin->prog_data;

   return true;
}

static void
upload_dynamic_state(struct blorp_context *context,
                     const void *data, uint32_t size,
                     uint32_t alignment, enum blorp_dynamic_state name)
{
   struct anv_device *device = context->driver_ctx;

   device->blorp.dynamic_states[name] =
      anv_state_pool_emit_data(&device->dynamic_state_pool,
                               size, alignment, data);
}

static nir_shader *
get_fp64_nir(struct blorp_context *context)
{
   struct anv_device *device = context->driver_ctx;
   return device->fp64_nir;
}

void
anv_device_init_blorp(struct anv_device *device)
{
   const struct blorp_config config = {
      .use_mesh_shading = device->vk.enabled_extensions.EXT_mesh_shader,
      .use_unrestricted_depth_range =
         device->vk.enabled_extensions.EXT_depth_range_unrestricted,
      .use_cached_dynamic_states = true,
   };

   blorp_init_brw(&device->blorp.context, device, &device->isl_dev,
                  device->physical->compiler, &config);
   device->blorp.context.get_fp64_nir = get_fp64_nir;
   device->blorp.context.lookup_shader = lookup_blorp_shader;
   device->blorp.context.upload_shader = upload_blorp_shader;
   device->blorp.context.enable_tbimr = device->physical->instance->enable_tbimr;
   device->blorp.context.exec = anv_genX(device->info, blorp_exec);
   device->blorp.context.upload_dynamic_state = upload_dynamic_state;

   anv_genX(device->info, blorp_init_dynamic_states)(&device->blorp.context);
}

void
anv_device_finish_blorp(struct anv_device *device)
{
#ifdef HAVE_VALGRIND
   /* We only need to free these to prevent valgrind errors.  The backing
    * BO will go away in a couple of lines so we don't actually leak.
    */
   for (uint32_t i = 0; i < ARRAY_SIZE(device->blorp.dynamic_states); i++) {
      anv_state_pool_free(&device->dynamic_state_pool,
                          device->blorp.dynamic_states[i]);
   }
#endif
   blorp_finish(&device->blorp.context);
}

static void
anv_blorp_batch_init(struct anv_cmd_buffer *cmd_buffer,
                     struct blorp_batch *batch, enum blorp_batch_flags flags)
{
   VkQueueFlags queue_flags = cmd_buffer->queue_family->queueFlags;

   if (queue_flags & VK_QUEUE_GRAPHICS_BIT) {
      /* blorp runs on render engine by default */
   } else if (queue_flags & VK_QUEUE_COMPUTE_BIT) {
      flags |= BLORP_BATCH_USE_COMPUTE;
   } else if (queue_flags & VK_QUEUE_TRANSFER_BIT) {
      flags |= BLORP_BATCH_USE_BLITTER;
   } else {
      UNREACHABLE("unknown queue family");
   }

   /* Can't have both flags at the same time. */
   assert((flags & BLORP_BATCH_USE_BLITTER) == 0 ||
          (flags & BLORP_BATCH_USE_COMPUTE) == 0);

   /* If blorp needs a VS shader, we can't have the component packing of the
    * driver interfere with blorp's shader.
    */
   flags |= BLORP_BATCH_EMIT_3DSTATE_VF;

   if (!cmd_buffer->device->physical->instance->enable_vf_distribution)
      flags |= BLORP_BATCH_DISABLE_VF_DISTRIBUTION;

   if (cmd_buffer->batch.engine_class == INTEL_ENGINE_CLASS_COMPUTE)
      flags |= BLORP_BATCH_COMPUTE_ENGINE;

   blorp_batch_init(&cmd_buffer->device->blorp.context, batch, cmd_buffer, flags);
}

static void
anv_blorp_batch_finish(struct blorp_batch *batch)
{
   blorp_batch_finish(batch);
}

static isl_surf_usage_flags_t
get_usage_flag_for_cmd_buffer(const struct anv_cmd_buffer *cmd_buffer,
                              bool is_dest, bool is_depth, bool protected)
{
   isl_surf_usage_flags_t usage;

   switch (cmd_buffer->queue_family->engine_class) {
   case INTEL_ENGINE_CLASS_RENDER:
      if (is_dest) {
         /* Make the blorp operation match the MOCS used in
          * cmd_buffer_emit_depth_stencil()
          */
         if (is_depth)
            usage = ISL_SURF_USAGE_DEPTH_BIT;
         else
            usage = ISL_SURF_USAGE_RENDER_TARGET_BIT;
      } else {
         usage = ISL_SURF_USAGE_TEXTURE_BIT;
      }
      break;
   case INTEL_ENGINE_CLASS_COMPUTE:
      usage = is_dest ? ISL_SURF_USAGE_STORAGE_BIT :
                        ISL_SURF_USAGE_TEXTURE_BIT;
      break;
   case INTEL_ENGINE_CLASS_COPY:
      usage = is_dest ? ISL_SURF_USAGE_BLITTER_DST_BIT :
                        ISL_SURF_USAGE_BLITTER_SRC_BIT;
      break;
   default:
      UNREACHABLE("Unhandled engine class");
   }

   if (protected)
      usage |= ISL_SURF_USAGE_PROTECTED_BIT;

   return usage;
}

static void
get_blorp_surf_for_anv_address(struct anv_cmd_buffer *cmd_buffer,
                               struct anv_address address,
                               uint32_t width, uint32_t height,
                               uint32_t row_pitch, enum isl_format format,
                               bool is_dest,
                               struct blorp_surf *blorp_surf,
                               struct isl_surf *isl_surf)
{
   bool ok UNUSED;
   isl_surf_usage_flags_t usage =
      get_usage_flag_for_cmd_buffer(cmd_buffer, is_dest, false,
                                    address.protected);

   *blorp_surf = (struct blorp_surf) {
      .surf = isl_surf,
      .addr = {
         .buffer = address.bo,
         .offset = address.offset,
         .mocs = anv_mocs(cmd_buffer->device, address.bo, usage),
      },
   };

   ok = isl_surf_init(&cmd_buffer->device->isl_dev, isl_surf,
                     .dim = ISL_SURF_DIM_2D,
                     .format = format,
                     .width = width,
                     .height = height,
                     .depth = 1,
                     .levels = 1,
                     .array_len = 1,
                     .samples = 1,
                     .row_pitch_B = row_pitch,
                     .usage = usage,
                     .tiling_flags = ISL_TILING_LINEAR_BIT);
   assert(ok);
}

static void
get_blorp_surf_for_anv_buffer(struct anv_cmd_buffer *cmd_buffer,
                              struct anv_buffer *buffer, uint64_t offset,
                              uint32_t width, uint32_t height,
                              uint32_t row_pitch, enum isl_format format,
                              bool is_dest,
                              struct blorp_surf *blorp_surf,
                              struct isl_surf *isl_surf)
{
   get_blorp_surf_for_anv_address(cmd_buffer,
                                  anv_address_add(buffer->address, offset),
                                  width, height, row_pitch, format,
                                  is_dest, blorp_surf, isl_surf);
}

/* Pick something high enough that it won't be used in core and low enough it
 * will never map to an extension.
 */
#define ANV_IMAGE_LAYOUT_EXPLICIT_AUX (VkImageLayout)10000000

static struct blorp_address
anv_to_blorp_address(struct anv_address addr)
{
   return (struct blorp_address) {
      .buffer = addr.bo,
      .offset = addr.offset,
   };
}

static void
get_blorp_surf_for_anv_image(const struct anv_cmd_buffer *cmd_buffer,
                             const struct anv_image *image,
                             VkImageAspectFlags aspect,
                             VkImageUsageFlags usage,
                             VkImageLayout layout,
                             enum isl_aux_usage aux_usage,
                             enum isl_format view_fmt,
                             bool cross_aspect,
                             struct blorp_surf *blorp_surf)
{
   const struct anv_device *device = cmd_buffer->device;
   const uint32_t plane = anv_image_aspect_to_plane(image, aspect);

   if (layout != ANV_IMAGE_LAYOUT_EXPLICIT_AUX) {
      assert(usage != 0);
      aux_usage = anv_layout_to_aux_usage(device->info, image,
                                          aspect, usage, layout,
                                          cmd_buffer->queue_family->queueFlags);
   }

   const bool is_dest = usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT;
   isl_surf_usage_flags_t isl_usage =
      get_usage_flag_for_cmd_buffer(cmd_buffer, is_dest,
                                    aspect & VK_IMAGE_ASPECT_DEPTH_BIT,
                                    anv_image_is_protected(image));
   const struct anv_surface *surface = &image->planes[plane].primary_surface;
   const struct anv_address address =
      anv_image_address(image, &surface->memory_range);

   *blorp_surf = (struct blorp_surf) {
      .surf = &surface->isl,
      .addr = {
         .buffer = address.bo,
         .offset = address.offset,
         .mocs = anv_mocs(device, address.bo, isl_usage),
      },
   };

   if (aux_usage != ISL_AUX_USAGE_NONE) {
      const struct anv_surface *aux_surface = &image->planes[plane].aux_surface;
      const struct anv_address aux_address =
         anv_image_address(image, &aux_surface->memory_range);

      blorp_surf->aux_usage = aux_usage;
      blorp_surf->aux_surf = &aux_surface->isl;

      if (!anv_address_is_null(aux_address)) {
         blorp_surf->aux_addr = (struct blorp_address) {
            .buffer = aux_address.bo,
            .offset = aux_address.offset,
            .mocs = anv_mocs(device, aux_address.bo, isl_usage),
         };
      }

      const struct anv_address clear_color_addr =
         anv_image_get_clear_color_addr(
            device, image, cross_aspect ? ISL_FORMAT_RAW : view_fmt, aspect,
            !is_dest);
      blorp_surf->clear_color_addr = anv_to_blorp_address(clear_color_addr);

      if (aspect & VK_IMAGE_ASPECT_DEPTH_BIT)
         blorp_surf->clear_color = anv_image_hiz_clear_value(image);
   }
}

static void
copy_image(struct anv_cmd_buffer *cmd_buffer,
           struct blorp_batch *batch,
           const struct anv_image *src_image,
           VkImageLayout src_image_layout,
           const struct anv_image *dst_image,
           VkImageLayout dst_image_layout,
           const VkImageCopy2 *region)
{
   VkOffset3D srcOffset =
      vk_image_sanitize_offset(&src_image->vk, region->srcOffset);
   VkOffset3D dstOffset =
      vk_image_sanitize_offset(&dst_image->vk, region->dstOffset);
   VkExtent3D extent =
      vk_image_sanitize_extent(&src_image->vk, region->extent);

   const uint32_t dst_level = region->dstSubresource.mipLevel;
   unsigned dst_base_layer, layer_count;
   if (dst_image->vk.image_type == VK_IMAGE_TYPE_3D) {
      dst_base_layer = region->dstOffset.z;
      layer_count = region->extent.depth;
   } else {
      dst_base_layer = region->dstSubresource.baseArrayLayer;
      layer_count = vk_image_subresource_layer_count(&dst_image->vk,
                                                     &region->dstSubresource);
   }

   const uint32_t src_level = region->srcSubresource.mipLevel;
   unsigned src_base_layer;
   if (src_image->vk.image_type == VK_IMAGE_TYPE_3D) {
      src_base_layer = region->srcOffset.z;
   } else {
      src_base_layer = region->srcSubresource.baseArrayLayer;
      assert(layer_count ==
             vk_image_subresource_layer_count(&src_image->vk,
                                              &region->srcSubresource));
   }

   VkImageAspectFlags src_mask = region->srcSubresource.aspectMask,
      dst_mask = region->dstSubresource.aspectMask;

   const bool cross_aspect = !anv_image_aspects_compatible(src_mask, dst_mask);

   if (util_bitcount(src_mask) > 1) {
      anv_foreach_image_aspect_bit(aspect_bit, src_image, src_mask) {
         enum isl_format src_format, dst_format;
         int plane = anv_image_aspect_to_plane(src_image, 1UL << aspect_bit);
         blorp_copy_get_formats(&cmd_buffer->device->isl_dev,
                                &src_image->planes[plane].primary_surface.isl,
                                &dst_image->planes[plane].primary_surface.isl,
                                &src_format, &dst_format);

         struct blorp_surf src_surf, dst_surf;
         get_blorp_surf_for_anv_image(cmd_buffer,
                                      src_image, 1UL << aspect_bit,
                                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                      src_image_layout, ISL_AUX_USAGE_NONE,
                                      src_format, cross_aspect, &src_surf);
         get_blorp_surf_for_anv_image(cmd_buffer,
                                      dst_image, 1UL << aspect_bit,
                                      VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                      dst_image_layout, ISL_AUX_USAGE_NONE,
                                      dst_format, cross_aspect, &dst_surf);
         anv_cmd_buffer_mark_image_written(cmd_buffer, dst_image,
                                           1UL << aspect_bit,
                                           dst_surf.aux_usage, dst_level,
                                           dst_base_layer, layer_count);

         for (unsigned i = 0; i < layer_count; i++) {
            blorp_copy(batch, &src_surf, src_level, src_base_layer + i,
                       &dst_surf, dst_level, dst_base_layer + i,
                       srcOffset.x, srcOffset.y,
                       dstOffset.x, dstOffset.y,
                       extent.width, extent.height);
         }
      }
   } else {
      /* This case handles the ycbcr images, aspect mask are compatible but
       * don't need to be the same.
       */
      enum isl_format src_format, dst_format;
      int s_plane = anv_image_aspect_to_plane(src_image, src_mask);
      int d_plane = anv_image_aspect_to_plane(dst_image, dst_mask);
      blorp_copy_get_formats(&cmd_buffer->device->isl_dev,
                             &src_image->planes[s_plane].primary_surface.isl,
                             &dst_image->planes[d_plane].primary_surface.isl,
                             &src_format, &dst_format);

      struct blorp_surf src_surf, dst_surf;
      get_blorp_surf_for_anv_image(cmd_buffer, src_image, src_mask,
                                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                   src_image_layout, ISL_AUX_USAGE_NONE,
                                   src_format, cross_aspect, &src_surf);
      get_blorp_surf_for_anv_image(cmd_buffer, dst_image, dst_mask,
                                   VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                   dst_image_layout, ISL_AUX_USAGE_NONE,
                                   dst_format, cross_aspect, &dst_surf);
      anv_cmd_buffer_mark_image_written(cmd_buffer, dst_image, dst_mask,
                                        dst_surf.aux_usage, dst_level,
                                        dst_base_layer, layer_count);

      for (unsigned i = 0; i < layer_count; i++) {
         blorp_copy(batch, &src_surf, src_level, src_base_layer + i,
                    &dst_surf, dst_level, dst_base_layer + i,
                    srcOffset.x, srcOffset.y,
                    dstOffset.x, dstOffset.y,
                    extent.width, extent.height);
      }
   }
}

static bool
anv_blorp_blitter_execute_on_companion(struct anv_cmd_buffer *cmd_buffer,
                                       const struct anv_image *image,
                                       uint32_t region_count,
                                       const VkBufferImageCopy2* regions)
{
   if (!anv_cmd_buffer_is_blitter_queue(cmd_buffer))
      return false;

   bool blorp_execute_on_companion = false;

   for (unsigned r = 0; r < region_count && !blorp_execute_on_companion; r++) {
      VkImageAspectFlags aspect_mask = regions[r].imageSubresource.aspectMask;

      enum isl_format linear_format =
         anv_get_isl_format(cmd_buffer->device->physical, image->vk.format,
                            aspect_mask, VK_IMAGE_TILING_LINEAR);
      const struct isl_format_layout *linear_fmtl =
         isl_format_get_layout(linear_format);

      switch (linear_fmtl->bpb) {
      case 96:
         /* We can only support linear mode for 96bpp on blitter engine. */
         blorp_execute_on_companion |=
            image->vk.tiling != VK_IMAGE_TILING_LINEAR;
         break;
      default:
         blorp_execute_on_companion |= linear_fmtl->bpb % 3 == 0;
         break;
      }
   }

   return blorp_execute_on_companion;
}

static bool
is_image_multisampled(const struct anv_image *image)
{
   return image->vk.samples > 1;
}

static bool
is_image_emulated(const struct anv_image *image)
{
   return image->emu_plane_format != VK_FORMAT_UNDEFINED;
}

static bool
is_image_hiz_compressed(const struct anv_image *image)
{
   if (!(image->vk.aspects & VK_IMAGE_ASPECT_DEPTH_BIT))
      return false;

   const uint32_t plane =
      anv_image_aspect_to_plane(image, VK_IMAGE_ASPECT_DEPTH_BIT);
   return isl_aux_usage_has_hiz(image->planes[plane].aux_usage);
}

static bool
is_image_hiz_non_wt_ccs_compressed(const struct anv_image *image)
{
   if (!(image->vk.aspects & VK_IMAGE_ASPECT_DEPTH_BIT))
      return false;

   const uint32_t plane =
      anv_image_aspect_to_plane(image, VK_IMAGE_ASPECT_DEPTH_BIT);
   return isl_aux_usage_has_hiz(image->planes[plane].aux_usage) &&
          image->planes[plane].aux_usage != ISL_AUX_USAGE_HIZ_CCS_WT;
}

static bool
is_image_hiz_non_ccs_compressed(const struct anv_image *image)
{
   if (!(image->vk.aspects & VK_IMAGE_ASPECT_DEPTH_BIT))
      return false;

   const uint32_t plane =
      anv_image_aspect_to_plane(image, VK_IMAGE_ASPECT_DEPTH_BIT);
   return image->planes[plane].aux_usage == ISL_AUX_USAGE_HIZ;
}

static bool
is_image_stc_ccs_compressed(const struct anv_image *image)
{
   /* STC_CCS is used for the CPS surfaces, hence the COLOR_BIT inclusion */
   if (!(image->vk.aspects & (VK_IMAGE_ASPECT_STENCIL_BIT |
                              VK_IMAGE_ASPECT_COLOR_BIT)))
      return false;

   const uint32_t plane =
      anv_image_aspect_to_plane(image,
                                image->vk.aspects &
                                (VK_IMAGE_ASPECT_COLOR_BIT |
                                 VK_IMAGE_ASPECT_STENCIL_BIT));
   return image->planes[plane].aux_usage == ISL_AUX_USAGE_STC_CCS;
}

bool
anv_blorp_execute_on_companion(struct anv_cmd_buffer *cmd_buffer,
                               const struct anv_image *src_image,
                               const struct anv_image *dst_image)
{
   const struct intel_device_info *devinfo = cmd_buffer->device->info;

   /* RCS can do everything, it's the Über-engine */
   if (anv_cmd_buffer_is_render_queue(cmd_buffer))
      return false;

   /* MSAA images have to be dealt with on the companion RCS command buffer
    * for both CCS && BCS engines.
    *
    * TODO: relax this for Xe3+ on CCS when we have Blorp MSAA copies.
    */
   if ((src_image && is_image_multisampled(src_image)) ||
       (dst_image && is_image_multisampled(dst_image)))
      return true;

   if (anv_cmd_buffer_is_blitter_queue(cmd_buffer)) {
      /* Emulation of formats is done through a compute shader, so we need the
       * companion command buffer for the blitter engine.
       */
      if ((src_image && is_image_emulated(src_image)) ||
          (dst_image && is_image_emulated(dst_image)))
         return true;

      /* Wa_22019225126: The compression pairing bit on blitter engine is not
       * programmed correctly for depth/stencil resources. Fallback to RCS
       * engine for performing a copy to workaround the issue.
       *
       * Unfortunately that workaround hasn't propagate back to DG2 products,
       * but they're for sure affected as well, hence the version check.
       */
      if ((intel_needs_workaround(devinfo, 22019225126) ||
           devinfo->verx10 == 125) &&
          ((src_image && (is_image_stc_ccs_compressed(src_image) ||
                          is_image_hiz_compressed(src_image))) ||
           (dst_image && (is_image_stc_ccs_compressed(dst_image) ||
                          is_image_hiz_compressed(dst_image)))))
         return true;
   }

   /* HiZ compression without CCS_WT will not work, it would require us to
    * synchronize the HiZ data with CCS on queue transfer.
    */
   if (src_image && is_image_hiz_non_wt_ccs_compressed(src_image))
      return true;

   /* Pre Gfx20 the only engine that can generate STC_CCS data is RCS through
    * the stencil output due to the difference in compression pairing bit. On
    * Gfx20 there is no difference.
    */
   if (devinfo->ver < 20 && dst_image && is_image_stc_ccs_compressed(dst_image))
      return true;

   /* Blitter & compute engine cannot generate HiZ data */
   if (dst_image && is_image_hiz_compressed(dst_image))
      return true;

   return false;
}

void anv_CmdCopyImage2(
    VkCommandBuffer                             commandBuffer,
    const VkCopyImageInfo2*                     pCopyImageInfo)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_image, src_image, pCopyImageInfo->srcImage);
   ANV_FROM_HANDLE(anv_image, dst_image, pCopyImageInfo->dstImage);

   anv_blorp_require_rcs(cmd_buffer, src_image, dst_image) {
      struct blorp_batch batch;
      anv_blorp_batch_init(cmd_buffer, &batch, 0);

      for (unsigned r = 0; r < pCopyImageInfo->regionCount; r++) {
         copy_image(cmd_buffer, &batch,
                    src_image, pCopyImageInfo->srcImageLayout,
                    dst_image, pCopyImageInfo->dstImageLayout,
                    &pCopyImageInfo->pRegions[r]);
      }

      anv_blorp_batch_finish(&batch);

      if (dst_image->emu_plane_format != VK_FORMAT_UNDEFINED) {
         assert(!anv_cmd_buffer_is_blitter_queue(cmd_buffer));
         const enum anv_pipe_bits pipe_bits =
            anv_cmd_buffer_is_compute_queue(cmd_buffer) ?
            ANV_PIPE_HDC_PIPELINE_FLUSH_BIT :
            ANV_PIPE_RENDER_TARGET_CACHE_FLUSH_BIT;
         anv_add_pending_pipe_bits(cmd_buffer, pipe_bits,
                                   "Copy flush before astc emu");

         for (unsigned r = 0; r < pCopyImageInfo->regionCount; r++) {
            const VkImageCopy2 *region = &pCopyImageInfo->pRegions[r];
            const VkOffset3D block_offset = vk_image_offset_to_elements(
                  &dst_image->vk, region->dstOffset);
            const VkExtent3D block_extent = vk_image_extent_to_elements(
                  &src_image->vk, region->extent);
            anv_astc_emu_process(cmd_buffer, dst_image,
                                 pCopyImageInfo->dstImageLayout,
                                 &region->dstSubresource,
                                 block_offset, block_extent);
         }
      }
   }
}

static void
copy_buffer_to_image(struct anv_cmd_buffer *cmd_buffer,
                     struct blorp_batch *batch,
                     struct anv_address mem_addr,
                     const struct vk_image_buffer_layout *mem_layout,
                     const struct anv_image *anv_image,
                     VkImageLayout image_layout,
                     VkImageSubresourceLayers sub_resource,
                     VkOffset3D image_offset,
                     VkExtent3D image_extent,
                     bool memory_to_image)
{
   struct {
      struct blorp_surf surf;
      const struct isl_surf *isl_surf;
      enum isl_format copy_format;
      uint32_t level;
      VkOffset3D offset;
   } image, memory, *src, *dst;

   struct isl_surf memory_isl_surf;
   memory.isl_surf = &memory_isl_surf;
   memory.level = 0;
   memory.offset = (VkOffset3D) { 0, 0, 0 };

   if (memory_to_image) {
      src = &memory;
      dst = &image;
   } else {
      src = &image;
      dst = &memory;
   }

   const unsigned plane = anv_image_aspect_to_plane(anv_image,
                                                    sub_resource.aspectMask);
   image.isl_surf = &anv_image->planes[plane].primary_surface.isl;
   image.offset =
      vk_image_sanitize_offset(&anv_image->vk, image_offset);
   image.level = sub_resource.mipLevel;

   VkExtent3D extent =
      vk_image_sanitize_extent(&anv_image->vk, image_extent);
   if (anv_image->vk.image_type != VK_IMAGE_TYPE_3D) {
      image.offset.z = sub_resource.baseArrayLayer;
      extent.depth =
         vk_image_subresource_layer_count(&anv_image->vk, &sub_resource);
   }

   const enum isl_format linear_format =
      anv_get_isl_format(cmd_buffer->device->physical, anv_image->vk.format,
                         sub_resource.aspectMask, VK_IMAGE_TILING_LINEAR);

   get_blorp_surf_for_anv_address(cmd_buffer, mem_addr,
                                  extent.width, extent.height,
                                  mem_layout->row_stride_B, linear_format,
                                  false, &memory.surf, &memory_isl_surf);

   blorp_copy_get_formats(&cmd_buffer->device->isl_dev,
                          src->isl_surf, dst->isl_surf,
                          &src->copy_format, &dst->copy_format);

   get_blorp_surf_for_anv_image(cmd_buffer, anv_image, sub_resource.aspectMask,
                                memory_to_image ?
                                VK_IMAGE_USAGE_TRANSFER_DST_BIT :
                                VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                image_layout, ISL_AUX_USAGE_NONE,
                                image.copy_format, false, &image.surf);

   if (&image == dst) {
      anv_cmd_buffer_mark_image_written(cmd_buffer, anv_image,
                                        sub_resource.aspectMask,
                                        dst->surf.aux_usage,
                                        dst->level,
                                        dst->offset.z, extent.depth);
   }

   for (unsigned z = 0; z < extent.depth; z++) {
      blorp_copy(batch, &src->surf, src->level, src->offset.z,
                 &dst->surf, dst->level, dst->offset.z,
                 src->offset.x, src->offset.y, dst->offset.x, dst->offset.y,
                 extent.width, extent.height);

      image.offset.z++;
      memory.surf.addr.offset += mem_layout->image_stride_B;
   }
}

void anv_CmdCopyBufferToImage2(
    VkCommandBuffer                             commandBuffer,
    const VkCopyBufferToImageInfo2*             pCopyBufferToImageInfo)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_buffer, src_buffer, pCopyBufferToImageInfo->srcBuffer);
   ANV_FROM_HANDLE(anv_image, dst_image, pCopyBufferToImageInfo->dstImage);

   bool blorp_execute_on_companion =
      anv_blorp_execute_on_companion(cmd_buffer, NULL, dst_image);

   /* Check if any one of the aspects is incompatible with the blitter engine,
    * if true, use the companion RCS command buffer for blit operation since 3
    * component formats are not supported natively except 96bpb on the blitter.
    */
   blorp_execute_on_companion |=
      anv_blorp_blitter_execute_on_companion(cmd_buffer, dst_image,
                                             pCopyBufferToImageInfo->regionCount,
                                             pCopyBufferToImageInfo->pRegions);

   anv_cmd_require_rcs(cmd_buffer, blorp_execute_on_companion) {
      struct blorp_batch batch;
      anv_blorp_batch_init(cmd_buffer, &batch, 0);

      for (unsigned r = 0; r < pCopyBufferToImageInfo->regionCount; r++) {
         const VkBufferImageCopy2 *region = &pCopyBufferToImageInfo->pRegions[r];
         const struct vk_image_buffer_layout buffer_layout =
            vk_image_buffer_copy_layout(&dst_image->vk, region);

         copy_buffer_to_image(cmd_buffer, &batch,
                              anv_address_add(src_buffer->address,
                                              region->bufferOffset),
                              &buffer_layout,
                              dst_image, pCopyBufferToImageInfo->dstImageLayout,
                              region->imageSubresource,
                              region->imageOffset, region->imageExtent,
                              true);
      }

      anv_blorp_batch_finish(&batch);

      if (dst_image->emu_plane_format != VK_FORMAT_UNDEFINED) {
         assert(!anv_cmd_buffer_is_blitter_queue(cmd_buffer));
         const enum anv_pipe_bits pipe_bits =
            anv_cmd_buffer_is_compute_queue(cmd_buffer) ?
            ANV_PIPE_HDC_PIPELINE_FLUSH_BIT :
            ANV_PIPE_RENDER_TARGET_CACHE_FLUSH_BIT;
         anv_add_pending_pipe_bits(cmd_buffer, pipe_bits,
                                   "Copy flush before astc emu");

         for (unsigned r = 0; r < pCopyBufferToImageInfo->regionCount; r++) {
            const VkBufferImageCopy2 *region =
               &pCopyBufferToImageInfo->pRegions[r];
            const VkOffset3D block_offset = vk_image_offset_to_elements(
                  &dst_image->vk, region->imageOffset);
            const VkExtent3D block_extent = vk_image_extent_to_elements(
                  &dst_image->vk, region->imageExtent);
            anv_astc_emu_process(cmd_buffer, dst_image,
                                 pCopyBufferToImageInfo->dstImageLayout,
                                 &region->imageSubresource,
                                 block_offset, block_extent);
         }
      }
   }
}

static void
anv_add_buffer_write_pending_bits(struct anv_cmd_buffer *cmd_buffer,
                                  const char *reason)
{
   const struct intel_device_info *devinfo = cmd_buffer->device->info;

   if (anv_cmd_buffer_is_blitter_queue(cmd_buffer))
      return;

   cmd_buffer->state.queries.buffer_write_bits |=
      (cmd_buffer->state.current_pipeline ==
       cmd_buffer->device->physical->gpgpu_pipeline_value) ?
      ANV_QUERY_COMPUTE_WRITES_PENDING_BITS :
      ANV_QUERY_RENDER_TARGET_WRITES_PENDING_BITS(devinfo);
}

void anv_CmdCopyImageToBuffer2(
    VkCommandBuffer                             commandBuffer,
    const VkCopyImageToBufferInfo2*             pCopyImageToBufferInfo)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_image, src_image, pCopyImageToBufferInfo->srcImage);
   ANV_FROM_HANDLE(anv_buffer, dst_buffer, pCopyImageToBufferInfo->dstBuffer);

   bool blorp_execute_on_companion =
      anv_blorp_execute_on_companion(cmd_buffer, src_image, NULL);

   /* Check if any one of the aspects is incompatible with the blitter engine,
    * if true, use the companion RCS command buffer for blit operation since 3
    * component formats are not supported natively except 96bpb on the blitter.
    */
   blorp_execute_on_companion |=
      anv_blorp_blitter_execute_on_companion(cmd_buffer, src_image,
                                             pCopyImageToBufferInfo->regionCount,
                                             pCopyImageToBufferInfo->pRegions);

   anv_cmd_require_rcs(cmd_buffer, blorp_execute_on_companion) {
      struct blorp_batch batch;
      anv_blorp_batch_init(cmd_buffer, &batch, 0);

      for (unsigned r = 0; r < pCopyImageToBufferInfo->regionCount; r++) {
         const VkBufferImageCopy2 *region = &pCopyImageToBufferInfo->pRegions[r];
         const struct vk_image_buffer_layout buffer_layout =
            vk_image_buffer_copy_layout(&src_image->vk, region);

         copy_buffer_to_image(cmd_buffer, &batch,
                              anv_address_add(dst_buffer->address,
                                              region->bufferOffset),
                              &buffer_layout,
                              src_image, pCopyImageToBufferInfo->srcImageLayout,
                              region->imageSubresource,
                              region->imageOffset, region->imageExtent,
                              false);
      }

      anv_add_buffer_write_pending_bits(cmd_buffer, "after copy image to buffer");

      anv_blorp_batch_finish(&batch);
   }
}

static bool
flip_coords(unsigned *src0, unsigned *src1, unsigned *dst0, unsigned *dst1)
{
   bool flip = false;
   if (*src0 > *src1) {
      unsigned tmp = *src0;
      *src0 = *src1;
      *src1 = tmp;
      flip = !flip;
   }

   if (*dst0 > *dst1) {
      unsigned tmp = *dst0;
      *dst0 = *dst1;
      *dst1 = tmp;
      flip = !flip;
   }

   return flip;
}

static void
blit_image(struct anv_cmd_buffer *cmd_buffer,
           struct blorp_batch *batch,
           const struct anv_image *src_image,
           VkImageLayout src_image_layout,
           const struct anv_image *dst_image,
           VkImageLayout dst_image_layout,
           const VkImageBlit2 *region,
           VkFilter filter)
{
   const VkImageSubresourceLayers *src_res = &region->srcSubresource;
   const VkImageSubresourceLayers *dst_res = &region->dstSubresource;

   struct blorp_surf src, dst;

   enum blorp_filter blorp_filter;
   switch (filter) {
   case VK_FILTER_NEAREST:
      blorp_filter = BLORP_FILTER_NEAREST;
      break;
   case VK_FILTER_LINEAR:
      blorp_filter = BLORP_FILTER_BILINEAR;
      break;
   default:
      UNREACHABLE("Invalid filter");
   }

   assert(anv_image_aspects_compatible(src_res->aspectMask,
                                       dst_res->aspectMask));

   anv_foreach_image_aspect_bit(aspect_bit, src_image, src_res->aspectMask) {
      VkFormat src_vk_format =
         src_image->emu_plane_format != VK_FORMAT_UNDEFINED ?
         src_image->emu_plane_format : src_image->vk.format;

      struct anv_format_plane src_format =
         anv_get_format_aspect(cmd_buffer->device->physical, src_vk_format,
                               1U << aspect_bit, src_image->vk.tiling);
      struct anv_format_plane dst_format =
         anv_get_format_aspect(cmd_buffer->device->physical, dst_image->vk.format,
                               1U << aspect_bit, dst_image->vk.tiling);

      get_blorp_surf_for_anv_image(cmd_buffer,
                                   src_image, 1U << aspect_bit,
                                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                   src_image_layout, ISL_AUX_USAGE_NONE,
                                   src_format.isl_format, false, &src);
      get_blorp_surf_for_anv_image(cmd_buffer,
                                   dst_image, 1U << aspect_bit,
                                   VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                   dst_image_layout, ISL_AUX_USAGE_NONE,
                                   dst_format.isl_format, false, &dst);

      if (src_image->emu_plane_format != VK_FORMAT_UNDEFINED) {
         /* redirect src to the hidden plane */
         const uint32_t plane = src_image->n_planes;
         const struct anv_surface *surface =
            &src_image->planes[plane].primary_surface;
         const struct anv_address address =
            anv_image_address(src_image, &surface->memory_range);
         src.surf = &surface->isl,
         src.addr.offset = address.offset;
      }

      unsigned dst_start, dst_end;
      if (dst_image->vk.image_type == VK_IMAGE_TYPE_3D) {
         assert(dst_res->baseArrayLayer == 0);
         dst_start = region->dstOffsets[0].z;
         dst_end = region->dstOffsets[1].z;
      } else {
         dst_start = dst_res->baseArrayLayer;
         dst_end = dst_start +
            vk_image_subresource_layer_count(&dst_image->vk, dst_res);
      }

      unsigned src_start, src_end;
      if (src_image->vk.image_type == VK_IMAGE_TYPE_3D) {
         assert(src_res->baseArrayLayer == 0);
         src_start = region->srcOffsets[0].z;
         src_end = region->srcOffsets[1].z;
      } else {
         src_start = src_res->baseArrayLayer;
         src_end = src_start +
            vk_image_subresource_layer_count(&src_image->vk, src_res);
      }

      bool flip_z = flip_coords(&src_start, &src_end, &dst_start, &dst_end);
      const unsigned num_layers = dst_end - dst_start;
      float src_z_step = (float)(src_end - src_start) / (float)num_layers;

      /* There is no interpolation to the pixel center during rendering, so
       * add the 0.5 offset ourselves here. */
      float depth_center_offset = 0;
      if (src_image->vk.image_type == VK_IMAGE_TYPE_3D)
         depth_center_offset = 0.5 / num_layers * (src_end - src_start);

      if (flip_z) {
         src_start = src_end;
         src_z_step *= -1;
         depth_center_offset *= -1;
      }

      unsigned src_x0 = region->srcOffsets[0].x;
      unsigned src_x1 = region->srcOffsets[1].x;
      unsigned dst_x0 = region->dstOffsets[0].x;
      unsigned dst_x1 = region->dstOffsets[1].x;
      bool flip_x = flip_coords(&src_x0, &src_x1, &dst_x0, &dst_x1);

      unsigned src_y0 = region->srcOffsets[0].y;
      unsigned src_y1 = region->srcOffsets[1].y;
      unsigned dst_y0 = region->dstOffsets[0].y;
      unsigned dst_y1 = region->dstOffsets[1].y;
      bool flip_y = flip_coords(&src_y0, &src_y1, &dst_y0, &dst_y1);

      anv_cmd_buffer_mark_image_written(cmd_buffer, dst_image,
                                        1U << aspect_bit,
                                        dst.aux_usage,
                                        dst_res->mipLevel,
                                        dst_start, num_layers);

      for (unsigned i = 0; i < num_layers; i++) {
         unsigned dst_z = dst_start + i;
         float src_z = src_start + i * src_z_step + depth_center_offset;

         blorp_blit(batch, &src, src_res->mipLevel, src_z,
                    src_format.isl_format, src_format.swizzle,
                    &dst, dst_res->mipLevel, dst_z,
                    dst_format.isl_format, dst_format.swizzle,
                    src_x0, src_y0, src_x1, src_y1,
                    dst_x0, dst_y0, dst_x1, dst_y1,
                    blorp_filter, flip_x, flip_y);
      }
   }
}

void anv_CmdBlitImage2(
    VkCommandBuffer                             commandBuffer,
    const VkBlitImageInfo2*                     pBlitImageInfo)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_image, src_image, pBlitImageInfo->srcImage);
   ANV_FROM_HANDLE(anv_image, dst_image, pBlitImageInfo->dstImage);

   struct blorp_batch batch;
   anv_blorp_batch_init(cmd_buffer, &batch, 0);

   for (unsigned r = 0; r < pBlitImageInfo->regionCount; r++) {
      blit_image(cmd_buffer, &batch,
                 src_image, pBlitImageInfo->srcImageLayout,
                 dst_image, pBlitImageInfo->dstImageLayout,
                 &pBlitImageInfo->pRegions[r], pBlitImageInfo->filter);
   }

   anv_blorp_batch_finish(&batch);
}

/* This is maximum possible width/height our HW can handle */
#define MAX_SURFACE_DIM (1ull << 14)

static void
copy_memory(struct anv_device *device,
            struct blorp_batch *batch,
            struct anv_address src_addr,
            struct anv_address dst_addr,
            uint64_t size)
{
   struct blorp_address src = {
      .buffer = src_addr.bo,
      .offset = src_addr.offset,
      .mocs = anv_mocs(device, src_addr.bo,
                       blorp_batch_isl_copy_usage(batch, false /* is_dest */,
                                                  src_addr.protected)),
   };
   struct blorp_address dst = {
      .buffer = dst_addr.bo,
      .offset = dst_addr.offset,
      .mocs = anv_mocs(device, dst_addr.bo,
                       blorp_batch_isl_copy_usage(batch, true /* is_dest */,
                                                  dst_addr.protected)),
   };

   blorp_buffer_copy(batch, src, dst, size);
}

void
anv_cmd_copy_addr(struct anv_cmd_buffer *cmd_buffer,
                  struct anv_address src_addr,
                  struct anv_address dst_addr,
                  uint64_t size)
{
   struct anv_device *device = cmd_buffer->device;

   struct blorp_batch batch;
   anv_blorp_batch_init(cmd_buffer, &batch,
                        cmd_buffer->state.current_pipeline ==
                        cmd_buffer->device->physical->gpgpu_pipeline_value ?
                        BLORP_BATCH_USE_COMPUTE : 0);

   copy_memory(device, &batch, src_addr, dst_addr, size);

   anv_add_buffer_write_pending_bits(cmd_buffer, "after copy buffer");
   anv_blorp_batch_finish(&batch);
}

void anv_CmdCopyBuffer2(
    VkCommandBuffer                             commandBuffer,
    const VkCopyBufferInfo2*                    pCopyBufferInfo)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_buffer, src_buffer, pCopyBufferInfo->srcBuffer);
   ANV_FROM_HANDLE(anv_buffer, dst_buffer, pCopyBufferInfo->dstBuffer);

   struct blorp_batch batch;
   anv_blorp_batch_init(cmd_buffer, &batch,
                        cmd_buffer->state.current_pipeline ==
                        cmd_buffer->device->physical->gpgpu_pipeline_value ?
                        BLORP_BATCH_USE_COMPUTE : 0);

   for (unsigned r = 0; r < pCopyBufferInfo->regionCount; r++) {
      const VkBufferCopy2 *region = &pCopyBufferInfo->pRegions[r];

      copy_memory(cmd_buffer->device, &batch,
                  anv_address_add(src_buffer->address, region->srcOffset),
                  anv_address_add(dst_buffer->address, region->dstOffset),
                  region->size);
   }

   anv_add_buffer_write_pending_bits(cmd_buffer, "after copy buffer");

   anv_blorp_batch_finish(&batch);
}

void
anv_cmd_buffer_update_addr(
    struct anv_cmd_buffer*                      cmd_buffer,
    struct anv_address                          address,
    VkDeviceSize                                dataSize,
    const void*                                 pData)
{
   struct blorp_batch batch;
   anv_blorp_batch_init(cmd_buffer, &batch,
                        cmd_buffer->state.current_pipeline ==
                        cmd_buffer->device->physical->gpgpu_pipeline_value ?
                        BLORP_BATCH_USE_COMPUTE : 0);

   /* We can't quite grab a full block because the state stream needs a
    * little data at the top to build its linked list.
    */
   const uint32_t max_update_size =
      cmd_buffer->device->dynamic_state_pool.block_size - 64;

   assert(max_update_size < MAX_SURFACE_DIM * 4);

   /* We're about to read data that was written from the CPU.  Flush the
    * texture cache so we don't get anything stale.
    */
   anv_add_pending_pipe_bits(cmd_buffer,
                             ANV_PIPE_TEXTURE_CACHE_INVALIDATE_BIT,
                             "before UpdateBuffer");

   while (dataSize) {
      const uint32_t copy_size = MIN2(dataSize, max_update_size);

      struct anv_state tmp_data =
         anv_cmd_buffer_alloc_temporary_state(cmd_buffer, copy_size, 64);
      struct anv_address tmp_addr =
         anv_cmd_buffer_temporary_state_address(cmd_buffer, tmp_data);

      memcpy(tmp_data.map, pData, copy_size);

      struct blorp_address src = {
         .buffer = tmp_addr.bo,
         .offset = tmp_addr.offset,
         .mocs = anv_mocs(cmd_buffer->device, NULL,
                          get_usage_flag_for_cmd_buffer(cmd_buffer,
                                                        false /* is_dest */,
                                                        false /* is_depth */,
                                                        tmp_addr.protected)),
      };
      struct blorp_address dst = {
         .buffer = address.bo,
         .offset = address.offset,
         .mocs = anv_mocs(cmd_buffer->device, address.bo,
                          get_usage_flag_for_cmd_buffer(
                             cmd_buffer,
                             true /* is_dest */,
                             false /* is_depth */,
                             address.protected)),
      };

      blorp_buffer_copy(&batch, src, dst, copy_size);

      dataSize -= copy_size;
      address = anv_address_add(address, copy_size);
      pData = (void *)pData + copy_size;
   }

   anv_add_buffer_write_pending_bits(cmd_buffer, "update buffer");

   anv_blorp_batch_finish(&batch);
}

void anv_CmdUpdateBuffer(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    dstBuffer,
    VkDeviceSize                                dstOffset,
    VkDeviceSize                                dataSize,
    const void*                                 pData)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_buffer, dst_buffer, dstBuffer);

   anv_cmd_buffer_update_addr(cmd_buffer,
                              anv_address_add(dst_buffer->address, dstOffset),
                              dataSize, pData);
}

void
anv_cmd_buffer_fill_area(struct anv_cmd_buffer *cmd_buffer,
                         struct anv_address address,
                         VkDeviceSize size,
                         uint32_t data)
{
   struct blorp_batch batch;
   anv_blorp_batch_init(cmd_buffer, &batch,
                        cmd_buffer->state.current_pipeline ==
                        cmd_buffer->device->physical->gpgpu_pipeline_value ?
                        BLORP_BATCH_USE_COMPUTE : 0);

   union isl_color_value color = {
      .u32 = { data, data, data, data },
   };

   isl_surf_usage_flags_t usage =
      get_usage_flag_for_cmd_buffer(cmd_buffer, true /* is_dest */,
                                    false /* is_depth */, address.protected);

   struct isl_surf isl_surf;
   struct blorp_surf surf = {
      .addr = {
         .buffer = address.bo,
         .offset = address.offset,
         .mocs = anv_mocs(cmd_buffer->device, address.bo, usage),
      },
      .surf = &isl_surf,
   };

   do {
      isl_surf_from_mem(&cmd_buffer->device->isl_dev, &isl_surf,
                        surf.addr.offset, size, ISL_TILING_LINEAR);

      blorp_clear(&batch, &surf, isl_surf.format, ISL_SWIZZLE_IDENTITY, 0, 0,
                  isl_surf.logical_level0_px.a, 0, 0,
                  isl_surf.logical_level0_px.w,
                  isl_surf.logical_level0_px.h,
                  color, 0 /* color_write_disable */);

      size -= isl_surf.size_B;
      surf.addr.offset += isl_surf.size_B;
   } while (size != 0);

   anv_blorp_batch_finish(&batch);
}

void
anv_cmd_fill_buffer_addr(VkCommandBuffer commandBuffer,
                         VkDeviceAddress dstAddr,
                         VkDeviceSize    fillSize,
                         uint32_t        data)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   anv_cmd_buffer_fill_area(cmd_buffer, anv_address_from_u64(dstAddr),
                            fillSize, data);

   anv_add_buffer_write_pending_bits(cmd_buffer, "after fill buffer");
}

void anv_CmdFillBuffer(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    dstBuffer,
    VkDeviceSize                                dstOffset,
    VkDeviceSize                                fillSize,
    uint32_t                                    data)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_buffer, dst_buffer, dstBuffer);

   fillSize = vk_buffer_range(&dst_buffer->vk, dstOffset, fillSize);

   /* From the Vulkan spec:
    *
    *    "size is the number of bytes to fill, and must be either a multiple
    *    of 4, or VK_WHOLE_SIZE to fill the range from offset to the end of
    *    the buffer. If VK_WHOLE_SIZE is used and the remaining size of the
    *    buffer is not a multiple of 4, then the nearest smaller multiple is
    *    used."
    */
   fillSize &= ~3ull;

   anv_cmd_buffer_fill_area(cmd_buffer,
                            anv_address_add(dst_buffer->address, dstOffset),
                            fillSize, data);

   anv_add_buffer_write_pending_bits(cmd_buffer, "after fill buffer");
}

static void
exec_ccs_op(struct anv_cmd_buffer *cmd_buffer,
            struct blorp_batch *batch,
            const struct anv_image *image,
            enum isl_format format, struct isl_swizzle swizzle,
            VkImageAspectFlagBits aspect, uint32_t level,
            uint32_t base_layer, uint32_t layer_count,
            enum isl_aux_op ccs_op, union isl_color_value *clear_value)
{
   assert(image->vk.aspects & VK_IMAGE_ASPECT_ANY_COLOR_BIT_ANV);
   assert(image->vk.samples == 1);
   assert(level < anv_image_aux_levels(image, aspect));
   /* Multi-LOD YcBcR is not allowed */
   assert(image->n_planes == 1 || level == 0);
   assert(base_layer + layer_count <=
          anv_image_aux_layers(image, aspect, level));

   const uint32_t plane = anv_image_aspect_to_plane(image, aspect);

   struct blorp_surf surf;
   get_blorp_surf_for_anv_image(cmd_buffer, image, aspect,
                                VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                ANV_IMAGE_LAYOUT_EXPLICIT_AUX,
                                image->planes[plane].aux_usage,
                                format, false, &surf);

   uint32_t level_width = u_minify(surf.surf->logical_level0_px.w, level);
   uint32_t level_height = u_minify(surf.surf->logical_level0_px.h, level);

   /* Blorp will store the clear color for us if we provide the clear color
    * address and we are doing a fast clear. So we save the clear value into
    * the blorp surface.
    */
   if (clear_value)
      surf.clear_color = *clear_value;

   switch (ccs_op) {
   case ISL_AUX_OP_FAST_CLEAR:
      blorp_fast_clear(batch, &surf, format, swizzle,
                       level, base_layer, layer_count,
                       0, 0, level_width, level_height);
      break;
   case ISL_AUX_OP_FULL_RESOLVE:
   case ISL_AUX_OP_PARTIAL_RESOLVE: {
      /* Wa_1508744258: Enable RHWO optimization for resolves */
      const bool enable_rhwo_opt =
         intel_needs_workaround(cmd_buffer->device->info, 1508744258);

      if (enable_rhwo_opt)
         cmd_buffer->state.pending_rhwo_optimization_enabled = true;

      blorp_ccs_resolve(batch, &surf, level, base_layer, layer_count,
                        format, ccs_op);

      if (enable_rhwo_opt)
         cmd_buffer->state.pending_rhwo_optimization_enabled = false;
      break;
   }
   case ISL_AUX_OP_AMBIGUATE:
      for (uint32_t a = 0; a < layer_count; a++) {
         const uint32_t layer = base_layer + a;
         blorp_ccs_ambiguate(batch, &surf, level, layer);
      }
      break;
   default:
      UNREACHABLE("Unsupported CCS operation");
   }
}

static void
exec_mcs_op(struct anv_cmd_buffer *cmd_buffer,
            struct blorp_batch *batch,
            const struct anv_image *image,
            enum isl_format format, struct isl_swizzle swizzle,
            VkImageAspectFlagBits aspect,
            uint32_t base_layer, uint32_t layer_count,
            enum isl_aux_op mcs_op, union isl_color_value *clear_value)
{
   assert(image->vk.aspects == VK_IMAGE_ASPECT_COLOR_BIT);
   assert(image->vk.samples > 1);
   assert(base_layer + layer_count <= anv_image_aux_layers(image, aspect, 0));

   /* Multisampling with multi-planar formats is not supported */
   assert(image->n_planes == 1);

   struct blorp_surf surf;
   get_blorp_surf_for_anv_image(cmd_buffer, image, aspect,
                                VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                ANV_IMAGE_LAYOUT_EXPLICIT_AUX,
                                image->planes[0].aux_usage, format,
                                false, &surf);

   /* Blorp will store the clear color for us if we provide the clear color
    * address and we are doing a fast clear. So we save the clear value into
    * the blorp surface.
    */
   if (clear_value)
      surf.clear_color = *clear_value;

   switch (mcs_op) {
   case ISL_AUX_OP_FAST_CLEAR:
      blorp_fast_clear(batch, &surf, format, swizzle,
                       0, base_layer, layer_count,
                       0, 0, image->vk.extent.width, image->vk.extent.height);
      break;
   case ISL_AUX_OP_PARTIAL_RESOLVE:
      blorp_mcs_partial_resolve(batch, &surf, format,
                                base_layer, layer_count);
      break;
   case ISL_AUX_OP_AMBIGUATE:
      blorp_mcs_ambiguate(batch, &surf, base_layer, layer_count);
      break;
   case ISL_AUX_OP_FULL_RESOLVE:
   default:
      UNREACHABLE("Unsupported MCS operation");
   }
}

void anv_CmdClearColorImage(
    VkCommandBuffer                             commandBuffer,
    VkImage                                     _image,
    VkImageLayout                               imageLayout,
    const VkClearColorValue*                    pColor,
    uint32_t                                    rangeCount,
    const VkImageSubresourceRange*              pRanges)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_image, image, _image);

   anv_blorp_require_rcs(cmd_buffer, NULL, image) {
      struct blorp_batch batch;
      anv_blorp_batch_init(cmd_buffer, &batch, 0);

      struct anv_format_plane src_format =
         anv_get_format_aspect(cmd_buffer->device->physical, image->vk.format,
                               VK_IMAGE_ASPECT_COLOR_BIT, image->vk.tiling);
      struct blorp_surf surf;
      get_blorp_surf_for_anv_image(cmd_buffer, image,
                                   VK_IMAGE_ASPECT_COLOR_BIT,
                                   VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                   imageLayout, ISL_AUX_USAGE_NONE,
                                   src_format.isl_format, false, &surf);

      union isl_color_value clear_color = vk_to_isl_color(*pColor);


      for (unsigned r = 0; r < rangeCount; r++) {
         assert(pRanges[r].aspectMask == VK_IMAGE_ASPECT_COLOR_BIT);

         uint32_t level_count =
            vk_image_subresource_level_count(&image->vk, &pRanges[r]);

         for (uint32_t i = 0; i < level_count; i++) {
            const unsigned level = pRanges[r].baseMipLevel + i;
            const VkExtent3D level_extent =
               vk_image_mip_level_extent(&image->vk, level);

            VkClearRect clear_rect = {};
            clear_rect.rect.extent.width = level_extent.width;
            clear_rect.rect.extent.height = level_extent.height;
            if (image->vk.image_type == VK_IMAGE_TYPE_3D) {
               clear_rect.baseArrayLayer = 0;
               clear_rect.layerCount = level_extent.depth;
            } else {
               clear_rect.baseArrayLayer = pRanges[r].baseArrayLayer;
               clear_rect.layerCount =
                  vk_image_subresource_layer_count(&image->vk, &pRanges[r]);
            }

            if (image->planes[0].aux_usage == ISL_AUX_USAGE_STC_CCS) {
               assert(image->vk.usage & VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR);
               blorp_hiz_clear_depth_stencil(&batch, NULL, &surf, level,
                                             clear_rect.baseArrayLayer,
                                             clear_rect.layerCount,
                                             clear_rect.rect.offset.x,
                                             clear_rect.rect.offset.y,
                                             clear_rect.rect.extent.width,
                                             clear_rect.rect.extent.height,
                                             false /* depth clear */, 0 /* depth value */,
                                             true /* stencil_clear */, clear_color.u32[0] /* stencil_value */);
            } else if (anv_can_fast_clear_color(cmd_buffer, image,
                                                pRanges[r].aspectMask,
                                                level, &clear_rect,
                                                imageLayout, src_format.isl_format,
                                                clear_color)) {
               assert(level == 0);
               assert(clear_rect.baseArrayLayer == 0);
               if (image->vk.samples == 1) {
                  exec_ccs_op(cmd_buffer, &batch, image, src_format.isl_format,
                              src_format.swizzle, VK_IMAGE_ASPECT_COLOR_BIT,
                              0, 0, 1, ISL_AUX_OP_FAST_CLEAR, &clear_color);
               } else {
                  exec_mcs_op(cmd_buffer, &batch, image, src_format.isl_format,
                              src_format.swizzle, VK_IMAGE_ASPECT_COLOR_BIT,
                              0, 1, ISL_AUX_OP_FAST_CLEAR, &clear_color);
               }

               if (cmd_buffer->device->info->ver < 20) {
                  anv_cmd_buffer_mark_image_fast_cleared(cmd_buffer, image,
                                                         src_format.isl_format,
                                                         src_format.swizzle,
                                                         clear_color);
               }

               clear_rect.baseArrayLayer++;
               if (--clear_rect.layerCount == 0)
                  continue;
            }

            anv_cmd_buffer_mark_image_written(cmd_buffer, image,
                                              pRanges[r].aspectMask,
                                              surf.aux_usage, level,
                                              clear_rect.baseArrayLayer,
                                              clear_rect.layerCount);

            blorp_clear(&batch, &surf,
                        src_format.isl_format, src_format.swizzle, level,
                        clear_rect.baseArrayLayer,
                        clear_rect.layerCount,
                        clear_rect.rect.offset.x,
                        clear_rect.rect.offset.y,
                        clear_rect.rect.extent.width,
                        clear_rect.rect.extent.height,
                        clear_color, 0 /* color_write_disable */);
         }
      }

      anv_blorp_batch_finish(&batch);
   }
}

void anv_CmdClearDepthStencilImage(
    VkCommandBuffer                             commandBuffer,
    VkImage                                     image_h,
    VkImageLayout                               imageLayout,
    const VkClearDepthStencilValue*             pDepthStencil,
    uint32_t                                    rangeCount,
    const VkImageSubresourceRange*              pRanges)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_image, image, image_h);

   struct blorp_batch batch;
   anv_blorp_batch_init(cmd_buffer, &batch, 0);
   assert((batch.flags & BLORP_BATCH_USE_COMPUTE) == 0);

   struct blorp_surf depth, stencil;
   if (image->vk.aspects & VK_IMAGE_ASPECT_DEPTH_BIT) {
      get_blorp_surf_for_anv_image(cmd_buffer,
                                   image, VK_IMAGE_ASPECT_DEPTH_BIT,
                                   VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                   imageLayout, ISL_AUX_USAGE_NONE,
                                   ISL_FORMAT_UNSUPPORTED, false, &depth);
   } else {
      memset(&depth, 0, sizeof(depth));
   }

   if (image->vk.aspects & VK_IMAGE_ASPECT_STENCIL_BIT) {
      get_blorp_surf_for_anv_image(cmd_buffer,
                                   image, VK_IMAGE_ASPECT_STENCIL_BIT,
                                   VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                   imageLayout, ISL_AUX_USAGE_NONE,
                                   ISL_FORMAT_UNSUPPORTED, false, &stencil);
   } else {
      memset(&stencil, 0, sizeof(stencil));
   }

   for (unsigned r = 0; r < rangeCount; r++) {
      if (pRanges[r].aspectMask == 0)
         continue;

      bool clear_depth = pRanges[r].aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT;
      bool clear_stencil = pRanges[r].aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT;
      assert(clear_depth || clear_stencil);

      unsigned base_layer = pRanges[r].baseArrayLayer;
      uint32_t layer_count =
         vk_image_subresource_layer_count(&image->vk, &pRanges[r]);
      uint32_t level_count =
         vk_image_subresource_level_count(&image->vk, &pRanges[r]);

      for (uint32_t i = 0; i < level_count; i++) {
         const unsigned level = pRanges[r].baseMipLevel + i;
         const unsigned level_width = u_minify(image->vk.extent.width, level);
         const unsigned level_height = u_minify(image->vk.extent.height, level);

         if (image->vk.image_type == VK_IMAGE_TYPE_3D)
            layer_count = u_minify(image->vk.extent.depth, level);

         const VkRect2D area = {
            .offset.x = 0,
            .offset.y = 0,
            .extent.width = level_width,
            .extent.height = level_height,
         };

         if (anv_can_hiz_clear_image(cmd_buffer, image, imageLayout,
                                     pRanges[r].aspectMask,
                                     pDepthStencil->depth, area, level)) {
            anv_image_hiz_clear(cmd_buffer, image, pRanges[r].aspectMask,
                                level, base_layer, layer_count, area,
                                pDepthStencil);
            continue;
         }

         blorp_clear_depth_stencil(&batch, &depth, &stencil,
                                   level, base_layer, layer_count,
                                   0, 0, level_width, level_height,
                                   clear_depth, pDepthStencil->depth,
                                   clear_stencil ? 0xff : 0,
                                   pDepthStencil->stencil);
      }
   }

   anv_blorp_batch_finish(&batch);
}

VkResult
anv_cmd_buffer_alloc_blorp_binding_table(struct anv_cmd_buffer *cmd_buffer,
                                         uint32_t num_entries,
                                         uint32_t *state_offset,
                                         struct anv_state *bt_state)
{
   *bt_state = anv_cmd_buffer_alloc_binding_table(cmd_buffer, num_entries,
                                                  state_offset);
   if (bt_state->map == NULL) {
      /* We ran out of space.  Grab a new binding table block. */
      VkResult result = anv_cmd_buffer_new_binding_table_block(cmd_buffer);
      if (result != VK_SUCCESS)
         return result;

      /* Re-emit state base addresses so we get the new surface state base
       * address before we start emitting binding tables etc.
       */
      anv_cmd_buffer_emit_bt_pool_base_address(cmd_buffer);

      *bt_state = anv_cmd_buffer_alloc_binding_table(cmd_buffer, num_entries,
                                                     state_offset);
      assert(bt_state->map != NULL);
   }

   return VK_SUCCESS;
}

static VkResult
binding_table_for_surface_state(struct anv_cmd_buffer *cmd_buffer,
                                struct anv_state surface_state,
                                uint32_t *bt_offset)
{
   uint32_t state_offset;
   struct anv_state bt_state;

   VkResult result =
      anv_cmd_buffer_alloc_blorp_binding_table(cmd_buffer, 1, &state_offset,
                                               &bt_state);
   if (result != VK_SUCCESS)
      return result;

   uint32_t *bt_map = bt_state.map;
   bt_map[0] = surface_state.offset + state_offset;

   *bt_offset = bt_state.offset;
   return VK_SUCCESS;
}

static bool
can_fast_clear_color_att(struct anv_cmd_buffer *cmd_buffer,
                         struct blorp_batch *batch,
                         const struct anv_attachment *att,
                         const VkClearAttachment *attachment,
                         uint32_t rectCount, const VkClearRect *pRects)
{
   union isl_color_value clear_color =
      vk_to_isl_color(attachment->clearValue.color);

   if (INTEL_DEBUG(DEBUG_NO_FAST_CLEAR))
      return false;

   /* We don't support fast clearing with conditional rendering at the
    * moment. All the tracking done around fast clears (clear color updates
    * and fast-clear type updates) happens unconditionally.
    */
   if (batch->flags & BLORP_BATCH_PREDICATE_ENABLE)
      return false;

   if (rectCount > 1) {
      anv_perf_warn(VK_LOG_OBJS(&cmd_buffer->device->vk.base),
                    "Fast clears for vkCmdClearAttachments supported only for rectCount == 1");
      return false;
   }

   /* We only support fast-clearing a single layer */
   if (pRects[0].layerCount > 1)
      return false;

   if (att->iview->n_planes != 1) {
      anv_perf_warn(VK_LOG_OBJS(&cmd_buffer->device->vk.base),
                    "Fast clears for vkCmdClearAttachments not supported on "
                    "multiplanar images");
      return false;
   }

   VkClearRect rect = pRects[0];
   rect.baseArrayLayer += att->iview->planes[0].isl.base_array_layer;

   bool is_multiview = cmd_buffer->state.gfx.view_mask != 0;
   if (is_multiview && (cmd_buffer->state.gfx.view_mask != 1))
      return false;

   return anv_can_fast_clear_color(cmd_buffer, att->iview->image,
                                   att->iview->vk.aspects,
                                   att->iview->vk.base_mip_level,
                                   &rect, att->layout,
                                   att->iview->planes[0].isl.format,
                                   clear_color);
}

static void
clear_color_attachment(struct anv_cmd_buffer *cmd_buffer,
                       struct blorp_batch *batch,
                       const VkClearAttachment *attachment,
                       uint32_t rectCount, const VkClearRect *pRects)
{
   struct anv_cmd_graphics_state *gfx = &cmd_buffer->state.gfx;
   const uint32_t att_idx = attachment->colorAttachment;
   assert(att_idx < gfx->color_att_count);
   const struct anv_attachment *att = &gfx->color_att[att_idx];

   if (att->vk_format == VK_FORMAT_UNDEFINED)
      return;

   union isl_color_value clear_color =
      vk_to_isl_color(attachment->clearValue.color);

   const struct anv_image_view *iview = att->iview;
   if (iview &&
       can_fast_clear_color_att(cmd_buffer, batch, att,
                                attachment, rectCount, pRects)) {
      if (iview->image->vk.samples == 1) {
         exec_ccs_op(cmd_buffer, batch, iview->image,
                     iview->planes[0].isl.format,
                     iview->planes[0].isl.swizzle,
                     VK_IMAGE_ASPECT_COLOR_BIT,
                     0, 0, 1, ISL_AUX_OP_FAST_CLEAR,
                     &clear_color);
      } else {
         exec_mcs_op(cmd_buffer, batch, iview->image,
                     iview->planes[0].isl.format,
                     iview->planes[0].isl.swizzle,
                     VK_IMAGE_ASPECT_COLOR_BIT,
                     0, 1, ISL_AUX_OP_FAST_CLEAR,
                     &clear_color);
      }

      if (cmd_buffer->device->info->ver < 20) {
         anv_cmd_buffer_mark_image_fast_cleared(cmd_buffer, iview->image,
                                                iview->planes[0].isl.format,
                                                iview->planes[0].isl.swizzle,
                                                clear_color);
         anv_cmd_buffer_load_clear_color(cmd_buffer, att->surface_state.state,
                                         iview);
      }
      return;
   }

   uint32_t binding_table;
   VkResult result =
      binding_table_for_surface_state(cmd_buffer, att->surface_state.state,
                                      &binding_table);
   if (result != VK_SUCCESS)
      return;

   /* If multiview is enabled we ignore baseArrayLayer and layerCount */
   if (gfx->view_mask) {
      u_foreach_bit(view_idx, gfx->view_mask) {
         for (uint32_t r = 0; r < rectCount; ++r) {
            const VkOffset2D offset = pRects[r].rect.offset;
            const VkExtent2D extent = pRects[r].rect.extent;
            blorp_clear_attachments(batch, binding_table,
                                    ISL_FORMAT_UNSUPPORTED,
                                    gfx->samples,
                                    view_idx, 1,
                                    offset.x, offset.y,
                                    offset.x + extent.width,
                                    offset.y + extent.height,
                                    true, clear_color, false, 0.0f, 0, 0);
         }
      }
      return;
   }

   for (uint32_t r = 0; r < rectCount; ++r) {
      const VkOffset2D offset = pRects[r].rect.offset;
      const VkExtent2D extent = pRects[r].rect.extent;
      assert(pRects[r].layerCount != VK_REMAINING_ARRAY_LAYERS);
      blorp_clear_attachments(batch, binding_table,
                              ISL_FORMAT_UNSUPPORTED,
                              gfx->samples,
                              pRects[r].baseArrayLayer,
                              pRects[r].layerCount,
                              offset.x, offset.y,
                              offset.x + extent.width, offset.y + extent.height,
                              true, clear_color, false, 0.0f, 0, 0);
   }
}

static void
anv_fast_clear_depth_stencil(struct anv_cmd_buffer *cmd_buffer,
                             struct blorp_batch *batch,
                             const struct anv_image *image,
                             VkImageAspectFlags aspects,
                             uint32_t level,
                             uint32_t base_layer, uint32_t layer_count,
                             VkRect2D area,
                             const VkClearDepthStencilValue *clear_value)
{
   assert(image->vk.aspects & (VK_IMAGE_ASPECT_DEPTH_BIT |
                               VK_IMAGE_ASPECT_STENCIL_BIT));

   struct blorp_surf depth = {};
   if (aspects & VK_IMAGE_ASPECT_DEPTH_BIT) {
      const uint32_t plane =
         anv_image_aspect_to_plane(image, VK_IMAGE_ASPECT_DEPTH_BIT);
      assert(base_layer + layer_count <=
             anv_image_aux_layers(image, VK_IMAGE_ASPECT_DEPTH_BIT, level));
      get_blorp_surf_for_anv_image(cmd_buffer,
                                   image, VK_IMAGE_ASPECT_DEPTH_BIT,
                                   0, ANV_IMAGE_LAYOUT_EXPLICIT_AUX,
                                   image->planes[plane].aux_usage,
                                   ISL_FORMAT_UNSUPPORTED, false, &depth);
   }

   struct blorp_surf stencil = {};
   if (aspects & VK_IMAGE_ASPECT_STENCIL_BIT) {
      const uint32_t plane =
         anv_image_aspect_to_plane(image, VK_IMAGE_ASPECT_STENCIL_BIT);
      get_blorp_surf_for_anv_image(cmd_buffer,
                                   image, VK_IMAGE_ASPECT_STENCIL_BIT,
                                   0, ANV_IMAGE_LAYOUT_EXPLICIT_AUX,
                                   image->planes[plane].aux_usage,
                                   ISL_FORMAT_UNSUPPORTED, false, &stencil);
   }

   /* From the Sky Lake PRM Volume 7, "Depth Buffer Clear":
    *
    *    "The following is required when performing a depth buffer clear with
    *    using the WM_STATE or 3DSTATE_WM:
    *
    *       * If other rendering operations have preceded this clear, a
    *         PIPE_CONTROL with depth cache flush enabled, Depth Stall bit
    *         enabled must be issued before the rectangle primitive used for
    *         the depth buffer clear operation.
    *       * [...]"
    *
    * Even though the PRM only says that this is required if using 3DSTATE_WM
    * and a 3DPRIMITIVE, the GPU appears to also need this to avoid occasional
    * hangs when doing a clear with WM_HZ_OP.
    */
   anv_add_pending_pipe_bits(cmd_buffer,
                             ANV_PIPE_DEPTH_CACHE_FLUSH_BIT |
                             ANV_PIPE_DEPTH_STALL_BIT,
                             "before clear hiz");

   if ((aspects & VK_IMAGE_ASPECT_DEPTH_BIT) &&
       depth.aux_usage == ISL_AUX_USAGE_HIZ_CCS_WT) {
      /* From Bspec 47010 (Depth Buffer Clear):
       *
       *    Since the fast clear cycles to CCS are not cached in TileCache,
       *    any previous depth buffer writes to overlapping pixels must be
       *    flushed out of TileCache before a succeeding Depth Buffer Clear.
       *    This restriction only applies to Depth Buffer with write-thru
       *    enabled, since fast clears to CCS only occur for write-thru mode.
       *
       * There may have been a write to this depth buffer. Flush it from the
       * tile cache just in case.
       *
       * Set CS stall bit to guarantee that the fast clear starts the execution
       * after the tile cache flush completed.
       *
       * There is no Bspec requirement to flush the data cache but the
       * experiment shows that flusing the data cache helps to resolve the
       * corruption.
       */
      unsigned wa_flush = cmd_buffer->device->info->verx10 >= 125 ?
                          ANV_PIPE_DATA_CACHE_FLUSH_BIT : 0;
      anv_add_pending_pipe_bits(cmd_buffer,
                                ANV_PIPE_DEPTH_CACHE_FLUSH_BIT |
                                ANV_PIPE_CS_STALL_BIT |
                                ANV_PIPE_TILE_CACHE_FLUSH_BIT |
                                wa_flush,
                                "before clear hiz_ccs_wt");
   }

   blorp_hiz_clear_depth_stencil(batch, &depth, &stencil,
                                 level, base_layer, layer_count,
                                 area.offset.x, area.offset.y,
                                 area.offset.x + area.extent.width,
                                 area.offset.y + area.extent.height,
                                 aspects & VK_IMAGE_ASPECT_DEPTH_BIT,
                                 clear_value->depth,
                                 aspects & VK_IMAGE_ASPECT_STENCIL_BIT,
                                 clear_value->stencil);

   /* From the SKL PRM, Depth Buffer Clear:
    *
    *    "Depth Buffer Clear Workaround
    *
    *    Depth buffer clear pass using any of the methods (WM_STATE,
    *    3DSTATE_WM or 3DSTATE_WM_HZ_OP) must be followed by a PIPE_CONTROL
    *    command with DEPTH_STALL bit and Depth FLUSH bits “set” before
    *    starting to render.  DepthStall and DepthFlush are not needed between
    *    consecutive depth clear passes nor is it required if the depth-clear
    *    pass was done with “full_surf_clear” bit set in the
    *    3DSTATE_WM_HZ_OP."
    *
    * Even though the PRM provides a bunch of conditions under which this is
    * supposedly unnecessary, we choose to perform the flush unconditionally
    * just to be safe.
    *
    * From Bspec 46959, a programming note applicable to Gfx12+:
    *
    *    "Since HZ_OP has to be sent twice (first time set the clear/resolve state
    *    and 2nd time to clear the state), and HW internally flushes the depth
    *    cache on HZ_OP, there is no need to explicitly send a Depth Cache flush
    *    after Clear or Resolve."
    */
   if (cmd_buffer->device->info->verx10 < 120) {
      anv_add_pending_pipe_bits(cmd_buffer,
                                ANV_PIPE_DEPTH_CACHE_FLUSH_BIT |
                                ANV_PIPE_DEPTH_STALL_BIT,
                                "after clear hiz");
   }
}

static bool
can_hiz_clear_att(struct anv_cmd_buffer *cmd_buffer,
                  struct blorp_batch *batch,
                  const struct anv_attachment *ds_att,
                  const VkClearAttachment *attachment,
                  uint32_t rectCount, const VkClearRect *pRects)
{
   if (INTEL_DEBUG(DEBUG_NO_FAST_CLEAR))
      return false;

   /* From Bspec's section MI_PREDICATE:
    *
    *    "The MI_PREDICATE command is used to control the Predicate state bit,
    *    which in turn can be used to enable/disable the processing of
    *    3DPRIMITIVE commands."
    *
    * Also from BDW/CHV Bspec's 3DSTATE_WM_HZ_OP programming notes:
    *
    *    "This command does NOT support predication from the use of the
    *    MI_PREDICATE register. To predicate depth clears and resolves on you
    *    must fall back to using the 3D_PRIMITIVE or GPGPU_WALKER commands."
    *
    * Since BLORP's predication is currently dependent on MI_PREDICATE, fall
    * back to the slow depth clear path when the BLORP_BATCH_PREDICATE_ENABLE
    * flag is set.
    */
   if (batch->flags & BLORP_BATCH_PREDICATE_ENABLE)
      return false;

   if (rectCount > 1) {
      anv_perf_warn(VK_LOG_OBJS(&cmd_buffer->device->vk.base),
                    "Fast clears for vkCmdClearAttachments supported only for rectCount == 1");
      return false;
   }

   /* When the BLORP_BATCH_NO_EMIT_DEPTH_STENCIL flag is set, BLORP can only
    * clear the first slice of the currently configured depth/stencil view.
    */
   assert(batch->flags & BLORP_BATCH_NO_EMIT_DEPTH_STENCIL);
   if (pRects[0].layerCount > 1 || pRects[0].baseArrayLayer > 0)
      return false;

   return anv_can_hiz_clear_image(cmd_buffer, ds_att->iview->image,
                                  ds_att->layout,
                                  attachment->aspectMask,
                                  attachment->clearValue.depthStencil.depth,
                                  pRects->rect,
                                  ds_att->iview->vk.base_mip_level);
}

static void
clear_depth_stencil_attachment(struct anv_cmd_buffer *cmd_buffer,
                               struct blorp_batch *batch,
                               const VkClearAttachment *attachment,
                               uint32_t rectCount, const VkClearRect *pRects)
{
   static const union isl_color_value color_value = { .u32 = { 0, } };
   struct anv_cmd_graphics_state *gfx = &cmd_buffer->state.gfx;
   const struct anv_attachment *d_att = &gfx->depth_att;
   const struct anv_attachment *s_att = &gfx->stencil_att;
   if (d_att->vk_format == VK_FORMAT_UNDEFINED &&
       s_att->vk_format == VK_FORMAT_UNDEFINED)
      return;

   const struct anv_attachment *ds_att = d_att->iview ? d_att : s_att;
   if (ds_att->iview &&
       can_hiz_clear_att(cmd_buffer, batch, ds_att, attachment, rectCount, pRects)) {
      anv_fast_clear_depth_stencil(cmd_buffer, batch, ds_att->iview->image,
                                   attachment->aspectMask,
                                   ds_att->iview->planes[0].isl.base_level,
                                   ds_att->iview->planes[0].isl.base_array_layer,
                                   pRects[0].layerCount, pRects->rect,
                                   &attachment->clearValue.depthStencil);
      return;
   }

   bool clear_depth = attachment->aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT;
   bool clear_stencil = attachment->aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT;

   enum isl_format depth_format = ISL_FORMAT_UNSUPPORTED;
   if (d_att->vk_format != VK_FORMAT_UNDEFINED) {
      depth_format = anv_get_isl_format(cmd_buffer->device->physical,
                                        d_att->vk_format,
                                        VK_IMAGE_ASPECT_DEPTH_BIT,
                                        VK_IMAGE_TILING_OPTIMAL);
   }

   uint32_t binding_table;
   VkResult result =
      binding_table_for_surface_state(cmd_buffer,
                                      gfx->null_surface_state,
                                      &binding_table);
   if (result != VK_SUCCESS)
      return;

   /* If multiview is enabled we ignore baseArrayLayer and layerCount */
   if (gfx->view_mask) {
      u_foreach_bit(view_idx, gfx->view_mask) {
         for (uint32_t r = 0; r < rectCount; ++r) {
            const VkOffset2D offset = pRects[r].rect.offset;
            const VkExtent2D extent = pRects[r].rect.extent;
            VkClearDepthStencilValue value = attachment->clearValue.depthStencil;
            blorp_clear_attachments(batch, binding_table,
                                    depth_format,
                                    gfx->samples,
                                    view_idx, 1,
                                    offset.x, offset.y,
                                    offset.x + extent.width,
                                    offset.y + extent.height,
                                    false, color_value,
                                    clear_depth, value.depth,
                                    clear_stencil ? 0xff : 0, value.stencil);
         }
      }
      return;
   }

   for (uint32_t r = 0; r < rectCount; ++r) {
      const VkOffset2D offset = pRects[r].rect.offset;
      const VkExtent2D extent = pRects[r].rect.extent;
      VkClearDepthStencilValue value = attachment->clearValue.depthStencil;
      assert(pRects[r].layerCount != VK_REMAINING_ARRAY_LAYERS);
      blorp_clear_attachments(batch, binding_table,
                              depth_format,
                              gfx->samples,
                              pRects[r].baseArrayLayer,
                              pRects[r].layerCount,
                              offset.x, offset.y,
                              offset.x + extent.width, offset.y + extent.height,
                              false, color_value,
                              clear_depth, value.depth,
                              clear_stencil ? 0xff : 0, value.stencil);
   }
}

void anv_CmdClearAttachments(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    attachmentCount,
    const VkClearAttachment*                    pAttachments,
    uint32_t                                    rectCount,
    const VkClearRect*                          pRects)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   /* Because this gets called within a render pass, we tell blorp not to
    * trash our depth and stencil buffers.
    */
   struct blorp_batch batch;
   enum blorp_batch_flags flags = BLORP_BATCH_NO_EMIT_DEPTH_STENCIL;
   if (cmd_buffer->state.conditional_render_enabled) {
      anv_cmd_emit_conditional_render_predicate(cmd_buffer);
      flags |= BLORP_BATCH_PREDICATE_ENABLE;
   }
   anv_blorp_batch_init(cmd_buffer, &batch, flags);

   for (uint32_t a = 0; a < attachmentCount; ++a) {
      if (pAttachments[a].aspectMask & VK_IMAGE_ASPECT_ANY_COLOR_BIT_ANV) {
         assert(pAttachments[a].aspectMask == VK_IMAGE_ASPECT_COLOR_BIT);
         clear_color_attachment(cmd_buffer, &batch,
                                &pAttachments[a],
                                rectCount, pRects);
      } else {
         clear_depth_stencil_attachment(cmd_buffer, &batch,
                                        &pAttachments[a],
                                        rectCount, pRects);
      }
   }

   anv_blorp_batch_finish(&batch);
}

static void
anv_image_msaa_resolve(struct anv_cmd_buffer *cmd_buffer,
                       const struct anv_image *src_image,
                       enum isl_format src_format_override,
                       enum isl_aux_usage src_aux_usage,
                       uint32_t src_level, uint32_t src_base_layer,
                       const struct anv_image *dst_image,
                       enum isl_format dst_format_override,
                       enum isl_aux_usage dst_aux_usage,
                       uint32_t dst_level, uint32_t dst_base_layer,
                       VkImageAspectFlagBits aspect,
                       uint32_t src_x, uint32_t src_y,
                       uint32_t dst_x, uint32_t dst_y,
                       uint32_t width, uint32_t height,
                       uint32_t layer_count,
                       enum blorp_filter filter)
{
   struct blorp_batch batch;
   anv_blorp_batch_init(cmd_buffer, &batch, 0);
   assert((batch.flags & BLORP_BATCH_USE_COMPUTE) == 0);

   assert(src_image->vk.image_type == VK_IMAGE_TYPE_2D);
   assert(src_image->vk.samples > 1);
   assert((dst_image->vk.image_type == VK_IMAGE_TYPE_2D) ||
          (dst_image->vk.image_type == VK_IMAGE_TYPE_3D &&
           dst_base_layer == 0 && layer_count == 1));
   assert(dst_image->vk.samples == 1);

   struct blorp_surf src_surf, dst_surf;
   get_blorp_surf_for_anv_image(cmd_buffer, src_image, aspect,
                                VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                ANV_IMAGE_LAYOUT_EXPLICIT_AUX,
                                src_aux_usage, src_format_override,
                                false, &src_surf);
   get_blorp_surf_for_anv_image(cmd_buffer, dst_image, aspect,
                                VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                ANV_IMAGE_LAYOUT_EXPLICIT_AUX,
                                dst_aux_usage, dst_format_override,
                                false, &dst_surf);
   anv_cmd_buffer_mark_image_written(cmd_buffer, dst_image,
                                     aspect, dst_aux_usage,
                                     dst_level, dst_base_layer, layer_count);

   if (filter == BLORP_FILTER_NONE) {
      /* If no explicit filter is provided, then it's implied by the type of
       * the source image.
       */
      if ((src_surf.surf->usage & ISL_SURF_USAGE_DEPTH_BIT) ||
          (src_surf.surf->usage & ISL_SURF_USAGE_STENCIL_BIT) ||
          isl_format_has_int_channel(src_surf.surf->format)) {
         filter = BLORP_FILTER_SAMPLE_0;
      } else {
         filter = BLORP_FILTER_AVERAGE;
      }
   }

   for (uint32_t l = 0; l < layer_count; l++) {
      blorp_blit(&batch,
                 &src_surf, src_level, src_base_layer + l,
                 src_format_override, ISL_SWIZZLE_IDENTITY,
                 &dst_surf, dst_level, dst_base_layer + l,
                 dst_format_override, ISL_SWIZZLE_IDENTITY,
                 src_x, src_y, src_x + width, src_y + height,
                 dst_x, dst_y, dst_x + width, dst_y + height,
                 filter, false, false);
   }

   anv_blorp_batch_finish(&batch);
}

static enum blorp_filter
vk_to_blorp_resolve_mode(VkResolveModeFlagBits vk_mode)
{
   switch (vk_mode) {
   case VK_RESOLVE_MODE_SAMPLE_ZERO_BIT:
      return BLORP_FILTER_SAMPLE_0;
   case VK_RESOLVE_MODE_AVERAGE_BIT:
      return BLORP_FILTER_AVERAGE;
   case VK_RESOLVE_MODE_MIN_BIT:
      return BLORP_FILTER_MIN_SAMPLE;
   case VK_RESOLVE_MODE_MAX_BIT:
      return BLORP_FILTER_MAX_SAMPLE;
   default:
      return BLORP_FILTER_NONE;
   }
}

static inline struct isl_swizzle
conv_ycbcr_swizzle(const struct vk_format_ycbcr_plane *ycbcr_plane)
{
   const enum isl_channel_select vk_swiz_to_isl[] = {
      [VK_COMPONENT_SWIZZLE_R] = ISL_CHANNEL_SELECT_RED,
      [VK_COMPONENT_SWIZZLE_G] = ISL_CHANNEL_SELECT_GREEN,
      [VK_COMPONENT_SWIZZLE_B] = ISL_CHANNEL_SELECT_BLUE,
      [VK_COMPONENT_SWIZZLE_A] = ISL_CHANNEL_SELECT_ALPHA,
      [VK_COMPONENT_SWIZZLE_ZERO] = ISL_CHANNEL_SELECT_ZERO,
      [VK_COMPONENT_SWIZZLE_ONE] = ISL_CHANNEL_SELECT_ONE,
      [VK_COMPONENT_SWIZZLE_IDENTITY] = ISL_CHANNEL_SELECT_ZERO,
   };

   struct isl_swizzle swiz;
   swiz.r = vk_swiz_to_isl[ycbcr_plane->ycbcr_swizzle[0]];
   swiz.g = vk_swiz_to_isl[ycbcr_plane->ycbcr_swizzle[1]];
   swiz.b = vk_swiz_to_isl[ycbcr_plane->ycbcr_swizzle[2]];
   swiz.a = vk_swiz_to_isl[ycbcr_plane->ycbcr_swizzle[3]];

   return swiz;
}

void
anv_attachment_external_resolve(struct anv_cmd_buffer *cmd_buffer,
                                const struct anv_attachment *att)
{
   struct anv_cmd_graphics_state *gfx = &cmd_buffer->state.gfx;
   const struct anv_image_view *src_iview = att->iview;
   const struct anv_image_view *dst_iview = att->resolve_iview;
   const struct anv_image *src_image = src_iview->image;
   const struct anv_image *dst_image = dst_iview->image;
   enum isl_format src_format = src_iview->planes[0].isl.format;
   const VkRect2D render_area = gfx->render_area;
   uint32_t src_level = src_iview->planes[0].isl.base_level;
   uint32_t src_base_layer = src_iview->planes[0].isl.base_array_layer;
   uint32_t dst_level = dst_iview->planes[0].isl.base_level;
   uint32_t dst_base_layer = dst_iview->planes[0].isl.base_array_layer;
   uint32_t src_x1 = render_area.offset.x,
            src_x2 = render_area.offset.x + render_area.extent.width,
            src_y1 = render_area.offset.y,
            src_y2 = render_area.offset.y + render_area.extent.height;

   const VkImageAspectFlags plane_aspects[] = {
      VK_IMAGE_ASPECT_PLANE_0_BIT,
      VK_IMAGE_ASPECT_PLANE_1_BIT,
      VK_IMAGE_ASPECT_PLANE_2_BIT,
   };

   const struct vk_format_ycbcr_info *ycbcr_info =
      vk_format_get_ycbcr_info(dst_iview->vk.format);
   assert(ycbcr_info);

   struct blorp_surf src_surf, dst_surf;
   get_blorp_surf_for_anv_image(cmd_buffer, src_image,
                                VK_IMAGE_ASPECT_COLOR_BIT,
                                VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                att->resolve_layout,
                                ISL_AUX_USAGE_NONE, src_format,
                                false, &src_surf);

   struct blorp_batch batch;
   anv_blorp_batch_init(cmd_buffer, &batch, 0);

   for (uint8_t i = 0; i < ycbcr_info->n_planes; i++) {
      VkImageAspectFlags aspect_mask = plane_aspects[i];
      get_blorp_surf_for_anv_image(cmd_buffer, dst_image, aspect_mask,
                                   VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                   att->resolve_layout,
                                   ISL_AUX_USAGE_NONE,
                                   dst_iview->planes[i].isl.format,
                                   false, &dst_surf);

      anv_cmd_buffer_mark_image_written(cmd_buffer, dst_image,
                                        aspect_mask, dst_surf.aux_usage,
                                        dst_level, dst_base_layer, 1);

      const struct vk_format_ycbcr_plane *plane = &ycbcr_info->planes[i];
      struct isl_swizzle plane_swizzle = conv_ycbcr_swizzle(plane);

      uint32_t dst_x1 = render_area.offset.x / plane->denominator_scales[0],
               dst_x2 = (render_area.offset.x + render_area.extent.width) /
                        plane->denominator_scales[0],
               dst_y1 = render_area.offset.y / plane->denominator_scales[1],
               dst_y2 = (render_area.offset.y + render_area.extent.height) /
                        plane->denominator_scales[1];

      blorp_blit(&batch,
                 &src_surf, src_level, src_base_layer,
                 src_format, plane_swizzle,
                 &dst_surf, dst_level, dst_base_layer,
                 dst_iview->planes[i].isl.format, ISL_SWIZZLE_IDENTITY,
                 src_x1, src_y1, src_x2, src_y2,
                 dst_x1, dst_y1, dst_x2, dst_y2,
                 BLORP_FILTER_BILINEAR, false, false);
   }

   anv_blorp_batch_finish(&batch);
}

void
anv_attachment_msaa_resolve(struct anv_cmd_buffer *cmd_buffer,
                            const struct anv_attachment *att,
                            VkImageLayout layout,
                            VkImageAspectFlagBits aspect)
{
   struct anv_cmd_graphics_state *gfx = &cmd_buffer->state.gfx;
   const struct anv_image_view *src_iview = att->iview;
   const struct anv_image_view *dst_iview = att->resolve_iview;

   enum isl_aux_usage src_aux_usage =
      anv_layout_to_aux_usage(cmd_buffer->device->info,
                              src_iview->image, aspect,
                              VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                              layout,
                              cmd_buffer->queue_family->queueFlags);

   enum isl_aux_usage dst_aux_usage =
      anv_layout_to_aux_usage(cmd_buffer->device->info,
                              dst_iview->image, aspect,
                              VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                              att->resolve_layout,
                              cmd_buffer->queue_family->queueFlags);

   enum blorp_filter filter = vk_to_blorp_resolve_mode(att->resolve_mode);

   /* Depth/stencil should not use their view format for resolve because they
    * go in pairs.
    */
   enum isl_format src_format = ISL_FORMAT_UNSUPPORTED;
   enum isl_format dst_format = ISL_FORMAT_UNSUPPORTED;
   if (!(aspect & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))) {
      src_format = src_iview->planes[0].isl.format;
      dst_format = dst_iview->planes[0].isl.format;
   }

   if (att->skip_srgb_decode) {
      src_format = isl_format_srgb_to_linear(src_format);
      dst_format = isl_format_srgb_to_linear(dst_format);
   }

   const VkRect2D render_area = gfx->render_area;
   if (gfx->view_mask == 0) {
      anv_image_msaa_resolve(cmd_buffer,
                             src_iview->image, src_format, src_aux_usage,
                             src_iview->planes[0].isl.base_level,
                             src_iview->planes[0].isl.base_array_layer,
                             dst_iview->image, dst_format, dst_aux_usage,
                             dst_iview->planes[0].isl.base_level,
                             dst_iview->planes[0].isl.base_array_layer,
                             aspect,
                             render_area.offset.x, render_area.offset.y,
                             render_area.offset.x, render_area.offset.y,
                             render_area.extent.width,
                             render_area.extent.height,
                             gfx->layer_count, filter);
   } else {
      uint32_t res_view_mask = gfx->view_mask;
      while (res_view_mask) {
         int i = u_bit_scan(&res_view_mask);

         anv_image_msaa_resolve(cmd_buffer,
                                src_iview->image, src_format, src_aux_usage,
                                src_iview->planes[0].isl.base_level,
                                src_iview->planes[0].isl.base_array_layer + i,
                                dst_iview->image, dst_format, dst_aux_usage,
                                dst_iview->planes[0].isl.base_level,
                                dst_iview->planes[0].isl.base_array_layer + i,
                                aspect,
                                render_area.offset.x, render_area.offset.y,
                                render_area.offset.x, render_area.offset.y,
                                render_area.extent.width,
                                render_area.extent.height,
                                1, filter);
      }
   }
}

static void
resolve_image(struct anv_cmd_buffer *cmd_buffer,
              const struct anv_image *src_image,
              VkImageLayout src_image_layout,
              const struct anv_image *dst_image,
              VkImageLayout dst_image_layout,
              const VkImageResolve2 *region,
              const VkResolveImageModeInfoKHR *res_info)
{
   assert(region->srcSubresource.aspectMask == region->dstSubresource.aspectMask);
   assert(vk_image_subresource_layer_count(&src_image->vk, &region->srcSubresource) ==
          vk_image_subresource_layer_count(&dst_image->vk, &region->dstSubresource));

   const uint32_t layer_count =
      vk_image_subresource_layer_count(&dst_image->vk, &region->dstSubresource);
   const bool skip_srgb_decode =
      res_info ?
      (res_info->flags & VK_RESOLVE_IMAGE_SKIP_TRANSFER_FUNCTION_BIT_KHR) :
      false;

   anv_foreach_image_aspect_bit(aspect_bit, src_image,
                                region->srcSubresource.aspectMask) {
      const VkImageAspectFlags aspect = (1 << aspect_bit);
      const uint32_t plane = anv_image_aspect_to_plane(src_image, aspect);

      enum isl_aux_usage src_aux_usage =
         anv_layout_to_aux_usage(cmd_buffer->device->info, src_image,
                                 aspect,
                                 VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                 src_image_layout,
                                 cmd_buffer->queue_family->queueFlags);
      enum isl_aux_usage dst_aux_usage =
         anv_layout_to_aux_usage(cmd_buffer->device->info, dst_image,
                                 aspect,
                                 VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                 dst_image_layout,
                                 cmd_buffer->queue_family->queueFlags);

      const enum blorp_filter filter = res_info ?
         (aspect & VK_IMAGE_ASPECT_STENCIL_BIT) ?
         vk_to_blorp_resolve_mode(res_info->stencilResolveMode) :
         vk_to_blorp_resolve_mode(res_info->resolveMode) :
         BLORP_FILTER_NONE;

      enum isl_format src_format = src_image->planes[plane].primary_surface.isl.format;
      enum isl_format dst_format = dst_image->planes[plane].primary_surface.isl.format;
      if (skip_srgb_decode) {
         src_format = isl_format_srgb_to_linear(src_format);
         dst_format = isl_format_srgb_to_linear(dst_format);
      }

      anv_image_msaa_resolve(cmd_buffer,
                             src_image, src_format, src_aux_usage,
                             region->srcSubresource.mipLevel,
                             region->srcSubresource.baseArrayLayer,
                             dst_image, dst_format, dst_aux_usage,
                             region->dstSubresource.mipLevel,
                             region->dstSubresource.baseArrayLayer,
                             aspect,
                             region->srcOffset.x,
                             region->srcOffset.y,
                             region->dstOffset.x,
                             region->dstOffset.y,
                             region->extent.width,
                             region->extent.height,
                             layer_count, filter);
   }
}

void anv_CmdResolveImage2(
    VkCommandBuffer                             commandBuffer,
    const VkResolveImageInfo2*                  pResolveImageInfo)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_image, src_image, pResolveImageInfo->srcImage);
   ANV_FROM_HANDLE(anv_image, dst_image, pResolveImageInfo->dstImage);

   const VkResolveImageModeInfoKHR *res_info =
      vk_find_struct_const(pResolveImageInfo->pNext,
                           RESOLVE_IMAGE_MODE_INFO_KHR);

   for (uint32_t r = 0; r < pResolveImageInfo->regionCount; r++) {
      resolve_image(cmd_buffer,
                    src_image, pResolveImageInfo->srcImageLayout,
                    dst_image, pResolveImageInfo->dstImageLayout,
                    &pResolveImageInfo->pRegions[r],
                    res_info);
   }
}

void
anv_image_clear_color(struct anv_cmd_buffer *cmd_buffer,
                      const struct anv_image *image,
                      VkImageAspectFlagBits aspect,
                      enum isl_aux_usage aux_usage,
                      enum isl_format format, struct isl_swizzle swizzle,
                      uint32_t level, uint32_t base_layer, uint32_t layer_count,
                      VkRect2D area, union isl_color_value clear_color)
{
   assert((aspect & ~VK_IMAGE_ASPECT_ANY_COLOR_BIT_ANV) == 0);
   assert(util_bitcount(aspect) == 1);

   /* We don't support planar images with multisampling yet */
   assert(image->vk.samples == 1 || image->n_planes == 1);

   struct blorp_batch batch;
   anv_blorp_batch_init(cmd_buffer, &batch, 0);

   struct blorp_surf surf;
   get_blorp_surf_for_anv_image(cmd_buffer, image, aspect,
                                VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                ANV_IMAGE_LAYOUT_EXPLICIT_AUX,
                                aux_usage, format, false, &surf);
   anv_cmd_buffer_mark_image_written(cmd_buffer, image, aspect, aux_usage,
                                     level, base_layer, layer_count);

   blorp_clear(&batch, &surf, format, anv_swizzle_for_render(swizzle),
               level, base_layer, layer_count,
               area.offset.x, area.offset.y,
               area.offset.x + area.extent.width,
               area.offset.y + area.extent.height,
               clear_color, 0 /* color_write_disable */);

   anv_blorp_batch_finish(&batch);
}

void
anv_image_clear_depth_stencil(struct anv_cmd_buffer *cmd_buffer,
                              const struct anv_image *image,
                              VkImageAspectFlags aspects,
                              enum isl_aux_usage depth_aux_usage,
                              uint32_t level,
                              uint32_t base_layer, uint32_t layer_count,
                              VkRect2D area,
                              const VkClearDepthStencilValue *clear_value)
{
   assert(image->vk.aspects & (VK_IMAGE_ASPECT_DEPTH_BIT |
                               VK_IMAGE_ASPECT_STENCIL_BIT));
   assert(layer_count > 0);

   struct blorp_batch batch;
   anv_blorp_batch_init(cmd_buffer, &batch, 0);
   assert((batch.flags & BLORP_BATCH_USE_COMPUTE) == 0);

   struct blorp_surf depth = {};
   if (aspects & VK_IMAGE_ASPECT_DEPTH_BIT) {
      get_blorp_surf_for_anv_image(cmd_buffer,
                                   image, VK_IMAGE_ASPECT_DEPTH_BIT,
                                   0, ANV_IMAGE_LAYOUT_EXPLICIT_AUX,
                                   depth_aux_usage, ISL_FORMAT_UNSUPPORTED,
                                   false, &depth);
   }

   struct blorp_surf stencil = {};
   if (aspects & VK_IMAGE_ASPECT_STENCIL_BIT) {
      const uint32_t plane =
         anv_image_aspect_to_plane(image, VK_IMAGE_ASPECT_STENCIL_BIT);
      get_blorp_surf_for_anv_image(cmd_buffer,
                                   image, VK_IMAGE_ASPECT_STENCIL_BIT,
                                   0, ANV_IMAGE_LAYOUT_EXPLICIT_AUX,
                                   image->planes[plane].aux_usage,
                                   ISL_FORMAT_UNSUPPORTED, false, &stencil);
   }

   /* Blorp may choose to clear stencil using RGBA32_UINT for better
    * performance.  If it does this, we need to flush it out of the depth
    * cache before rendering to it.
    */
   anv_add_pending_pipe_bits(cmd_buffer,
                             ANV_PIPE_DEPTH_CACHE_FLUSH_BIT |
                             ANV_PIPE_END_OF_PIPE_SYNC_BIT,
                             "before clear DS");

   blorp_clear_depth_stencil(&batch, &depth, &stencil,
                             level, base_layer, layer_count,
                             area.offset.x, area.offset.y,
                             area.offset.x + area.extent.width,
                             area.offset.y + area.extent.height,
                             aspects & VK_IMAGE_ASPECT_DEPTH_BIT,
                             clear_value->depth,
                             (aspects & VK_IMAGE_ASPECT_STENCIL_BIT) ? 0xff : 0,
                             clear_value->stencil);

   /* Blorp may choose to clear stencil using RGBA32_UINT for better
    * performance.  If it does this, we need to flush it out of the render
    * cache before someone starts trying to do stencil on it.
    */
   anv_add_pending_pipe_bits(cmd_buffer,
                             ANV_PIPE_RENDER_TARGET_CACHE_FLUSH_BIT |
                             ANV_PIPE_END_OF_PIPE_SYNC_BIT,
                             "after clear DS");

   anv_blorp_batch_finish(&batch);
}

void
anv_image_hiz_op(struct anv_cmd_buffer *cmd_buffer,
                 const struct anv_image *image,
                 VkImageAspectFlagBits aspect, uint32_t level,
                 uint32_t base_layer, uint32_t layer_count,
                 enum isl_aux_op hiz_op)
{
   assert(aspect == VK_IMAGE_ASPECT_DEPTH_BIT);
   assert(base_layer + layer_count <= anv_image_aux_layers(image, aspect, level));
   const uint32_t plane = anv_image_aspect_to_plane(image, aspect);
   assert(plane == 0);

   struct blorp_batch batch;
   anv_blorp_batch_init(cmd_buffer, &batch, 0);
   assert((batch.flags & BLORP_BATCH_USE_COMPUTE) == 0);

   struct blorp_surf surf;
   get_blorp_surf_for_anv_image(cmd_buffer,
                                image, VK_IMAGE_ASPECT_DEPTH_BIT,
                                0, ANV_IMAGE_LAYOUT_EXPLICIT_AUX,
                                image->planes[plane].aux_usage,
                                ISL_FORMAT_UNSUPPORTED, false, &surf);

   blorp_hiz_op(&batch, &surf, level, base_layer, layer_count, hiz_op);

   anv_blorp_batch_finish(&batch);
}

void
anv_image_hiz_clear(struct anv_cmd_buffer *cmd_buffer,
                    const struct anv_image *image,
                    VkImageAspectFlags aspects,
                    uint32_t level,
                    uint32_t base_layer, uint32_t layer_count,
                    VkRect2D area,
                    const VkClearDepthStencilValue *clear_value)
{
   struct blorp_batch batch;
   anv_blorp_batch_init(cmd_buffer, &batch, 0);
   assert((batch.flags & BLORP_BATCH_USE_COMPUTE) == 0);

   anv_fast_clear_depth_stencil(cmd_buffer, &batch, image, aspects, level,
                                base_layer, layer_count, area, clear_value);

   anv_blorp_batch_finish(&batch);
}

void
anv_image_mcs_op(struct anv_cmd_buffer *cmd_buffer,
                 const struct anv_image *image,
                 enum isl_format format, struct isl_swizzle swizzle,
                 VkImageAspectFlagBits aspect,
                 uint32_t base_layer, uint32_t layer_count,
                 enum isl_aux_op mcs_op, union isl_color_value *clear_value,
                 bool predicate)
{
   struct blorp_batch batch;
   anv_blorp_batch_init(cmd_buffer, &batch,
                        BLORP_BATCH_PREDICATE_ENABLE * predicate);
   assert((batch.flags & BLORP_BATCH_USE_COMPUTE) == 0);

   exec_mcs_op(cmd_buffer, &batch, image, format, swizzle, aspect,
               base_layer, layer_count, mcs_op, clear_value);

   anv_blorp_batch_finish(&batch);
}

void
anv_image_ccs_op(struct anv_cmd_buffer *cmd_buffer,
                 const struct anv_image *image,
                 enum isl_format format, struct isl_swizzle swizzle,
                 VkImageAspectFlagBits aspect, uint32_t level,
                 uint32_t base_layer, uint32_t layer_count,
                 enum isl_aux_op ccs_op, union isl_color_value *clear_value,
                 bool predicate)
{
   struct blorp_batch batch;
   anv_blorp_batch_init(cmd_buffer, &batch,
                        BLORP_BATCH_PREDICATE_ENABLE * predicate);
   assert((batch.flags & BLORP_BATCH_USE_COMPUTE) == 0);

   exec_ccs_op(cmd_buffer, &batch, image, format, swizzle, aspect, level,
               base_layer, layer_count, ccs_op, clear_value);

   anv_blorp_batch_finish(&batch);
}
