/*
 * Copyright (c) 2024 Tomeu Vizoso <tomeu@tomeuvizoso.net>
 * SPDX-License-Identifier: MIT
 */

#ifndef ETHOSU_ML_H
#define ETHOSU_ML_H

#include <util/u_dynarray.h>

#include "ethosu_device.h"

#define SHRAM_BANKS                 48
#define SHRAM_RESERVED_OUTPUT_BANKS 2
#define SHRAM_RESERVED_UNUSED_BANKS 2
#define SHRAM_RESERVED_END_BANKS    2
#define SHRAM_TOTAL_BANKS           SHRAM_BANKS
#define SHRAM_BANK_SIZE_BYTES       1024
#define LUT8_SIZE                   256
#define SHRAM_LUT_BASE(lut)         (46 * SHRAM_BANK_SIZE_BYTES + (lut) * LUT8_SIZE)
#define ACC_BITS                    32 /* Use for now always 32-bit accumulators */
#define IFM_GRANULE                 8
#define ACC_GRANULE                 16
#define ARCH_SPLIT_DEPTH            16
#define BANK_SIZE_BYTES             1024
#define IFM_GRANULE                 8

extern struct ethosu_block ARCH_OFM_BLOCK_MAX;
extern struct ethosu_block SUB_KERNEL_MAX;

/* Maximum register index for state tracking arrays.
 * All CMD0 offsets are ≤ 0x18F and CMD1 offsets are ≤ 0x1A0,
 * so 512 entries covers the full range. */
#define ETHOSU_MAX_REG_INDEX 512

#define COEFS_REGION   0
#define IO_REGION      1
#define SCRATCH_REGION 2
#define LUT_REGION     0x103     // Internal SHRAM

enum ethosu_operation_type {
   ETHOSU_OPERATION_TYPE_NONE,
   ETHOSU_OPERATION_TYPE_CONVOLUTION,
   ETHOSU_OPERATION_TYPE_POOLING,
   ETHOSU_OPERATION_TYPE_ELTWISE,
   ETHOSU_OPERATION_TYPE_DMA,
};

enum ethosu_eltwise_type {
   ETHOSU_ELTWISE_TYPE_MUL,
   ETHOSU_ELTWISE_TYPE_ADD,
   ETHOSU_ELTWISE_TYPE_SUB,
   ETHOSU_ELTWISE_TYPE_MIN,
   ETHOSU_ELTWISE_TYPE_MAX,
   ETHOSU_ELTWISE_TYPE_LRELU,
   ETHOSU_ELTWISE_TYPE_ABS,
   /* U85 only */
   ETHOSU_ELTWISE_TYPE_CLZ,
   ETHOSU_ELTWISE_TYPE_SHR,
   ETHOSU_ELTWISE_TYPE_SHL,
   ETHOSU_ELTWISE_TYPE_LSR,
   ETHOSU_ELTWISE_TYPE_DIV,
   ETHOSU_ELTWISE_TYPE_CMP_EQ = 0x10,
   ETHOSU_ELTWISE_TYPE_CMP_NE,
   ETHOSU_ELTWISE_TYPE_CMP_GE,
   ETHOSU_ELTWISE_TYPE_CMP_GT,
   ETHOSU_ELTWISE_TYPE_CMP_AND = 0x21,
   ETHOSU_ELTWISE_TYPE_CMP_OR,
   ETHOSU_ELTWISE_TYPE_CMP_XOR,
   ETHOSU_ELTWISE_TYPE_CMP_NOT,
   ETHOSU_ELTWISE_TYPE_CMP_AND_NOT = 0x2A,
};

struct ethosu_tile_box {
   unsigned height_0;     /* The height of tile 0 */
   unsigned height_1;     /* The height of tile 1, 0 if unused */
   unsigned width_0;      /* The width of tile 0, and tile 2 (if used) */
   unsigned addresses[4]; /* A list of 4 addresses, set unused addresses to 0 */
};

enum ethosu_layout {
   ETHOSU_LAYOUT_NHWC,
   ETHOSU_LAYOUT_NHCWB16,
};

enum ethosu_rounding_mode {
   ETHOSU_ROUNDING_DOUBLE = 0,
   ETHOSU_ROUNDING_TRUNCATE,
   ETHOSU_ROUNDING_NATURAL,
};

enum ethosu_upscale_mode {
   ETHOSU_UPSCALE_NONE = 0,
   ETHOSU_UPSCALE_NEAREST = 1,
   ETHOSU_UPSCALE_ZEROS = 2,
};

struct ethosu_stride {
   unsigned x;
   unsigned y;
   unsigned c;
};

struct ethosu_tensor;

struct ethosu_feature_map {
   struct ethosu_tensor *tensor;
   struct ethosu_block shape;
   struct ethosu_stride stride;
   bool is_signed;
   uint8_t precision;
   struct ethosu_tile_box tiles;
   unsigned zero_point;
   float scale;
   uint16_t scalar;
   uint8_t region;
};

struct ethosu_kernel {
   unsigned height;
   unsigned width;
   unsigned stride_y;
   unsigned stride_x;
   unsigned dilation_y;
   unsigned dilation_x;
   bool depthwise;
   bool is_signed;
   unsigned zero_point;
   float scale;
   /* Per-channel quantization (NULL for per-tensor) */
   float *scales;
   int *zero_points;
};

struct ethosu_padding {
   unsigned top;
   unsigned left;
   unsigned bottom;
   unsigned right;
};

struct ethosu_address_range {
   unsigned region;
   unsigned address;
   long size;
};

struct ethosu_shram_layout {
   unsigned ib_start;
   unsigned ib_end;
   unsigned ib_start2;
   unsigned ab_start;
   unsigned lut_start;
};

