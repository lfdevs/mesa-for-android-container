/*
 * Copyright © 2026 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "gamma_file_view.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <stdint.h>

#include "shaders/gamma_shader_interface.h"

#include "util/gamma_format.h"
#include "util/macros.h"
#include "util/os_time.h"
#include "util/u_math.h"

#include "vulkan/vulkan_core.h"

#include <backends/imgui_impl_vulkan.h>
#include "imgui.h"
#include "imgui_internal.h"

#include "gamma_app.h"
#include "gamma_util.h"

gamma_render_list::~gamma_render_list()
{
   vkDestroyDescriptorPool(app->device, descriptor_pool, nullptr);
}

void
gamma_render_list::build()
{
   if (tasks.empty())
      return;

   constants = gamma_create_backed_buffer(app, tasks.size() * sizeof(gamma_render_params),
                                          gamma_memory_type_host_visible, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
   gamma_render_params *consts = (gamma_render_params *)constants->map;
   for (uint32_t i = 0; i < tasks.size(); i++)
      consts[i] = tasks[i].params;

   VkDescriptorPoolSize pool_sizes[] = {
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
   };
   VkDescriptorPoolCreateInfo pool_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
      .maxSets = 1,
      .poolSizeCount = ARRAY_SIZE(pool_sizes),
      .pPoolSizes = pool_sizes,
   };

   gamma_check_vk_result(vkCreateDescriptorPool(app->device, &pool_info, nullptr, &descriptor_pool));

   VkDescriptorSetAllocateInfo set_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = descriptor_pool,
      .descriptorSetCount = 1,
      .pSetLayouts = &app->renderer_set_layout,
   };

   gamma_check_vk_result(vkAllocateDescriptorSets(app->device, &set_info, &descriptor_set));

   VkDescriptorBufferInfo constants_buffer_info = {
      .buffer = constants->buffer,
      .range = VK_WHOLE_SIZE,
   };

   VkWriteDescriptorSet constants_buffer_write = {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = descriptor_set,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .pBufferInfo = &constants_buffer_info,
   };

   vkUpdateDescriptorSets(app->device, 1, &constants_buffer_write, 0, nullptr);
}

gamma_imgui_texture::~gamma_imgui_texture()
{
   if (descriptor_set)
      ImGui_ImplVulkan_RemoveTexture(descriptor_set);
}

gamma_acceleration_structure::gamma_acceleration_structure(gamma_file_view *view): app(view->app), view(view)
{
}

void
gamma_acceleration_structure::load(FILE *file, uint32_t chunk_size)
{
   fread(&header, sizeof(header), 1, file);

   primitive_counts = (uint32_t *)malloc(header.geometry_count * sizeof(uint32_t));
   primitive_counts_exclusive_sum = (uint32_t *)malloc(header.geometry_count * sizeof(uint32_t));
   fread(primitive_counts, header.geometry_count * sizeof(uint32_t), 1, file);
   for (uint32_t i = 0; i < header.geometry_count; i++) {
      primitive_counts_exclusive_sum[i] = primitive_count;
      primitive_count += primitive_counts[i];
   }

   name = (char *)malloc(header.name_size + 1);
   name[header.name_size] = 0;
   fread(name, header.name_size, 1, file);

   uint64_t data_size = chunk_size - header.name_size - sizeof(gamma_acceleration_structure_header);
   data = malloc(data_size);
   fread(data, data_size, 1, file);
}

void
gamma_acceleration_structure::init_camera()
{
   gamma_vec3 center = {
      .x = (aabb.max.x + aabb.min.x) / 2,
      .y = (aabb.max.y + aabb.min.y) / 2,
      .z = (aabb.max.z + aabb.min.z) / 2,
   };
   gamma_vec3 extent = {
      .x = aabb.max.x - aabb.min.x,
      .y = aabb.max.y - aabb.min.y,
      .z = aabb.max.z - aabb.min.z,
   };
   float max_extent = fmax(fmax(extent.x, extent.y), extent.z);

   ui.camera_position = center;
   ui.camera_position.x -= max_extent;
   ui.camera_position.y -= max_extent;
   ui.camera_position.z -= max_extent;
   ui.camera_theta = M_PI / 4;
   ui.camera_phi = M_PI / 4;
}

void
gamma_file_view::load(FILE *file, const gamma_header *header)
{
   for (uint32_t i = 0; i < header->chunk_count; i++) {
      gamma_chunk_header chunk_header;
      fread(&chunk_header, sizeof(chunk_header), 1, file);

      if (chunk_header.type == gamma_chunk_type_acceleration_structure) {
         std::unique_ptr<gamma_acceleration_structure> acceleration_structure = create_acceleration_structure();
         acceleration_structure->app = app;
         acceleration_structure->load(file, chunk_header.size);
         address_map[acceleration_structure->header.address] = acceleration_structures.size();
         acceleration_structures.push_back(std::move(acceleration_structure));
      } else if (chunk_header.type >= gamma_chunk_type_driver_start) {
         load_driver_specific(file, &chunk_header, header);
      } else {
         fprintf(stderr, "gamma: Invalid gamma_chunk_type\n");
         return;
      }
   }
}

gamma_acceleration_structure *
gamma_file_view::addr_to_acceleration_structure(uint64_t address)
{
   if (address_map.empty())
      return nullptr;

   auto lower_bound = address_map.lower_bound(address);

   if (lower_bound == address_map.end() || lower_bound->first > address)
      lower_bound--;

   gamma_acceleration_structure *acceleration_structure = acceleration_structures[lower_bound->second].get();
   if (address >= acceleration_structure->header.address &&
       address < acceleration_structure->header.address + acceleration_structure->header.allocated_size)
      return acceleration_structure;

   return nullptr;
}

std::unique_ptr<gamma_file_view>
gamma_create_file_view(gamma_app *app, const char *file_path)
{
   int64_t start_time = os_time_get_nano();

   FILE *file = fopen(file_path, "rb");
   if (!file)
      return nullptr;

   gamma_header header;
   fread(&header, sizeof(header), 1, file);

   std::unique_ptr<gamma_file_view> view = nullptr;
   if (header.driver == gamma_driver_radv)
      view = gamma_create_file_view_radv();
   else
      return nullptr;

   view->app = app;
   view->path = file_path;
   view->load(file, &header);

   fclose(file);

   int64_t end_time = os_time_get_nano();
   fprintf(stderr, "gamma: Opening file \"%s\" took %.2fms\n", file_path, (end_time - start_time) / 1000000.0);

   return view;
}

static const std::map<VkAccelerationStructureTypeKHR, const char *> acceleration_structure_type_filters = {
   {VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, "TLAS"},
   {VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, "BLAS"},
   {VK_ACCELERATION_STRUCTURE_TYPE_GENERIC_KHR, "any"},
};

static const std::map<uint32_t, const char *> visualization_colors = {
   {gamma_visualization_color_primitive_index, "primitive_index"},
   {gamma_visualization_color_geometry_index, "geometry_index"},
   {gamma_visualization_color_instance_index, "instance_index"},
};

static const std::map<uint32_t, const char *> camera_types = {
   {gamma_camera_type_first_person, "first person"},
   {gamma_camera_type_pivot, "pivot"},
};

static const gamma_mat4 basis_transforms[] = {
   {.elements = {0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}},  /* x -> -y, y -> x, z -> z */
   {.elements = {-1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}}, /* x -> -x, y -> -y, z -> z */
   {.elements = {1, 0, 0, 0, 0, 0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1}},  /* x -> x, y -> z, z -> -y */
   {.elements = {0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}},  /* x -> y, y -> -x, z -> z */
   {.elements = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}},   /* x -> x, y -> y, z -> z */
   {.elements = {1, 0, 0, 0, 0, 0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1}},  /* x -> x, y -> -z, z -> y */
};

