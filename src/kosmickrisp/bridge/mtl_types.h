/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 *
 * Structures and enums found in this file are a 1-1 mapping of Metal's
 * equivalents
 */

#ifndef KK_MTL_TYPES_H
#define KK_MTL_TYPES_H 1

#include <stddef.h> /* For size_t definition */

/** HANDLES */
typedef void mtl_device;
typedef void mtl_heap;
typedef void mtl_buffer;
typedef void mtl_texture;
typedef void mtl_command_queue;
typedef void mtl_command_buffer;
typedef void mtl_command_encoder;
typedef void mtl_blit_encoder;
typedef void mtl_compute_encoder;
typedef void mtl_render_encoder;
typedef void mtl_event;
typedef void mtl_shared_event;
typedef void mtl_sampler_descriptor;
typedef void mtl_sampler;
typedef void mtl_compute_pipeline_state;
typedef void mtl_library;
typedef void mtl_render_pipeline_state;
typedef void mtl_function;
typedef void mtl_resource;
typedef void mtl_render_pass_descriptor;
typedef void mtl_render_pipeline_descriptor;
typedef void mtl_fence;
typedef void mtl_stencil_descriptor;
typedef void mtl_depth_stencil_descriptor;
typedef void mtl_depth_stencil_state;
typedef void mtl_render_pass_attachment_descriptor;
typedef void mtl_residency_set;
typedef void mtl_allocation;

/** ENUMS */
enum mtl_cpu_cache_mode {
   MTL_CPU_CACHE_MODE_DEFAULT_CACHE = 0,
   MTL_CPU_CACHE_MODE_WRITE_COMBINED = 1,
};

enum mtl_storage_mode {
   MTL_STORAGE_MODE_SHARED = 0,
   MTL_STORAGE_MODE_MANAGED = 1,
   MTL_STORAGE_MODE_PRIVATE = 2,
   MTL_STORAGE_MODE_MEMORYLESS = 3,
};

enum mtl_hazard_tracking_mode {
   MTL_HAZARD_TRACKING_MODE_DFEAULT = 0,
   MTL_HAZARD_TRACKING_MODE_UNTRACKED = 1,
   MTL_HAZARD_TRACKING_MODE_TRACKED = 2,
};

#define MTL_RESOURCE_CPU_CACHE_MODE_SHIFT       0
#define MTL_RESOURCE_STORAGE_MODE_SHIFT         4
#define MTL_RESOURCE_HAZARD_TRACKING_MODE_SHIFT 8
enum mtl_resource_options {
   MTL_RESOURCE_CPU_CACHE_MODE_DEFAULT_CACHE =
      MTL_CPU_CACHE_MODE_DEFAULT_CACHE << MTL_RESOURCE_CPU_CACHE_MODE_SHIFT,
   MTL_RESOURCE_CPU_CACHE_MODE_WRITE_COMBINED =
      MTL_CPU_CACHE_MODE_WRITE_COMBINED << MTL_RESOURCE_CPU_CACHE_MODE_SHIFT,
   MTL_RESOURCE_STORAGE_MODE_SHARED = MTL_STORAGE_MODE_SHARED
                                      << MTL_RESOURCE_STORAGE_MODE_SHIFT,
   MTL_RESOURCE_STORAGE_MODE_PRIVATE = MTL_STORAGE_MODE_PRIVATE
                                       << MTL_RESOURCE_STORAGE_MODE_SHIFT,
   MTL_RESOURCE_TRACKING_MODE_DEFAULT =
      MTL_HAZARD_TRACKING_MODE_DFEAULT
      << MTL_RESOURCE_HAZARD_TRACKING_MODE_SHIFT,
   MTL_RESOURCE_TRACKING_MODE_UNTRACKED =
      MTL_HAZARD_TRACKING_MODE_UNTRACKED
      << MTL_RESOURCE_HAZARD_TRACKING_MODE_SHIFT,
   MTL_RESOURCE_TRACKING_MODE_TRACKED =
      MTL_HAZARD_TRACKING_MODE_TRACKED
      << MTL_RESOURCE_HAZARD_TRACKING_MODE_SHIFT,
};

