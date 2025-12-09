/*
 * Copyright (c) 2023-2024 Tomeu Vizoso <tomeu@tomeuvizoso.net>
 * SPDX-License-Identifier: MIT
 */

#ifndef H_ETNA_ML
#define H_ETNA_ML

#include "pipe/p_state.h"
#include "util/u_dynarray.h"
#include "util/macros.h"
#include "etnaviv_context.h"

#define MAX_CONFIG_BOS 4

enum etna_job_type {
    ETNA_JOB_TYPE_NN,
    ETNA_JOB_TYPE_TP,
    ETNA_JOB_TYPE_CONCAT, /* Fake operation, won't execute on HW. Hack will go away after the move to NIR. */
    ETNA_JOB_TYPE_SPLIT, /* Fake operation, won't execute on HW. Hack will go away after the move to NIR. */
    ETNA_JOB_TYPE_BYPASS, /* Fake operation, won't execute on HW. Hack will go away after the move to NIR. */
};

enum etna_ml_tp_type {
   ETNA_ML_TP_TRANSPOSE,
   ETNA_ML_TP_DETRANSPOSE,
   ETNA_ML_TP_RESHUFFLE,
   ETNA_ML_TP_PAD,
   ETNA_ML_TP_RELU,
   ETNA_ML_TP_ABSOLUTE,
   ETNA_ML_TP_LOGISTIC,
};

enum etna_ml_tensor_layout {
   ETNA_ML_LAYOUT_ANY = 0,
   ETNA_ML_LAYOUT_NHWC,
   ETNA_ML_LAYOUT_NCHW,
};

struct etna_ml_tensor {
   struct pipe_resource *resource;
   unsigned offset;
   unsigned size;
   enum etna_ml_tensor_layout exp_layout; /* expected */
   enum etna_ml_tensor_layout act_layout; /* actual */
};

struct etna_ml_subgraph {
   struct pipe_ml_subgraph base;

   struct util_dynarray operations;

   /* Indexed by tensor index */
   struct util_dynarray tensors; /* Contains struct etna_ml_tensor */
};

struct etna_vip_instruction {
   enum etna_job_type type;
   enum etna_ml_tp_type tp_type;

   struct etna_bo *configs[MAX_CONFIG_BOS];
   struct etna_bo *coefficients;
   struct etna_bo *pwl_lut;
   struct pipe_resource *input;
   unsigned input_offset;
   struct pipe_resource *output;
   unsigned output_offset;

   struct etna_bo *kernel;
};

#define MAX_TENSORS 10
struct etna_operation {
   struct list_head link;

   enum etna_job_type type;
   enum etna_ml_tp_type tp_type;

   bool addition;
   bool depthwise;
   bool pointwise;
   bool fully_connected;
   bool pooling_first_pixel;
   bool padding_same;
   bool relu;

   unsigned stride;

   unsigned input_tensors[MAX_TENSORS];
   unsigned input_count;
   unsigned input_tensor_sizes[MAX_TENSORS];

   /* The following apply to the first input tensor only */
   unsigned input_width;
   unsigned input_height;
   unsigned input_channels;
   uint8_t input_zero_point;
   float input_scale;

   unsigned output_tensors[MAX_TENSORS];
   unsigned output_count;
   unsigned output_tensor_sizes[MAX_TENSORS];

   /* The following apply to the first output tensor only */
   unsigned output_width;
   unsigned output_height;
   unsigned output_channels;
   uint8_t output_zero_point;
   float output_scale;

   struct pipe_resource *weight_tensor;
   unsigned weight_width;
   unsigned weight_height;
   uint8_t weight_zero_point;
   float weight_scale;
   bool weight_signed;

   uint8_t addition_offset;

   struct pipe_resource *bias_tensor;

   unsigned pad_before_x;
   unsigned pad_after_x;
   unsigned pad_before_y;
   unsigned pad_after_y;
   unsigned pad_before_z;
   unsigned pad_after_z;
};

#define ML_DBG(fmt, ...)                                  \
   do {                                                   \
      if (DBG_ENABLED(ETNA_DBG_ML_MSGS))                  \
         _debug_printf(fmt, ##__VA_ARGS__);             \
   } while (0)

unsigned etna_ml_allocate_tensor(struct etna_ml_subgraph *subgraph);
void etna_ml_create_tensor(struct etna_ml_subgraph *subgraph, unsigned idx, unsigned size);
struct etna_ml_tensor *etna_ml_get_tensor(struct etna_ml_subgraph *subgraph, unsigned idx);
struct pipe_resource *etna_ml_get_resource(struct etna_ml_subgraph *subgraph, unsigned idx);
unsigned etna_ml_get_offset(struct etna_ml_subgraph *subgraph, unsigned idx);
unsigned etna_ml_get_size(struct etna_ml_subgraph *subgraph, unsigned idx);

struct etna_bo *etna_ml_create_bo(struct pipe_context *pctx, size_t size);

struct pipe_resource *etna_ml_create_resource(struct pipe_context *pctx, size_t size);

struct etna_core_npu_info *etna_ml_get_core_info(struct etna_context *context);

bool
etna_ml_operation_supported(struct pipe_context *pcontext,
                            const struct pipe_ml_operation *operation);

struct pipe_ml_subgraph *
etna_ml_subgraph_create(struct pipe_context *context,
                        const struct pipe_ml_operation *operations,
                        unsigned count);

void
etna_ml_subgraph_invoke(struct pipe_context *pctx, struct pipe_ml_subgraph *psubgraph,
                        unsigned inputs_count, unsigned input_idxs[], void *inputs[], bool is_signed[]);

void
etna_ml_subgraph_read_outputs(struct pipe_context *context, struct pipe_ml_subgraph *subgraph,
                              unsigned outputs_count, unsigned output_idxs[], void *outputs[],
                              bool is_signed[]);

void
etna_ml_subgraph_destroy(struct pipe_context *context, struct pipe_ml_subgraph *subgraph);

#endif