struct gamma_basis_guizmo_axis_description {
   const char *label;
   gamma_vec4 end;
   ImVec4 color;
   gamma_vec4 transformed_end;
};

static int
gamma_basis_guizmo_axis_description_compare(const void *_a, const void *_b)
{
   const gamma_basis_guizmo_axis_description *a = (const gamma_basis_guizmo_axis_description *)_a;
   const gamma_basis_guizmo_axis_description *b = (const gamma_basis_guizmo_axis_description *)_b;
   /* End points are sorted in ascending order, large z values need to be rendered first. */
   return a->transformed_end.z > b->transformed_end.z ? -1 : 1;
}

void
gamma_file_view::begin()
{
   if (!ui.initialized) {
      ImGuiViewport *viewport = ImGui::GetMainViewport();

      ui.dockspace_id = ImGui::GetID(path.c_str());
      ImGui::DockBuilderAddNode(ui.dockspace_id, ImGuiDockNodeFlags_DockSpace);
      ImGui::DockBuilderSetNodeSize(ui.dockspace_id, viewport->Size);

      ImGuiID left_and_center = 0;
      ImGuiID right = 0;
      ImGui::DockBuilderSplitNode(ui.dockspace_id, ImGuiDir_Right, 0.3, &right, &left_and_center);
      ImGuiID left = 0;
      ImGuiID center = 0;
      ImGui::DockBuilderSplitNode(left_and_center, ImGuiDir_Left, 0.3 / 0.7, &left, &center);
      ImGuiID left_top = 0;
      ImGuiID left_bottom = 0;
      ImGui::DockBuilderSplitNode(left, ImGuiDir_Up, 0.6, &left_top, &left_bottom);
      ImGuiID right_top = 0;
      ImGuiID right_bottom = 0;
      ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, 0.6, &right_top, &right_bottom);

      ImGui::DockBuilderDockWindow("acceleration structures", right_top);
      ImGui::DockBuilderDockWindow("viewport", right_bottom);
      ImGui::DockBuilderDockWindow("BVH", left_top);
      ImGui::DockBuilderDockWindow("node info", left_bottom);
      for (const auto &acceleration_structure : acceleration_structures)
         ImGui::DockBuilderDockWindow(acceleration_structure->name, center);

      dock_driver_specific(left_top, left_bottom, center, right_top, right_bottom);

      ImGui::DockBuilderFinish(ui.dockspace_id);

      ui.dockspace_class.ClassId = ImHashStr(path.c_str());

      ui.filter_acceleration_structure_type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

      ui.initialized = true;
   }

   if (ui.request_focus) {
      ImGui::SetWindowFocus();
      ui.request_focus = false;
   }

   ImGui::DockSpace(ui.dockspace_id, ImVec2(0, 0), ImGuiDockNodeFlags_NoWindowMenuButton, &ui.dockspace_class);

   ImGui::SetNextWindowClass(&ui.dockspace_class);

   ImGui::Begin("acceleration structures");

   if (ImGui::BeginCombo("type", acceleration_structure_type_filters.at(ui.filter_acceleration_structure_type))) {
      for (const auto &filter : acceleration_structure_type_filters) {
         if (ImGui::Selectable(filter.second))
            ui.filter_acceleration_structure_type = filter.first;
      }
      ImGui::EndCombo();
   }

   if (ImGui::BeginTable("acceleration structures", 4, ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable)) {
      ImGui::TableSetupColumn("name");
      ImGui::TableSetupColumn("type");
      ImGui::TableSetupColumn("allocated size");
      ImGui::TableSetupColumn("compacted size");
      ImGui::TableHeadersRow();

      for (const auto &acceleration_structure : acceleration_structures) {
         if (ui.filter_acceleration_structure_type != VK_ACCELERATION_STRUCTURE_TYPE_GENERIC_KHR &&
             ui.filter_acceleration_structure_type != acceleration_structure->header.type)
            continue;

         ImGui::TableNextRow();
         ImGui::TableSetColumnIndex(0);
         if (ImGui::Selectable(acceleration_structure->name, false,
                               ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_SelectOnClick)) {
            if (acceleration_structure->ui.opened)
               acceleration_structure->ui.request_focus = true;
            acceleration_structure->ui.opened = true;
         }
         ImGui::TableSetColumnIndex(1);
         ImGui::Text("%s", acceleration_structure_type_filters.at(acceleration_structure->header.type));
         ImGui::TableSetColumnIndex(2);
         ImGui::Text("%.1f MiB", (double)acceleration_structure->header.allocated_size / (1024 * 1024));
         ImGui::TableSetColumnIndex(3);
         ImGui::Text("%.1f MiB", (double)acceleration_structure->header.compacted_size / (1024 * 1024));
      }

      ImGui::EndTable();
   }

   ImGui::End();

   ImGui::Begin("viewport");

   const char *axis_names[] = {"+x", "+y", "+z", "-x", "-y", "-z"};

   if (ImGui::BeginCombo("up axis", axis_names[up_axis])) {
      for (uint32_t i = 0; i < ARRAY_SIZE(axis_names); i++) {
         if (ImGui::Selectable(axis_names[i]))
            up_axis = i;
      }
      ImGui::EndCombo();
   }

   if (ui.focused_acceleration_structure) {
      ImGui::SeparatorText("camera");

      if (ImGui::BeginCombo("type", camera_types.at(ui.focused_acceleration_structure->ui.camera_type))) {
         for (const auto &type : camera_types) {
            if (ImGui::Selectable(type.second))
               ui.focused_acceleration_structure->ui.camera_type = (gamma_camera_type)type.first;
         }
         ImGui::EndCombo();
      }

      ImGui::SliderFloat("speed", &ui.focused_acceleration_structure->ui.camera_speed, 0, 1);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
         ImGui::SetTooltip("Speed in max scene extent per second");

      ImGui::SliderFloat("near", &ui.focused_acceleration_structure->ui.camera_near, 0, 0.001);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
         ImGui::SetTooltip("Camera near plane in max scene extent");

      ImGui::SliderFloat("far", &ui.focused_acceleration_structure->ui.camera_far, 0, 10);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
         ImGui::SetTooltip("Camera far plane in max scene extent");

      ImGui::SliderFloat("fov", &ui.focused_acceleration_structure->ui.camera_fov, 0, 179);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
         ImGui::SetTooltip("Field of view in degrees");

      ImGui::SeparatorText("visualization");

      if (ImGui::BeginCombo("color",
                            visualization_colors.at(ui.focused_acceleration_structure->ui.visualization_color))) {
         for (const auto &color : visualization_colors) {
            if (ImGui::Selectable(color.second))
               ui.focused_acceleration_structure->ui.visualization_color = (gamma_visualization_color)color.first;
         }
         ImGui::EndCombo();
      }
   }
   ImGui::End();
}

