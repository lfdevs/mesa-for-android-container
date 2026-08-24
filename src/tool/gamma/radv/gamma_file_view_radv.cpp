/*
 * Copyright © 2026 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "gamma_file_view_radv.h"
#include "gamma_file_view.h"

#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <imgui.h>
#include <memory>
#include <stdint.h>

#include "imgui_internal.h"

#include "shaders/gamma_shader_interface.h"
#include "util/gamma_format.h"
#include "util/macros.h"
#include "vulkan/vulkan_core.h"

#include "compiler/spirv/spirv.h"

#include "bvh_defines.h"
#include "gamma_app.h"
#include "gamma_util.h"
#include "radv_gamma.h"

std::unique_ptr<gamma_acceleration_structure>
gamma_file_view_radv::create_acceleration_structure()
{
   return std::make_unique<gamma_acceleration_structure_radv>(this);
}

float
gamma_file_view_radv::handle_mouse_click(gamma_acceleration_structure *_acceleration_structure, gamma_ray ray,
                                         bool select)
{
   gamma_acceleration_structure_radv *acceleration_structure =
      (gamma_acceleration_structure_radv *)_acceleration_structure;

   float tmax = INFINITY;

   if (select)
      acceleration_structure->radv_ui.selected_node_id = RADV_BVH_INVALID_NODE;

   std::unordered_set<uint32_t> path;
   if (trace_info->bvh8)
      bvh8_traverse(acceleration_structure, ray, RADV_BVH_ROOT_NODE, &tmax, path, select);
   else
      bvh4_traverse(acceleration_structure, ray, RADV_BVH_ROOT_NODE, &tmax, path, select);

   return tmax;
}

void
gamma_file_view_radv::load(FILE *file, const gamma_header *header)
{
   gamma_file_view::load(file, header);

   uint32_t vertex_count = 0;
   for (uint32_t i = 0; i < acceleration_structures.size(); i++) {
      gamma_acceleration_structure_radv *acceleration_structure =
         (gamma_acceleration_structure_radv *)acceleration_structures[i].get();

      uint32_t primitive_vertex_count = acceleration_structure->header.geometry_type == VK_GEOMETRY_TYPE_TRIANGLES_KHR
                                           ? 3
                                           : GAMMA_FILLED_CUBE_VERTEX_COUNT;

      if (acceleration_structure->header.geometry_type != VK_GEOMETRY_TYPE_INSTANCES_KHR) {
         acceleration_structure->first_vertex = vertex_count;
         acceleration_structure->wireframe_first_vertex = vertex_count;
         vertex_count += primitive_vertex_count * acceleration_structure->primitive_count;

         if (acceleration_structure->header.geometry_type == VK_GEOMETRY_TYPE_AABBS_KHR) {
            acceleration_structure->wireframe_first_vertex = vertex_count;
            vertex_count += GAMMA_CUBE_VERTEX_COUNT * acceleration_structure->primitive_count;
         }
      }
   }

   gamma_vertex *vertices = nullptr;
   std::shared_ptr<gamma_backed_buffer> vertex_buffer = nullptr;
   if (vertex_count) {
      vertex_buffer =
         gamma_create_backed_buffer(app, vertex_count * sizeof(gamma_vertex), gamma_memory_type_device_local,
                                    VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, false);
      vertices = (gamma_vertex *)app->upload_memory(vertex_buffer);
   }

   for (uint32_t i = 0; i < acceleration_structures.size(); i++) {
      gamma_acceleration_structure_radv *acceleration_structure =
         (gamma_acceleration_structure_radv *)acceleration_structures[i].get();

      acceleration_structure->bvh = (const uint8_t *)acceleration_structure->data;

      if (trace_info->bvh8)
         acceleration_structure->aabb = bvh8_scene_aabb(acceleration_structure);
      else
         acceleration_structure->aabb = bvh4_scene_aabb(acceleration_structure);

      acceleration_structure->init_camera();

      if (acceleration_structure->header.geometry_type != VK_GEOMETRY_TYPE_INSTANCES_KHR &&
          acceleration_structure->primitive_count) {
         acceleration_structure->ui.vertex_buffer = vertex_buffer;

         if (trace_info->bvh8) {
            bvh8_get_vertices(acceleration_structure, RADV_BVH_ROOT_NODE,
                              vertices + acceleration_structure->first_vertex,
                              vertices + acceleration_structure->wireframe_first_vertex);
         } else {
            bvh4_get_vertices(acceleration_structure, RADV_BVH_ROOT_NODE,
                              vertices + acceleration_structure->first_vertex,
                              vertices + acceleration_structure->wireframe_first_vertex);
         }
      }
   }

   app->flush_upload_memory();

   bool is_first_tlas = true;
   for (uint32_t i = 0; i < acceleration_structures.size(); i++) {
      gamma_acceleration_structure_radv *acceleration_structure =
         (gamma_acceleration_structure_radv *)acceleration_structures[i].get();

      if (acceleration_structure->header.geometry_type != VK_GEOMETRY_TYPE_INSTANCES_KHR)
         continue;

      if (is_first_tlas) {
         acceleration_structure->ui.opened = true;
         acceleration_structure->ui.request_focus = true;
         is_first_tlas = false;
      }

      acceleration_structure->ui.instance_render_list = std::make_shared<gamma_render_list>(app);

      if (trace_info->bvh8)
         bvh8_get_instances(acceleration_structure, RADV_BVH_ROOT_NODE);

      acceleration_structure->ui.instance_render_list->build();
   }

   for (uint32_t i = 0; i < dispatch_count; i++) {
      auto dispatch_view = std::make_shared<gamma_dispatch_view>();

      dispatch_view->ui.settings.width = dispatch_infos[i].dimensions[0];
      dispatch_view->ui.settings.height = dispatch_infos[i].dimensions[1];
      dispatch_view->ui.settings.depth = dispatch_infos[i].dimensions[2];
      dispatch_view->info = dispatch_infos + i;
      dispatch_view->invocation_count =
         dispatch_infos[i].dimensions[0] * dispatch_infos[i].dimensions[1] * dispatch_infos[i].dimensions[2];

      dispatch_view->invocations.resize(dispatch_view->invocation_count);

      /* Stupid but fast enough */
      for (uint32_t j = 0; j < dispatch_view->invocation_count; j++) {
         if (dispatch_view->invocation_count % (j + 1))
            continue;

         std::map<uint32_t, uint32_t> sizes;

         uint32_t div = dispatch_view->invocation_count / (j + 1);
         for (uint32_t k = 0; k < div; k++) {
            if ((div % (k + 1)) == 0)
               sizes[k + 1] = div / (k + 1);
         }

         dispatch_view->valid_dispatch_sizes[j + 1] = sizes;
      }

      dispatch_views.push_back(dispatch_view);
   }

   uint64_t data_size =
      ray_history_data_size - sizeof(radv_gamma_ray_history_header) - sizeof(radv_gamma_dispatch_info) * dispatch_count;
   for (uint32_t offset = 0; offset < data_size;) {
      const radv_packed_token_header *header = (const radv_packed_token_header *)(history_data + offset);
      if (header->token_type == radv_packed_token_trace_ray) {
         const radv_packed_trace_ray_token *token = (const radv_packed_trace_ray_token *)(history_data + offset);
         dispatch_views[token->dispatch_index]->invocations[token->header.launch_index].offsets.push_back(offset);
         offset += sizeof(radv_packed_trace_ray_token);
      } else if (header->token_type == radv_packed_token_iteration) {
         const radv_packed_iteration_token *token = (const radv_packed_iteration_token *)(history_data + offset);
         dispatch_views[token->dispatch_index]->invocations[token->header.launch_index].offsets.push_back(offset);

         gamma_invocation_data *invocation =
            &dispatch_views[token->dispatch_index]->invocations[token->header.launch_index];

         uint32_t node_type = token->node_id & 0xf;
         if (node_type == radv_bvh_node_box32)
            invocation->box_iteration_count++;
         else if (node_type == radv_bvh_node_instance)
            invocation->instance_iteration_count++;
         else
            invocation->primitive_iteration_count++;

         offset += sizeof(radv_packed_iteration_token);
      } else if (header->token_type == radv_packed_token_accel_struct) {
         const radv_packed_accel_struct_token *token = (const radv_packed_accel_struct_token *)(history_data + offset);
         dispatch_views[token->dispatch_index]->invocations[token->header.launch_index].offsets.push_back(offset);
         offset += sizeof(radv_packed_accel_struct_token);
      } else if (header->token_type == radv_packed_token_trace_ray_hit) {
         offset += sizeof(radv_packed_trace_ray_end_token);
      } else if (header->token_type == radv_packed_token_trace_ray_miss) {
         offset += offsetof(radv_packed_trace_ray_end_token, primitive_id);
      }
   }

   for (uint32_t i = 0; i < dispatch_count; i++) {
      gamma_dispatch_view *dispatch_view = dispatch_views[i].get();
      for (uint32_t j = 0; j < dispatch_view->invocation_count; j++) {
         gamma_invocation_data *invocation = &dispatch_views[i]->invocations[j];
         uint32_t iteration_count = invocation->box_iteration_count + invocation->instance_iteration_count +
                                    invocation->primitive_iteration_count;
         dispatch_view->max_iteration_count = MAX2(dispatch_view->max_iteration_count, iteration_count);
      }
      dispatch_view->ui.settings.max_iteration_count = dispatch_view->max_iteration_count;
   }

   app->finish_upload_memory();
}

