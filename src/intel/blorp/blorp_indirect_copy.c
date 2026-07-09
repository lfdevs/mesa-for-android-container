/* Copyright © 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "blorp_nir_builder.h"

#include "blorp_priv.h"
#include "blorp_shaders.h"

#pragma pack(push, 1)
struct blorp_indirect_copy_mem2img_key {
   struct blorp_base_key base;

   /* How many dimensions does our image have? 1, 2 or 3. */
   uint32_t dimensions;

   /* When compressed formats are used, we pretend they are a non-compressed
    * format, of the same bpb. Since we can't maintain the exact same layout
    * of mipmap and layer offsets, we're forced to make adjustments to where X
    * and Y actually start, and are also forced to copy only one layer (or Z
    * axis position) per shader invocation.
    */
   int forced_layer_or_z;

   /* Info taken from isl_format_layout. */
   uint16_t format_Bpb;
   uint16_t format_bw;
   uint16_t format_bh;
   uint16_t format_bd;
};
#pragma pack(pop)

/* Refer to struct blorp_wm_inputs_indirect. */
struct blorp_indirect_vars {
   nir_variable *indirect_buf_addr;
   nir_variable *stride;
   nir_variable *copy_count;
   nir_variable *copy_idx;
   nir_variable *max_layer;
   nir_variable *x_offset;
   nir_variable *y_offset;
};

static enum isl_format
get_format_for_copy(int format_bpb)
{
   switch (format_bpb) {
   case 8:   return ISL_FORMAT_R8_UINT;
   case 16:  return ISL_FORMAT_R16_UINT;
   case 24:  return ISL_FORMAT_R8G8B8_UINT;
   case 32:  return ISL_FORMAT_R32_UINT;
   case 48:  return ISL_FORMAT_R16G16B16_UINT;
   case 64:  return ISL_FORMAT_R32G32_UINT;
   case 96:  return ISL_FORMAT_R32G32B32_UINT;
   case 128: return ISL_FORMAT_R32G32B32A32_UINT;
   default:
      mesa_loge("unexpected format bpb: %d", format_bpb);
      assert(false);
      return ISL_FORMAT_UNSUPPORTED;
   }
}

static void
blorp_indirect_vars_init(nir_builder *b, struct blorp_indirect_vars *v)
{
   v->copy_count =
      BLORP_CREATE_NIR_INPUT(b->shader, indirect.copy_count,
                             glsl_uint_type());
   v->indirect_buf_addr =
      BLORP_CREATE_NIR_INPUT(b->shader, indirect.indirect_buf_addr,
                             glsl_uint64_t_type());
   v->stride =
      BLORP_CREATE_NIR_INPUT(b->shader, indirect.indirect_buf_stride,
                             glsl_uint64_t_type());
   v->copy_idx =
      BLORP_CREATE_NIR_INPUT(b->shader, indirect.copy_idx,
                             glsl_uint_type());
   v->max_layer =
      BLORP_CREATE_NIR_INPUT(b->shader, indirect.max_layer,
                             glsl_uint_type());
   v->x_offset =
      BLORP_CREATE_NIR_INPUT(b->shader, indirect.x_offset,
                             glsl_uint_type());
   v->y_offset =
      BLORP_CREATE_NIR_INPUT(b->shader, indirect.y_offset,
                             glsl_uint_type());
}

static nir_shader *
blorp_build_copy_mem_indirect_shader(struct blorp_batch *batch,
                                     void *mem_ctx)
{

   struct blorp_context *blorp = batch->blorp;

   mesa_shader_stage stage = MESA_SHADER_COMPUTE;
   const nir_shader_compiler_options *nir_options =
      blorp->compiler->nir_options(blorp, stage);
   nir_builder b = nir_builder_init_simple_shader(stage, nir_options,
                                                  "copy mem indirect");
   ralloc_steal(mem_ctx, b.shader);

   b.shader->info.workgroup_size[0] = 32;
   b.shader->info.workgroup_size[1] = 1;
   b.shader->info.workgroup_size[2] = 1;

   struct blorp_indirect_vars v;
   blorp_indirect_vars_init(&b, &v);

   /* The indirect buffer is an array containing 'copy_count'
    * VkCopyMemoryIndirectCommandKHR structures, separated by 'stride' bytes.
    */
   nir_def *copy_count = nir_load_var(&b, v.copy_count);
   nir_def *indirect_buf_addr = nir_load_var(&b, v.indirect_buf_addr);
   nir_def *stride = nir_load_var(&b, v.stride);
   nir_def *global_id =
      nir_channel(&b, nir_load_global_invocation_id(&b, 32), 0);

   blorp_copy_memory_indirect_shader(&b, indirect_buf_addr, copy_count,
                                     stride, global_id);

   return b.shader;
}

static nir_shader *
blorp_build_copy_mem2img_indirect_shader(struct blorp_batch *batch,
                                         void *mem_ctx,
                                         struct blorp_indirect_copy_mem2img_key *key)
{
   struct blorp_context *blorp = batch->blorp;