void
gamma_file_view::end()
{
}

bool
gamma_file_view::begin_viewport(gamma_acceleration_structure *acceleration_structure)
{
   rendering_state.render = false;
   if (!acceleration_structure->ui.opened)
      return false;

   rendering_state.render = ImGui::Begin(acceleration_structure->name);

   if (acceleration_structure->ui.request_focus) {
      ImGui::SetWindowFocus();
      rendering_state.render = true;
      acceleration_structure->ui.request_focus = false;
   }

   if (ImGui::IsWindowFocused()) {
      ui.focused_acceleration_structure = acceleration_structure;
   }

   if (!rendering_state.render) {
      acceleration_structure->ui.prev_focused = ImGui::IsWindowFocused();
      ImGui::End();
      return false;
   }

   rendering_state.acceleration_structure = acceleration_structure;
   rendering_state.viewport_offset = ImGui::GetCursorScreenPos();
   rendering_state.viewport_size = ImGui::GetContentRegionAvail();

   uint32_t viewport_width = rendering_state.viewport_size.x;
   uint32_t viewport_height = rendering_state.viewport_size.y;
   if ((viewport_width != acceleration_structure->ui.viewport.width ||
        viewport_height != acceleration_structure->ui.viewport.height) &&
       rendering_state.viewport_size.x > 0 && rendering_state.viewport_size.y > 0) {
      printf("gamma: Resizing viewport to (%u, %u)\n", viewport_width, viewport_height);

      VkExtent3D viewport_extent = {
         .width = viewport_width,
         .height = viewport_height,
         .depth = 1,
      };

      acceleration_structure->ui.viewport.cb =
         gamma_create_backed_image(app, viewport_extent, {VK_FORMAT_R8G8B8A8_UNORM},
                                   VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT,
                                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                   VK_IMAGE_ASPECT_COLOR_BIT, app->sample_count, 1);
      acceleration_structure->ui.viewport.db = gamma_create_backed_image(
         app, viewport_extent, {VK_FORMAT_D32_SFLOAT}, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT,
         VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, app->sample_count, 1);
      acceleration_structure->ui.viewport.resolved_image = gamma_create_backed_image(
         app, viewport_extent, {VK_FORMAT_R8G8B8A8_UNORM},
         VK_FORMAT_FEATURE_TRANSFER_DST_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT,
         VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1);

      acceleration_structure->ui.viewport.width = viewport_width;
      acceleration_structure->ui.viewport.height = viewport_height;

      acceleration_structure->ui.viewport.surface = std::make_shared<gamma_imgui_texture>(ImGui_ImplVulkan_AddTexture(
         acceleration_structure->ui.viewport.resolved_image->image_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));

      VkImageMemoryBarrier initial_transitions[] = {
         {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_NONE,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .srcQueueFamilyIndex = app->queue_family_index,
            .dstQueueFamilyIndex = app->queue_family_index,
            .image = acceleration_structure->ui.viewport.resolved_image->image,
            .subresourceRange =
               {
                  .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                  .levelCount = 1,
                  .layerCount = 1,
               },
         },
         {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_NONE,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = app->queue_family_index,
            .dstQueueFamilyIndex = app->queue_family_index,
            .image = acceleration_structure->ui.viewport.cb->image,
            .subresourceRange =
               {
                  .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                  .levelCount = 1,
                  .layerCount = 1,
               },
         },
         {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_NONE,
            .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = app->queue_family_index,
            .dstQueueFamilyIndex = app->queue_family_index,
            .image = acceleration_structure->ui.viewport.db->image,
            .subresourceRange =
               {
                  .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                  .levelCount = 1,
                  .layerCount = 1,
               },
         },
      };

      vkCmdPipelineBarrier(app->command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                           VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                              VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                           0, 0, nullptr, 0, nullptr, ARRAY_SIZE(initial_transitions), initial_transitions);
   }

   ImVec2 mouse_position = ImGui::GetMousePos();
   bool mouse_inside_viewport =
      mouse_position.x >= rendering_state.viewport_offset.x && mouse_position.y >= rendering_state.viewport_offset.y &&
      mouse_position.x <= rendering_state.viewport_offset.x + rendering_state.viewport_size.x &&
      mouse_position.y <= rendering_state.viewport_offset.y + rendering_state.viewport_size.y;

   gamma_vec3 extent = {
      .x = acceleration_structure->aabb.max.x - acceleration_structure->aabb.min.x,
      .y = acceleration_structure->aabb.max.y - acceleration_structure->aabb.min.y,
      .z = acceleration_structure->aabb.max.z - acceleration_structure->aabb.min.z,
   };
   float max_extent = fmax(fmax(extent.x, extent.y), extent.z);

   float near = max_extent * acceleration_structure->ui.camera_near;
   float far = max_extent * acceleration_structure->ui.camera_far;
   float fov = M_PI / 180 * acceleration_structure->ui.camera_fov;
   float x_aspect_ratio = 1;
   float y_aspect_ratio = 1;
   if (viewport_width > viewport_height) {
      y_aspect_ratio = (float)viewport_width / (float)viewport_height;
   } else {
      x_aspect_ratio = (float)viewport_height / (float)viewport_width;
   }
   rendering_state.projection_matrix = gamma_mat4::zero();
   rendering_state.projection_matrix.elements[0 + 0 * 4] = x_aspect_ratio / tan(fov / 2);
   rendering_state.projection_matrix.elements[1 + 1 * 4] = y_aspect_ratio / tan(fov / 2);
   rendering_state.projection_matrix.elements[2 + 2 * 4] = far / (far - near);
   rendering_state.projection_matrix.elements[3 + 2 * 4] = 1;
   rendering_state.projection_matrix.elements[2 + 3 * 4] = -2 * far * near / (far - near);

   rendering_state.inv_camera_rotation = gamma_mat4();
   float camera_x_phi = acceleration_structure->ui.camera_phi - M_PI_2;
   float camera_x_theta = M_PI_2;
   rendering_state.inv_camera_rotation.elements[0 * 4 + 0] = cos(camera_x_phi) * sin(camera_x_theta);
   rendering_state.inv_camera_rotation.elements[1 * 4 + 0] = cos(camera_x_theta);
   rendering_state.inv_camera_rotation.elements[2 * 4 + 0] = sin(camera_x_phi) * sin(camera_x_theta);
   float camera_y_phi = acceleration_structure->ui.camera_phi;
   float camera_y_theta = acceleration_structure->ui.camera_theta - M_PI_2;
   rendering_state.inv_camera_rotation.elements[0 * 4 + 1] = cos(camera_y_phi) * sin(camera_y_theta);
   rendering_state.inv_camera_rotation.elements[1 * 4 + 1] = cos(camera_y_theta);
   rendering_state.inv_camera_rotation.elements[2 * 4 + 1] = sin(camera_y_phi) * sin(camera_y_theta);
   float camera_z_phi = acceleration_structure->ui.camera_phi;
   float camera_z_theta = acceleration_structure->ui.camera_theta;
   rendering_state.inv_camera_rotation.elements[0 * 4 + 2] = cos(camera_z_phi) * sin(camera_z_theta);
   rendering_state.inv_camera_rotation.elements[1 * 4 + 2] = cos(camera_z_theta);
   rendering_state.inv_camera_rotation.elements[2 * 4 + 2] = sin(camera_z_phi) * sin(camera_z_theta);

   if (acceleration_structure->ui.camera_type == gamma_camera_type_pivot &&
       acceleration_structure->ui.camera_pivot_valid) {
      gamma_vec3 camera_z = {
         .x = rendering_state.inv_camera_rotation.elements[0 * 4 + 2],
         .y = rendering_state.inv_camera_rotation.elements[1 * 4 + 2],
         .z = rendering_state.inv_camera_rotation.elements[2 * 4 + 2],
      };

      acceleration_structure->ui.camera_position.x =
         acceleration_structure->ui.camera_pivot.x - acceleration_structure->ui.camera_pivot_distance * camera_z.x;
      acceleration_structure->ui.camera_position.y =
         acceleration_structure->ui.camera_pivot.y - acceleration_structure->ui.camera_pivot_distance * camera_z.y;
      acceleration_structure->ui.camera_position.z =
         acceleration_structure->ui.camera_pivot.z - acceleration_structure->ui.camera_pivot_distance * camera_z.z;
   }

   gamma_mat4 inv_camera_position;
   inv_camera_position.elements[0 + 3 * 4] = -acceleration_structure->ui.camera_position.x;
   inv_camera_position.elements[1 + 3 * 4] = -acceleration_structure->ui.camera_position.y;
   inv_camera_position.elements[2 + 3 * 4] = -acceleration_structure->ui.camera_position.z;

   rendering_state.view_projection_matrix =
      gamma_mat4::mul(rendering_state.projection_matrix,
                      gamma_mat4::mul(rendering_state.inv_camera_rotation,
                                      gamma_mat4::mul(inv_camera_position, basis_transforms[up_axis])));

   rendering_state.camera_pivot = acceleration_structure->ui.camera_pivot;

   if (acceleration_structure->ui.camera_type != gamma_camera_type_pivot)
      acceleration_structure->ui.camera_pivot_valid = false;

   bool started_middle_dragging = false;

   bool right_mouse_button_pressed = ImGui::IsMouseDown(ImGuiMouseButton_Right);
   bool middle_mouse_button_pressed = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
   bool left_mouse_button_pressed = ImGui::IsMouseDown(ImGuiMouseButton_Left);

   if (ImGui::IsWindowFocused()) {
      float ds = app->dt * acceleration_structure->ui.camera_speed * max_extent;
      if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl))
         ds *= 2;

      gamma_vec3 dr;
      if (ImGui::IsKeyDown(ImGuiKey_W)) {
         dr.x += cos(acceleration_structure->ui.camera_phi) * ds;
         dr.z += sin(acceleration_structure->ui.camera_phi) * ds;
      }
      if (ImGui::IsKeyDown(ImGuiKey_S)) {
         dr.x -= cos(acceleration_structure->ui.camera_phi) * ds;
         dr.z -= sin(acceleration_structure->ui.camera_phi) * ds;
      }
      if (ImGui::IsKeyDown(ImGuiKey_A)) {
         dr.x += cos(acceleration_structure->ui.camera_phi + M_PI_2) * ds;
         dr.z += sin(acceleration_structure->ui.camera_phi + M_PI_2) * ds;
      }
      if (ImGui::IsKeyDown(ImGuiKey_D)) {
         dr.x -= cos(acceleration_structure->ui.camera_phi + M_PI_2) * ds;
         dr.z -= sin(acceleration_structure->ui.camera_phi + M_PI_2) * ds;
      }
      if (ImGui::IsKeyDown(ImGuiKey_Space)) {
         dr.y -= ds;
      }
      if (ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift)) {
         dr.y += ds;
      }

      if (acceleration_structure->ui.camera_type == gamma_camera_type_pivot) {
         acceleration_structure->ui.camera_pivot.x += dr.x;
         acceleration_structure->ui.camera_pivot.y += dr.y;
         acceleration_structure->ui.camera_pivot.z += dr.z;
      } else {
         acceleration_structure->ui.camera_position.x += dr.x;
         acceleration_structure->ui.camera_position.y += dr.y;
         acceleration_structure->ui.camera_position.z += dr.z;
      }

      /* Detect thet start/end of dragging inside the viewport. */
      if (!acceleration_structure->ui.is_middle_dragging_viewport &&
          !acceleration_structure->ui.is_right_dragging_viewport && mouse_inside_viewport) {
         if (right_mouse_button_pressed) {
            acceleration_structure->ui.prev_mouse_pos = mouse_position;
            acceleration_structure->ui.is_right_dragging_viewport = true;
         } else if (middle_mouse_button_pressed) {
            acceleration_structure->ui.prev_mouse_pos = mouse_position;
            acceleration_structure->ui.is_middle_dragging_viewport = true;
            started_middle_dragging = true;
         }
      }
      if (acceleration_structure->ui.is_right_dragging_viewport && !right_mouse_button_pressed)
         acceleration_structure->ui.is_right_dragging_viewport = false;
      if (acceleration_structure->ui.is_middle_dragging_viewport && !middle_mouse_button_pressed)
         acceleration_structure->ui.is_middle_dragging_viewport = false;

      if (acceleration_structure->ui.is_right_dragging_viewport) {
         float drag_delta_x = mouse_position.x - acceleration_structure->ui.prev_mouse_pos.x;
         float drag_delta_y = mouse_position.y - acceleration_structure->ui.prev_mouse_pos.y;
         acceleration_structure->ui.camera_phi += drag_delta_x / x_aspect_ratio * fov / viewport_width;
         acceleration_structure->ui.camera_theta += drag_delta_y / y_aspect_ratio * fov / viewport_height;
      }

      if (mouse_inside_viewport) {
         gamma_mat4 inv_view_projection_matrix;
         util_invert_mat4x4(inv_view_projection_matrix.elements, rendering_state.view_projection_matrix.elements);

         gamma_vec4 ndc_mouse_position = {
            .x = (mouse_position.x - rendering_state.viewport_offset.x) / rendering_state.viewport_size.x * 2 - 1,
            .y = (mouse_position.y - rendering_state.viewport_offset.y) / rendering_state.viewport_size.y * 2 - 1,
            .z = 1,
            .w = 1,
         };
         gamma_vec4 global_mouse_position4 = gamma_mat4::mul_vec4(inv_view_projection_matrix, ndc_mouse_position);
         gamma_vec3 global_mouse_position = {
            .x = global_mouse_position4.x / global_mouse_position4.w,
            .y = global_mouse_position4.y / global_mouse_position4.w,
            .z = global_mouse_position4.z / global_mouse_position4.w,
         };
         gamma_vec4 origin4 = {
            .x = acceleration_structure->ui.camera_position.x,
            .y = acceleration_structure->ui.camera_position.y,
            .z = acceleration_structure->ui.camera_position.z,
         };
         origin4 = gamma_mat4::mul_vec4(gamma_mat4::transpose(basis_transforms[up_axis]), origin4);
         gamma_vec3 origin = {origin4.x, origin4.y, origin4.z};
         gamma_ray ray = {
            .origin = origin,
            .direction = gamma_vec3::sub(global_mouse_position, origin),
         };
         gamma_vec4 camera_z = {
            .x = rendering_state.inv_camera_rotation.elements[0 * 4 + 2],
            .y = rendering_state.inv_camera_rotation.elements[1 * 4 + 2],
            .z = rendering_state.inv_camera_rotation.elements[2 * 4 + 2],
         };
         camera_z = gamma_mat4::mul_vec4(gamma_mat4::transpose(basis_transforms[up_axis]), camera_z);
         float t = handle_mouse_click(acceleration_structure, ray, false);

         if (started_middle_dragging) {
            acceleration_structure->ui.middle_drag_start_z_distance =
               t * (ray.direction.x * camera_z.x + ray.direction.y * camera_z.y + ray.direction.z * camera_z.z);
            if (t == INFINITY)
               acceleration_structure->ui.middle_drag_start_z_distance = far;
         }

         if (acceleration_structure->ui.camera_type == gamma_camera_type_pivot &&
             !acceleration_structure->ui.camera_pivot_valid &&
             (left_mouse_button_pressed || middle_mouse_button_pressed || right_mouse_button_pressed) && t < INFINITY) {
            acceleration_structure->ui.camera_pivot = ray.origin;
            acceleration_structure->ui.camera_pivot.x += t * ray.direction.x;
            acceleration_structure->ui.camera_pivot.y += t * ray.direction.y;
            acceleration_structure->ui.camera_pivot.z += t * ray.direction.z;
            acceleration_structure->ui.camera_pivot_distance = t * sqrt(gamma_vec3::dot(ray.direction, ray.direction));
            acceleration_structure->ui.camera_pivot_valid = true;
         }
      }

      if (acceleration_structure->ui.is_middle_dragging_viewport) {
         float drag_delta_x = mouse_position.x - acceleration_structure->ui.prev_mouse_pos.x;
         float drag_delta_y = mouse_position.y - acceleration_structure->ui.prev_mouse_pos.y;

         float dx = -drag_delta_x * 2.0 / viewport_width / x_aspect_ratio * tan(fov / 2) *
                    acceleration_structure->ui.middle_drag_start_z_distance;
         gamma_vec3 dr = {
            .x = rendering_state.inv_camera_rotation.elements[0 * 4 + 0] * dx,
            .y = rendering_state.inv_camera_rotation.elements[1 * 4 + 0] * dx,
            .z = rendering_state.inv_camera_rotation.elements[2 * 4 + 0] * dx,
         };

         float dy = -drag_delta_y * 2.0 / viewport_height / y_aspect_ratio * tan(fov / 2) *
                    acceleration_structure->ui.middle_drag_start_z_distance;
         dr.x += rendering_state.inv_camera_rotation.elements[0 * 4 + 1] * dy;
         dr.y += rendering_state.inv_camera_rotation.elements[1 * 4 + 1] * dy;
         dr.z += rendering_state.inv_camera_rotation.elements[2 * 4 + 1] * dy;

         if (acceleration_structure->ui.camera_type == gamma_camera_type_pivot) {
            acceleration_structure->ui.camera_pivot.x += dr.x;
            acceleration_structure->ui.camera_pivot.y += dr.y;
            acceleration_structure->ui.camera_pivot.z += dr.z;
         } else {
            acceleration_structure->ui.camera_position.x += dr.x;
            acceleration_structure->ui.camera_position.y += dr.y;
            acceleration_structure->ui.camera_position.z += dr.z;
         }
      }

      acceleration_structure->ui.prev_mouse_pos = mouse_position;

      if (acceleration_structure->ui.camera_type == gamma_camera_type_pivot &&
          acceleration_structure->ui.camera_pivot_valid && mouse_inside_viewport) {
         float mouse_wheel = ImGui::GetIO().MouseWheel;
         acceleration_structure->ui.camera_pivot_distance /= pow(1.1, mouse_wheel);
      }
   }

   if (ImGui::IsMouseClicked(ImGuiMouseButton_Left, ImGuiInputFlags_None) && mouse_inside_viewport &&
       acceleration_structure->ui.prev_focused) {
      gamma_mat4 inv_view_projection_matrix;
      util_invert_mat4x4(inv_view_projection_matrix.elements, rendering_state.view_projection_matrix.elements);

      gamma_vec4 ndc_mouse_position = {
         .x = (mouse_position.x - rendering_state.viewport_offset.x) / rendering_state.viewport_size.x * 2 - 1,
         .y = (mouse_position.y - rendering_state.viewport_offset.y) / rendering_state.viewport_size.y * 2 - 1,
         .z = 1,
         .w = 1,
      };
      gamma_vec4 global_mouse_position4 = gamma_mat4::mul_vec4(inv_view_projection_matrix, ndc_mouse_position);
      gamma_vec3 global_mouse_position = {
         .x = global_mouse_position4.x / global_mouse_position4.w,
         .y = global_mouse_position4.y / global_mouse_position4.w,
         .z = global_mouse_position4.z / global_mouse_position4.w,
      };
      gamma_vec4 origin4 = {
         .x = acceleration_structure->ui.camera_position.x,
         .y = acceleration_structure->ui.camera_position.y,
         .z = acceleration_structure->ui.camera_position.z,
      };
      origin4 = gamma_mat4::mul_vec4(gamma_mat4::transpose(basis_transforms[up_axis]), origin4);
      gamma_vec3 origin = {origin4.x, origin4.y, origin4.z};
      gamma_ray ray = {
         .origin = origin,
         .direction = gamma_vec3::sub(global_mouse_position, origin),
      };
      handle_mouse_click(acceleration_structure, ray, true);
   }

   acceleration_structure->ui.prev_focused = ImGui::IsWindowFocused();

   acceleration_structure->ui.viewport.render_list = std::make_shared<gamma_render_list>(app);

   return true;
}

