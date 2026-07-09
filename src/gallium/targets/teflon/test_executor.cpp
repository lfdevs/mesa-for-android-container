/*
 * Copyright (c) 2023-2024 Tomeu Vizoso <tomeu@tomeuvizoso.net>
 * SPDX-License-Identifier: MIT
 */

#include <cstring>
#include <cstdlib>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <stdio.h>
#include <type_traits>
#include <vector>
#include <gtest/gtest.h>

#include "tensorflow/lite/c/c_api.h"
#include "tensorflow/lite/c/c_api_experimental.h"
#include "tensorflow/lite/c/common.h"

#include <fcntl.h>
#include "test_executor.h"
#include "tflite-ref-ops.h"

#include "util/os_misc.h"

static float
randf(float min, float max)
{
   return ((max - min) * ((float)rand() / (float)RAND_MAX)) + min;
}

template<typename T>
static std::vector<T>
generate_random_values(const std::vector<int> &shape, T min, T max)
{
   size_t size = 1;
   for (int dim : shape)
      size *= dim;

   std::vector<T> result(size);
   if constexpr (std::is_integral<T>::value) {
      for (size_t i = 0; i < size; i++)
         result[i] = rand() % (max - min + 1) + min;
   } else if constexpr (std::is_floating_point<T>::value) {
      for (size_t i = 0; i < size; i++)
         result[i] = randf(min, max);
   }

   return result;
}

static void
tflite_error_cb(void *user_data, const char *format, va_list args)
{
   vfprintf(stderr, format, args);
}

TfLiteDelegate *(*tflite_plugin_create_delegate)(char **options_keys,
                                                 char **options_values,
                                                 size_t num_options,
                                                 void (*report_error)(const char *));

void (*tflite_plugin_destroy_delegate)(TfLiteDelegate *delegate);

static void
load_delegate()
{
   const char *delegate_path = os_get_option("TEFLON_TEST_DELEGATE");
   assert(delegate_path);

   void *delegate_lib = dlopen(delegate_path, RTLD_LAZY | RTLD_LOCAL);
   assert(delegate_lib);

   tflite_plugin_create_delegate = reinterpret_cast<TfLiteDelegate *(*)(char **options_keys,
                                                                        char **options_values,
                                                                        size_t num_options,
                                                                        void (*report_error)(const char *))>(
      dlsym(delegate_lib, "tflite_plugin_create_delegate"));
   assert(tflite_plugin_create_delegate);

   tflite_plugin_destroy_delegate = reinterpret_cast<void (*)(TfLiteDelegate *delegate)>(
      dlsym(delegate_lib, "tflite_plugin_destroy_delegate"));
   assert(tflite_plugin_destroy_delegate);
}

bool
cache_is_enabled(void)
{
   return os_get_option("TEFLON_ENABLE_CACHE");
}

void *
read_buf(const char *path, size_t *buf_size)
{
   FILE *f = fopen(path, "rb");
   if (f == NULL)
      return NULL;

   fseek(f, 0, SEEK_END);
   long fsize = ftell(f);
   fseek(f, 0, SEEK_SET);

   void *buf = malloc(fsize);
   fread(buf, fsize, 1, f);

   fclose(f);

   if (buf_size != NULL)
      *buf_size = fsize;

   return buf;
}

