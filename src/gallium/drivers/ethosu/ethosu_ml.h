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
#define ACC_BITS                    32 /* Use for now always 32-bit accumulators */
#define IFM_GRANULE                 8
#define ACC_GRANULE                 16
#define ARCH_SPLIT_DEPTH            16
#define BANK_SIZE_BYTES             1024
#define IFM_GRANULE                 8

extern struct ethosu_block ARCH_OFM_BLOCK_MAX;
extern struct ethosu_block SUB_KERNEL_MAX;
extern struct ethosu_block IFM_UBLOCK;
extern struct ethosu_block OFM_UBLOCK;

#define COEFS_REGION   0
#define IO_REGION      1
#define SCRATCH_REGION 2

struct ethosu_block {
   unsigned width;
   unsigned height;
   unsigned depth;
};

enum ethosu_operation_type {
   ETHOSU_OPERATION_TYPE_CONVOLUTION,
   ETHOSU_OPERATION_TYPE_POOLING,
   ETHOSU_OPERATION_TYPE_ELTWISE,
   ETHOSU_OPERATION_TYPE_DMA,
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
struct ethosu_feature_map {
   unsigned tensor_idx;
   struct ethosu_block shape;
   bool is_signed;
   struct ethosu_tile_box tiles;
   unsigned zero_point;
   float scale;
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
   struct ethosu_shram_layout shram_layout;
   unsigned bank_size;
   enum ethosu_acc_type acc_type;
   bool is_partkernel;
};

#define MAX_MEMORY_ACCESSES 5 /* IFM, IFM2, Scales, Weights, LUT*/

struct ethosu_operation {
   enum ethosu_operation_type type;

   struct ethosu_block_config block_config;

   union {
      struct {
         struct ethosu_address_range weights;
         struct ethosu_address_range scales;
         bool part_kernel_first;
         bool depthwise;
      } conv;

      struct {
         bool avg; /* true for avg, false for max */
      } pooling;

      struct {
         unsigned lut_bytes;
      } eltwise;

      struct {
         unsigned address;
         long size;
      } dma;
   };

   struct ethosu_feature_map ifm;
   struct ethosu_feature_map ifm2;
   struct ethosu_feature_map ofm;

   struct ethosu_kernel kernel;
   struct ethosu_padding pad;
   bool upscale;
   enum ethosu_rounding_mode round_mode;

   struct ethosu_address_range read_accesses[MAX_MEMORY_ACCESSES];
   struct ethosu_address_range write_accesses[MAX_MEMORY_ACCESSES];
};

struct ethosu_tensor {
   unsigned index;
   unsigned offset;
   unsigned size;
   struct ethosu_block shape;
   enum ethosu_layout layout;
};

struct ethosu_subgraph {
   struct pipe_ml_subgraph base;

   struct util_dynarray operations; /* ethosu_operation */
   struct util_dynarray tensors;    /* ethosu_tensor* */

   unsigned cmdstream_used;
   uint32_t *cmdstream;
   uint32_t *cursor;
   uint32_t cmdstream_bo;

   struct pipe_resource *io_rsrc;
   unsigned io_used;

   uint8_t *coefs;
   struct pipe_resource *coefs_rsrc;
   unsigned coefs_used;
};

bool
ethosu_ml_operation_supported(struct pipe_context *pcontext, const struct pipe_ml_operation *operation);

struct pipe_ml_subgraph *
ethosu_ml_subgraph_create(struct pipe_context *pcontext,
                          const struct pipe_ml_operation *poperations,
                          unsigned count);

void ethosu_ml_subgraph_invoke(struct pipe_context *pcontext,
                               struct pipe_ml_subgraph *psubgraph,
                               unsigned inputs_count, unsigned input_idxs[],
                               void *inputs[], bool is_signed[]);

void ethosu_ml_subgraph_read_outputs(struct pipe_context *pcontext,
                                     struct pipe_ml_subgraph *psubgraph,
                                     unsigned outputs_count,
                                     unsigned output_idxs[], void *outputs[],
                                     bool is_signed[]);

void ethosu_ml_subgraph_destroy(struct pipe_context *context,
                                struct pipe_ml_subgraph *psubgraph);

void ethosu_allocate_feature_map(struct ethosu_subgraph *subgraph, struct ethosu_feature_map *feature_map);

void ethosu_register_tensor(struct ethosu_subgraph *subgraph, const struct pipe_tensor *ptensor);

struct ethosu_tensor *ethosu_find_tensor(struct ethosu_subgraph *subgraph, unsigned tensor_idx);

void ethosu_dump_buffer(const uint8_t *ptr, char *name, int operation_nr,
                        int suboperation_nr, int offset, unsigned size);

int ethosu_round_up_to_multiple(int a, int b);

int ethosu_round_up_divide(int a, int b);

int ethosu_quantize_scale(double scale, uint32_t *shift);

#endif /* ETHOSU_ML_H */
