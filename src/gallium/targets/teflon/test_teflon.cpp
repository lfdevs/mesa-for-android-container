/*
 * Copyright (c) 2023-2024 Tomeu Vizoso <tomeu@tomeuvizoso.net>
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <cstdio>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <sys/mman.h>

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include "tensorflow/lite/c/c_api.h"
#include "test_executor.h"

#include "util/os_misc.h"

#define TOLERANCE 6

static void
test_model(void *buf, size_t buf_size, std::string cache_dir, unsigned tolerance)
{
   void **input = NULL;
   size_t num_inputs;
   void **cpu_output;
   size_t *output_sizes;
   TfLiteType *output_types;
   size_t num_outputs;
   void **npu_output;

   TfLiteModel *model = TfLiteModelCreate(buf, buf_size);
   assert(model);

   run_model(model, EXECUTOR_CPU, &input, &num_inputs, &cpu_output, &output_sizes, &output_types, &num_outputs, cache_dir);
   run_model(model, EXECUTOR_NPU, &input, &num_inputs, &npu_output, &output_sizes, &output_types, &num_outputs, cache_dir);

   const char *dump_output = os_get_option("TEFLON_DUMP_OUTPUT");
   if (dump_output && atoi(dump_output) == 1) {
      for (unsigned i = 0; i < num_outputs; i++) {
         char name[250];
         int fd;
         unsigned size = output_sizes[i];

         if (output_types[i] == kTfLiteFloat32)
            size *= 4;

         sprintf(name, "out-%d.bin", i);
         fd = open(name, O_RDWR | O_CREAT | O_TRUNC, S_IRWXU);
         write(fd, npu_output[i], size);
         close(fd);

         sprintf(name, "cpu-out-%d.bin", i);
         fd = open(name, O_RDWR | O_CREAT | O_TRUNC, S_IRWXU);
         write(fd, cpu_output[i], size);
         close(fd);
      }
   }

   for (size_t i = 0; i < num_outputs; i++) {
      for (size_t j = 0; j < output_sizes[i]; j++) {
         switch (output_types[i]) {
         case kTfLiteFloat32: {
            float *cpu = ((float **)cpu_output)[i];
            float *npu = ((float **)npu_output)[i];
            if (abs(cpu[j] - npu[j]) > tolerance / 33.0) {
               std::cout << "CPU: ";
               for (int k = 0; k < std::min(int(output_sizes[i]), 24); k++)
                  std::cout << std::setfill('0') << std::setw(6) << cpu[k] << " ";
               std::cout << "\n";
               std::cout << "NPU: ";
               for (int k = 0; k < std::min(int(output_sizes[i]), 24); k++)
                  std::cout << std::setfill('0') << std::setw(6) << npu[k] << " ";
               std::cout << "\n";

               FAIL() << "Output at " << j << " from the NPU (" << std::setfill('0') << std::setw(2) << npu[j] << ") doesn't match that from the CPU (" << std::setfill('0') << std::setw(2) << cpu[j] << ").";
            }
            break;
         }
         case kTfLiteInt8: {
            int8_t *cpu = ((int8_t **)cpu_output)[i];
            int8_t *npu = ((int8_t **)npu_output)[i];
            if (abs(cpu[j] - npu[j]) > tolerance) {
               std::cout << "CPU: ";
               for (int k = 0; k < std::min(int(output_sizes[i]), 24); k++)
                  std::cout << std::setfill('0') << std::setw(2) << std::hex << int(cpu[k] & 0xff) << " ";
               std::cout << "\n";
               std::cout << "NPU: ";
               for (int k = 0; k < std::min(int(output_sizes[i]), 24); k++)
                  std::cout << std::setfill('0') << std::setw(2) << std::hex << int(npu[k] & 0xff) << " ";
               std::cout << "\n";

               FAIL() << "Output at " << j << " from the NPU (" << std::setfill('0') << std::setw(2) << std::hex << int(npu[j] & 0xff) << ") doesn't match that from the CPU (" << std::setfill('0') << std::setw(2) << std::hex << int(cpu[j] & 0xff) << ").";
            }
            break;
         }
         case kTfLiteUInt8: {
            uint8_t *cpu = ((uint8_t **)cpu_output)[i];
            uint8_t *npu = ((uint8_t **)npu_output)[i];
            if (abs(cpu[j] - npu[j]) > tolerance) {
               std::cout << "CPU: ";
               for (int k = 0; k < std::min(int(output_sizes[i]), 24); k++)
                  std::cout << std::setfill('0') << std::setw(2) << std::hex << int(cpu[k]) << " ";
               std::cout << "\n";
               std::cout << "NPU: ";
               for (int k = 0; k < std::min(int(output_sizes[i]), 24); k++)
                  std::cout << std::setfill('0') << std::setw(2) << std::hex << int(npu[k]) << " ";
               std::cout << "\n";

               FAIL() << "Output at " << j << " from the NPU (" << std::setfill('0') << std::setw(2) << std::hex << int(npu[j]) << ") doesn't match that from the CPU (" << std::setfill('0') << std::setw(2) << std::hex << int(cpu[j]) << ").";
            }
            break;
         }
         case kTfLiteInt16: {
            int16_t *cpu = ((int16_t **)cpu_output)[i];
            int16_t *npu = ((int16_t **)npu_output)[i];
            if (abs(cpu[j] - npu[j]) > tolerance) {
               std::cout << "CPU: ";
               for (int k = 0; k < std::min(int(output_sizes[i]), 24); k++)
                  std::cout << std::setfill('0') << std::setw(4) << std::hex << int(cpu[k] & 0xffff) << " ";
               std::cout << "\n";
               std::cout << "NPU: ";
               for (int k = 0; k < std::min(int(output_sizes[i]), 24); k++)
                  std::cout << std::setfill('0') << std::setw(4) << std::hex << int(npu[k] & 0xffff) << " ";
               std::cout << "\n";

               FAIL() << "Output at " << j << " from the NPU (" << std::setfill('0') << std::setw(4) << std::hex << int(npu[j] & 0xffff) << ") doesn't match that from the CPU (" << std::setfill('0') << std::setw(4) << std::hex << int(cpu[j] & 0xffff) << ").";
            }
            break;
         }
         case kTfLiteUInt16: {
            uint16_t *cpu = ((uint16_t **)cpu_output)[i];
            uint16_t *npu = ((uint16_t **)npu_output)[i];
            if (abs(cpu[j] - npu[j]) > tolerance) {
               std::cout << "CPU: ";
               for (int k = 0; k < std::min(int(output_sizes[i]), 24); k++)
                  std::cout << std::setfill('0') << std::setw(4) << std::hex << int(cpu[k]) << " ";
               std::cout << "\n";
               std::cout << "NPU: ";
               for (int k = 0; k < std::min(int(output_sizes[i]), 24); k++)
                  std::cout << std::setfill('0') << std::setw(4) << std::hex << int(npu[k]) << " ";
               std::cout << "\n";

               FAIL() << "Output at " << j << " from the NPU (" << std::setfill('0') << std::setw(4) << std::hex << int(npu[j]) << ") doesn't match that from the CPU (" << std::setfill('0') << std::setw(4) << std::hex << int(cpu[j]) << ").";
            }
            break;
         }
         case kTfLiteInt32: {
            int32_t *cpu = ((int32_t **)cpu_output)[i];
            int32_t *npu = ((int32_t **)npu_output)[i];
            if (abs(cpu[j] - npu[j]) > tolerance) {
               std::cout << "CPU: ";
               for (int k = 0; k < std::min(int(output_sizes[i]), 24); k++)
                  std::cout << std::setfill('0') << std::setw(8) << std::hex << int(cpu[k] & 0xffffffff) << " ";
               std::cout << "\n";
               std::cout << "NPU: ";
               for (int k = 0; k < std::min(int(output_sizes[i]), 24); k++)
                  std::cout << std::setfill('0') << std::setw(8) << std::hex << int(npu[k] & 0xffffffff) << " ";
               std::cout << "\n";

               FAIL() << "Output at " << j << " from the NPU (" << std::setfill('0') << std::setw(4) << std::hex << int(npu[j] & 0xffffffff) << ") doesn't match that from the CPU (" << std::setfill('0') << std::setw(8) << std::hex << int(cpu[j] & 0xffffffff) << ").";
            }
            break;
         }
         case kTfLiteUInt32: {
            uint16_t *cpu = ((uint16_t **)cpu_output)[i];
            uint16_t *npu = ((uint16_t **)npu_output)[i];
            if (abs(cpu[j] - npu[j]) > tolerance) {
               std::cout << "CPU: ";
               for (int k = 0; k < std::min(int(output_sizes[i]), 24); k++)
                  std::cout << std::setfill('0') << std::setw(8) << std::hex << int(cpu[k]) << " ";
               std::cout << "\n";
               std::cout << "NPU: ";
               for (int k = 0; k < std::min(int(output_sizes[i]), 24); k++)
                  std::cout << std::setfill('0') << std::setw(8) << std::hex << int(npu[k]) << " ";
               std::cout << "\n";

               FAIL() << "Output at " << j << " from the NPU (" << std::setfill('0') << std::setw(8) << std::hex << int(npu[j]) << ") doesn't match that from the CPU (" << std::setfill('0') << std::setw(8) << std::hex << int(cpu[j]) << ").";
            }
            break;
         }
         default:
            FAIL() << "Unsupported data type for output tensor";
         }
      }
   }

   for (size_t i = 0; i < num_inputs; i++)
      free(input[i]);
   free(input);

   for (size_t i = 0; i < num_outputs; i++)
      free(cpu_output[i]);
   free(cpu_output);

   for (size_t i = 0; i < num_outputs; i++)
      free(npu_output[i]);
   free(npu_output);

   free(output_sizes);
   free(output_types);

   TfLiteModelDelete(model);
}

static void
test_model_file(std::string file_name, unsigned tolerance, bool use_cache)
{
   std::ostringstream cache_dir;

   if (use_cache) {
      auto path = std::filesystem::path(file_name);
      cache_dir << "/var/cache/teflon_tests/";
      cache_dir << path.parent_path().filename().string();
      cache_dir << "_";
      cache_dir << path.stem().string();
   }

   srand(4);

   struct stat sb;
   int model_fd = open(file_name.c_str(), O_RDONLY);
   fstat(model_fd, &sb);
   void *model_data = mmap(0, sb.st_size, PROT_READ, MAP_PRIVATE, model_fd, 0);
   test_model(model_data, sb.st_size, cache_dir.str(), tolerance);
   munmap(model_data, sb.st_size);
   close(model_fd);
}

class Models : public testing::TestWithParam<std::string> {};

TEST_P(Models, Op)
{
   std::ostringstream file_path;
   auto test_name = GetParam();
   test_name.replace(test_name.find("_"), 1, "/");
   assert(os_get_option("TEFLON_TEST_DATA"));
   file_path << os_get_option("TEFLON_TEST_DATA") << "/models/" << test_name << ".tflite";

   test_model_file(file_path.str(), TOLERANCE, true);
}

std::vector<std::string>
get_model_files(void)
{
   assert(os_get_option("TEFLON_TEST_DATA"));
   std::stringstream dir;
   dir << os_get_option("TEFLON_TEST_DATA") << "/models";

   std::vector<std::string> paths;
   std::filesystem::recursive_directory_iterator b(dir.str());
   for (auto const &f : b) {
      if (f.path().extension() != ".tflite")
         continue;

      std::stringstream path;
      path << f.path().parent_path().filename().string();
      path << "_" << f.path().stem().string();
      paths.push_back(path.str());
   }

   std::sort(paths.begin(), paths.end());

   return paths;
}

static inline std::string
ModelsTestCaseName(
   const testing::TestParamInfo<std::string> &info)
{
   return info.param;
}

INSTANTIATE_TEST_SUITE_P(
   , Models,
   ::testing::ValuesIn(get_model_files()),
   ModelsTestCaseName);

int
main(int argc, char **argv)
{
   if (argc > 1 && !strcmp(argv[1], "run_model")) {
      test_model_file(std::string(argv[2]), TOLERANCE, false);
   } else {
      testing::InitGoogleTest(&argc, argv);
      return RUN_ALL_TESTS();
   }
}
