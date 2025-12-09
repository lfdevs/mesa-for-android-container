/*
 * Copyright 2018 Collabora Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "zink_context.h"
#include "zink_format.h"
#include "zink_resource.h"
#include "zink_screen.h"
#include "zink_surface.h"
#include "zink_kopper.h"

#include "util/format/u_format.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"

static VkImageViewType
vkviewtype_from_pipe(enum pipe_texture_target target, bool need_2D)
{
   switch (target) {
   case PIPE_TEXTURE_1D:
      return need_2D ? VK_IMAGE_VIEW_TYPE_2D : VK_IMAGE_VIEW_TYPE_1D;

   case PIPE_TEXTURE_1D_ARRAY:
      return need_2D ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_1D_ARRAY;

   case PIPE_TEXTURE_2D:
   case PIPE_TEXTURE_RECT:
      return VK_IMAGE_VIEW_TYPE_2D;

   case PIPE_TEXTURE_2D_ARRAY:
      return VK_IMAGE_VIEW_TYPE_2D_ARRAY;

   case PIPE_TEXTURE_CUBE:
      return VK_IMAGE_VIEW_TYPE_CUBE;

   case PIPE_TEXTURE_CUBE_ARRAY:
      return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
      break;

   case PIPE_TEXTURE_3D:
      return VK_IMAGE_VIEW_TYPE_3D;

   default:
      UNREACHABLE("unsupported target");
   }
}

VkImageViewCreateInfo
create_ivci(struct zink_screen *screen,
            struct zink_resource *res,
            const struct pipe_surface *templ,
            enum pipe_texture_target target)
{
   VkImageViewCreateInfo ivci;
   /* zero holes since this is hashed */
   memset(&ivci, 0, sizeof(VkImageViewCreateInfo));
   ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
   ivci.image = res->obj->image;
   ivci.viewType = vkviewtype_from_pipe(target, res->need_2D);
   ivci.format = res->base.b.format == PIPE_FORMAT_A8_UNORM ? res->format : zink_get_format(screen, templ->format);
   assert(ivci.format != VK_FORMAT_UNDEFINED);

   /* TODO: it's currently illegal to use non-identity swizzles for framebuffer attachments,
    * but if that ever changes, this will be useful
   const struct util_format_description *desc = util_format_description(templ->format);
   ivci.components.r = zink_component_mapping(zink_clamp_void_swizzle(desc, PIPE_SWIZZLE_X));
   ivci.components.g = zink_component_mapping(zink_clamp_void_swizzle(desc, PIPE_SWIZZLE_Y));
   ivci.components.b = zink_component_mapping(zink_clamp_void_swizzle(desc, PIPE_SWIZZLE_Z));
   ivci.components.a = zink_component_mapping(zink_clamp_void_swizzle(desc, PIPE_SWIZZLE_W));
   */
   ivci.components.r = VK_COMPONENT_SWIZZLE_R;
   ivci.components.g = VK_COMPONENT_SWIZZLE_G;
   ivci.components.b = VK_COMPONENT_SWIZZLE_B;
   ivci.components.a = VK_COMPONENT_SWIZZLE_A;

   ivci.subresourceRange.aspectMask = res->aspect;
   ivci.subresourceRange.baseMipLevel = templ->level;
   ivci.subresourceRange.levelCount = 1;
   ivci.subresourceRange.baseArrayLayer = templ->first_layer;
   ivci.subresourceRange.layerCount = 1 + templ->last_layer - templ->first_layer;
   assert(ivci.viewType != VK_IMAGE_VIEW_TYPE_3D || ivci.subresourceRange.baseArrayLayer == 0);
   assert(ivci.viewType != VK_IMAGE_VIEW_TYPE_3D || ivci.subresourceRange.layerCount == 1);
   /* ensure cube image types get clamped to 2D/2D_ARRAY as expected for partial views */
   ivci.viewType = zink_surface_clamp_viewtype(ivci.viewType, templ->first_layer, templ->last_layer, res->base.b.array_size);

   return ivci;
}