void
gamma_file_view::render(const gamma_render_task &task)
{
   gamma_render_task task_copy = task;
   task_copy.params.transform = gamma_mat4::mul(rendering_state.view_projection_matrix, task_copy.params.transform);
   rendering_state.acceleration_structure->ui.viewport.render_list->tasks.push_back(task_copy);
}

void
gamma_file_view::render_aabb(const gamma_render_task &base_task, gamma_aabb aabb, bool fill)
{
   gamma_render_task task = base_task;
   task.type = gamma_render_task_type_lines;
   task.vertex_buffer = fill ? app->filled_cube_vertex_buffer.get() : app->cube_vertex_buffer.get();
   task.first_vertex = 0;
   task.vertex_count = fill ? GAMMA_FILLED_CUBE_VERTEX_COUNT : GAMMA_CUBE_VERTEX_COUNT;

   gamma_mat4 aabb_matrix;
   aabb_matrix.elements[0 + 0 * 4] = aabb.max.x - aabb.min.x;
   aabb_matrix.elements[1 + 1 * 4] = aabb.max.y - aabb.min.y;
   aabb_matrix.elements[2 + 2 * 4] = aabb.max.z - aabb.min.z;
   aabb_matrix.elements[0 + 3 * 4] = aabb.min.x;
   aabb_matrix.elements[1 + 3 * 4] = aabb.min.y;
   aabb_matrix.elements[2 + 3 * 4] = aabb.min.z;
   task.params.transform = gamma_mat4::mul(task.params.transform, aabb_matrix);

   render(task);
}

