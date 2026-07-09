/*
 * Copyright © 2026 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "gamma_app.h"
#include "gamma_file_view.h"
#include "gamma_util.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "shaders/gamma_shader_interface.h"
#include "util/os_time.h"
#include "vulkan/vulkan_core.h"
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_vulkan.h>

#include "backends/imgui_impl_sdl3.h"
#include "imgui.h"
#include "imgui_internal.h"

static const uint32_t renderer_vert_spv[] = {
#include "shaders/renderer_vert.spv.h"
};

static const uint32_t renderer_frag_spv[] = {
#include "shaders/renderer_frag.spv.h"
};

#include "util/macros.h"

int
gamma_app_init(gamma_app *app)
{
   int64_t start_time = os_time_get_nano();

   SDL_Init(SDL_INIT_VIDEO);

   app->window = SDL_CreateWindow("gamma", 720, 400, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED);
   if (app->window == NULL) {
      fprintf(stderr, "gamma: Failed to create window: %s\n", SDL_GetError());
      gamma_app_finish(app);
      return 1;
   }

   uint32_t sdl_extensions_count = 0;
   const char *const *sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_extensions_count);

   uint32_t api_version = VK_API_VERSION_1_0;
   vkEnumerateInstanceVersion(&api_version);

   if (api_version < VK_API_VERSION_1_4) {
      fprintf(stderr, "gamma: VK_API_VERSION_1_4 is required but not supported.\n");
      gamma_app_finish(app);
      return 1;
   }

   uint32_t layer_property_count = 0;
   vkEnumerateInstanceLayerProperties(&layer_property_count, nullptr);
   std::vector<VkLayerProperties> layer_properties(layer_property_count);
   vkEnumerateInstanceLayerProperties(&layer_property_count, layer_properties.data());

   bool has_validation = false;
   for (const VkLayerProperties &properties : layer_properties) {
      if (!strcmp("VK_LAYER_KHRONOS_validation", properties.layerName)) {
         has_validation = true;
         break;
      }
   }

   if (has_validation)
      fprintf(stderr, "gamma: Enabling VK_LAYER_KHRONOS_validation.\n");

   VkApplicationInfo app_info = {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = "mesa.gamma",
      .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
      .pEngineName = "mesa.gamma",
      .engineVersion = VK_MAKE_VERSION(1, 0, 0),
      .apiVersion = api_version,
   };

   const char *layers[] = {"VK_LAYER_KHRONOS_validation"};

   VkInstanceCreateInfo instance_info = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &app_info,
      .enabledLayerCount = (uint32_t)(has_validation ? ARRAY_SIZE(layers) : 0),
      .ppEnabledLayerNames = layers,
      .enabledExtensionCount = sdl_extensions_count,
      .ppEnabledExtensionNames = sdl_extensions,
   };

   gamma_check_vk_result(vkCreateInstance(&instance_info, nullptr, &app->instance));

   VkSurfaceKHR surface;
   if (!SDL_Vulkan_CreateSurface(app->window, app->instance, nullptr, &surface)) {
      fprintf(stderr, "gamma: Failed to create VkSurfaceKHR: %s\n", SDL_GetError());
      gamma_app_finish(app);
      return 1;
   }

   uint32_t physical_device_count = 0;
   gamma_check_vk_result(vkEnumeratePhysicalDevices(app->instance, &physical_device_count, nullptr));
   std::vector<VkPhysicalDevice> physical_devices(physical_device_count);
   gamma_check_vk_result(vkEnumeratePhysicalDevices(app->instance, &physical_device_count, physical_devices.data()));

   for (VkPhysicalDevice pdev : physical_devices) {
      uint32_t extension_property_count = 0;
      vkEnumerateDeviceExtensionProperties(pdev, nullptr, &extension_property_count, nullptr);
      std::vector<VkExtensionProperties> extension_properties(extension_property_count);
      vkEnumerateDeviceExtensionProperties(pdev, nullptr, &extension_property_count, extension_properties.data());

      bool has_multi_draw = false;
      for (const VkExtensionProperties &properties : extension_properties) {
         if (!strcmp(VK_EXT_MULTI_DRAW_EXTENSION_NAME, properties.extensionName)) {
            has_multi_draw = true;
            break;
         }
      }

      if (!has_multi_draw)
         continue;

      VkPhysicalDeviceMultiDrawFeaturesEXT multi_draw_features = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTI_DRAW_FEATURES_EXT,
      };

      VkPhysicalDeviceVulkan11Features features11 = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
         .pNext = &multi_draw_features,
      };

      VkPhysicalDeviceVulkan12Features features12 = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
         .pNext = &features11,
      };

      VkPhysicalDeviceVulkan13Features features13 = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
         .pNext = &features12,
      };

      VkPhysicalDeviceVulkan14Features features14 = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
         .pNext = &features13,
      };

      VkPhysicalDeviceFeatures2 features2 = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
         .pNext = &features14,
      };
      vkGetPhysicalDeviceFeatures2(pdev, &features2);

      if (!features2.features.fillModeNonSolid || !features2.features.wideLines || !features11.shaderDrawParameters ||
          !features12.scalarBlockLayout || !features13.dynamicRendering || !features14.maintenance5 ||
          !multi_draw_features.multiDraw)
         continue;

      uint32_t queue_family_property_count = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(pdev, &queue_family_property_count, nullptr);
      std::vector<VkQueueFamilyProperties> queue_family_properties(queue_family_property_count);
      vkGetPhysicalDeviceQueueFamilyProperties(pdev, &queue_family_property_count, queue_family_properties.data());

      for (uint32_t i = 0; i < queue_family_property_count; i++) {
         if (queue_family_properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            app->queue_family_index = i;
            break;
         }
      }

      app->pdev = pdev;
      break;
   }

   if (!app->pdev) {
      fprintf(stderr, "gamma: Failed to find suitable VkPhysicalDevice\n");
      gamma_app_finish(app);
      return 1;
   }

   VkPhysicalDeviceMultiDrawPropertiesEXT multi_draw_properties = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTI_DRAW_PROPERTIES_EXT,
   };

   VkPhysicalDeviceProperties2 properties2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
      .pNext = &multi_draw_properties,
   };

   vkGetPhysicalDeviceProperties2(app->pdev, &properties2);

   VkSampleCountFlags sample_counts = properties2.properties.limits.framebufferColorSampleCounts &
                                      properties2.properties.limits.framebufferDepthSampleCounts;
   app->sample_count = 1;
   if (sample_counts & VK_SAMPLE_COUNT_16_BIT)
      app->sample_count = 16;
   else if (sample_counts & VK_SAMPLE_COUNT_8_BIT)
      app->sample_count = 8;
   else if (sample_counts & VK_SAMPLE_COUNT_4_BIT)
      app->sample_count = 4;
   else if (sample_counts & VK_SAMPLE_COUNT_2_BIT)
      app->sample_count = 2;

   fprintf(stderr, "gamma: Using GPU: %s\n", properties2.properties.deviceName);

   float thick_line_width = fmin(3.0, properties2.properties.limits.lineWidthRange[1]);

   app->max_multi_draw_count = multi_draw_properties.maxMultiDrawCount;

   float priority = 1.0;
   VkDeviceQueueCreateInfo queue_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = app->queue_family_index,
      .queueCount = 1,
      .pQueuePriorities = &priority,
   };

   const char *extensions[] = {VK_EXT_MULTI_DRAW_EXTENSION_NAME, VK_KHR_SWAPCHAIN_EXTENSION_NAME};

   VkPhysicalDeviceFeatures features = {
      .fillModeNonSolid = true,
      .wideLines = true,
   };

   VkPhysicalDeviceVulkan11Features features11 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
      .shaderDrawParameters = true,
   };

   VkPhysicalDeviceVulkan12Features features12 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
      .pNext = &features11,
      .scalarBlockLayout = true,
   };

   VkPhysicalDeviceVulkan13Features features13 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
      .pNext = &features12,
      .dynamicRendering = true,
   };

   VkPhysicalDeviceVulkan14Features features14 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
      .pNext = &features13,
      .maintenance5 = true,
   };

   VkPhysicalDeviceMultiDrawFeaturesEXT multi_draw_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTI_DRAW_FEATURES_EXT,
      .pNext = &features14,
      .multiDraw = true,
   };

   VkDeviceCreateInfo device_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = &multi_draw_features,
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &queue_info,
      .enabledExtensionCount = ARRAY_SIZE(extensions),
      .ppEnabledExtensionNames = extensions,
      .pEnabledFeatures = &features,
   };
   gamma_check_vk_result(vkCreateDevice(app->pdev, &device_info, nullptr, &app->device));

   vkGetDeviceQueue(app->device, app->queue_family_index, 0, &app->queue);

   app->vkCmdDrawMultiEXT = (PFN_vkCmdDrawMultiEXT)vkGetDeviceProcAddr(app->device, "vkCmdDrawMultiEXT");

   VkDescriptorPoolSize pool_sizes[] = {
      {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE + GAMMA_MAX_OPEN_VIEWS},
      {VK_DESCRIPTOR_TYPE_SAMPLER, IMGUI_IMPL_VULKAN_MINIMUM_SAMPLER_POOL_SIZE},
   };
   VkDescriptorPoolCreateInfo pool_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
      .maxSets = IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE + IMGUI_IMPL_VULKAN_MINIMUM_SAMPLER_POOL_SIZE +
                 GAMMA_MAX_OPEN_VIEWS,
      .poolSizeCount = ARRAY_SIZE(pool_sizes),
      .pPoolSizes = pool_sizes,
   };

   VkDescriptorPool descriptor_pool;
   gamma_check_vk_result(vkCreateDescriptorPool(app->device, &pool_info, nullptr, &descriptor_pool));

   VkFormat request_formats[] = {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8_UNORM,
                                 VK_FORMAT_R8G8B8_UNORM};
   VkColorSpaceKHR color_space = VK_COLORSPACE_SRGB_NONLINEAR_KHR;

   VkClearColorValue clear_color_value = {
      .float32 = {1, 1, 1, 1},
   };

   VkClearValue clear_value = {
      .color = clear_color_value,
   };

   app->imgui_window.ClearValue = clear_value;
   app->imgui_window.Surface = surface;
   app->imgui_window.SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(app->pdev, surface, request_formats,
                                                                           ARRAY_SIZE(request_formats), color_space);

   VkPresentModeKHR present_modes[] = {VK_PRESENT_MODE_FIFO_KHR};
   app->imgui_window.PresentMode =
      ImGui_ImplVulkanH_SelectPresentMode(app->pdev, surface, present_modes, ARRAY_SIZE(present_modes));

   int w, h;
   SDL_GetWindowSize(app->window, &w, &h);
   ImGui_ImplVulkanH_CreateOrResizeWindow(app->instance, app->pdev, app->device, &app->imgui_window,
                                          app->queue_family_index, nullptr, w, h, 2, 0);

   IMGUI_CHECKVERSION();
   ImGui::CreateContext();

   ImGuiIO &io = ImGui::GetIO();
   // io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
   io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
   io.Fonts->AddFontDefaultVector();
   io.IniFilename = nullptr;

   ImGui::StyleColorsLight();

   ImGuiStyle *style = &ImGui::GetStyle();
   style->WindowBorderSize = style->_MainScale;
   style->FrameBorderSize = style->_MainScale;
   style->PopupBorderSize = style->_MainScale;
   style->WindowRounding = 8.0f;
   style->ChildRounding = 8.0f;
   style->FrameRounding = 8.0f;
   style->PopupRounding = 8.0f;
   style->GrabRounding = 8.0f;
   style->ImageBorderSize = 0;
   style->WindowPadding = ImVec2(8, 4);
   style->ItemSpacing = ImVec2(8, 6);
   style->DockingNodeHasCloseButton = false;
   style->Colors[ImGuiCol_DockingEmptyBg] = ImVec4(1, 1, 1, 1);

   ImGui_ImplSDL3_InitForVulkan(app->window);
   ImGui_ImplVulkan_InitInfo init_info = {};
   init_info.Instance = app->instance;
   init_info.PhysicalDevice = app->pdev;
   init_info.Device = app->device;
   init_info.QueueFamily = app->queue_family_index;
   init_info.Queue = app->queue;
   init_info.PipelineCache = VK_NULL_HANDLE;
   init_info.DescriptorPool = descriptor_pool;
   init_info.MinImageCount = 2;
   init_info.ImageCount = app->imgui_window.ImageCount;
   init_info.Allocator = nullptr;
   init_info.PipelineInfoMain.RenderPass = app->imgui_window.RenderPass;
   init_info.PipelineInfoMain.Subpass = 0;
   init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
   init_info.CheckVkResultFn = nullptr;
   ImGui_ImplVulkan_Init(&init_info);

   VkCommandPoolCreateInfo command_pool_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .queueFamilyIndex = app->queue_family_index,
   };
   gamma_check_vk_result(vkCreateCommandPool(app->device, &command_pool_info, nullptr, &app->staging_command_pool));

   /* Start uploading data to the GPU before compiling pipelines to reduce startup time. */
   app->cube_vertex_buffer =
      gamma_create_backed_buffer(app, GAMMA_CUBE_VERTEX_COUNT * sizeof(gamma_vertex), gamma_memory_type_device_local,
                                 VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, false);
   gamma_vertex *cube_vertices = (gamma_vertex *)app->upload_memory(app->cube_vertex_buffer);
   gamma_generate_cube_vertices(cube_vertices, {.min = {0, 0, 0}, .max = {1, 1, 1}});

   app->filled_cube_vertex_buffer = gamma_create_backed_buffer(
      app, GAMMA_FILLED_CUBE_VERTEX_COUNT * sizeof(gamma_vertex), gamma_memory_type_device_local,
      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, false);
   gamma_vertex *filled_cube_vertices = (gamma_vertex *)app->upload_memory(app->filled_cube_vertex_buffer);
   gamma_generate_filled_cube_vertices(filled_cube_vertices, {.min = {0, 0, 0}, .max = {1, 1, 1}}, 0, 0);

   app->flush_upload_memory();

   VkDescriptorSetLayoutBinding binding = {
      .binding = 0,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
   };

   VkDescriptorSetLayoutCreateInfo set_layout_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = 1,
      .pBindings = &binding,
   };

   gamma_check_vk_result(
      vkCreateDescriptorSetLayout(app->device, &set_layout_info, nullptr, &app->renderer_set_layout));

   VkPushConstantRange push_constant_range = {
      .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
      .size = sizeof(gamma_push_constants),
   };

   VkPipelineLayoutCreateInfo pipeline_layout_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &app->renderer_set_layout,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &push_constant_range,
   };

   gamma_check_vk_result(
      vkCreatePipelineLayout(app->device, &pipeline_layout_info, nullptr, &app->renderer_pipeline_layout));

   VkFormat color_attachment_format = VK_FORMAT_R8G8B8A8_UNORM;

   VkPipelineRenderingCreateInfo rendering_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
      .colorAttachmentCount = 1,
      .pColorAttachmentFormats = &color_attachment_format,
      .depthAttachmentFormat = VK_FORMAT_D32_SFLOAT,
   };

   VkShaderModuleCreateInfo module_infos[2] = {
      {
         .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
         .codeSize = sizeof(renderer_vert_spv),
         .pCode = renderer_vert_spv,
      },
      {
         .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
         .codeSize = sizeof(renderer_frag_spv),
         .pCode = renderer_frag_spv,
      },
   };

   VkPipelineShaderStageCreateInfo stage_infos[2] = {
      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
       .pNext = &module_infos[0],
       .stage = VK_SHADER_STAGE_VERTEX_BIT,
       .pName = "main"},
      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
       .pNext = &module_infos[1],
       .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
       .pName = "main"},
   };

   VkVertexInputBindingDescription vertex_binding_info = {
      .binding = 0,
      .stride = sizeof(gamma_vertex),
      .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
   };

   VkVertexInputAttributeDescription vertex_attribute_infos[] = {
      {
         .location = 0,
         .binding = 0,
         .format = VK_FORMAT_R32G32B32_SFLOAT,
         .offset = offsetof(gamma_vertex, position),
      },
      {
         .location = 1,
         .binding = 0,
         .format = VK_FORMAT_R32_UINT,
         .offset = offsetof(gamma_vertex, geometry_index),
      },
      {
         .location = 2,
         .binding = 0,
         .format = VK_FORMAT_R32_UINT,
         .offset = offsetof(gamma_vertex, primitive_index),
      },
   };

   VkPipelineVertexInputStateCreateInfo vertex_input_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 1,
      .pVertexBindingDescriptions = &vertex_binding_info,
      .vertexAttributeDescriptionCount = ARRAY_SIZE(vertex_attribute_infos),
      .pVertexAttributeDescriptions = vertex_attribute_infos,
   };

   VkPipelineInputAssemblyStateCreateInfo input_assembly_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
   };

   VkPipelineViewportStateCreateInfo viewpoirt_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = 1,
      .scissorCount = 1,
   };

   VkPipelineRasterizationStateCreateInfo rasterization_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .polygonMode = VK_POLYGON_MODE_FILL,
      .cullMode = VK_CULL_MODE_NONE,
      .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
      .lineWidth = 1,
   };

   VkPipelineMultisampleStateCreateInfo multisample_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = (VkSampleCountFlagBits)app->sample_count,
      .minSampleShading = 1.0,
   };

   VkPipelineDepthStencilStateCreateInfo depth_stencil_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable = true,
      .depthWriteEnable = true,
      .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
      .minDepthBounds = 0.0,
      .maxDepthBounds = 1.0,
   };

   VkPipelineColorBlendAttachmentState color_blend_attachment_info = {
      .colorWriteMask =
         VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
   };

   VkPipelineColorBlendStateCreateInfo color_blend_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments = &color_blend_attachment_info,
   };

   VkDynamicState dynamic_states[] = {
      VK_DYNAMIC_STATE_VIEWPORT,
      VK_DYNAMIC_STATE_SCISSOR,
   };

   VkPipelineDynamicStateCreateInfo dynamic_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = ARRAY_SIZE(dynamic_states),
      .pDynamicStates = dynamic_states,
   };

   VkGraphicsPipelineCreateInfo graphics_pipeline_info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext = &rendering_info,
      .stageCount = ARRAY_SIZE(stage_infos),
      .pStages = stage_infos,
      .pVertexInputState = &vertex_input_info,
      .pInputAssemblyState = &input_assembly_info,
      .pViewportState = &viewpoirt_info,
      .pRasterizationState = &rasterization_info,
      .pMultisampleState = &multisample_info,
      .pDepthStencilState = &depth_stencil_info,
      .pColorBlendState = &color_blend_info,
      .pDynamicState = &dynamic_info,
      .layout = app->renderer_pipeline_layout,
   };

   gamma_check_vk_result(
      vkCreateGraphicsPipelines(app->device, VK_NULL_HANDLE, 1, &graphics_pipeline_info, nullptr, &app->fill_pipeline));

   rasterization_info.polygonMode = VK_POLYGON_MODE_LINE;
   gamma_check_vk_result(vkCreateGraphicsPipelines(app->device, VK_NULL_HANDLE, 1, &graphics_pipeline_info, nullptr,
                                                   &app->wireframe_pipeline));

   input_assembly_info.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
   gamma_check_vk_result(vkCreateGraphicsPipelines(app->device, VK_NULL_HANDLE, 1, &graphics_pipeline_info, nullptr,
                                                   &app->lines_pipeline));

   rasterization_info.lineWidth = thick_line_width;
   input_assembly_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
   gamma_check_vk_result(vkCreateGraphicsPipelines(app->device, VK_NULL_HANDLE, 1, &graphics_pipeline_info, nullptr,
                                                   &app->thick_wireframe_pipeline));

   input_assembly_info.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
   gamma_check_vk_result(vkCreateGraphicsPipelines(app->device, VK_NULL_HANDLE, 1, &graphics_pipeline_info, nullptr,
                                                   &app->thick_lines_pipeline));

   app->finish_upload_memory();

   int64_t end_time = os_time_get_nano();
   fprintf(stderr, "gamma: Creating window state took %.2fms\n", (end_time - start_time) / 1000000.0);

   return 0;
}

