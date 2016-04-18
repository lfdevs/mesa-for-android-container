/*
 * Copyright © 2010 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/** @file brw_fs_visitor.cpp
 *
 * This file supports generating the FS LIR from the GLSL IR.  The LIR
 * makes it easier to do backend-specific optimizations than doing so
 * in the GLSL IR or in the native code.
 */
#include "brw_fs.h"
#include "compiler/glsl_types.h"

using namespace brw;

fs_reg *
fs_visitor::emit_vs_system_value(int location)
{
   fs_reg *reg = new(this->mem_ctx)
      fs_reg(ATTR, 4 * _mesa_bitcount_64(nir->info.inputs_read),
             BRW_REGISTER_TYPE_D);
   brw_vs_prog_data *vs_prog_data = (brw_vs_prog_data *) prog_data;

   switch (location) {
   case SYSTEM_VALUE_BASE_VERTEX:
      reg->reg_offset = 0;
      vs_prog_data->uses_basevertex = true;
      break;
   case SYSTEM_VALUE_BASE_INSTANCE:
      reg->reg_offset = 1;
      vs_prog_data->uses_baseinstance = true;
      break;
   case SYSTEM_VALUE_VERTEX_ID:
      unreachable("should have been lowered");
   case SYSTEM_VALUE_VERTEX_ID_ZERO_BASE:
      reg->reg_offset = 2;
      vs_prog_data->uses_vertexid = true;
      break;
   case SYSTEM_VALUE_INSTANCE_ID:
      reg->reg_offset = 3;
      vs_prog_data->uses_instanceid = true;
      break;
   case SYSTEM_VALUE_DRAW_ID:
      if (nir->info.system_values_read &
          (BITFIELD64_BIT(SYSTEM_VALUE_BASE_VERTEX) |
           BITFIELD64_BIT(SYSTEM_VALUE_BASE_INSTANCE) |
           BITFIELD64_BIT(SYSTEM_VALUE_VERTEX_ID_ZERO_BASE) |
           BITFIELD64_BIT(SYSTEM_VALUE_INSTANCE_ID)))
         reg->nr += 4;
      reg->reg_offset = 0;
      vs_prog_data->uses_drawid = true;
      break;
   default:
      unreachable("not reached");
   }

   return reg;
}

/* Sample from the MCS surface attached to this multisample texture. */
fs_reg
fs_visitor::emit_mcs_fetch(const fs_reg &coordinate, unsigned components,
                           const fs_reg &texture)
{
   const fs_reg dest = vgrf(glsl_type::uvec4_type);

   fs_reg srcs[TEX_LOGICAL_NUM_SRCS];
   srcs[TEX_LOGICAL_SRC_COORDINATE] = coordinate;
   srcs[TEX_LOGICAL_SRC_SURFACE] = texture;
   srcs[TEX_LOGICAL_SRC_SAMPLER] = texture;
   srcs[TEX_LOGICAL_SRC_COORD_COMPONENTS] = brw_imm_d(components);
   srcs[TEX_LOGICAL_SRC_GRAD_COMPONENTS] = brw_imm_d(0);

   fs_inst *inst = bld.emit(SHADER_OPCODE_TXF_MCS_LOGICAL, dest, srcs,
                            ARRAY_SIZE(srcs));

   /* We only care about one or two regs of response, but the sampler always
    * writes 4/8.
    */
   inst->regs_written = 4 * dispatch_width / 8;

   return dest;
}

