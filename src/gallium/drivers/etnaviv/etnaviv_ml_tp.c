/*
 * Copyright (c) 2023-2024 Tomeu Vizoso <tomeu@tomeuvizoso.net>
 * SPDX-License-Identifier: MIT
 */

#include "hw/state.xml.h"
#include "util/u_inlines.h"

#include "etnaviv_context.h"
#include "etnaviv_debug.h"
#include "etnaviv_emit.h"
#include "etnaviv_ml.h"
#include "etnaviv_ml_tp.h"

#define DIMS3(w, h, c) (input_width == w && input_height == h && input_channels == c)
#define FIELD(field, bits) uint32_t field : bits;

struct etna_tp_params {
   /* 0 */
   FIELD(in_image_x_size, 16)
   FIELD(unused0, 16)

   /* 1 */
   FIELD(in_image_y_size, 16)
   FIELD(in_image_z_size, 16)

   /* 2 */
   FIELD(in_image_stride, 16)
   FIELD(unused1, 16)

   /* 3 */
   FIELD(in_image_slice, 32)

   /* 4 */
   FIELD(in_window_x_start, 16)
   FIELD(in_window_y_start, 16)

   /* 5 */
   FIELD(in_window_x_end, 16)
   FIELD(in_window_y_end, 16)

   /* 6 */
   FIELD(in_tile_sequence, 2)
   FIELD(in_tile_global_mem, 1)
   FIELD(in_image_global_mem, 1)
   FIELD(alu_i2f_enable, 1)
   FIELD(alu_square_enable, 1)
   FIELD(alu_horz_processing, 3) /* Watch out, it is split in two in the blob */
   FIELD(alu_horz_proc_count, 6)
   FIELD(alu_horz_proc_stride, 1)
   FIELD(alu_vert_processing, 2)
   FIELD(unused2, 1)
   FIELD(alu_vert_proc_count, 6)
   FIELD(alu_vert_proc_stride, 1)
   FIELD(alu_nms_enable, 1)
   FIELD(alu_pwl_enable, 1)
   FIELD(alu_mult_enable, 1)
   FIELD(alu_f2i_enable, 1)
   FIELD(alu_load_pwl_lut, 1)
   FIELD(alu_load_pwl_lut_global_mem, 1)

   /* 7 */
   FIELD(in_tile_list_address, 32)

   /* 8 */
   FIELD(in_tile_x_size, 16)
   FIELD(in_tile_y_size, 16)

   /* 9 */
   FIELD(in_tile_x_inc, 16)
   FIELD(in_tile_y_inc, 16)

   /* 10 */
   FIELD(in_image_base_address, 32)

   /* 11 */
   FIELD(alu_load_pwl_lut_address, 32)

   /* 12 */
   FIELD(out_tile_skip_at_border, 1)
   FIELD(out_image_global_mem, 1)
   FIELD(out_loop_1_reset, 1)
   FIELD(out_loop_2_reset, 1)
   FIELD(out_loop_3_reset, 1)
   FIELD(out_brick_mode, 1)
   FIELD(alu_z_filter_mode, 1)
   FIELD(unused3, 1)
   FIELD(in_window_z_start_overfetch, 2)
   FIELD(unused4, 1)
   FIELD(in_window_z_end_overfetch, 2)
   FIELD(unused5, 1)
   FIELD(alu_square_preshift, 4)
   FIELD(in_image_data_type, 3)
   FIELD(out_image_data_type, 3)
   FIELD(unused6, 4)
   FIELD(alu_pwl_sign_support, 1)
   FIELD(alu_relu_enable, 1)
   FIELD(no_flush, 1)
   FIELD(last, 1)

   /* 13 */
   FIELD(out_image_base_address, 32)

   /* 14 */
   FIELD(out_loop_0_inc, 32)

   /* 15 */
   FIELD(out_loop_1_inc, 32)

   /* 16 */
   FIELD(out_loop_0_count, 16)
   FIELD(out_loop_1_count, 16)

   /* 17 */
   FIELD(out_loop_2_inc, 32)

   /* 18 */
   FIELD(out_loop_3_inc, 32)

   /* 19 */
   FIELD(out_loop_2_count, 16)
   FIELD(out_loop_3_count, 16)

   /* 20 */
   FIELD(out_loop_4_inc, 32)

   /* 21 */
   FIELD(out_loop_5_inc, 32)

   /* 22 */
   FIELD(out_loop_4_count, 16)
   FIELD(out_loop_5_count, 16)

   /* 23 */
   FIELD(out_loop_6_inc, 32)

   /* 24 */
   FIELD(alu_filter_pwl_swap, 1)
   FIELD(flat_rounding_mode, 2)
   FIELD(integer_rounding_mode, 2)
   FIELD(alu_input_preshift, 5)
   FIELD(alu_output_postshift, 5)
   FIELD(alu_reorder_bits_used, 4)
   FIELD(alu_reorder_loop_2_mode, 1)
   FIELD(unused7, 4)
   FIELD(in_image_border_mode, 2)
   FIELD(alu_output_postshift_5_6, 2)
   FIELD(unused8, 4)

   /* 25 */
   FIELD(in_image_circular_buf_size, 32)  /* >> 6 */

   /* 26 */
   FIELD(in_image_circular_buf_end_address_plus_1, 32)  /* >> 6 */

   /* 27 */
   FIELD(out_image_circular_buf_size, 32)  /* >> 6 */

   /* 28 */
   FIELD(out_image_circular_buf_end_address_plus_1, 32)  /* >> 6 */

   /* 29 */
   FIELD(in_image_border_const, 16)
   FIELD(coef_zp, 8)
   FIELD(in_zp, 8)

   /* 30 */
   FIELD(out_zp, 8)
   FIELD(alu_output_post_multiplier, 15)
   FIELD(unused9, 9)
};

