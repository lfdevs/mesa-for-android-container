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
#include "util/bitset.h"
#include "util/gamma_format.h"
#include "util/macros.h"
#include "util/u_math.h"
#include "vulkan/vulkan_core.h"

#include "bvh_defines.h"
#include "gamma_app.h"
#include "gamma_util.h"

struct radv_bvh8_box_node_iterator {
   const radv_gfx12_box_node *node;
   uint32_t internal_id;
   uint32_t primitive_id;
   int32_t valid_child_count_minus_one;
   int32_t index = -1;

   radv_bvh8_box_node_iterator(const radv_gfx12_box_node *node): node(node)
   {
      internal_id = node->internal_base_id;
      primitive_id = node->primitive_base_id;
      valid_child_count_minus_one = node->child_count_exponents >> 28;
      if (valid_child_count_minus_one == 0xF)
         valid_child_count_minus_one = -1;
   }

   uint32_t next()
   {
      index++;
      if (index > (int32_t)valid_child_count_minus_one)
         return RADV_BVH_INVALID_NODE;

      uint32_t child_type = (node->children[index].dword2 >> 24) & 0xf;
      uint32_t child_size = node->children[index].dword2 >> 28;

      uint32_t child_id;
      if (child_type == radv_bvh_node_box32) {
         child_id = internal_id;
         internal_id += (child_size * RADV_GFX12_BVH_NODE_SIZE) >> 3;
      } else {
         child_id = primitive_id;
         primitive_id += (child_size * RADV_GFX12_BVH_NODE_SIZE) >> 3;
      }

      return child_id | child_type;
   }
};

static gamma_aabb
radv_bvh8_box_child_get_aabb(radv_gfx12_box_child child, vec3 origin, uint32_t child_count_exponents)
{
   gamma_aabb aabb;

   uint32_t exponents[3] = {
      child_count_exponents & 0xff,
      (child_count_exponents >> 8) & 0xff,
      (child_count_exponents >> 16) & 0xff,
   };
   float extent[3] = {
      uif(exponents[0] << 23),
      uif(exponents[1] << 23),
      uif(exponents[2] << 23),
   };

   aabb.min.x = (float)(child.dword0 & 0xfff) / 0x1000 * extent[0] + origin.x;
   aabb.min.y = (float)((child.dword0 >> 12) & 0xfff) / 0x1000 * extent[1] + origin.y;
   aabb.min.z = (float)(child.dword1 & 0xfff) / 0x1000 * extent[2] + origin.z;
   aabb.max.x = (float)(((child.dword1 >> 12) & 0xfff) + 1) / 0x1000 * extent[0] + origin.x;
   aabb.max.y = (float)((child.dword2 & 0xfff) + 1) / 0x1000 * extent[1] + origin.y;
   aabb.max.z = (float)(((child.dword2 >> 12) & 0xfff) + 1) / 0x1000 * extent[2] + origin.z;

   return aabb;
}

struct radv_bvh8_primitive_node_info {
   uint32_t geometry_index_base_bits_div_2;
   uint32_t geometry_index_bits_div_2;
   uint32_t triangle_pair_count_minus_one;
   uint32_t primitive_index_base_bits;
   uint32_t primitive_index_bits;
   uint32_t indices_midpoint;
   uint32_t vertex_bits_minus_one[3];
   uint32_t trailing_zero_bits;
   gamma_triangle triangles[16];

   uint32_t procedural_mask = 0;