struct gamma_tracked_render_state {
   gamma_render_task last_render_task;
   bool first_task = true;
   std::vector<VkMultiDrawInfoEXT> draws;
   uint32_t first_param = 0;
};

static void
gamma_flush_draws(gamma_file_view *view, gamma_tracked_render_state *state, const uint32_t *flags_override,
                  gamma_mat4 transform, uint32_t task_index)
{
   if (state->draws.empty())
      return;

   gamma_push_constants consts = {
      .transform = transform,
      .first_param = state->first_param,
      .flags = flags_override ? *flags_override : state->last_render_task.flags,
   };

   vkCmdPushConstants(view->app->command_buffer, view->app->renderer_pipeline_layout,
                      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(consts), &consts);

   view->app->vkCmdDrawMultiEXT(view->app->command_buffer, state->draws.size(), state->draws.data(), 1, 0,
                                sizeof(VkMultiDrawInfoEXT));

   state->draws.clear();
   state->first_param = task_index;
}

static void
gamma_process_render_list(gamma_file_view *view, gamma_render_list *list, gamma_tracked_render_state *state,
                          const VkRenderingInfo *rendering_info, const uint32_t *flags_override, gamma_mat4 transform)
{
   for (uint32_t i = 0; i < list->tasks.size(); i++) {
      const gamma_render_task &task = list->tasks[i];

      if (task.wait_for_prev) {
         gamma_flush_draws(view, state, flags_override, transform, i);

         vkCmdEndRendering(view->app->command_buffer);

         VkImageMemoryBarrier barriers[] = {
            {
               .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
               .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
               .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
               .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
               .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
               .srcQueueFamilyIndex = view->app->queue_family_index,
               .dstQueueFamilyIndex = view->app->queue_family_index,
               .image = view->rendering_state.acceleration_structure->ui.viewport.cb->image,
               .subresourceRange =
                  {
                     .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                     .levelCount = 1,
                     .layerCount = 1,
                  },
            },
            {
               .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
               .srcAccessMask =
                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
               .dstAccessMask =
                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
               .oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
               .newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
               .srcQueueFamilyIndex = view->app->queue_family_index,
               .dstQueueFamilyIndex = view->app->queue_family_index,
               .image = view->rendering_state.acceleration_structure->ui.viewport.db->image,
               .subresourceRange =
                  {
                     .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                     .levelCount = 1,
                     .layerCount = 1,
                  },
            },
         };

         vkCmdPipelineBarrier(view->app->command_buffer,
                              VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                 VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                              VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                 VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                              0, 0, nullptr, 0, nullptr, ARRAY_SIZE(barriers), barriers);

         vkCmdBeginRendering(view->app->command_buffer, rendering_info);
      }

      if (task.type == gamma_render_task_type_render_list) {
         gamma_flush_draws(view, state, flags_override, transform, i);
         state->first_task = true;

         gamma_process_render_list(view, task.render_list, state, rendering_info,
                                   task.override_flags ? &task.flags : nullptr, task.params.transform);

         state->first_param = i + 1;
         state->first_task = true;

         continue;
      }

      if (state->first_task || task.type != state->last_render_task.type) {
         gamma_flush_draws(view, state, flags_override, transform, i);

         VkPipeline pipeline = VK_NULL_HANDLE;
         switch (task.type) {
         case gamma_render_task_type_solid:
            pipeline = view->app->fill_pipeline;
            break;
         case gamma_render_task_type_wireframe:
            pipeline = view->app->wireframe_pipeline;
            break;
         case gamma_render_task_type_thick_wireframe:
            pipeline = view->app->thick_wireframe_pipeline;
            break;
         case gamma_render_task_type_lines:
            pipeline = view->app->lines_pipeline;
            break;
         case gamma_render_task_type_thick_lines:
            pipeline = view->app->thick_lines_pipeline;
            break;
         default:
            assert(false);
         }
         vkCmdBindPipeline(view->app->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

         vkCmdBindDescriptorSets(view->app->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 view->app->renderer_pipeline_layout, 0, 1, &list->descriptor_set, 0, nullptr);
      }

      if (state->first_task || task.vertex_buffer != state->last_render_task.vertex_buffer) {
         gamma_flush_draws(view, state, flags_override, transform, i);

         VkDeviceSize vertex_buffer_offset = 0;
         vkCmdBindVertexBuffers(view->app->command_buffer, 0, 1, &task.vertex_buffer->buffer, &vertex_buffer_offset);
      }

      if (task.flags != state->last_render_task.flags)
         gamma_flush_draws(view, state, flags_override, transform, i);

      if (state->draws.size() >= view->app->max_multi_draw_count)
         gamma_flush_draws(view, state, flags_override, transform, i);

      VkMultiDrawInfoEXT draw = {
         .firstVertex = task.first_vertex,
         .vertexCount = task.vertex_count,
      };
      state->draws.push_back(draw);

      state->last_render_task = task;
      state->first_task = false;
   }

   gamma_flush_draws(view, state, flags_override, transform, list->tasks.size() - 1);
}

void
gamma_file_view::render_viewport()
{
   if (!rendering_state.render)
      return;

   rendering_state.acceleration_structure->ui.viewport.render_list->build();

   VkRenderingAttachmentInfo color_attachment = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = rendering_state.acceleration_structure->ui.viewport.cb->image_view,
      .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .clearValue =
         {
            .color =
               {
                  .float32 = {1, 1, 1, 1},
               },
         },
   };

   VkRenderingAttachmentInfo depth_attachment = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = rendering_state.acceleration_structure->ui.viewport.db->image_view,
      .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .clearValue =
         {
            .depthStencil =
               {
                  .depth = 1,
                  .stencil = 0,
               },
         },
   };

   uint32_t viewport_width = rendering_state.viewport_size.x;
   uint32_t viewport_height = rendering_state.viewport_size.y;

   VkRenderingInfo rendering_info = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea =
         {
            .extent =
               {
                  .width = viewport_width,
                  .height = viewport_height,
               },
         },
      .layerCount = 1,
      .colorAttachmentCount = 1,
      .pColorAttachments = &color_attachment,
      .pDepthAttachment = &depth_attachment,
   };

   vkCmdBeginRendering(app->command_buffer, &rendering_info);

   VkViewport viewport = {
      .width = (float)viewport_width,
      .height = (float)viewport_height,
      .minDepth = 0.0,
      .maxDepth = 1.0,
   };
   vkCmdSetViewport(app->command_buffer, 0, 1, &viewport);

   VkRect2D scissor = {
      .extent =
         {
            .width = viewport_width,
            .height = viewport_height,
         },
   };
   vkCmdSetScissor(app->command_buffer, 0, 1, &scissor);

   /* Set load ops to load in case rendering needs to be restarted to sync. */
   color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
   depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;

   gamma_tracked_render_state state;
   gamma_process_render_list(this, rendering_state.acceleration_structure->ui.viewport.render_list.get(), &state,
                             &rendering_info, nullptr, gamma_mat4());
   rendering_state.acceleration_structure->ui.viewport.render_list->tasks.clear();

   vkCmdEndRendering(app->command_buffer);

   VkImageMemoryBarrier transitions[] = {
      {
         .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
         .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
         .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
         .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
         .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
         .srcQueueFamilyIndex = app->queue_family_index,
         .dstQueueFamilyIndex = app->queue_family_index,
         .image = rendering_state.acceleration_structure->ui.viewport.cb->image,
         .subresourceRange =
            {
               .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
               .levelCount = 1,
               .layerCount = 1,
            },
      },
      {
         .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
         .srcAccessMask = VK_ACCESS_NONE,
         .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
         .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
         .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
         .srcQueueFamilyIndex = app->queue_family_index,
         .dstQueueFamilyIndex = app->queue_family_index,
         .image = rendering_state.acceleration_structure->ui.viewport.resolved_image->image,
         .subresourceRange =
            {
               .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
               .levelCount = 1,
               .layerCount = 1,
            },
      },
   };

   vkCmdPipelineBarrier(app->command_buffer,
                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                           VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, ARRAY_SIZE(transitions),
                        transitions);

   VkImageResolve resolve_region = {
      .srcSubresource =
         {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .layerCount = 1,
         },
      .dstSubresource =
         {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .layerCount = 1,
         },
      .extent =
         {
            .width = viewport_width,
            .height = viewport_height,
            .depth = 1,
         },
   };
   vkCmdResolveImage(app->command_buffer, rendering_state.acceleration_structure->ui.viewport.cb->image,
                     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     rendering_state.acceleration_structure->ui.viewport.resolved_image->image,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &resolve_region);

   VkImageMemoryBarrier transitions2[] = {
      {
         .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
         .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
         .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
         .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
         .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
         .srcQueueFamilyIndex = app->queue_family_index,
         .dstQueueFamilyIndex = app->queue_family_index,
         .image = rendering_state.acceleration_structure->ui.viewport.resolved_image->image,
         .subresourceRange =
            {
               .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
               .levelCount = 1,
               .layerCount = 1,
            },
      },
      {
         .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
         .srcAccessMask = VK_ACCESS_NONE,
         .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
         .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
         .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
         .srcQueueFamilyIndex = app->queue_family_index,
         .dstQueueFamilyIndex = app->queue_family_index,
         .image = rendering_state.acceleration_structure->ui.viewport.cb->image,
         .subresourceRange =
            {
               .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
               .levelCount = 1,
               .layerCount = 1,
            },
      },
   };

   vkCmdPipelineBarrier(app->command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0,
                        nullptr, 0, nullptr, ARRAY_SIZE(transitions2), transitions2);

   ImTextureRef viewport_tex_ref = rendering_state.acceleration_structure->ui.viewport.surface->descriptor_set;
   ImGui::Image(viewport_tex_ref, rendering_state.viewport_size);
}