static void
set_default_tp_config(struct etna_tp_params *map)
{
   map->unused0 = 0x0;
   map->unused1 = 0x0;
   map->in_window_x_start = 0x0;
   map->in_window_y_start = 0x0;
   map->in_tile_sequence = 0x0;
   map->in_tile_global_mem = 0x0;
   map->in_image_global_mem = 0x1;
   map->alu_i2f_enable = 0x1;
   map->alu_square_enable = 0x0;
   map->alu_horz_processing = 0x0;
   map->alu_horz_proc_count = 0x0;
   map->alu_horz_proc_stride = 0x0;
   map->alu_vert_processing = 0x0;
   map->unused2 = 0x0;
   map->alu_vert_proc_count = 0x0;
   map->alu_vert_proc_stride = 0x0;
   map->alu_nms_enable = 0x0;
   map->alu_pwl_enable = 0x0;
   map->alu_mult_enable = 0x0;
   map->alu_f2i_enable = 0x1;
   map->alu_load_pwl_lut = 0x0;
   map->alu_load_pwl_lut_global_mem = 0x0;
   map->in_tile_list_address = 0x0;
   map->in_tile_x_size = 0x1;
   map->in_tile_x_inc = 0x1;
   map->alu_load_pwl_lut_address = 0x0;
   map->out_tile_skip_at_border = 0x0;
   map->out_image_global_mem = 0x1;
   map->out_loop_1_reset = 0x0;
   map->out_loop_2_reset = 0x0;
   map->out_loop_3_reset = 0x0;
   map->out_brick_mode = 0x0;
   map->alu_z_filter_mode = 0x0;
   map->unused3 = 0x0;
   map->in_window_z_start_overfetch = 0x0;
   map->unused4 = 0x0;
   map->in_window_z_end_overfetch = 0x0;
   map->unused5 = 0x0;
   map->alu_square_preshift = 0x0;
   map->in_image_data_type = 0x0;
   map->out_image_data_type = 0x0;
   map->unused6 = 0x0;
   map->alu_pwl_sign_support = 0x0;
   map->alu_relu_enable = 0x0;
   map->no_flush = 0x0;
   map->last = 0x1;
   map->out_loop_0_inc = 0x1;
   map->out_loop_3_inc = 0x0;
   map->out_loop_3_count = 0x1;
   map->out_loop_4_inc = 0x0;
   map->out_loop_5_inc = 0x0;
   map->out_loop_4_count = 0x1;
   map->out_loop_5_count = 0x1;
   map->out_loop_6_inc = 0x0;
   map->alu_filter_pwl_swap = 0x0;
   map->flat_rounding_mode = 0x1;
   map->integer_rounding_mode = 0x1;
   map->alu_input_preshift = 0x0;
   map->alu_output_postshift = 0x0;
   map->alu_reorder_bits_used = 0x0;
   map->alu_reorder_loop_2_mode = 0x0;
   map->unused7 = 0x0;
   map->in_image_border_mode = 0x0;
   map->alu_output_postshift_5_6 = 0x0;
   map->unused8 = 0x0;
   map->in_image_border_const = 0x0;
   map->coef_zp = 0x0;
   map->alu_output_post_multiplier = 0x0;
   map->unused9 = 0x0;
}

static struct etna_bo *
create_transpose_config(struct etna_ml_subgraph *subgraph, const struct etna_operation *operation)
{
   struct etna_bo *bo = etna_ml_create_bo(subgraph->base.context, sizeof(struct etna_tp_params));

   etna_bo_cpu_prep(bo, DRM_ETNA_PREP_WRITE);

   struct etna_tp_params *map = etna_bo_map(bo);

   set_default_tp_config(map);

   map->in_image_x_size = operation->input_channels;
   map->in_image_y_size = operation->input_height;
   map->in_image_z_size = operation->input_width;
   map->in_image_stride = operation->input_channels;
   map->in_image_slice = operation->input_height * operation->input_channels;
   map->in_window_x_end = operation->input_channels - 1;
   map->in_window_y_end = operation->input_height - 1;
   map->in_tile_y_size = operation->input_height;
   map->in_tile_y_inc = operation->input_height;

   struct pipe_resource *input = etna_ml_get_resource(subgraph, operation->input_tensors[0]);
   unsigned offset = etna_ml_get_offset(subgraph, operation->input_tensors[0]);
   map->in_image_base_address = etna_bo_gpu_va(etna_buffer_resource(input)->bo) + offset;

   struct pipe_resource *output = etna_ml_get_resource(subgraph, operation->output_tensors[0]);
   offset = etna_ml_get_offset(subgraph, operation->output_tensors[0]);
   map->out_image_base_address = etna_bo_gpu_va(etna_buffer_resource(output)->bo) + offset;

   map->out_loop_1_inc = operation->input_width * operation->input_height;
   map->out_loop_0_count = operation->input_height;
   map->out_loop_1_count = operation->input_channels;
   map->out_loop_2_inc = operation->input_height;
   map->out_loop_2_count = operation->input_width;
   map->in_image_circular_buf_size = 0x0;
   map->in_image_circular_buf_end_address_plus_1 = 0xFFFFFFFF >> 6;
   map->out_image_circular_buf_size = 0x0;
   map->out_image_circular_buf_end_address_plus_1 = 0xFFFFFFFF >> 6;
   map->in_zp = operation->input_zero_point;
   map->out_zp = operation->input_zero_point;
   map->no_flush = 0x0;

   etna_bo_cpu_fini(bo);

   return bo;
}

static struct etna_bo *
create_detranspose_config(struct etna_ml_subgraph *subgraph, const struct etna_operation *operation)
{
   unsigned input_width = operation->input_width;
   unsigned input_height = operation->input_height;
   unsigned input_channels = operation->input_channels;
   struct etna_bo *bo = etna_ml_create_bo(subgraph->base.context, sizeof(struct etna_tp_params));

   etna_bo_cpu_prep(bo, DRM_ETNA_PREP_WRITE);

   struct etna_tp_params *map = etna_bo_map(bo);

   set_default_tp_config(map);

   map->in_image_x_size = input_width;
   map->in_image_y_size = input_height * input_channels;
   map->in_image_z_size = 0x1;
   map->in_image_stride = input_width;
   map->in_image_slice = input_width * input_height * input_channels;
   map->in_window_x_end = input_width - 1;
   map->in_window_y_end = input_height * input_channels - 1;
   map->in_tile_y_size = 0x1;
   map->in_tile_y_inc = 0x1;

   struct pipe_resource *input = etna_ml_get_resource(subgraph, operation->input_tensors[0]);
   unsigned offset = etna_ml_get_offset(subgraph, operation->input_tensors[0]);
   map->in_image_base_address = etna_bo_gpu_va(etna_buffer_resource(input)->bo) + offset;

   struct pipe_resource *output = etna_ml_get_resource(subgraph, operation->output_tensors[0]);
   offset = etna_ml_get_offset(subgraph, operation->output_tensors[0]);
   map->out_image_base_address = etna_bo_gpu_va(etna_buffer_resource(output)->bo) + offset;

   map->out_loop_0_inc = input_channels;
   map->out_loop_1_inc = 0x0;
   map->out_loop_0_count = input_height;
   map->out_loop_1_count = 0x1;
   map->out_loop_2_inc = input_height * input_channels;
   map->out_loop_2_count = input_width;
   map->out_loop_3_inc = 0x1;
   map->out_loop_3_count = input_channels;
   map->out_loop_4_inc = input_width * input_height * input_channels;
   map->in_image_circular_buf_size = 0x0;
   map->in_image_circular_buf_end_address_plus_1 = 0xFFFFFFFF >> 6;
   map->out_image_circular_buf_size = 0x0;
   map->out_image_circular_buf_end_address_plus_1 = 0xFFFFFFFF >> 6;
   map->in_zp = operation->input_zero_point;
   map->out_zp = operation->input_zero_point;

   etna_bo_cpu_fini(bo);

   return bo;
}