void
fs_visitor::emit_texture(ir_texture_opcode op,
                         const glsl_type *dest_type,
                         fs_reg coordinate, int coord_components,
                         fs_reg shadow_c,
                         fs_reg lod, fs_reg lod2, int grad_components,
                         fs_reg sample_index,
                         fs_reg offset_value,
                         fs_reg mcs,
                         int gather_component,
                         bool is_cube_array,
                         uint32_t surface,
                         fs_reg surface_reg,
                         uint32_t sampler,
                         fs_reg sampler_reg)
{
   fs_inst *inst = NULL;

   if (op == ir_query_levels) {
      /* textureQueryLevels() is implemented in terms of TXS so we need to
       * pass a valid LOD argument.
       */
      assert(lod.file == BAD_FILE);
      lod = brw_imm_ud(0u);
   }

   if (op == ir_samples_identical) {
      fs_reg dst = vgrf(glsl_type::get_instance(dest_type->base_type, 1, 1));

      /* If mcs is an immediate value, it means there is no MCS.  In that case
       * just return false.
       */
      if (mcs.file == BRW_IMMEDIATE_VALUE) {
         bld.MOV(dst, brw_imm_ud(0u));
      } else if ((key_tex->msaa_16 & (1 << sampler))) {
         fs_reg tmp = vgrf(glsl_type::uint_type);
         bld.OR(tmp, mcs, offset(mcs, bld, 1));
         bld.CMP(dst, tmp, brw_imm_ud(0u), BRW_CONDITIONAL_EQ);
      } else {
         bld.CMP(dst, mcs, brw_imm_ud(0u), BRW_CONDITIONAL_EQ);
      }

      this->result = dst;
      return;
   }

   /* Writemasking doesn't eliminate channels on SIMD8 texture
    * samples, so don't worry about them.
    */
   fs_reg dst = vgrf(glsl_type::get_instance(dest_type->base_type, 4, 1));

   fs_reg srcs[TEX_LOGICAL_NUM_SRCS];
   srcs[TEX_LOGICAL_SRC_COORDINATE] = coordinate;
   srcs[TEX_LOGICAL_SRC_SHADOW_C] = shadow_c;
   srcs[TEX_LOGICAL_SRC_LOD] = lod;
   srcs[TEX_LOGICAL_SRC_LOD2] = lod2;
   srcs[TEX_LOGICAL_SRC_SAMPLE_INDEX] = sample_index;
   srcs[TEX_LOGICAL_SRC_MCS] = mcs;
   srcs[TEX_LOGICAL_SRC_SURFACE] = surface_reg;
   srcs[TEX_LOGICAL_SRC_SAMPLER] = sampler_reg;
   srcs[TEX_LOGICAL_SRC_OFFSET_VALUE] = offset_value;
   srcs[TEX_LOGICAL_SRC_COORD_COMPONENTS] = brw_imm_d(coord_components);
   srcs[TEX_LOGICAL_SRC_GRAD_COMPONENTS] = brw_imm_d(grad_components);

   enum opcode opcode;
   switch (op) {
   case ir_tex:
      opcode = SHADER_OPCODE_TEX_LOGICAL;
      break;
   case ir_txb:
      opcode = FS_OPCODE_TXB_LOGICAL;
      break;
   case ir_txl:
      opcode = SHADER_OPCODE_TXL_LOGICAL;
      break;
   case ir_txd:
      opcode = SHADER_OPCODE_TXD_LOGICAL;
      break;
   case ir_txf:
      opcode = SHADER_OPCODE_TXF_LOGICAL;
      break;
   case ir_txf_ms:
      if ((key_tex->msaa_16 & (1 << sampler)))
         opcode = SHADER_OPCODE_TXF_CMS_W_LOGICAL;
      else
         opcode = SHADER_OPCODE_TXF_CMS_LOGICAL;
      break;
   case ir_txs:
   case ir_query_levels:
      opcode = SHADER_OPCODE_TXS_LOGICAL;
      break;
   case ir_lod:
      opcode = SHADER_OPCODE_LOD_LOGICAL;
      break;
   case ir_tg4:
      opcode = (offset_value.file != BAD_FILE && offset_value.file != IMM ?
                SHADER_OPCODE_TG4_OFFSET_LOGICAL : SHADER_OPCODE_TG4_LOGICAL);
      break;
   default:
      unreachable("Invalid texture opcode.");
   }

   inst = bld.emit(opcode, dst, srcs, ARRAY_SIZE(srcs));
   inst->regs_written = 4 * dispatch_width / 8;

   if (shadow_c.file != BAD_FILE)
      inst->shadow_compare = true;

   if (offset_value.file == IMM)
      inst->offset = offset_value.ud;

   if (op == ir_tg4) {
      if (gather_component == 1 &&
          key_tex->gather_channel_quirk_mask & (1 << surface)) {
         /* gather4 sampler is broken for green channel on RG32F --
          * we must ask for blue instead.
          */
         inst->offset |= 2 << 16;
      } else {
         inst->offset |= gather_component << 16;
      }

      if (devinfo->gen == 6)
         emit_gen6_gather_wa(key_tex->gen6_gather_wa[surface], dst);
   }

   /* fixup #layers for cube map arrays */
   if (op == ir_txs && (devinfo->gen < 7 || is_cube_array)) {
      fs_reg depth = offset(dst, bld, 2);
      fs_reg fixed_depth = vgrf(glsl_type::int_type);

      if (is_cube_array) {
         bld.emit(SHADER_OPCODE_INT_QUOTIENT, fixed_depth, depth, brw_imm_d(6));
      } else if (devinfo->gen < 7) {
         /* Gen4-6 return 0 instead of 1 for single layer surfaces. */
         bld.emit_minmax(fixed_depth, depth, brw_imm_d(1), BRW_CONDITIONAL_GE);
      }

      fs_reg *fixed_payload = ralloc_array(mem_ctx, fs_reg, inst->regs_written);
      int components = inst->regs_written / (inst->exec_size / 8);
      for (int i = 0; i < components; i++) {
         if (i == 2) {
            fixed_payload[i] = fixed_depth;
         } else {
            fixed_payload[i] = offset(dst, bld, i);
         }
      }
      bld.LOAD_PAYLOAD(dst, fixed_payload, components, 0);
   }

   if (op == ir_query_levels) {
      /* # levels is in .w */
      dst = offset(dst, bld, 3);
   }

   this->result = dst;
}

/**
 * Apply workarounds for Gen6 gather with UINT/SINT
 */
void
fs_visitor::emit_gen6_gather_wa(uint8_t wa, fs_reg dst)
{
   if (!wa)
      return;

   int width = (wa & WA_8BIT) ? 8 : 16;

   for (int i = 0; i < 4; i++) {
      fs_reg dst_f = retype(dst, BRW_REGISTER_TYPE_F);
      /* Convert from UNORM to UINT */
      bld.MUL(dst_f, dst_f, brw_imm_f((1 << width) - 1));
      bld.MOV(dst, dst_f);

      if (wa & WA_SIGN) {
         /* Reinterpret the UINT value as a signed INT value by
          * shifting the sign bit into place, then shifting back
          * preserving sign.
          */
         bld.SHL(dst, dst, brw_imm_d(32 - width));
         bld.ASR(dst, dst, brw_imm_d(32 - width));
      }

      dst = offset(dst, bld, 1);
   }
}