enum mtl_blit_options {
   MTL_BLIT_OPTION_NONE = 0,
   MTL_BLIT_OPTION_DEPTH_FROM_DEPTH_STENCIL = 1 << 0,
   MTL_BLIT_OPTION_STENCIL_FROM_DEPTH_STENCIL = 1 << 1,
};

enum mtl_resource_usage {
   MTL_RESOURCE_USAGE_READ = 1 << 0,
   MTL_RESOURCE_USAGE_WRITE = 1 << 1,
};

enum mtl_primitive_type {
   MTL_PRIMITIVE_TYPE_POINT = 0,
   MTL_PRIMITIVE_TYPE_LINE = 1,
   MTL_PRIMITIVE_TYPE_LINE_STRIP = 2,
   MTL_PRIMITIVE_TYPE_TRIANGLE = 3,
   MTL_PRIMITIVE_TYPE_TRIANGLE_STRIP = 4,
};

enum mtl_primitive_topology_class {
   MTL_PRIMITIVE_TOPOLOGY_CLASS_UNSPECIFIED = 0,
   MTL_PRIMITIVE_TOPOLOGY_CLASS_POINT = 1,
   MTL_PRIMITIVE_TOPOLOGY_CLASS_LINE = 2,
   MTL_PRIMITIVE_TOPOLOGY_CLASS_TRIANGLE = 3,
};

enum mtl_texture_type {
   MTL_TEXTURE_TYPE_1D = 0u,
   MTL_TEXTURE_TYPE_1D_ARRAY = 1u,
   MTL_TEXTURE_TYPE_2D = 2u,
   MTL_TEXTURE_TYPE_2D_ARRAY = 3u,
   MTL_TEXTURE_TYPE_2D_MULTISAMPLE = 4u,
   MTL_TEXTURE_TYPE_CUBE = 5u,
   MTL_TEXTURE_TYPE_CUBE_ARRAY = 6u,
   MTL_TEXTURE_TYPE_3D = 7u,
   MTL_TEXTURE_TYPE_2D_ARRAY_MULTISAMPLE = 8u,
   MTL_TEXTURE_TYPE_TEXTURE_BUFFER = 9u,
};

enum mtl_texture_usage {
   MTL_TEXTURE_USAGE_UNKNOWN = 0x0000,
   MTL_TEXTURE_USAGE_SHADER_READ = 0x0001,
   MTL_TEXTURE_USAGE_SHADER_WRITE = 0X0002,
   MTL_TEXTURE_USAGE_RENDER_TARGET = 0X0004,
   MTL_TEXTURE_USAGE_PIXEL_FORMAT_VIEW = 0X0010,
   MTL_TEXTURE_USAGE_SHADER_ATOMIC = 0X0020,
};

enum mtl_load_action {
   MTL_LOAD_ACTION_DONT_CARE = 0u,
   MTL_LOAD_ACTION_LOAD = 1u,
   MTL_LOAD_ACTION_CLEAR = 2u,
};

enum mtl_store_action {
   MTL_STORE_ACTION_DONT_CARE = 0u,
   MTL_STORE_ACTION_STORE = 1u,
   MTL_STORE_ACTION_MULTISAMPLE_RESOLVE = 2u,
   MTL_STORE_ACTION_STORE_AND_MULTISAMPLE_RESOLVE = 3u,
   MTL_STORE_ACTION_UNKNOWN = 4u,
   MTL_STORE_ACTION_CUSTOM_SAMPLE_DEPTH_STORE = 5u,
};

enum mtl_texture_swizzle {
   MTL_TEXTURE_SWIZZLE_ZERO = 0,
   MTL_TEXTURE_SWIZZLE_ONE = 1,
   MTL_TEXTURE_SWIZZLE_RED = 2,
   MTL_TEXTURE_SWIZZLE_GREEN = 3,
   MTL_TEXTURE_SWIZZLE_BLUE = 4,
   MTL_TEXTURE_SWIZZLE_ALPHA = 5,
};

