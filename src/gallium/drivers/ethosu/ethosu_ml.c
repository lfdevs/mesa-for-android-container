/*
 * Copyright (c) 2024 Tomeu Vizoso <tomeu@tomeuvizoso.net>
 * SPDX-License-Identifier: MIT
 */

#include "pipe/p_defines.h"
#include "pipe/p_screen.h"
#include "pipe/p_state.h"
#include "util/macros.h"
#include "util/u_dynarray.h"
#include "util/u_inlines.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <xf86drm.h>

#include "drm-uapi/ethosu_accel.h"

#include "ethosu_cmd.h"
#include "ethosu_lower.h"
#include "ethosu_ml.h"

struct ethosu_block IFM_UBLOCK = {2, 2, 8};
struct ethosu_block OFM_UBLOCK = {2, 2, 8};
struct ethosu_block ARCH_OFM_BLOCK_MAX = {64, 32, 128};
struct ethosu_block SUB_KERNEL_MAX = {8, 8, 65536};

void
ethosu_dump_buffer(const uint8_t *ptr, char *name, int operation_nr,
                   int suboperation_nr, int offset, unsigned size)
{
   char buffer[255];

   snprintf(buffer, sizeof(buffer), "mesa-%s-%03u-%03u.bin", name, operation_nr,
            suboperation_nr);

   FILE *f = fopen(buffer, "wb");
   assert(f);
   fwrite(ptr + offset, 1, size, f);
   if (ferror(f)) {
      DBG("Error in writing to file: %s\n", strerror(errno));
   }
   fflush(f);
   fclose(f);
}

void
ethosu_register_tensor(struct ethosu_subgraph *subgraph,
                       const struct pipe_tensor *ptensor)
{
   struct ethosu_tensor new_tensor = {0};
   new_tensor.index = ptensor->index;
   new_tensor.shape.height = ptensor->dims[1];
   new_tensor.shape.width = ptensor->dims[2];
   new_tensor.shape.depth = ptensor->dims[3];
   new_tensor.layout = ETHOSU_LAYOUT_NHWC;
   util_dynarray_append(&subgraph->tensors, new_tensor);
}

void
ethosu_allocate_feature_map(struct ethosu_subgraph *subgraph, struct ethosu_feature_map *feature_map)
{
   struct ethosu_tensor *tensor = ethosu_find_tensor(subgraph, feature_map->tensor_idx);
   unsigned size;

   if (tensor->layout == ETHOSU_LAYOUT_NHWC) {
      size = tensor->shape.width * tensor->shape.height * tensor->shape.depth;
   } else if (tensor->layout == ETHOSU_LAYOUT_NHCWB16) {
      size = tensor->shape.width * tensor->shape.height * align(tensor->shape.depth, 16);
   } else {
      assert(0 && "Unsupported layout");
      size = 0; // This should never happen
   }

   assert(tensor);

   if (tensor->size > 0) {
      feature_map->tiles.addresses[0] = tensor->offset;
      return;
   }

   tensor->offset = subgraph->io_used;
   tensor->size = size;
   subgraph->io_used += ALIGN_POT(size, 16);

   feature_map->tiles.addresses[0] = tensor->offset;
}

struct ethosu_tensor *
ethosu_find_tensor(struct ethosu_subgraph *subgraph, unsigned tensor_idx)
{
   util_dynarray_foreach (&subgraph->tensors, struct ethosu_tensor, tensor) {
      if (tensor->index == tensor_idx) {
         return tensor;
      }
   }
   return NULL;
}

int
ethosu_round_up_to_multiple(int a, int b)
{
   return ((a + b - 1) / b) * b;
}

int
ethosu_round_up_divide(int a, int b)
{
   return (a + b - 1) / b;
}

int
ethosu_quantize_scale(double scale, uint32_t *shift)
{
   int exponent = 0;
   double significand = frexp(scale, &exponent);
   uint32_t quantized_scale = round(significand * (double)(1LL << 31));
   *shift = 31 - exponent;
   if (*shift > 63) {
      if (quantized_scale > exp2(*shift - 63)) {
         quantized_scale = quantized_scale >> (*shift - 63);
         *shift = 63;
      } else {
         // Not possible to get back within bounds, set scale and shift to 0
         // as the shift would shift away all relevant bits anyway.
         quantized_scale = 0;
         *shift = 0;
      }
   } else if (*shift < 0 && quantized_scale < exp2(*shift + 32)) {
      quantized_scale = quantized_scale << (0 - *shift);
      *shift = 0;
   }

   return quantized_scale;
}

static bool
tensor_quantization_supported(struct pipe_tensor *tensor)
{
   /*
    * Per-axis quantization not supported, for details see:
    * https://ai.google.dev/edge/litert/models/quantization_spec#per-axis_vs_per-tensor
    */
   return tensor->scales == NULL && tensor->zero_points == NULL;
}