static const char *node_type_names[16] = {
   "triangle0", "triangle1", "triangle2", "triangle3", "invalid4",  "box",       "instance",  "invalid7",
   "triangle4", "triangle5", "triangle6", "triangle7", "invalid12", "invalid13", "invalid14", "invalid15",
};

void
gamma_file_view_radv::load_driver_specific(FILE *file, const gamma_chunk_header *chunk_header,
                                           const gamma_header *header)
{
   if (chunk_header->type == gamma_chunk_type_trace_info_radv) {
      assert(chunk_header->size == sizeof(radv_gamma_trace_info));
      trace_info = (radv_gamma_trace_info *)malloc(sizeof(radv_gamma_trace_info));
      fread(trace_info, sizeof(radv_gamma_trace_info), 1, file);
      return;
   }

   ray_history_data = malloc(chunk_header->size);
   fread(ray_history_data, chunk_header->size, 1, file);
   ray_history_data_size = chunk_header->size;

   history_header = (const radv_gamma_ray_history_header *)ray_history_data;
   dispatch_infos = (const radv_gamma_dispatch_info *)(history_header + 1);
   history_data = (const uint8_t *)(dispatch_infos + history_header->dispatch_count);
   dispatch_count = history_header->dispatch_count;
}

void
gamma_file_view_radv::dock_driver_specific(uint32_t left_top, uint32_t left_bottom, uint32_t center, uint32_t right_top,
                                           uint32_t right_bottom)
{
   ImGui::DockBuilderDockWindow("dispatches", right_top);

   for (uint32_t i = 0; i < dispatch_count; i++) {
      char tmp[32];
      sprintf(tmp, "dispatch[%u]", i);
      ImGui::DockBuilderDockWindow(tmp, center);
   }

   ImGui::DockBuilderDockWindow("dispatch", right_bottom);
   ImGui::DockBuilderDockWindow("ray history", left_top);
   ImGui::DockBuilderDockWindow("invocation", left_bottom);
}