   radv_bvh8_primitive_node_info(const BITSET_WORD *node)
   {
      geometry_index_base_bits_div_2 = BITSET_EXTRACT(node, 20, 4);
      geometry_index_bits_div_2 = BITSET_EXTRACT(node, 24, 4);
      triangle_pair_count_minus_one = BITSET_EXTRACT(node, 28, 3);
      primitive_index_base_bits = BITSET_EXTRACT(node, 32, 5);
      primitive_index_bits = BITSET_EXTRACT(node, 37, 5);
      indices_midpoint = BITSET_EXTRACT(node, 42, 10);
      vertex_bits_minus_one[0] = BITSET_EXTRACT(node, 0, 5);
      vertex_bits_minus_one[1] = BITSET_EXTRACT(node, 5, 5);
      vertex_bits_minus_one[2] = BITSET_EXTRACT(node, 10, 5);
      trailing_zero_bits = BITSET_EXTRACT(node, 15, 5);

      uint32_t geometry_id_base = BITSET_EXTRACT(node, indices_midpoint - geometry_index_base_bits_div_2 * 2,
                                                 geometry_index_base_bits_div_2 * 2);
      uint32_t primitive_id_base = BITSET_EXTRACT(node, indices_midpoint, primitive_index_base_bits);

      for (uint32_t i = 0; i < (triangle_pair_count_minus_one + 1) * 2; i++) {
         uint32_t pair_offset = 1024 - (i / 2 + 1) * RADV_GFX12_PRIMITIVE_NODE_PAIR_DESC_SIZE;
         uint32_t vertex_indices_offset = pair_offset + ((i % 2) == 0 ? 17 : 3);

         uint32_t vertex_indices[] = {
            BITSET_EXTRACT(node, vertex_indices_offset + 0, 4),
            BITSET_EXTRACT(node, vertex_indices_offset + 4, 4),
            BITSET_EXTRACT(node, vertex_indices_offset + 8, 4),
         };

         if (vertex_indices[0] == 0 && vertex_indices[1] == 0 && vertex_indices[2] == 0) {
            triangles[i].primitive_index = GAMMA_PRIMITIVE_INDEX_INACTIVE;
            continue;
         }

         bool is_procedural = vertex_indices[0] == 0xf && vertex_indices[1] == 0xf;
         if (is_procedural)
            procedural_mask |= BITFIELD_BIT(i);

         uint32_t payload_size[3] = {
            vertex_bits_minus_one[0] + 1,
            vertex_bits_minus_one[1] + 1,
            vertex_bits_minus_one[2] + 1,
         };
         uint32_t total_payload_size = payload_size[0] + payload_size[1] + payload_size[2];

         uint32_t prefix_size[3] = {
            32 - trailing_zero_bits - payload_size[0],
            32 - trailing_zero_bits - payload_size[1],
            32 - trailing_zero_bits - payload_size[2],
         };
         uint32_t total_prefix_size = prefix_size[0] + prefix_size[1] + prefix_size[2];
         uint32_t prefix[3] = {
            BITSET_EXTRACT(node, RADV_GFX12_PRIMITIVE_NODE_HEADER_SIZE, prefix_size[0]) << (32 - prefix_size[0]),
            BITSET_EXTRACT(node, RADV_GFX12_PRIMITIVE_NODE_HEADER_SIZE + prefix_size[0], prefix_size[1])
               << (32 - prefix_size[1]),
            BITSET_EXTRACT(node, RADV_GFX12_PRIMITIVE_NODE_HEADER_SIZE + prefix_size[0] + prefix_size[1], prefix_size[2])
               << (32 - prefix_size[2]),
         };

         for (uint32_t j = 0; j < (is_procedural ? 0 : 3); j++) {
            uint32_t payload[3] = {
               BITSET_EXTRACT(
                  node,
                  RADV_GFX12_PRIMITIVE_NODE_HEADER_SIZE + total_prefix_size + total_payload_size * vertex_indices[j],
                  payload_size[0])
                  << trailing_zero_bits,
               BITSET_EXTRACT(node,
                              RADV_GFX12_PRIMITIVE_NODE_HEADER_SIZE + total_prefix_size +
                                 total_payload_size * vertex_indices[j] + payload_size[0],
                              payload_size[1])
                  << trailing_zero_bits,
               BITSET_EXTRACT(node,
                              RADV_GFX12_PRIMITIVE_NODE_HEADER_SIZE + total_prefix_size +
                                 total_payload_size * vertex_indices[j] + payload_size[0] + payload_size[1],
                              payload_size[2])
                  << trailing_zero_bits,
            };
            triangles[i].vertices[j].x = uif(prefix[0] + payload[0]);
            triangles[i].vertices[j].y = uif(prefix[1] + payload[1]);
            triangles[i].vertices[j].z = uif(prefix[2] + payload[2]);
         }

         uint32_t geometry_index = geometry_id_base;
         uint32_t primitive_index = primitive_id_base;
         if (i) {
            geometry_index =
               (geometry_id_base & ~BITFIELD64_MASK(geometry_index_bits_div_2 * 2)) |
               BITSET_EXTRACT(node,
                              indices_midpoint - geometry_index_base_bits_div_2 * 2 - geometry_index_bits_div_2 * 2 * i,
                              geometry_index_bits_div_2 * 2);

            primitive_index =
               (primitive_id_base & ~BITFIELD64_MASK(primitive_index_bits)) |
               BITSET_EXTRACT(node, indices_midpoint + primitive_index_base_bits + primitive_index_bits * (i - 1),
                              primitive_index_bits);
         };
         triangles[i].geometry_index = geometry_index;
         triangles[i].primitive_index = primitive_index;
      }
   }
};