bool
ethosu_ml_operation_supported(struct pipe_context *pcontext,
                              const struct pipe_ml_operation *operation)
{
   bool supported = false;

   switch (operation->type) {
   case PIPE_ML_OPERATION_TYPE_CONVOLUTION: {
      struct pipe_tensor *input_tensor = operation->input_tensors[0];
      struct pipe_tensor *weight_tensor = operation->conv.weight_tensor;
      struct pipe_tensor *bias_tensor = operation->conv.bias_tensor;
      struct pipe_tensor *output_tensor = operation->output_tensors[0];

      // Dilation and per-axis quantization not yet implemented
      if (tensor_quantization_supported(input_tensor) &&
          tensor_quantization_supported(weight_tensor) &&
          tensor_quantization_supported(bias_tensor) &&
          tensor_quantization_supported(output_tensor) &&
          operation->conv.dilation_width_factor == 1 &&
          operation->conv.dilation_height_factor == 1)
         supported = true;

      break;
   }
   case PIPE_ML_OPERATION_TYPE_ADD:
      supported = operation->input_tensors[0]->resource == NULL &&
                  operation->input_tensors[1]->resource == NULL;
      break;
   case PIPE_ML_OPERATION_TYPE_POOLING:
   case PIPE_ML_OPERATION_TYPE_STRIDED_SLICE:
   case PIPE_ML_OPERATION_TYPE_PAD:
   case PIPE_ML_OPERATION_TYPE_RESIZE:
      supported = true;
      break;
   case PIPE_ML_OPERATION_TYPE_CONCATENATION:
      supported = operation->conc.axis == 3 ||
                  operation->conc.axis == -1;
      break;
   default:
      supported = false;
   }

   return supported;
}

struct pipe_ml_subgraph *
ethosu_ml_subgraph_create(struct pipe_context *pcontext,
                          const struct pipe_ml_operation *poperations,
                          unsigned count)
{
   struct pipe_screen *pscreen = pcontext->screen;
   struct ethosu_screen *screen = ethosu_screen(pscreen);
   struct ethosu_subgraph *subgraph;

   subgraph = calloc(1, sizeof(*subgraph));
   subgraph->base.context = pcontext;

   subgraph->tensors = UTIL_DYNARRAY_INIT;
   subgraph->operations = UTIL_DYNARRAY_INIT;

   ethosu_lower_graph(subgraph, poperations, count);

   ethosu_emit_cmdstream(subgraph);

   struct drm_ethosu_cmdstream_bo_create cmd_bo_create = {
      .size = (subgraph->cursor - subgraph->cmdstream) * sizeof(*subgraph->cursor),
      .data = (uintptr_t)subgraph->cmdstream,
   };

   if (DBG_ENABLED(ETHOSU_DBG_DUMP_BOS))
      ethosu_dump_buffer((uint8_t *)subgraph->cmdstream, "cmdstream", 0, 0, 0, (subgraph->cursor - subgraph->cmdstream) * sizeof(*subgraph->cursor));

   int ret = drmIoctl(screen->fd, DRM_IOCTL_ETHOSU_CMDSTREAM_BO_CREATE, &cmd_bo_create);
   assert(ret == 0);

   free(subgraph->cmdstream);

   subgraph->cmdstream_bo = cmd_bo_create.handle;

   if (subgraph->coefs_used > 0) {
      subgraph->coefs_rsrc = pipe_buffer_create(pscreen, 0, PIPE_USAGE_DEFAULT, subgraph->coefs_used);
      pipe_buffer_write(subgraph->base.context, subgraph->coefs_rsrc, 0, subgraph->coefs_used, subgraph->coefs);

      free(subgraph->coefs);
      subgraph->coefs = NULL;

      if (DBG_ENABLED(ETHOSU_DBG_DUMP_BOS)) {
         struct pipe_transfer *transfer_in;
         uint8_t *buf = pipe_buffer_map(subgraph->base.context, subgraph->coefs_rsrc,
                                        PIPE_MAP_READ, &transfer_in);
         ethosu_dump_buffer(buf, "coefs", 0, 0, 0, pipe_buffer_size(subgraph->coefs_rsrc));
         pipe_buffer_unmap(subgraph->base.context, transfer_in);
      }
   }

   subgraph->io_rsrc = pipe_buffer_create(pscreen, 0, PIPE_USAGE_DEFAULT, subgraph->io_used);

   return &subgraph->base;
}

