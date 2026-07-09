/*
 * Copyright © 2026 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <imgui.h>
#include <map>
#include <memory>
#include <stdint.h>
#include "bvh_defines.h"
#include "gamma_file_view.h"
#include "gamma_util.h"
#include "radv_gamma.h"
#include <unordered_set>

struct gamma_acceleration_structure_radv : public gamma_acceleration_structure {
   gamma_acceleration_structure_radv(gamma_file_view *view): gamma_acceleration_structure(view)
   {
   }

   struct {
      uint32_t selected_node_id = RADV_BVH_INVALID_NODE;
      bool scroll_to_selected_node = false;
      std::unordered_set<uint32_t> open_path;
   } radv_ui;

   const uint8_t *bvh;

   uint32_t first_vertex;
   uint32_t wireframe_first_vertex;
};

struct gamma_dispatch_view_settings {
   uint32_t width = 0;
   uint32_t height = 0;
   uint32_t depth = 0;
   uint32_t z = 0;
   uint32_t max_iteration_count = 0;
   bool show_box_iteration_count = true;
   bool show_instance_iteration_count = true;
   bool show_primitive_iteration_count = true;
};

struct gamma_invocation_data {
   std::vector<uint32_t> offsets;
   uint32_t selected_ray = 0;
   uint32_t box_iteration_count = 0;
   uint32_t instance_iteration_count = 0;
   uint32_t primitive_iteration_count = 0;
};

struct gamma_dispatch_view {
   struct {
      bool open = false;
      bool request_focus = false;

      bool initialize_viewport = true;

      std::vector<std::shared_ptr<ImTextureData>> texture_data;
      std::vector<std::shared_ptr<ImTextureData>> destroy_texture_data;

      float image_scale;
      gamma_vec2 viewport_center;
      bool is_dragging_viewport = false;
      ImVec2 prev_mouse_pos;
      uint32_t selection_x = UINT32_MAX;
      uint32_t selection_y = UINT32_MAX;

      gamma_dispatch_view_settings prev_settings;
      gamma_dispatch_view_settings settings;
   } ui;

   const radv_gamma_dispatch_info *info;

   uint32_t invocation_count;
   uint32_t max_iteration_count = 0;

   std::vector<gamma_invocation_data> invocations;

   std::map<uint32_t, std::map<uint32_t, uint32_t>> valid_dispatch_sizes;
};

struct gamma_file_view_radv : public gamma_file_view {
   std::vector<std::shared_ptr<gamma_dispatch_view>> dispatch_views;

   gamma_dispatch_view *selected_dispatch = nullptr;

   void *ray_history_data = nullptr;
   const radv_gamma_ray_history_header *history_header = nullptr;
   const radv_gamma_dispatch_info *dispatch_infos = nullptr;
   const uint8_t *history_data = nullptr;
   uint32_t dispatch_count = 0;
   uint64_t ray_history_data_size;

   radv_gamma_trace_info *trace_info;

   virtual std::unique_ptr<gamma_acceleration_structure> create_acceleration_structure() override;

   virtual void load(FILE *file, const gamma_header *header) override;

   virtual void load_driver_specific(FILE *file, const gamma_chunk_header *chunk_header,
                                     const gamma_header *header) override;

   virtual void dock_driver_specific(uint32_t left_top, uint32_t left_bottom, uint32_t center, uint32_t right_top,
                                     uint32_t right_bottom) override;

   virtual float handle_mouse_click(gamma_acceleration_structure *acceleration_structure, gamma_ray ray,
                                    bool select) override;

   virtual void run() override;

   gamma_invocation_data *get_selected_invocation();

   /* bvh4 */
   void bvh4_traverse(gamma_acceleration_structure_radv *acceleration_structure, gamma_ray ray, uint32_t id,
                      float *tmax, const std::unordered_set<uint32_t> &path, bool select);

   gamma_aabb bvh4_scene_aabb(gamma_acceleration_structure_radv *acceleration_structure);

   void bvh4_get_vertices(gamma_acceleration_structure_radv *acceleration_structure, uint32_t id,
                          gamma_vertex *vertices, gamma_vertex *wireframe_vertices);

   void bvh4_get_instances(gamma_acceleration_structure_radv *tlas, uint32_t id);

   void bvh4_render_selection(gamma_acceleration_structure_radv *acceleration_structure);

   void bvh4_draw(gamma_acceleration_structure_radv *acceleration_structure, uint32_t id);

   void bvh4_draw_node_info(const gamma_acceleration_structure_radv *acceleration_structure, uint32_t id);

   void bvh4_get_instance_info(const gamma_acceleration_structure_radv *acceleration_structure, uint32_t id,
                               gamma_mat4 *transform, gamma_acceleration_structure_radv **blas);

   /* bvh8 */
   void bvh8_traverse(gamma_acceleration_structure_radv *acceleration_structure, gamma_ray ray, uint32_t id,
                      float *tmax, const std::unordered_set<uint32_t> &path, bool select);

   gamma_aabb bvh8_scene_aabb(gamma_acceleration_structure_radv *acceleration_structure);

   void bvh8_get_vertices(gamma_acceleration_structure_radv *acceleration_structure, uint32_t id,
                          gamma_vertex *vertices, gamma_vertex *wireframe_vertices);

   void bvh8_get_instances(gamma_acceleration_structure_radv *tlas, uint32_t id);

   void bvh8_render_selection(gamma_acceleration_structure_radv *acceleration_structure);

   void bvh8_draw(gamma_acceleration_structure_radv *acceleration_structure, uint32_t id);

   void bvh8_draw_node_info(const gamma_acceleration_structure_radv *acceleration_structure, uint32_t id);

   void bvh8_get_instance_info(const gamma_acceleration_structure_radv *acceleration_structure, uint32_t id,
                               gamma_mat4 *transform, gamma_acceleration_structure_radv **blas);
};

static inline uint64_t
radv_node_to_addr(uint64_t node)
{
   node &= ~7ull;
   node <<= 19;
   return ((int64_t)node) >> 16;
}

static inline gamma_mat4
mat3x4_to_gamma(mat3x4 m)
{
   gamma_mat4 result;
   for (uint32_t i = 0; i < 3; i++) {
      for (uint32_t j = 0; j < 4; j++) {
         result.elements[i + j * 4] = m.values[i][j];
      }
   }
   return result;
}
