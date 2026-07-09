/*
 * Copyright © 2026 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include <cstdint>

#include "gamma_app.h"
#include "gamma_file_view.h"
#include "gamma_util.h"

#include "backends/imgui_impl_sdl3.h"
#include "vulkan/vulkan_core.h"

int
main(int argc, char **argv)
{
   gamma_app app = gamma_app();
   int ret = gamma_app_init(&app);
   if (ret)
      return ret;

   for (int i = 1; i < argc; i++)
      app.open_files.push_back(gamma_create_file_view(&app, argv[i]));

   VkResult result;
   bool needs_resize = true;
   bool done = false;
   while (!done) {
      SDL_Event event;

      while (SDL_PollEvent(&event)) {
         ImGui_ImplSDL3_ProcessEvent(&event);
         if (event.type == SDL_EVENT_QUIT)
            done = true;
         if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(app.window))
            done = true;
      }

      if (SDL_GetWindowFlags(app.window) & SDL_WINDOW_MINIMIZED) {
         SDL_Delay(10);
         continue;
      }

      int width, height;
      SDL_GetWindowSize(app.window, &width, &height);
      if (app.imgui_window.Width != width || app.imgui_window.Height != height)
         needs_resize = true;

      if (needs_resize) {
         gamma_app_resize(&app, width, height);
         needs_resize = false;
         continue;
      }

      VkSemaphore image_acquired_semaphore =
         app.imgui_window.FrameSemaphores[app.imgui_window.SemaphoreIndex].ImageAcquiredSemaphore;
      VkSemaphore render_complete_semaphore =
         app.imgui_window.FrameSemaphores[app.imgui_window.SemaphoreIndex].RenderCompleteSemaphore;
      result = vkAcquireNextImageKHR(app.device, app.imgui_window.Swapchain, UINT64_MAX, image_acquired_semaphore,
                                     VK_NULL_HANDLE, &app.imgui_window.FrameIndex);
      if (result != VK_SUCCESS) {
         if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            needs_resize = true;
            continue;
         }
         gamma_check_vk_result(result);
      }

      ImGui_ImplVulkanH_Frame *fd = &app.imgui_window.Frames[app.imgui_window.FrameIndex];
      gamma_check_vk_result(vkWaitForFences(app.device, 1, &fd->Fence, VK_TRUE, UINT64_MAX));
      gamma_check_vk_result(vkResetFences(app.device, 1, &fd->Fence));

      gamma_check_vk_result(vkResetCommandPool(app.device, fd->CommandPool, 0));

      VkCommandBufferBeginInfo command_buffer_begin_info = {
         .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
         .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
      };
      gamma_check_vk_result(vkBeginCommandBuffer(fd->CommandBuffer, &command_buffer_begin_info));

      ImGui_ImplVulkan_NewFrame();
      ImGui_ImplSDL3_NewFrame();
      ImGui::NewFrame();

      app.command_buffer = fd->CommandBuffer;
      if (gamma_app_run(&app))
         break;

      ImGui::Render();

      ImDrawData *draw_data = ImGui::GetDrawData();

      VkRenderPassBeginInfo render_pass_begin_info = {
         .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
         .renderPass = app.imgui_window.RenderPass,
         .framebuffer = fd->Framebuffer,
         .renderArea =
            {
               .extent =
                  {
                     .width = (uint32_t)app.imgui_window.Width,
                     .height = (uint32_t)app.imgui_window.Height,
                  },
            },
         .clearValueCount = 1,
         .pClearValues = &app.imgui_window.ClearValue,
      };
      vkCmdBeginRenderPass(fd->CommandBuffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

      ImGui_ImplVulkan_RenderDrawData(draw_data, fd->CommandBuffer);

      vkCmdEndRenderPass(fd->CommandBuffer);

      gamma_check_vk_result(vkEndCommandBuffer(fd->CommandBuffer));

      VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      VkSubmitInfo submit_info = {
         .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
         .waitSemaphoreCount = 1,
         .pWaitSemaphores = &image_acquired_semaphore,
         .pWaitDstStageMask = &wait_stage,
         .commandBufferCount = 1,
         .pCommandBuffers = &fd->CommandBuffer,
         .signalSemaphoreCount = 1,
         .pSignalSemaphores = &render_complete_semaphore,
      };
      gamma_check_vk_result(vkQueueSubmit(app.queue, 1, &submit_info, fd->Fence));

      VkPresentInfoKHR present_info = {
         .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
         .waitSemaphoreCount = 1,
         .pWaitSemaphores = &render_complete_semaphore,
         .swapchainCount = 1,
         .pSwapchains = &app.imgui_window.Swapchain,
         .pImageIndices = &app.imgui_window.FrameIndex,
      };
      result = vkQueuePresentKHR(app.queue, &present_info);
      if (result != VK_SUCCESS) {
         if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            needs_resize = true;
            continue;
         }
         gamma_check_vk_result(result);
      }
      app.imgui_window.SemaphoreIndex = (app.imgui_window.SemaphoreIndex + 1) % app.imgui_window.SemaphoreCount;
   }

   gamma_app_finish(&app);

   return 0;
}