static unsigned
split_reshuffle(struct etna_ml_subgraph *subgraph, const struct etna_operation *operation, unsigned tp_core, unsigned tp_cores_used, unsigned *in_dims, unsigned *out_dims, unsigned *pad_x_out, unsigned *pad_y_out)
{
   unsigned remaining_out_size, remaining_in_size;
   unsigned dim_to_split = 0;

   if (operation->input_channels >= out_dims[dim_to_split]) {
      if (out_dims[1] >= out_dims[dim_to_split])
         dim_to_split = 1;

      if (out_dims[2] >= out_dims[dim_to_split])
         dim_to_split = 2;
   }

   remaining_in_size = in_dims[dim_to_split];
   remaining_out_size = out_dims[dim_to_split];

   for (unsigned i = 0; i <= tp_core; i++) {
      unsigned size = DIV_ROUND_UP(remaining_out_size, (tp_cores_used - i));
      unsigned pad_x = 0;
      unsigned pad_y = 0;

      if (operation->padding_same) {
         if (operation->weight_width == 5) {
            if (i == 0 || dim_to_split != 0)
               pad_x++;

            if (i == 0 || dim_to_split != 1)
               pad_y++;
         }

         if (operation->input_width % 2)
            if (i == 0 || dim_to_split != 0)
               pad_x++;

         if (operation->input_height % 2)
            if (i == 0 || dim_to_split != 1)
               pad_y++;
      }

      if (i < tp_cores_used - 1) {
         in_dims[dim_to_split] = size;

         if (dim_to_split != 2)
            in_dims[dim_to_split] *= operation->stride;

         if (dim_to_split == 0)
            in_dims[dim_to_split] -= pad_x;
         else if (dim_to_split == 1)
            in_dims[dim_to_split] -= pad_y;

         remaining_in_size -= in_dims[dim_to_split];
      } else
         in_dims[dim_to_split] = remaining_in_size;

      if (i == tp_core) {
         if (pad_x_out)
            *pad_x_out = pad_x;
         if (pad_y_out)
            *pad_y_out = pad_y;
      }

      out_dims[dim_to_split] = size;

      remaining_out_size -= size;
   }

   return dim_to_split;
}

static struct etna_bo *
create_reshuffle_config(struct etna_ml_subgraph *subgraph, const struct etna_operation *operation,
                        unsigned tp_core, unsigned tp_cores_used)
{
   struct etna_bo *bo = etna_ml_create_bo(subgraph->base.context, sizeof(struct etna_tp_params));
   unsigned input_width = operation->input_width;
   unsigned input_height = operation->input_height;
   unsigned output_width = operation->output_width;
   unsigned output_height = operation->output_height;
   unsigned in_dims[3];
   unsigned out_dims[3];

   etna_bo_cpu_prep(bo, DRM_ETNA_PREP_WRITE);

   struct etna_tp_params *map = etna_bo_map(bo);

   set_default_tp_config(map);

   SWAP(input_width, input_height);
   SWAP(output_width, output_height);

   in_dims[0] = input_width;
   in_dims[1] = input_height;
   in_dims[2] = operation->input_channels;

   out_dims[0] = output_width;
   out_dims[1] = output_height;
   out_dims[2] = operation->input_channels;

   unsigned pad_x = 0;
   unsigned pad_y = 0;
   unsigned split_dim = split_reshuffle(subgraph, operation, tp_core, tp_cores_used, in_dims, out_dims, &pad_x, &pad_y);

   map->in_image_x_size = in_dims[0];
   map->in_image_y_size = in_dims[1];
   map->in_image_z_size = in_dims[2];

   ML_DBG("map->in_image_z_size %d in_dims[2] %d split_dim %d\n", map->in_image_z_size, in_dims[2], split_dim);

   map->in_image_stride = operation->input_height;
   map->in_image_slice = input_width * input_height;

   map->in_window_x_start = 0x0 - pad_x;
   map->in_window_y_start = 0x0 - pad_y;

   unsigned out_loop_0_count = 0x2;
   map->in_window_x_end = out_dims[0] * out_loop_0_count - 1 - pad_x;
   map->in_window_y_end = out_dims[1] * 2 - 1 - pad_y;
   map->in_tile_x_size = out_dims[0] * out_loop_0_count;
   map->in_tile_x_inc = map->in_tile_x_size;
   map->in_tile_y_size = out_dims[1] * 2;
   map->in_tile_y_inc = out_dims[1] * 2;

   struct pipe_resource *input = etna_ml_get_resource(subgraph, operation->input_tensors[0]);
   unsigned offset = etna_ml_get_offset(subgraph, operation->input_tensors[0]);
   map->in_image_base_address = etna_bo_gpu_va(etna_buffer_resource(input)->bo) + offset;

   struct pipe_resource *output = etna_ml_get_resource(subgraph, operation->output_tensors[0]);
   offset = etna_ml_get_offset(subgraph, operation->output_tensors[0]);
   map->out_image_base_address = etna_bo_gpu_va(etna_buffer_resource(output)->bo) + offset;

   for (unsigned i = 0; i < tp_core; i++) {
      unsigned in_dims[3];
      unsigned out_dims[3];
      unsigned in_offset = 0;
      unsigned out_offset = 0;

      in_dims[0] = input_width;
      in_dims[1] = input_height;
      in_dims[2] = operation->input_channels;

      out_dims[0] = output_width;
      out_dims[1] = output_height;
      out_dims[2] = operation->input_channels;

      unsigned split_dim = split_reshuffle(subgraph, operation, i, tp_cores_used, in_dims, out_dims, NULL, NULL);

      switch(split_dim) {
         case 0:
            in_offset = in_dims[0];
            out_offset = out_dims[0];
            break;
         case 1:
            in_offset = map->in_image_stride * in_dims[1];
            out_offset = output_height * out_dims[1];
            break;
         case 2:
            in_offset = map->in_image_slice * in_dims[2];
            out_offset = out_dims[2] * map->in_tile_x_size * map->in_tile_y_size;
            break;
         default:
            break;
      }

      map->in_image_base_address += in_offset;
      map->out_image_base_address += out_offset;
   }

   map->out_loop_1_reset = 0x1;
   map->out_loop_2_reset = 0x0;
   map->out_loop_3_reset = 0x1;
   map->out_loop_0_inc = output_width * output_height;
   map->out_loop_1_inc = 0x1;
   map->out_loop_0_count = out_loop_0_count;
   map->out_loop_1_count = out_dims[0];
   map->out_loop_2_count = out_loop_0_count;
   map->out_loop_3_count = out_dims[1];
   map->out_loop_2_inc = map->out_loop_0_inc * 2;
   map->out_loop_3_inc = output_width;
   map->out_loop_6_inc = map->out_loop_0_inc * 4;

   map->in_zp = operation->input_zero_point;
   map->out_zp = operation->input_zero_point;

   if (tp_cores_used > 1)
      map->no_flush = tp_core < tp_cores_used - 1;

   map->in_image_circular_buf_size = 0x0;
   map->in_image_circular_buf_end_address_plus_1 = 0xFFFFFFFF >> 6;
   map->out_image_circular_buf_size = 0x0;
   map->out_image_circular_buf_end_address_plus_1 = 0xFFFFFFFF >> 6;

   etna_bo_cpu_fini(bo);

   return bo;
}