static gamma_mat4
radv_bvh8_instance_node_get_transform(const radv_gfx12_instance_node *node)
{
   gamma_mat4 inv_transform = mat3x4_to_gamma(node->wto_matrix);
   gamma_mat4 transform;
   util_invert_mat4x4(transform.elements, inv_transform.elements);
   return transform;
}

static gamma_acceleration_structure_radv *
radv_bvh8_instance_node_get_blas(gamma_file_view *view, const radv_gfx12_instance_node *node)
{
   return (gamma_acceleration_structure_radv *)view->addr_to_acceleration_structure(
      radv_node_to_addr(node->pointer_flags_bvh_addr));
}

void
gamma_file_view_radv::bvh8_traverse(gamma_acceleration_structure_radv *acceleration_structure, gamma_ray ray,
                                    uint32_t id, float *tmax, const std::unordered_set<uint32_t> &path, bool select)
{
   uint32_t offset = (id & (~0xf)) << 3;

   std::unordered_set<uint32_t> path_copy = path;
   if (select)
      path_copy.insert(id);

   const radv_gfx12_box_node *node = (const radv_gfx12_box_node *)(acceleration_structure->bvh + offset);
   radv_bvh8_box_node_iterator iterator = node;
   for (uint32_t child_id = iterator.next(); child_id != RADV_BVH_INVALID_NODE; child_id = iterator.next()) {
      uint32_t child_offset = (child_id & (~0xf)) << 3;
      uint32_t child_type = child_id & 0xf;
      gamma_aabb aabb =
         radv_bvh8_box_child_get_aabb(node->children[iterator.index], node->origin, node->child_count_exponents);
      float t = gamma_aabb::intersect_ray(aabb, ray);
      if (t >= *tmax)
         continue;

      if (child_type == radv_bvh_node_box32) {
         bvh8_traverse(acceleration_structure, ray, child_id, tmax, path_copy, select);
      } else if (child_type != radv_bvh_node_instance) {
         const BITSET_WORD *node = (const BITSET_WORD *)(acceleration_structure->bvh + child_offset);
         radv_bvh8_primitive_node_info info(node);
         for (uint32_t i = 0; i < (info.triangle_pair_count_minus_one + 1) * 2; i++) {
            gamma_triangle triangle = info.triangles[i];
            if (triangle.primitive_index == GAMMA_PRIMITIVE_INDEX_INACTIVE)
               continue;

            if (!(info.procedural_mask & BITFIELD_BIT(i)))
               t = gamma_triangle::intersect_ray(triangle, ray);

            if (t < *tmax) {
               *tmax = t;
               if (select) {
                  acceleration_structure->radv_ui.selected_node_id = child_id;
                  acceleration_structure->radv_ui.open_path = path_copy;
                  acceleration_structure->radv_ui.scroll_to_selected_node = true;
               }
            }
         }
      } else {
         const radv_gfx12_instance_node *instance =
            (const radv_gfx12_instance_node *)(acceleration_structure->bvh + child_offset);
         gamma_ray object_ray = gamma_ray::transform(ray, mat3x4_to_gamma(instance->wto_matrix));
         gamma_acceleration_structure_radv *blas = radv_bvh8_instance_node_get_blas(this, instance);
         float blas_tmax = INFINITY;
         std::unordered_set<uint32_t> blas_path;
         bvh8_traverse(blas, object_ray, RADV_BVH_ROOT_NODE, &blas_tmax, blas_path, false);
         if (blas_tmax < INFINITY) {
            if (select) {
               acceleration_structure->radv_ui.selected_node_id = child_id;
               acceleration_structure->radv_ui.open_path = path_copy;
               acceleration_structure->radv_ui.scroll_to_selected_node = true;
            }
            *tmax = blas_tmax;
         }
      }
   }
}

