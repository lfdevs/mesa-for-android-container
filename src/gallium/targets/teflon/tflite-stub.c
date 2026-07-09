/*
 * Copyright (c) 2023-2024 Tomeu Vizoso <tomeu@tomeuvizoso.net>
 * SPDX-License-Identifier: MIT
 *
 * Stub implementations of TensorFlow Lite C API.
 *
 */

#include "tensorflow/lite/c/c_api.h"
#include "tensorflow/lite/c/c_api_experimental.h"
#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/core/c/c_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#define TFLITE_STUB_NOT_IMPLEMENTED(func_name) \
   do { \
      fprintf(stderr, \
              "ERROR: TensorFlow Lite stub called: %s()\n" \
              "       TensorFlow Lite is not properly installed.\n" \
              "       This function has no real implementation.\n", \
              func_name); \
      fflush(stderr); \
      assert(!"TensorFlow Lite stub invoked - TFLite not installed"); \
   } while (0)

void
TfLiteInterpreterOptionsAddDelegate(TfLiteInterpreterOptions *options, TfLiteOpaqueDelegate *delegate)
{
   TFLITE_STUB_NOT_IMPLEMENTED(__func__);
}

void
TfLiteInterpreterOptionsAddBuiltinOp(TfLiteInterpreterOptions *options,
                                     TfLiteBuiltinOperator op,
                                     const TfLiteRegistration *registration,
                                     int32_t min_version,
                                     int32_t max_version)
{
   TFLITE_STUB_NOT_IMPLEMENTED(__func__);
}

void
TfLiteInterpreterOptionsAddCustomOp(TfLiteInterpreterOptions *options,
                                    const char *name,
                                    const TfLiteRegistration *registration,
                                    int32_t min_version,
                                    int32_t max_version)
{
   TFLITE_STUB_NOT_IMPLEMENTED(__func__);
}

void
TfLiteInterpreterOptionsSetErrorReporter(
   TfLiteInterpreterOptions *options,
   void (*reporter)(void *user_data, const char *format, va_list args),
   void *user_data)
{
   TFLITE_STUB_NOT_IMPLEMENTED(__func__);
}

TfLiteInterpreter *
TfLiteInterpreterCreate(
   const TfLiteModel *model,
   const TfLiteInterpreterOptions *optional_options)
{
   TFLITE_STUB_NOT_IMPLEMENTED(__func__);
   return NULL;
}

TfLiteInterpreter *
TfLiteInterpreterCreateWithSelectedOps(
   const TfLiteModel *model,
   const TfLiteInterpreterOptions *options)
{
   TFLITE_STUB_NOT_IMPLEMENTED(__func__);
   return NULL;
}

TfLiteStatus
TfLiteInterpreterAllocateTensors(TfLiteInterpreter *interpreter)
{
   TFLITE_STUB_NOT_IMPLEMENTED(__func__);
   return 0;
}

int32_t
TfLiteInterpreterGetInputTensorCount(const TfLiteInterpreter *interpreter)
{
   TFLITE_STUB_NOT_IMPLEMENTED(__func__);
   return 0;
}

TfLiteTensor *
TfLiteInterpreterGetInputTensor(const TfLiteInterpreter *interpreter, int32_t input_index)
{
   TFLITE_STUB_NOT_IMPLEMENTED(__func__);
   return NULL;
}

TfLiteStatus
TfLiteTensorCopyFromBuffer(TfLiteTensor *tensor,
                           const void *input_data,
                           size_t input_data_size)
{
   TFLITE_STUB_NOT_IMPLEMENTED(__func__);
   return 0;
}

TfLiteStatus
TfLiteInterpreterInvoke(TfLiteInterpreter *interpreter)
{
   TFLITE_STUB_NOT_IMPLEMENTED(__func__);
   return 0;
}

int32_t
TfLiteInterpreterGetOutputTensorCount(const TfLiteInterpreter *interpreter)
{
   TFLITE_STUB_NOT_IMPLEMENTED(__func__);
   return 0;
}

const TfLiteTensor *
TfLiteInterpreterGetOutputTensor(const TfLiteInterpreter *interpreter, int32_t output_index)
{
   TFLITE_STUB_NOT_IMPLEMENTED(__func__);
   return NULL;
}

TfLiteStatus
TfLiteTensorCopyToBuffer(const TfLiteTensor *tensor,
                         void *output_data,
                         size_t output_data_size)
{
   TFLITE_STUB_NOT_IMPLEMENTED(__func__);
   return 0;
}

void
TfLiteInterpreterDelete(TfLiteInterpreter *interpreter)
{
   TFLITE_STUB_NOT_IMPLEMENTED(__func__);
}

void
TfLiteInterpreterOptionsDelete(TfLiteInterpreterOptions *options)
{
   TFLITE_STUB_NOT_IMPLEMENTED(__func__);
}

TfLiteModel *
TfLiteModelCreate(const void *model_data, size_t model_size)
{
   TFLITE_STUB_NOT_IMPLEMENTED(__func__);
   return NULL;
}

void
TfLiteModelDelete(TfLiteModel *model)
{
   TFLITE_STUB_NOT_IMPLEMENTED(__func__);
}

/* FIXME: Why do we need to redeclare the prototype for this one here? */
TfLiteInterpreterOptions *TfLiteInterpreterOptionsCreate(void);

TfLiteInterpreterOptions *
TfLiteInterpreterOptionsCreate(void)
{
   TFLITE_STUB_NOT_IMPLEMENTED(__func__);
   return NULL;
}