static void
split_pad(struct etna_ml_subgraph *subgraph, const struct etna_operation *operation,
          unsigned tp_core, unsigned tp_cores_used, unsigned *in_dims, unsigned *out_dims)
{
   unsigned remaining_out_size, remaining_in_size;
   unsigned dim_to_split = 2;

   remaining_in_size = in_dims[dim_to_split];
   remaining_out_size = out_dims[dim_to_split];

   for (unsigned i = 0; i <= tp_core; i++) {
      unsigned in_size = DIV_ROUND_UP(remaining_in_size, (tp_cores_used - i));
      unsigned out_size = DIV_ROUND_UP(remaining_out_size, (tp_cores_used - i));

      if (i < tp_cores_used - 1) {
         in_dims[dim_to_split] = in_size;
         remaining_in_size -= in_dims[dim_to_split];
      } else
         in_dims[dim_to_split] = remaining_in_size;

      out_dims[dim_to_split] = out_size;

      remaining_out_size -= out_size;
   }
}

static struct etna_bo *
create_pad_config(struct etna_ml_subgraph *subgraph, const struct etna_operation *operation,
                  unsigned tp_core, unsigned tp_cores_used)
{
   struct pipe_context *pctx = subgraph->base.context;
   struct etna_bo *bo = etna_ml_create_bo(pctx, sizeof(struct etna_tp_params));
   unsigned input_width = operation->input_width;
   unsigned input_height = operation->input_height;
   unsigned input_channels = operation->input_channels;
   unsigned output_width = operation->output_width;
   unsigned output_height = operation->output_height;
   unsigned output_channels = operation->output_channels;
   unsigned pad_before_x = operation->pad_before_x;
   unsigned pad_after_x = operation->pad_after_x;
   unsigned pad_before_y = operation->pad_before_y;
   unsigned pad_after_y = operation->pad_after_y;
   unsigned pad_before_z = operation->pad_before_z;
   unsigned pad_after_z = operation->pad_after_z;
   unsigned in_dims[3];
   unsigned out_dims[3];
   struct etna_ml_tensor *input = etna_ml_get_tensor(subgraph, operation->input_tensors[0]);

   if (input->exp_layout == ETNA_ML_LAYOUT_NHWC &&
       input->act_layout == ETNA_ML_LAYOUT_NCHW) {
      SWAP(input_width, input_height);
      SWAP(output_width, output_height);
      SWAP(pad_before_x, pad_before_y);
      SWAP(pad_after_x, pad_after_y);
   } else {
      SWAP(input_width, input_channels);
      SWAP(output_width, output_channels);
      SWAP(pad_before_x, pad_before_z);
      SWAP(pad_after_x, pad_after_z);
   }

   etna_bo_cpu_prep(bo, DRM_ETNA_PREP_WRITE);

   struct etna_tp_params *map = etna_bo_map(bo);

   set_default_tp_config(map);

   in_dims[0] = input_width;
   in_dims[1] = input_height;
   in_dims[2] = input_channels;

   out_dims[0] = output_width;
   out_dims[1] = output_height;
   out_dims[2] = output_channels;

   split_pad(subgraph, operation, tp_core, tp_cores_used, in_dims, out_dims);

   map->in_image_x_size = in_dims[0];
   map->in_image_y_size = in_dims[1];
   map->in_image_z_size = in_dims[2];

   map->in_image_stride = input_width;
   map->in_image_slice = input_width * input_height;

   map->in_window_x_start = 0x0 - pad_before_x;
   map->in_window_y_start = 0x0 - pad_before_y;
   map->in_window_x_end = input_width - 1 + pad_after_x;
   map->in_window_y_end = input_height - 1 + pad_after_y;

   if (tp_cores_used > 1) {
      if (pad_before_z)
         map->in_window_z_start_overfetch = tp_core == 0;

      if (pad_after_z)
         map->in_window_z_end_overfetch = tp_core == tp_cores_used - 1;
   }

   map->in_tile_x_size = out_dims[0];
   map->in_tile_x_inc = out_dims[0];
   map->in_tile_y_size = out_dims[1];
   map->in_tile_y_inc = out_dims[1];

   unsigned offset = etna_ml_get_offset(subgraph, operation->input_tensors[0]);
   map->in_image_base_address = etna_bo_gpu_va(etna_buffer_resource(input->resource)->bo) + offset;

   struct pipe_resource *output = etna_ml_get_resource(subgraph, operation->output_tensors[0]);
   offset = etna_ml_get_offset(subgraph, operation->output_tensors[0]);
   map->out_image_base_address = etna_bo_gpu_va(etna_buffer_resource(output)->bo) + offset;

   for (unsigned i = 0; i < tp_core; i++) {
      unsigned in_dims[3];
      unsigned out_dims[3];
      unsigned in_offset = 0;
      unsigned out_offset = 0;

      in_dims[0] = input_width;
      in_dims[1] = input_height;
      in_dims[2] = input_channels;

      out_dims[0] = output_width;
      out_dims[1] = output_height;
      out_dims[2] = output_channels;

      split_pad(subgraph, operation, i, tp_cores_used, in_dims, out_dims);

      in_offset = map->in_image_slice * in_dims[2];

      out_offset = map->in_tile_x_size * map->in_tile_y_size;

      if (i == 0)
         out_offset *= in_dims[2] + pad_before_z;
      else
         out_offset *= in_dims[2];

      map->in_image_base_address += in_offset;
      map->out_image_base_address += out_offset;
   }

   map->out_loop_1_reset = 0x0;
   map->out_loop_2_reset = 0x0;
   map->out_loop_3_reset = 0x0;
   map->out_loop_0_inc = 0x0;
   map->out_loop_1_inc = 0x1;
   map->out_loop_0_count = 0x1;
   map->out_loop_1_count = out_dims[0];
   map->out_loop_2_count = out_dims[1];
   map->out_loop_3_count = 0x1;
   map->out_loop_2_inc = out_dims[0];
   map->out_loop_3_inc = 0x0;
   map->out_loop_6_inc = out_dims[0] * out_dims[1];

   map->in_zp = operation->input_zero_point;
   map->out_zp = operation->output_zero_point;

   if (tp_cores_used > 1)
      map->no_flush = tp_core < tp_cores_used - 1;

   map->in_image_circular_buf_size = 0x0;
   map->in_image_circular_buf_end_address_plus_1 = 0xFFFFFFFF >> 6;
   map->out_image_circular_buf_size = 0x0;
   map->out_image_circular_buf_end_address_plus_1 = 0xFFFFFFFF >> 6;

   etna_bo_cpu_fini(bo);

   return bo;
}

static inline uint8_t
etna_tensor_zero_point(const struct pipe_tensor *tensor)
{
   if (tensor->is_signed) {
      assert(tensor->zero_point >= -128 && tensor->zero_point <= 127);
      return tensor->zero_point + 128;
   } else {
      assert(tensor->zero_point >= 0 && tensor->zero_point <= 255);
      return tensor->zero_point;
   }
}

static void
split_pwl_lut(struct etna_ml_subgraph *subgraph, const struct etna_operation *operation,
              unsigned tp_core, unsigned tp_cores_used, unsigned *in_dims, unsigned *out_dims)
{
   unsigned remaining_in_size;
   const unsigned dim_to_split = 2;

   remaining_in_size = in_dims[dim_to_split];

   for (unsigned i = 0; i <= tp_core; i++) {
      unsigned size = DIV_ROUND_UP(remaining_in_size, (tp_cores_used - i));

      if (i < tp_cores_used - 1) {
         in_dims[dim_to_split] = size;
         remaining_in_size -= in_dims[dim_to_split];
      } else
         in_dims[dim_to_split] = remaining_in_size;

      out_dims[dim_to_split] = size;
   }
}

