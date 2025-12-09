/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "vk_to_mtl_map.h"

#include "kosmickrisp/bridge/mtl_types.h"

#include "util/format/u_format.h"

#include "vulkan/vulkan.h"
#include "vk_meta.h"

struct mtl_origin
vk_offset_3d_to_mtl_origin(const struct VkOffset3D *offset)
{
   struct mtl_origin ret = {
      .x = offset->x,
      .y = offset->y,
      .z = offset->z,
   };
   return ret;
}

struct mtl_size
vk_extent_3d_to_mtl_size(const struct VkExtent3D *extent)
{
   struct mtl_size ret = {
      .x = extent->width,
      .y = extent->height,
      .z = extent->depth,
   };
   return ret;
}

enum mtl_primitive_type
vk_primitive_topology_to_mtl_primitive_type(enum VkPrimitiveTopology topology)
{
   switch (topology) {
   case VK_PRIMITIVE_TOPOLOGY_POINT_LIST:
      return MTL_PRIMITIVE_TYPE_POINT;
   case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:
      return MTL_PRIMITIVE_TYPE_LINE;
   case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP:
      return MTL_PRIMITIVE_TYPE_LINE_STRIP;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch"
   case VK_PRIMITIVE_TOPOLOGY_META_RECT_LIST_MESA:
#pragma GCC diagnostic pop
      /* Triangle fans are emulated meaning we'll translate the index buffer to
       * triangle list or generate a index buffer if there's none */
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:
      return MTL_PRIMITIVE_TYPE_TRIANGLE;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
      return MTL_PRIMITIVE_TYPE_TRIANGLE_STRIP;
   default:
      assert(0 && "Primitive topology not supported!");
      return 0;
   }
}

enum mtl_primitive_topology_class
vk_primitive_topology_to_mtl_primitive_topology_class(
   enum VkPrimitiveTopology topology)
{
   switch (topology) {
   case VK_PRIMITIVE_TOPOLOGY_POINT_LIST:
      return MTL_PRIMITIVE_TOPOLOGY_CLASS_POINT;
   case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:
   case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP:
      return MTL_PRIMITIVE_TOPOLOGY_CLASS_LINE;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch"
   case VK_PRIMITIVE_TOPOLOGY_META_RECT_LIST_MESA:
#pragma GCC diagnostic pop
      return MTL_PRIMITIVE_TOPOLOGY_CLASS_TRIANGLE;
   default:
      return MTL_PRIMITIVE_TOPOLOGY_CLASS_UNSPECIFIED;
   }
}

enum mtl_load_action
vk_attachment_load_op_to_mtl_load_action(enum VkAttachmentLoadOp op)
{
   switch (op) {
   case VK_ATTACHMENT_LOAD_OP_LOAD:
      return MTL_LOAD_ACTION_LOAD;
   case VK_ATTACHMENT_LOAD_OP_CLEAR:
      return MTL_LOAD_ACTION_CLEAR;
   case VK_ATTACHMENT_LOAD_OP_DONT_CARE:
      return MTL_LOAD_ACTION_DONT_CARE;
   default:
      assert(false && "Unsupported VkAttachmentLoadOp");
      return MTL_LOAD_ACTION_DONT_CARE;
   };
}

enum mtl_store_action
vk_attachment_store_op_to_mtl_store_action(enum VkAttachmentStoreOp op)
{
   switch (op) {
   case VK_ATTACHMENT_STORE_OP_STORE:
      return MTL_STORE_ACTION_STORE;
   case VK_ATTACHMENT_STORE_OP_DONT_CARE:
      return MTL_STORE_ACTION_DONT_CARE;
   case VK_ATTACHMENT_STORE_OP_NONE:
      return MTL_STORE_ACTION_UNKNOWN;
   default:
      assert(false && "Unsupported VkAttachmentStoreOp");
      return MTL_STORE_ACTION_UNKNOWN;
   };
}

enum mtl_sampler_address_mode
vk_sampler_address_mode_to_mtl_sampler_address_mode(
   enum VkSamplerAddressMode mode)
{
   switch (mode) {
   case VK_SAMPLER_ADDRESS_MODE_REPEAT:
      return MTL_SAMPLER_ADDRESS_MODE_REPEAT;
   case VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT:
      return MTL_SAMPLER_ADDRESS_MODE_MIRROR_REPEAT;
   case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:
      return MTL_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
   case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER:
      return MTL_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER_COLOR;
   case VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE:
      return MTL_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
   default:
      UNREACHABLE("Unsupported address mode");
   }
}

