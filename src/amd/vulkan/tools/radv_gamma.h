/*
 * Copyright © 2026 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

struct radv_gamma_trace_info {
   uint64_t bvh8 : 1;
   uint64_t padding : 63;
};

struct radv_gamma_ray_history_header {
   uint64_t dispatch_count;
};

enum radv_gamma_dispatch_type {
   radv_gamma_dispatch_type_trace_rays,
   radv_gamma_dispatch_type_trace_rays_indirect,
   radv_gamma_dispatch_type_trace_rays_indirect2,
};

struct radv_gamma_dispatch_info {
   enum radv_gamma_dispatch_type type;
   uint32_t dimensions[3];
};

enum radv_packed_token_type {
   radv_packed_token_trace_ray,
   radv_packed_token_iteration,
   radv_packed_token_accel_struct,
   radv_packed_token_trace_ray_hit,
   radv_packed_token_trace_ray_miss,
};

struct radv_packed_token_header {
   uint32_t launch_index : 29;
   uint32_t token_type : 3;
};

struct radv_packed_trace_ray_token {
   struct radv_packed_token_header header;

   uint32_t accel_struct_lo;
   uint32_t accel_struct_hi;

   uint32_t flags : 16;
   uint32_t dispatch_index : 16;

   uint32_t sbt_offset : 4;
   uint32_t sbt_stride : 4;
   uint32_t miss_index : 16;
   uint32_t cull_mask : 8;

   float origin[3];
   float tmin;
   float direction[3];
   float tmax;
};

struct radv_packed_trace_ray_end_token {
   struct radv_packed_token_header header;

   uint32_t dispatch_index;

   uint32_t iteration_count : 16;
   uint32_t instance_count : 16;

   uint32_t ahit_count : 16;
   uint32_t isec_count : 16;

   uint32_t primitive_id;
   uint32_t geometry_id;

   uint32_t instance_id : 24;
   uint32_t hit_kind : 8;

   float t;
};

struct radv_packed_iteration_token {
   struct radv_packed_token_header header;
   uint32_t dispatch_index;
   uint32_t node_id;
};

struct radv_packed_accel_struct_token {
   struct radv_packed_token_header header;
   uint32_t dispatch_index;
   uint64_t accel_struct;
};