static struct etna_bo *
create_pwl_lut_config(struct etna_ml_subgraph *subgraph, const struct etna_operation *operation,
                   unsigned tp_core, unsigned tp_cores_used, struct etna_bo *pwl_lut)
{
   struct pipe_context *pctx = subgraph->base.context;
   struct etna_bo *bo = etna_ml_create_bo(pctx, sizeof(struct etna_tp_params));
   unsigned input_width = operation->input_width;
   unsigned input_height = operation->input_height;
   unsigned input_channels = operation->input_channels;
   unsigned output_width = operation->output_width;
   unsigned output_height = operation->output_height;
   unsigned output_channels = operation->output_channels;
   unsigned in_dims[3];
   unsigned out_dims[3];

   SWAP(input_width, input_channels);
   SWAP(output_width, output_channels);

   etna_bo_cpu_prep(bo, DRM_ETNA_PREP_WRITE);

   struct etna_tp_params *map = etna_bo_map(bo);

   set_default_tp_config(map);

   in_dims[0] = input_width;
   in_dims[1] = input_height;
   in_dims[2] = input_channels;

   out_dims[0] = output_width;
   out_dims[1] = output_height;
   out_dims[2] = output_channels;

   split_pwl_lut(subgraph, operation, tp_core, tp_cores_used, in_dims, out_dims);

   map->in_image_x_size = in_dims[0];
   map->in_image_y_size = in_dims[1];
   map->in_image_z_size = in_dims[2];

   map->in_image_stride = input_width;
   map->in_image_slice = input_width * input_height;

   map->in_window_x_start = 0x0;
   map->in_window_y_start = 0x0;

   map->in_window_x_end = in_dims[0] - 1;
   map->in_window_y_end = in_dims[1] - 1;

   map->in_tile_x_size = 64;
   map->in_tile_x_inc = 64;
   map->in_tile_y_size = 16;
   map->in_tile_y_inc = 16;

   map->alu_pwl_enable = 0x1;
   map->alu_load_pwl_lut = 0x1;
   map->alu_load_pwl_lut_global_mem = 0x1;
   map->alu_load_pwl_lut_address = etna_bo_gpu_va(pwl_lut);
   map->alu_pwl_sign_support = 0x1;

   struct pipe_resource *input = etna_ml_get_resource(subgraph, operation->input_tensors[0]);
   unsigned offset = etna_ml_get_offset(subgraph, operation->input_tensors[0]);
   map->in_image_base_address = etna_bo_gpu_va(etna_buffer_resource(input)->bo) + offset;

   struct pipe_resource *output = etna_ml_get_resource(subgraph, operation->output_tensors[0]);
   offset = etna_ml_get_offset(subgraph, operation->output_tensors[0]);
   map->out_image_base_address = etna_bo_gpu_va(etna_buffer_resource(output)->bo) + offset;

   for (unsigned i = 0; i < tp_core; i++) {
      unsigned in_dims[3];
      unsigned out_dims[3];
      unsigned in_offset = 0;
      unsigned out_offset = 0;

      in_dims[0] = input_width;
      in_dims[1] = input_height;
      in_dims[2] = input_channels;

      out_dims[0] = output_width;
      out_dims[1] = output_height;
      out_dims[2] = output_channels;

      split_pwl_lut(subgraph, operation, i, tp_cores_used, in_dims, out_dims);

      in_offset = map->in_image_slice * in_dims[2];
      out_offset = out_dims[0] * out_dims[1] * out_dims[2];

      map->in_image_base_address += in_offset;
      map->out_image_base_address += out_offset;
   }

   map->out_loop_1_reset = 0x1;
   map->out_loop_2_reset = 0x1;
   map->out_loop_3_reset = 0x0;

   map->out_loop_0_count = 0x1;
   map->out_loop_1_count = 0x0;
   map->out_loop_2_count = 0x0;
   map->out_loop_3_count = DIV_ROUND_UP(in_dims[0], 64);
   map->out_loop_4_count = DIV_ROUND_UP(in_dims[1], 16);

   map->out_loop_0_inc = 0x0;
   map->out_loop_1_inc = 0x1;
   map->out_loop_2_inc = out_dims[0];
   map->out_loop_3_inc = 64;
   map->out_loop_4_inc = out_dims[0] * 16;
   map->out_loop_6_inc = out_dims[0] * out_dims[1];

   map->in_zp = operation->input_zero_point;
   map->out_zp = operation->output_zero_point;

   uint32_t scale;

   if (operation->tp_type == ETNA_ML_TP_LOGISTIC)
      scale = fui(1.0 / operation->output_scale);
   else
      scale = fui(operation->input_scale / operation->output_scale);

   /* This should compensate for some loss of precision */
   if ((scale >> 7 & 1) != 0 && (scale & 0x17f) != 0) {
      scale += 0x100;
   }

   map->alu_output_post_multiplier = (scale >> 8) & 0x7fff;

   uint32_t remainder = 0x7f - (scale >> 0x17);
   map->alu_output_postshift = remainder & 0x1f;
   remainder >>= 5;
   map->alu_output_postshift_5_6 = remainder & 3;

   if (tp_cores_used > 1)
      map->no_flush = tp_core < tp_cores_used - 1;

   map->in_image_circular_buf_size = 0x0;
   map->in_image_circular_buf_end_address_plus_1 = 0xFFFFFFFF >> 6;
   map->out_image_circular_buf_size = 0x0;
   map->out_image_circular_buf_end_address_plus_1 = 0xFFFFFFFF >> 6;

   etna_bo_cpu_fini(bo);

   return bo;
}

void
etna_ml_lower_transpose(struct etna_ml_subgraph *subgraph,
                        const struct pipe_tensor *input_tensor,
                        struct etna_operation *operation)
{
   operation->type = ETNA_JOB_TYPE_TP;
   operation->tp_type = ETNA_ML_TP_TRANSPOSE;

   operation->input_tensors[0] = input_tensor->index;
   operation->input_count = 1;
   operation->input_width = input_tensor->dims[1];
   operation->input_height = input_tensor->dims[2];
   operation->input_channels = input_tensor->dims[3];
   operation->input_zero_point = etna_tensor_zero_point(input_tensor);
   operation->input_scale = input_tensor->scale;
   operation->input_tensor_sizes[0] = operation->input_width *
                                      operation->input_height *
                                      operation->input_channels;

   operation->output_width = operation->input_width;
   operation->output_height = operation->input_height;
   operation->output_channels = operation->input_channels;
   operation->output_zero_point = operation->input_zero_point;
   operation->output_scale = operation->input_scale;
   operation->output_tensor_sizes[0] = operation->output_width *
                                       operation->output_height *
                                       operation->output_channels;
}