enum mtl_sampler_border_color
vk_border_color_to_mtl_sampler_border_color(enum VkBorderColor color)
{
   switch (color) {
   case VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK:
   case VK_BORDER_COLOR_INT_TRANSPARENT_BLACK:
      return MTL_SAMPLER_BORDER_COLOR_TRANSPARENT_BLACK;
   case VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK:
   case VK_BORDER_COLOR_INT_OPAQUE_BLACK:
      return MTL_SAMPLER_BORDER_COLOR_OPAQUE_BLACK;
   case VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE:
   case VK_BORDER_COLOR_INT_OPAQUE_WHITE:
      return MTL_SAMPLER_BORDER_COLOR_OPAQUE_WHITE;
   case VK_BORDER_COLOR_FLOAT_CUSTOM_EXT:
   case VK_BORDER_COLOR_INT_CUSTOM_EXT:
      return MTL_SAMPLER_BORDER_COLOR_OPAQUE_WHITE;
   default:
      UNREACHABLE("Unsupported address mode");
   }
}

enum mtl_sampler_min_mag_filter
vk_filter_to_mtl_sampler_min_mag_filter(enum VkFilter filter)
{
   switch (filter) {
   case VK_FILTER_NEAREST:
      return MTL_SAMPLER_MIN_MAG_FILTER_NEAREST;
   case VK_FILTER_LINEAR:
      return MTL_SAMPLER_MIN_MAG_FILTER_LINEAR;
   default:
      UNREACHABLE("Unsupported address mode");
   }
}

enum mtl_sampler_mip_filter
vk_sampler_mipmap_mode_to_mtl_sampler_mip_filter(enum VkSamplerMipmapMode mode)
{
   switch (mode) {
   case VK_SAMPLER_MIPMAP_MODE_NEAREST:
      return MTL_SAMPLER_MIP_FILTER_NEAREST;
   case VK_SAMPLER_MIPMAP_MODE_LINEAR:
      return MTL_SAMPLER_MIP_FILTER_LINEAR;
   default:
      UNREACHABLE("Unsupported address mode");
   }
}

enum mtl_compare_function
vk_compare_op_to_mtl_compare_function(enum VkCompareOp op)
{
   switch (op) {
   case VK_COMPARE_OP_NEVER:
      return MTL_COMPARE_FUNCTION_NEVER;
   case VK_COMPARE_OP_LESS:
      return MTL_COMPARE_FUNCTION_LESS;
   case VK_COMPARE_OP_EQUAL:
      return MTL_COMPARE_FUNCTION_EQUAL;
   case VK_COMPARE_OP_LESS_OR_EQUAL:
      return MTL_COMPARE_FUNCTION_LESS_EQUAL;
   case VK_COMPARE_OP_GREATER:
      return MTL_COMPARE_FUNCTION_GREATER;
   case VK_COMPARE_OP_NOT_EQUAL:
      return MTL_COMPARE_FUNCTION_NOT_EQUAL;
   case VK_COMPARE_OP_GREATER_OR_EQUAL:
      return MTL_COMPARE_FUNCTION_GREATER_EQUAL;
   case VK_COMPARE_OP_ALWAYS:
      return MTL_COMPARE_FUNCTION_ALWAYS;
   default:
      UNREACHABLE("Unsupported address mode");
   }
}

enum mtl_winding
vk_front_face_to_mtl_winding(enum VkFrontFace face)
{
   switch (face) {
   case VK_FRONT_FACE_CLOCKWISE:
      return MTL_WINDING_CLOCKWISE;
   case VK_FRONT_FACE_COUNTER_CLOCKWISE:
      return MTL_WINDING_COUNTER_CLOCKWISE;
   default:
      assert(false && "Unsupported VkFrontFace");
      return MTL_WINDING_CLOCKWISE;
   }
}

enum mtl_cull_mode
vk_front_face_to_mtl_cull_mode(enum VkCullModeFlagBits mode)
{
   switch (mode) {
   case VK_CULL_MODE_NONE:
      return MTL_CULL_MODE_NONE;
   case VK_CULL_MODE_FRONT_BIT:
      return MTL_CULL_MODE_FRONT;
   case VK_CULL_MODE_BACK_BIT:
      return MTL_CULL_MODE_BACK;
   default:
      UNREACHABLE("Unsupported VkCullModeFlags");
   }
}

enum mtl_index_type
index_size_in_bytes_to_mtl_index_type(unsigned bytes)
{
   switch (bytes) {
   case 2u:
      return MTL_INDEX_TYPE_UINT16;
   case 4u:
      return MTL_INDEX_TYPE_UINT32;
   default:
      UNREACHABLE("Unsupported byte size for index");
   }
}