void
ethosu_ml_subgraph_invoke(struct pipe_context *pcontext,
                          struct pipe_ml_subgraph *psubgraph,
                          unsigned inputs_count, unsigned input_idxs[],
                          void *inputs[], bool is_signed[])
{
   struct ethosu_screen *screen = ethosu_screen(pcontext->screen);
   struct ethosu_subgraph *subgraph = (struct ethosu_subgraph *)(psubgraph);
   struct drm_ethosu_submit submit = {0};
   struct drm_ethosu_job job = {0};
   struct timespec start, end;
   int ret;

   for (unsigned i = 0; i < inputs_count; i++) {
      struct ethosu_tensor *input = ethosu_find_tensor(subgraph, input_idxs[i]);
      assert(input);

      if (DBG_ENABLED(ETHOSU_DBG_DUMP_BOS))
         ethosu_dump_buffer(inputs[i], "input", 0, 0, 0, input->size);

      pipe_buffer_write(pcontext, subgraph->io_rsrc, input->offset, input->size, inputs[i]);
   }

   if (DBG_ENABLED(ETHOSU_DBG_DUMP_BOS)) {
      struct pipe_transfer *transfer_in;
      uint8_t *buf = pipe_buffer_map(subgraph->base.context, subgraph->io_rsrc,
                                     PIPE_MAP_READ, &transfer_in);
      ethosu_dump_buffer(buf, "io-before", 0, 0, 0, pipe_buffer_size(subgraph->io_rsrc));
      pipe_buffer_unmap(subgraph->base.context, transfer_in);
   }

   job.cmd_bo = subgraph->cmdstream_bo;

   if (subgraph->coefs_rsrc) {
      job.region_bo_handles[COEFS_REGION] = ethosu_resource(subgraph->coefs_rsrc)->handle;
      if (!DBG_ENABLED(ETHOSU_DBG_DISABLE_SRAM)) {
         job.region_bo_handles[SCRATCH_REGION] = 0;
         job.sram_size = screen->info.sram_size;
      }
   }

   job.region_bo_handles[IO_REGION] = ethosu_resource(subgraph->io_rsrc)->handle;

   submit.jobs = (uintptr_t)&job;
   submit.job_count = 1;

   if (DBG_ENABLED(ETHOSU_DBG_MSGS))
      clock_gettime(CLOCK_MONOTONIC_RAW, &start);

   ret = drmIoctl(screen->fd, DRM_IOCTL_ETHOSU_SUBMIT, &submit);
   assert(ret == 0);

   if (DBG_ENABLED(ETHOSU_DBG_MSGS)) {
      clock_gettime(CLOCK_MONOTONIC_RAW, &end);
      long long duration_ns = (long long)(end.tv_sec - start.tv_sec) * 1000000000LL + (end.tv_nsec - start.tv_nsec);
      DBG("Submission took %lld ms\n", duration_ns / 1000000);

      /* Force a sync */
      struct pipe_transfer *transfer_in;
      pipe_buffer_map(subgraph->base.context, subgraph->io_rsrc, PIPE_MAP_READ, &transfer_in);
      pipe_buffer_unmap(subgraph->base.context, transfer_in);

      clock_gettime(CLOCK_MONOTONIC_RAW, &end);
      duration_ns = (long long)(end.tv_sec - start.tv_sec) * 1000000000LL + (end.tv_nsec - start.tv_nsec);
      DBG("Execution took %lld ms\n", duration_ns / 1000000);
   }
}

void
ethosu_ml_subgraph_read_outputs(struct pipe_context *pcontext,
                                struct pipe_ml_subgraph *psubgraph,
                                unsigned outputs_count,
                                unsigned output_idxs[], void *outputsv[],
                                bool is_signed[])
{
   struct ethosu_subgraph *subgraph = (struct ethosu_subgraph *)(psubgraph);
   uint8_t **outputs = (uint8_t **)outputsv;

   for (int i = 0; i < outputs_count; i++) {
      struct ethosu_tensor *output = ethosu_find_tensor(subgraph, output_idxs[i]);

      if (DBG_ENABLED(ETHOSU_DBG_DUMP_BOS)) {
         struct pipe_transfer *transfer_in;
         uint8_t *buf = pipe_buffer_map(subgraph->base.context, subgraph->io_rsrc,
                                        PIPE_MAP_READ, &transfer_in);
         ethosu_dump_buffer(buf, "io-after", 0, 0, 0, pipe_buffer_size(subgraph->io_rsrc));
         pipe_buffer_unmap(subgraph->base.context, transfer_in);
      }

      pipe_buffer_read(pcontext, subgraph->io_rsrc, output->offset, output->size, outputs[i]);
   }
}

void
ethosu_ml_subgraph_destroy(struct pipe_context *pcontext,
                           struct pipe_ml_subgraph *psubgraph)
{
   int ret;
   struct drm_gem_close arg = {0};
   struct ethosu_screen *screen = ethosu_screen(pcontext->screen);
   struct ethosu_subgraph *subgraph = (struct ethosu_subgraph *)(psubgraph);

   pipe_resource_reference(&subgraph->io_rsrc, NULL);
   pipe_resource_reference(&subgraph->coefs_rsrc, NULL);

   arg.handle = subgraph->cmdstream_bo;
   ret = drmIoctl(screen->fd, DRM_IOCTL_GEM_CLOSE, &arg);
   assert(ret >= 0);

   util_dynarray_fini(&subgraph->operations);
   util_dynarray_fini(&subgraph->tensors);

   free(subgraph);
}
