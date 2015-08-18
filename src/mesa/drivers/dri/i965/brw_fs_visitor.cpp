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
#include <sys/types.h>

#include "main/macros.h"
#include "main/shaderobj.h"
#include "program/prog_parameter.h"
#include "program/prog_print.h"
#include "program/prog_optimize.h"
#include "util/register_allocate.h"
#include "program/hash_table.h"
#include "brw_context.h"
#include "brw_eu.h"
#include "brw_wm.h"
#include "brw_cs.h"
#include "brw_vec4.h"
#include "brw_fs.h"
#include "main/uniforms.h"
#include "glsl/glsl_types.h"
#include "glsl/ir_optimization.h"
#include "program/sampler.h"

using namespace brw;

fs_reg *
fs_visitor::emit_vs_system_value(int location)
{
   fs_reg *reg = new(this->mem_ctx)
      fs_reg(ATTR, VERT_ATTRIB_MAX, BRW_REGISTER_TYPE_D);
   brw_vs_prog_data *vs_prog_data = (brw_vs_prog_data *) prog_data;

   switch (location) {
   case SYSTEM_VALUE_BASE_VERTEX:
      reg->reg_offset = 0;
      vs_prog_data->uses_vertexid = true;
      break;
   case SYSTEM_VALUE_VERTEX_ID:
   case SYSTEM_VALUE_VERTEX_ID_ZERO_BASE:
      reg->reg_offset = 2;
      vs_prog_data->uses_vertexid = true;
      break;
   case SYSTEM_VALUE_INSTANCE_ID:
      reg->reg_offset = 3;
      vs_prog_data->uses_instanceid = true;
      break;
   default:
      unreachable("not reached");
   }

   return reg;
}

fs_reg
fs_visitor::rescale_texcoord(fs_reg coordinate, int coord_components,
                             bool is_rect, uint32_t sampler, int texunit)
{
   bool needs_gl_clamp = true;
   fs_reg scale_x, scale_y;

   /* The 965 requires the EU to do the normalization of GL rectangle
    * texture coordinates.  We use the program parameter state
    * tracking to get the scaling factor.
    */
   if (is_rect &&
       (devinfo->gen < 6 ||
        (devinfo->gen >= 6 && (key_tex->gl_clamp_mask[0] & (1 << sampler) ||
                               key_tex->gl_clamp_mask[1] & (1 << sampler))))) {
      struct gl_program_parameter_list *params = prog->Parameters;
      int tokens[STATE_LENGTH] = {
	 STATE_INTERNAL,
	 STATE_TEXRECT_SCALE,
	 texunit,
	 0,
	 0
      };

      no16("rectangle scale uniform setup not supported on SIMD16\n");
      if (dispatch_width == 16) {
	 return coordinate;
      }

      GLuint index = _mesa_add_state_reference(params,
					       (gl_state_index *)tokens);
      /* Try to find existing copies of the texrect scale uniforms. */
      for (unsigned i = 0; i < uniforms; i++) {
         if (stage_prog_data->param[i] ==
             &prog->Parameters->ParameterValues[index][0]) {
            scale_x = fs_reg(UNIFORM, i);
            scale_y = fs_reg(UNIFORM, i + 1);
            break;
         }
      }

      /* If we didn't already set them up, do so now. */
      if (scale_x.file == BAD_FILE) {
         scale_x = fs_reg(UNIFORM, uniforms);
         scale_y = fs_reg(UNIFORM, uniforms + 1);

         stage_prog_data->param[uniforms++] =
            &prog->Parameters->ParameterValues[index][0];
         stage_prog_data->param[uniforms++] =
            &prog->Parameters->ParameterValues[index][1];
      }
   }

   /* The 965 requires the EU to do the normalization of GL rectangle
    * texture coordinates.  We use the program parameter state
    * tracking to get the scaling factor.
    */
   if (devinfo->gen < 6 && is_rect) {
      fs_reg dst = fs_reg(GRF, alloc.allocate(coord_components));
      fs_reg src = coordinate;
      coordinate = dst;

      bld.MUL(dst, src, scale_x);
      dst = offset(dst, bld, 1);
      src = offset(src, bld, 1);
      bld.MUL(dst, src, scale_y);
   } else if (is_rect) {
      /* On gen6+, the sampler handles the rectangle coordinates
       * natively, without needing rescaling.  But that means we have
       * to do GL_CLAMP clamping at the [0, width], [0, height] scale,
       * not [0, 1] like the default case below.
       */
      needs_gl_clamp = false;

      for (int i = 0; i < 2; i++) {
	 if (key_tex->gl_clamp_mask[i] & (1 << sampler)) {
	    fs_reg chan = coordinate;
	    chan = offset(chan, bld, i);

            set_condmod(BRW_CONDITIONAL_GE,
                        bld.emit(BRW_OPCODE_SEL, chan, chan, fs_reg(0.0f)));

	    /* Our parameter comes in as 1.0/width or 1.0/height,
	     * because that's what people normally want for doing
	     * texture rectangle handling.  We need width or height
	     * for clamping, but we don't care enough to make a new
	     * parameter type, so just invert back.
	     */
	    fs_reg limit = vgrf(glsl_type::float_type);
            bld.MOV(limit, i == 0 ? scale_x : scale_y);
            bld.emit(SHADER_OPCODE_RCP, limit, limit);

            set_condmod(BRW_CONDITIONAL_L,
                        bld.emit(BRW_OPCODE_SEL, chan, chan, limit));
	 }
      }
   }

   if (coord_components > 0 && needs_gl_clamp) {
      for (int i = 0; i < MIN2(coord_components, 3); i++) {
	 if (key_tex->gl_clamp_mask[i] & (1 << sampler)) {
	    fs_reg chan = coordinate;
	    chan = offset(chan, bld, i);
            set_saturate(true, bld.MOV(chan, chan));
	 }
      }
   }
   return coordinate;
}