/** Emits a dummy fragment shader consisting of magenta for bringup purposes. */
void
fs_visitor::emit_dummy_fs()
{
   int reg_width = dispatch_width / 8;

   /* Everyone's favorite color. */
   const float color[4] = { 1.0, 0.0, 1.0, 0.0 };
   for (int i = 0; i < 4; i++) {
      bld.MOV(fs_reg(MRF, 2 + i * reg_width, BRW_REGISTER_TYPE_F),
              brw_imm_f(color[i]));
   }

   fs_inst *write;
   write = bld.emit(FS_OPCODE_FB_WRITE);
   write->eot = true;
   if (devinfo->gen >= 6) {
      write->base_mrf = 2;
      write->mlen = 4 * reg_width;
   } else {
      write->header_size = 2;
      write->base_mrf = 0;
      write->mlen = 2 + 4 * reg_width;
   }

   /* Tell the SF we don't have any inputs.  Gen4-5 require at least one
    * varying to avoid GPU hangs, so set that.
    */
   brw_wm_prog_data *wm_prog_data = (brw_wm_prog_data *) this->prog_data;
   wm_prog_data->num_varying_inputs = devinfo->gen < 6 ? 1 : 0;
   memset(wm_prog_data->urb_setup, -1,
          sizeof(wm_prog_data->urb_setup[0]) * VARYING_SLOT_MAX);

   /* We don't have any uniforms. */
   stage_prog_data->nr_params = 0;
   stage_prog_data->nr_pull_params = 0;
   stage_prog_data->curb_read_length = 0;
   stage_prog_data->dispatch_grf_start_reg = 2;
   wm_prog_data->dispatch_grf_start_reg_16 = 2;
   grf_used = 1; /* Gen4-5 don't allow zero GRF blocks */

   calculate_cfg();
}

/* The register location here is relative to the start of the URB
 * data.  It will get adjusted to be a real location before
 * generate_code() time.
 */
struct brw_reg
fs_visitor::interp_reg(int location, int channel)
{
   assert(stage == MESA_SHADER_FRAGMENT);
   brw_wm_prog_data *prog_data = (brw_wm_prog_data*) this->prog_data;
   int regnr = prog_data->urb_setup[location] * 2 + channel / 2;
   int stride = (channel & 1) * 4;

   assert(prog_data->urb_setup[location] != -1);

   return brw_vec1_grf(regnr, stride);
}

/** Emits the interpolation for the varying inputs. */
void
fs_visitor::emit_interpolation_setup_gen4()
{
   struct brw_reg g1_uw = retype(brw_vec1_grf(1, 0), BRW_REGISTER_TYPE_UW);

   fs_builder abld = bld.annotate("compute pixel centers");
   this->pixel_x = vgrf(glsl_type::uint_type);
   this->pixel_y = vgrf(glsl_type::uint_type);
   this->pixel_x.type = BRW_REGISTER_TYPE_UW;
   this->pixel_y.type = BRW_REGISTER_TYPE_UW;
   abld.ADD(this->pixel_x,
            fs_reg(stride(suboffset(g1_uw, 4), 2, 4, 0)),
            fs_reg(brw_imm_v(0x10101010)));
   abld.ADD(this->pixel_y,
            fs_reg(stride(suboffset(g1_uw, 5), 2, 4, 0)),
            fs_reg(brw_imm_v(0x11001100)));

   abld = bld.annotate("compute pixel deltas from v0");

   this->delta_xy[BRW_WM_PERSPECTIVE_PIXEL_BARYCENTRIC] =
      vgrf(glsl_type::vec2_type);
   const fs_reg &delta_xy = this->delta_xy[BRW_WM_PERSPECTIVE_PIXEL_BARYCENTRIC];
   const fs_reg xstart(negate(brw_vec1_grf(1, 0)));
   const fs_reg ystart(negate(brw_vec1_grf(1, 1)));

   if (devinfo->has_pln && dispatch_width == 16) {
      for (unsigned i = 0; i < 2; i++) {
         abld.half(i).ADD(half(offset(delta_xy, abld, i), 0),
                          half(this->pixel_x, i), xstart);
         abld.half(i).ADD(half(offset(delta_xy, abld, i), 1),
                          half(this->pixel_y, i), ystart);
      }
   } else {
      abld.ADD(offset(delta_xy, abld, 0), this->pixel_x, xstart);
      abld.ADD(offset(delta_xy, abld, 1), this->pixel_y, ystart);
   }

   abld = bld.annotate("compute pos.w and 1/pos.w");
   /* Compute wpos.w.  It's always in our setup, since it's needed to
    * interpolate the other attributes.
    */
   this->wpos_w = vgrf(glsl_type::float_type);
   abld.emit(FS_OPCODE_LINTERP, wpos_w, delta_xy,
             interp_reg(VARYING_SLOT_POS, 3));
   /* Compute the pixel 1/W value from wpos.w. */
   this->pixel_w = vgrf(glsl_type::float_type);
   abld.emit(SHADER_OPCODE_RCP, this->pixel_w, wpos_w);
}

