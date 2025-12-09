/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */
#ifndef KK_MTL_TO_VK_MAP_H
#define KK_MTL_TO_VK_MAP_H 1

enum pipe_format;

struct mtl_origin;
struct mtl_size;
enum mtl_primitive_type;
enum mtl_primitive_topology_class;
enum mtl_load_action;
enum mtl_store_action;
enum mtl_sampler_address_mode;
enum mtl_sampler_border_color;
enum mtl_sampler_min_mag_filter;
enum mtl_sampler_mip_filter;
enum mtl_compare_function;
enum mtl_winding;
enum mtl_cull_mode;
enum mtl_index_type;

struct VkOffset3D;
struct VkExtent3D;
union VkClearColorValue;
enum VkPrimitiveTopology;
enum VkAttachmentLoadOp;
enum VkAttachmentStoreOp;
enum VkSamplerAddressMode;
enum VkBorderColor;
enum VkFilter;
enum VkSamplerMipmapMode;
enum VkCompareOp;
enum VkFrontFace;
enum VkCullModeFlagBits;

/* STRUCTS */
struct mtl_origin vk_offset_3d_to_mtl_origin(const struct VkOffset3D *offset);

struct mtl_size vk_extent_3d_to_mtl_size(const struct VkExtent3D *extent);

/* ENUMS */
enum mtl_primitive_type
vk_primitive_topology_to_mtl_primitive_type(enum VkPrimitiveTopology topology);

enum mtl_primitive_topology_class
vk_primitive_topology_to_mtl_primitive_topology_class(
   enum VkPrimitiveTopology topology);

enum mtl_load_action
vk_attachment_load_op_to_mtl_load_action(enum VkAttachmentLoadOp op);

enum mtl_store_action
vk_attachment_store_op_to_mtl_store_action(enum VkAttachmentStoreOp op);

enum mtl_sampler_address_mode
vk_sampler_address_mode_to_mtl_sampler_address_mode(
   enum VkSamplerAddressMode mode);

enum mtl_sampler_border_color
vk_border_color_to_mtl_sampler_border_color(enum VkBorderColor color);

enum mtl_sampler_min_mag_filter
vk_filter_to_mtl_sampler_min_mag_filter(enum VkFilter filter);

enum mtl_sampler_mip_filter
vk_sampler_mipmap_mode_to_mtl_sampler_mip_filter(enum VkSamplerMipmapMode mode);

enum mtl_compare_function
vk_compare_op_to_mtl_compare_function(enum VkCompareOp op);

enum mtl_winding vk_front_face_to_mtl_winding(enum VkFrontFace face);

enum mtl_cull_mode vk_front_face_to_mtl_cull_mode(enum VkCullModeFlagBits mode);

enum mtl_index_type index_size_in_bytes_to_mtl_index_type(unsigned bytes);

#endif /* KK_MTL_TO_VK_MAP_H */
