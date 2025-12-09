/*
 * Copyright (c) 2024 Tomeu Vizoso <tomeu@tomeuvizoso.net>
 * SPDX-License-Identifier: MIT
 */

#include "rkt_task.h"
#include "rkt_ml.h"

static unsigned
calc_entries_per_slice(struct rkt_operation *operation)
{
   unsigned bpe = sizeof(uint8_t);
   unsigned atomics_per_entry = CBUF_ENTRY_SIZE / FEATURE_ATOMIC_SIZE;
   unsigned total_c_atomics =
      DIV_ROUND_UP(operation->input_channels * bpe, FEATURE_ATOMIC_SIZE);
   unsigned last_c_atomics = total_c_atomics % atomics_per_entry;
   unsigned int_c_entries =
      (total_c_atomics / atomics_per_entry) * operation->input_width;
   unsigned frac_c_entries =
      (last_c_atomics == 3)
         ? operation->input_width
         : DIV_ROUND_UP(last_c_atomics * operation->input_width,
                        atomics_per_entry);

   return int_c_entries + frac_c_entries;
}

static unsigned
calc_input_banks(struct rkt_operation *operation)
{
   unsigned entries_per_slice = calc_entries_per_slice(operation);
   return DIV_ROUND_UP(entries_per_slice * operation->input_height,
                       CBUF_ENTRIES_PER_BANK);
}

static unsigned
calc_weights_banks(struct rkt_operation *operation)
{
   unsigned bpe = sizeof(uint8_t);
   unsigned bytes = operation->weights_width * operation->weights_height *
                    operation->input_channels * bpe;
   unsigned entries;
   unsigned banks;

   if (!operation->depthwise)
      bytes *= operation->output_channels;
   entries = DIV_ROUND_UP(bytes, CBUF_ENTRY_SIZE);
   banks = DIV_ROUND_UP(entries, CBUF_ENTRIES_PER_BANK);

   /* Why do we need an extra bank? The calc above might be wrong on this HW */
   banks++;

   return banks;
}

static unsigned
calc_line_stride(unsigned width)
{
   return width * ATOMIC_K_SIZE * sizeof(uint8_t);
}

static void
calc_explicit_padding(const struct rkt_operation *operation,
                      unsigned *pad_top, unsigned *pad_bottom,
                      unsigned *pad_left, unsigned *pad_right)
{
   if (operation->padding_same && operation->weights_width > 1) {
      /* Convert from implicit to explicit padding */
      unsigned pad_along_width =
         MAX2((operation->output_width - 1) * operation->stride +
                 operation->weights_width - operation->input_width,
              0);
      unsigned pad_along_height =
         MAX2((operation->output_height - 1) * operation->stride +
                 operation->weights_height - operation->input_height,
              0);
      *pad_left = pad_along_height / 2;
      *pad_right = pad_along_height - *pad_left;
      *pad_top = pad_along_width / 2;
      *pad_bottom = pad_along_width - *pad_top;
   } else {
      *pad_left = 0;
      *pad_right = 0;
      *pad_top = 0;
      *pad_bottom = 0;
   }
}

static void
fill_task(struct rkt_ml_subgraph *subgraph,
          struct rkt_operation *operation,
          struct split_task *task)
{
   task->stride_x = operation->stride;
   task->stride_y = operation->stride;

   task->input_width = operation->input_width;
   if (task->input_width == 8 &&
       (operation->addition_input || operation->add_tensor != -1))
      task->input_width *= 2;

   task->input_height = operation->input_height;
   task->input_channels =
      align(MAX2(operation->input_channels, FEATURE_ATOMIC_SIZE),
            FEATURE_ATOMIC_SIZE);
   task->input_channels_real = operation->input_channels;
   task->input_zero_point = operation->input_zero_point;
   task->input_scale = operation->input_scale;

   task->output_width = operation->output_width;
   task->output_height = operation->output_height;

   task->output_channels_real = operation->output_channels;
   task->output_channels = align(MAX2(operation->output_channels, 32), 32);
   if (operation->depthwise) {
      if (task->output_channels_real <= 32)
         task->output_channels *= 2;
      task->output_channels = align(task->output_channels, 64);
   }

