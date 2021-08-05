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

#include "zink_resource.h"

#include "zink_batch.h"
#include "zink_context.h"
#include "zink_fence.h"
#include "zink_program.h"
#include "zink_screen.h"

#ifdef VK_USE_PLATFORM_METAL_EXT
#include "QuartzCore/CAMetalLayer.h"
#endif
#include "vulkan/wsi/wsi_common.h"

#include "util/slab.h"
#include "util/u_blitter.h"
#include "util/u_debug.h"
#include "util/format/u_format.h"
#include "util/u_transfer_helper.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_upload_mgr.h"

#include "frontend/sw_winsys.h"

#ifndef _WIN32
#define ZINK_USE_DMABUF
#endif

#ifdef ZINK_USE_DMABUF
#include "drm-uapi/drm_fourcc.h"
#else
/* these won't actually be used */
#define DRM_FORMAT_MOD_INVALID 0
#define DRM_FORMAT_MOD_LINEAR 0
#endif

static void
zink_transfer_flush_region(struct pipe_context *pctx,
                           struct pipe_transfer *ptrans,
                           const struct pipe_box *box);
static void *
zink_transfer_map(struct pipe_context *pctx,
                  struct pipe_resource *pres,
                  unsigned level,
                  unsigned usage,
                  const struct pipe_box *box,
                  struct pipe_transfer **transfer);
static void
zink_transfer_unmap(struct pipe_context *pctx,
                    struct pipe_transfer *ptrans);

void
debug_describe_zink_resource_object(char *buf, const struct zink_resource_object *ptr)
{
   sprintf(buf, "zink_resource_object");
}

static uint32_t
get_resource_usage(struct zink_resource *res)
{
   bool reads = zink_batch_usage_exists(res->obj->reads);
   bool writes = zink_batch_usage_exists(res->obj->writes);
   uint32_t batch_uses = 0;
   if (reads)
      batch_uses |= ZINK_RESOURCE_ACCESS_READ;
   if (writes)
      batch_uses |= ZINK_RESOURCE_ACCESS_WRITE;
   return batch_uses;
}

static uint32_t
mem_hash(const void *key)
{
   const struct mem_key *mkey = key;
   return _mesa_hash_data(&mkey->key, sizeof(mkey->key));
}

static bool
mem_equals(const void *a, const void *b)
{
   const struct mem_key *ma = a;
   const struct mem_key *mb = b;
   return !memcmp(&ma->key, &mb->key, sizeof(ma->key));
}

static void
cache_or_free_mem(struct zink_screen *screen, struct zink_resource_object *obj)
{
   if (obj->mkey.key.heap_index != UINT32_MAX) {
      simple_mtx_lock(&screen->mem_cache_mtx);
      struct hash_entry *he = _mesa_hash_table_search_pre_hashed(screen->resource_mem_cache, obj->mem_hash, &obj->mkey);
      assert(he);
      struct util_dynarray *array = he->data;
      struct mem_key *mkey = (void*)he->key;

      unsigned seen = mkey->seen_count;
      mkey->seen_count--;
      if (util_dynarray_num_elements(array, struct mem_cache_entry) < seen) {
         struct mem_cache_entry mc = { obj->mem, obj->map };
         screen->mem_cache_size += obj->size;
         if (sizeof(void*) == 4 && obj->map) {
            vkUnmapMemory(screen->dev, obj->mem);
            mc.map = NULL;
         }
         util_dynarray_append(array, struct mem_cache_entry, mc);
         simple_mtx_unlock(&screen->mem_cache_mtx);
         return;
      }
      simple_mtx_unlock(&screen->mem_cache_mtx);
   }
   vkFreeMemory(screen->dev, obj->mem, NULL);
}

void
zink_destroy_resource_object(struct zink_screen *screen, struct zink_resource_object *obj)
{
   if (obj->is_buffer) {
      if (obj->sbuffer)
         vkDestroyBuffer(screen->dev, obj->sbuffer, NULL);
      vkDestroyBuffer(screen->dev, obj->buffer, NULL);
   } else {
      vkDestroyImage(screen->dev, obj->image, NULL);
   }

   zink_descriptor_set_refs_clear(&obj->desc_set_refs, obj);
   cache_or_free_mem(screen, obj);
   FREE(obj);
}

static void
zink_resource_destroy(struct pipe_screen *pscreen,
                      struct pipe_resource *pres)
{
   struct zink_screen *screen = zink_screen(pscreen);
   struct zink_resource *res = zink_resource(pres);
   if (pres->target == PIPE_BUFFER) {
      util_range_destroy(&res->valid_buffer_range);
      util_idalloc_mt_free(&screen->buffer_ids, res->base.buffer_id_unique);
   }

   zink_resource_object_reference(screen, &res->obj, NULL);
   zink_resource_object_reference(screen, &res->scanout_obj, NULL);
   threaded_resource_deinit(pres);
   FREE(res);
}