void
etna_ml_lower_detranspose(struct etna_ml_subgraph *subgraph,
                          const struct pipe_tensor *output_tensor,
                          struct etna_operation *operation)
{
   operation->type = ETNA_JOB_TYPE_TP;
   operation->tp_type = ETNA_ML_TP_DETRANSPOSE;

   operation->input_tensors[0] = output_tensor->index;
   operation->input_count = 1;
   operation->input_width = output_tensor->dims[1];
   operation->input_height = output_tensor->dims[2];
   operation->input_channels = output_tensor->dims[3];
   operation->input_zero_point = etna_tensor_zero_point(output_tensor);
   operation->input_scale = output_tensor->scale;
   operation->input_tensor_sizes[0] = operation->input_width *
                                      operation->input_height *
                                      operation->input_channels;

   operation->output_width = operation->input_width;
   operation->output_height = operation->input_height;
   operation->output_channels = operation->input_channels;
   operation->output_zero_point = operation->input_zero_point;
   operation->output_scale = operation->input_scale;
   operation->output_tensor_sizes[0] = operation->output_width *
                                       operation->output_height *
                                       operation->output_channels;
}

void
etna_ml_lower_reshuffle(struct etna_ml_subgraph *subgraph,
                        const struct pipe_ml_operation *convolution,
                        struct etna_operation *operation)
{
   operation->type = ETNA_JOB_TYPE_TP;
   operation->tp_type = ETNA_ML_TP_RESHUFFLE;
   operation->stride = convolution->conv.stride_x;
   operation->padding_same = convolution->conv.padding_same;

   operation->input_count = 1;
   operation->input_width = convolution->input_tensors[0]->dims[1];
   operation->input_height = convolution->input_tensors[0]->dims[2];
   operation->input_channels = convolution->input_tensors[0]->dims[3];
   operation->input_zero_point = etna_tensor_zero_point(convolution->input_tensors[0]);
   operation->input_scale = convolution->input_tensors[0]->scale;
   operation->input_tensor_sizes[0] = operation->input_width *
                                      operation->input_height *
                                      operation->input_channels;

   operation->output_count = 1;
   operation->output_width = DIV_ROUND_UP(operation->input_width, operation->stride);
   operation->output_height = DIV_ROUND_UP(operation->input_height, operation->stride);
   operation->output_channels = operation->input_channels * operation->stride * operation->stride;
   operation->output_zero_point = etna_tensor_zero_point(convolution->input_tensors[0]);
   operation->output_scale = convolution->input_tensors[0]->scale;
   operation->output_tensor_sizes[0] = operation->output_width *
                                       operation->output_height *
                                       operation->output_channels;

   /* When destriding a convolution, the transformation to be made to the input
    * tensor will depend on the size of the weight tensor.
    */
   operation->weight_width = convolution->conv.weight_tensor->dims[1];
   operation->weight_height = convolution->conv.weight_tensor->dims[2];

   if (operation->padding_same) {
      if (operation->weight_width == 5) {
         operation->output_width += 2;
         operation->output_height += 2;
      } else {
         operation->output_width += 1;
         operation->output_height += 1;
      }
   }
}

void
etna_ml_lower_pad(struct etna_ml_subgraph *subgraph,
                  const struct pipe_ml_operation *pad,
                  struct etna_operation *operation)
{
   operation->type = ETNA_JOB_TYPE_TP;
   operation->tp_type = ETNA_ML_TP_PAD;
   operation->stride = 1;

   operation->input_count = 1;
   operation->input_width = pad->input_tensors[0]->dims[1];
   operation->input_height = pad->input_tensors[0]->dims[2];
   operation->input_channels = pad->input_tensors[0]->dims[3];
   operation->input_tensor_sizes[0] = operation->input_width *
                                      operation->input_height *
                                      operation->input_channels;
   operation->input_zero_point = etna_tensor_zero_point(pad->input_tensors[0]);
   operation->input_scale = pad->input_tensors[0]->scale;

   operation->output_count = 1;
   operation->output_width = pad->output_tensors[0]->dims[1];
   operation->output_height = pad->output_tensors[0]->dims[2];
   operation->output_channels = pad->output_tensors[0]->dims[3];
   operation->output_zero_point = etna_tensor_zero_point(pad->output_tensors[0]);
   operation->output_scale = pad->output_tensors[0]->scale;
   operation->output_tensor_sizes[0] = operation->output_width *
                                       operation->output_height *
                                       operation->output_channels;

   operation->pad_before_x = pad->pad.before_x;
   operation->pad_after_x = pad->pad.after_x;
   operation->pad_before_y = pad->pad.before_y;
   operation->pad_after_y = pad->pad.after_y;
   operation->pad_before_z = pad->pad.before_z;
   operation->pad_after_z = pad->pad.after_z;
}

void
etna_ml_lower_relu(struct etna_ml_subgraph *subgraph,
                   const struct pipe_ml_operation *operation,
                   const struct pipe_tensor *input_tensor,
                   struct etna_operation *relu)
{
   relu->type = ETNA_JOB_TYPE_TP;
   relu->tp_type = ETNA_ML_TP_RELU;

   relu->input_tensors[0] = input_tensor->index;
   relu->input_count = 1;
   relu->input_width = input_tensor->dims[1];
   relu->input_height = input_tensor->dims[2];
   relu->input_channels = input_tensor->dims[3];
   relu->input_tensor_sizes[0] = relu->input_width *
                                 relu->input_height *
                                 relu->input_channels;
   relu->input_zero_point = etna_tensor_zero_point(input_tensor);
   relu->input_scale = input_tensor->scale;

   relu->output_count = 1;
   relu->output_width = operation->output_tensors[0]->dims[1];
   relu->output_height = operation->output_tensors[0]->dims[2];
   relu->output_channels = operation->output_tensors[0]->dims[3];
   relu->output_zero_point = etna_tensor_zero_point(operation->output_tensors[0]);
   relu->output_scale = operation->output_tensors[0]->scale;
   relu->output_tensor_sizes[0] = relu->output_width *
                                  relu->output_height *
                                  relu->output_channels;
}

void
etna_ml_lower_absolute(struct etna_ml_subgraph *subgraph,
                       const struct pipe_ml_operation *abs,
                       struct etna_operation *operation)
{
   operation->type = ETNA_JOB_TYPE_TP;
   operation->tp_type = ETNA_ML_TP_ABSOLUTE;
   operation->stride = 1;

   operation->input_count = 1;
   operation->input_width = abs->input_tensors[0]->dims[1];
   operation->input_height = abs->input_tensors[0]->dims[2];
   operation->input_channels = abs->input_tensors[0]->dims[3];
   operation->input_tensor_sizes[0] = operation->input_width *
                                      operation->input_height *
                                      operation->input_channels;
   operation->input_zero_point = etna_tensor_zero_point(abs->input_tensors[0]);
   operation->input_scale = abs->input_tensors[0]->scale;

   operation->output_count = 1;
   operation->output_width = abs->output_tensors[0]->dims[1];
   operation->output_height = abs->output_tensors[0]->dims[2];
   operation->output_channels = abs->output_tensors[0]->dims[3];
   operation->output_zero_point = etna_tensor_zero_point(abs->output_tensors[0]);
   operation->output_scale = abs->output_tensors[0]->scale;
   operation->output_tensor_sizes[0] = operation->output_width *
                                       operation->output_height *
                                       operation->output_channels;
}