/** Emits the interpolation for the varying inputs. */
void
fs_visitor::emit_interpolation_setup_gen6()
{
   struct brw_reg g1_uw = retype(brw_vec1_grf(1, 0), BRW_REGISTER_TYPE_UW);

   fs_builder abld = bld.annotate("compute pixel centers");
   if (devinfo->gen >= 8 || dispatch_width == 8) {
      /* The "Register Region Restrictions" page says for BDW (and newer,
       * presumably):
       *
       *     "When destination spans two registers, the source may be one or
       *      two registers. The destination elements must be evenly split
       *      between the two registers."
       *
       * Thus we can do a single add(16) in SIMD8 or an add(32) in SIMD16 to
       * compute our pixel centers.
       */
      fs_reg int_pixel_xy(VGRF, alloc.allocate(dispatch_width / 8),
                          BRW_REGISTER_TYPE_UW);

      const fs_builder dbld = abld.exec_all().group(dispatch_width * 2, 0);
      dbld.ADD(int_pixel_xy,
               fs_reg(stride(suboffset(g1_uw, 4), 1, 4, 0)),
               fs_reg(brw_imm_v(0x11001010)));

      this->pixel_x = vgrf(glsl_type::float_type);
      this->pixel_y = vgrf(glsl_type::float_type);
      abld.emit(FS_OPCODE_PIXEL_X, this->pixel_x, int_pixel_xy);
      abld.emit(FS_OPCODE_PIXEL_Y, this->pixel_y, int_pixel_xy);
   } else {
      /* The "Register Region Restrictions" page says for SNB, IVB, HSW:
       *
       *     "When destination spans two registers, the source MUST span two
       *      registers."
       *
       * Since the GRF source of the ADD will only read a single register, we
       * must do two separate ADDs in SIMD16.
       */
      fs_reg int_pixel_x = vgrf(glsl_type::uint_type);
      fs_reg int_pixel_y = vgrf(glsl_type::uint_type);
      int_pixel_x.type = BRW_REGISTER_TYPE_UW;
      int_pixel_y.type = BRW_REGISTER_TYPE_UW;
      abld.ADD(int_pixel_x,
               fs_reg(stride(suboffset(g1_uw, 4), 2, 4, 0)),
               fs_reg(brw_imm_v(0x10101010)));
      abld.ADD(int_pixel_y,
               fs_reg(stride(suboffset(g1_uw, 5), 2, 4, 0)),
               fs_reg(brw_imm_v(0x11001100)));

      /* As of gen6, we can no longer mix float and int sources.  We have
       * to turn the integer pixel centers into floats for their actual
       * use.
       */
      this->pixel_x = vgrf(glsl_type::float_type);
      this->pixel_y = vgrf(glsl_type::float_type);
      abld.MOV(this->pixel_x, int_pixel_x);
      abld.MOV(this->pixel_y, int_pixel_y);
   }

   abld = bld.annotate("compute pos.w");
   this->pixel_w = fs_reg(brw_vec8_grf(payload.source_w_reg, 0));
   this->wpos_w = vgrf(glsl_type::float_type);
   abld.emit(SHADER_OPCODE_RCP, this->wpos_w, this->pixel_w);

   for (int i = 0; i < BRW_WM_BARYCENTRIC_INTERP_MODE_COUNT; ++i) {
      uint8_t reg = payload.barycentric_coord_reg[i];
      this->delta_xy[i] = fs_reg(brw_vec16_grf(reg, 0));
   }
}

static enum brw_conditional_mod
cond_for_alpha_func(GLenum func)
{
   switch(func) {
      case GL_GREATER:
         return BRW_CONDITIONAL_G;
      case GL_GEQUAL:
         return BRW_CONDITIONAL_GE;
      case GL_LESS:
         return BRW_CONDITIONAL_L;
      case GL_LEQUAL:
         return BRW_CONDITIONAL_LE;
      case GL_EQUAL:
         return BRW_CONDITIONAL_EQ;
      case GL_NOTEQUAL:
         return BRW_CONDITIONAL_NEQ;
      default:
         unreachable("Not reached");
   }
}

/**
 * Alpha test support for when we compile it into the shader instead
 * of using the normal fixed-function alpha test.
 */
void
fs_visitor::emit_alpha_test()
{
   assert(stage == MESA_SHADER_FRAGMENT);
   brw_wm_prog_key *key = (brw_wm_prog_key*) this->key;
   const fs_builder abld = bld.annotate("Alpha test");

   fs_inst *cmp;
   if (key->alpha_test_func == GL_ALWAYS)
      return;

   if (key->alpha_test_func == GL_NEVER) {
      /* f0.1 = 0 */
      fs_reg some_reg = fs_reg(retype(brw_vec8_grf(0, 0),
                                      BRW_REGISTER_TYPE_UW));
      cmp = abld.CMP(bld.null_reg_f(), some_reg, some_reg,
                     BRW_CONDITIONAL_NEQ);
   } else {
      /* RT0 alpha */
      fs_reg color = offset(outputs[0], bld, 3);

      /* f0.1 &= func(color, ref) */
      cmp = abld.CMP(bld.null_reg_f(), color, brw_imm_f(key->alpha_test_ref),
                     cond_for_alpha_func(key->alpha_test_func));
   }
   cmp->predicate = BRW_PREDICATE_NORMAL;
   cmp->flag_subreg = 1;
}

