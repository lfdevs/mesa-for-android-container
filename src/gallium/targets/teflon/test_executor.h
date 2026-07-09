/*
 * Copyright (c) 2023-2024 Tomeu Vizoso <tomeu@tomeuvizoso.net>
 * SPDX-License-Identifier: MIT
 */

#include <cstddef>
enum executor {
   EXECUTOR_CPU,
   EXECUTOR_NPU,
};

struct TfLiteModel;

void run_model(TfLiteModel *model, enum executor executor, void ***input, size_t *num_inputs,
               void ***output, size_t **output_sizes, TfLiteType **output_types,
               size_t *num_outputs, std::string cache_dir);

bool cache_is_enabled(void);

void *read_buf(const char *path, size_t *buf_size);