void
run_model(TfLiteModel *model, enum executor executor, void ***input, size_t *num_inputs,
          void ***output, size_t **output_sizes, TfLiteType **output_types,
          size_t *num_outputs, std::string cache_dir)
{
   TfLiteDelegate *delegate = NULL;
   TfLiteInterpreterOptions *options = TfLiteInterpreterOptionsCreate();

   tflite_register_ref_ops(options);

   if (executor == EXECUTOR_NPU) {
      load_delegate();
      delegate = tflite_plugin_create_delegate(NULL, NULL, 0, NULL);
      TfLiteInterpreterOptionsAddDelegate(options, delegate);
   }

   TfLiteInterpreterOptionsSetErrorReporter(options, tflite_error_cb, NULL);

   TfLiteInterpreter *interpreter = TfLiteInterpreterCreateWithSelectedOps(model, options);
   assert(interpreter);

   EXPECT_EQ(TfLiteInterpreterAllocateTensors(interpreter), kTfLiteOk);

   *num_inputs = TfLiteInterpreterGetInputTensorCount(interpreter);
   if (*input == NULL)
      *input = (void **)calloc(*num_inputs, sizeof(*input));
   for (unsigned i = 0; i < *num_inputs; i++) {
      TfLiteTensor *input_tensor = TfLiteInterpreterGetInputTensor(interpreter, i);
      std::ostringstream input_cache;
      input_cache << cache_dir << "/" << "input-" << i << ".data";

      if (input_tensor->allocation_type != kTfLiteArenaRw)
         continue;
      
      if ((*input)[i] == NULL) {
         if (cache_is_enabled())
            (*input)[i] = read_buf(input_cache.str().c_str(), NULL);
         if ((*input)[i] == NULL) {
            (*input)[i] = malloc(input_tensor->bytes);

            std::vector<int> shape;

            shape.resize(input_tensor->dims->size);
            for (int j = 0; j < input_tensor->dims->size; j++)
               shape[j] = input_tensor->dims->data[j];

            switch (input_tensor->type) {
            case kTfLiteFloat32: {
               std::vector<float> a = generate_random_values<float>(shape, -1.0f, 1.0f);
               memcpy((*input)[i], a.data(), input_tensor->bytes);
               break;
            }
            default: {
               std::vector<uint8_t> a = generate_random_values<uint8_t>(shape, 0, 255);
               memcpy((*input)[i], a.data(), input_tensor->bytes);
               break;
            }
            }

            if (cache_is_enabled()) {
               if (!cache_dir.empty() && !std::filesystem::exists(cache_dir))
                  std::filesystem::create_directory(cache_dir);

               std::ofstream file(input_cache.str().c_str(), std::ios::out | std::ios::binary);
               file.write(reinterpret_cast<const char *>((*input)[i]), input_tensor->bytes);
               file.close();
            }
         }
      }

      TfLiteTensorCopyFromBuffer(input_tensor, (*input)[i], input_tensor->bytes);
   }

   std::ostringstream output_cache;
   output_cache << cache_dir << "/" << "output-" << 0 << ".data";

   if (executor == EXECUTOR_NPU || !cache_is_enabled() || !std::filesystem::exists(output_cache.str())) {
      EXPECT_EQ(TfLiteInterpreterInvoke(interpreter), kTfLiteOk);
   }

   *num_outputs = TfLiteInterpreterGetOutputTensorCount(interpreter);
   *output = (void **)malloc(sizeof(*output) * *num_outputs);
   *output_sizes = (size_t *)malloc(sizeof(*output_sizes) * *num_outputs);
   *output_types = (TfLiteType *)malloc(sizeof(*output_types) * *num_outputs);
   for (unsigned i = 0; i < *num_outputs; i++) {
      const TfLiteTensor *output_tensor = TfLiteInterpreterGetOutputTensor(interpreter, i);
      output_cache.str("");
      output_cache << cache_dir << "/" << "output-" << i << ".data";
      (*output_types)[i] = output_tensor->type;

      if (executor == EXECUTOR_CPU && cache_is_enabled() && std::filesystem::exists(output_cache.str())) {
         (*output)[i] = read_buf(output_cache.str().c_str(), NULL);
      } else {
         (*output)[i] = malloc(output_tensor->bytes);
         EXPECT_EQ(TfLiteTensorCopyToBuffer(output_tensor, (*output)[i], output_tensor->bytes), kTfLiteOk);

         if (cache_is_enabled() && executor == EXECUTOR_CPU) {
            std::ofstream file = std::ofstream(output_cache.str().c_str(), std::ios::out | std::ios::binary);
            file.write(reinterpret_cast<const char *>((*output)[i]), output_tensor->bytes);
            file.close();
         }
      }

      switch (output_tensor->type) {
      case kTfLiteInt32:
      case kTfLiteUInt32:
      case kTfLiteFloat32: {
         (*output_sizes)[i] = output_tensor->bytes / 4;
         break;
      }
      case kTfLiteInt16:
      case kTfLiteUInt16: {
         (*output_sizes)[i] = output_tensor->bytes / 2;
         break;
      }
      default: {
         (*output_sizes)[i] = output_tensor->bytes;
         break;
      }
      }
   }

   TfLiteInterpreterDelete(interpreter);
   if (executor == EXECUTOR_NPU)
      tflite_plugin_destroy_delegate(delegate);
   TfLiteInterpreterOptionsDelete(options);
}