/* Sample from the MCS surface attached to this multisample texture. */
fs_reg
fs_visitor::emit_mcs_fetch(const fs_reg &coordinate, unsigned components,
                           const fs_reg &sampler)
{
   const fs_reg dest = vgrf(glsl_type::uvec4_type);
   const fs_reg srcs[] = {
      coordinate, fs_reg(), fs_reg(), fs_reg(), fs_reg(), fs_reg(),
      sampler, fs_reg(), fs_reg(components), fs_reg(0)
   };
   fs_inst *inst = bld.emit(SHADER_OPCODE_TXF_MCS_LOGICAL, dest, srcs,
                            ARRAY_SIZE(srcs));

   /* We only care about one reg of response, but the sampler always writes
    * 4/8.
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
                         bool is_rect,
                         uint32_t sampler,
                         fs_reg sampler_reg, int texunit)
{
   fs_inst *inst = NULL;

   if (op == ir_tg4) {
      /* When tg4 is used with the degenerate ZERO/ONE swizzles, don't bother
       * emitting anything other than setting up the constant result.
       */
      int swiz = GET_SWZ(key_tex->swizzles[sampler], gather_component);
      if (swiz == SWIZZLE_ZERO || swiz == SWIZZLE_ONE) {

         fs_reg res = vgrf(glsl_type::vec4_type);
         this->result = res;

         for (int i=0; i<4; i++) {
            bld.MOV(res, fs_reg(swiz == SWIZZLE_ZERO ? 0.0f : 1.0f));
            res = offset(res, bld, 1);
         }
         return;
      }
   }

   if (op == ir_query_levels) {
      /* textureQueryLevels() is implemented in terms of TXS so we need to
       * pass a valid LOD argument.
       */
      assert(lod.file == BAD_FILE);
      lod = fs_reg(0u);
   }

   if (coordinate.file != BAD_FILE) {
      /* FINISHME: Texture coordinate rescaling doesn't work with non-constant
       * samplers.  This should only be a problem with GL_CLAMP on Gen7.
       */
      coordinate = rescale_texcoord(coordinate, coord_components, is_rect,
                                    sampler, texunit);
   }

   /* Writemasking doesn't eliminate channels on SIMD8 texture
    * samples, so don't worry about them.
    */
   fs_reg dst = vgrf(glsl_type::get_instance(dest_type->base_type, 4, 1));
   const fs_reg srcs[] = {
      coordinate, shadow_c, lod, lod2,
      sample_index, mcs, sampler_reg, offset_value,
      fs_reg(coord_components), fs_reg(grad_components)
   };
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
      inst->offset = offset_value.fixed_hw_reg.dw1.ud;

   if (op == ir_tg4) {
      inst->offset |=
         gather_channel(gather_component, sampler) << 16; /* M0.2:16-17 */

      if (devinfo->gen == 6)
         emit_gen6_gather_wa(key_tex->gen6_gather_wa[sampler], dst);
   }

   /* fixup #layers for cube map arrays */
   if (op == ir_txs && is_cube_array) {
      fs_reg depth = offset(dst, bld, 2);
      fs_reg fixed_depth = vgrf(glsl_type::int_type);
      bld.emit(SHADER_OPCODE_INT_QUOTIENT, fixed_depth, depth, fs_reg(6));

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

   swizzle_result(op, dest_type->vector_elements, dst, sampler);
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
      bld.MUL(dst_f, dst_f, fs_reg((float)((1 << width) - 1)));
      bld.MOV(dst, dst_f);

      if (wa & WA_SIGN) {
         /* Reinterpret the UINT value as a signed INT value by
          * shifting the sign bit into place, then shifting back
          * preserving sign.
          */
         bld.SHL(dst, dst, fs_reg(32 - width));
         bld.ASR(dst, dst, fs_reg(32 - width));
      }

      dst = offset(dst, bld, 1);
   }
}

