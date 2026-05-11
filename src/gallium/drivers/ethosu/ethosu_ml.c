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
   new_tensor.type_size = ptensor->type_size;
   util_dynarray_append(&subgraph->tensors, new_tensor);
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
ethosu_quantize_scale(double scale, int32_t *shift, bool reduced)
{
   int exponent = 0;
   double significand = frexp(scale, &exponent);
   int32_t quantized_scale = round(significand * (double)(1LL << 31));
   *shift = 31 - exponent;

   if (reduced) {
      quantized_scale = (quantized_scale >> 16) + (quantized_scale >> 15 & 1);
      // make sure reduced scale does not overflow
      quantized_scale = MIN2(quantized_scale, 0x7FFF);
      *shift -= 16;
   }

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

bool
ethosu_ml_operation_supported(struct pipe_ml_device *pdevice,
                              const struct pipe_ml_operation *operation)
{
   bool supported = false;

   if (operation->input_tensors[0]->type_size == 4 ||
       operation->output_tensors[0]->type_size == 4)
      return false;

   switch (operation->type) {
   case PIPE_ML_OPERATION_TYPE_FULLY_CONNECTED: {
      supported = true;
      break;
   }
   case PIPE_ML_OPERATION_TYPE_CONVOLUTION: {
      /*
       * Dilation is not yet implemented.
       */
      if (operation->conv.dilation_width_factor == 1 &&
          operation->conv.dilation_height_factor == 1)
         supported = true;

      break;
   }
   case PIPE_ML_OPERATION_TYPE_MAXIMUM:
   case PIPE_ML_OPERATION_TYPE_MINIMUM:
   case PIPE_ML_OPERATION_TYPE_MUL:
   case PIPE_ML_OPERATION_TYPE_ADD:
   case PIPE_ML_OPERATION_TYPE_POOLING:
   case PIPE_ML_OPERATION_TYPE_STRIDED_SLICE:
   case PIPE_ML_OPERATION_TYPE_PAD:
   case PIPE_ML_OPERATION_TYPE_LOGISTIC:
   case PIPE_ML_OPERATION_TYPE_TANH:
   case PIPE_ML_OPERATION_TYPE_HSWISH:
   case PIPE_ML_OPERATION_TYPE_LEAKY_RELU:
   case PIPE_ML_OPERATION_TYPE_QUANTIZE:
   case PIPE_ML_OPERATION_TYPE_RESHAPE:
      supported = true;
      break;
   case PIPE_ML_OPERATION_TYPE_RESIZE: {
      /* NPU only supports 2x nearest neighbor upscaling */
      struct pipe_tensor *input = operation->input_tensors[0];
      struct pipe_tensor *output = operation->output_tensors[0];
      bool is_2x_height = (output->dims[1] == 2 * input->dims[1]);
      bool is_2x_width = (output->dims[2] == 2 * input->dims[2]);
      supported = is_2x_height && is_2x_width;
      break;
   }
   case PIPE_ML_OPERATION_TYPE_CONCATENATION:
      supported = operation->conc.axis <= 3 && operation->conc.axis >= -1;
      break;
   default:
      supported = false;
   }

   return supported;
}

struct pipe_ml_subgraph *
ethosu_ml_subgraph_create(struct pipe_ml_device *pdevice,
                          const struct pipe_ml_operation *poperations,
                          unsigned count)
{
   struct ethosu_subgraph *subgraph;

   subgraph = calloc(1, sizeof(*subgraph));
   subgraph->base.device = pdevice;

   subgraph->tensors = UTIL_DYNARRAY_INIT;
   subgraph->operations = UTIL_DYNARRAY_INIT;

   /* Allocate register state tracking arrays */
   subgraph->cmd0_state = calloc(ETHOSU_MAX_REG_INDEX, sizeof(*subgraph->cmd0_state));
   subgraph->cmd1_state = calloc(ETHOSU_MAX_REG_INDEX, sizeof(*subgraph->cmd1_state));
   subgraph->cmd0_valid = calloc(ETHOSU_MAX_REG_INDEX, sizeof(bool));
   subgraph->cmd1_valid = calloc(ETHOSU_MAX_REG_INDEX, sizeof(bool));
   if (!subgraph->cmd0_state || !subgraph->cmd1_state ||
       !subgraph->cmd0_valid || !subgraph->cmd1_valid) {
      free(subgraph->cmd0_state);
      free(subgraph->cmd1_state);
      free(subgraph->cmd0_valid);
      free(subgraph->cmd1_valid);
      free(subgraph);
      return NULL;
   }

   ethosu_lower_graph(subgraph, poperations, count);

   ethosu_emit_cmdstream(subgraph);

   util_dynarray_foreach (&subgraph->operations, struct ethosu_operation, operation) {
      free(operation->kernel.scales);
      free(operation->kernel.zero_points);
   }
   util_dynarray_fini(&subgraph->operations);

   free(subgraph->cmd0_state);
   free(subgraph->cmd1_state);
   free(subgraph->cmd0_valid);
   free(subgraph->cmd1_valid);

   return &subgraph->base;
}

uint8_t *
ethosu_ml_subgraph_serialize(struct pipe_ml_device *pdevice,
                             struct pipe_ml_subgraph *psubgraph,
                             size_t *size)
{
   struct ethosu_subgraph *subgraph = (struct ethosu_subgraph *)(psubgraph);
   uint64_t header_size = NUM_HEADER_FIELDS * sizeof(uint64_t);
   uint64_t tensors_size = util_dynarray_num_elements(&subgraph->tensors,
      struct ethosu_tensor) * NUM_TENSOR_FIELDS * sizeof(uint32_t);
   uint64_t cmdstream_size = (subgraph->cursor - subgraph->cmdstream) *
      sizeof(*subgraph->cursor);
   uint64_t coefs_size = subgraph->coefs_used * sizeof(*subgraph->coefs);
   uint64_t io_size = subgraph->io_used;
   uint64_t total_size = header_size + cmdstream_size + coefs_size +
      tensors_size;
   uint8_t *buffer, *cursor;

   buffer = malloc(total_size);
   if (!buffer)
      return NULL;

   cursor = buffer;

   uint64_t *header = (uint64_t *)cursor;
   header[0] = cmdstream_size;
   header[1] = coefs_size;
   header[2] = io_size;
   header[3] = tensors_size;
   cursor += header_size;

   uint32_t *tensors = (uint32_t *)cursor;
   util_dynarray_foreach(&subgraph->tensors, struct ethosu_tensor, tensor) {
      tensors[0] = tensor->index;
      tensors[1] = tensor->offset;
      tensors[2] = tensor->size;
      tensors += NUM_TENSOR_FIELDS;
   }
   cursor += tensors_size;

   memcpy(cursor, subgraph->cmdstream, cmdstream_size);
   cursor += cmdstream_size;

   if (coefs_size > 0)
      memcpy(cursor, subgraph->coefs, coefs_size);

   *size = total_size;
   return buffer;
}

static void
prepare_for_submission(struct ethosu_subgraph *subgraph,
                       struct pipe_context *pcontext)
{
   int ret;
   subgraph->screen = ethosu_screen(pcontext->screen);
   struct ethosu_screen *screen = subgraph->screen;
   uint64_t cmdstream_size = (subgraph->cursor - subgraph->cmdstream) *
      sizeof(*subgraph->cursor);

   if (DBG_ENABLED(ETHOSU_DBG_DUMP_BOS))
      ethosu_dump_buffer((uint8_t *)subgraph->cmdstream, "cmdstream", 0, 0, 0,
                         cmdstream_size);

   if (cmdstream_size) {
      struct drm_ethosu_cmdstream_bo_create cmd_bo_create = {
         .size = cmdstream_size,
         .data = (uintptr_t)subgraph->cmdstream,
      };

      ret = drmIoctl(screen->fd, DRM_IOCTL_ETHOSU_CMDSTREAM_BO_CREATE,
                     &cmd_bo_create);
      assert(ret == 0);

      free(subgraph->cmdstream);
      subgraph->cmdstream = NULL;

      subgraph->cmdstream_bo = cmd_bo_create.handle;
   }

   DBG("subgraph->coefs_used %d\n", subgraph->coefs_used);
   if (subgraph->coefs_used > 0) {
      subgraph->coefs_rsrc = pipe_buffer_create(pcontext->screen, 0,
                                                PIPE_USAGE_DEFAULT,
                                                subgraph->coefs_used);
      pipe_buffer_write(pcontext, subgraph->coefs_rsrc, 0,
                        subgraph->coefs_used, subgraph->coefs);

      free(subgraph->coefs);
      subgraph->coefs = NULL;

      if (DBG_ENABLED(ETHOSU_DBG_DUMP_BOS)) {
         struct pipe_transfer *transfer_in;
         uint8_t *buf = pipe_buffer_map(pcontext, subgraph->coefs_rsrc,
                                        PIPE_MAP_READ, &transfer_in);
         ethosu_dump_buffer(buf, "coefs", 0, 0, 0,
                            pipe_buffer_size(subgraph->coefs_rsrc));
         pipe_buffer_unmap(pcontext, transfer_in);
      }
   }

   DBG("subgraph->io_used %d\n", subgraph->io_used);
   subgraph->io_rsrc = pipe_buffer_create(pcontext->screen, 0,
                                          PIPE_USAGE_DEFAULT,
                                          subgraph->io_used);
}

struct pipe_ml_subgraph *
ethosu_ml_subgraph_deserialize(struct pipe_context *pcontext,
                               const uint8_t *data,
                               size_t size)
{
   struct ethosu_subgraph *subgraph;

   if (size < NUM_HEADER_FIELDS * sizeof(uint64_t))
      return NULL;

   subgraph = calloc(1, sizeof(*subgraph));
   if (!subgraph)
      return NULL;

   subgraph->base.device = pcontext->screen->get_ml_device(pcontext->screen);

   util_dynarray_init(&subgraph->tensors, NULL);

   const uint64_t *header = (const uint64_t *)data;
   uint64_t header_size = NUM_HEADER_FIELDS * sizeof(uint64_t);
   uint64_t cmdstream_size = header[0];
   uint64_t coefs_size = header[1];
   uint64_t io_size = header[2];
   uint64_t tensors_size = header[3];
   data += header_size;

   if (size != header_size + cmdstream_size + coefs_size + tensors_size) {
      free(subgraph);
      return NULL;
   }

   for (unsigned i = 0;
        i < tensors_size / (NUM_TENSOR_FIELDS * sizeof(uint32_t)); i++) {
      struct ethosu_tensor tensor = {0};
      const uint32_t *tdata = (const uint32_t *)data;
      tensor.index = tdata[0];
      tensor.offset = tdata[1];
      tensor.size = tdata[2];
      util_dynarray_append(&subgraph->tensors, tensor);
      data += NUM_TENSOR_FIELDS * sizeof(uint32_t);
   }

   subgraph->cmdstream_used = cmdstream_size / sizeof(*subgraph->cmdstream);
   subgraph->cmdstream = malloc(cmdstream_size);
   if (!subgraph->cmdstream) {
      util_dynarray_fini(&subgraph->tensors);
      free(subgraph);
      return NULL;
   }
   memcpy(subgraph->cmdstream, data, cmdstream_size);
   subgraph->cursor = subgraph->cmdstream + subgraph->cmdstream_used;
   data += cmdstream_size;

   subgraph->coefs_used = coefs_size;
   if (coefs_size > 0) {
      subgraph->coefs = malloc(coefs_size);
      if (!subgraph->coefs) {
         free(subgraph->cmdstream);
         util_dynarray_fini(&subgraph->tensors);
         free(subgraph);
         return NULL;
      }
      memcpy(subgraph->coefs, data, coefs_size);
   }

   subgraph->io_used = io_size;

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

   if (subgraph->io_rsrc == NULL)
      prepare_for_submission(subgraph, pcontext);

   for (unsigned i = 0; i < inputs_count; i++) {
      struct ethosu_tensor *input = ethosu_find_tensor(subgraph, input_idxs[i]);
      assert(input);

      if (DBG_ENABLED(ETHOSU_DBG_DUMP_BOS))
         ethosu_dump_buffer(inputs[i], "input", 0, 0, 0, input->size);

      pipe_buffer_write(pcontext, subgraph->io_rsrc, input->offset, input->size, inputs[i]);
   }

   if (DBG_ENABLED(ETHOSU_DBG_DUMP_BOS)) {
      struct pipe_transfer *transfer_in;
      uint8_t *buf = pipe_buffer_map(pcontext, subgraph->io_rsrc,
                                     PIPE_MAP_READ, &transfer_in);
      ethosu_dump_buffer(buf, "io-before", 0, 0, 0, pipe_buffer_size(subgraph->io_rsrc));
      pipe_buffer_unmap(pcontext, transfer_in);
   }

   if (!subgraph->cmdstream_bo)
      return;

   job.cmd_bo = subgraph->cmdstream_bo;

   if (subgraph->coefs_rsrc) {
      job.region_bo_handles[COEFS_REGION] = ethosu_resource(subgraph->coefs_rsrc)->handle;
      if (!DBG_ENABLED(ETHOSU_DBG_DISABLE_SRAM)) {
         job.region_bo_handles[SCRATCH_REGION] = 0;
         job.sram_size = ethosu_ml_device(subgraph->base.device)->sram_size;
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
      pipe_buffer_map(pcontext, subgraph->io_rsrc, PIPE_MAP_READ, &transfer_in);
      pipe_buffer_unmap(pcontext, transfer_in);

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
         uint8_t *buf = pipe_buffer_map(pcontext, subgraph->io_rsrc,
                                        PIPE_MAP_READ, &transfer_in);
         ethosu_dump_buffer(buf, "io-after", 0, 0, 0, pipe_buffer_size(subgraph->io_rsrc));
         pipe_buffer_unmap(pcontext, transfer_in);
      }

      pipe_buffer_read(pcontext, subgraph->io_rsrc, output->offset, output->size, outputs[i]);
   }
}

void
ethosu_ml_subgraph_destroy(struct pipe_ml_device *pdevice,
                           struct pipe_ml_subgraph *psubgraph)
{
   struct ethosu_subgraph *subgraph = (struct ethosu_subgraph *)(psubgraph);

   if (subgraph->io_rsrc) {
      /* Post-submission state: cleanup DRM resources */
      struct ethosu_screen *screen = subgraph->screen;
      struct drm_gem_close arg = {0};
      int ret;

      pipe_resource_reference(&subgraph->io_rsrc, NULL);
      pipe_resource_reference(&subgraph->coefs_rsrc, NULL);

      if (subgraph->cmdstream_bo) {
         arg.handle = subgraph->cmdstream_bo;
         ret = drmIoctl(screen->fd, DRM_IOCTL_GEM_CLOSE, &arg);
         assert(ret >= 0);
      }
   } else {
      /* Pre-submission state: cleanup raw buffers */
      free(subgraph->cmdstream);
      free(subgraph->coefs);
   }

   util_dynarray_fini(&subgraph->tensors);

   free(subgraph);
}