gamma_aabb
gamma_file_view_radv::bvh8_scene_aabb(gamma_acceleration_structure_radv *acceleration_structure)
{
   gamma_aabb aabb;

   uint32_t root_offset = (RADV_BVH_ROOT_NODE & (~0xf)) << 3;
   uint32_t root_type = RADV_BVH_ROOT_NODE & 0xf;
   assert(root_type == radv_bvh_node_box32);
   const radv_gfx12_box_node *root_node = (const radv_gfx12_box_node *)(acceleration_structure->bvh + root_offset);
   for (uint32_t i = 0; i <= root_node->child_count_exponents >> 28; i++) {
      gamma_aabb child_aabb =
         radv_bvh8_box_child_get_aabb(root_node->children[i], root_node->origin, root_node->child_count_exponents);
      if (i == 0) {
         aabb = child_aabb;
      } else {
         aabb = gamma_aabb::combine(aabb, child_aabb);
      }
   }

   return aabb;
}

void
gamma_file_view_radv::bvh8_get_vertices(gamma_acceleration_structure_radv *acceleration_structure, uint32_t id,
                                        gamma_vertex *vertices, gamma_vertex *wireframe_vertices)
{
   uint32_t offset = (id & (~0xf)) << 3;
   const radv_gfx12_box_node *box_node = (const radv_gfx12_box_node *)(acceleration_structure->bvh + offset);
   radv_bvh8_box_node_iterator iterator = box_node;
   for (uint32_t child_id = iterator.next(); child_id != RADV_BVH_INVALID_NODE; child_id = iterator.next()) {
      uint32_t child_offset = (child_id & (~0xf)) << 3;
      uint32_t child_type = child_id & 0xf;
      if (child_type == radv_bvh_node_box32) {
         bvh8_get_vertices(acceleration_structure, child_id, vertices, wireframe_vertices);
      } else if (child_type != radv_bvh_node_instance) {
         const BITSET_WORD *node = (const BITSET_WORD *)(acceleration_structure->bvh + child_offset);
         radv_bvh8_primitive_node_info info(node);
         for (uint32_t i = 0; i < (info.triangle_pair_count_minus_one + 1) * 2; i++) {
            gamma_triangle triangle = info.triangles[i];
            if (triangle.primitive_index == GAMMA_PRIMITIVE_INDEX_INACTIVE)
               continue;

            uint32_t dst_index = acceleration_structure->primitive_counts_exclusive_sum[triangle.geometry_index] +
                                 triangle.primitive_index;

            if (info.procedural_mask & BITFIELD_BIT(i)) {
               gamma_aabb aabb = radv_bvh8_box_child_get_aabb(box_node->children[iterator.index], box_node->origin,
                                                              box_node->child_count_exponents);
               gamma_generate_cube_vertices(wireframe_vertices + dst_index * GAMMA_CUBE_VERTEX_COUNT, aabb);
               gamma_generate_filled_cube_vertices(vertices + dst_index * GAMMA_FILLED_CUBE_VERTEX_COUNT, aabb,
                                                   triangle.geometry_index, triangle.primitive_index);
               continue;
            }

            vertices[dst_index * 3 + 0].position = triangle.vertices[0];
            vertices[dst_index * 3 + 0].geometry_index = triangle.geometry_index;
            vertices[dst_index * 3 + 0].primitive_index = triangle.primitive_index;

            vertices[dst_index * 3 + 1].position = triangle.vertices[1];
            vertices[dst_index * 3 + 1].geometry_index = triangle.geometry_index;
            vertices[dst_index * 3 + 1].primitive_index = triangle.primitive_index;

            vertices[dst_index * 3 + 2].position = triangle.vertices[2];
            vertices[dst_index * 3 + 2].geometry_index = triangle.geometry_index;
            vertices[dst_index * 3 + 2].primitive_index = triangle.primitive_index;
         }
      }
   }
}