void
gamma_app_finish(gamma_app *app)
{
   vkDeviceWaitIdle(app->device);

   vkDestroyPipeline(app->device, app->fill_pipeline, nullptr);
   vkDestroyPipeline(app->device, app->wireframe_pipeline, nullptr);
   vkDestroyPipelineLayout(app->device, app->renderer_pipeline_layout, nullptr);
   vkDestroyDescriptorSetLayout(app->device, app->renderer_set_layout, nullptr);

   vkDestroyCommandPool(app->device, app->staging_command_pool, nullptr);

   /* Cleanup anything potentially using Vulkan first. */
   app->open_files.clear();

   ImGui_ImplVulkan_Shutdown();
   ImGui_ImplSDL3_Shutdown();
   ImGui::DestroyContext();

   SDL_DestroyWindow(app->window);

   SDL_Quit();
}

void
gamma_app_resize(gamma_app *app, uint32_t width, uint32_t height)
{
   ImGui_ImplVulkan_SetMinImageCount(2);
   ImGui_ImplVulkanH_CreateOrResizeWindow(app->instance, app->pdev, app->device, &app->imgui_window,
                                          app->queue_family_index, nullptr, width, height, 2, 0);
}

static SDLCALL void
gamma_open_file_cb(void *userdata, const char *const *filelist, int filter)
{
   gamma_app *app = (gamma_app *)userdata;

   if (!filelist)
      return;

   for (uint32_t i = 0; filelist[i]; i++) {
      /* O(N^2) should be fine here. Focus the file instead of opening it again if it was already opened. */
      bool already_opened = false;
      for (const auto &view : app->open_files) {
         if (view->path == filelist[i]) {
            already_opened = true;
            view->ui.request_focus = true;
            break;
         }
      }
      if (!already_opened)
         app->open_files.push_back(gamma_create_file_view(app, filelist[i]));
   }
}

