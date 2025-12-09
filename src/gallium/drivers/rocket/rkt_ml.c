/*
 * Copyright (c) 2024 Tomeu Vizoso <tomeu@tomeuvizoso.net>
 * SPDX-License-Identifier: MIT
 */

#include "pipe/p_state.h"
#include "util/macros.h"
#include "util/u_dynarray.h"
#include "util/u_inlines.h"

#include <xf86drm.h>

#include "drm-uapi/rocket_accel.h"

#include "rkt_coefs.h"
#include "rkt_ml.h"
#include "rkt_regcmd.h"
#include "rkt_task.h"

void
rkt_dump_buffer(const uint8_t *ptr, char *name, int operation_nr,
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

static void
create_tensor(struct rkt_ml_subgraph *subgraph, unsigned idx,
              unsigned size)
{
   struct pipe_context *context = subgraph->base.context;
   struct pipe_resource **tensors = util_dynarray_begin(&subgraph->tensors);

   assert(idx < util_dynarray_num_elements(&subgraph->tensors,
                                           struct pipe_resource *));

   struct pipe_resource *res = tensors[idx];

   if (res != NULL) {
      assert(size == pipe_buffer_size(res));
      return;
   }

   res = pipe_buffer_create(context->screen, 0, PIPE_USAGE_DEFAULT, size);
   tensors[idx] = res;
}

struct rkt_resource *
rkt_get_tensor(struct rkt_ml_subgraph *subgraph,
               unsigned idx)
{
   return rkt_resource(
      *util_dynarray_element(&subgraph->tensors, struct pipe_resource *, idx));
}

bool
rkt_is_depthwise(const struct pipe_ml_operation *poperation)
{
   unsigned input_channels = poperation->input_tensors[0]->dims[3];
   unsigned output_channels = poperation->output_tensors[0]->dims[3];

   return poperation->conv.depthwise && input_channels > 1 &&
          output_channels > 1;
}

static unsigned
calc_raw_output_size(struct rkt_operation *operation)
{
   unsigned output_channels_1 =
      DIV_ROUND_UP(operation->output_channels, FEATURE_ATOMIC_SIZE) * 2;
   unsigned output_channels_2 = FEATURE_ATOMIC_SIZE;

   return operation->output_width * operation->output_height *
          output_channels_1 * output_channels_2;
}

static void
compile_operation(struct rkt_ml_subgraph *subgraph,
                  struct rkt_operation *operation)
{
   struct pipe_context *pcontext = subgraph->base.context;
   unsigned regcfg_total_size = 0;
   struct util_dynarray *regcfgs;
   struct pipe_transfer *transfer = NULL;
   unsigned num_tasks =
      util_dynarray_num_elements(&operation->tasks, struct split_task);

   regcfgs = calloc(num_tasks, sizeof(struct util_dynarray));

   for (int i = 0; i < num_tasks; i++) {
      regcfgs[i] = UTIL_DYNARRAY_INIT;
      rkt_fill_regcmd(subgraph, operation, &regcfgs[i], i);

      unsigned size =
         util_dynarray_num_elements(&regcfgs[i], uint64_t) * sizeof(uint64_t);
      regcfg_total_size += align(size, 64);
   }

   operation->regcmd = pipe_buffer_create(pcontext->screen, 0,
                                          PIPE_USAGE_DEFAULT, regcfg_total_size);
   uint8_t *regcmd =
      pipe_buffer_map(pcontext, operation->regcmd, PIPE_MAP_WRITE, &transfer);

   unsigned regcmd_offset = 0;
   for (int i = 0; i < num_tasks; i++) {
      unsigned size = util_dynarray_num_elements(&regcfgs[i], uint64_t);
      struct split_task *task =
         util_dynarray_element(&operation->tasks, struct split_task, i);

      if (i < num_tasks - 1) {
         /* Patch next address and amount of regs to fetch, positions are relative
          * to end */
         unsigned reg_count = util_dynarray_num_elements(&regcfgs[i], uint64_t);
         uint64_t *next_address_reg =
            util_dynarray_element(&regcfgs[i], uint64_t, reg_count - 4);
         uint64_t *reg_count_reg =
            util_dynarray_element(&regcfgs[i], uint64_t, reg_count - 3);

         uint64_t addr = rkt_resource(operation->regcmd)->phys_addr +
                         regcmd_offset + align(size * sizeof(uint64_t), 64);
         *next_address_reg |= addr << 16;

         unsigned regs_to_fetch =
            util_dynarray_num_elements(&regcfgs[i + 1], uint64_t);
         regs_to_fetch -= 4;
         regs_to_fetch = align(regs_to_fetch / 2, 2);
         *reg_count_reg |= regs_to_fetch << 16;
      }

      memcpy(regcmd + regcmd_offset, util_dynarray_begin(&regcfgs[i]),
             size * sizeof(uint64_t));
      util_dynarray_fini(&regcfgs[i]);

      task->regcfg_amount = size;
      task->regcfg_addr =
         rkt_resource(operation->regcmd)->phys_addr + regcmd_offset;

      if (DBG_ENABLED(ROCKET_DBG_DUMP_BOS))
         rkt_dump_buffer(regcmd, "regcmd", 0, i, regcmd_offset,
                         (size + 4) * sizeof(uint64_t));

      regcmd_offset += align(size * sizeof(uint64_t), 64);
   }

   pipe_buffer_unmap(pcontext, transfer);

   for (int i = 0; i < num_tasks; i++) {
      util_dynarray_fini(&regcfgs[i]);
   }

   free(regcfgs);
}

static void
lower_convolution(struct rkt_ml_subgraph *subgraph,
                  const struct pipe_ml_operation *poperation,
                  struct rkt_operation *operation)
{
   operation->tasks = UTIL_DYNARRAY_INIT;

   operation->depthwise = rkt_is_depthwise(poperation);
   operation->padding_same = poperation->conv.padding_same;
   operation->stride = poperation->conv.stride_x;

   operation->input_index = poperation->input_tensors[0]->index;
   operation->input_width = poperation->input_tensors[0]->dims[1];
   operation->input_height = poperation->input_tensors[0]->dims[2];
   operation->input_channels = poperation->input_tensors[0]->dims[3];
   operation->input_zero_point = poperation->input_tensors[0]->zero_point;
   operation->input_scale = poperation->input_tensors[0]->scale;

   operation->output_index = poperation->output_tensors[0]->index;
   operation->output_width = poperation->output_tensors[0]->dims[1];
   operation->output_height = poperation->output_tensors[0]->dims[2];
   operation->output_channels = poperation->output_tensors[0]->dims[3];
   operation->output_zero_point = poperation->output_tensors[0]->zero_point;
   operation->output_scale = poperation->output_tensors[0]->scale;

   operation->weights_width = poperation->conv.weight_tensor->dims[1];
   operation->weights_height = poperation->conv.weight_tensor->dims[2];
   operation->weights_zero_point = poperation->conv.weight_tensor->zero_point;
   operation->weights_scale = poperation->conv.weight_tensor->scale;

   operation->weights = rkt_fill_weights(subgraph, poperation);
   operation->biases =
      rkt_fill_biases(subgraph, poperation, &operation->truncate_bits);
}

static struct rkt_operation *
find_first_consumer(struct rkt_ml_subgraph *subgraph, unsigned tensor_index)
{
   util_dynarray_foreach (&subgraph->operations, struct rkt_operation,
                          operation) {
      if (operation->input_index == tensor_index)
         return operation;
   }

   return NULL;
}

static struct rkt_operation *
find_producer(struct rkt_ml_subgraph *subgraph,
              unsigned tensor_index)
{
   util_dynarray_foreach (&subgraph->operations, struct rkt_operation,
                          operation) {
      if (operation->output_index == tensor_index)
         return operation;
   }

   return NULL;
}

static unsigned
count_tensors(const struct pipe_ml_operation *poperations,
              unsigned count)
{
   unsigned tensor_count = 0;

   for (unsigned i = 0; i < count; i++) {
      const struct pipe_ml_operation *poperation = &poperations[i];
      tensor_count = MAX2(tensor_count, poperation->input_tensors[0]->index);
      tensor_count = MAX2(tensor_count, poperation->output_tensors[0]->index);
      switch (poperation->type) {
      case PIPE_ML_OPERATION_TYPE_CONVOLUTION:
         tensor_count = MAX2(tensor_count, poperation->conv.weight_tensor->index);
         tensor_count = MAX2(tensor_count, poperation->conv.bias_tensor->index);
         break;
      case PIPE_ML_OPERATION_TYPE_ADD:
         tensor_count = MAX2(tensor_count, poperation->input_tensors[1]->index);
         break;
      default:
         DBG("poperation->type %d\n", poperation->type);
         UNREACHABLE("Unsupported ML operation type");
      }
   }

   return tensor_count + 1;
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
rkt_ml_operation_supported(struct pipe_context *pcontext,
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
   default:
      supported = false;
   }

   return supported;
}

struct pipe_ml_subgraph *
rkt_ml_subgraph_create(struct pipe_context *pcontext,
                       const struct pipe_ml_operation *poperations,
                       unsigned count)
{
   struct rkt_ml_subgraph *subgraph;
   unsigned tensor_count;

   subgraph = calloc(1, sizeof(*subgraph));
   subgraph->base.context = pcontext;

   tensor_count = count_tensors(poperations, count);
   subgraph->tensors = UTIL_DYNARRAY_INIT;
   subgraph->operations = UTIL_DYNARRAY_INIT;
   if (!util_dynarray_resize(&subgraph->tensors, struct pipe_resource *,
                             tensor_count))
      return NULL;
   memset(util_dynarray_begin(&subgraph->tensors), 0, subgraph->tensors.size);

   /* Lower */
   for (int i = 0; i < count; i++) {
      struct rkt_operation operation = {0};
      operation.add_tensor = -1;

      switch (poperations[i].type) {
      case PIPE_ML_OPERATION_TYPE_CONVOLUTION:
         lower_convolution(subgraph, &poperations[i], &operation);
         util_dynarray_append(&subgraph->operations, operation);
         break;
      case PIPE_ML_OPERATION_TYPE_ADD: {
         /* Fuse tensor addition into convolution*/
         struct rkt_operation *input_op_1 =
            find_producer(subgraph, poperations[i].input_tensors[1]->index);
         struct rkt_operation *input_op_2 =
            find_producer(subgraph, poperations[i].input_tensors[0]->index);

         assert(input_op_1);
         assert(input_op_2);

         if (input_op_1 == NULL) {
            /* Graph input */
            input_op_2->add_tensor = poperations[i].input_tensors[1]->index;
         } else {
            input_op_1->addition_input = true;
            input_op_2->add_tensor = input_op_1->output_index;
         }

         input_op_2->output_index = poperations[i].output_tensors[0]->index;
         input_op_2->addition_offset =
            0x80 - poperations[i].input_tensors[1]->zero_point;
         input_op_2->addition_scale = poperations[i].input_tensors[1]->scale;

         break;
      }
      default:
         DBG("poperation->type %d\n", poperations[i].type);
         UNREACHABLE("Unsupported ML operation type");
      }
   }

   /* Create input tensors */
   util_dynarray_foreach (&subgraph->operations, struct rkt_operation,
                          operation) {
      unsigned input_channels_1 =
         DIV_ROUND_UP(operation->input_channels, FEATURE_ATOMIC_SIZE) * 2;
      unsigned input_channels_2 = FEATURE_ATOMIC_SIZE;
      unsigned input_size = operation->input_width * operation->input_height *
                            input_channels_1 * input_channels_2;

      create_tensor(subgraph, operation->input_index, input_size);
   }

   /* Create output tensors */
   util_dynarray_foreach (&subgraph->operations, struct rkt_operation,
                          operation) {
      struct rkt_resource *res =
         rkt_get_tensor(subgraph, operation->output_index);
      if (res != NULL)
         continue;

      create_tensor(subgraph, operation->output_index,
                    calc_raw_output_size(operation));
   }

   /* Compile */
   util_dynarray_foreach (&subgraph->operations, struct rkt_operation,
                          operation) {
      rkt_split_tasks(subgraph, operation);
      compile_operation(subgraph, operation);
   }

   return &subgraph->base;
}

void
rkt_ml_subgraph_invoke(struct pipe_context *pcontext,
                       struct pipe_ml_subgraph *psubgraph,
                       unsigned inputs_count, unsigned input_idxs[],
                       void *inputs[], bool is_signed[])
{
   struct rkt_screen *screen = rkt_screen(pcontext->screen);
   struct rkt_ml_subgraph *subgraph = (struct rkt_ml_subgraph *)(psubgraph);
   int ret;

   DBG("Processing input\n");

   for (int i = 0; i < inputs_count; i++) {
      struct rkt_operation *operation =
         find_first_consumer(subgraph, input_idxs[i]);
      struct pipe_resource *input =
         &rkt_get_tensor(subgraph, input_idxs[i])->base;
      unsigned input_channels = operation->input_channels;
      unsigned output_channels = operation->output_channels;

      struct rkt_resource *input_tensor =
         rkt_get_tensor(subgraph, operation->input_index);
      if (output_channels == 1 && input_channels == 1 &&
          !operation->addition_input && (operation->add_tensor == -1)) {
         pipe_buffer_copy(pcontext, &input_tensor->base, input, 0, 0,
                          pipe_buffer_size(input));
      } else {
         unsigned input_width = operation->input_width;
         unsigned input_height = operation->input_height;
         unsigned zero_point = operation->input_zero_point;
         struct pipe_transfer *transfer_out;
         uint8_t(*input_in)[input_height][input_channels] = inputs[i];
         uint8_t *map = pipe_buffer_map(pcontext, &input_tensor->base,
                                        PIPE_MAP_WRITE, &transfer_out);

         DBG("Converting data\n");

         /*
          * From the NVDLA docs: "For int8, one element of data refers to an 8-bit
          * signed integer." But only when transposing do we seem to need to
          * convert to signed. The DMA unit seems to be able to convert from
          * unsigned to signed though.
          */
         if (input_channels == 1) {
            unsigned n = 0;
            for (int x = 0; x < input_width; x++) {
               for (int y = 0; y < MAX2(input_height, FEATURE_ATOMIC_SIZE); y++) {
                  if (y < input_height)
                     map[n++] = input_in[x][y][0];
                  else
                     map[n++] = zero_point;
               }
            }
         } else {
            unsigned n = 0;
            for (int u = 0; u < DIV_ROUND_UP(input_channels, FEATURE_ATOMIC_SIZE);
                 u++) {
               for (int x = 0; x < input_width; x++) {
                  for (int y = 0; y < input_height; y++) {
                     for (int c = 0; c < FEATURE_ATOMIC_SIZE; c++) {
                        unsigned input_channel = c + u * FEATURE_ATOMIC_SIZE;
                        if (input_channel < input_channels)
                           map[n++] = input_in[x][y][input_channel] - 0x80;
                        else
                           map[n++] = zero_point - 0x80;
                     }
                  }
               }
            }
         }

         if (DBG_ENABLED(ROCKET_DBG_DUMP_BOS))
            rkt_dump_buffer(map, "input", 0, 0, 0,
                            rkt_get_tensor(subgraph, input_idxs[i])->bo_size);

         DBG("Converted data\n");

         pipe_buffer_unmap(pcontext, transfer_out);
      }
   }
   DBG("Processed input\n");

   DBG("Submitting graph\n");

   struct util_dynarray jobs = UTIL_DYNARRAY_INIT;

   util_dynarray_foreach (&subgraph->operations, struct rkt_operation,
                          operation) {
      unsigned num_inputs = operation->add_tensor != -1 ? 2 : 1;
      uint32_t *in_bo_handles = calloc(num_inputs, sizeof(uint32_t));
      uint32_t *out_bo_handles = malloc(sizeof(uint32_t));

      in_bo_handles[0] = rkt_get_tensor(subgraph, operation->input_index)->handle;

      if (operation->add_tensor != -1)
         in_bo_handles[1] =
            rkt_get_tensor(subgraph, operation->add_tensor)->handle;

      out_bo_handles[0] =
         rkt_get_tensor(subgraph, operation->output_index)->handle;

      if (operation->reuse_weights_cbuf) {
         /* Submit all tasks to the same core, so weights can be reused */
         unsigned num_tasks =
            util_dynarray_num_elements(&operation->tasks, struct split_task);
         struct drm_rocket_task *tasks = calloc(num_tasks, sizeof(*tasks));
         unsigned task_count = 0;
         util_dynarray_foreach (&operation->tasks, struct split_task, task) {
            tasks[task_count].regcmd = task->regcfg_addr;
            tasks[task_count].regcmd_count = task->regcfg_amount;
            task_count++;
         }
         struct drm_rocket_job job = {0};
         job.task_struct_size = sizeof(struct drm_rocket_task);
         job.in_bo_handles = (uint64_t)(uintptr_t)in_bo_handles;
         job.in_bo_handle_count = num_inputs;
         job.out_bo_handles = (uint64_t)(uintptr_t)out_bo_handles;
         job.out_bo_handle_count = 1;
         job.tasks = (uint64_t)tasks;
         job.task_count = task_count;
         util_dynarray_append(&jobs, job);
      } else {
         /* Spread tasks among cores, for parallelism */
         util_dynarray_foreach (&operation->tasks, struct split_task, task) {
            struct drm_rocket_task *ktask = calloc(1, sizeof(*ktask));
            ktask->regcmd = task->regcfg_addr;
            ktask->regcmd_count = task->regcfg_amount;

            struct drm_rocket_job job = {0};
            job.task_struct_size = sizeof(struct drm_rocket_task);
            job.in_bo_handles = (uint64_t)(uintptr_t)in_bo_handles;
            job.in_bo_handle_count = num_inputs;
            job.out_bo_handles = (uint64_t)(uintptr_t)out_bo_handles;
            job.out_bo_handle_count = 1;
            job.tasks = (uint64_t)ktask;
            job.task_count = 1;
            util_dynarray_append(&jobs, job);
         }
      }
   }

   struct drm_rocket_submit submit = {0};
   submit.job_struct_size = sizeof(struct drm_rocket_job);
   submit.jobs = (uint64_t)util_dynarray_begin(&jobs);
   submit.job_count = util_dynarray_num_elements(&jobs, struct drm_rocket_job);

   ret = drmIoctl(screen->fd, DRM_IOCTL_ROCKET_SUBMIT, &submit);
   assert(ret == 0);

   util_dynarray_foreach (&jobs, struct drm_rocket_job, job) {
      free((void *)job->in_bo_handles);
      free((void *)job->out_bo_handles);
      free((void *)job->tasks);
   }
   util_dynarray_fini(&jobs);

   DBG("Submitted graph\n");
}

void
rkt_ml_subgraph_read_outputs(struct pipe_context *pcontext,
                             struct pipe_ml_subgraph *psubgraph,
                             unsigned outputs_count,
                             unsigned output_idxs[], void *outputs[],
                             bool is_signed[])
{
   struct rkt_ml_subgraph *subgraph = (struct rkt_ml_subgraph *)(psubgraph);

   DBG("Processing output\n");

   for (int i = 0; i < outputs_count; i++) {

      struct rkt_operation *operation = find_producer(subgraph, output_idxs[i]);
      struct rkt_resource *output_tensor =
         rkt_get_tensor(subgraph, output_idxs[i]);
      struct pipe_transfer *transfer = NULL;
      uint8_t *raw_output;
      uint8_t(*output_in)[operation->output_height][operation->output_width]
                         [FEATURE_ATOMIC_SIZE];
      uint8_t(*output_out)[operation->output_width][operation->output_channels];

      DBG("Before pipe_buffer_map\n");
      raw_output = pipe_buffer_map(pcontext, &output_tensor->base, PIPE_MAP_READ,
                                   &transfer);
      DBG("After pipe_buffer_map\n");

      DBG("Converting data\n");

      output_in = (void *)raw_output;
      output_out = (void *)outputs[i];

      if (DBG_ENABLED(ROCKET_DBG_DUMP_BOS))
         rkt_dump_buffer(raw_output, "output", 0, 0, 0, output_tensor->bo_size);

      for (int oc = 0; oc < operation->output_channels; oc++) {
         for (int x = 0; x < operation->output_width; x++) {
            for (int y = 0; y < operation->output_height; y++) {
               unsigned c = oc % FEATURE_ATOMIC_SIZE;
               unsigned g = oc / FEATURE_ATOMIC_SIZE;
               output_out[y][x][oc] = output_in[g][y][x][c] + 0x80;
            }
         }
      }

      DBG("Converted data\n");

      pipe_buffer_unmap(pcontext, transfer);
   }

   DBG("Processed output\n");
}

static void
free_operation(struct rkt_operation *operation)
{
   util_dynarray_fini(&operation->tasks);
   pipe_resource_reference(&operation->regcmd, NULL);
   pipe_resource_reference(&operation->weights, NULL);
   pipe_resource_reference(&operation->biases, NULL);
}

void
rkt_ml_subgraph_destroy(struct pipe_context *context,
                        struct pipe_ml_subgraph *psubgraph)
{
   struct rkt_ml_subgraph *subgraph = (struct rkt_ml_subgraph *)(psubgraph);

   util_dynarray_foreach (&subgraph->operations, struct rkt_operation, operation)
      free_operation(operation);
   util_dynarray_fini(&subgraph->operations);

   util_dynarray_foreach (&subgraph->tensors, struct pipe_resource *, tensor)
      if (tensor)
         pipe_resource_reference(tensor, NULL);
   util_dynarray_fini(&subgraph->tensors);

   free(subgraph);
}