   mesa_shader_stage stage = MESA_SHADER_COMPUTE;
   const nir_shader_compiler_options *nir_options =
      blorp->compiler->nir_options(blorp, stage);
   nir_builder b = nir_builder_init_simple_shader(stage, nir_options,
                                                  "copy mem2img indirect");
   ralloc_steal(mem_ctx, b.shader);

   b.shader->info.workgroup_size[0] = 4;
   b.shader->info.workgroup_size[1] = 8;
   b.shader->info.workgroup_size[2] = 1;

   struct blorp_indirect_vars v;
   blorp_indirect_vars_init(&b, &v);

   /* The indirect buffer is an array containing 'copy_count'
    * VkCopyMemoryToImageIndirectCommandKHR structures, separated by 'stride'
    * bytes.
    */
   nir_def *indirect_buf_addr = nir_load_var(&b, v.indirect_buf_addr);
   nir_def *stride = nir_load_var(&b, v.stride);
   nir_def *copy_idx = nir_load_var(&b, v.copy_idx);
   nir_def *max_layer = nir_load_var(&b, v.max_layer);
   nir_def *dest_coord_offsets_arr[2] = {
      nir_load_var(&b, v.x_offset),
      nir_load_var(&b, v.y_offset),
   };
   nir_def *dest_coord_offsets = nir_vec(&b, dest_coord_offsets_arr, 2);

   nir_def *global_id = nir_load_global_invocation_id(&b, 32);

   /* Shader keys (constants). */
   nir_def *dimensions = nir_imm_int(&b, key->dimensions);
   nir_def *forced_layer_or_z = nir_imm_int(&b, key->forced_layer_or_z);
   nir_def *format_Bpb = nir_imm_intN_t(&b, key->format_Bpb, 16);
   nir_def *format_block_size_arr[3] = {
      nir_imm_int(&b, key->format_bw),
      nir_imm_int(&b, key->format_bh),
      nir_imm_int(&b, key->format_bd),
   };
   nir_def *format_block_size = nir_vec(&b, format_block_size_arr, 3);

   /* Constants derived from the shader keys. */
   nir_def *is_block_compressed =
      nir_imm_bool(&b, key->format_bw > 1 || key->format_bh > 1 ||
                       key->format_bd > 1);

   blorp_copy_memory_to_image_indirect_shader(&b, indirect_buf_addr,
                                              stride,
                                              copy_idx,
                                              max_layer,
                                              dest_coord_offsets,
                                              global_id,
                                              dimensions,
                                              forced_layer_or_z,
                                              format_Bpb,
                                              format_block_size,
                                              is_block_compressed);

   return b.shader;
}

static bool
blorp_get_copy_mem_indirect_kernel_cs(struct blorp_batch *batch,
                                      struct blorp_params *params)
{
   struct blorp_context *blorp = batch->blorp;
   const char *key = "copy_mem_indirect_kernel_cs";
   uint32_t key_size = strlen(key);

   if (blorp->lookup_shader(batch, key, key_size, &params->cs_prog_kernel,
                            &params->cs_prog_data))
      return true;

   void *mem_ctx = ralloc_context(NULL);

   nir_shader *nir =
      blorp_build_copy_mem_indirect_shader(batch, mem_ctx);

   const struct blorp_program prog =
      blorp_compile_cs(blorp, mem_ctx, nir, key, key_size);

   bool result = blorp->upload_shader(batch, MESA_SHADER_COMPUTE,
                                      key, key_size,
                                      prog.kernel, prog.kernel_size,
                                      prog.prog_data, prog.prog_data_size,
                                      &params->cs_prog_kernel,
                                      &params->cs_prog_data);
   assert(result);

   ralloc_free(mem_ctx);
   return result;
}

static bool
blorp_get_copy_mem2img_indirect_kernel_cs(struct blorp_batch *batch,
                                          struct blorp_params *params,
                                          struct blorp_indirect_copy_mem2img_key *key)
{
   struct blorp_context *blorp = batch->blorp;

   if (blorp->lookup_shader(batch, key, sizeof(*key), &params->cs_prog_kernel,
                            &params->cs_prog_data))
      return true;

   void *mem_ctx = ralloc_context(NULL);

   nir_shader *nir =
      blorp_build_copy_mem2img_indirect_shader(batch, mem_ctx, key);

   const struct blorp_program prog =
      blorp_compile_cs(blorp, mem_ctx, nir, key, sizeof(*key));

   bool result = blorp->upload_shader(batch, MESA_SHADER_COMPUTE,
                                      key, sizeof(*key),
                                      prog.kernel, prog.kernel_size,
                                      prog.prog_data, prog.prog_data_size,
                                      &params->cs_prog_kernel,
                                      &params->cs_prog_data);
   assert(result);

   ralloc_free(mem_ctx);
   return result;
}