enum mtl_index_type {
   MTL_INDEX_TYPE_UINT16 = 0,
   MTL_INDEX_TYPE_UINT32 = 1,
};

enum mtl_sampler_address_mode {
   MTL_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE = 0,
   MTL_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE = 1,
   MTL_SAMPLER_ADDRESS_MODE_REPEAT = 2,
   MTL_SAMPLER_ADDRESS_MODE_MIRROR_REPEAT = 3,
   MTL_SAMPLER_ADDRESS_MODE_CLAMP_TO_ZERO = 4,
   MTL_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER_COLOR = 5,
};

enum mtl_sampler_border_color {
   MTL_SAMPLER_BORDER_COLOR_TRANSPARENT_BLACK = 0,
   MTL_SAMPLER_BORDER_COLOR_OPAQUE_BLACK = 1,
   MTL_SAMPLER_BORDER_COLOR_OPAQUE_WHITE = 2,
};

enum mtl_sampler_min_mag_filter {
   MTL_SAMPLER_MIN_MAG_FILTER_NEAREST = 0,
   MTL_SAMPLER_MIN_MAG_FILTER_LINEAR = 1,
};

enum mtl_sampler_mip_filter {
   MTL_SAMPLER_MIP_FILTER_NOT_MIP_MAPPED = 0,
   MTL_SAMPLER_MIP_FILTER_NEAREST = 1,
   MTL_SAMPLER_MIP_FILTER_LINEAR = 2,
};

enum mtl_compare_function {
   MTL_COMPARE_FUNCTION_NEVER = 0,
   MTL_COMPARE_FUNCTION_LESS = 1,
   MTL_COMPARE_FUNCTION_EQUAL = 2,
   MTL_COMPARE_FUNCTION_LESS_EQUAL = 3,
   MTL_COMPARE_FUNCTION_GREATER = 4,
   MTL_COMPARE_FUNCTION_NOT_EQUAL = 5,
   MTL_COMPARE_FUNCTION_GREATER_EQUAL = 6,
   MTL_COMPARE_FUNCTION_ALWAYS = 7,
};

enum mtl_winding {
   MTL_WINDING_CLOCKWISE = 0,
   MTL_WINDING_COUNTER_CLOCKWISE = 1,
};

enum mtl_cull_mode {
   MTL_CULL_MODE_NONE = 0,
   MTL_CULL_MODE_FRONT = 1,
   MTL_CULL_MODE_BACK = 2,
};

enum mtl_visibility_result_mode {
   MTL_VISIBILITY_RESULT_MODE_DISABLED = 0,
   MTL_VISIBILITY_RESULT_MODE_BOOLEAN = 1,
   MTL_VISIBILITY_RESULT_MODE_COUNTING = 2,
};

enum mtl_depth_clip_mode {
   MTL_DEPTH_CLIP_MODE_CLIP = 0,
   MTL_DEPTH_CLIP_MODE_CLAMP = 1,
};

/** STRUCTURES */
struct mtl_range {
   size_t offset;
   size_t length;
};

struct mtl_origin {
   size_t x, y, z;
};

struct mtl_size {
   size_t x, y, z;
};

struct mtl_viewport {
   double originX, originY, width, height, znear, zfar;
};

struct mtl_clear_color {
   union {
      struct {
         double red, green, blue, alpha;
      };
      double channel[4];
   };
};

struct mtl_scissor_rect {
   size_t x, y, width, height;
};

struct mtl_texture_swizzle_channels {
   enum mtl_texture_swizzle red;
   enum mtl_texture_swizzle green;
   enum mtl_texture_swizzle blue;
   enum mtl_texture_swizzle alpha;
};

struct mtl_buffer_image_copy {
   struct mtl_size image_size;
   struct mtl_origin image_origin;
   mtl_buffer *buffer;
   mtl_texture *image;
   size_t buffer_offset_B;
   size_t buffer_stride_B;
   size_t buffer_2d_image_size_B;
   size_t image_slice;
   size_t image_level;
   enum mtl_blit_options options;
};

#endif /* KK_MTL_TYPES_H */
