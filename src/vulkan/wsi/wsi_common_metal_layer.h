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

VkResult
wsi_metal_layer_configure(const CAMetalLayer *metal_layer,
   uint32_t width, uint32_t height, uint32_t image_count,
   VkFormat format, VkColorSpaceKHR color_space,
   bool enable_opaque, bool enable_immediate);

CAMetalDrawable *
wsi_metal_layer_acquire_drawable(const CAMetalLayer *metal_layer);

void
wsi_metal_release_drawable(CAMetalDrawable *drawable_ptr);

struct wsi_metal_layer_blit_context;

struct wsi_metal_layer_blit_context *
wsi_create_metal_layer_blit_context();

void
wsi_destroy_metal_layer_blit_context(struct wsi_metal_layer_blit_context *context);

void
wsi_metal_layer_blit_and_present(struct wsi_metal_layer_blit_context *context,
   CAMetalDrawable **drawable_ptr, void *buffer,
   uint32_t width, uint32_t height, uint32_t row_pitch);

#endif // WSI_COMMON_METAL_LAYER_H