static const SDL_DialogFileFilter gamma_file_filters[] = {
   {"gamma file", "gamma"},
};

bool
gamma_app_run(gamma_app *app)
{
   if (!app->last_run_time_valid) {
      app->last_run_time = std::chrono::high_resolution_clock::now();
      app->last_run_time_valid = true;
   }
   std::chrono::high_resolution_clock::time_point now = std::chrono::high_resolution_clock::now();
   uint64_t dt_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now - app->last_run_time).count();
   app->dt = dt_ns / 1000000000.0;
   app->last_run_time = now;

   gamma_vec3 selection_colors[] = {
      {228.0 / 255.0, 3.0 / 255.0, 3.0 / 255.0},   {255.0 / 255.0, 140.0 / 255.0, 0.0 / 255.0},
      {255.0 / 255.0, 237.0 / 255.0, 0.0 / 255.0}, {0.0 / 255.0, 128.0 / 255.0, 38.0 / 255.0},
      {0.0 / 255.0, 76.0 / 255.0, 255.0 / 255.0},  {115.0 / 255.0, 41.0 / 255.0, 130.0 / 255.0},
   };
   float selection_color = app->t * 6.0;
   float selection_color_fract = selection_color - (uint64_t)selection_color;
   app->selection_color.x =
      selection_colors[((uint64_t)selection_color + 0) % ARRAY_SIZE(selection_colors)].x * (1 - selection_color_fract) +
      selection_colors[((uint64_t)selection_color + 1) % ARRAY_SIZE(selection_colors)].x * selection_color_fract;
   app->selection_color.y =
      selection_colors[((uint64_t)selection_color + 0) % ARRAY_SIZE(selection_colors)].y * (1 - selection_color_fract) +
      selection_colors[((uint64_t)selection_color + 1) % ARRAY_SIZE(selection_colors)].y * selection_color_fract;
   app->selection_color.z =
      selection_colors[((uint64_t)selection_color + 0) % ARRAY_SIZE(selection_colors)].z * (1 - selection_color_fract) +
      selection_colors[((uint64_t)selection_color + 1) % ARRAY_SIZE(selection_colors)].z * selection_color_fract;

   ImGuiKeyChord exit_chord = ImGuiMod_Ctrl | ImGuiKey_Q;
   if (ImGui::Shortcut(exit_chord, ImGuiInputFlags_RouteGlobal))
      return true;

   ImGuiKeyChord open_chord = ImGuiMod_Ctrl | ImGuiKey_O;
   if (ImGui::Shortcut(open_chord, ImGuiInputFlags_RouteGlobal)) {
      SDL_ShowOpenFileDialog(gamma_open_file_cb, app, app->window, gamma_file_filters, ARRAY_SIZE(gamma_file_filters),
                             nullptr, false);
   }

   if (ImGui::BeginMainMenuBar()) {
      if (ImGui::BeginMenu("File")) {
         if (ImGui::MenuItem("Open", ImGui::GetKeyChordName(open_chord))) {
            SDL_ShowOpenFileDialog(gamma_open_file_cb, app, app->window, gamma_file_filters,
                                   ARRAY_SIZE(gamma_file_filters), nullptr, false);
         }
         if (ImGui::MenuItem("Exit", ImGui::GetKeyChordName(exit_chord)))
            return true;
         ImGui::EndMenu();
      }

      uint32_t framerate = 0;
      if (app->dt != 0)
         framerate = (uint32_t)(1 / app->dt);

      char tmp[32];
      sprintf(tmp, "%u fps", framerate);

      ImVec2 framerate_text_size = ImGui::CalcTextSize(tmp);
      ImGui::SameLine(ImGui::GetWindowWidth() - framerate_text_size.x - 10);
      ImGui::Text("%s", tmp);

      ImGui::EndMainMenuBar();
   }

   ImGuiWindowClass top_level_class;
   top_level_class.ClassId = ImHashStr("top_level_class");
   top_level_class.DockingAllowUnclassed = false;

   ImGuiID root_dockspace_id = ImGui::GetID("root_dockspace");
   ImGui::DockSpaceOverViewport(root_dockspace_id, nullptr, ImGuiDockNodeFlags_NoWindowMenuButton, &top_level_class);

   for (const auto &view : app->open_files) {
      ImGui::SetNextWindowClass(&top_level_class);
      ImGui::SetNextWindowDockID(root_dockspace_id, ImGuiCond_FirstUseEver);

      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
      ImGui::Begin(view->path.filename().c_str());
      ImGui::PopStyleVar();

      view->run();

      ImGui::End();
   }

   app->t += app->dt;

   return false;
}