static uint32_t
get_memory_type_index(struct zink_screen *screen,
                      const VkMemoryRequirements *reqs,
                      VkMemoryPropertyFlags props)
{
   int32_t idx = -1;
   for (uint32_t i = 0u; i < VK_MAX_MEMORY_TYPES; i++) {
      if (((reqs->memoryTypeBits >> i) & 1) == 1) {
         if ((screen->info.mem_props.memoryTypes[i].propertyFlags & props) == props) {
            if (!(props & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) &&
                screen->info.mem_props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) {
               idx = i;
            } else
               return i;
         }
      }
   }
   if (idx >= 0)
      return idx;

   if (props & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) {
      /* if no suitable cached memory can be found, fall back
       * to non-cached memory instead.
       */
      return get_memory_type_index(screen, reqs,
         props & ~VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
   }

   unreachable("Unsupported memory-type");
   return 0;
}

static VkImageAspectFlags
aspect_from_format(enum pipe_format fmt)
{
   if (util_format_is_depth_or_stencil(fmt)) {
      VkImageAspectFlags aspect = 0;
      const struct util_format_description *desc = util_format_description(fmt);
      if (util_format_has_depth(desc))
         aspect |= VK_IMAGE_ASPECT_DEPTH_BIT;
      if (util_format_has_stencil(desc))
         aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
      return aspect;
   } else
     return VK_IMAGE_ASPECT_COLOR_BIT;
}

static VkBufferCreateInfo
create_bci(struct zink_screen *screen, const struct pipe_resource *templ, unsigned bind)
{
   VkBufferCreateInfo bci = {0};
   bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
   bci.size = templ->width0;
   assert(bci.size > 0);

   bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
               VK_BUFFER_USAGE_TRANSFER_DST_BIT |
               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

   bci.usage |= VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT |
                VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT |
                VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_COUNTER_BUFFER_BIT_EXT;

   if (bind & PIPE_BIND_SHADER_IMAGE)
      bci.usage |= VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;

   if (templ->flags & PIPE_RESOURCE_FLAG_SPARSE)
      bci.flags |= VK_BUFFER_CREATE_SPARSE_BINDING_BIT;
   return bci;
}

static bool
check_ici(struct zink_screen *screen, VkImageCreateInfo *ici, uint64_t modifier)
{
   VkImageFormatProperties image_props;
   VkResult ret;
   assert(modifier == DRM_FORMAT_MOD_INVALID ||
          (screen->vk.GetPhysicalDeviceImageFormatProperties2 && screen->info.have_EXT_image_drm_format_modifier));
   if (screen->vk.GetPhysicalDeviceImageFormatProperties2) {
      VkImageFormatProperties2 props2 = {0};
      props2.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
      VkPhysicalDeviceImageFormatInfo2 info = {0};
      info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
      info.format = ici->format;
      info.type = ici->imageType;
      info.tiling = ici->tiling;
      info.usage = ici->usage;
      info.flags = ici->flags;

      VkPhysicalDeviceImageDrmFormatModifierInfoEXT mod_info;
      if (modifier != DRM_FORMAT_MOD_INVALID) {
         mod_info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT;
         mod_info.pNext = NULL;
         mod_info.drmFormatModifier = modifier;
         mod_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
         mod_info.queueFamilyIndexCount = 0;
         info.pNext = &mod_info;
      }
      ret = screen->vk.GetPhysicalDeviceImageFormatProperties2(screen->pdev, &info, &props2);
      image_props = props2.imageFormatProperties;
   } else
      ret = vkGetPhysicalDeviceImageFormatProperties(screen->pdev, ici->format, ici->imageType,
                                                   ici->tiling, ici->usage, ici->flags, &image_props);
   return ret == VK_SUCCESS;
}

static VkImageUsageFlags
get_image_usage_for_feats(struct zink_screen *screen, VkFormatFeatureFlags feats, const struct pipe_resource *templ, unsigned bind)
{
   VkImageUsageFlags usage = 0;
   /* sadly, gallium doesn't let us know if it'll ever need this, so we have to assume */
   if (feats & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT)
      usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
   if (feats & VK_FORMAT_FEATURE_TRANSFER_DST_BIT)
      usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
   if (feats & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT && (bind & (PIPE_BIND_LINEAR | PIPE_BIND_SHARED)) != (PIPE_BIND_LINEAR | PIPE_BIND_SHARED))
      usage |= VK_IMAGE_USAGE_SAMPLED_BIT;

   if ((templ->nr_samples <= 1 || screen->info.feats.features.shaderStorageImageMultisample) &&
       (bind & PIPE_BIND_SHADER_IMAGE)) {
      if (feats & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)
         usage |= VK_IMAGE_USAGE_STORAGE_BIT;
   }

   if (bind & PIPE_BIND_RENDER_TARGET) {
      if (feats & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)
         usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
      else
         return 0;
   }

   if (bind & PIPE_BIND_DEPTH_STENCIL) {
      if (feats & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
         usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
      else
         return 0;
   /* this is unlikely to occur and has been included for completeness */
   } else if (bind & PIPE_BIND_SAMPLER_VIEW && !(usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
      if (feats & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)
         usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
      else
         return 0;
   }

   if (templ->flags & PIPE_RESOURCE_FLAG_SPARSE)
      usage |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;

   if (bind & PIPE_BIND_STREAM_OUTPUT)
      usage |= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
   return usage;
}

static VkFormatFeatureFlags
find_modifier_feats(const struct zink_modifier_prop *prop, uint64_t modifier, uint64_t *mod)
{
   for (unsigned j = 0; j < prop->drmFormatModifierCount; j++) {
      if (prop->pDrmFormatModifierProperties[j].drmFormatModifier == modifier) {
         *mod = modifier;
         return prop->pDrmFormatModifierProperties[j].drmFormatModifierTilingFeatures;
      }
   }
   return 0;
}

static VkImageUsageFlags
get_image_usage(struct zink_screen *screen, VkImageCreateInfo *ici, const struct pipe_resource *templ, unsigned bind, unsigned modifiers_count, const uint64_t *modifiers, uint64_t *mod)
{
   VkImageTiling tiling = ici->tiling;
   *mod = DRM_FORMAT_MOD_INVALID;
   if (modifiers_count) {
      bool have_linear = false;
      const struct zink_modifier_prop *prop = &screen->modifier_props[templ->format];
      assert(tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT);
      for (unsigned i = 0; i < modifiers_count; i++) {
         if (modifiers[i] == DRM_FORMAT_MOD_LINEAR) {
            have_linear = true;
            continue;
         }
         VkFormatFeatureFlags feats = find_modifier_feats(prop, modifiers[i], mod);
         if (feats) {
            VkImageUsageFlags usage = get_image_usage_for_feats(screen, feats, templ, bind);
            if (usage) {
               ici->usage = usage;
               if (check_ici(screen, ici, *mod))
                  return usage;
            }
         }
      }
      /* only try linear if no other options available */
      if (have_linear) {
         VkFormatFeatureFlags feats = find_modifier_feats(prop, DRM_FORMAT_MOD_LINEAR, mod);
         if (feats) {
            VkImageUsageFlags usage = get_image_usage_for_feats(screen, feats, templ, bind);
            if (usage) {
               ici->usage = usage;
               if (check_ici(screen, ici, *mod))
                  return usage;
            }
         }
      }
   } else
   {
      VkFormatProperties props = screen->format_props[templ->format];
      VkFormatFeatureFlags feats = tiling == VK_IMAGE_TILING_LINEAR ? props.linearTilingFeatures : props.optimalTilingFeatures;
      VkImageUsageFlags usage = get_image_usage_for_feats(screen, feats, templ, bind);
      if (usage) {
         ici->usage = usage;
         if (check_ici(screen, ici, *mod))
            return usage;
      }
   }
   *mod = DRM_FORMAT_MOD_INVALID;
   return 0;
}

static uint64_t
create_ici(struct zink_screen *screen, VkImageCreateInfo *ici, const struct pipe_resource *templ, unsigned bind, unsigned modifiers_count, const uint64_t *modifiers, bool *success)
{
   ici->sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
   ici->flags = bind & (PIPE_BIND_SCANOUT | PIPE_BIND_DEPTH_STENCIL) ? 0 : VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;

   switch (templ->target) {
   case PIPE_TEXTURE_1D:
   case PIPE_TEXTURE_1D_ARRAY:
      ici->imageType = VK_IMAGE_TYPE_1D;
      break;

   case PIPE_TEXTURE_CUBE:
   case PIPE_TEXTURE_CUBE_ARRAY:
      ici->flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
      FALLTHROUGH;
   case PIPE_TEXTURE_2D:
   case PIPE_TEXTURE_2D_ARRAY:
   case PIPE_TEXTURE_RECT:
      ici->imageType = VK_IMAGE_TYPE_2D;
      break;

   case PIPE_TEXTURE_3D:
      ici->imageType = VK_IMAGE_TYPE_3D;
      if (bind & PIPE_BIND_RENDER_TARGET)
         ici->flags |= VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT;
      break;

   case PIPE_BUFFER:
      unreachable("PIPE_BUFFER should already be handled");

   default:
      unreachable("Unknown target");
   }

   if (screen->info.have_EXT_sample_locations &&
       bind & PIPE_BIND_DEPTH_STENCIL &&
       util_format_has_depth(util_format_description(templ->format)))
      ici->flags |= VK_IMAGE_CREATE_SAMPLE_LOCATIONS_COMPATIBLE_DEPTH_BIT_EXT;

   ici->format = zink_get_format(screen, templ->format);
   ici->extent.width = templ->width0;
   ici->extent.height = templ->height0;
   ici->extent.depth = templ->depth0;
   ici->mipLevels = templ->last_level + 1;
   ici->arrayLayers = MAX2(templ->array_size, 1);
   ici->samples = templ->nr_samples ? templ->nr_samples : VK_SAMPLE_COUNT_1_BIT;
   ici->tiling = modifiers_count ? VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT : bind & PIPE_BIND_LINEAR ? VK_IMAGE_TILING_LINEAR : VK_IMAGE_TILING_OPTIMAL;
   ici->sharingMode = VK_SHARING_MODE_EXCLUSIVE;
   ici->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

   if (templ->target == PIPE_TEXTURE_CUBE ||
       templ->target == PIPE_TEXTURE_CUBE_ARRAY ||
       (templ->target == PIPE_TEXTURE_2D_ARRAY &&
        ici->extent.width == ici->extent.height &&
        ici->arrayLayers >= 6)) {
      VkImageFormatProperties props;
      if (vkGetPhysicalDeviceImageFormatProperties(screen->pdev, ici->format,
                                                   ici->imageType, ici->tiling,
                                                   ici->usage, ici->flags |
                                                   VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
                                                   &props) == VK_SUCCESS) {
         if (props.sampleCounts & ici->samples)
            ici->flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
      }
   }

   if (templ->target == PIPE_TEXTURE_CUBE)
      ici->arrayLayers *= 6;

   if (templ->usage == PIPE_USAGE_STAGING &&
       templ->format != PIPE_FORMAT_B4G4R4A4_UNORM &&
       templ->format != PIPE_FORMAT_B4G4R4A4_UINT)
      ici->tiling = VK_IMAGE_TILING_LINEAR;

   bool first = true;
   bool tried[2] = {0};
   uint64_t mod = DRM_FORMAT_MOD_INVALID;
   while (!ici->usage) {
      if (!first) {
         switch (ici->tiling) {
         case VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT:
            ici->tiling = VK_IMAGE_TILING_OPTIMAL;
            modifiers_count = 0;
            break;
         case VK_IMAGE_TILING_OPTIMAL:
            ici->tiling = VK_IMAGE_TILING_LINEAR;
            break;
         case VK_IMAGE_TILING_LINEAR:
            if (bind & PIPE_BIND_LINEAR) {
               *success = false;
               return DRM_FORMAT_MOD_INVALID;
            }
            ici->tiling = VK_IMAGE_TILING_OPTIMAL;
            break;
         default:
            unreachable("unhandled tiling mode");
         }
         if (tried[ici->tiling]) {
            *success = false;
               return DRM_FORMAT_MOD_INVALID;
         }
      }
      ici->usage = get_image_usage(screen, ici, templ, bind, modifiers_count, modifiers, &mod);
      first = false;
      if (ici->tiling != VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT)
         tried[ici->tiling] = true;
   }

   *success = true;
   return mod;
}

static struct zink_resource_object *
resource_object_create(struct zink_screen *screen, const struct pipe_resource *templ, struct winsys_handle *whandle, bool *optimal_tiling,
                       const uint64_t *modifiers, int modifiers_count)
{
   struct zink_resource_object *obj = CALLOC_STRUCT(zink_resource_object);
   if (!obj)
      return NULL;

   VkMemoryRequirements reqs = {0};
   VkMemoryPropertyFlags flags;
   /* TODO: remove linear for wsi */
   bool scanout = (templ->bind & (PIPE_BIND_SCANOUT | PIPE_BIND_LINEAR)) == (PIPE_BIND_SCANOUT | PIPE_BIND_LINEAR);
   bool shared = (templ->bind & (PIPE_BIND_SHARED | PIPE_BIND_LINEAR)) == (PIPE_BIND_SHARED | PIPE_BIND_LINEAR);

   pipe_reference_init(&obj->reference, 1);
   util_dynarray_init(&obj->desc_set_refs.refs, NULL);
   if (templ->target == PIPE_BUFFER) {
      VkBufferCreateInfo bci = create_bci(screen, templ, templ->bind);

      if (vkCreateBuffer(screen->dev, &bci, NULL, &obj->buffer) != VK_SUCCESS) {
         debug_printf("vkCreateBuffer failed\n");
         goto fail1;
      }

      vkGetBufferMemoryRequirements(screen->dev, obj->buffer, &reqs);
      if (templ->usage == PIPE_USAGE_STAGING)
         flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
      else if (templ->usage == PIPE_USAGE_STREAM)
         flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
      else if (templ->usage == PIPE_USAGE_IMMUTABLE)
         flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
      else
         flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
      obj->is_buffer = true;
      obj->transfer_dst = true;
   } else {
      bool winsys_modifier = shared && whandle && whandle->modifier != DRM_FORMAT_MOD_INVALID;
      const uint64_t *ici_modifiers = winsys_modifier ? &whandle->modifier : modifiers;
      unsigned ici_modifier_count = winsys_modifier ? 1 : modifiers_count;
      bool success = false;
      VkImageCreateInfo ici = {0};
      uint64_t mod = create_ici(screen, &ici, templ, templ->bind, ici_modifier_count, ici_modifiers, &success);
      VkExternalMemoryImageCreateInfo emici = {0};
      VkImageDrmFormatModifierExplicitCreateInfoEXT idfmeci = {0};
      VkImageDrmFormatModifierListCreateInfoEXT idfmlci;
      if (!success)
         goto fail1;

      if (shared) {
         emici.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
         emici.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
         ici.pNext = &emici;

         assert(ici.tiling != VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT || mod != DRM_FORMAT_MOD_INVALID);
         if (winsys_modifier && ici.tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT) {
            assert(mod == whandle->modifier);
            idfmeci.sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT;
            idfmeci.pNext = ici.pNext;
            idfmeci.drmFormatModifier = mod;

            /* TODO: store these values from other planes in their
             * respective zink_resource, and walk the next-pointers to
             * build up the planar array here instead.
             */
            assert(util_format_get_num_planes(templ->format) == 1);
            idfmeci.drmFormatModifierPlaneCount = 1;
            VkSubresourceLayout plane_layout = {
               .offset = whandle->offset,
               .size = 0,
               .rowPitch = whandle->stride,
               .arrayPitch = 0,
               .depthPitch = 0,
            };
            idfmeci.pPlaneLayouts = &plane_layout;

            ici.pNext = &idfmeci;
         } else if (ici.tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT) {
            idfmlci.sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT;
            idfmlci.pNext = ici.pNext;
            idfmlci.drmFormatModifierCount = 1;
            idfmlci.pDrmFormatModifiers = &mod;
            ici.pNext = &idfmlci;
         } else if (ici.tiling == VK_IMAGE_TILING_OPTIMAL) {
            // TODO: remove for wsi
            ici.pNext = NULL;
            scanout = false;
            shared = false;
         }
      }

      if (optimal_tiling)
         *optimal_tiling = ici.tiling == VK_IMAGE_TILING_OPTIMAL;

      if (ici.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT)
         obj->transfer_dst = true;

      if (ici.tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT)
         obj->modifier_aspect = VK_IMAGE_ASPECT_MEMORY_PLANE_0_BIT_EXT;

      struct wsi_image_create_info image_wsi_info = {
         VK_STRUCTURE_TYPE_WSI_IMAGE_CREATE_INFO_MESA,
         NULL,
         .scanout = true,
      };

      if ((screen->needs_mesa_wsi || screen->needs_mesa_flush_wsi) && scanout) {
         image_wsi_info.pNext = ici.pNext;
         ici.pNext = &image_wsi_info;
      }

      VkResult result = vkCreateImage(screen->dev, &ici, NULL, &obj->image);
      if (result != VK_SUCCESS) {
         debug_printf("vkCreateImage failed\n");
         goto fail1;
      }

      vkGetImageMemoryRequirements(screen->dev, obj->image, &reqs);
      if (templ->usage == PIPE_USAGE_STAGING && ici.tiling == VK_IMAGE_TILING_LINEAR)
        flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
      else
        flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
   }

   if (templ->flags & PIPE_RESOURCE_FLAG_MAP_COHERENT || templ->usage == PIPE_USAGE_DYNAMIC)
      flags |= VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
   else if (!(flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) &&
            templ->usage == PIPE_USAGE_STAGING)
      flags |= VK_MEMORY_PROPERTY_HOST_CACHED_BIT;

   VkMemoryAllocateInfo mai = {0};
   mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
   mai.allocationSize = reqs.size;
   mai.memoryTypeIndex = get_memory_type_index(screen, &reqs, flags);

   VkMemoryType mem_type = screen->info.mem_props.memoryTypes[mai.memoryTypeIndex];
   obj->coherent = mem_type.propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
   if (!(templ->flags & PIPE_RESOURCE_FLAG_SPARSE))
      obj->host_visible = mem_type.propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
   if (templ->target == PIPE_BUFFER && !obj->coherent && obj->host_visible) {
      mai.allocationSize = reqs.size = align(reqs.size, screen->info.props.limits.nonCoherentAtomSize);
   }

   VkExportMemoryAllocateInfo emai = {0};
   if (templ->bind & PIPE_BIND_SHARED && shared) {
      emai.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
      emai.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

      emai.pNext = mai.pNext;
      mai.pNext = &emai;
   }

   VkImportMemoryFdInfoKHR imfi = {
      VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
      NULL,
   };

   if (whandle && whandle->type == WINSYS_HANDLE_TYPE_FD) {
      imfi.pNext = NULL;
      imfi.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
      imfi.fd = whandle->handle;

      imfi.pNext = mai.pNext;
      emai.pNext = &imfi;
   }

   struct wsi_memory_allocate_info memory_wsi_info = {
      VK_STRUCTURE_TYPE_WSI_MEMORY_ALLOCATE_INFO_MESA,
      NULL,
   };

   if (screen->needs_mesa_wsi && scanout) {
      memory_wsi_info.implicit_sync = true;

      memory_wsi_info.pNext = mai.pNext;
      mai.pNext = &memory_wsi_info;
   }

   if (!mai.pNext && !(templ->flags & (PIPE_RESOURCE_FLAG_MAP_COHERENT | PIPE_RESOURCE_FLAG_SPARSE))) {
      obj->mkey.key.reqs = reqs;
      obj->mkey.key.heap_index = mai.memoryTypeIndex;
      obj->mem_hash = mem_hash(&obj->mkey);
      simple_mtx_lock(&screen->mem_cache_mtx);

      struct hash_entry *he = _mesa_hash_table_search_pre_hashed(screen->resource_mem_cache, obj->mem_hash, &obj->mkey);
      struct mem_key *mkey;
      if (he) {
         struct util_dynarray *array = he->data;
         mkey = (void*)he->key;
         if (array && util_dynarray_num_elements(array, struct mem_cache_entry)) {
            struct mem_cache_entry mc = util_dynarray_pop(array, struct mem_cache_entry);
            obj->mem = mc.mem;
            obj->map = mc.map;
            screen->mem_cache_size -= reqs.size;
            screen->mem_cache_count--;
         }
      } else {
         mkey = ralloc(screen->resource_mem_cache, struct mem_key);
         memcpy(&mkey->key, &obj->mkey.key, sizeof(obj->mkey.key));
         mkey->seen_count = 0;
         struct util_dynarray *array = rzalloc(screen->resource_mem_cache, struct util_dynarray);
         util_dynarray_init(array, screen->resource_mem_cache);
         _mesa_hash_table_insert_pre_hashed(screen->resource_mem_cache, obj->mem_hash, mkey, array);
      }
      mkey->seen_count++;
      simple_mtx_unlock(&screen->mem_cache_mtx);
   } else
      obj->mkey.key.heap_index = UINT32_MAX;

   /* TODO: sparse buffers should probably allocate multiple regions of memory instead of giant blobs? */
   if (!obj->mem && vkAllocateMemory(screen->dev, &mai, NULL, &obj->mem) != VK_SUCCESS) {
      debug_printf("vkAllocateMemory failed\n");
      goto fail2;
   }

   obj->offset = 0;
   obj->size = reqs.size;

   if (templ->target == PIPE_BUFFER) {
      if (!(templ->flags & PIPE_RESOURCE_FLAG_SPARSE))
         if (vkBindBufferMemory(screen->dev, obj->buffer, obj->mem, obj->offset) != VK_SUCCESS)
            goto fail3;
   } else {
      if (vkBindImageMemory(screen->dev, obj->image, obj->mem, obj->offset) != VK_SUCCESS)
         goto fail3;
   }
   return obj;

fail3:
   vkFreeMemory(screen->dev, obj->mem, NULL);

fail2:
   if (templ->target == PIPE_BUFFER)
      vkDestroyBuffer(screen->dev, obj->buffer, NULL);
   else
      vkDestroyImage(screen->dev, obj->image, NULL);
fail1:
   FREE(obj);
   return NULL;
}

static struct pipe_resource *
resource_create(struct pipe_screen *pscreen,
                const struct pipe_resource *templ,
                struct winsys_handle *whandle,
                unsigned external_usage,
                const uint64_t *modifiers, int modifiers_count)
{
   struct zink_screen *screen = zink_screen(pscreen);
   struct zink_resource *res = CALLOC_STRUCT(zink_resource);

   if (modifiers_count > 0) {
      /* for rebinds */
      res->modifiers_count = modifiers_count;
      res->modifiers = mem_dup(modifiers, modifiers_count * sizeof(uint64_t));
      if (!res->modifiers) {
         FREE(res);
         return NULL;
      }
   }

   res->base.b = *templ;

   threaded_resource_init(&res->base.b);
   pipe_reference_init(&res->base.b.reference, 1);
   res->base.b.screen = pscreen;

   bool optimal_tiling = false;
   res->obj = resource_object_create(screen, templ, whandle, &optimal_tiling, modifiers, 0);
   if (!res->obj) {
      free(res->modifiers);
      FREE(res);
      return NULL;
   }

   res->internal_format = templ->format;
   if (templ->target == PIPE_BUFFER) {
      util_range_init(&res->valid_buffer_range);
   } else {
      res->format = zink_get_format(screen, templ->format);
      res->layout = VK_IMAGE_LAYOUT_UNDEFINED;
      res->optimal_tiling = optimal_tiling;
      res->aspect = aspect_from_format(templ->format);
      if (res->base.b.bind & (PIPE_BIND_SCANOUT | PIPE_BIND_SHARED) && optimal_tiling) {
         // TODO: remove for wsi
         struct pipe_resource templ2 = res->base.b;
         templ2.bind = (res->base.b.bind & (PIPE_BIND_SCANOUT | PIPE_BIND_SHARED)) | PIPE_BIND_LINEAR;
         res->scanout_obj = resource_object_create(screen, &templ2, whandle, &optimal_tiling, modifiers, modifiers_count);
         assert(!optimal_tiling);
      }
   }

   if (screen->winsys && (templ->bind & PIPE_BIND_DISPLAY_TARGET)) {
      struct sw_winsys *winsys = screen->winsys;
      res->dt = winsys->displaytarget_create(screen->winsys,
                                             res->base.b.bind,
                                             res->base.b.format,
                                             templ->width0,
                                             templ->height0,
                                             64, NULL,
                                             &res->dt_stride);
   }
   if (res->obj->is_buffer)
      res->base.buffer_id_unique = util_idalloc_mt_alloc(&screen->buffer_ids);

   return &res->base.b;
}

static struct pipe_resource *
zink_resource_create(struct pipe_screen *pscreen,
                     const struct pipe_resource *templ)
{
   return resource_create(pscreen, templ, NULL, 0, NULL, 0);
}

static struct pipe_resource *
zink_resource_create_with_modifiers(struct pipe_screen *pscreen, const struct pipe_resource *templ,
                                    const uint64_t *modifiers, int modifiers_count)
{
   return resource_create(pscreen, templ, NULL, 0, modifiers, modifiers_count);
}

static bool
zink_resource_get_param(struct pipe_screen *pscreen, struct pipe_context *pctx,
                        struct pipe_resource *pres,
                        unsigned plane,
                        unsigned layer,
                        unsigned level,
                        enum pipe_resource_param param,
                        unsigned handle_usage,
                        uint64_t *value)
{
   struct zink_screen *screen = zink_screen(pscreen);
   struct zink_resource *res = zink_resource(pres);
   //TODO: remove for wsi
   struct zink_resource_object *obj = res->scanout_obj ? res->scanout_obj : res->obj;
   VkImageAspectFlags aspect = obj->modifier_aspect ? obj->modifier_aspect : res->aspect;
   struct winsys_handle whandle;
   switch (param) {
   case PIPE_RESOURCE_PARAM_NPLANES:
      /* not yet implemented */
      *value = 1;
      break;

   case PIPE_RESOURCE_PARAM_STRIDE: {
      VkImageSubresource sub_res = {0};
      VkSubresourceLayout sub_res_layout = {0};

      sub_res.aspectMask = aspect;

      vkGetImageSubresourceLayout(screen->dev, obj->image, &sub_res, &sub_res_layout);

      *value = sub_res_layout.rowPitch;
      break;
   }

   case PIPE_RESOURCE_PARAM_OFFSET: {
         VkImageSubresource isr = {
            aspect,
            level,
            layer
         };
         VkSubresourceLayout srl;
         vkGetImageSubresourceLayout(screen->dev, obj->image, &isr, &srl);
         *value = srl.offset;
         break;
   }

   case PIPE_RESOURCE_PARAM_MODIFIER: {
      *value = DRM_FORMAT_MOD_INVALID;
      if (!screen->info.have_EXT_image_drm_format_modifier)
         return false;
      VkImageDrmFormatModifierPropertiesEXT prop;
      prop.sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_PROPERTIES_EXT;
      prop.pNext = NULL;
      if (screen->vk.GetImageDrmFormatModifierPropertiesEXT(screen->dev, obj->image, &prop) == VK_SUCCESS)
         *value = prop.drmFormatModifier;
      break;
   }

   case PIPE_RESOURCE_PARAM_LAYER_STRIDE: {
         VkImageSubresource isr = {
            aspect,
            level,
            layer
         };
         VkSubresourceLayout srl;
         vkGetImageSubresourceLayout(screen->dev, obj->image, &isr, &srl);
         if (res->base.b.target == PIPE_TEXTURE_3D)
            *value = srl.depthPitch;
         else
            *value = srl.arrayPitch;
         break;
   }

   case PIPE_RESOURCE_PARAM_HANDLE_TYPE_SHARED:
   case PIPE_RESOURCE_PARAM_HANDLE_TYPE_KMS:
   case PIPE_RESOURCE_PARAM_HANDLE_TYPE_FD: {
      memset(&whandle, 0, sizeof(whandle));
      if (param == PIPE_RESOURCE_PARAM_HANDLE_TYPE_SHARED)
         whandle.type = WINSYS_HANDLE_TYPE_SHARED;
      else if (param == PIPE_RESOURCE_PARAM_HANDLE_TYPE_KMS)
         whandle.type = WINSYS_HANDLE_TYPE_KMS;
      else if (param == PIPE_RESOURCE_PARAM_HANDLE_TYPE_FD)
         whandle.type = WINSYS_HANDLE_TYPE_FD;

      if (!pscreen->resource_get_handle(pscreen, pctx, pres, &whandle, handle_usage))
         return false;

      *value = whandle.handle;
      break;
   }
   }
   return true;
}

static bool
zink_resource_get_handle(struct pipe_screen *pscreen,
                         struct pipe_context *context,
                         struct pipe_resource *tex,
                         struct winsys_handle *whandle,
                         unsigned usage)
{
   if (whandle->type == WINSYS_HANDLE_TYPE_FD) {
#ifdef ZINK_USE_DMABUF
      struct zink_resource *res = zink_resource(tex);
      struct zink_screen *screen = zink_screen(pscreen);
      //TODO: remove for wsi
      struct zink_resource_object *obj = res->scanout_obj ? res->scanout_obj : res->obj;

      VkMemoryGetFdInfoKHR fd_info = {0};
      int fd;
      fd_info.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
      //TODO: remove for wsi
      fd_info.memory = obj->mem;
      fd_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
      VkResult result = (*screen->vk.GetMemoryFdKHR)(screen->dev, &fd_info, &fd);
      if (result != VK_SUCCESS)
         return false;
      whandle->handle = fd;
      uint64_t value;
      zink_resource_get_param(pscreen, context, tex, 0, 0, 0, PIPE_RESOURCE_PARAM_MODIFIER, 0, &value);
      whandle->modifier = value;
      zink_resource_get_param(pscreen, context, tex, 0, 0, 0, PIPE_RESOURCE_PARAM_OFFSET, 0, &value);
      whandle->offset = value;
      zink_resource_get_param(pscreen, context, tex, 0, 0, 0, PIPE_RESOURCE_PARAM_STRIDE, 0, &value);
      whandle->stride = value;
#else
      return false;
#endif
   }
   return true;
}

static struct pipe_resource *
zink_resource_from_handle(struct pipe_screen *pscreen,
                 const struct pipe_resource *templ,
                 struct winsys_handle *whandle,
                 unsigned usage)
{
#ifdef ZINK_USE_DMABUF
   if (whandle->modifier != DRM_FORMAT_MOD_INVALID &&
       !zink_screen(pscreen)->info.have_EXT_image_drm_format_modifier)
      return NULL;

   /* ignore any AUX planes, as well as planar formats */
   if (templ->format == PIPE_FORMAT_NONE ||
       util_format_get_num_planes(templ->format) != 1)
      return NULL;

   uint64_t modifier = DRM_FORMAT_MOD_INVALID;
   int modifier_count = 0;
   if (whandle->modifier != DRM_FORMAT_MOD_INVALID) {
      modifier = whandle->modifier;
      modifier_count = 1;
   }
   return resource_create(pscreen, templ, whandle, usage, &modifier, modifier_count);
#else
   return NULL;
#endif
}

static bool
invalidate_buffer(struct zink_context *ctx, struct zink_resource *res)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);

   assert(res->base.b.target == PIPE_BUFFER);

   if (res->base.b.flags & PIPE_RESOURCE_FLAG_SPARSE)
      return false;

   if (res->valid_buffer_range.start > res->valid_buffer_range.end)
      return false;

   if (res->bind_history & ZINK_RESOURCE_USAGE_STREAMOUT)
      ctx->dirty_so_targets = true;
   /* force counter buffer reset */
   res->bind_history &= ~ZINK_RESOURCE_USAGE_STREAMOUT;

   util_range_set_empty(&res->valid_buffer_range);
   if (!get_resource_usage(res))
      return false;

   struct zink_resource_object *old_obj = res->obj;
   struct zink_resource_object *new_obj = resource_object_create(screen, &res->base.b, NULL, NULL, NULL, 0);
   if (!new_obj) {
      debug_printf("new backing resource alloc failed!");
      return false;
   }
   bool needs_unref = true;
   if (zink_batch_usage_exists(old_obj->reads) ||
       zink_batch_usage_exists(old_obj->writes)) {
      zink_batch_reference_resource_move(&ctx->batch, res);
      needs_unref = false;
   }
   res->obj = new_obj;
   res->access_stage = 0;
   res->access = 0;
   res->unordered_barrier = false;
   zink_resource_rebind(ctx, res);
   zink_descriptor_set_refs_clear(&old_obj->desc_set_refs, old_obj);
   if (needs_unref)
      zink_resource_object_reference(screen, &old_obj, NULL);
   return true;
}


static void
zink_resource_invalidate(struct pipe_context *pctx, struct pipe_resource *pres)
{
   if (pres->target == PIPE_BUFFER)
      invalidate_buffer(zink_context(pctx), zink_resource(pres));
}

static void
zink_transfer_copy_bufimage(struct zink_context *ctx,
                            struct zink_resource *dst,
                            struct zink_resource *src,
                            struct zink_transfer *trans)
{
   assert((trans->base.b.usage & (PIPE_MAP_DEPTH_ONLY | PIPE_MAP_STENCIL_ONLY)) !=
          (PIPE_MAP_DEPTH_ONLY | PIPE_MAP_STENCIL_ONLY));

   bool buf2img = src->base.b.target == PIPE_BUFFER;

   struct pipe_box box = trans->base.b.box;
   int x = box.x;
   if (buf2img)
      box.x = trans->offset;

   if (dst->obj->transfer_dst)
      zink_copy_image_buffer(ctx, NULL, dst, src, trans->base.b.level, buf2img ? x : 0,
                              box.y, box.z, trans->base.b.level, &box, trans->base.b.usage);
   else
      util_blitter_copy_texture(ctx->blitter, &dst->base.b, trans->base.b.level,
                                x, box.y, box.z, &src->base.b,
                                0, &box);
}

bool
zink_resource_has_usage(struct zink_resource *res, enum zink_resource_access usage)
{
   uint32_t batch_uses = get_resource_usage(res);
   return batch_uses & usage;
}

ALWAYS_INLINE static void
align_offset_size(const VkDeviceSize alignment, VkDeviceSize *offset, VkDeviceSize *size, VkDeviceSize obj_size)
{
   VkDeviceSize align = *offset % alignment;
   if (alignment - 1 > *offset)
      *offset = 0;
   else
      *offset -= align, *size += align;
   align = alignment - (*size % alignment);
   if (*offset + *size + align > obj_size)
      *size = obj_size - *offset;
   else
      *size += align;
}

VkMappedMemoryRange
zink_resource_init_mem_range(struct zink_screen *screen, struct zink_resource_object *obj, VkDeviceSize offset, VkDeviceSize size)
{
   assert(obj->size);
   align_offset_size(screen->info.props.limits.nonCoherentAtomSize, &offset, &size, obj->size);
   VkMappedMemoryRange range = {
      VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
      NULL,
      obj->mem,
      offset,
      size
   };
   assert(range.size);
   return range;
}

static void *
map_resource(struct zink_screen *screen, struct zink_resource *res)
{
   VkResult result = VK_SUCCESS;
   if (res->obj->map)
      return res->obj->map;
   assert(res->obj->host_visible);
   result = vkMapMemory(screen->dev, res->obj->mem, res->obj->offset,
                        res->obj->size, 0, &res->obj->map);
   if (zink_screen_handle_vkresult(screen, result))
      return res->obj->map;
   return NULL;
}

static void
unmap_resource(struct zink_screen *screen, struct zink_resource *res)
{
   res->obj->map = NULL;
   vkUnmapMemory(screen->dev, res->obj->mem);
}

static void *
buffer_transfer_map(struct zink_context *ctx, struct zink_resource *res, unsigned usage,
                    const struct pipe_box *box, struct zink_transfer *trans)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   void *ptr = NULL;

   if (res->base.is_user_ptr)
      usage |= PIPE_MAP_PERSISTENT;

   /* See if the buffer range being mapped has never been initialized,
    * in which case it can be mapped unsynchronized. */
   if (!(usage & (PIPE_MAP_UNSYNCHRONIZED | TC_TRANSFER_MAP_NO_INFER_UNSYNCHRONIZED)) &&
       usage & PIPE_MAP_WRITE && !res->base.is_shared &&
       !util_ranges_intersect(&res->valid_buffer_range, box->x, box->x + box->width)) {
      usage |= PIPE_MAP_UNSYNCHRONIZED;
   }

   /* If discarding the entire range, discard the whole resource instead. */
   if (usage & PIPE_MAP_DISCARD_RANGE && box->x == 0 && box->width == res->base.b.width0) {
      usage |= PIPE_MAP_DISCARD_WHOLE_RESOURCE;
   }

   if (usage & PIPE_MAP_DISCARD_WHOLE_RESOURCE &&
       !(usage & (PIPE_MAP_UNSYNCHRONIZED | TC_TRANSFER_MAP_NO_INVALIDATE))) {
      assert(usage & PIPE_MAP_WRITE);

      if (invalidate_buffer(ctx, res)) {
         /* At this point, the buffer is always idle. */
         usage |= PIPE_MAP_UNSYNCHRONIZED;
      } else {
         /* Fall back to a temporary buffer. */
         usage |= PIPE_MAP_DISCARD_RANGE;
      }
   }

   if ((usage & PIPE_MAP_WRITE) &&
       (usage & PIPE_MAP_DISCARD_RANGE || (!(usage & PIPE_MAP_READ) && zink_resource_has_usage(res, ZINK_RESOURCE_ACCESS_RW))) &&
       ((!res->obj->host_visible) || !(usage & (PIPE_MAP_UNSYNCHRONIZED | PIPE_MAP_PERSISTENT)))) {

      /* Check if mapping this buffer would cause waiting for the GPU.
       */

      if (!res->obj->host_visible ||
          !zink_batch_usage_check_completion(ctx, res->obj->reads) ||
          !zink_batch_usage_check_completion(ctx, res->obj->writes)) {
         /* Do a wait-free write-only transfer using a temporary buffer. */
         unsigned offset;

         /* If we are not called from the driver thread, we have
          * to use the uploader from u_threaded_context, which is
          * local to the calling thread.
          */
         struct u_upload_mgr *mgr;
         if (usage & TC_TRANSFER_MAP_THREADED_UNSYNC)
            mgr = ctx->tc->base.stream_uploader;
         else
            mgr = ctx->base.stream_uploader;
         u_upload_alloc(mgr, 0, box->width + box->x,
                     screen->info.props.limits.minMemoryMapAlignment, &offset,
                     (struct pipe_resource **)&trans->staging_res, (void **)&ptr);
         res = zink_resource(trans->staging_res);
         trans->offset = offset;
      } else {
         /* At this point, the buffer is always idle (we checked it above). */
         usage |= PIPE_MAP_UNSYNCHRONIZED;
      }
   } else if ((usage & PIPE_MAP_READ) && !(usage & PIPE_MAP_PERSISTENT)) {
      assert(!(usage & (TC_TRANSFER_MAP_THREADED_UNSYNC | PIPE_MAP_THREAD_SAFE)));
      if (usage & PIPE_MAP_DONTBLOCK) {
         /* sparse/device-local will always need to wait since it has to copy */
         if (!res->obj->host_visible)
            return NULL;
         if (!zink_batch_usage_check_completion(ctx, res->obj->writes))
            return NULL;
      } else if (!res->obj->host_visible) {
         trans->staging_res = pipe_buffer_create(&screen->base, PIPE_BIND_LINEAR, PIPE_USAGE_STAGING, box->x + box->width);
         if (!trans->staging_res)
            return NULL;
         struct zink_resource *staging_res = zink_resource(trans->staging_res);
         zink_copy_buffer(ctx, NULL, staging_res, res, box->x, box->x, box->width);
         res = staging_res;
         zink_fence_wait(&ctx->base);
      } else
         zink_batch_usage_wait(ctx, res->obj->writes);
   }

   if (!ptr) {
      /* if writing to a streamout buffer, ensure synchronization next time it's used */
      if (usage & PIPE_MAP_WRITE && res->bind_history & ZINK_RESOURCE_USAGE_STREAMOUT) {
         ctx->dirty_so_targets = true;
         /* force counter buffer reset */
         res->bind_history &= ~ZINK_RESOURCE_USAGE_STREAMOUT;
      }
      ptr = map_resource(screen, res);
      if (!ptr)
         return NULL;
   }

   if (!res->obj->coherent
#if defined(MVK_VERSION)
      // Work around for MoltenVk limitation specifically on coherent memory
      // MoltenVk returns blank memory ranges when there should be data present
      // This is a known limitation of MoltenVK.
      // See https://github.com/KhronosGroup/MoltenVK/blob/master/Docs/MoltenVK_Runtime_UserGuide.md#known-moltenvk-limitations

       || screen->instance_info.have_MVK_moltenvk
#endif
      ) {
      VkDeviceSize size = box->width;
      VkDeviceSize offset = res->obj->offset + trans->offset + box->x;
      VkMappedMemoryRange range = zink_resource_init_mem_range(screen, res->obj, offset, size);
      if (vkInvalidateMappedMemoryRanges(screen->dev, 1, &range) != VK_SUCCESS) {
         vkUnmapMemory(screen->dev, res->obj->mem);
         return NULL;
      }
   }
   trans->base.b.usage = usage;
   if (usage & PIPE_MAP_WRITE)
      util_range_add(&res->base.b, &res->valid_buffer_range, box->x, box->x + box->width);
   return ptr;
}