fs_inst *
fs_visitor::emit_single_fb_write(const fs_builder &bld,
                                 fs_reg color0, fs_reg color1,
                                 fs_reg src0_alpha, unsigned components)
{
   assert(stage == MESA_SHADER_FRAGMENT);
   brw_wm_prog_data *prog_data = (brw_wm_prog_data*) this->prog_data;

   /* Hand over gl_FragDepth or the payload depth. */
   const fs_reg dst_depth = (payload.dest_depth_reg ?
                             fs_reg(brw_vec8_grf(payload.dest_depth_reg, 0)) :
                             fs_reg());
   fs_reg src_depth, src_stencil;

   if (source_depth_to_render_target) {
      if (nir->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_DEPTH))
         src_depth = frag_depth;
      else
         src_depth = fs_reg(brw_vec8_grf(payload.source_depth_reg, 0));
   }

   if (nir->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_STENCIL))
      src_stencil = frag_stencil;

   const fs_reg sources[] = {
      color0, color1, src0_alpha, src_depth, dst_depth, src_stencil,
      sample_mask, brw_imm_ud(components)
   };
   assert(ARRAY_SIZE(sources) - 1 == FB_WRITE_LOGICAL_SRC_COMPONENTS);
   fs_inst *write = bld.emit(FS_OPCODE_FB_WRITE_LOGICAL, fs_reg(),
                             sources, ARRAY_SIZE(sources));

   if (prog_data->uses_kill) {
      write->predicate = BRW_PREDICATE_NORMAL;
      write->flag_subreg = 1;
   }

   return write;
}

void
fs_visitor::emit_fb_writes()
{
   assert(stage == MESA_SHADER_FRAGMENT);
   brw_wm_prog_data *prog_data = (brw_wm_prog_data*) this->prog_data;
   brw_wm_prog_key *key = (brw_wm_prog_key*) this->key;

   fs_inst *inst = NULL;

   if (source_depth_to_render_target && devinfo->gen == 6) {
      /* For outputting oDepth on gen6, SIMD8 writes have to be used.  This
       * would require SIMD8 moves of each half to message regs, e.g. by using
       * the SIMD lowering pass.  Unfortunately this is more difficult than it
       * sounds because the SIMD8 single-source message lacks channel selects
       * for the second and third subspans.
       */
      no16("Missing support for simd16 depth writes on gen6\n");
   }

   if (nir->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_STENCIL)) {
      /* From the 'Render Target Write message' section of the docs:
       * "Output Stencil is not supported with SIMD16 Render Target Write
       * Messages."
       *
       * FINISHME: split 16 into 2 8s
       */
      no16("FINISHME: support 2 simd8 writes for gl_FragStencilRefARB\n");
   }

   if (do_dual_src) {
      const fs_builder abld = bld.annotate("FB dual-source write");

      inst = emit_single_fb_write(abld, this->outputs[0],
                                  this->dual_src_output, reg_undef, 4);
      inst->target = 0;

      prog_data->dual_src_blend = true;
   } else {
      for (int target = 0; target < key->nr_color_regions; target++) {
         /* Skip over outputs that weren't written. */
         if (this->outputs[target].file == BAD_FILE)
            continue;

         const fs_builder abld = bld.annotate(
            ralloc_asprintf(this->mem_ctx, "FB write target %d", target));

         fs_reg src0_alpha;
         if (devinfo->gen >= 6 && key->replicate_alpha && target != 0)
            src0_alpha = offset(outputs[0], bld, 3);

         inst = emit_single_fb_write(abld, this->outputs[target], reg_undef,
                                     src0_alpha,
                                     this->output_components[target]);
         inst->target = target;
      }
   }

   if (inst == NULL) {
      /* Even if there's no color buffers enabled, we still need to send
       * alpha out the pipeline to our null renderbuffer to support
       * alpha-testing, alpha-to-coverage, and so on.
       */
      /* FINISHME: Factor out this frequently recurring pattern into a
       * helper function.
       */
      const fs_reg srcs[] = { reg_undef, reg_undef,
                              reg_undef, offset(this->outputs[0], bld, 3) };
      const fs_reg tmp = bld.vgrf(BRW_REGISTER_TYPE_UD, 4);
      bld.LOAD_PAYLOAD(tmp, srcs, 4, 0);

      inst = emit_single_fb_write(bld, tmp, reg_undef, reg_undef, 4);
      inst->target = 0;
   }

   inst->eot = true;
}

void
fs_visitor::setup_uniform_clipplane_values(gl_clip_plane *clip_planes)
{
   const struct brw_vs_prog_key *key =
      (const struct brw_vs_prog_key *) this->key;

   for (int i = 0; i < key->nr_userclip_plane_consts; i++) {
      this->userplane[i] = fs_reg(UNIFORM, uniforms);
      for (int j = 0; j < 4; ++j) {
         stage_prog_data->param[uniforms + j] =
            (gl_constant_value *) &clip_planes[i][j];
      }
      uniforms += 4;
   }
}

/**
 * Lower legacy fixed-function and gl_ClipVertex clipping to clip distances.
 *
 * This does nothing if the shader uses gl_ClipDistance or user clipping is
 * disabled altogether.
 */
