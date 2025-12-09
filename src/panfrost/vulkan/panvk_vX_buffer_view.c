/*
 * Copyright © 2021 Collabora Ltd.
 *
 * Derived from tu_image.c which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "genxml/gen_macros.h"

#include "panvk_buffer.h"
#include "panvk_buffer_view.h"
#include "panvk_device.h"
#include "panvk_entrypoints.h"
#include "panvk_priv_bo.h"

#include "pan_afbc.h"
#include "pan_desc.h"
#include "pan_props.h"
#include "pan_texture.h"

#include "vk_format.h"
#include "vk_log.h"

VKAPI_ATTR VkResult VKAPI_CALL
panvk_per_arch(CreateBufferView)(VkDevice _device,
                                 const VkBufferViewCreateInfo *pCreateInfo,
                                 const VkAllocationCallbacks *pAllocator,
                                 VkBufferView *pView)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_buffer, buffer, pCreateInfo->buffer);

   struct panvk_buffer_view *view = vk_object_zalloc(
      &device->vk, pAllocator, sizeof(*view), VK_OBJECT_TYPE_BUFFER_VIEW);

   if (!view)
      return panvk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_buffer_view_init(&device->vk, &view->vk, pCreateInfo);

   enum pipe_format pfmt = vk_format_to_pipe_format(view->vk.format);

   uint64_t address = panvk_buffer_gpu_ptr(buffer, pCreateInfo->offset);
   VkBufferUsageFlags tex_usage_mask = VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;

#if PAN_ARCH >= 9
   tex_usage_mask |= VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;
#else
   /* This alignment constraint only applies when TextureDescriptors are used. */
   assert(!(address & 63));
#endif

   if (buffer->vk.usage & tex_usage_mask) {
      struct pan_buffer_view bview = {
         .format = pfmt,
         .astc.hdr = util_format_is_astc_hdr(pfmt),
         .width_el = view->vk.elements,
         .base = address,
      };

#if PAN_ARCH >= 9
      view->mem = panvk_pool_alloc_desc(&device->mempools.rw, BUFFER);
      GENX(pan_buffer_texture_emit)(&bview, &view->descs.buf);
#else
      view->mem =
         panvk_pool_alloc_desc(&device->mempools.rw, SURFACE_WITH_STRIDE);
      struct pan_ptr ptr = {
         .gpu = panvk_priv_mem_dev_addr(view->mem),
         .cpu = panvk_priv_mem_host_addr(view->mem),
      };
      GENX(pan_buffer_texture_emit)(&bview, &view->descs.tex, &ptr);
#endif
   }

#if PAN_ARCH < 9
   if (buffer->vk.usage & VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT) {
      unsigned blksz = vk_format_get_blocksize(pCreateInfo->format);

      pan_pack(&view->descs.img_attrib_buf[0], ATTRIBUTE_BUFFER, cfg) {
         /* The format is the only thing we lack to emit attribute descriptors
          * when copying from the set to the attribute tables. Instead of
          * making the descriptor size to store an extra format, we pack
          * the 22-bit format with the texel stride, which is expected to be
          * fit in remaining 10 bits.
          */
         uint32_t hw_fmt = GENX(pan_format_from_pipe_format)(pfmt)->hw;

         assert(blksz < BITFIELD_MASK(10));
         assert(hw_fmt < BITFIELD_MASK(22));

         cfg.type = MALI_ATTRIBUTE_TYPE_3D_LINEAR;
         cfg.pointer = address;
         cfg.stride = blksz | (hw_fmt << 10);
         cfg.size = view->vk.elements * blksz;
      }

      struct mali_attribute_buffer_packed *buf = &view->descs.img_attrib_buf[1];
      pan_cast_and_pack(buf, ATTRIBUTE_BUFFER_CONTINUATION_3D, cfg) {
         cfg.s_dimension = view->vk.elements;
         cfg.t_dimension = 1;
         cfg.r_dimension = 1;
         cfg.row_stride = view->vk.elements * blksz;
      }
   }
#endif

   *pView = panvk_buffer_view_to_handle(view);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(DestroyBufferView)(VkDevice _device, VkBufferView bufferView,
                                  const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_buffer_view, view, bufferView);

   if (!view)
      return;

   panvk_pool_free_mem(&view->mem);
   vk_buffer_view_destroy(&device->vk, pAllocator, &view->vk);
}