static void
apply_view_usage_for_format(struct zink_screen *screen, struct pipe_resource *pres, enum pipe_format format, VkImageViewCreateInfo *ivci, VkImageViewUsageCreateInfo *usage_info)
{
   struct zink_resource *res = zink_resource(pres);
   VkFormatFeatureFlags feats = res->linear ?
                                zink_get_format_props(screen, format)->linearTilingFeatures :
                                zink_get_format_props(screen, format)->optimalTilingFeatures;
   uint64_t attachment = (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT);
   usage_info->usage = res->obj->vkusage & ~attachment;
   if (res->obj->modifier_aspect) {
      feats = res->obj->vkfeats;
      /* intersect format features for current modifier */
      for (unsigned i = 0; i < screen->modifier_props[format].drmFormatModifierCount; i++) {
         if (res->obj->modifier == screen->modifier_props[format].pDrmFormatModifierProperties[i].drmFormatModifier)
            feats &= screen->modifier_props[format].pDrmFormatModifierProperties[i].drmFormatModifierTilingFeatures;
      }
   }
   /* if the format features don't support framebuffer attachment, use VkImageViewUsageCreateInfo to remove it */
   if ((res->obj->vkusage & attachment) &&
       !(feats & (VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))) {
      ivci->pNext = usage_info;
   } else {
      ivci->pNext = NULL;
   }
}

static struct zink_surface *
create_surface(struct pipe_context *pctx,
               struct pipe_resource *pres,
               const struct zink_surface_key *templ,
               VkImageViewCreateInfo *ivci)
{
   struct zink_screen *screen = zink_screen(pctx->screen);

   struct zink_surface *surface = CALLOC_STRUCT(zink_surface);
   if (!surface)
      return NULL;

   assert(ivci->image);
   VkImageViewUsageCreateInfo usage_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO };
   apply_view_usage_for_format(screen, pres, templ->format, ivci, &usage_info);
   VkResult result = VKSCR(CreateImageView)(screen->dev, ivci, NULL,
                                            &surface->image_view);
   if (result != VK_SUCCESS) {
      mesa_loge("ZINK: vkCreateImageView failed (%s)", vk_Result_to_str(result));
      FREE(surface);
      return NULL;
   }
   surface->key = *templ;

   return surface;
}

static uint32_t
hash_key(const void *key)
{
   return _mesa_hash_data(key, sizeof(struct zink_surface_key));
}

static struct set *
get_surface_cache(struct zink_resource *res)
{
   struct kopper_displaytarget *cdt = res->obj->dt;
   assert(!res->obj->dt || res->obj->dt_idx != UINT32_MAX);
   return res->obj->dt ? &cdt->swapchain->images[res->obj->dt_idx].surface_cache : &res->obj->surface_cache;
}

static unsigned
componentmapping_to_pipe(VkComponentSwizzle c)
{
   switch (c) {
   case VK_COMPONENT_SWIZZLE_ZERO:
      return PIPE_SWIZZLE_0;
   case VK_COMPONENT_SWIZZLE_ONE:
      return PIPE_SWIZZLE_1;
   case VK_COMPONENT_SWIZZLE_R:
      return PIPE_SWIZZLE_X;
   case VK_COMPONENT_SWIZZLE_G:
      return PIPE_SWIZZLE_Y;
   case VK_COMPONENT_SWIZZLE_B:
      return PIPE_SWIZZLE_Z;
   case VK_COMPONENT_SWIZZLE_A:
      return PIPE_SWIZZLE_W;
   default:
      UNREACHABLE("unknown swizzle");
   }
}

static struct zink_surface_key
templ_to_key(const struct pipe_surface *templ, const VkImageViewCreateInfo *ivci)
{
   VkImageViewType baseviewtype = vkviewtype_from_pipe(templ->texture->target, zink_resource(templ->texture)->need_2D);
   enum zink_surface_type type = ZINK_SURFACE_NORMAL;
   if (baseviewtype != ivci->viewType) {
      switch (ivci->viewType) {
      case VK_IMAGE_VIEW_TYPE_1D_ARRAY:
      case VK_IMAGE_VIEW_TYPE_2D_ARRAY:
         type = ZINK_SURFACE_ARRAYED;
         break;
      default:
         type = ZINK_SURFACE_LAYERED;
         break;
      }
   }
   struct zink_surface_key key = {
      .format = templ->format,
      .viewtype = type,
      .stencil = ivci->subresourceRange.aspectMask == VK_IMAGE_ASPECT_STENCIL_BIT,
      .swizzle_r = componentmapping_to_pipe(ivci->components.r),
      .swizzle_g = componentmapping_to_pipe(ivci->components.g),
      .swizzle_b = componentmapping_to_pipe(ivci->components.b),
      .swizzle_a = componentmapping_to_pipe(ivci->components.a),
      .first_level = ivci->subresourceRange.baseMipLevel,
      .level_count = ivci->subresourceRange.levelCount,
      .first_layer = templ->first_layer,
      .last_layer = templ->last_layer,
   };
   return key;
}