void
gamma_file_view_radv::bvh8_get_instances(gamma_acceleration_structure_radv *tlas, uint32_t id)
{
   uint32_t offset = (id & (~0xf)) << 3;
   uint32_t type = id & 0xf;

   if (type == radv_bvh_node_box32) {
      const radv_gfx12_box_node *node = (const radv_gfx12_box_node *)(tlas->bvh + offset);
      radv_bvh8_box_node_iterator iterator = node;
      for (uint32_t child_id = iterator.next(); child_id != RADV_BVH_INVALID_NODE; child_id = iterator.next())
         bvh8_get_instances(tlas, child_id);
   } else if (type == radv_bvh_node_instance) {
      const radv_gfx12_instance_node *node = (const radv_gfx12_instance_node *)(tlas->bvh + offset);
      const radv_gfx12_instance_node_user_data *user_data = (const radv_gfx12_instance_node_user_data *)(node + 1);
      gamma_acceleration_structure_radv *blas = radv_bvh8_instance_node_get_blas(this, node);

      uint32_t primitive_vertex_count =
         blas->header.geometry_type == VK_GEOMETRY_TYPE_TRIANGLES_KHR ? 3 : GAMMA_FILLED_CUBE_VERTEX_COUNT;

      gamma_render_task solid_task;
      solid_task.vertex_buffer = blas->ui.vertex_buffer.get();
      solid_task.first_vertex = blas->first_vertex;
      solid_task.vertex_count = blas->primitive_count * primitive_vertex_count;
      solid_task.params.transform = radv_bvh8_instance_node_get_transform(node);
      solid_task.params.color = gamma_index_to_color(user_data->instance_index);
      solid_task.flags = GAMMA_RENDERER_COLOR_PUSH_CONSTANT;
      tlas->ui.instance_render_list->tasks.push_back(solid_task);
   }
}

static const char *node_type_names[16] = {
   "triangle0", "triangle1", "triangle2", "triangle3", "invalid4",  "box",       "instance",  "invalid7",
   "triangle4", "triangle5", "triangle6", "triangle7", "invalid12", "invalid13", "invalid14", "invalid15",
};

void
gamma_file_view_radv::bvh8_draw(gamma_acceleration_structure_radv *acceleration_structure, uint32_t id)
{
   uint32_t offset = (id & (~0xf)) << 3;
   uint32_t type = id & 0xf;

   bool selected = acceleration_structure->radv_ui.selected_node_id == id;

   ImGuiTreeNodeFlags node_flags =
      ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
   if (type != radv_bvh_node_box32)
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
      if (type == radv_bvh_node_box32) {
         const radv_gfx12_box_node *node = (const radv_gfx12_box_node *)(acceleration_structure->bvh + offset);
         radv_bvh8_box_node_iterator iterator = node;
         for (uint32_t child_id = iterator.next(); child_id != RADV_BVH_INVALID_NODE; child_id = iterator.next())
            bvh8_draw(acceleration_structure, child_id);
      }
      ImGui::TreePop();
   }

   if (acceleration_structure->radv_ui.scroll_to_selected_node && selected) {
      ImGui::SetScrollHereY();
      acceleration_structure->radv_ui.scroll_to_selected_node = false;
   }
}

