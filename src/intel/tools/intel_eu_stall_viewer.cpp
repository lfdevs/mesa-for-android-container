/* Copyright © 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <getopt.h>
#include <sys/stat.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <ostream>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "util/list.h"
#include "util/macros.h"
#include "util/sparse_array.h"

#include "imgui/imgui.h"
#include "imgui/intel_imgui.h"

thread_local ImGuiContext* __MesaImGui;

enum stall_type {
   STALL_TYPE_TDR,
   STALL_TYPE_OTHER,
   STALL_TYPE_CONTROL,
   STALL_TYPE_PIPESTALL,
   STALL_TYPE_SEND,
   STALL_TYPE_DIST_ACC,
   STALL_TYPE_SBID,
   STALL_TYPE_SYNC,
   STALL_TYPE_INST_FETCH,
   STALL_TYPE_ACTIVE,
   STALL_TYPE_SUM,

   STALL_TYPE_COUNT,
};

const char *stall_type_names[STALL_TYPE_COUNT] = {
   "tdr",
   "other",
   "control",
   "pipestall",
   "send",
   "dist acc",
   "sbid",
   "sync",
   "inst fetch",
   "active",
   "sum",
};

struct stall {
   uint64_t types[STALL_TYPE_COUNT];
};

stall merge_stalls(const stall &a, const stall &b)
{
   struct stall r = {};
   for (uint32_t i = 0; i < STALL_TYPE_COUNT; i++)
      r.types[i] = a.types[i] + b.types[i];
   return r;
}

class shader {
private:
   std::string filename_;
   std::vector<std::tuple<uint64_t, std::string, std::string, stall>> instructions_;
   uint64_t start_;
   uint64_t end_;
   uint64_t hits_;
   stall max_hits_;

   bool visible_;
   int stall_type_;

public:
   shader(shader *other)
      : filename_(other->filename_)
      , instructions_(other->instructions_)
      , start_(other->start_)
      , end_(other->end_)
      , hits_(other->hits_)
      , max_hits_(other->max_hits_)
      , visible_(other->visible_)
      , stall_type_(other->stall_type_)
   {
   }

   shader(const std::filesystem::path &path)
      : filename_(path.string())
      , start_(UINT64_MAX)
      , end_(0)
      , hits_(0)
      , max_hits_()
      , visible_(false)
      , stall_type_(STALL_TYPE_SUM)
   {
      std::ifstream ifs(path.string());

      std::regex re("(0x[0-9a-fA-F]+):[ ]+(.*)");
      std::regex desc_re("[ ]+(.*MsgDesc.*)");
      std::smatch m;

      std::string line;
      while (getline(ifs, line)) {
         if (std::regex_match(line, m, re)) {
            uint64_t offset = std::stoul(m[1], NULL, 16);
            std::string inst = m[2];
            instructions_.push_back(std::make_tuple(offset, inst, "", (stall){}));
            start_ = std::min(offset, start_);
            end_ = std::max(offset, end_);
         } else if (std::regex_match(line, m, desc_re)) {
            std::get<2>(instructions_.back()) = m[1];
         } else {
            instructions_.push_back(std::make_tuple(UINT64_MAX, line, "", (stall){}));
         }
      }
   }

   const std::string name() const {
      return filename_;
   }

   uint64_t start() const {
      return start_;
   }

   uint64_t end() const {
      return end_;
   }

   uint64_t total_hits() const {
      return max_hits_.types[STALL_TYPE_SUM];
   }

   void hit(uint64_t offset, const struct stall &stall) {
      for (auto &inst : instructions_) {
         if (offset == std::get<0>(inst)) {
            std::get<3>(inst) = merge_stalls(std::get<3>(inst), stall);
            break;
         }
      }
      for (uint32_t i = 0; i < STALL_TYPE_COUNT; i++)
         max_hits_.types[i] = std::max(max_hits_.types[i], stall.types[i]);
   }

   void open() {
      visible_ = true;
   }

   void render() {
      if (!visible_)
         return;

      ImGui::SetNextWindowSize(ImVec2(400, 400), ImGuiCond_Appearing);
      ImGui::Begin(filename_.c_str(), &visible_);
      ImGui::Combo("Stall type", &stall_type_, stall_type_names, STALL_TYPE_COUNT);
      ImGui::BeginChild("shader instructions", ImVec2(ImGui::GetWindowWidth(), -FLT_MIN));

      if (ImGui::BeginTable("table1", 2, 0)) {
         float progress_ratio = std::min(0.10f, 100.0f / ImGui::GetWindowWidth());
         ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, progress_ratio);
         ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 1.0f - progress_ratio);
         for (const auto &inst : instructions_) {
            ImGui::TableNextRow();

            float inst_hotness =
               (double) std::get<3>(inst).types[stall_type_] /
               (double) std::max(max_hits_.types[stall_type_], 1ul);
            ImU32 row_bg_color = ImGui::GetColorU32(ImVec4(inst_hotness, 0.0f, 0.0f, 0.5f));
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, row_bg_color);

            ImGui::TableNextColumn();
            ImGui::ProgressBar(inst_hotness);

            // Fill cells
            ImGui::TableNextColumn();
            ImGui::Text("%s %s", std::get<1>(inst).c_str(), std::get<2>(inst).c_str());
         }
         ImGui::EndTable();
      }

      ImGui::EndChild();
      ImGui::End();
   }
};

static std::vector<std::shared_ptr<shader>> shaders;
static uint64_t max_shader_hit = 0;

static void draw_ui(void)
{
   static ImGuiTextFilter shader_filter;

   ImGui::SetNextWindowSize(ImVec2(400, 800), ImGuiCond_FirstUseEver);
   ImGui::Begin("Shaders");
   shader_filter.Draw("Shader filter");

   ImGui::BeginChild(ImGui::GetID("shader list:"));
   for (auto shader : shaders) {
      if (!shader_filter.PassFilter(shader->name().c_str()))
         continue;
      float shader_hotness =
         (double) shader->total_hits() /
         (double) max_shader_hit;
      ImU32 button_color = ImGui::GetColorU32(ImVec4(shader_hotness, 0.0f, 0.0f, 0.5f));
      ImGui::PushStyleColor(ImGuiCol_Button, button_color);
      if (ImGui::Button(shader->name().c_str())) {
         shader->open();
      }
      ImGui::PopStyleColor();
   }
   ImGui::EndChild();
   ImGui::End();

   for (auto shader : shaders) {
      shader->render();
   }
}

static void
print_help(const char *progname, FILE *file)
{
   fprintf(file,
           "Usage: %s --shaders\n"
           "\n"
           "    -s, --shaders directory    Directory with shader dumps\n"
           "    -c, --stall-csv            EU stall file generated by intel_monitor\n"
           "    -h, --help                 Print this screen\n"
           , progname);
}

int
main(int argc, char *argv[])
{
   int c, i;
   bool help = false;
   const char *shaders_directory = NULL, *stall_csv_filename;
   const struct option aubinator_opts[] = {
      { "shaders",       required_argument, NULL,                          's'  },
      { "stall-csv",     required_argument, NULL,                          'c'  },
      { "help",          no_argument,       (int *) &help,                 true },
      { NULL,            0,                 NULL,                          0    },
   };

   i = 0;
   while ((c = getopt_long(argc, argv, "c:hs:", aubinator_opts, &i)) != -1) {
      switch (c) {
      case 's':
         shaders_directory = optarg;
         break;
      case 'c':
         stall_csv_filename = optarg;
         break;
      case 'h':
         print_help(argv[0], stderr);
         return EXIT_SUCCESS;
      default:
         break;
      }
   }

   if (!shaders_directory || !stall_csv_filename) {
      print_help(argv[0], stderr);
      return EXIT_FAILURE;
   }

   struct util_sparse_array shader_map;
   util_sparse_array_init(&shader_map, sizeof(shader *), 512);

   /* Load all the shaders */
   for (const auto& entry : std::filesystem::directory_iterator(shaders_directory)) {
      std::filesystem::path outfilename = entry.path();
      std::string outfilename_str = outfilename.string();
      const char* path = outfilename_str.c_str();
      struct stat sb;

      if (stat(path, &sb) == 0 && !(sb.st_mode & S_IFDIR)) {
         shaders.push_back(std::make_shared<shader>(new shader(path)));

         for (uint64_t i = shaders.back()->start(); i <= shaders.back()->end(); i += 64) {
            shader **p_shader = (shader **)util_sparse_array_get(&shader_map, i / 64);
            *p_shader = shaders.back().get();
         }
      }
   }

   {
      std::string line;
      std::ifstream ifs(stall_csv_filename);
      getline(ifs, line);
      while (getline(ifs, line)) {
         std::istringstream ss(line);

         uint64_t offset;
         stall stall = {};
         std::string s;
         std::getline(ss, s, ','); offset = std::stoul(s, NULL, 16);
         for (uint32_t i = 0; i < STALL_TYPE_COUNT; i++) {
            std::getline(ss, s, ',');
            stall.types[i] = std::stoul(s);
         }

         /* Find the shader associated with this hit */
         shader **p_shader = (shader **)util_sparse_array_get(&shader_map, offset / 64);
         if (*p_shader != NULL)
            (*p_shader)->hit(offset, stall);
      }
   }

   max_shader_hit = 0;
   for (const auto &shader : shaders)
      max_shader_hit = std::max(shader->total_hits(), max_shader_hit);

   intel_imgui_ui("Intel EU Stall Viewer", draw_ui);

   return EXIT_SUCCESS;
}
