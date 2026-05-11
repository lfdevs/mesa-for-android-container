/* Copyright © 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>

#include <GLFW/glfw3.h>

#include "imgui/imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "intel_imgui.h"

static void glfw_error_callback(int error, const char* description)
{
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

extern "C" void
intel_imgui_ui(const char *window_name, intel_imgui_draw_cb callback)
{
   glfwSetErrorCallback(glfw_error_callback);
   if (!glfwInit())
      return;

   const char* glsl_version = "#version 130";
   glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
   glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

   float main_scale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor()); // Valid on GLFW 3.3+ only
   GLFWwindow* window = glfwCreateWindow((int)(1280 * main_scale), (int)(800 * main_scale), window_name, nullptr, nullptr);
   if (window == nullptr)
      return;
   glfwMakeContextCurrent(window);
   glfwSwapInterval(1);

   IMGUI_CHECKVERSION();
   ImGui::CreateContext();
   ImGuiIO& io = ImGui::GetIO(); (void)io;
   io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

   ImGui::StyleColorsDark();

   ImGuiStyle& style = ImGui::GetStyle();
   style.ScaleAllSizes(main_scale);
   style.FontScaleDpi = main_scale;

   ImGui_ImplGlfw_InitForOpenGL(window, true);
   ImGui_ImplOpenGL3_Init(glsl_version);

   ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

   while (!glfwWindowShouldClose(window)) {
      glfwPollEvents();
      if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0)
      {
         ImGui_ImplGlfw_Sleep(10);
         continue;
      }

      /* Start the Dear ImGui frame */
      ImGui_ImplOpenGL3_NewFrame();
      ImGui_ImplGlfw_NewFrame();
      ImGui::NewFrame();

      /* Callback to draw the UI */
      callback();

      /* Render */
      ImGui::Render();
      int display_w, display_h;
      glfwGetFramebufferSize(window, &display_w, &display_h);
      glViewport(0, 0, display_w, display_h);
      glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
      glClear(GL_COLOR_BUFFER_BIT);
      ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

      glfwSwapBuffers(window);
   }

   /* Cleanup */
   ImGui_ImplOpenGL3_Shutdown();
   ImGui_ImplGlfw_Shutdown();
   ImGui::DestroyContext();

   glfwDestroyWindow(window);
   glfwTerminate();
}