void
etna_ml_lower_logistic(struct etna_ml_subgraph *subgraph,
                       const struct pipe_ml_operation *abs,
                       struct etna_operation *operation)
{
   operation->type = ETNA_JOB_TYPE_TP;
   operation->tp_type = ETNA_ML_TP_LOGISTIC;
   operation->stride = 1;

   operation->input_count = 1;
   operation->input_width = abs->input_tensors[0]->dims[1];
   operation->input_height = abs->input_tensors[0]->dims[2];
   operation->input_channels = abs->input_tensors[0]->dims[3];
   operation->input_tensor_sizes[0] = operation->input_width *
                                      operation->input_height *
                                      operation->input_channels;
   operation->input_zero_point = etna_tensor_zero_point(abs->input_tensors[0]);
   operation->input_scale = abs->input_tensors[0]->scale;

   operation->output_count = 1;
   operation->output_width = abs->output_tensors[0]->dims[1];
   operation->output_height = abs->output_tensors[0]->dims[2];
   operation->output_channels = abs->output_tensors[0]->dims[3];
   operation->output_zero_point = etna_tensor_zero_point(abs->output_tensors[0]);
   operation->output_scale = abs->output_tensors[0]->scale;
   operation->output_tensor_sizes[0] = operation->output_width *
                                       operation->output_height *
                                       operation->output_channels;
}

static struct etna_bo *
create_relu_lut_bo(struct etna_ml_subgraph *subgraph,
                   const struct etna_operation *operation)
{
   struct pipe_context *context = subgraph->base.context;
   struct etna_context *ctx = etna_context(context);
   const unsigned lut_length = 1024;
   struct etna_bo *pwl_lut = etna_bo_new(ctx->screen->dev,
                                         lut_length * sizeof(uint32_t),
                                         DRM_ETNA_GEM_CACHE_WC);

   etna_bo_cpu_prep(pwl_lut, DRM_ETNA_PREP_WRITE);

   uint32_t *map = etna_bo_map(pwl_lut);

   for (int i = 0; i < lut_length; i++) {
      if (i < 16)
         map[i] = 0x0;
      else if (i < 496)
         map[i] = 0x8000 + (i - 16) * 0x800;
      else if (i < 512)
         map[i] = 0xf7fff;
      else
         map[i] = 0x100000;
   }

   etna_bo_cpu_fini(pwl_lut);

   return pwl_lut;
}

static struct etna_bo *
create_abs_lut_bo(struct etna_ml_subgraph *subgraph,
                  const struct etna_operation *operation)
{
   struct pipe_context *context = subgraph->base.context;
   struct etna_context *ctx = etna_context(context);
   unsigned lut_length = 1024;
   struct etna_bo *pwl_lut = etna_bo_new(ctx->screen->dev,
                                         lut_length * sizeof(uint32_t),
                                         DRM_ETNA_GEM_CACHE_WC);

   etna_bo_cpu_prep(pwl_lut, DRM_ETNA_PREP_WRITE);

   uint32_t *map = etna_bo_map(pwl_lut);

   for (int i = 0; i < lut_length; i++) {
      if (i < 16)
         map[i] = 0x0;
      else if (i < 496)
         map[i] = 0x8000 + (i - 16) * 0x800;
      else if (i < 512)
         map[i] = 0xf7fff;
      else if (i < 528)
         map[i] = 0x100000;
      else if (i < 1008)
         map[i] = 0x8000 + (i - 528) * 0x800;
      else if (i < 1024)
         map[i] = 0xf7fff;
   }

   etna_bo_cpu_fini(pwl_lut);

   return pwl_lut;
}

/* Based on code from src/util/half_float.c in Mesa itself. */
static uint32_t
fp21(float val)
{
   const fi_type fi = {val};
   const int flt_m = fi.i & 0x7fffff;
   const int flt_e = (fi.i >> 23) & 0xff;
   const int flt_s = (fi.i >> 31) & 0x1;
   int s, e, m = 0;

   /* sign bit */
   s = flt_s;

   /* handle special cases */
   if ((flt_e == 0) && (flt_m == 0)) {
      /* zero */
      /* m = 0; - already set */
      e = 0;
   }
   else if ((flt_e == 0) && (flt_m != 0)) {
      /* denorm -- denorm float maps to 0 fp21 */
      /* m = 0; - already set */
      e = 0;
   }
   else if ((flt_e == 0xff) && (flt_m == 0)) {
      /* infinity */
      /* m = 0; - already set */
      e = 15;
   }
   else if ((flt_e == 0xff) && (flt_m != 0)) {
      /* Retain the top bits of a NaN to make sure that the quiet/signaling
      * status stays the same.
      */
      m = flt_m >> 13;
      if (!m)
         m = 1;
      e = 15;
   }
   else {
      /* regular number */
      const int new_exp = flt_e - 127;
      if (new_exp < -14) {
         /* The float32 lies in the range (0.0, min_normal16) and is rounded
          * to a nearby float16 value. The result will be either zero, subnormal,
          * or normal.
          */
         e = 0;
         m = _mesa_lroundevenf((1 << 24) * fabsf(fi.f));
      }
      else if (new_exp > 15) {
         /* map this value to infinity */
         e = 0x1e;
         m = 0x7fff;
      }
      else {
         /* The float32 lies in the range
          *   [min_normal16, max_normal16 + max_step16)
          * and is rounded to a nearby float16 value. The result will be
          * either normal or infinite.
          */
         e = new_exp + 15;
         m = _mesa_lroundevenf(flt_m / (float) (1 << 8));
      }
   }

  return (s << 20) | (e << 15) | m;
}

/* Based on code from src/util/half_float.c in Mesa itself. */
static float
fp32(uint32_t val)
{
   union fi f32;

   /* Exponent / Mantissa */
   f32.ui = (val & 0xfffff) << 8;

   /* Sign */
   f32.ui |= (uint32_t)(val & 0x100000) << 11;
   f32.ui += 0x38000000;

   return f32.f;
}

static struct etna_bo *
create_log_lut_bo(struct etna_ml_subgraph *subgraph,
                  const struct etna_operation *operation)
{
   struct pipe_context *context = subgraph->base.context;
   struct etna_context *ctx = etna_context(context);
   unsigned lut_table_len = 1024;

   struct etna_bo *pwl_lut = etna_bo_new(ctx->screen->dev,
                                         lut_table_len * sizeof(uint32_t),
                                         DRM_ETNA_GEM_CACHE_WC);

   etna_bo_cpu_prep(pwl_lut, DRM_ETNA_PREP_WRITE);

   uint32_t *map = etna_bo_map(pwl_lut);
   unsigned pos = 0;

   uint32_t initial_val = fp21(0.5);
   for (int i = 0; i < 16; i++)
         map[pos++] = initial_val;

   for (int cur_val = 0x8000; cur_val < 0xf8000; cur_val += 0x800) {
      float val = fp32(cur_val);
      val *= operation->input_scale;
      val *= -1.0;
      val = expf(val);
      val = 1.0f / (val + 1.0f);
      map[pos++] = fp21(val);
   }

   uint32_t middle_val = fp21(1.0);
   for (int i = 0; i < 16; i++)
         map[pos++] = middle_val;

   uint32_t middle_2_val = fp21(0.5);
   for (int i = 0; i < 17; i++)
         map[pos++] = middle_2_val;

   for (int cur_val = 0x108800; cur_val < 0x1f8000; cur_val += 0x800) {
      float val = fp32(cur_val);
      val *= operation->input_scale;
      val *= -1.0;
      val = expf(val);
      val = 1.0f / (val + 1.0f);
      map[pos++] = fp21(val);
   }

   uint32_t final_val = fp21(0.0);
   for (int i = 0; i < 16; i++)
         map[pos++] = final_val;

   etna_bo_cpu_fini(pwl_lut);

   return pwl_lut;
}