void
gamma_file_view::end_viewport()
{
   if (!rendering_state.render)
      return;

   ImDrawList *draw_list = ImGui::GetWindowDrawList();

   float basis_guizmo_radius = 60;
   float axis_length = basis_guizmo_radius * 0.6;
   float axis_thickness = 3;
   float axis_label_radius = 8;

   gamma_basis_guizmo_axis_description axis_descriptions[] = {
      {
         .label = "X",
         .end = {1, 0, 0, 1},
         .color = ImVec4(0.8, 0.0, 0.0, 1.0),
      },
      {
         .label = "Y",
         .end = {0, 1, 0, 1},
         .color = ImVec4(0.0, 0.8, 0.0, 1.0),
      },
      {
         .label = "Z",
         .end = {0, 0, 1, 1},
         .color = ImVec4(0.0, 0.0, 0.8, 1.0),
      },
   };

   gamma_mat4 view_projection_matrix =
      gamma_mat4::mul(rendering_state.projection_matrix,
                      gamma_mat4::mul(rendering_state.inv_camera_rotation, basis_transforms[up_axis]));
   gamma_mat4 inv_view_projection_matrix;
   util_invert_mat4x4(inv_view_projection_matrix.elements, view_projection_matrix.elements);

   ImVec2 basis_guizmo_center =
      ImVec2(rendering_state.viewport_offset.x + rendering_state.viewport_size.x - basis_guizmo_radius,
             rendering_state.viewport_offset.y + basis_guizmo_radius);

   gamma_vec4 ndc_basis_guizmo_center = {
      .x = 1 - basis_guizmo_radius * 2 / rendering_state.viewport_size.x,
      .y = -1 + basis_guizmo_radius * 2 / rendering_state.viewport_size.y,
      .z = 1,
      .w = 1,
   };
   gamma_vec4 global_basis_guizmo_center = gamma_mat4::mul_vec4(inv_view_projection_matrix, ndc_basis_guizmo_center);
   global_basis_guizmo_center.x /= global_basis_guizmo_center.w;
   global_basis_guizmo_center.y /= global_basis_guizmo_center.w;
   global_basis_guizmo_center.z /= global_basis_guizmo_center.w;

   gamma_vec4 ndc_basis_guizmo_corner = {
      .x = 1 + (axis_length - basis_guizmo_radius) * 2 / rendering_state.viewport_size.x,
      .y = -1 + basis_guizmo_radius * 2 / rendering_state.viewport_size.y,
      .z = 1,
      .w = 1,
   };
   gamma_vec4 global_basis_guizmo_corner = gamma_mat4::mul_vec4(inv_view_projection_matrix, ndc_basis_guizmo_corner);
   global_basis_guizmo_corner.x /= global_basis_guizmo_corner.w;
   global_basis_guizmo_corner.y /= global_basis_guizmo_corner.w;
   global_basis_guizmo_corner.z /= global_basis_guizmo_corner.w;

   gamma_vec3 global_test_axis = {
      .x = global_basis_guizmo_corner.x - global_basis_guizmo_center.x,
      .y = global_basis_guizmo_corner.y - global_basis_guizmo_center.y,
      .z = global_basis_guizmo_corner.z - global_basis_guizmo_center.z,
   };

   float global_axis_length = sqrt(global_test_axis.x * global_test_axis.x + global_test_axis.y * global_test_axis.y +
                                   global_test_axis.z * global_test_axis.z);

   for (uint32_t j = 0; j < ARRAY_SIZE(axis_descriptions); j++) {
      gamma_vec4 end = {
         .x = global_basis_guizmo_center.x + axis_descriptions[j].end.x * global_axis_length,
         .y = global_basis_guizmo_center.y + axis_descriptions[j].end.y * global_axis_length,
         .z = global_basis_guizmo_center.z + axis_descriptions[j].end.z * global_axis_length,
         .w = 1,
      };
      end = gamma_mat4::mul_vec4(view_projection_matrix, end);
      end.x = (end.x / end.w + 1.0) / 2 * rendering_state.viewport_size.x + rendering_state.viewport_offset.x;
      end.y = (end.y / end.w + 1.0) / 2 * rendering_state.viewport_size.y + rendering_state.viewport_offset.y;
      end.z = end.z / end.w;
      axis_descriptions[j].transformed_end = end;
   }

   qsort(axis_descriptions, ARRAY_SIZE(axis_descriptions), sizeof(axis_descriptions[0]),
         gamma_basis_guizmo_axis_description_compare);

   ImGui::PushFont(nullptr, axis_label_radius * 2);
   for (uint32_t j = 0; j < ARRAY_SIZE(axis_descriptions); j++) {
      ImVec2 screen_space_end = ImVec2(axis_descriptions[j].transformed_end.x, axis_descriptions[j].transformed_end.y);

      ImU32 color = ImGui::ColorConvertFloat4ToU32(axis_descriptions[j].color);
      draw_list->AddLine(basis_guizmo_center, screen_space_end, color, axis_thickness);
      draw_list->AddCircleFilled(screen_space_end, axis_label_radius, color);

      const char *label = axis_descriptions[j].label;
      ImVec2 x_label_size = ImGui::CalcTextSize(label);
      draw_list->AddText(ImVec2(screen_space_end.x - x_label_size.x / 2, screen_space_end.y - x_label_size.y / 2),
                         ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, 1)), label);
   }
   ImGui::PopFont();

   if (rendering_state.acceleration_structure->ui.camera_type == gamma_camera_type_pivot) {
      ImVec2 pivot_center = ImGui::GetMousePos();
      if (rendering_state.acceleration_structure->ui.camera_pivot_valid) {
         gamma_vec4 pos4 = {
            .x = rendering_state.camera_pivot.x,
            .y = rendering_state.camera_pivot.y,
            .z = rendering_state.camera_pivot.z,
            .w = 1,
         };
         pos4 = gamma_mat4::mul_vec4(gamma_mat4::transpose(basis_transforms[up_axis]), pos4);
         pos4 = gamma_mat4::mul_vec4(rendering_state.view_projection_matrix, pos4);
         pivot_center.x =
            ((pos4.x / pos4.w) + 1.0) / 2.0 * rendering_state.viewport_size.x + rendering_state.viewport_offset.x;
         pivot_center.y =
            ((pos4.y / pos4.w) + 1.0) / 2.0 * rendering_state.viewport_size.y + rendering_state.viewport_offset.y;
      }

      float pivot_radius = 6;
      float pivot_thickness = 1;
      ImU32 pivot_color = ImGui::ColorConvertFloat4ToU32(ImVec4(0, 0, 0, pivot_thickness));

      draw_list->AddCircle(pivot_center, pivot_radius, pivot_color, 0, 1);
      draw_list->AddLine(ImVec2(pivot_center.x - pivot_radius / sqrt(2), pivot_center.y - pivot_radius / sqrt(2)),
                         ImVec2(pivot_center.x + pivot_radius / sqrt(2), pivot_center.y + pivot_radius / sqrt(2)),
                         pivot_color, pivot_thickness);
      draw_list->AddLine(ImVec2(pivot_center.x - pivot_radius / sqrt(2), pivot_center.y + pivot_radius / sqrt(2)),
                         ImVec2(pivot_center.x + pivot_radius / sqrt(2), pivot_center.y - pivot_radius / sqrt(2)),
                         pivot_color, pivot_thickness);
   }

   /* Take a reference of the viewport state so the GPU resources are kept alive while they are used. */
   rendering_state.acceleration_structure->ui.pending_viewports.resize(app->imgui_window.SemaphoreCount);
   rendering_state.acceleration_structure->ui.pending_viewports[app->imgui_window.SemaphoreIndex] =
      rendering_state.acceleration_structure->ui.viewport;

   ImGui::End();
}