static void
gamma_amd_draw_box8_child_info(const char *table_id, radv_gfx12_box_child child, vec3 origin,
                               uint32_t child_count_exponents)
{
   if (ImGui::BeginTable(table_id, 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
      ImGui::TableNextColumnText("cull_flags");
      ImGui::TableNextColumnText("0x%x", child.dword0 >> 24);

      ImGui::TableNextColumnText("cull_mask");
      ImGui::TableNextColumnText("0x%x", child.dword1 >> 24);

      ImGui::TableNextColumnText("type");
      ImGui::TableNextColumnText("%s", node_type_names[(child.dword2 >> 24) & 0xF]);

      ImGui::TableNextColumnText("stride_128b");
      ImGui::TableNextColumnText("0x%x", child.dword1 >> 28);

      struct gamma_aabb aabb = radv_bvh8_box_child_get_aabb(child, origin, child_count_exponents);

      ImGui::TableNextColumn();
      bool min_open = ImGui::TreeNodeEx("min");
      ImGui::TableNextColumnText("(0x%x, 0x%x, 0x%x)", (child.dword0 >> 0) & 0xfff, (child.dword0 >> 12) & 0xfff,
                                 (child.dword1 >> 0) & 0xfff);
      if (min_open) {
         ImGui::TableNextColumn();
         ImGui::TableNextColumnText("(%f, %f, %f)", aabb.min.x, aabb.min.y, aabb.min.z);
         ImGui::TreePop();
      }

      ImGui::TableNextColumn();
      bool max_open = ImGui::TreeNodeEx("max");
      ImGui::TableNextColumnText("(0x%x, 0x%x, 0x%x)", (child.dword1 >> 12) & 0xfff, (child.dword2 >> 0) & 0xfff,
                                 (child.dword2 >> 12) & 0xfff);
      if (max_open) {
         ImGui::TableNextColumn();
         ImGui::TableNextColumnText("(%f, %f, %f)", aabb.max.x, aabb.max.y, aabb.max.z);
         ImGui::TreePop();
      }

      ImGui::EndTable();
   }
}

void
gamma_file_view_radv::bvh8_draw_node_info(const gamma_acceleration_structure_radv *acceleration_structure, uint32_t id)
{

   uint32_t offset = (acceleration_structure->radv_ui.selected_node_id & (~0xf)) << 3;
   uint32_t type = acceleration_structure->radv_ui.selected_node_id & 0xf;

   if (type == radv_bvh_node_box32) {
      const radv_gfx12_box_node *node = (const radv_gfx12_box_node *)(acceleration_structure->bvh + offset);

      char tmp[64];
      sprintf(tmp, "box8 node (0x%x)", offset);

      ImGui::SeparatorText(tmp);

      if (ImGui::BeginTable("box8 node properties", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
         ImGui::TableNextColumnText("internal_base_id");
         ImGui::TableNextColumnText("0x%x", node->internal_base_id);

         ImGui::TableNextColumnText("primitive_base_id");
         ImGui::TableNextColumnText("0x%x", node->primitive_base_id);

         ImGui::TableNextColumnText("origin");
         ImGui::TableNextColumnText("(%f, %f, %f)", node->origin.x, node->origin.y, node->origin.z);

         ImGui::TableNextColumnText("child_count - 1");
         ImGui::TableNextColumnText("%u", node->child_count_exponents >> 28);

         ImGui::TableNextColumnText("exponents");
         ImGui::TableNextColumnText("(0x%x, 0x%x, 0x%x)", (node->child_count_exponents >> 0) & 0xFF,
                                    (node->child_count_exponents >> 8) & 0xFF,
                                    (node->child_count_exponents >> 16) & 0xFF);

         ImGui::TableNextColumnText("obb_matrix_index");
         ImGui::TableNextColumnText("0x%x", node->obb_matrix_index);

         ImGui::EndTable();
      }

      for (uint32_t i = 0; i <= node->child_count_exponents >> 28; i++) {
         sprintf(tmp, "child[%u]", i);
         ImGui::SeparatorText(tmp);

         sprintf(tmp, "child[%u] properties", i);
         gamma_amd_draw_box8_child_info(tmp, node->children[i], node->origin, node->child_count_exponents);
      }
   } else if (type == radv_bvh_node_instance) {
      const radv_gfx12_instance_node *node = (const radv_gfx12_instance_node *)(acceleration_structure->bvh + offset);

      char tmp[64];
      sprintf(tmp, "instance node (0x%x)", offset);

      ImGui::SeparatorText(tmp);

      if (ImGui::BeginTable("instance node properties", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
         for (uint32_t i = 0; i < 3; i++) {
            ImGui::TableNextColumnText("wto_matrix.row[%u]", i);
            ImGui::TableNextColumnText("(%f, %f, %f, %f)", node->wto_matrix.values[i][0], node->wto_matrix.values[i][1],
                                       node->wto_matrix.values[i][2], node->wto_matrix.values[i][3]);
         }

         ImGui::TableNextColumnText("pointer_flags_bvh_addr");
         ImGui::TableNextColumn();
         sprintf(tmp, "0x%" PRIx64, node->pointer_flags_bvh_addr);
         if (ImGui::TextLink(tmp)) {
            gamma_acceleration_structure_radv *blas = radv_bvh8_instance_node_get_blas(this, node);
            if (blas->ui.opened)
               blas->ui.request_focus = true;
            blas->ui.opened = true;
         }

         ImGui::TableNextColumnText("pointer_flags");
         ImGui::TableNextColumnText(
            "%s%s%s%s%s%s", (node->pointer_flags_bvh_addr & RADV_BLAS_POINTER_FORCE_OPAQUE) ? "force_opaque " : "",
            (node->pointer_flags_bvh_addr & RADV_BLAS_POINTER_FORCE_NON_OPAQUE) ? "force_non_opaque " : "",
            (node->pointer_flags_bvh_addr & RADV_BLAS_POINTER_DISABLE_TRI_CULL) ? "disable_tri_cull " : "",
            (node->pointer_flags_bvh_addr & RADV_BLAS_POINTER_FLIP_FACING) ? "flip_facing " : "",
            (node->pointer_flags_bvh_addr & RADV_BLAS_POINTER_SKIP_TRIANGLES) ? "skip_triangles " : "",
            (node->pointer_flags_bvh_addr & RADV_BLAS_POINTER_SKIP_AABBS) ? "skip_aabbs " : "");

         ImGui::TableNextColumnText("cull_mask");
         ImGui::TableNextColumnText("0x%x", node->cull_mask_user_data >> 24);

         ImGui::TableNextColumnText("user_data");
         ImGui::TableNextColumnText("0x%x", node->cull_mask_user_data & 0xFFFFFF);

         ImGui::TableNextColumnText("origin");
         ImGui::TableNextColumnText("(%f, %f, %f)", node->origin.x, node->origin.y, node->origin.z);

         ImGui::TableNextColumnText("child_count - 1");
         ImGui::TableNextColumnText("%u", node->child_count_exponents >> 28);

         ImGui::TableNextColumnText("exponents");
         ImGui::TableNextColumnText("(0x%x, 0x%x, 0x%x)", (node->child_count_exponents >> 0) & 0xFF,
                                    (node->child_count_exponents >> 8) & 0xFF,
                                    (node->child_count_exponents >> 16) & 0xFF);

         ImGui::EndTable();
      }

      for (uint32_t i = 0; i <= node->child_count_exponents >> 28; i++) {
         sprintf(tmp, "child[%u]", i);
         ImGui::SeparatorText(tmp);

         sprintf(tmp, "child[%u] properties", i);
         gamma_amd_draw_box8_child_info(tmp, node->children[i], node->origin, node->child_count_exponents);
      }
   } else {
      const BITSET_WORD *node = (const BITSET_WORD *)(acceleration_structure->bvh + offset);
      radv_bvh8_primitive_node_info info(node);

      char tmp[64];
      sprintf(tmp, "primitive node (0x%x)", offset);

      ImGui::SeparatorText(tmp);

      if (ImGui::BeginTable("primitive node properties", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
         ImGui::TableNextColumnText("vertex_bits - 1");
         ImGui::TableNextColumnText("(%u, %u, %u)", info.vertex_bits_minus_one[0], info.vertex_bits_minus_one[1],
                                    info.vertex_bits_minus_one[2]);

         ImGui::TableNextColumnText("trailing_zero_bits");
         ImGui::TableNextColumnText("%u", info.trailing_zero_bits);

         ImGui::TableNextColumnText("geometry_index_base_bits / 2");
         ImGui::TableNextColumnText("%u", info.geometry_index_base_bits_div_2);

         ImGui::TableNextColumnText("geometry_index_bits / 2");
         ImGui::TableNextColumnText("%u", info.geometry_index_bits_div_2);

         ImGui::TableNextColumnText("triangle_pair_count - 1");
         ImGui::TableNextColumnText("%u", info.triangle_pair_count_minus_one);

         ImGui::TableNextColumnText("primitive_index_base_bits");
         ImGui::TableNextColumnText("%u", info.primitive_index_base_bits);

         ImGui::TableNextColumnText("primitive_index_bits");
         ImGui::TableNextColumnText("%u", info.primitive_index_bits);

         ImGui::EndTable();
      }

      for (uint32_t i = 0; i <= info.triangle_pair_count_minus_one; i++) {
         sprintf(tmp, "pair_desc[%u]", i);
         ImGui::SeparatorText(tmp);

         uint32_t pair_offset = 1024 - (i + 1) * RADV_GFX12_PRIMITIVE_NODE_PAIR_DESC_SIZE;

         sprintf(tmp, "pair_desc[%u] properties", i);
         if (ImGui::BeginTable(tmp, 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableNextColumnText("flags");
            ImGui::TableNextColumnText("%s%s%s%s%s", BITSET_EXTRACT(node, pair_offset + 0, 1) ? "prim_range_stop " : "",
                                       BITSET_EXTRACT(node, pair_offset + 15, 1) ? "tri0_double_sided " : "",
                                       BITSET_EXTRACT(node, pair_offset + 16, 1) ? "tri0_opaque " : "",
                                       BITSET_EXTRACT(node, pair_offset + 1, 1) ? "tri1_double_sided " : "",
                                       BITSET_EXTRACT(node, pair_offset + 2, 1) ? "tri1_opaque " : "");

            for (uint32_t j = 0; j < 2; j++) {
               uint32_t triangle_index = i * 2 + j;
               uint32_t vertex_indices_offset = pair_offset + (triangle_index == 0 ? 17 : 3);

               sprintf(tmp, "tri%u vertices", j);
               ImGui::TableNextColumn();
               bool vertices_open = ImGui::TreeNodeEx(tmp);
               uint32_t vertex_indices[] = {
                  BITSET_EXTRACT(node, vertex_indices_offset + 0, 4),
                  BITSET_EXTRACT(node, vertex_indices_offset + 4, 4),
                  BITSET_EXTRACT(node, vertex_indices_offset + 8, 4),
               };
               ImGui::TableNextColumnText("(%u, %u, %u)", vertex_indices[0], vertex_indices[1], vertex_indices[2]);
               if (vertices_open) {
                  for (uint32_t k = 0; k < 3; k++) {
                     ImGui::TableNextColumn();
                     ImGui::TableNextColumnText("(%f, %f, %f)", info.triangles[triangle_index].vertices[k].x,
                                                info.triangles[triangle_index].vertices[k].y,
                                                info.triangles[triangle_index].vertices[k].z);
                  }
                  ImGui::TreePop();
               }

               ImGui::TableNextColumnText("tri%u geometry_index", j);
               ImGui::TableNextColumnText("0x%x", info.triangles[triangle_index].geometry_index);

               ImGui::TableNextColumnText("tri%u primitive_index", j);
               ImGui::TableNextColumnText("0x%x", info.triangles[triangle_index].primitive_index);
            }

            ImGui::EndTable();
         }
      }
   }
}

void
gamma_file_view_radv::bvh8_render_selection(gamma_acceleration_structure_radv *acceleration_structure)
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

   uint32_t offset = (acceleration_structure->radv_ui.selected_node_id & (~0xf)) << 3;
   uint32_t type = acceleration_structure->radv_ui.selected_node_id & 0xf;

   if (type == radv_bvh_node_box32) {
      const radv_gfx12_box_node *node = (const radv_gfx12_box_node *)(acceleration_structure->bvh + offset);
      radv_bvh8_box_node_iterator iterator = node;
      for (uint32_t child_id = iterator.next(); child_id != RADV_BVH_INVALID_NODE; child_id = iterator.next()) {
         render_aabb(selection_task, radv_bvh8_box_child_get_aabb(node->children[iterator.index], node->origin,
                                                                  node->child_count_exponents));
         selection_task.wait_for_prev = false;
      }
   } else if (type != radv_bvh_node_instance) {
      const BITSET_WORD *node = (const BITSET_WORD *)(acceleration_structure->bvh + offset);
      radv_bvh8_primitive_node_info info(node);
      for (uint32_t i = 0; i < (info.triangle_pair_count_minus_one + 1) * 2; i++) {
         gamma_triangle triangle = info.triangles[i];
         if (triangle.primitive_index == GAMMA_PRIMITIVE_INDEX_INACTIVE)
            continue;

         uint32_t index =
            acceleration_structure->primitive_counts_exclusive_sum[triangle.geometry_index] + triangle.primitive_index;
         if (info.procedural_mask & BITFIELD_BIT(i))
            selection_task.first_vertex = index * GAMMA_CUBE_VERTEX_COUNT;
         else
            selection_task.first_vertex = index * 3;

         selection_task.first_vertex += acceleration_structure->wireframe_first_vertex;

         render(selection_task);
         selection_task.wait_for_prev = false;
      }
   } else {
      const radv_gfx12_instance_node *node = (const radv_gfx12_instance_node *)(acceleration_structure->bvh + offset);
      selection_task.params.transform = radv_bvh8_instance_node_get_transform(node);
      render_aabb(selection_task, radv_bvh8_instance_node_get_blas(this, node)->aabb);
   }
}

void
gamma_file_view_radv::bvh8_get_instance_info(const gamma_acceleration_structure_radv *acceleration_structure,
                                             uint32_t id, gamma_mat4 *transform,
                                             gamma_acceleration_structure_radv **blas)
{

   uint32_t offset = (id & (~0xf)) << 3;
   const radv_gfx12_instance_node *node = (const radv_gfx12_instance_node *)(acceleration_structure->bvh + offset);
   *blas = radv_bvh8_instance_node_get_blas(this, node);
   *transform = mat3x4_to_gamma(node->wto_matrix);
}