   task->output_zero_point = operation->output_zero_point;
   task->output_scale = operation->output_scale;

   if (task->input_channels_real == 1 &&
       (task->output_channels_real > 1 ||
        (operation->addition_input || operation->add_tensor != -1))) {
      task->input_width = MAX2(task->input_width, FEATURE_ATOMIC_SIZE);
      task->input_line_stride =
         MAX2(calc_line_stride(operation->input_width) / FEATURE_ATOMIC_SIZE,
              FEATURE_ATOMIC_SIZE);

      if (operation->input_channels == 32 && operation->input_width == 80) {
         task->input_line_stride *= 4;
         task->input_surface_stride = (float)task->input_line_stride *
                                      (((float)task->input_height / 4) - 1);
      } else
         task->input_surface_stride =
            (float)task->input_line_stride * (((float)task->input_height) - 1);
   } else {
      task->input_line_stride = calc_line_stride(operation->input_width) / 4;
      task->input_surface_stride =
         (float)task->input_line_stride * (((float)task->input_height / 4) - 1);
   }

   if (task->input_width == 8 &&
       (operation->addition_input || operation->add_tensor != -1)) {
      task->input_line_stride /= 2;
      task->input_surface_stride = 112;
   }

   int output_line_stride = calc_line_stride(operation->output_width);
   task->output_surface_stride = output_line_stride * task->output_height;
   task->output_surface_stride /= FEATURE_ATOMIC_SIZE;

   if (task->input_channels_real == 1)
      task->input_data_entries = task->input_width * task->input_height;
   else if (task->input_width == 40 && task->input_channels_real == 40)
      task->input_data_entries = 40;
   else
      task->input_data_entries = DIV_ROUND_UP(
         task->input_width * 2 *
            DIV_ROUND_UP(task->input_channels_real, FEATURE_ATOMIC_SIZE),
         8);

   task->weights_width = operation->weights_width;
   task->weights_height = operation->weights_height;
   task->weights_zero_point = operation->weights_zero_point;
   task->weights_scale = operation->weights_scale;

   if (operation->depthwise)
      task->weights_kernels = 1;
   else
      task->weights_kernels = align(operation->output_channels, 2);

   task->surfaces_per_row = task->output_width * task->output_height * 2;
   if (operation->depthwise)
      task->surfaces_per_row *= 2;
}

void
rkt_split_tasks(struct rkt_ml_subgraph *subgraph,
                struct rkt_operation *operation)
{
   /* Function mostly taken from NVDLA */
   unsigned entries_per_slice = calc_entries_per_slice(operation);
   unsigned input_banks_required = calc_input_banks(operation);
   unsigned weights_banks_required = calc_weights_banks(operation);
   unsigned available_weights_banks = weights_banks_required;
   unsigned available_input_banks = CBUF_BANKS - weights_banks_required;
   unsigned pad_top;
   unsigned pad_bottom;
   unsigned pad_left;
   unsigned pad_right;

   calc_explicit_padding(operation, &pad_top, &pad_bottom, &pad_left,
                         &pad_right);

   if (weights_banks_required + 1 < CBUF_BANKS) {
      /* Full weights, partial input */
      operation->reuse_weights_cbuf = true;
   } else {
      /* Partial weights, partial input */
      operation->reuse_weights_cbuf = false;
      available_input_banks = 7;
      available_weights_banks = CBUF_BANKS - available_input_banks;
   }

   if (input_banks_required <= available_input_banks) {
      /* Full weights, full input */

      struct split_task task = {0};

      task.num = 0;
      fill_task(subgraph, operation, &task);
      task.input_banks = input_banks_required;
      task.weights_banks = CBUF_BANKS - task.input_banks;
      task.input_height = operation->input_height;

      task.pad_top = pad_top;
      task.pad_bottom = pad_bottom;
      task.pad_left = pad_left;
      task.pad_right = pad_right;

      task.atomic_count = task.output_width * task.output_height;

      util_dynarray_append(&operation->tasks, task);

      return;
   }

   struct split_task task = {0};
   unsigned available_slices =
      (CBUF_ENTRIES_PER_BANK * available_input_banks) / entries_per_slice;

   task.num = 0;
   fill_task(subgraph, operation, &task);
   task.input_banks = available_input_banks;
   task.weights_banks = available_weights_banks;

