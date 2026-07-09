/*
 * Copyright © 2026 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <vector>

#include "imgui.h"

#include "gamma_util.h"

#include "shaders/gamma_shader_interface.h"
#include "util/gamma_format.h"
#include "vulkan/vulkan_core.h"

struct gamma_app;

enum gamma_render_task_type {
   gamma_render_task_type_solid,
   gamma_render_task_type_wireframe,
   gamma_render_task_type_thick_wireframe,
   gamma_render_task_type_lines,
   gamma_render_task_type_thick_lines,
   gamma_render_task_type_render_list,
};

struct gamma_render_list;

struct gamma_render_task {
   gamma_render_task_type type = gamma_render_task_type_solid;
   union {
      const gamma_backed_buffer *vertex_buffer = nullptr;
      gamma_render_list *render_list;
   };
   uint32_t first_vertex = 0;
   uint32_t vertex_count = 0;
   gamma_render_params params;
   uint32_t flags = 0;
   bool wait_for_prev = false;
   bool override_flags = false;
};

struct gamma_render_list {
   gamma_app *app;
   std::vector<gamma_render_task> tasks;
   std::shared_ptr<gamma_backed_buffer> constants = nullptr;
   VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
   VkDescriptorSet descriptor_set = VK_NULL_HANDLE;

   gamma_render_list(gamma_app *app): app(app)
   {
   }

   ~gamma_render_list();

   void build();
};

struct gamma_imgui_texture {
   VkDescriptorSet descriptor_set;

   gamma_imgui_texture(VkDescriptorSet descriptor_set): descriptor_set(descriptor_set)
   {
   }

   ~gamma_imgui_texture();
};

struct gamma_viewport_state {
   std::shared_ptr<gamma_imgui_texture> surface = nullptr;
   std::shared_ptr<gamma_backed_image> resolved_image = nullptr;
   std::shared_ptr<gamma_backed_image> cb = nullptr;
   std::shared_ptr<gamma_backed_image> db = nullptr;
   uint32_t width = 0;
   uint32_t height = 0;
   std::shared_ptr<gamma_render_list> render_list = nullptr;
};

enum gamma_visualization_color {
   gamma_visualization_color_primitive_index,
   gamma_visualization_color_geometry_index,
   gamma_visualization_color_instance_index,
};

static inline uint32_t
gamma_visualization_color_to_renderer_color(gamma_visualization_color color)
{
   switch (color) {
   case gamma_visualization_color_primitive_index:
      return GAMMA_RENDERER_COLOR_PRIMITIVE_INDEX;
   case gamma_visualization_color_geometry_index:
      return GAMMA_RENDERER_COLOR_GEOMETRY_INDEX;
   case gamma_visualization_color_instance_index:
      return GAMMA_RENDERER_COLOR_PUSH_CONSTANT;
   }
   return GAMMA_RENDERER_COLOR_PRIMITIVE_INDEX;
}

static inline gamma_vec3
gamma_index_to_color(uint32_t index)
{
   return {
      .x = ((index * 0xd83f0930) >> 24) / 255.0f,
      .y = ((index * 0x8fa9836b) >> 24) / 255.0f,
      .z = ((index * 0x3037f8ad) >> 24) / 255.0f,
   };
}

enum gamma_camera_type {
   gamma_camera_type_first_person,
   gamma_camera_type_pivot,
};

struct gamma_file_view;

struct gamma_acceleration_structure {
   gamma_app *app = nullptr;
   gamma_file_view *view = nullptr;
   gamma_acceleration_structure_header header;
   char *name = nullptr;
   void *data = nullptr;

   uint32_t *primitive_counts = nullptr;
   uint32_t *primitive_counts_exclusive_sum = nullptr;
   uint32_t primitive_count = 0;

   gamma_aabb aabb;

   struct {
      bool opened = false;
      bool request_focus = false;
      bool prev_focused = false;

      gamma_viewport_state viewport;
      std::vector<gamma_viewport_state> pending_viewports;

      std::shared_ptr<gamma_backed_buffer> vertex_buffer = nullptr;
      std::shared_ptr<gamma_render_list> instance_render_list = nullptr;

      gamma_camera_type camera_type = gamma_camera_type_first_person;
      float camera_phi = 0;
      float camera_theta = 0;
      gamma_vec3 camera_pivot = {0, 0, 0};
      float camera_pivot_distance = 0;
      bool camera_pivot_valid = false;
      gamma_vec3 camera_position = {0, 0, 0};
      float camera_speed = 0.3;
      float camera_near = 0.0001;
      float camera_far = 5.0;
      float camera_fov = 60.0;

      gamma_visualization_color visualization_color = gamma_visualization_color_primitive_index;

      bool is_right_dragging_viewport;
      bool is_middle_dragging_viewport;
      ImVec2 prev_mouse_pos;
      float middle_drag_start_z_distance;
   } ui;

   gamma_acceleration_structure(gamma_file_view *view);

   virtual ~gamma_acceleration_structure()
   {
      free(name);
      free(data);
   }

   void init_camera();

   void load(FILE *file, uint32_t chunk_size);
};

struct gamma_file_view {
   gamma_app *app;
   std::filesystem::path path;

   std::vector<std::unique_ptr<gamma_acceleration_structure>> acceleration_structures;
   std::map<uint64_t, uint32_t> address_map;

   uint32_t up_axis = 1 + 3; /* +x, +y, +z, -x, -y, -z */

   struct {
      ImGuiID dockspace_id;
      ImGuiWindowClass dockspace_class;
      bool request_focus = true;
      bool initialized;
      VkAccelerationStructureTypeKHR filter_acceleration_structure_type;
      gamma_acceleration_structure *focused_acceleration_structure = nullptr;
   } ui;

   struct {
      gamma_acceleration_structure *acceleration_structure;
      gamma_mat4 projection_matrix;
      gamma_mat4 inv_camera_rotation;
      gamma_mat4 view_projection_matrix;
      ImVec2 viewport_offset;
      ImVec2 viewport_size;
      bool render;
      gamma_vec3 camera_pivot;
   } rendering_state;

   virtual void load(FILE *file, const gamma_header *header);

   virtual void load_driver_specific(FILE *file, const gamma_chunk_header *chunk_header, const gamma_header *header)
   {
   }

   virtual std::unique_ptr<gamma_acceleration_structure> create_acceleration_structure() = 0;

   virtual void dock_driver_specific(uint32_t left_top, uint32_t left_bottom, uint32_t center, uint32_t right_top,
                                     uint32_t right_bottom)
   {
   }

   virtual float handle_mouse_click(gamma_acceleration_structure *acceleration_structure, gamma_ray ray, bool select)
   {
      return INFINITY;
   }

   virtual void run() = 0;

   /* Helper functions for updating and drawing the UI */
   void begin();
   void end();

   bool begin_viewport(gamma_acceleration_structure *acceleration_structure);

   void render(const gamma_render_task &task);
   void render_aabb(const gamma_render_task &base_task, gamma_aabb aabb, bool fill = false);

   void render_viewport();

   void draw_point(gamma_vec3 pos, gamma_vec3 color, float stroke);
   void draw_line(gamma_vec3 start, gamma_vec3 end, gamma_vec3 color, float stroke);

   void end_viewport();

   void begin_bvh_tree();
   void end_bvh_tree();

   void begin_node_info();
   void end_node_info();

   gamma_acceleration_structure *addr_to_acceleration_structure(uint64_t address);
};

std::unique_ptr<gamma_file_view> gamma_create_file_view(gamma_app *app, const char *file);

/* Create functions for gamma_file_views specialized for drivers. */

std::unique_ptr<gamma_file_view> gamma_create_file_view_radv();