gamma_invocation_data *
gamma_file_view_radv::get_selected_invocation()
{

   if (selected_dispatch && selected_dispatch->ui.selection_x != UINT32_MAX) {
      uint32_t selection_index = selected_dispatch->ui.selection_x +
                                 selected_dispatch->ui.selection_y * selected_dispatch->ui.settings.width +
                                 selected_dispatch->ui.settings.z * selected_dispatch->ui.settings.width *
                                    selected_dispatch->ui.settings.height;
      return &selected_dispatch->invocations[selection_index];
   }
   return nullptr;
}

void
gamma_file_view_radv::run()
{
   begin();

   gamma_invocation_data *selected_invocation = get_selected_invocation();

   for (auto &_acceleration_structure : acceleration_structures) {
      gamma_acceleration_structure_radv *acceleration_structure =
         (gamma_acceleration_structure_radv *)_acceleration_structure.get();

      if (begin_viewport(acceleration_structure)) {
         uint32_t primitive_vertex_count =
            acceleration_structure->header.geometry_type == VK_GEOMETRY_TYPE_TRIANGLES_KHR
               ? 3
               : GAMMA_FILLED_CUBE_VERTEX_COUNT;

         if (acceleration_structure->ui.vertex_buffer) {
            gamma_render_task solid_task;
            solid_task.vertex_buffer = acceleration_structure->ui.vertex_buffer.get();
            solid_task.first_vertex = acceleration_structure->first_vertex;
            solid_task.vertex_count = acceleration_structure->primitive_count * primitive_vertex_count;
            solid_task.flags =
               gamma_visualization_color_to_renderer_color(acceleration_structure->ui.visualization_color);
            render(solid_task);

            if (acceleration_structure->wireframe_first_vertex != acceleration_structure->first_vertex) {
               gamma_render_task wireframe_task;
               wireframe_task.type = gamma_render_task_type_lines;
               wireframe_task.vertex_buffer = acceleration_structure->ui.vertex_buffer.get();
               wireframe_task.first_vertex = acceleration_structure->wireframe_first_vertex;
               wireframe_task.vertex_count = acceleration_structure->primitive_count * GAMMA_CUBE_VERTEX_COUNT;
               wireframe_task.flags = GAMMA_RENDERER_COLOR_PUSH_CONSTANT;
               wireframe_task.wait_for_prev = true;
               render(wireframe_task);
            } else {
               gamma_render_task wireframe_task = solid_task;
               wireframe_task.type = gamma_render_task_type_wireframe;
               wireframe_task.flags = GAMMA_RENDERER_COLOR_PUSH_CONSTANT;
               wireframe_task.wait_for_prev = true;
               render(wireframe_task);
            }
         } else {
            gamma_render_task tlas_task;
            tlas_task.type = gamma_render_task_type_render_list;
            tlas_task.render_list = acceleration_structure->ui.instance_render_list.get();
            tlas_task.flags =
               gamma_visualization_color_to_renderer_color(acceleration_structure->ui.visualization_color);
            tlas_task.override_flags = true;
            render(tlas_task);
         }

         gamma_render_task scene_aabb_task;
         scene_aabb_task.flags = GAMMA_RENDERER_COLOR_PUSH_CONSTANT;
         render_aabb(scene_aabb_task, acceleration_structure->aabb);

         if (acceleration_structure->radv_ui.selected_node_id != RADV_BVH_INVALID_NODE) {
            if (trace_info->bvh8)
               bvh8_render_selection(acceleration_structure);
            else
               bvh4_render_selection(acceleration_structure);
         }

         render_viewport();

         if (selected_invocation && !selected_invocation->offsets.empty()) {
            const radv_packed_trace_ray_token *trace_ray =
               (const radv_packed_trace_ray_token *)(history_data +
                                                     selected_invocation->offsets[selected_invocation->selected_ray]);

            uint64_t accel_struct_addr = ((uint64_t)trace_ray->accel_struct_hi << 32) | trace_ray->accel_struct_lo;
            gamma_acceleration_structure_radv *trace_accel_struct =
               (gamma_acceleration_structure_radv *)addr_to_acceleration_structure(accel_struct_addr);

            bool draw_ray = acceleration_structure == trace_accel_struct;
            gamma_mat4 transform;

            for (uint32_t i = selected_invocation->selected_ray + 1; i < selected_invocation->offsets.size(); i++) {
               const radv_packed_token_header *header =
                  (const radv_packed_token_header *)(history_data + selected_invocation->offsets[i]);
               if (header->token_type == radv_packed_token_trace_ray)
                  break;

               if (header->token_type == radv_packed_token_iteration) {
                  const radv_packed_iteration_token *token = (const radv_packed_iteration_token *)header;

                  uint32_t type = token->node_id & 0xf;

                  if (type == radv_bvh_node_instance) {
                     gamma_mat4 instance_transform;
                     gamma_acceleration_structure_radv *blas = nullptr;
                     if (trace_info->bvh8)
                        bvh8_get_instance_info(trace_accel_struct, token->node_id, &instance_transform, &blas);
                     else
                        bvh4_get_instance_info(trace_accel_struct, token->node_id, &instance_transform, &blas);
                     if (blas == acceleration_structure) {
                        draw_ray = true;
                        transform = instance_transform;
                        break;
                     }
                  }
               }
            }

            if (draw_ray) {
               gamma_ray ray = {
                  .origin = {trace_ray->origin[0], trace_ray->origin[1], trace_ray->origin[2]},
                  .direction = {trace_ray->direction[0], trace_ray->direction[1], trace_ray->direction[2]},
               };
               ray = gamma_ray::transform(ray, transform);
               draw_point(ray.origin, app->selection_color, 4);
               draw_line(ray.origin,
                         {trace_ray->origin[0] + trace_ray->direction[0] * trace_ray->tmax,
                          trace_ray->origin[1] + trace_ray->direction[1] * trace_ray->tmax,
                          trace_ray->origin[2] + trace_ray->direction[2] * trace_ray->tmax},
                         app->selection_color, 1);
            }
         }
      }
      end_viewport();
   }

   gamma_acceleration_structure_radv *acceleration_structure =
      (gamma_acceleration_structure_radv *)ui.focused_acceleration_structure;

   begin_bvh_tree();
   if (acceleration_structure) {

      if (ImGui::BeginTable("BVH", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg)) {
         ImGui::TableSetupColumn("node type");
         ImGui::TableSetupColumn("offset");
         ImGui::TableHeadersRow();

         if (trace_info->bvh8)
            bvh8_draw(acceleration_structure, RADV_BVH_ROOT_NODE);
         else
            bvh4_draw(acceleration_structure, RADV_BVH_ROOT_NODE);

         ImGui::EndTable();
      }

      acceleration_structure->radv_ui.open_path.clear();
   }
   end_bvh_tree();

   begin_node_info();

   if (acceleration_structure && acceleration_structure->radv_ui.selected_node_id != RADV_BVH_INVALID_NODE) {
      if (trace_info->bvh8)
         bvh8_draw_node_info(acceleration_structure, acceleration_structure->radv_ui.selected_node_id);
      else
         bvh4_draw_node_info(acceleration_structure, acceleration_structure->radv_ui.selected_node_id);
   }

   end_node_info();

   ImGui::Begin("dispatches", nullptr, ImGuiWindowFlags_NoFocusOnAppearing);

   if (ImGui::BeginTable("dispatches", 5, ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable)) {
      ImGui::TableSetupColumn("index");
      ImGui::TableSetupColumn("type");
      ImGui::TableSetupColumn("width");
      ImGui::TableSetupColumn("height");
      ImGui::TableSetupColumn("depth");
      ImGui::TableHeadersRow();

      for (uint32_t i = 0; i < dispatch_count; i++) {
         ImGui::TableNextRow();
         ImGui::TableSetColumnIndex(0);
         char tmp[32];
         sprintf(tmp, "%u", i);
         if (ImGui::Selectable(tmp, false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_SelectOnClick)) {
            if (dispatch_views[i]->ui.open)
               dispatch_views[i]->ui.request_focus = true;
            dispatch_views[i]->ui.open = true;
         }
         ImGui::TableSetColumnIndex(1);
         switch (dispatch_infos[i].type) {
         case radv_gamma_dispatch_type_trace_rays:
            ImGui::Text("vkCmdTraceRays");
            break;
         case radv_gamma_dispatch_type_trace_rays_indirect:
            ImGui::Text("vkCmdTraceRaysIndirect");
            break;
         case radv_gamma_dispatch_type_trace_rays_indirect2:
            ImGui::Text("vkCmdTraceRaysIndirect2");
            break;
         }
         ImGui::TableSetColumnIndex(2);
         ImGui::Text("%u", dispatch_infos[i].dimensions[0]);
         ImGui::TableSetColumnIndex(3);
         ImGui::Text("%u", dispatch_infos[i].dimensions[1]);
         ImGui::TableSetColumnIndex(4);
         ImGui::Text("%u", dispatch_infos[i].dimensions[2]);
      }

      ImGui::EndTable();
   }

   ImGui::End();

   ImGui::Begin("dispatch", nullptr, ImGuiWindowFlags_NoFocusOnAppearing);

   if (selected_dispatch) {
      ImGui::SeparatorText("reshape");

      if (ImGui::Button("reset")) {
         selected_dispatch->ui.settings.width = selected_dispatch->info->dimensions[0];
         selected_dispatch->ui.settings.height = selected_dispatch->info->dimensions[1];
         selected_dispatch->ui.settings.depth = selected_dispatch->info->dimensions[2];
         selected_dispatch->ui.selection_x = UINT32_MAX;
         selected_dispatch->ui.selection_y = UINT32_MAX;
         selected_dispatch->ui.initialize_viewport = true;
      }
      ImGui::SameLine();

      char tmp[32];
      sprintf(tmp, "%ux%ux%u", selected_dispatch->ui.settings.width, selected_dispatch->ui.settings.height,
              selected_dispatch->ui.settings.depth);
      if (ImGui::BeginMenu(tmp)) {
         for (const auto &valid_size : selected_dispatch->valid_dispatch_sizes) {
            sprintf(tmp, "%ux", valid_size.first);
            if (ImGui::BeginMenu(tmp)) {
               for (const auto &valid_size2 : valid_size.second) {
                  sprintf(tmp, "%ux%ux%u", valid_size.first, valid_size2.first, valid_size2.second);
                  if (ImGui::MenuItem(tmp)) {
                     selected_dispatch->ui.settings.width = valid_size.first;
                     selected_dispatch->ui.settings.height = valid_size2.first;
                     selected_dispatch->ui.settings.depth = valid_size2.second;
                     selected_dispatch->ui.selection_x = UINT32_MAX;
                     selected_dispatch->ui.selection_y = UINT32_MAX;
                     selected_dispatch->ui.initialize_viewport = true;
                  }
               }
               ImGui::EndMenu();
            }
         }
         ImGui::EndMenu();
      }

      ImGui::SeparatorText("slice");

      int z = selected_dispatch->ui.settings.z;
      ImGui::InputInt("z", &z, 1);
      selected_dispatch->ui.settings.z = MIN2((uint32_t)z, selected_dispatch->ui.settings.depth - 1);

      int max_iteration_count = selected_dispatch->ui.settings.max_iteration_count;
      ImGui::SliderInt("max iteration count", &max_iteration_count, 0, selected_dispatch->max_iteration_count);
      selected_dispatch->ui.settings.max_iteration_count = max_iteration_count;

      ImGui::Checkbox("box_iteration_count", &selected_dispatch->ui.settings.show_box_iteration_count);
      ImGui::Checkbox("instance_iteration_count", &selected_dispatch->ui.settings.show_instance_iteration_count);
      ImGui::Checkbox("primitive_iteration_count", &selected_dispatch->ui.settings.show_primitive_iteration_count);
   }

   ImGui::End();

   for (uint32_t i = 0; i < dispatch_count; i++) {
      gamma_dispatch_view *dispatch_view = dispatch_views[i].get();
      if (!dispatch_view->ui.open)
         continue;

      char tmp[32];
      sprintf(tmp, "dispatch[%u]", i);
      if (!ImGui::Begin(tmp, nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
         ImGui::End();
         continue;
      }

      if (dispatch_view->ui.request_focus) {
         ImGui::SetWindowFocus();
         dispatch_view->ui.request_focus = false;
      }

      if (ImGui::IsWindowFocused())
         selected_dispatch = dispatch_view;

      if (memcmp(&dispatch_view->ui.settings, &dispatch_view->ui.prev_settings, sizeof(dispatch_view->ui.settings))) {
         std::shared_ptr<ImTextureData> texture_data = std::make_shared<ImTextureData>();
         texture_data->Create(ImTextureFormat_RGBA32, dispatch_view->ui.settings.width,
                              dispatch_view->ui.settings.height);

         uint32_t base_index =
            dispatch_view->ui.settings.z * dispatch_view->ui.settings.width * dispatch_view->ui.settings.height;

         for (uint32_t y = 0; y < dispatch_view->ui.settings.height; y++) {
            for (uint32_t x = 0; x < dispatch_view->ui.settings.width; x++) {
               gamma_invocation_data *invocation =
                  &dispatch_view->invocations[x + y * dispatch_view->ui.settings.width + base_index];
               uint32_t iteration_count = 0;
               if (dispatch_view->ui.settings.show_box_iteration_count)
                  iteration_count += invocation->box_iteration_count;
               if (dispatch_view->ui.settings.show_instance_iteration_count)
                  iteration_count += invocation->instance_iteration_count;
               if (dispatch_view->ui.settings.show_primitive_iteration_count)
                  iteration_count += invocation->primitive_iteration_count;

               float brightness =
                  fmin((float)iteration_count / (float)dispatch_view->ui.settings.max_iteration_count, 1.0);
               ImVec4 color = {brightness, brightness, brightness, 1};

               ImU32 *pixel = (ImU32 *)texture_data->GetPixelsAt(x, y);
               *pixel = ImGui::ColorConvertFloat4ToU32(color);
            }
         }
         ImGui::RegisterUserTexture(texture_data.get());
         dispatch_view->ui.texture_data.push_back(texture_data);

         dispatch_view->ui.prev_settings = dispatch_view->ui.settings;
      }

      for (auto it = dispatch_view->ui.destroy_texture_data.begin();
           it != dispatch_view->ui.destroy_texture_data.end();) {
         (*it)->UnusedFrames++;
         if ((*it)->Status == ImTextureStatus_Destroyed) {
            ImGui::UnregisterUserTexture(it->get());
            it = dispatch_view->ui.destroy_texture_data.erase(it);
         } else {
            it++;
         }
      }

      while (dispatch_view->ui.texture_data.size() > 1) {
         if (dispatch_view->ui.texture_data[1]->Status != ImTextureStatus_OK)
            break;

         if (dispatch_view->ui.texture_data[0]->Status != ImTextureStatus_OK)
            break;

         dispatch_view->ui.texture_data[0]->Status = ImTextureStatus_WantDestroy;
         dispatch_view->ui.texture_data[0]->DestroyPixels();
         dispatch_view->ui.destroy_texture_data.push_back(dispatch_view->ui.texture_data[0]);
         dispatch_view->ui.texture_data.erase(dispatch_view->ui.texture_data.begin());
      }

      ImVec2 viewport_offset = ImGui::GetCursorScreenPos();
      ImVec2 viewport_size = ImGui::GetContentRegionAvail();
      ImVec2 image_size = ImVec2(dispatch_view->ui.settings.width, dispatch_view->ui.settings.height);

      if (dispatch_view->ui.initialize_viewport) {
         dispatch_view->ui.initialize_viewport = false;

         dispatch_view->ui.image_scale = fmin(viewport_size.x / image_size.x, viewport_size.y / image_size.y);
         dispatch_view->ui.viewport_center.x = image_size.x / 2;
         dispatch_view->ui.viewport_center.y = image_size.y / 2;
      }

      ImVec2 mouse_position = ImGui::GetMousePos();
      bool mouse_inside_viewport = mouse_position.x >= viewport_offset.x && mouse_position.y >= viewport_offset.y &&
                                   mouse_position.x <= viewport_offset.x + viewport_size.x &&
                                   mouse_position.y <= viewport_offset.y + viewport_size.y;

      if (ImGui::IsWindowFocused()) {
         bool right_mouse_button_pressed = ImGui::IsMouseDown(ImGuiMouseButton_Right);

         /* Detect thet start/end of dragging inside the viewport. */
         if (!dispatch_view->ui.is_dragging_viewport && right_mouse_button_pressed && mouse_inside_viewport) {
            dispatch_view->ui.prev_mouse_pos = mouse_position;
            dispatch_view->ui.is_dragging_viewport = true;
         }
         if (dispatch_view->ui.is_dragging_viewport && !right_mouse_button_pressed) {
            dispatch_view->ui.is_dragging_viewport = false;
         }

         if (dispatch_view->ui.is_dragging_viewport) {
            float drag_delta_x = mouse_position.x - dispatch_view->ui.prev_mouse_pos.x;
            float drag_delta_y = mouse_position.y - dispatch_view->ui.prev_mouse_pos.y;
            dispatch_view->ui.viewport_center.x -= drag_delta_x / dispatch_view->ui.image_scale;
            dispatch_view->ui.viewport_center.y -= drag_delta_y / dispatch_view->ui.image_scale;
         }

         dispatch_view->ui.prev_mouse_pos = mouse_position;

         if (mouse_inside_viewport) {
            float mouse_wheel = ImGui::GetIO().MouseWheel;
            dispatch_view->ui.image_scale *= pow(1.1, mouse_wheel);
         }

         if (ImGui::IsMouseClicked(ImGuiMouseButton_Left, ImGuiInputFlags_None) && mouse_inside_viewport) {
            float image_x =
               (mouse_position.x - (viewport_offset.x + viewport_size.x / 2)) / dispatch_view->ui.image_scale +
               dispatch_view->ui.viewport_center.x;
            float image_y =
               (mouse_position.y - (viewport_offset.y + viewport_size.y / 2)) / dispatch_view->ui.image_scale +
               dispatch_view->ui.viewport_center.y;
            if (image_x >= 0 && image_x < image_size.x && image_y >= 0 && image_y < image_size.y) {
               dispatch_view->ui.selection_x = (uint32_t)image_x;
               dispatch_view->ui.selection_y = (uint32_t)image_y;
            }
         }
      }

      if (dispatch_view->ui.texture_data.size() && dispatch_view->ui.texture_data[0]->Status == ImTextureStatus_OK) {
         ImDrawList *draw_list = ImGui::GetWindowDrawList();

         ImGui::SetCursorPosX(ImGui::GetCursorPosX() -
                              dispatch_view->ui.viewport_center.x * dispatch_view->ui.image_scale +
                              viewport_size.x / 2);
         ImGui::SetCursorPosY(ImGui::GetCursorPosY() -
                              dispatch_view->ui.viewport_center.y * dispatch_view->ui.image_scale +
                              viewport_size.y / 2);

         ImVec2 global_image_offset = ImGui::GetCursorScreenPos();

         draw_list->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerNearest);
         ImGui::Image(
            dispatch_view->ui.texture_data[0]->GetTexRef(),
            ImVec2(image_size.x * dispatch_view->ui.image_scale, image_size.y * dispatch_view->ui.image_scale));
         draw_list->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerLinear);

         if (dispatch_view->ui.selection_x != UINT32_MAX) {
            ImU32 selection_color = ImGui::ColorConvertFloat4ToU32(
               ImVec4(app->selection_color.x, app->selection_color.y, app->selection_color.z, 1));
            ImVec2 tl = {
               global_image_offset.x + (dispatch_view->ui.selection_x + 0) * dispatch_view->ui.image_scale,
               global_image_offset.y + (dispatch_view->ui.selection_y + 0) * dispatch_view->ui.image_scale,
            };
            ImVec2 br = {
               global_image_offset.x + (dispatch_view->ui.selection_x + 1) * dispatch_view->ui.image_scale,
               global_image_offset.y + (dispatch_view->ui.selection_y + 1) * dispatch_view->ui.image_scale,
            };
            float thickness = fmax(1.0, 0.2 * dispatch_view->ui.image_scale);
            draw_list->AddRect(tl, br, selection_color, thickness, 0, thickness);
         }
      }

      ImGui::End();
   }

   ImGui::Begin("invocation", nullptr, ImGuiWindowFlags_NoFocusOnAppearing);
   if (ImGui::BeginTable("invocation_info", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
      ImGui::TableNextColumnText("launch_id");
      if (selected_dispatch && selected_invocation && !selected_invocation->offsets.empty()) {
         const radv_packed_token_header *header =
            (const radv_packed_token_header *)(history_data + selected_invocation->offsets[0]);
         uint32_t x = header->launch_index % selected_dispatch->info->dimensions[0];
         uint32_t y = (header->launch_index / selected_dispatch->info->dimensions[0]) % selected_dispatch->info->dimensions[1];
         uint32_t z = header->launch_index / selected_dispatch->info->dimensions[0] / selected_dispatch->info->dimensions[1];
         ImGui::TableNextColumnText("(%u, %u, %u)", x, y, z);
      } else {
         ImGui::TableNextColumnText("--");
      }
      ImGui::EndTable();
   }
   ImGui::BeginChild("traces");
   if (selected_invocation) {
      for (uint32_t i = 0; i < selected_invocation->offsets.size(); i++) {
         const radv_packed_token_header *header =
            (const radv_packed_token_header *)(history_data + selected_invocation->offsets[i]);
         if (header->token_type != radv_packed_token_trace_ray)
            continue;

         const radv_packed_trace_ray_token *token = (const radv_packed_trace_ray_token *)header;

         char tmp[32];
         sprintf(tmp, "trace_ray[%u]", i);

         ImGui::Separator();
         if (ImGui::Selectable(tmp, i == selected_invocation->selected_ray))
            selected_invocation->selected_ray = i;

         if (ImGui::BeginTable(tmp, 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableNextColumnText("accel_struct");
            ImGui::TableNextColumn();
            uint64_t addr = ((uint64_t)token->accel_struct_hi << 32) | token->accel_struct_lo;
            sprintf(tmp, "0x%" PRIx64, addr);
            if (ImGui::TextLink(tmp)) {
               gamma_acceleration_structure *acceleration_structure = addr_to_acceleration_structure(addr);
               if (acceleration_structure->ui.opened)
                  acceleration_structure->ui.request_focus = true;
               acceleration_structure->ui.opened = true;
            }

            ImGui::TableNextColumnText("origin");
            ImGui::TableNextColumnText("(%f, %f, %f)", token->origin[0], token->origin[1], token->origin[2]);

            ImGui::TableNextColumnText("direction");
            ImGui::TableNextColumnText("(%f, %f, %f)", token->direction[0], token->direction[1], token->direction[2]);

            ImGui::TableNextColumnText("tmin/tmax");
            ImGui::TableNextColumnText("(%f, %f)", token->tmin, token->tmax);

            ImGui::TableNextColumnText("sbt_offset");
            ImGui::TableNextColumnText("%u", token->sbt_offset);

            ImGui::TableNextColumnText("sbt_stride");
            ImGui::TableNextColumnText("%u", token->sbt_stride);

            ImGui::TableNextColumnText("miss_index");
            ImGui::TableNextColumnText("%u", token->miss_index);

            ImGui::TableNextColumnText("cull_mask");
            ImGui::TableNextColumnText("0x%x", token->cull_mask);

            ImGui::TableNextColumnText("flags");
            ImGui::TableNextColumnText(
               "%s%s%s%s%s%s%s%s%s%s", (token->flags & SpvRayFlagsOpaqueKHRMask) ? "Opaque " : "",
               (token->flags & SpvRayFlagsNoOpaqueKHRMask) ? "NoOpaque " : "",
               (token->flags & SpvRayFlagsTerminateOnFirstHitKHRMask) ? "TerminateOnFirstHit " : "",
               (token->flags & SpvRayFlagsSkipClosestHitShaderKHRMask) ? "SkipClosestHitShader " : "",
               (token->flags & SpvRayFlagsCullBackFacingTrianglesKHRMask) ? "CullBackFacingTriangles " : "",
               (token->flags & SpvRayFlagsCullFrontFacingTrianglesKHRMask) ? "CullFrontFacingTriangles " : "",
               (token->flags & SpvRayFlagsCullOpaqueKHRMask) ? "CullOpaque " : "",
               (token->flags & SpvRayFlagsCullNoOpaqueKHRMask) ? "CullNoOpaque " : "",
               (token->flags & SpvRayFlagsSkipTrianglesKHRMask) ? "SkipTriangles " : "",
               (token->flags & SpvRayFlagsSkipAABBsKHRMask) ? "SkipAABBs " : "");

            ImGui::EndTable();
         }
      }
   }
   ImGui::EndChild();
   ImGui::End();

   ImGui::Begin("ray history", nullptr, ImGuiWindowFlags_NoFocusOnAppearing);

   if (selected_invocation && !selected_invocation->offsets.empty()) {
      const radv_packed_trace_ray_token *trace_ray =
         (const radv_packed_trace_ray_token *)(history_data +
                                               selected_invocation->offsets[selected_invocation->selected_ray]);
      uint64_t accel_struct_addr = ((uint64_t)trace_ray->accel_struct_hi << 32) | trace_ray->accel_struct_lo;

      if (ImGui::BeginTable("history", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg)) {
         for (uint32_t i = selected_invocation->selected_ray + 1; i < selected_invocation->offsets.size(); i++) {
            const radv_packed_token_header *header =
               (const radv_packed_token_header *)(history_data + selected_invocation->offsets[i]);
            if (header->token_type == radv_packed_token_trace_ray)
               break;

            if (header->token_type == radv_packed_token_iteration) {
               const radv_packed_iteration_token *token = (const radv_packed_iteration_token *)header;

               uint32_t offset = (token->node_id & (~0xf)) << 3;
               uint32_t type = token->node_id & 0xf;

               gamma_acceleration_structure_radv *acceleration_structure =
                  (gamma_acceleration_structure_radv *)addr_to_acceleration_structure(accel_struct_addr);

               char selectable_label[32];
               sprintf(selectable_label, "%s##%x", node_type_names[type], i);

               ImGui::TableNextColumn();
               if (ImGui::Selectable(selectable_label,
                                     token->node_id == acceleration_structure->radv_ui.selected_node_id,
                                     ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_SelectOnClick)) {
                  if (acceleration_structure->ui.opened)
                     acceleration_structure->ui.request_focus = true;
                  acceleration_structure->ui.opened = true;
                  acceleration_structure->radv_ui.selected_node_id = token->node_id;
               }
               ImGui::TableNextColumnText("0x%x", offset);
            } else if (header->token_type == radv_packed_token_accel_struct) {
               const radv_packed_accel_struct_token *token = (const radv_packed_accel_struct_token *)header;
               accel_struct_addr = token->accel_struct;
            }
         }

         ImGui::EndTable();
      }
   }

   ImGui::End();

   end();
}

std::unique_ptr<gamma_file_view>
gamma_create_file_view_radv()
{
   return std::make_unique<gamma_file_view_radv>();
}