   task.top_slice = 0;
   task.bottom_slice = available_slices - 1;

   task.pad_top = pad_top;
   task.pad_left = pad_left;
   task.pad_right = pad_right;

   util_dynarray_append(&operation->tasks, task);

   for (unsigned slice = operation->weights_height - pad_top - 1;
        slice < operation->input_height;) {
      memset(&task, 0, sizeof(task));

      struct split_task *prev_task = util_dynarray_element(
         &operation->tasks, struct split_task,
         util_dynarray_num_elements(&operation->tasks, struct split_task) - 1);

      while (slice <= prev_task->bottom_slice) {
         slice += operation->stride;
      }
      if (slice > prev_task->bottom_slice) {
         slice -= operation->stride;
      }

      task.num = util_dynarray_num_elements(&operation->tasks, struct split_task);
      fill_task(subgraph, operation, &task);
      task.top_slice = MIN2(slice, prev_task->bottom_slice) -
                       (operation->weights_height - 1) + operation->stride;
      task.bottom_slice = task.top_slice + available_slices - 1;
      task.pad_left = pad_left;
      task.pad_right = pad_right;

      // check if current task is the last one
      if (task.bottom_slice >= operation->input_height - 1) {
         task.bottom_slice = operation->input_height - 1;
         task.pad_bottom = pad_bottom;
         util_dynarray_append(&operation->tasks, task);
         break;
      }

      slice = task.top_slice + operation->weights_height - 1;
      util_dynarray_append(&operation->tasks, task);
   }

   struct split_task *last_task = util_dynarray_element(
      &operation->tasks, struct split_task,
      util_dynarray_num_elements(&operation->tasks, struct split_task) - 1);
   if (last_task->top_slice >= operation->input_height ||
       last_task->bottom_slice >= (operation->input_height + pad_bottom)) {
      (void)util_dynarray_pop_ptr(&operation->tasks, struct split_task);
   }

   // determine overlap slices between 2 split chunks
   for (int i = 1;
        i < util_dynarray_num_elements(&operation->tasks, struct split_task);
        i++) {
      struct split_task *prev_task =
         util_dynarray_element(&operation->tasks, struct split_task, i - 1);
      struct split_task *cur_task =
         util_dynarray_element(&operation->tasks, struct split_task, i);

      if (prev_task->bottom_slice >= cur_task->top_slice) {
         cur_task->num_overlap_slices =
            prev_task->bottom_slice - cur_task->top_slice + 1;
         prev_task->num_retain_slices = cur_task->num_overlap_slices;
      } else {
         cur_task->num_overlap_slices = 0;
         prev_task->num_retain_slices = 0;
      }
   }

   unsigned output_height_processed = 0;
   for (int i = 0;
        i < util_dynarray_num_elements(&operation->tasks, struct split_task);
        i++) {
      struct split_task *cur_task =
         util_dynarray_element(&operation->tasks, struct split_task, i);

      unsigned slice = cur_task->top_slice + (operation->weights_height - 1) -
                       cur_task->pad_top;

      while (slice <= cur_task->bottom_slice + cur_task->pad_bottom) {
         slice += operation->stride;
         cur_task->convolutions++;
      }

      cur_task->bottom_slice =
         MIN2(cur_task->bottom_slice, operation->input_height - 1);

      cur_task->input_height = cur_task->bottom_slice - cur_task->top_slice + 1;

      cur_task->output_width = (cur_task->input_width + cur_task->pad_left +
                                cur_task->pad_right - operation->weights_width) /
                                  operation->stride +
                               1;
      cur_task->output_height =
         (cur_task->input_height + cur_task->pad_top + cur_task->pad_bottom -
          operation->weights_height) /
            operation->stride +
         1;
      cur_task->atomic_count = cur_task->output_width * cur_task->output_height;

      cur_task->input_offset =
         calc_line_stride(operation->input_width) * cur_task->top_slice;
      cur_task->output_offset =
         calc_line_stride(operation->output_width) * output_height_processed;

      cur_task->input_banks = available_input_banks;
      cur_task->weights_banks = available_weights_banks;

      output_height_processed += cur_task->output_height;
   }
}