void fs_visitor::compute_clip_distance(gl_clip_plane *clip_planes)
{
   struct brw_vue_prog_data *vue_prog_data =
      (struct brw_vue_prog_data *) prog_data;
   const struct brw_vs_prog_key *key =
      (const struct brw_vs_prog_key *) this->key;

   /* Bail unless some sort of legacy clipping is enabled */
   if (key->nr_userclip_plane_consts == 0)
      return;

   /* From the GLSL 1.30 spec, section 7.1 (Vertex Shader Special Variables):
    *
    *     "If a linked set of shaders forming the vertex stage contains no
    *     static write to gl_ClipVertex or gl_ClipDistance, but the
    *     application has requested clipping against user clip planes through
    *     the API, then the coordinate written to gl_Position is used for
    *     comparison against the user clip planes."
    *
    * This function is only called if the shader didn't write to
    * gl_ClipDistance.  Accordingly, we use gl_ClipVertex to perform clipping
    * if the user wrote to it; otherwise we use gl_Position.
    */

   gl_varying_slot clip_vertex = VARYING_SLOT_CLIP_VERTEX;
   if (!(vue_prog_data->vue_map.slots_valid & VARYING_BIT_CLIP_VERTEX))
      clip_vertex = VARYING_SLOT_POS;

   /* If the clip vertex isn't written, skip this.  Typically this means
    * the GS will set up clipping. */
   if (outputs[clip_vertex].file == BAD_FILE)
      return;

   setup_uniform_clipplane_values(clip_planes);

   const fs_builder abld = bld.annotate("user clip distances");

   this->outputs[VARYING_SLOT_CLIP_DIST0] = vgrf(glsl_type::vec4_type);
   this->output_components[VARYING_SLOT_CLIP_DIST0] = 4;
   this->outputs[VARYING_SLOT_CLIP_DIST1] = vgrf(glsl_type::vec4_type);
   this->output_components[VARYING_SLOT_CLIP_DIST1] = 4;

   for (int i = 0; i < key->nr_userclip_plane_consts; i++) {
      fs_reg u = userplane[i];
      fs_reg output = outputs[VARYING_SLOT_CLIP_DIST0 + i / 4];
      output.reg_offset = i & 3;

      abld.MUL(output, outputs[clip_vertex], u);
      for (int j = 1; j < 4; j++) {
         u.nr = userplane[i].nr + j;
         abld.MAD(output, output, offset(outputs[clip_vertex], bld, j), u);
      }
   }
}