void
etna_ml_compile_operation_tp(struct etna_ml_subgraph *subgraph,
                             const struct etna_operation *operation,
                             struct etna_vip_instruction *instruction)
{
   struct etna_context *ctx = etna_context(subgraph->base.context);
   struct pipe_resource *input = etna_ml_get_resource(subgraph, operation->input_tensors[0]);
   assert(input);
   pipe_resource_reference(&instruction->input, input);

   struct pipe_resource *output = etna_ml_get_resource(subgraph, operation->output_tensors[0]);
   assert(output);
   pipe_resource_reference(&instruction->output, output);

   instruction->input_offset = etna_ml_get_offset(subgraph, operation->input_tensors[0]);
   instruction->output_offset = etna_ml_get_offset(subgraph, operation->output_tensors[0]);

   switch (operation->tp_type) {
   case ETNA_ML_TP_TRANSPOSE:
      instruction->configs[0] = create_transpose_config(subgraph, operation);
      break;
   case ETNA_ML_TP_DETRANSPOSE:
      instruction->configs[0] = create_detranspose_config(subgraph, operation);
      break;
   case ETNA_ML_TP_RESHUFFLE: {
      unsigned tp_core_count = etna_ml_get_core_info(ctx)->tp_core_count;
      unsigned tp_cores_used;

      tp_cores_used = (operation->input_width > 8 || operation->input_channels > 1) ? tp_core_count : 1;

      /* TODO: Run among the 4 cores for faster performance */
      if ((operation->input_width == 320 || operation->input_width == 224) &&
          operation->input_channels == 3)
         tp_cores_used = 1;

      ML_DBG("reshuffle: input_width %d tp_cores_used %d\n", operation->input_width, tp_cores_used);
      for (unsigned i = 0; i < tp_cores_used; i++) {
         instruction->configs[i] = create_reshuffle_config(subgraph, operation, i, tp_cores_used);
      }
      break;
   }
   case ETNA_ML_TP_PAD: {
      unsigned tp_cores_used = etna_ml_get_core_info(ctx)->tp_core_count;

      if (operation->input_width == 1)
         tp_cores_used = 1;

      ML_DBG("pad: input_width %d tp_cores_used %d\n", operation->input_width, tp_cores_used);
      for (unsigned i = 0; i < tp_cores_used; i++) {
         instruction->configs[i] = create_pad_config(subgraph, operation, i, tp_cores_used);
      }
      break;
   }
   case ETNA_ML_TP_RELU: {
      unsigned tp_cores_used = etna_ml_get_core_info(ctx)->tp_core_count;

      if (operation->input_width < 6)
         tp_cores_used = 1;

      ML_DBG("relu: input_width %d tp_cores_used %d\n", operation->input_width, tp_cores_used);
      instruction->pwl_lut = create_relu_lut_bo(subgraph, operation);
      for (unsigned i = 0; i < tp_cores_used; i++) {
         instruction->configs[i] = create_pwl_lut_config(subgraph, operation, i, tp_cores_used, instruction->pwl_lut);
      }
      break;
   }
   case ETNA_ML_TP_ABSOLUTE: {
      unsigned tp_cores_used = etna_ml_get_core_info(ctx)->tp_core_count;

      ML_DBG("absolute: input_width %d tp_cores_used %d\n", operation->input_width, tp_cores_used);
      instruction->pwl_lut = create_abs_lut_bo(subgraph, operation);
      for (unsigned i = 0; i < tp_cores_used; i++) {
         instruction->configs[i] = create_pwl_lut_config(subgraph, operation, i, tp_cores_used, instruction->pwl_lut);
      }
      break;
   }
   case ETNA_ML_TP_LOGISTIC: {
      unsigned tp_cores_used = etna_ml_get_core_info(ctx)->tp_core_count;

      if (operation->input_width < 6)
         tp_cores_used = 1;

      ML_DBG("logistic: input_width %d tp_cores_used %d\n", operation->input_width, tp_cores_used);
      instruction->pwl_lut = create_log_lut_bo(subgraph, operation);
      for (unsigned i = 0; i < tp_cores_used; i++) {
         instruction->configs[i] = create_pwl_lut_config(subgraph, operation, i, tp_cores_used, instruction->pwl_lut);
      }
      break;
   }
   }
   instruction->type = ETNA_JOB_TYPE_TP;
   instruction->tp_type = operation->tp_type;
}

void
etna_ml_emit_operation_tp(struct etna_ml_subgraph *subgraph,
                          struct etna_vip_instruction *operation,
                          unsigned idx)
{
   struct etna_context *ctx = etna_context(subgraph->base.context);
   unsigned tp_core_count = etna_ml_get_core_info(ctx)->tp_core_count;
   struct etna_cmd_stream *stream = ctx->stream;
   bool more_than_one_tp_job = operation->configs[1] != NULL;
   bool parallel = DBG_ENABLED(ETNA_DBG_NPU_PARALLEL);

   for (unsigned j = 0; j < tp_core_count && operation->configs[j]; j++) {
      unsigned offset = parallel ? idx + 1 : 0;

      if (more_than_one_tp_job && (j < tp_core_count - 1))
            offset = parallel ? 0x1f : 0x1;

      etna_set_state(stream, VIVS_GL_OCB_REMAP_START, 0x0);
      etna_set_state(stream, VIVS_GL_OCB_REMAP_END, 0x0);
      etna_set_state(stream, VIVS_GL_TP_CONFIG, 0x0);

      if (more_than_one_tp_job &&
          (operation->tp_type == ETNA_ML_TP_PAD ||
           operation->tp_type == ETNA_ML_TP_RELU ||
           operation->tp_type == ETNA_ML_TP_ABSOLUTE)) {
         etna_set_state(stream, VIVS_GL_UNK03950, j < tp_core_count - 1 ? 0x8 : 0x0);
      } else {
         etna_set_state(stream, VIVS_GL_UNK03950, 0x0);
      }

      etna_set_state_reloc(stream, VIVS_PS_TP_INST_ADDR, &(struct etna_reloc) {
         .bo = operation->configs[j],
         .flags = ETNA_RELOC_READ,
         .offset = offset,
      });
   }
   etna_set_state(stream, VIVS_PS_UNK10A4, parallel ? idx + 1 : 0x0);
}