void
gamma_file_view::draw_point(gamma_vec3 pos, gamma_vec3 color, float stroke)
{
   ImDrawList *draw_list = ImGui::GetWindowDrawList();

   gamma_vec4 pos4 = {pos.x, pos.y, pos.z, 1};
   pos4 = gamma_mat4::mul_vec4(rendering_state.view_projection_matrix, pos4);
   pos4.x = ((pos4.x / pos4.w) + 1.0) / 2.0 * rendering_state.viewport_size.x;
   pos4.y = ((pos4.y / pos4.w) + 1.0) / 2.0 * rendering_state.viewport_size.y;
   if (pos4.z > 0) {
      draw_list->AddCircleFilled(
         ImVec2(rendering_state.viewport_offset.x + pos4.x, rendering_state.viewport_offset.y + pos4.y), stroke,
         ImGui::ColorConvertFloat4ToU32(ImVec4(color.x, color.y, color.z, 1)));
   }
}

void
gamma_file_view::draw_line(gamma_vec3 start, gamma_vec3 end, gamma_vec3 color, float stroke)
{
   ImDrawList *draw_list = ImGui::GetWindowDrawList();

   gamma_vec4 start4 = {start.x, start.y, start.z, 1};
   start4 = gamma_mat4::mul_vec4(rendering_state.view_projection_matrix, start4);

   gamma_vec4 end4 = {end.x, end.y, end.z, 1};
   end4 = gamma_mat4::mul_vec4(rendering_state.view_projection_matrix, end4);

   if (start4.z < 0 && end4.z < 0)
      return;

   if (start4.z < 0) {
      /* 0 = start.z + t * (end.z - start.z) => t = -start.z / (end.z - start.z) */
      float t = -start4.z / (end4.z - start4.z);
      start4.x += (end4.x - start4.x) * t;
      start4.y += (end4.y - start4.y) * t;
      start4.z += (end4.z - start4.z) * t;
      start4.w += (end4.w - start4.w) * t;
   }

   if (end4.z < 0) {
      /* 0 = end.z + t * (start.z - end.z) => t = -end.z / (start.z - end.z) */
      float t = -end4.z / (start4.z - end4.z);
      end4.x += (start4.x - end4.x) * t;
      end4.y += (start4.y - end4.y) * t;
      end4.z += (start4.z - end4.z) * t;
      end4.w += (start4.w - end4.w) * t;
   }

   start4.x = ((start4.x / start4.w) + 1.0) / 2.0 * rendering_state.viewport_size.x;
   start4.y = ((start4.y / start4.w) + 1.0) / 2.0 * rendering_state.viewport_size.y;

   end4.x = ((end4.x / end4.w) + 1.0) / 2.0 * rendering_state.viewport_size.x;
   end4.y = ((end4.y / end4.w) + 1.0) / 2.0 * rendering_state.viewport_size.y;

   draw_list->AddLine(
      ImVec2(rendering_state.viewport_offset.x + start4.x, rendering_state.viewport_offset.y + start4.y),
      ImVec2(rendering_state.viewport_offset.x + end4.x, rendering_state.viewport_offset.y + end4.y),
      ImGui::ColorConvertFloat4ToU32(ImVec4(color.x, color.y, color.z, 1)), stroke);
}

void
gamma_file_view::begin_bvh_tree()
{
   ImGui::Begin("BVH");
}
void
gamma_file_view::end_bvh_tree()
{
   ImGui::End();
}

void
gamma_file_view::begin_node_info()
{
   ImGui::Begin("node info");
}

void
gamma_file_view::end_node_info()
{
   ImGui::End();
}