/**
 * Set up the gather channel based on the swizzle, for gather4.
 */
uint32_t
fs_visitor::gather_channel(int orig_chan, uint32_t sampler)
{
   int swiz = GET_SWZ(key_tex->swizzles[sampler], orig_chan);
   switch (swiz) {
      case SWIZZLE_X: return 0;
      case SWIZZLE_Y:
         /* gather4 sampler is broken for green channel on RG32F --
          * we must ask for blue instead.
          */
         if (key_tex->gather_channel_quirk_mask & (1 << sampler))
            return 2;
         return 1;
      case SWIZZLE_Z: return 2;
      case SWIZZLE_W: return 3;
      default:
         unreachable("Not reached"); /* zero, one swizzles handled already */
   }
}

/**
 * Swizzle the result of a texture result.  This is necessary for
 * EXT_texture_swizzle as well as DEPTH_TEXTURE_MODE for shadow comparisons.
 */
void
fs_visitor::swizzle_result(ir_texture_opcode op, int dest_components,
                           fs_reg orig_val, uint32_t sampler)
{
   if (op == ir_query_levels) {
      /* # levels is in .w */
      this->result = offset(orig_val, bld, 3);
      return;
   }

   this->result = orig_val;

   /* txs,lod don't actually sample the texture, so swizzling the result
    * makes no sense.
    */
   if (op == ir_txs || op == ir_lod || op == ir_tg4)
      return;

   if (dest_components == 1) {
      /* Ignore DEPTH_TEXTURE_MODE swizzling. */
   } else if (key_tex->swizzles[sampler] != SWIZZLE_NOOP) {
      fs_reg swizzled_result = vgrf(glsl_type::vec4_type);
      swizzled_result.type = orig_val.type;

      for (int i = 0; i < 4; i++) {
	 int swiz = GET_SWZ(key_tex->swizzles[sampler], i);
	 fs_reg l = swizzled_result;
	 l = offset(l, bld, i);

	 if (swiz == SWIZZLE_ZERO) {
            bld.MOV(l, fs_reg(0.0f));
	 } else if (swiz == SWIZZLE_ONE) {
            bld.MOV(l, fs_reg(1.0f));
	 } else {
            bld.MOV(l, offset(orig_val, bld,
                                  GET_SWZ(key_tex->swizzles[sampler], i)));
	 }
      }
      this->result = swizzled_result;
   }
}

