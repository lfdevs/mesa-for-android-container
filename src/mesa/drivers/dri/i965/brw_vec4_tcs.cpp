/*
 * Copyright © 2013 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/**
 * \file brw_vec4_tcs.cpp
 *
 * Tessellaton control shader specific code derived from the vec4_visitor class.
 */

#include "brw_nir.h"
#include "brw_vec4_tcs.h"
#include "brw_fs.h"

namespace brw {

vec4_tcs_visitor::vec4_tcs_visitor(const struct brw_compiler *compiler,
                                   void *log_data,
                                   const struct brw_tcs_prog_key *key,
                                   struct brw_tcs_prog_data *prog_data,
                                   const nir_shader *nir,
                                   void *mem_ctx,
                                   int shader_time_index,
                                   const struct brw_vue_map *input_vue_map)
   : vec4_visitor(compiler, log_data, &key->tex, &prog_data->base,
                  nir, mem_ctx, false, shader_time_index),
     input_vue_map(input_vue_map), key(key)
{
}


void
vec4_tcs_visitor::nir_setup_system_value_intrinsic(nir_intrinsic_instr *instr)
{
}

dst_reg *
vec4_tcs_visitor::make_reg_for_system_value(int location)
{
   return NULL;
}


void
vec4_tcs_visitor::setup_payload()
{
   int reg = 0;

   /* The payload always contains important data in r0, which contains
    * the URB handles that are passed on to the URB write at the end
    * of the thread.
    */
   reg++;

   /* r1.0 - r4.7 may contain the input control point URB handles,
    * which we use to pull vertex data.
    */
   reg += 4;

   /* Push constants may start at r5.0 */
   reg = setup_uniforms(reg);

   this->first_non_payload_grf = reg;
}


void
vec4_tcs_visitor::emit_prolog()
{
   invocation_id = src_reg(this, glsl_type::uint_type);
   emit(TCS_OPCODE_GET_INSTANCE_ID, dst_reg(invocation_id));

   /* HS threads are dispatched with the dispatch mask set to 0xFF.
    * If there are an odd number of output vertices, then the final
    * HS instance dispatched will only have its bottom half doing real
    * work, and so we need to disable the upper half:
    */
   if (nir->info.tcs.vertices_out % 2) {
      emit(CMP(dst_null_d(), invocation_id,
               brw_imm_ud(nir->info.tcs.vertices_out), BRW_CONDITIONAL_L));

      /* Matching ENDIF is in emit_thread_end() */
      emit(IF(BRW_PREDICATE_NORMAL));
   }
}


void
vec4_tcs_visitor::emit_thread_end()
{
   vec4_instruction *inst;
   current_annotation = "thread end";

   if (nir->info.tcs.vertices_out % 2) {
      emit(BRW_OPCODE_ENDIF);
   }

   if (devinfo->gen == 7) {
      struct brw_tcs_prog_data *tcs_prog_data =
         (struct brw_tcs_prog_data *) prog_data;

      current_annotation = "release input vertices";

      /* Synchronize all threads, so we know that no one is still
       * using the input URB handles.
       */
      if (tcs_prog_data->instances > 1) {
         dst_reg header = dst_reg(this, glsl_type::uvec4_type);
         emit(TCS_OPCODE_CREATE_BARRIER_HEADER, header);
         emit(SHADER_OPCODE_BARRIER, dst_null_ud(), src_reg(header));
      }

      /* Make thread 0 (invocations <1, 0>) release pairs of ICP handles.
       * We want to compare the bottom half of invocation_id with 0, but
       * use that truth value for the top half as well.  Unfortunately,
       * we don't have stride in the vec4 world, nor UV immediates in
       * align16, so we need an opcode to get invocation_id<0,4,0>.
       */
      set_condmod(BRW_CONDITIONAL_Z,
                  emit(TCS_OPCODE_SRC0_010_IS_ZERO, dst_null_d(),
                       invocation_id));
      emit(IF(BRW_PREDICATE_NORMAL));
      for (unsigned i = 0; i < key->input_vertices; i += 2) {
         /* If we have an odd number of input vertices, the last will be
          * unpaired.  We don't want to use an interleaved URB write in
          * that case.
          */
         const bool is_unpaired = i == key->input_vertices - 1;

         dst_reg header(this, glsl_type::uvec4_type);
         emit(TCS_OPCODE_RELEASE_INPUT, header, brw_imm_ud(i),
              brw_imm_ud(is_unpaired));
      }
      emit(BRW_OPCODE_ENDIF);
   }

   if (unlikely(INTEL_DEBUG & DEBUG_SHADER_TIME))
      emit_shader_time_end();

   inst = emit(TCS_OPCODE_THREAD_END);
   inst->base_mrf = 14;
   inst->mlen = 2;
}


void
vec4_tcs_visitor::emit_input_urb_read(const dst_reg &dst,
                                      const src_reg &vertex_index,
                                      unsigned base_offset,
                                      const src_reg &indirect_offset)
{
   vec4_instruction *inst;
   dst_reg temp(this, glsl_type::ivec4_type);
   temp.type = dst.type;

   /* Set up the message header to reference the proper parts of the URB */
   dst_reg header = dst_reg(this, glsl_type::uvec4_type);
   inst = emit(TCS_OPCODE_SET_INPUT_URB_OFFSETS, header, vertex_index,
               indirect_offset);
   inst->force_writemask_all = true;

   /* Read into a temporary, ignoring writemasking. */
   inst = emit(VEC4_OPCODE_URB_READ, temp, src_reg(header));
   inst->offset = base_offset;
   inst->mlen = 1;
   inst->base_mrf = -1;

   /* Copy the temporary to the destination to deal with writemasking.
    *
    * Also attempt to deal with gl_PointSize being in the .w component.
    */
   if (inst->offset == 0 && indirect_offset.file == BAD_FILE) {
      emit(MOV(dst, swizzle(src_reg(temp), BRW_SWIZZLE_WWWW)));
   } else {
      emit(MOV(dst, src_reg(temp)));
   }
}

void
vec4_tcs_visitor::emit_output_urb_read(const dst_reg &dst,
                                       unsigned base_offset,
                                       const src_reg &indirect_offset)
{
   vec4_instruction *inst;

   /* Set up the message header to reference the proper parts of the URB */
   dst_reg header = dst_reg(this, glsl_type::uvec4_type);
   inst = emit(TCS_OPCODE_SET_OUTPUT_URB_OFFSETS, header,
               brw_imm_ud(dst.writemask), indirect_offset);
   inst->force_writemask_all = true;

   /* Read into a temporary, ignoring writemasking. */
   vec4_instruction *read = emit(VEC4_OPCODE_URB_READ, dst, src_reg(header));
   read->offset = base_offset;
   read->mlen = 1;
   read->base_mrf = -1;
}

void
vec4_tcs_visitor::emit_urb_write(const src_reg &value,
                                 unsigned writemask,
                                 unsigned base_offset,
                                 const src_reg &indirect_offset)
{
   if (writemask == 0)
      return;

   src_reg message(this, glsl_type::uvec4_type, 2);
   vec4_instruction *inst;

   inst = emit(TCS_OPCODE_SET_OUTPUT_URB_OFFSETS, dst_reg(message),
               brw_imm_ud(writemask), indirect_offset);
   inst->force_writemask_all = true;
   inst = emit(MOV(offset(dst_reg(retype(message, value.type)), 1), value));
   inst->force_writemask_all = true;

   inst = emit(TCS_OPCODE_URB_WRITE, dst_null_f(), message);
   inst->offset = base_offset;
   inst->mlen = 2;
   inst->base_mrf = -1;
}

void
vec4_tcs_visitor::nir_emit_intrinsic(nir_intrinsic_instr *instr)
{
   switch (instr->intrinsic) {
   case nir_intrinsic_load_invocation_id:
      emit(MOV(get_nir_dest(instr->dest, BRW_REGISTER_TYPE_UD),
               invocation_id));
      break;
   case nir_intrinsic_load_primitive_id:
      emit(TCS_OPCODE_GET_PRIMITIVE_ID,
           get_nir_dest(instr->dest, BRW_REGISTER_TYPE_UD));
      break;
   case nir_intrinsic_load_patch_vertices_in:
      emit(MOV(get_nir_dest(instr->dest, BRW_REGISTER_TYPE_D),
               brw_imm_d(key->input_vertices)));
      break;
   case nir_intrinsic_load_per_vertex_input: {
      src_reg indirect_offset = get_indirect_offset(instr);
      unsigned imm_offset = instr->const_index[0];

      nir_const_value *vertex_const = nir_src_as_const_value(instr->src[0]);
      src_reg vertex_index =
         vertex_const ? src_reg(brw_imm_ud(vertex_const->u32[0]))
                      : get_nir_src(instr->src[0], BRW_REGISTER_TYPE_UD, 1);

      dst_reg dst = get_nir_dest(instr->dest, BRW_REGISTER_TYPE_D);
      dst.writemask = brw_writemask_for_size(instr->num_components);

      emit_input_urb_read(dst, vertex_index, imm_offset, indirect_offset);
      break;
   }
   case nir_intrinsic_load_input:
      unreachable("nir_lower_io should use load_per_vertex_input intrinsics");
      break;
   case nir_intrinsic_load_output:
   case nir_intrinsic_load_per_vertex_output: {
      src_reg indirect_offset = get_indirect_offset(instr);
      unsigned imm_offset = instr->const_index[0];

      dst_reg dst = get_nir_dest(instr->dest, BRW_REGISTER_TYPE_D);
      dst.writemask = brw_writemask_for_size(instr->num_components);

      if (imm_offset == 0 && indirect_offset.file == BAD_FILE) {
         dst.type = BRW_REGISTER_TYPE_F;

         /* This is a read of gl_TessLevelInner[], which lives in the
          * Patch URB header.  The layout depends on the domain.
          */
         switch (key->tes_primitive_mode) {
         case GL_QUADS: {
            /* DWords 3-2 (reversed); use offset 0 and WZYX swizzle. */
            dst_reg tmp(this, glsl_type::vec4_type);
            emit_output_urb_read(tmp, 0, src_reg());
            emit(MOV(writemask(dst, WRITEMASK_XY),
                     swizzle(src_reg(tmp), BRW_SWIZZLE_WZYX)));
            break;
         }
         case GL_TRIANGLES:
            /* DWord 4; use offset 1 but normal swizzle/writemask. */
            emit_output_urb_read(writemask(dst, WRITEMASK_X), 1, src_reg());
            break;
         case GL_ISOLINES:
            /* All channels are undefined. */
            return;
         default:
            unreachable("Bogus tessellation domain");
         }
      } else if (imm_offset == 1 && indirect_offset.file == BAD_FILE) {
         dst.type = BRW_REGISTER_TYPE_F;
         unsigned swiz = BRW_SWIZZLE_WZYX;

         /* This is a read of gl_TessLevelOuter[], which lives in the
          * high 4 DWords of the Patch URB header, in reverse order.
          */
         switch (key->tes_primitive_mode) {
         case GL_QUADS:
            dst.writemask = WRITEMASK_XYZW;
            break;
         case GL_TRIANGLES:
            dst.writemask = WRITEMASK_XYZ;
            break;
         case GL_ISOLINES:
            /* Isolines are not reversed; swizzle .zw -> .xy */
            swiz = BRW_SWIZZLE_ZWZW;
            dst.writemask = WRITEMASK_XY;
            return;
         default:
            unreachable("Bogus tessellation domain");
         }

         dst_reg tmp(this, glsl_type::vec4_type);
         emit_output_urb_read(tmp, 1, src_reg());
         emit(MOV(dst, swizzle(src_reg(tmp), swiz)));
      } else {
         emit_output_urb_read(dst, imm_offset, indirect_offset);
      }
      break;
   }
   case nir_intrinsic_store_output:
   case nir_intrinsic_store_per_vertex_output: {
      src_reg value = get_nir_src(instr->src[0]);
      unsigned mask = instr->const_index[1];
      unsigned swiz = BRW_SWIZZLE_XYZW;

      src_reg indirect_offset = get_indirect_offset(instr);
      unsigned imm_offset = instr->const_index[0];

      /* The passthrough shader writes the whole patch header as two vec4s;
       * skip all the gl_TessLevelInner/Outer swizzling.
       */
      if (indirect_offset.file == BAD_FILE && !is_passthrough_shader) {
         if (imm_offset == 0) {
            value.type = BRW_REGISTER_TYPE_F;

            mask &=
               (1 << tesslevel_inner_components(key->tes_primitive_mode)) - 1;

            /* This is a write to gl_TessLevelInner[], which lives in the
             * Patch URB header.  The layout depends on the domain.
             */
            switch (key->tes_primitive_mode) {
            case GL_QUADS:
               /* gl_TessLevelInner[].xy lives at DWords 3-2 (reversed).
                * We use an XXYX swizzle to reverse put .xy in the .wz
                * channels, and use a .zw writemask.
                */
               swiz = BRW_SWIZZLE4(0, 0, 1, 0);
               mask = writemask_for_backwards_vector(mask);
               break;
            case GL_TRIANGLES:
               /* gl_TessLevelInner[].x lives at DWord 4, so we set the
                * writemask to X and bump the URB offset by 1.
                */
               imm_offset = 1;
               break;
            case GL_ISOLINES:
               /* Skip; gl_TessLevelInner[] doesn't exist for isolines. */
               return;
            default:
               unreachable("Bogus tessellation domain");
            }
         } else if (imm_offset == 1) {
            value.type = BRW_REGISTER_TYPE_F;

            mask &=
               (1 << tesslevel_outer_components(key->tes_primitive_mode)) - 1;

            /* This is a write to gl_TessLevelOuter[] which lives in the
             * Patch URB Header at DWords 4-7.  However, it's reversed, so
             * instead of .xyzw we have .wzyx.
             */
            if (key->tes_primitive_mode == GL_ISOLINES) {
               /* Isolines .xy should be stored in .zw, in order. */
               swiz = BRW_SWIZZLE4(0, 0, 0, 1);
               mask <<= 2;
            } else {
               /* Other domains are reversed; store .wzyx instead of .xyzw. */
               swiz = BRW_SWIZZLE_WZYX;
               mask = writemask_for_backwards_vector(mask);
            }
         }
      }

      emit_urb_write(swizzle(value, swiz), mask,
                     imm_offset, indirect_offset);
      break;
   }

   case nir_intrinsic_barrier: {
      dst_reg header = dst_reg(this, glsl_type::uvec4_type);
      emit(TCS_OPCODE_CREATE_BARRIER_HEADER, header);
      emit(SHADER_OPCODE_BARRIER, dst_null_ud(), src_reg(header));
      break;
   }

   default:
      vec4_visitor::nir_emit_intrinsic(instr);
   }
}


extern "C" const unsigned *
brw_compile_tcs(const struct brw_compiler *compiler,
                void *log_data,
                void *mem_ctx,
                const struct brw_tcs_prog_key *key,
                struct brw_tcs_prog_data *prog_data,
                const nir_shader *src_shader,
                int shader_time_index,
                unsigned *final_assembly_size,
                char **error_str)
{
   const struct brw_device_info *devinfo = compiler->devinfo;
   struct brw_vue_prog_data *vue_prog_data = &prog_data->base;
   const bool is_scalar = compiler->scalar_stage[MESA_SHADER_TESS_CTRL];

   nir_shader *nir = nir_shader_clone(mem_ctx, src_shader);
   nir->info.outputs_written = key->outputs_written;
   nir->info.patch_outputs_written = key->patch_outputs_written;

   struct brw_vue_map input_vue_map;
   brw_compute_vue_map(devinfo, &input_vue_map,
                       nir->info.inputs_read & ~VARYING_BIT_PRIMITIVE_ID,
                       true);

   brw_compute_tess_vue_map(&vue_prog_data->vue_map,
                            nir->info.outputs_written,
                            nir->info.patch_outputs_written);

   nir = brw_nir_apply_sampler_key(nir, devinfo, &key->tex, is_scalar);
   brw_nir_lower_vue_inputs(nir, is_scalar, &input_vue_map);
   brw_nir_lower_tcs_outputs(nir, &vue_prog_data->vue_map);
   nir = brw_postprocess_nir(nir, compiler->devinfo, is_scalar);

   if (is_scalar)
      prog_data->instances = DIV_ROUND_UP(nir->info.tcs.vertices_out, 8);
   else
      prog_data->instances = DIV_ROUND_UP(nir->info.tcs.vertices_out, 2);

   /* Compute URB entry size.  The maximum allowed URB entry size is 32k.
    * That divides up as follows:
    *
    *     32 bytes for the patch header (tessellation factors)
    *    480 bytes for per-patch varyings (a varying component is 4 bytes and
    *              gl_MaxTessPatchComponents = 120)
    *  16384 bytes for per-vertex varyings (a varying component is 4 bytes,
    *              gl_MaxPatchVertices = 32 and
    *              gl_MaxTessControlOutputComponents = 128)
    *
    *  15808 bytes left for varying packing overhead
    */
   const int num_per_patch_slots = vue_prog_data->vue_map.num_per_patch_slots;
   const int num_per_vertex_slots = vue_prog_data->vue_map.num_per_vertex_slots;
   unsigned output_size_bytes = 0;
   /* Note that the patch header is counted in num_per_patch_slots. */
   output_size_bytes += num_per_patch_slots * 16;
   output_size_bytes += nir->info.tcs.vertices_out * num_per_vertex_slots * 16;

   assert(output_size_bytes >= 1);
   if (output_size_bytes > GEN7_MAX_HS_URB_ENTRY_SIZE_BYTES)
      return NULL;

   /* URB entry sizes are stored as a multiple of 64 bytes. */
   vue_prog_data->urb_entry_size = ALIGN(output_size_bytes, 64) / 64;

   /* HS does not use the usual payload pushing from URB to GRFs,
    * because we don't have enough registers for a full-size payload, and
    * the hardware is broken on Haswell anyway.
    */
   vue_prog_data->urb_read_length = 0;

   if (unlikely(INTEL_DEBUG & DEBUG_TCS)) {
      fprintf(stderr, "TCS Input ");
      brw_print_vue_map(stderr, &input_vue_map);
      fprintf(stderr, "TCS Output ");
      brw_print_vue_map(stderr, &vue_prog_data->vue_map);
   }

   if (is_scalar) {
      fs_visitor v(compiler, log_data, mem_ctx, (void *) key,
                   &prog_data->base.base, NULL, nir, 8,
                   shader_time_index, &input_vue_map);
      if (!v.run_tcs_single_patch()) {
         if (error_str)
            *error_str = ralloc_strdup(mem_ctx, v.fail_msg);
         return NULL;
      }

      prog_data->base.base.dispatch_grf_start_reg = v.payload.num_regs;
      prog_data->base.dispatch_mode = DISPATCH_MODE_SIMD8;

      fs_generator g(compiler, log_data, mem_ctx, (void *) key,
                     &prog_data->base.base, v.promoted_constants, false,
                     MESA_SHADER_TESS_CTRL);
      if (unlikely(INTEL_DEBUG & DEBUG_TCS)) {
         g.enable_debug(ralloc_asprintf(mem_ctx,
                                        "%s tessellation control shader %s",
                                        nir->info.label ? nir->info.label
                                                        : "unnamed",
                                        nir->info.name));
      }

      g.generate_code(v.cfg, 8);

      return g.get_assembly(final_assembly_size);
   } else {
      vec4_tcs_visitor v(compiler, log_data, key, prog_data,
                         nir, mem_ctx, shader_time_index, &input_vue_map);
      if (!v.run()) {
         if (error_str)
            *error_str = ralloc_strdup(mem_ctx, v.fail_msg);
         return NULL;
      }

      if (unlikely(INTEL_DEBUG & DEBUG_TCS))
         v.dump_instructions();


      return brw_vec4_generate_assembly(compiler, log_data, mem_ctx, nir,
                                        &prog_data->base, v.cfg,
                                        final_assembly_size);
   }
}


} /* namespace brw */