static void *
zink_transfer_map(struct pipe_context *pctx,
                  struct pipe_resource *pres,
                  unsigned level,
                  unsigned usage,
                  const struct pipe_box *box,
                  struct pipe_transfer **transfer)
{
   struct zink_context *ctx = zink_context(pctx);
   struct zink_screen *screen = zink_screen(pctx->screen);
   struct zink_resource *res = zink_resource(pres);

   struct zink_transfer *trans;

   if (usage & PIPE_MAP_THREAD_SAFE)
      trans = malloc(sizeof(*trans));
   else if (usage & TC_TRANSFER_MAP_THREADED_UNSYNC)
      trans = slab_alloc(&ctx->transfer_pool_unsync);
   else
      trans = slab_alloc(&ctx->transfer_pool);
   if (!trans)
      return NULL;

   memset(trans, 0, sizeof(*trans));
   pipe_resource_reference(&trans->base.b.resource, pres);

   trans->base.b.resource = pres;
   trans->base.b.level = level;
   trans->base.b.usage = usage;
   trans->base.b.box = *box;

   void *ptr, *base;
   if (pres->target == PIPE_BUFFER) {
      base = buffer_transfer_map(ctx, res, usage, box, trans);
      ptr = ((uint8_t *)base) + box->x;
   } else {
      if (usage & PIPE_MAP_WRITE && !(usage & PIPE_MAP_READ))
         /* this is like a blit, so we can potentially dump some clears or maybe we have to  */
         zink_fb_clears_apply_or_discard(ctx, pres, zink_rect_from_box(box), false);
      else if (usage & PIPE_MAP_READ)
         /* if the map region intersects with any clears then we have to apply them */
         zink_fb_clears_apply_region(ctx, pres, zink_rect_from_box(box));
      if (res->optimal_tiling || !res->obj->host_visible) {
         enum pipe_format format = pres->format;
         if (usage & PIPE_MAP_DEPTH_ONLY)
            format = util_format_get_depth_only(pres->format);
         else if (usage & PIPE_MAP_STENCIL_ONLY)
            format = PIPE_FORMAT_S8_UINT;
         trans->base.b.stride = util_format_get_stride(format, box->width);
         trans->base.b.layer_stride = util_format_get_2d_size(format,
                                                            trans->base.b.stride,
                                                            box->height);

         struct pipe_resource templ = *pres;
         templ.format = format;
         templ.usage = usage & PIPE_MAP_READ ? PIPE_USAGE_STAGING : PIPE_USAGE_STREAM;
         templ.target = PIPE_BUFFER;
         templ.bind = PIPE_BIND_LINEAR;
         templ.width0 = trans->base.b.layer_stride * box->depth;
         templ.height0 = templ.depth0 = 0;
         templ.last_level = 0;
         templ.array_size = 1;
         templ.flags = 0;

         trans->staging_res = zink_resource_create(pctx->screen, &templ);
         if (!trans->staging_res)
            return NULL;

         struct zink_resource *staging_res = zink_resource(trans->staging_res);

         if (usage & PIPE_MAP_READ) {
            /* force multi-context sync */
            if (zink_batch_usage_is_unflushed(res->obj->writes))
               zink_batch_usage_wait(ctx, res->obj->writes);
            zink_transfer_copy_bufimage(ctx, staging_res, res, trans);
            /* need to wait for rendering to finish */
            zink_fence_wait(pctx);
         }

         ptr = base = map_resource(screen, staging_res);
         if (!base)
            return NULL;
      } else {
         assert(!res->optimal_tiling);
         base = map_resource(screen, res);
         if (!base)
            return NULL;
         if (zink_resource_has_usage(res, ZINK_RESOURCE_ACCESS_RW)) {
            if (usage & PIPE_MAP_WRITE)
               zink_fence_wait(pctx);
            else
               zink_batch_usage_wait(ctx, res->obj->writes);
         }
         VkImageSubresource isr = {
            res->obj->modifier_aspect ? res->obj->modifier_aspect : res->aspect,
            level,
            0
         };
         VkSubresourceLayout srl;
         vkGetImageSubresourceLayout(screen->dev, res->obj->image, &isr, &srl);
         trans->base.b.stride = srl.rowPitch;
         if (res->base.b.target == PIPE_TEXTURE_3D)
            trans->base.b.layer_stride = srl.depthPitch;
         else
            trans->base.b.layer_stride = srl.arrayPitch;
         trans->offset = srl.offset;
         trans->depthPitch = srl.depthPitch;
         const struct util_format_description *desc = util_format_description(res->base.b.format);
         unsigned offset = srl.offset +
                           box->z * srl.depthPitch +
                           (box->y / desc->block.height) * srl.rowPitch +
                           (box->x / desc->block.width) * (desc->block.bits / 8);
         if (!res->obj->coherent) {
            VkDeviceSize size = box->width * box->height * desc->block.bits / 8;
            VkMappedMemoryRange range = zink_resource_init_mem_range(screen, res->obj, res->obj->offset + offset, size);
            vkFlushMappedMemoryRanges(screen->dev, 1, &range);
         }
         ptr = ((uint8_t *)base) + offset;
         if (sizeof(void*) == 4)
            trans->base.b.usage |= ZINK_MAP_TEMPORARY;
      }
   }
   if ((usage & PIPE_MAP_PERSISTENT) && !(usage & PIPE_MAP_COHERENT))
      res->obj->persistent_maps++;