/**
 * Try to replace IF/MOV/ELSE/MOV/ENDIF with SEL.
 *
 * Many GLSL shaders contain the following pattern:
 *
 *    x = condition ? foo : bar
 *
 * The compiler emits an ir_if tree for this, since each subexpression might be
 * a complex tree that could have side-effects or short-circuit logic.
 *
 * However, the common case is to simply select one of two constants or
 * variable values---which is exactly what SEL is for.  In this case, the
 * assembly looks like:
 *
 *    (+f0) IF
 *    MOV dst src0
 *    ELSE
 *    MOV dst src1
 *    ENDIF
 *
 * which can be easily translated into:
 *
 *    (+f0) SEL dst src0 src1
 *
 * If src0 is an immediate value, we promote it to a temporary GRF.
 */
bool
fs_visitor::try_replace_with_sel()
{
   fs_inst *endif_inst = (fs_inst *) instructions.get_tail();
   assert(endif_inst->opcode == BRW_OPCODE_ENDIF);

   /* Pattern match in reverse: IF, MOV, ELSE, MOV, ENDIF. */
   int opcodes[] = {
      BRW_OPCODE_IF, BRW_OPCODE_MOV, BRW_OPCODE_ELSE, BRW_OPCODE_MOV,
   };

   fs_inst *match = (fs_inst *) endif_inst->prev;
   for (int i = 0; i < 4; i++) {
      if (match->is_head_sentinel() || match->opcode != opcodes[4-i-1])
         return false;
      match = (fs_inst *) match->prev;
   }

   /* The opcodes match; it looks like the right sequence of instructions. */
   fs_inst *else_mov = (fs_inst *) endif_inst->prev;
   fs_inst *then_mov = (fs_inst *) else_mov->prev->prev;
   fs_inst *if_inst = (fs_inst *) then_mov->prev;

   /* Check that the MOVs are the right form. */
   if (then_mov->dst.equals(else_mov->dst) &&
       !then_mov->is_partial_write() &&
       !else_mov->is_partial_write()) {

      /* Remove the matched instructions; we'll emit a SEL to replace them. */
      while (!if_inst->next->is_tail_sentinel())
         if_inst->next->exec_node::remove();
      if_inst->exec_node::remove();

      /* Only the last source register can be a constant, so if the MOV in
       * the "then" clause uses a constant, we need to put it in a temporary.
       */
      fs_reg src0(then_mov->src[0]);
      if (src0.file == IMM) {
         src0 = vgrf(glsl_type::float_type);
         src0.type = then_mov->src[0].type;
         bld.MOV(src0, then_mov->src[0]);
      }

      if (if_inst->conditional_mod) {
         /* Sandybridge-specific IF with embedded comparison */
         bld.CMP(bld.null_reg_d(), if_inst->src[0], if_inst->src[1],
                 if_inst->conditional_mod);
         set_predicate(BRW_PREDICATE_NORMAL,
                       bld.emit(BRW_OPCODE_SEL, then_mov->dst,
                                src0, else_mov->src[0]));
      } else {
         /* Separate CMP and IF instructions */
         set_predicate_inv(if_inst->predicate, if_inst->predicate_inverse,
                           bld.emit(BRW_OPCODE_SEL, then_mov->dst,
                                    src0, else_mov->src[0]));
      }

      return true;
   }

   return false;
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
              fs_reg(color[i]));
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
      fs_reg int_pixel_xy(GRF, alloc.allocate(dispatch_width / 8),
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
      cmp = abld.CMP(bld.null_reg_f(), color, fs_reg(key->alpha_test_ref),
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
   fs_reg src_depth;

   if (source_depth_to_render_target) {
      if (prog->OutputsWritten & BITFIELD64_BIT(FRAG_RESULT_DEPTH))
         src_depth = frag_depth;
      else
         src_depth = fs_reg(brw_vec8_grf(payload.source_depth_reg, 0));
   }

   const fs_reg sources[] = {
      color0, color1, src0_alpha, src_depth, dst_depth, sample_mask,
      fs_reg(components)
   };
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
   const struct brw_vue_prog_key *key =
      (const struct brw_vue_prog_key *) this->key;

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
   const struct brw_vue_prog_key *key =
      (const struct brw_vue_prog_key *) this->key;

   /* Bail unless some sort of legacy clipping is enabled */
   if (!key->userclip_active || prog->UsesClipDistanceOut)
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
   this->outputs[VARYING_SLOT_CLIP_DIST1] = vgrf(glsl_type::vec4_type);

   for (int i = 0; i < key->nr_userclip_plane_consts; i++) {
      fs_reg u = userplane[i];
      fs_reg output = outputs[VARYING_SLOT_CLIP_DIST0 + i / 4];
      output.reg_offset = i & 3;

      abld.MUL(output, outputs[clip_vertex], u);
      for (int j = 1; j < 4; j++) {
         u.reg = userplane[i].reg + j;
         abld.MAD(output, output, offset(outputs[clip_vertex], bld, j), u);
      }
   }
}

void
fs_visitor::emit_urb_writes()
{
   int slot, urb_offset, length;
   struct brw_vs_prog_data *vs_prog_data =
      (struct brw_vs_prog_data *) prog_data;
   const struct brw_vs_prog_key *key =
      (const struct brw_vs_prog_key *) this->key;
   const GLbitfield64 psiz_mask =
      VARYING_BIT_LAYER | VARYING_BIT_VIEWPORT | VARYING_BIT_PSIZ;
   const struct brw_vue_map *vue_map = &vs_prog_data->base.vue_map;
   bool flush;
   fs_reg sources[8];

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
      fs_reg payload = fs_reg(GRF, alloc.allocate(2), BRW_REGISTER_TYPE_UD);
      bld.exec_all().MOV(payload, fs_reg(retype(brw_vec8_grf(1, 0),
                                                BRW_REGISTER_TYPE_UD)));

      fs_inst *inst = bld.emit(SHADER_OPCODE_URB_WRITE_SIMD8, reg_undef, payload);
      inst->eot = true;
      inst->mlen = 2;
      inst->offset = 1;
      return;
   }

   length = 0;
   urb_offset = 0;
   flush = false;
   for (slot = 0; slot < vue_map->num_slots; slot++) {
      fs_reg reg, src, zero;

      int varying = vue_map->slot_to_varying[slot];
      switch (varying) {
      case VARYING_SLOT_PSIZ:

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

         zero = fs_reg(GRF, alloc.allocate(1), BRW_REGISTER_TYPE_UD);
         bld.MOV(zero, fs_reg(0u));

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

      case BRW_VARYING_SLOT_NDC:
      case VARYING_SLOT_EDGE:
         unreachable("unexpected scalar vs output");
         break;

      case BRW_VARYING_SLOT_PAD:
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
         if (this->outputs[varying].file == BAD_FILE) {
            if (length > 0)
               flush = true;
            else
               urb_offset++;
            break;
         }

         if ((varying == VARYING_SLOT_COL0 ||
              varying == VARYING_SLOT_COL1 ||
              varying == VARYING_SLOT_BFC0 ||
              varying == VARYING_SLOT_BFC1) &&
             key->clamp_vertex_color) {
            /* We need to clamp these guys, so do a saturating MOV into a
             * temp register and use that for the payload.
             */
            for (int i = 0; i < 4; i++) {
               reg = fs_reg(GRF, alloc.allocate(1), outputs[varying].type);
               src = offset(this->outputs[varying], bld, i);
               set_saturate(true, bld.MOV(reg, src));
               sources[length++] = reg;
            }
         } else {
            for (int i = 0; i < 4; i++)
               sources[length++] = offset(this->outputs[varying], bld, i);
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
         fs_reg *payload_sources = ralloc_array(mem_ctx, fs_reg, length + 1);
         fs_reg payload = fs_reg(GRF, alloc.allocate(length + 1),
                                 BRW_REGISTER_TYPE_F);
         payload_sources[0] =
            fs_reg(retype(brw_vec8_grf(1, 0), BRW_REGISTER_TYPE_UD));

         memcpy(&payload_sources[1], sources, length * sizeof sources[0]);
         abld.LOAD_PAYLOAD(payload, payload_sources, length + 1, 1);

         fs_inst *inst =
            abld.emit(SHADER_OPCODE_URB_WRITE_SIMD8, reg_undef, payload);
         inst->eot = last;
         inst->mlen = length + 1;
         inst->offset = urb_offset;
         urb_offset = slot + 1;
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
   fs_reg payload = fs_reg(GRF, alloc.allocate(1), BRW_REGISTER_TYPE_UD);
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

   /* We are getting the barrier ID from the compute shader header */
   assert(stage == MESA_SHADER_COMPUTE);

   fs_reg payload = fs_reg(GRF, alloc.allocate(1), BRW_REGISTER_TYPE_UD);

   /* Clear the message payload */
   bld.exec_all().MOV(payload, fs_reg(0u));

   /* Copy bits 27:24 of r0.2 (barrier id) to the message payload reg.2 */
   fs_reg r0_2 = fs_reg(retype(brw_vec1_grf(0, 2), BRW_REGISTER_TYPE_UD));
   bld.exec_all().AND(component(payload, 2), r0_2, fs_reg(0x0f000000u));

   /* Emit a gateway "barrier" message using the payload we set up, followed
    * by a wait instruction.
    */
   bld.exec_all().emit(SHADER_OPCODE_BARRIER, reg_undef, payload);
}

fs_visitor::fs_visitor(const struct brw_compiler *compiler, void *log_data,
                       void *mem_ctx,
                       gl_shader_stage stage,
                       const void *key,
                       struct brw_stage_prog_data *prog_data,
                       struct gl_shader_program *shader_prog,
                       struct gl_program *prog,
                       unsigned dispatch_width,
                       int shader_time_index)
   : backend_shader(compiler, log_data, mem_ctx,
                    shader_prog, prog, prog_data, stage),
     key(key), prog_data(prog_data),
     dispatch_width(dispatch_width),
     shader_time_index(shader_time_index),
     promoted_constants(0),
     bld(fs_builder(this, dispatch_width).at_end())
{
   switch (stage) {
   case MESA_SHADER_FRAGMENT:
      key_tex = &((const brw_wm_prog_key *) key)->tex;
      break;
   case MESA_SHADER_VERTEX:
   case MESA_SHADER_GEOMETRY:
      key_tex = &((const brw_vue_prog_key *) key)->tex;
      break;
   case MESA_SHADER_COMPUTE:
      key_tex = &((const brw_cs_prog_key*) key)->tex;
      break;
   default:
      unreachable("unhandled shader stage");
   }

   this->failed = false;
   this->simd16_unsupported = false;
   this->no16_msg = NULL;

   this->nir_locals = NULL;
   this->nir_ssa_values = NULL;

   memset(&this->payload, 0, sizeof(this->payload));
   memset(this->outputs, 0, sizeof(this->outputs));
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

   this->spilled_any_registers = false;
   this->do_dual_src = false;

   if (dispatch_width == 8)
      this->param_size = rzalloc_array(mem_ctx, int, stage_prog_data->nr_params);
}

fs_visitor::~fs_visitor()
{
}