enum ethosu_acc_type {
   ETHOSU_ACC_TYPE_INT_32BIT = 0,
   ETHOSU_ACC_TYPE_INT_40BIT,
   ETHOSU_ACC_TYPE_FP_S5_10,
};

struct ethosu_block_config {
   struct ethosu_block ifm_block;
   struct ethosu_block ofm_block;
   struct ethosu_block ofm_ublock;
   struct ethosu_shram_layout shram_layout;
   unsigned bank_size;
   enum ethosu_acc_type acc_type;
   bool is_partkernel;
};

enum ethosu_pooling_type {
   ETHOSU_POOLING_TYPE_MAX = 0,
   ETHOSU_POOLING_TYPE_AVG,
   ETHOSU_POOLING_TYPE_REDUCE_SUM,
   ETHOSU_POOLING_TYPE_SUM,
   ETHOSU_POOLING_TYPE_NONE,
   ETHOSU_POOLING_TYPE_MIN,
   ETHOSU_POOLING_TYPE_ARGMAX_X,
   ETHOSU_POOLING_TYPE_ARGMAX_Y,
};

#define ETHOSU_POOLING_ACTIVATION_LUT(n)  (0x10 | (n))

#define MAX_MEMORY_ACCESSES 5 /* IFM, IFM2, Scales, Weights, LUT*/

struct ethosu_operation {
   enum ethosu_operation_type type;

   struct ethosu_block_config block_config;

   union {
      struct {
         struct ethosu_address_range weights;
         struct ethosu_address_range scales;
         bool depthwise;
         unsigned scale;
         unsigned shift;
      } conv;

      struct {
         enum ethosu_pooling_type type;
         bool nop;
         uint8_t activation;
         struct ethosu_address_range lut;
      } pooling;

      struct {
         enum ethosu_eltwise_type type;
         uint16_t activation_min;
         unsigned lut_bytes;
         bool ifm_reversed;
      } eltwise;

      struct {
         unsigned address;
         unsigned dst_address;
         long size;
         unsigned dst_region;
      } dma;
   };

   struct ethosu_feature_map ifm;
   struct ethosu_feature_map ifm2;
   struct ethosu_feature_map ofm;

   struct ethosu_kernel kernel;
   struct ethosu_padding pad;
   enum ethosu_upscale_mode upscale;
   enum ethosu_rounding_mode round_mode;

   struct ethosu_address_range read_accesses[MAX_MEMORY_ACCESSES];
   struct ethosu_address_range write_accesses[MAX_MEMORY_ACCESSES];
};

struct ethosu_tensor {
   unsigned index;
   unsigned offset;
   unsigned size;
   uint8_t type_size;
   struct ethosu_block shape;
   enum ethosu_layout layout;
};

#define NUM_HEADER_FIELDS  4
#define NUM_TENSOR_FIELDS  3

struct ethosu_subgraph {
   struct pipe_ml_subgraph base;

   struct ethosu_screen *screen; /* Set during prepare_for_submission */

   struct util_dynarray operations; /* ethosu_operation */
   struct util_dynarray tensors;    /* ethosu_tensor */

   unsigned cmdstream_used;
   uint32_t *cmdstream;
   uint32_t *cursor;
   uint32_t cmdstream_bo;

   struct pipe_resource *io_rsrc;
   unsigned io_used;

   uint8_t *coefs;
   struct pipe_resource *coefs_rsrc;
   unsigned coefs_used;

   /* Register state tracking to avoid emitting unchanged values */
   uint16_t *cmd0_state; /* Array of last values for CMD0 registers (16-bit) */
   uint64_t *cmd1_state; /* Array of last values for CMD1 registers */
   bool *cmd0_valid;     /* Track which CMD0 registers have been set */
   bool *cmd1_valid;     /* Track which CMD1 registers have been set */
};

bool
ethosu_ml_operation_supported(struct pipe_ml_device *pdevice,
                              const struct pipe_ml_operation *operation);

struct pipe_ml_subgraph *
ethosu_ml_subgraph_create(struct pipe_ml_device *pdevice,
                          const struct pipe_ml_operation *poperations,
                          unsigned count);

uint8_t *
ethosu_ml_subgraph_serialize(struct pipe_ml_device *pdevice,
                             struct pipe_ml_subgraph *psubgraph,
                             size_t *size);

struct pipe_ml_subgraph *
ethosu_ml_subgraph_deserialize(struct pipe_context *pcontext,
                               const uint8_t *data,
                               size_t size);

void ethosu_ml_subgraph_invoke(struct pipe_context *pcontext,
                               struct pipe_ml_subgraph *psubgraph,
                               unsigned inputs_count, unsigned input_idxs[],
                               void *inputs[], bool is_signed[]);

void ethosu_ml_subgraph_read_outputs(struct pipe_context *pcontext,
                                     struct pipe_ml_subgraph *psubgraph,
                                     unsigned outputs_count,
                                     unsigned output_idxs[], void *outputs[],
                                     bool is_signed[]);

void ethosu_ml_subgraph_destroy(struct pipe_ml_device *pdevice,
                                struct pipe_ml_subgraph *psubgraph);

void ethosu_register_tensor(struct ethosu_subgraph *subgraph, const struct pipe_tensor *ptensor);

struct ethosu_tensor *ethosu_find_tensor(struct ethosu_subgraph *subgraph, unsigned tensor_idx);

void ethosu_dump_buffer(const uint8_t *ptr, char *name, int operation_nr,
                        int suboperation_nr, int offset, unsigned size);

int ethosu_round_up_to_multiple(int a, int b);

int ethosu_round_up_divide(int a, int b);

int ethosu_quantize_scale(double scale, int32_t *shift, bool reduced);

#endif /* ETHOSU_ML_H */