   if (trans->base.b.usage & (PIPE_MAP_ONCE | ZINK_MAP_TEMPORARY))
      p_atomic_inc(&res->obj->map_count);

   *transfer = &trans->base.b;
   return ptr;
}

static void
zink_transfer_flush_region(struct pipe_context *pctx,
                           struct pipe_transfer *ptrans,
                           const struct pipe_box *box)
{
   struct zink_context *ctx = zink_context(pctx);
   struct zink_resource *res = zink_resource(ptrans->resource);
   struct zink_transfer *trans = (struct zink_transfer *)ptrans;

   if (trans->base.b.usage & PIPE_MAP_WRITE) {
      struct zink_screen *screen = zink_screen(pctx->screen);
      struct zink_resource *m = trans->staging_res ? zink_resource(trans->staging_res) :
                                                     res;
      ASSERTED VkDeviceSize size, offset;
      if (m->obj->is_buffer) {
         size = box->width;
         offset = trans->offset + box->x;
      } else {
         size = box->width * box->height * util_format_get_blocksize(m->base.b.format);
         offset = trans->offset +
                  box->z * trans->depthPitch +
                  util_format_get_2d_size(m->base.b.format, trans->base.b.stride, box->y) +
                  util_format_get_stride(m->base.b.format, box->x);
         assert(offset + size <= res->obj->size);
      }
      if (!m->obj->coherent) {
         VkMappedMemoryRange range = zink_resource_init_mem_range(screen, m->obj, m->obj->offset, m->obj->size);
         vkFlushMappedMemoryRanges(screen->dev, 1, &range);
      }
      if (trans->staging_res) {
         struct zink_resource *staging_res = zink_resource(trans->staging_res);

         if (ptrans->resource->target == PIPE_BUFFER)
            zink_copy_buffer(ctx, NULL, res, staging_res, box->x, offset, box->width);
         else
            zink_transfer_copy_bufimage(ctx, res, staging_res, trans);
      }
   }
}