void
fs_visitor::emit_urb_writes(const fs_reg &gs_vertex_count)
{
   int slot, urb_offset, length;
   int starting_urb_offset = 0;
   const struct brw_vue_prog_data *vue_prog_data =
      (const struct brw_vue_prog_data *) this->prog_data;
   const struct brw_vs_prog_key *vs_key =
      (const struct brw_vs_prog_key *) this->key;
   const GLbitfield64 psiz_mask =
      VARYING_BIT_LAYER | VARYING_BIT_VIEWPORT | VARYING_BIT_PSIZ;
   const struct brw_vue_map *vue_map = &vue_prog_data->vue_map;
   bool flush;
   fs_reg sources[8];
   fs_reg urb_handle;

   if (stage == MESA_SHADER_TESS_EVAL)
      urb_handle = fs_reg(retype(brw_vec8_grf(4, 0), BRW_REGISTER_TYPE_UD));
   else
      urb_handle = fs_reg(retype(brw_vec8_grf(1, 0), BRW_REGISTER_TYPE_UD));

   /* If we don't have any valid slots to write, just do a minimal urb write
    * send to terminate the shader.  This includes 1 slot of undefined data,
    * because it's invalid to write 0 data:
    *
    * From the Broadwell PRM, Volume 7: 3D Media GPGPU, Shared Functions -
    * Unified Return Buffer (URB) > URB_SIMD8_Write and URB_SIMD8_Read >
    * Write Data Payload:
    *
    *    "The write data payload can be between 1 and 8 message phases long."
    */
   if (vue_map->slots_valid == 0) {
      fs_reg payload = fs_reg(VGRF, alloc.allocate(2), BRW_REGISTER_TYPE_UD);
      bld.exec_all().MOV(payload, urb_handle);

      fs_inst *inst = bld.emit(SHADER_OPCODE_URB_WRITE_SIMD8, reg_undef, payload);
      inst->eot = true;
      inst->mlen = 2;
      inst->offset = 1;
      return;
   }

   opcode opcode = SHADER_OPCODE_URB_WRITE_SIMD8;
   int header_size = 1;
   fs_reg per_slot_offsets;

   if (stage == MESA_SHADER_GEOMETRY) {
      const struct brw_gs_prog_data *gs_prog_data =
         (const struct brw_gs_prog_data *) this->prog_data;

      /* We need to increment the Global Offset to skip over the control data
       * header and the extra "Vertex Count" field (1 HWord) at the beginning
       * of the VUE.  We're counting in OWords, so the units are doubled.
       */
      starting_urb_offset = 2 * gs_prog_data->control_data_header_size_hwords;
      if (gs_prog_data->static_vertex_count == -1)
         starting_urb_offset += 2;

      /* We also need to use per-slot offsets.  The per-slot offset is the
       * Vertex Count.  SIMD8 mode processes 8 different primitives at a
       * time; each may output a different number of vertices.
       */
      opcode = SHADER_OPCODE_URB_WRITE_SIMD8_PER_SLOT;
      header_size++;

      /* The URB offset is in 128-bit units, so we need to multiply by 2 */
      const int output_vertex_size_owords =
         gs_prog_data->output_vertex_size_hwords * 2;

      if (gs_vertex_count.file == IMM) {
         per_slot_offsets = brw_imm_ud(output_vertex_size_owords *
                                       gs_vertex_count.ud);
      } else {
         per_slot_offsets = vgrf(glsl_type::int_type);
         bld.MUL(per_slot_offsets, gs_vertex_count,
                 brw_imm_ud(output_vertex_size_owords));
      }
   }

   length = 0;
   urb_offset = starting_urb_offset;
   flush = false;
   for (slot = 0; slot < vue_map->num_slots; slot++) {
      int varying = vue_map->slot_to_varying[slot];
      switch (varying) {
      case VARYING_SLOT_PSIZ: {
         /* The point size varying slot is the vue header and is always in the
          * vue map.  But often none of the special varyings that live there
          * are written and in that case we can skip writing to the vue
          * header, provided the corresponding state properly clamps the
          * values further down the pipeline. */
         if ((vue_map->slots_valid & psiz_mask) == 0) {
            assert(length == 0);
            urb_offset++;
            break;
         }

         fs_reg zero(VGRF, alloc.allocate(1), BRW_REGISTER_TYPE_UD);
         bld.MOV(zero, brw_imm_ud(0u));

         sources[length++] = zero;
         if (vue_map->slots_valid & VARYING_BIT_LAYER)
            sources[length++] = this->outputs[VARYING_SLOT_LAYER];
         else
            sources[length++] = zero;

         if (vue_map->slots_valid & VARYING_BIT_VIEWPORT)
            sources[length++] = this->outputs[VARYING_SLOT_VIEWPORT];
         else
            sources[length++] = zero;

         if (vue_map->slots_valid & VARYING_BIT_PSIZ)
            sources[length++] = this->outputs[VARYING_SLOT_PSIZ];
         else
            sources[length++] = zero;
         break;
      }
      case BRW_VARYING_SLOT_NDC:
      case VARYING_SLOT_EDGE:
         unreachable("unexpected scalar vs output");
         break;

      default:
         /* gl_Position is always in the vue map, but isn't always written by
          * the shader.  Other varyings (clip distances) get added to the vue
          * map but don't always get written.  In those cases, the
          * corresponding this->output[] slot will be invalid we and can skip
          * the urb write for the varying.  If we've already queued up a vue
          * slot for writing we flush a mlen 5 urb write, otherwise we just
          * advance the urb_offset.
          */
         if (varying == BRW_VARYING_SLOT_PAD ||
             this->outputs[varying].file == BAD_FILE) {
            if (length > 0)
               flush = true;
            else
               urb_offset++;
            break;
         }

         if (stage == MESA_SHADER_VERTEX && vs_key->clamp_vertex_color &&
             (varying == VARYING_SLOT_COL0 ||
              varying == VARYING_SLOT_COL1 ||
              varying == VARYING_SLOT_BFC0 ||
              varying == VARYING_SLOT_BFC1)) {
            /* We need to clamp these guys, so do a saturating MOV into a
             * temp register and use that for the payload.
             */
            for (int i = 0; i < 4; i++) {
               fs_reg reg = fs_reg(VGRF, alloc.allocate(1), outputs[varying].type);
               fs_reg src = offset(this->outputs[varying], bld, i);
               set_saturate(true, bld.MOV(reg, src));
               sources[length++] = reg;
            }
         } else {
            for (unsigned i = 0; i < output_components[varying]; i++)
               sources[length++] = offset(this->outputs[varying], bld, i);
            for (unsigned i = output_components[varying]; i < 4; i++)
               sources[length++] = brw_imm_d(0);
         }
         break;
      }

      const fs_builder abld = bld.annotate("URB write");

      /* If we've queued up 8 registers of payload (2 VUE slots), if this is
       * the last slot or if we need to flush (see BAD_FILE varying case
       * above), emit a URB write send now to flush out the data.
       */
      int last = slot == vue_map->num_slots - 1;
      if (length == 8 || last)
         flush = true;
      if (flush) {
         fs_reg *payload_sources =
            ralloc_array(mem_ctx, fs_reg, length + header_size);
         fs_reg payload = fs_reg(VGRF, alloc.allocate(length + header_size),
                                 BRW_REGISTER_TYPE_F);
         payload_sources[0] = urb_handle;

         if (opcode == SHADER_OPCODE_URB_WRITE_SIMD8_PER_SLOT)
            payload_sources[1] = per_slot_offsets;

         memcpy(&payload_sources[header_size], sources,
                length * sizeof sources[0]);

         abld.LOAD_PAYLOAD(payload, payload_sources, length + header_size,
                           header_size);

         fs_inst *inst = abld.emit(opcode, reg_undef, payload);
         inst->eot = last && stage != MESA_SHADER_GEOMETRY;
         inst->mlen = length + header_size;
         inst->offset = urb_offset;
         urb_offset = starting_urb_offset + slot + 1;
         length = 0;
         flush = false;
      }
   }
}

void
fs_visitor::emit_cs_terminate()
{
   assert(devinfo->gen >= 7);

   /* We are getting the thread ID from the compute shader header */
   assert(stage == MESA_SHADER_COMPUTE);

   /* We can't directly send from g0, since sends with EOT have to use
    * g112-127. So, copy it to a virtual register, The register allocator will
    * make sure it uses the appropriate register range.
    */
   struct brw_reg g0 = retype(brw_vec8_grf(0, 0), BRW_REGISTER_TYPE_UD);
   fs_reg payload = fs_reg(VGRF, alloc.allocate(1), BRW_REGISTER_TYPE_UD);
   bld.group(8, 0).exec_all().MOV(payload, g0);

   /* Send a message to the thread spawner to terminate the thread. */
   fs_inst *inst = bld.exec_all()
                      .emit(CS_OPCODE_CS_TERMINATE, reg_undef, payload);
   inst->eot = true;
}