void
blorp_copy_memory_indirect(struct blorp_batch *batch,
                           uint64_t indirect_buf_addr,
                           uint32_t copy_count,
                           uint64_t stride)
{
   struct blorp_params params;
   blorp_params_init(&params);

   params.op = BLORP_OP_COPY_INDIRECT;
   params.shader_type = BLORP_SHADER_TYPE_COPY_INDIRECT;
   params.shader_pipeline = BLORP_SHADER_PIPELINE_COMPUTE;

   params.x0 = 0;
   params.y0 = 0;
   params.x1 = 1;
   params.y1 = 1;

   params.wm_inputs.indirect.indirect_buf_addr = indirect_buf_addr;
   params.wm_inputs.indirect.indirect_buf_stride = stride;
   params.wm_inputs.indirect.copy_count = copy_count;

   if (!blorp_get_copy_mem_indirect_kernel_cs(batch, &params)) {
      mesa_loge("%s: failed to get CS kernel", __func__);
      return;
   }

   batch->blorp->exec(batch, &params);
}

void
blorp_copy_memory_to_image_indirect(struct blorp_batch *batch,
                                    const struct blorp_surf *img_blorp_surf,
                                    uint64_t indirect_buf_addr,
                                    uint64_t indirect_buf_stride,
                                    uint32_t copy_idx,
                                    uint32_t img_mip_level,
                                    int layer_count,
                                    int forced_layer_or_z)
{
   enum isl_format original_format = img_blorp_surf->surf->format;
   const struct isl_format_layout *fmtl =
      isl_format_get_layout(original_format);
   enum isl_format copy_format = get_format_for_copy(fmtl->bpb);
   int dimensions = img_blorp_surf->surf->dim + 1;

   struct blorp_indirect_copy_mem2img_key key;
   BLORP_KEY_INIT(key, BLORP_SHADER_TYPE_COPY_INDIRECT,
                  BLORP_SHADER_PIPELINE_COMPUTE);
   key.dimensions = dimensions;
   key.forced_layer_or_z = forced_layer_or_z;
   key.format_Bpb = fmtl->bpb / 8;
   key.format_bw = fmtl->bw;
   key.format_bh = fmtl->bh;
   key.format_bd = fmtl->bd;

   struct blorp_params params;
   blorp_params_init(&params);

   params.op = BLORP_OP_COPY_IMAGE_INDIRECT;
   params.shader_type = BLORP_SHADER_TYPE_COPY_INDIRECT;
   params.shader_pipeline = BLORP_SHADER_PIPELINE_COMPUTE;

   params.wm_inputs.indirect.indirect_buf_addr = indirect_buf_addr;
   params.wm_inputs.indirect.indirect_buf_stride = indirect_buf_stride;
   params.wm_inputs.indirect.copy_idx = copy_idx;
   params.wm_inputs.indirect.dimensions = dimensions;
   params.wm_inputs.indirect.max_layer =
      img_blorp_surf->surf->logical_level0_px.array_len - 1;
   params.wm_inputs.indirect.forced_layer_or_z = forced_layer_or_z;

   /* params.dst is our image. */
   blorp_surface_info_init(batch, &params.dst, img_blorp_surf,
                           img_mip_level,
                           forced_layer_or_z == -1 ? 0 : forced_layer_or_z,
                           copy_format,
                           true /* is_dest */);

   struct isl_extent3d mip_dimensions = {
      .width = MAX2(params.dst.surf.logical_level0_px.w >> img_mip_level, 1),
      .height = MAX2(params.dst.surf.logical_level0_px.h >> img_mip_level, 1),
      .depth = MAX2(params.dst.surf.logical_level0_px.d >> img_mip_level, 1),
   };

   if (fmtl->bw > 1 || fmtl->bh > 1 || fmtl->bd > 1) {
      blorp_surf_convert_to_uncompressed(batch->blorp->isl_dev,
                                         &params.dst, NULL, NULL, NULL, NULL);
      params.wm_inputs.indirect.x_offset = params.dst.tile_x_sa;
      params.wm_inputs.indirect.y_offset = params.dst.tile_y_sa;

      mip_dimensions.width = params.dst.surf.logical_level0_px.w;
      mip_dimensions.height = params.dst.surf.logical_level0_px.h;
      mip_dimensions.depth = params.dst.surf.logical_level0_px.d;
   }

   /* These settings control the number of workgroups in the shader, see
    * blorp_exec_compute(). We don't need to divide by the local sizes here,
    * this will be done later.
    */
   params.x0 = 0;
   params.y0 = 0;
   params.x1 = mip_dimensions.width;
   params.y1 = mip_dimensions.height;
   if (forced_layer_or_z == -1) {
      /* We set this here so blorp_indirect_buf2img_get_dispatch_size() can
       * read it while figuring out how many shader instances we'll need.
       */
      params.num_layers = layer_count;
   } else {
      params.num_layers = 1;
   }

   if (!blorp_get_copy_mem2img_indirect_kernel_cs(batch, &params, &key)) {
      mesa_loge("%s: failed to get CS kernel", __func__);
      return;
   }

   batch->blorp->exec(batch, &params);
}