static void
zink_transfer_unmap(struct pipe_context *pctx,
                    struct pipe_transfer *ptrans)
{
   struct zink_context *ctx = zink_context(pctx);
   struct zink_screen *screen = zink_screen(pctx->screen);
   struct zink_resource *res = zink_resource(ptrans->resource);
   struct zink_transfer *trans = (struct zink_transfer *)ptrans;

   if (!(trans->base.b.usage & (PIPE_MAP_FLUSH_EXPLICIT | PIPE_MAP_COHERENT))) {
      zink_transfer_flush_region(pctx, ptrans, &ptrans->box);
   }

   if ((trans->base.b.usage & PIPE_MAP_ONCE && !trans->staging_res && !screen->threaded) ||
       (trans->base.b.usage & ZINK_MAP_TEMPORARY && !p_atomic_dec_return(&res->obj->map_count)))
      unmap_resource(screen, res);
   if ((trans->base.b.usage & PIPE_MAP_PERSISTENT) && !(trans->base.b.usage & PIPE_MAP_COHERENT))
      res->obj->persistent_maps--;

   if (trans->staging_res)
      pipe_resource_reference(&trans->staging_res, NULL);
   pipe_resource_reference(&trans->base.b.resource, NULL);

   if (trans->base.b.usage & PIPE_MAP_THREAD_SAFE) {
      free(trans);
   } else {
      /* Don't use pool_transfers_unsync. We are always in the driver
       * thread. Freeing an object into a different pool is allowed.
       */
      slab_free(&ctx->transfer_pool, ptrans);
   }
}