/* get a cached surface for a shader descriptor */
struct zink_surface *
zink_get_surface(struct zink_context *ctx,
            const struct pipe_surface *templ,
            VkImageViewCreateInfo *ivci)
{
   struct zink_surface *surface = NULL;
   struct zink_resource *res = zink_resource(templ->texture);

   /* not acquired */
   if (res->obj->dt && res->obj->dt_idx == UINT32_MAX)
      return NULL;
   if (!res->obj->dt && zink_format_needs_mutable(res->base.b.format, templ->format))
      /* mutable not set by default */
      zink_resource_object_init_mutable(ctx, res);
   /* reset for mutable obj switch */
   ivci->image = res->obj->image;
   struct zink_surface_key key = templ_to_key(templ, ivci);
   uint32_t hash = hash_key(&key);

   simple_mtx_lock(&res->obj->surface_mtx);
   struct set *ht = get_surface_cache(res);
   bool found = false;
   struct set_entry *entry = _mesa_set_search_or_add_pre_hashed(ht, hash, &key, &found);

   if (!found) {
      surface = create_surface(&ctx->base, &res->base.b, &key, ivci);
      if (!surface) {
         _mesa_set_remove(ht, entry);
         simple_mtx_unlock(&res->obj->surface_mtx);
         return NULL;
      }
      entry->key = surface;
   } else {
      surface = (void*)entry->key;
   }
   simple_mtx_unlock(&res->obj->surface_mtx);

   return surface;
}

static VkImageViewCreateInfo
create_fb_ivci(struct zink_screen *screen, struct zink_resource *res, const struct pipe_surface *templ)
{
   bool is_array = templ->last_layer != templ->first_layer;
   enum pipe_texture_target target_2d[] = {PIPE_TEXTURE_2D, PIPE_TEXTURE_2D_ARRAY};

   return create_ivci(screen, res, templ, res->base.b.target == PIPE_TEXTURE_3D ? target_2d[is_array] : res->base.b.target);
}

struct zink_surface *
zink_create_fb_surface(struct pipe_context *pctx,
                       const struct pipe_surface *templ)
{
   struct zink_resource *res = zink_resource(templ->texture);

   VkImageViewCreateInfo ivci = create_fb_ivci(zink_screen(pctx->screen), res, templ);
   return zink_get_surface(zink_context(pctx), templ, &ivci);
}

struct zink_surface *
zink_create_transient_surface(struct zink_context *ctx, const struct pipe_surface *psurf, unsigned nr_samples)
{
   struct zink_resource *res = zink_resource(psurf->texture);
   struct zink_resource *transient = res->transient;
   assert(nr_samples > 1);
   if (!res->transient) {
      /* transient fb attachment: not cached */
      struct pipe_resource rtempl = *psurf->texture;
      rtempl.nr_samples = nr_samples;
      rtempl.bind &= ~(PIPE_BIND_LINEAR | ZINK_BIND_DMABUF);
      rtempl.bind |= ZINK_BIND_TRANSIENT;
      if (zink_format_needs_mutable(rtempl.format, psurf->format))
         rtempl.bind |= ZINK_BIND_MUTABLE;
      res->transient = zink_resource(ctx->base.screen->resource_create(ctx->base.screen, &rtempl));
      transient = res->transient;
      if (unlikely(!transient)) {
         mesa_loge("ZINK: failed to create transient resource!");
         return NULL;
      }
   }

   VkImageViewCreateInfo ivci = create_fb_ivci(zink_screen(ctx->base.screen), res, psurf);
   ivci.image = transient->obj->image;
   ivci.pNext = NULL;
   struct pipe_surface templ = *psurf;
   templ.texture = &transient->base.b;
   return zink_get_surface(ctx, &templ, &ivci);
}
