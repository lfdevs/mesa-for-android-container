/*
 * Copyright © 2026 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "gamma_file_view.h"
#include "gamma_file_view_radv.h"

#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <imgui.h>
#include <memory>
#include <stdint.h>

#include "shaders/gamma_shader_interface.h"
#include "util/gamma_format.h"
#include "util/half_float.h"
#include "vulkan/vulkan_core.h"

#include "bvh_defines.h"
#include "gamma_app.h"
#include "gamma_util.h"

static radv_bvh_box32_node
radv_get_box_node(const void *node, uint32_t type)
{
   if (type == radv_bvh_node_box32)
      return *(const radv_bvh_box32_node *)node;

   const radv_bvh_box16_node *box16 = (const radv_bvh_box16_node *)node;

   radv_bvh_box32_node box32;
   memset(&box32, 0, sizeof(box32));

   for (uint32_t i = 0; i < 4; i++) {
      box32.children[i] = box16->children[i];
      box32.coords[i] = {
         .min =
            {
               .x = _mesa_half_to_float(box16->coords[i].min_x),
               .y = _mesa_half_to_float(box16->coords[i].min_y),
               .z = _mesa_half_to_float(box16->coords[i].min_z),
            },
         .max =
            {
               .x = _mesa_half_to_float(box16->coords[i].max_x),
               .y = _mesa_half_to_float(box16->coords[i].max_y),
               .z = _mesa_half_to_float(box16->coords[i].max_z),
            },
      };
   }

   return box32;
}

static gamma_acceleration_structure_radv *
radv_bvh_instance_node_get_blas(gamma_file_view *view, const radv_bvh_instance_node *node)
{
   return (gamma_acceleration_structure_radv *)view->addr_to_acceleration_structure(radv_node_to_addr(node->bvh_ptr));
}

static gamma_aabb
vk_aabb_to_gamma(vk_aabb aabb)
{
   return {
      .min = {aabb.min.x, aabb.min.y, aabb.min.z},
      .max = {aabb.max.x, aabb.max.y, aabb.max.z},
   };
}

void
gamma_file_view_radv::bvh4_traverse(gamma_acceleration_structure_radv *acceleration_structure, gamma_ray ray,
                                    uint32_t id, float *tmax, const std::unordered_set<uint32_t> &path, bool select)
{
   uint32_t offset = (id & (~0x7)) << 3;
   uint32_t type = id & 0x7;

   std::unordered_set<uint32_t> path_copy = path;
   if (select)
      path_copy.insert(id);

   radv_bvh_box32_node node = radv_get_box_node(acceleration_structure->bvh + offset, type);
   for (uint32_t i = 0; i < 4; i++) {
      if (node.children[i] == RADV_BVH_INVALID_NODE)
         continue;

      uint32_t child_offset = (node.children[i] & (~0x7)) << 3;
      uint32_t child_type = node.children[i] & 0x7;
      gamma_aabb aabb = vk_aabb_to_gamma(node.coords[i]);
      float t = gamma_aabb::intersect_ray(aabb, ray);
      if (t >= *tmax)
         continue;

      if (child_type == radv_bvh_node_box32 || child_type == radv_bvh_node_box16) {
         bvh4_traverse(acceleration_structure, ray, node.children[i], tmax, path_copy, select);
      } else if (child_type == radv_bvh_node_triangle) {
         const radv_bvh_triangle_node *triangle_node =
            (const radv_bvh_triangle_node *)(acceleration_structure->bvh + child_offset);

         gamma_triangle triangle;
         for (uint32_t j = 0; j < 3; j++) {
            triangle.vertices[j] = {triangle_node->coords[j][0], triangle_node->coords[j][1],
                                    triangle_node->coords[j][2]};
         }

         t = gamma_triangle::intersect_ray(triangle, ray);

         if (t < *tmax) {
            *tmax = t;
            if (select) {
               acceleration_structure->radv_ui.selected_node_id = node.children[i];
               acceleration_structure->radv_ui.open_path = path_copy;
               acceleration_structure->radv_ui.scroll_to_selected_node = true;
            }
         }
      } else if (child_type == radv_bvh_node_aabb) {
         if (t < *tmax) {
            *tmax = t;
            if (select) {
               acceleration_structure->radv_ui.selected_node_id = node.children[i];
               acceleration_structure->radv_ui.open_path = path_copy;
               acceleration_structure->radv_ui.scroll_to_selected_node = true;
            }
         }
      } else {
         const radv_bvh_instance_node *instance =
            (const radv_bvh_instance_node *)(acceleration_structure->bvh + child_offset);
         gamma_ray object_ray = gamma_ray::transform(ray, mat3x4_to_gamma(instance->wto_matrix));
         gamma_acceleration_structure_radv *blas = radv_bvh_instance_node_get_blas(this, instance);
         float blas_tmax = INFINITY;
         std::unordered_set<uint32_t> blas_path;
         bvh4_traverse(blas, object_ray, RADV_BVH_ROOT_NODE, &blas_tmax, blas_path, false);
         if (blas_tmax < INFINITY) {
            if (select) {
               acceleration_structure->radv_ui.selected_node_id = node.children[i];
               acceleration_structure->radv_ui.open_path = path_copy;
               acceleration_structure->radv_ui.scroll_to_selected_node = true;
            }
            *tmax = blas_tmax;
         }
      }
   }
}

gamma_aabb
gamma_file_view_radv::bvh4_scene_aabb(gamma_acceleration_structure_radv *acceleration_structure)
{
   gamma_aabb aabb;

   uint32_t root_offset = (RADV_BVH_ROOT_NODE & (~0x7)) << 3;
   uint32_t root_type = RADV_BVH_ROOT_NODE & 0x7;
   assert(root_type == radv_bvh_node_box32);

   bool first = true;
   const radv_bvh_box32_node *root_node = (const radv_bvh_box32_node *)(acceleration_structure->bvh + root_offset);
   for (uint32_t i = 0; i < 4; i++) {
      if (root_node->children[i] == RADV_BVH_INVALID_NODE)
         continue;

      gamma_aabb child_aabb = vk_aabb_to_gamma(root_node->coords[i]);
      if (first) {
         aabb = child_aabb;
         first = false;
      } else {
         aabb = gamma_aabb::combine(aabb, child_aabb);
      }
   }

   return aabb;
}

void
gamma_file_view_radv::bvh4_get_vertices(gamma_acceleration_structure_radv *acceleration_structure, uint32_t id,
                                        gamma_vertex *vertices, gamma_vertex *wireframe_vertices)
{
   uint32_t offset = (id & (~0x7)) << 3;
   uint32_t type = id & 0x7;
   radv_bvh_box32_node box_node = radv_get_box_node(acceleration_structure->bvh + offset, type);
   for (uint32_t i = 0; i < 4; i++) {
      if (box_node.children[i] == RADV_BVH_INVALID_NODE)
         continue;

      uint32_t child_offset = (box_node.children[i] & (~0x7)) << 3;
      uint32_t child_type = box_node.children[i] & 0x7;
      if (child_type == radv_bvh_node_box32 || child_type == radv_bvh_node_box16) {
         bvh4_get_vertices(acceleration_structure, box_node.children[i], vertices, wireframe_vertices);
      } else if (child_type == radv_bvh_node_triangle) {
         const radv_bvh_triangle_node *node =
            (const radv_bvh_triangle_node *)(acceleration_structure->bvh + child_offset);

         uint32_t geometry_index = node->geometry_id_and_flags & 0xffffff;

         uint32_t dst_index =
            acceleration_structure->primitive_counts_exclusive_sum[node->geometry_id_and_flags & 0xfffffff] +
            node->triangle_id;
         for (uint32_t j = 0; j < 3; j++) {
            vertices[dst_index * 3 + j].position = {node->coords[j][0], node->coords[j][1], node->coords[j][2]};
            vertices[dst_index * 3 + j].geometry_index = geometry_index;
            vertices[dst_index * 3 + j].primitive_index = node->triangle_id;
         }
      } else if (child_type == radv_bvh_node_aabb) {
         const radv_bvh_aabb_node *node = (const radv_bvh_aabb_node *)(acceleration_structure->bvh + child_offset);
         uint32_t dst_index =
            acceleration_structure->primitive_counts_exclusive_sum[node->geometry_id_and_flags & 0xfffffff] +
            node->primitive_id;
         gamma_aabb aabb = vk_aabb_to_gamma(box_node.coords[i]);
         gamma_generate_cube_vertices(wireframe_vertices + dst_index * GAMMA_CUBE_VERTEX_COUNT, aabb);
         gamma_generate_filled_cube_vertices(vertices + dst_index * GAMMA_FILLED_CUBE_VERTEX_COUNT, aabb,
                                             node->geometry_id_and_flags & 0xffffff, node->primitive_id);
      }
   }
}

void
gamma_file_view_radv::bvh4_get_instances(gamma_acceleration_structure_radv *tlas, uint32_t id)
{
   uint32_t offset = (id & (~0x7)) << 3;
   uint32_t type = id & 0x7;

   if (type == radv_bvh_node_box32 || type == radv_bvh_node_box16) {
      radv_bvh_box32_node node = radv_get_box_node(tlas->bvh + offset, type);
      for (uint32_t i = 0; i < 4; i++)
         if (node.children[i] != RADV_BVH_INVALID_NODE)
            bvh4_get_instances(tlas, node.children[i]);
   } else if (type == radv_bvh_node_instance) {
      const radv_bvh_instance_node *node = (const radv_bvh_instance_node *)(tlas->bvh + offset);
      gamma_acceleration_structure_radv *blas = radv_bvh_instance_node_get_blas(this, node);

      uint32_t primitive_vertex_count =
         blas->header.geometry_type == VK_GEOMETRY_TYPE_TRIANGLES_KHR ? 3 : GAMMA_FILLED_CUBE_VERTEX_COUNT;

      gamma_render_task solid_task;
      solid_task.vertex_buffer = blas->ui.vertex_buffer.get();
      solid_task.first_vertex = blas->first_vertex;
      solid_task.vertex_count = blas->primitive_count * primitive_vertex_count;
      solid_task.params.transform = mat3x4_to_gamma(node->otw_matrix);
      solid_task.params.color = gamma_index_to_color(node->instance_id);
      solid_task.flags = GAMMA_RENDERER_COLOR_PUSH_CONSTANT;
      tlas->ui.instance_render_list->tasks.push_back(solid_task);
   }
}

static const char *node_type_names[8] = {
   "triangle0", "triangle1", "triangle2", "triangle3", "box16", "box32", "instance", "aabb",
};

void
gamma_file_view_radv::bvh4_draw(gamma_acceleration_structure_radv *acceleration_structure, uint32_t id)
{
   uint32_t offset = (id & (~0x7)) << 3;
   uint32_t type = id & 0x7;

   bool selected = acceleration_structure->radv_ui.selected_node_id == id;

   ImGuiTreeNodeFlags node_flags =
      ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
   if (type != radv_bvh_node_box32 && type != radv_bvh_node_box16)
      node_flags |= ImGuiTreeNodeFlags_Leaf;
   if (selected)
      node_flags |= ImGuiTreeNodeFlags_Selected;

   ImGui::TableNextRow();
   ImGui::TableNextColumn();

   if (acceleration_structure->radv_ui.open_path.find(id) != acceleration_structure->radv_ui.open_path.end())
      ImGui::SetNextItemOpen(true);

   char tree_node_id[32];
   sprintf(tree_node_id, "%x %x", type, offset);

   bool open = ImGui::TreeNodeEx(tree_node_id, node_flags, "%s", node_type_names[type]);

   if (ImGui::IsItemClicked())
      acceleration_structure->radv_ui.selected_node_id = id;

   ImGui::TableNextColumnText("0x%x", offset);

   if (open) {
      if (type == radv_bvh_node_box32 || type == radv_bvh_node_box16) {
         radv_bvh_box32_node box_node = radv_get_box_node(acceleration_structure->bvh + offset, type);
         for (uint32_t i = 0; i < 4; i++) {
            if (box_node.children[i] != RADV_BVH_INVALID_NODE)
               bvh4_draw(acceleration_structure, box_node.children[i]);
         }
      }
      ImGui::TreePop();
   }

   if (acceleration_structure->radv_ui.scroll_to_selected_node && selected) {
      ImGui::SetScrollHereY();
      acceleration_structure->radv_ui.scroll_to_selected_node = false;
   }
}

void
gamma_file_view_radv::bvh4_draw_node_info(const gamma_acceleration_structure_radv *acceleration_structure, uint32_t id)
{

   uint32_t offset = (acceleration_structure->radv_ui.selected_node_id & (~0x7)) << 3;
   uint32_t type = acceleration_structure->radv_ui.selected_node_id & 0x7;

   if (type == radv_bvh_node_box32 || type == radv_bvh_node_box16) {
      radv_bvh_box32_node node = radv_get_box_node(acceleration_structure->bvh + offset, type);

      char tmp[64];
      sprintf(tmp, "box%u node (0x%x)", type == radv_bvh_node_box32 ? 32 : 16, offset);

      ImGui::SeparatorText(tmp);

      for (uint32_t i = 0; i < 4; i++) {
         sprintf(tmp, "child[%u]", i);
         ImGui::SeparatorText(tmp);

         sprintf(tmp, "child[%u] properties", i);

         if (ImGui::BeginTable(tmp, 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableNextColumnText("id");
            ImGui::TableNextColumnText("0x%x", node.children[i]);

            ImGui::TableNextColumnText("aabb");
            ImGui::TableNextColumnText("(%f, %f, %f)   (%f, %f, %f)", node.coords[i].min.x, node.coords[i].min.y,
                                       node.coords[i].min.z, node.coords[i].max.x, node.coords[i].max.y,
                                       node.coords[i].max.z);

            ImGui::EndTable();
         }
      }
   } else if (type == radv_bvh_node_instance) {
      const radv_bvh_instance_node *node = (const radv_bvh_instance_node *)(acceleration_structure->bvh + offset);

      char tmp[64];
      sprintf(tmp, "instance node (0x%x)", offset);

      ImGui::SeparatorText(tmp);

      if (ImGui::BeginTable("instance node properties", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
         ImGui::TableNextColumnText("bvh_ptr");
         ImGui::TableNextColumn();
         sprintf(tmp, "0x%" PRIx64, node->bvh_ptr);
         if (ImGui::TextLink(tmp)) {
            gamma_acceleration_structure_radv *blas = radv_bvh_instance_node_get_blas(this, node);
            if (blas->ui.opened)
               blas->ui.request_focus = true;
            blas->ui.opened = true;
         }

         for (uint32_t i = 0; i < 3; i++) {
            ImGui::TableNextColumnText("wto_matrix.row[%u]", i);
            ImGui::TableNextColumnText("(%f, %f, %f, %f)", node->wto_matrix.values[i][0], node->wto_matrix.values[i][1],
                                       node->wto_matrix.values[i][2], node->wto_matrix.values[i][3]);
         }

         for (uint32_t i = 0; i < 3; i++) {
            ImGui::TableNextColumnText("otw_matrix.row[%u]", i);
            ImGui::TableNextColumnText("(%f, %f, %f, %f)", node->otw_matrix.values[i][0], node->otw_matrix.values[i][1],
                                       node->otw_matrix.values[i][2], node->otw_matrix.values[i][3]);
         }

         ImGui::TableNextColumnText("user_data");
         ImGui::TableNextColumnText("0x%x", node->custom_instance_and_mask & 0xFFFFFF);

         ImGui::TableNextColumnText("mask");
         ImGui::TableNextColumnText("0x%x", node->custom_instance_and_mask >> 24);

         ImGui::TableNextColumnText("sbt_offset");
         ImGui::TableNextColumnText("0x%x", node->sbt_offset_and_flags & 0xFFFFFF);

         ImGui::TableNextColumnText("flags");
         ImGui::TableNextColumnText(
            "%s%s%s%s", (node->sbt_offset_and_flags & RADV_INSTANCE_FORCE_OPAQUE) ? "force_opaque " : "",
            (node->sbt_offset_and_flags & RADV_INSTANCE_NO_FORCE_NOT_OPAQUE) ? "no_force_not_opaque " : "",
            (node->sbt_offset_and_flags & RADV_INSTANCE_TRIANGLE_FACING_CULL_DISABLE) ? "triangle_facing_cull_disable "
                                                                                      : "",
            (node->sbt_offset_and_flags & RADV_INSTANCE_TRIANGLE_FLIP_FACING) ? "triangle_flip_facing " : "");

         ImGui::TableNextColumnText("instance_id");
         ImGui::TableNextColumnText("%u", node->instance_id);

         ImGui::EndTable();
      }
   } else if (type == radv_bvh_node_triangle) {
      const radv_bvh_triangle_node *node = (const radv_bvh_triangle_node *)(acceleration_structure->bvh + offset);

      char tmp[64];
      sprintf(tmp, "triangle node (0x%x)", offset);

      ImGui::SeparatorText(tmp);

      if (ImGui::BeginTable("triangle node properties", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
         for (uint32_t i = 0; i < 3; i++) {
            ImGui::TableNextColumnText("v%u", i);
            ImGui::TableNextColumnText("(%f, %f, %f)", node->coords[i][0], node->coords[i][1], node->coords[i][2]);
         }

         ImGui::TableNextColumnText("triangle_id");
         ImGui::TableNextColumnText("%u", node->triangle_id);

         ImGui::TableNextColumnText("geometry_id");
         ImGui::TableNextColumnText("%u", node->geometry_id_and_flags & 0xfffffff);

         ImGui::TableNextColumnText("flags");
         ImGui::TableNextColumnText("%s", (node->geometry_id_and_flags & RADV_GEOMETRY_OPAQUE) ? "opaque " : "");

         ImGui::EndTable();
      }
   } else {
      const radv_bvh_aabb_node *node = (const radv_bvh_aabb_node *)(acceleration_structure->bvh + offset);

      char tmp[64];
      sprintf(tmp, "aabb node (0x%x)", offset);

      ImGui::SeparatorText(tmp);

      if (ImGui::BeginTable("aabb node properties", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
         ImGui::TableNextColumnText("primitive_id");
         ImGui::TableNextColumnText("%u", node->primitive_id);

         ImGui::TableNextColumnText("geometry_id");
         ImGui::TableNextColumnText("%u", node->geometry_id_and_flags & 0xfffffff);

         ImGui::TableNextColumnText("flags");
         ImGui::TableNextColumnText("%s", (node->geometry_id_and_flags & RADV_GEOMETRY_OPAQUE) ? "opaque " : "");

         ImGui::EndTable();
      }
   }
}

void
gamma_file_view_radv::bvh4_render_selection(gamma_acceleration_structure_radv *acceleration_structure)
{
   gamma_render_task selection_task;
   selection_task.type = gamma_render_task_type_thick_wireframe;
   selection_task.vertex_buffer = acceleration_structure->ui.vertex_buffer.get();
   selection_task.vertex_count = 3;
   selection_task.params.color = app->selection_color;
   selection_task.flags = GAMMA_RENDERER_COLOR_PUSH_CONSTANT;
   selection_task.wait_for_prev = true;

   if (acceleration_structure->wireframe_first_vertex != acceleration_structure->first_vertex) {
      selection_task.type = gamma_render_task_type_thick_lines;
      selection_task.vertex_count = GAMMA_CUBE_VERTEX_COUNT;
   }

   uint32_t offset = (acceleration_structure->radv_ui.selected_node_id & (~0x7)) << 3;
   uint32_t type = acceleration_structure->radv_ui.selected_node_id & 0x7;

   if (type == radv_bvh_node_box32 || type == radv_bvh_node_box16) {
      radv_bvh_box32_node node = radv_get_box_node(acceleration_structure->bvh + offset, type);
      for (uint32_t i = 0; i < 4; i++) {
         if (node.children[i] != RADV_BVH_INVALID_NODE) {
            render_aabb(selection_task, vk_aabb_to_gamma(node.coords[i]));
            selection_task.wait_for_prev = false;
         }
      }
   } else if (type == radv_bvh_node_triangle) {
      const radv_bvh_triangle_node *node = (const radv_bvh_triangle_node *)(acceleration_structure->bvh + offset);
      uint32_t index = acceleration_structure->primitive_counts_exclusive_sum[node->geometry_id_and_flags & 0xfffffff] +
                       node->triangle_id;
      selection_task.first_vertex = acceleration_structure->wireframe_first_vertex + index * 3;
      render(selection_task);
      selection_task.wait_for_prev = false;
   } else if (type == radv_bvh_node_aabb) {
      const radv_bvh_aabb_node *node = (const radv_bvh_aabb_node *)(acceleration_structure->bvh + offset);
      uint32_t index = acceleration_structure->primitive_counts_exclusive_sum[node->geometry_id_and_flags & 0xfffffff] +
                       node->primitive_id;
      selection_task.first_vertex = acceleration_structure->wireframe_first_vertex + index * GAMMA_CUBE_VERTEX_COUNT;
      render(selection_task);
      selection_task.wait_for_prev = false;
   } else {
      const radv_bvh_instance_node *node = (const radv_bvh_instance_node *)(acceleration_structure->bvh + offset);
      selection_task.params.transform = mat3x4_to_gamma(node->otw_matrix);
      render_aabb(selection_task, radv_bvh_instance_node_get_blas(this, node)->aabb);
   }
}

void
gamma_file_view_radv::bvh4_get_instance_info(const gamma_acceleration_structure_radv *acceleration_structure,
                                             uint32_t id, gamma_mat4 *transform,
                                             gamma_acceleration_structure_radv **blas)
{

   uint32_t offset = (id & (~0x7)) << 3;
   const radv_bvh_instance_node *node = (const radv_bvh_instance_node *)(acceleration_structure->bvh + offset);
   *blas = radv_bvh_instance_node_get_blas(this, node);
   *transform = mat3x4_to_gamma(node->wto_matrix);
}