static void
zink_buffer_subdata(struct pipe_context *ctx, struct pipe_resource *buffer,
                    unsigned usage, unsigned offset, unsigned size, const void *data)
{
   struct pipe_transfer *transfer = NULL;
   struct pipe_box box;
   uint8_t *map = NULL;

   usage |= PIPE_MAP_WRITE | PIPE_MAP_ONCE;

   if (!(usage & PIPE_MAP_DIRECTLY))
      usage |= PIPE_MAP_DISCARD_RANGE;

   u_box_1d(offset, size, &box);
   map = zink_transfer_map(ctx, buffer, 0, usage, &box, &transfer);
   if (!map)
      return;

   memcpy(map, data, size);
   zink_transfer_unmap(ctx, transfer);
}

static struct pipe_resource *
zink_resource_get_separate_stencil(struct pipe_resource *pres)
{
   /* For packed depth-stencil, we treat depth as the primary resource
    * and store S8 as the "second plane" resource.
    */
   if (pres->next && pres->next->format == PIPE_FORMAT_S8_UINT)
      return pres->next;

   return NULL;

}

bool
zink_resource_object_init_storage(struct zink_context *ctx, struct zink_resource *res)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   /* base resource already has the cap */
   if (res->base.b.bind & PIPE_BIND_SHADER_IMAGE)
      return true;
   if (res->obj->is_buffer) {
      if (res->obj->sbuffer)
         return true;
      VkBufferCreateInfo bci = create_bci(screen, &res->base.b, res->base.b.bind | PIPE_BIND_SHADER_IMAGE);
      bci.size = res->obj->size;

      VkBuffer buffer;
      if (vkCreateBuffer(screen->dev, &bci, NULL, &buffer) != VK_SUCCESS)
         return false;
      vkBindBufferMemory(screen->dev, buffer, res->obj->mem, res->obj->offset);
      res->obj->sbuffer = res->obj->buffer;
      res->obj->buffer = buffer;
   } else {
      zink_fb_clears_apply_region(ctx, &res->base.b, (struct u_rect){0, res->base.b.width0, 0, res->base.b.height0});
      zink_resource_image_barrier(ctx, NULL, res, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 0, 0);
      res->base.b.bind |= PIPE_BIND_SHADER_IMAGE;
      struct zink_resource_object *old_obj = res->obj;
      struct zink_resource_object *new_obj = resource_object_create(screen, &res->base.b, NULL, &res->optimal_tiling, res->modifiers, res->modifiers_count);
      if (!new_obj) {
         debug_printf("new backing resource alloc failed!");
         res->base.b.bind &= ~PIPE_BIND_SHADER_IMAGE;
         return false;
      }
      struct zink_resource staging = *res;
      staging.obj = old_obj;
      bool needs_unref = true;
      if (get_resource_usage(res)) {
         zink_batch_reference_resource_move(&ctx->batch, res);
         needs_unref = false;
      }
      res->obj = new_obj;
      zink_descriptor_set_refs_clear(&old_obj->desc_set_refs, old_obj);
      for (unsigned i = 0; i <= res->base.b.last_level; i++) {
         struct pipe_box box = {0, 0, 0,
                                u_minify(res->base.b.width0, i),
                                u_minify(res->base.b.height0, i), res->base.b.array_size};
         box.depth = util_num_layers(&res->base.b, i);
         ctx->base.resource_copy_region(&ctx->base, &res->base.b, i, 0, 0, 0, &staging.base.b, i, &box);
      }
      if (needs_unref)
         zink_resource_object_reference(screen, &old_obj, NULL);
   }

   zink_resource_rebind(ctx, res);

   return true;
}