void *
gamma_app::upload_memory(std::shared_ptr<gamma_backed_buffer> dst)
{
   std::shared_ptr<gamma_backed_buffer> staging_buffer = gamma_create_backed_buffer(
      this, dst->size, gamma_memory_type_host_visible, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
   pending_staging_buffers.push_back({staging_buffer, dst});
   return staging_buffer->map;
}

void
gamma_app::flush_upload_memory()
{
   if (pending_staging_buffers.empty())
      return;

   assert(!staging_command_buffer);

   VkCommandBufferAllocateInfo command_buffer_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = staging_command_pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1,
   };
   gamma_check_vk_result(vkAllocateCommandBuffers(device, &command_buffer_info, &staging_command_buffer));

   VkCommandBufferBeginInfo begin_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
   };
   gamma_check_vk_result(vkBeginCommandBuffer(staging_command_buffer, &begin_info));

   for (const auto &staging_buffer : pending_staging_buffers) {
      VkBufferCopy copy = {
         .size = staging_buffer.first->size,
      };
      vkCmdCopyBuffer(staging_command_buffer, staging_buffer.first->buffer, staging_buffer.second->buffer, 1, &copy);
   }

   gamma_check_vk_result(vkEndCommandBuffer(staging_command_buffer));

   VkSubmitInfo submit_info = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .commandBufferCount = 1,
      .pCommandBuffers = &staging_command_buffer,
   };
   gamma_check_vk_result(vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE));
}

void
gamma_app::finish_upload_memory()
{
   if (!staging_command_buffer)
      return;

   gamma_check_vk_result(vkQueueWaitIdle(queue));

   vkFreeCommandBuffers(device, staging_command_pool, 1, &staging_command_buffer);
   staging_command_buffer = VK_NULL_HANDLE;

   pending_staging_buffers.clear();
}
