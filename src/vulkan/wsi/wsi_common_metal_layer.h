/*
 * Copyright 2024 Autodesk, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef WSI_COMMON_METAL_LAYER_H
#define WSI_COMMON_METAL_LAYER_H

#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_metal.h>

#include <stdint.h>
#include <stdbool.h>

typedef void CAMetalDrawable;

void
wsi_metal_layer_size(const CAMetalLayer *metal_layer,
   uint32_t *width, uint32_t *height);

void
wsi_metal_layer_set_immediate(const CAMetalLayer *metal_layer,
                              bool enable_immediate);

VkResult
wsi_metal_layer_configure(const CAMetalLayer *metal_layer,
   uint32_t width, uint32_t height, uint32_t image_count,
   VkFormat format, VkColorSpaceKHR color_space,
   bool enable_opaque, bool enable_immediate, bool framebuffer_only);

void
wsi_metal_layer_set_hdr_metadata(const CAMetalLayer *metal_layer,
                                 const VkHdrMetadataEXT *metadata);

CAMetalDrawable *
wsi_metal_layer_acquire_drawable(const CAMetalLayer *metal_layer);

void
wsi_metal_release_drawable(CAMetalDrawable *drawable_ptr);

void
wsi_metal_layer_add_presented_handler(CAMetalDrawable *drawable_ptr,
                                      void (*presented_handler)(void *, uint64_t),
                                      void *present_info_ptr, uint64_t present_id);

struct wsi_metal_layer_blit_context;

struct wsi_metal_layer_blit_context *
wsi_create_metal_layer_blit_context();

void
wsi_destroy_metal_layer_blit_context(struct wsi_metal_layer_blit_context *context);

void
wsi_metal_layer_blit_and_present(struct wsi_metal_layer_blit_context *context,
   CAMetalDrawable **drawable_ptr, void *buffer,
   uint32_t width, uint32_t height, uint32_t row_pitch);
void
wsi_metal_layer_present(CAMetalDrawable **drawable_ptr);

void
wsi_metal_layer_make_queue_resident(const CAMetalLayer *metal_layer,
   void *mtl4_command_queue);

void
wsi_metal_layer_remove_queue_resident(const CAMetalLayer *metal_layer,
   void *mtl4_command_queue);

#endif // WSI_COMMON_METAL_LAYER_H