void
zink_resource_setup_transfer_layouts(struct zink_context *ctx, struct zink_resource *src, struct zink_resource *dst)
{
   if (src == dst) {
      /* The Vulkan 1.1 specification says the following about valid usage
       * of vkCmdBlitImage:
       *
       * "srcImageLayout must be VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR,
       *  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL or VK_IMAGE_LAYOUT_GENERAL"
       *
       * and:
       *
       * "dstImageLayout must be VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR,
       *  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL or VK_IMAGE_LAYOUT_GENERAL"
       *
       * Since we cant have the same image in two states at the same time,
       * we're effectively left with VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR or
       * VK_IMAGE_LAYOUT_GENERAL. And since this isn't a present-related
       * operation, VK_IMAGE_LAYOUT_GENERAL seems most appropriate.
       */
      zink_resource_image_barrier(ctx, NULL, src,
                                  VK_IMAGE_LAYOUT_GENERAL,
                                  VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
                                  VK_PIPELINE_STAGE_TRANSFER_BIT);
   } else {
      zink_resource_image_barrier(ctx, NULL, src,
                                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                  VK_ACCESS_TRANSFER_READ_BIT,
                                  VK_PIPELINE_STAGE_TRANSFER_BIT);

      zink_resource_image_barrier(ctx, NULL, dst,
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  VK_ACCESS_TRANSFER_WRITE_BIT,
                                  VK_PIPELINE_STAGE_TRANSFER_BIT);
   }
}