void
fs_visitor::emit_barrier()
{
   assert(devinfo->gen >= 7);
   const uint32_t barrier_id_mask =
      devinfo->gen >= 9 ? 0x8f000000u : 0x0f000000u;

   /* We are getting the barrier ID from the compute shader header */
   assert(stage == MESA_SHADER_COMPUTE);

   fs_reg payload = fs_reg(VGRF, alloc.allocate(1), BRW_REGISTER_TYPE_UD);

   const fs_builder pbld = bld.exec_all().group(8, 0);

   /* Clear the message payload */
   pbld.MOV(payload, brw_imm_ud(0u));

   /* Copy the barrier id from r0.2 to the message payload reg.2 */
   fs_reg r0_2 = fs_reg(retype(brw_vec1_grf(0, 2), BRW_REGISTER_TYPE_UD));
   pbld.AND(component(payload, 2), r0_2, brw_imm_ud(barrier_id_mask));

   /* Emit a gateway "barrier" message using the payload we set up, followed
    * by a wait instruction.
    */
   bld.exec_all().emit(SHADER_OPCODE_BARRIER, reg_undef, payload);
}

fs_visitor::fs_visitor(const struct brw_compiler *compiler, void *log_data,
                       void *mem_ctx,
                       const void *key,
                       struct brw_stage_prog_data *prog_data,
                       struct gl_program *prog,
                       const nir_shader *shader,
                       unsigned dispatch_width,
                       int shader_time_index,
                       const struct brw_vue_map *input_vue_map)
   : backend_shader(compiler, log_data, mem_ctx, shader, prog_data),
     key(key), gs_compile(NULL), prog_data(prog_data), prog(prog),
     input_vue_map(input_vue_map),
     dispatch_width(dispatch_width),
     shader_time_index(shader_time_index),
     bld(fs_builder(this, dispatch_width).at_end())
{
   init();
}

fs_visitor::fs_visitor(const struct brw_compiler *compiler, void *log_data,
                       void *mem_ctx,
                       struct brw_gs_compile *c,
                       struct brw_gs_prog_data *prog_data,
                       const nir_shader *shader,
                       int shader_time_index)
   : backend_shader(compiler, log_data, mem_ctx, shader,
                    &prog_data->base.base),
     key(&c->key), gs_compile(c),
     prog_data(&prog_data->base.base), prog(NULL),
     dispatch_width(8),
     shader_time_index(shader_time_index),
     bld(fs_builder(this, dispatch_width).at_end())
{
   init();
}


void
fs_visitor::init()
{
   switch (stage) {
   case MESA_SHADER_FRAGMENT:
      key_tex = &((const brw_wm_prog_key *) key)->tex;
      break;
   case MESA_SHADER_VERTEX:
      key_tex = &((const brw_vs_prog_key *) key)->tex;
      break;
   case MESA_SHADER_TESS_EVAL:
      key_tex = &((const brw_tes_prog_key *) key)->tex;
      break;
   case MESA_SHADER_GEOMETRY:
      key_tex = &((const brw_gs_prog_key *) key)->tex;
      break;
   case MESA_SHADER_COMPUTE:
      key_tex = &((const brw_cs_prog_key*) key)->tex;
      break;
   default:
      unreachable("unhandled shader stage");
   }

   if (stage == MESA_SHADER_COMPUTE) {
      const brw_cs_prog_data *cs_prog_data =
         (const brw_cs_prog_data *) prog_data;
      unsigned size = cs_prog_data->local_size[0] *
                      cs_prog_data->local_size[1] *
                      cs_prog_data->local_size[2];
      size = DIV_ROUND_UP(size, devinfo->max_cs_threads);
      min_dispatch_width = size > 16 ? 32 : (size > 8 ? 16 : 8);
   } else {
      min_dispatch_width = 8;
   }

   this->prog_data = this->stage_prog_data;

   this->failed = false;
   this->simd16_unsupported = false;
   this->no16_msg = NULL;

   this->nir_locals = NULL;
   this->nir_ssa_values = NULL;

   memset(&this->payload, 0, sizeof(this->payload));
   memset(this->output_components, 0, sizeof(this->output_components));
   this->source_depth_to_render_target = false;
   this->runtime_check_aads_emit = false;
   this->first_non_payload_grf = 0;
   this->max_grf = devinfo->gen >= 7 ? GEN7_MRF_HACK_START : BRW_MAX_GRF;

   this->virtual_grf_start = NULL;
   this->virtual_grf_end = NULL;
   this->live_intervals = NULL;
   this->regs_live_at_ip = NULL;

   this->uniforms = 0;
   this->last_scratch = 0;
   this->pull_constant_loc = NULL;
   this->push_constant_loc = NULL;

   this->promoted_constants = 0,

   this->spilled_any_registers = false;
   this->do_dual_src = false;

   if (dispatch_width == 8)
      this->param_size = rzalloc_array(mem_ctx, int, stage_prog_data->nr_params);
}

fs_visitor::~fs_visitor()
{
}
