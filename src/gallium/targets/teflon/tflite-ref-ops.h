/*
 * Copyright (c) 2026 Arm, Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef TFLITE_REF_OPS_H
#define TFLITE_REF_OPS_H

#include "tensorflow/lite/c/c_api.h"

void tflite_register_ref_ops(TfLiteInterpreterOptions *options);

#endif /* TFLITE_REF_OPS_H */