void
zink_get_depth_stencil_resources(struct pipe_resource *res,
                                 struct zink_resource **out_z,
                                 struct zink_resource **out_s)
{
   if (!res) {
      if (out_z) *out_z = NULL;
      if (out_s) *out_s = NULL;
      return;
   }

   if (res->format != PIPE_FORMAT_S8_UINT) {
      if (out_z) *out_z = zink_resource(res);
      if (out_s) *out_s = zink_resource(zink_resource_get_separate_stencil(res));
   } else {
      if (out_z) *out_z = NULL;
      if (out_s) *out_s = zink_resource(res);
   }
}

static void
zink_resource_set_separate_stencil(struct pipe_resource *pres,
                                   struct pipe_resource *stencil)
{
   assert(util_format_has_depth(util_format_description(pres->format)));
   pipe_resource_reference(&pres->next, stencil);
}

static enum pipe_format
zink_resource_get_internal_format(struct pipe_resource *pres)
{
   struct zink_resource *res = zink_resource(pres);
   return res->internal_format;
}

static const struct u_transfer_vtbl transfer_vtbl = {
   .resource_create       = zink_resource_create,
   .resource_destroy      = zink_resource_destroy,
   .transfer_map          = zink_transfer_map,
   .transfer_unmap        = zink_transfer_unmap,
   .transfer_flush_region = zink_transfer_flush_region,
   .get_internal_format   = zink_resource_get_internal_format,
   .set_stencil           = zink_resource_set_separate_stencil,
   .get_stencil           = zink_resource_get_separate_stencil,
};

bool
zink_screen_resource_init(struct pipe_screen *pscreen)
{
   struct zink_screen *screen = zink_screen(pscreen);
   pscreen->resource_create = zink_resource_create;
   pscreen->resource_create_with_modifiers = zink_resource_create_with_modifiers;
   pscreen->resource_destroy = zink_resource_destroy;
   pscreen->transfer_helper = u_transfer_helper_create(&transfer_vtbl, true, true, false, false);

   if (screen->info.have_KHR_external_memory_fd) {
      pscreen->resource_get_handle = zink_resource_get_handle;
      pscreen->resource_from_handle = zink_resource_from_handle;
   }
   pscreen->resource_get_param = zink_resource_get_param;
   simple_mtx_init(&screen->mem_cache_mtx, mtx_plain);
   screen->resource_mem_cache = _mesa_hash_table_create(NULL, mem_hash, mem_equals);
   return !!screen->resource_mem_cache;
}

void
zink_context_resource_init(struct pipe_context *pctx)
{
   pctx->buffer_map = u_transfer_helper_deinterleave_transfer_map;
   pctx->buffer_unmap = u_transfer_helper_deinterleave_transfer_unmap;
   pctx->texture_map = u_transfer_helper_deinterleave_transfer_map;
   pctx->texture_unmap = u_transfer_helper_deinterleave_transfer_unmap;

   pctx->transfer_flush_region = u_transfer_helper_transfer_flush_region;
   pctx->buffer_subdata = zink_buffer_subdata;
   pctx->texture_subdata = u_default_texture_subdata;
   pctx->invalidate_resource = zink_resource_invalidate;
}
