/*
 * Copyright © 2012 Intel Corporation
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
 * \file brw_lower_texture_gradients.cpp
 */

#include "glsl/ir.h"
#include "glsl/ir_builder.h"
#include "program/prog_instruction.h"
#include "brw_context.h"

using namespace ir_builder;

class lower_texture_grad_visitor : public ir_hierarchical_visitor {
public:
   lower_texture_grad_visitor(bool has_sample_d_c)
      : has_sample_d_c(has_sample_d_c)
   {
      progress = false;
   }

   ir_visitor_status visit_leave(ir_texture *ir);


   bool progress;
   bool has_sample_d_c;

private:
   void emit(ir_variable *, ir_rvalue *);
   ir_variable *temp(void *ctx, const glsl_type *type, const char *name);
};

/**
 * Emit a variable declaration and an assignment to initialize it.
 */
void
lower_texture_grad_visitor::emit(ir_variable *var, ir_rvalue *value)
{
   base_ir->insert_before(var);
   base_ir->insert_before(assign(var, value));
}

/**
 * Emit a temporary variable declaration
 */
ir_variable *
lower_texture_grad_visitor::temp(void *ctx, const glsl_type *type, const char *name)
{
   ir_variable *var = new(ctx) ir_variable(type, name, ir_var_temporary);
   base_ir->insert_before(var);
   return var;
}

static const glsl_type *
txs_type(const glsl_type *type)
{
   unsigned dims;
   switch (type->sampler_dimensionality) {
   case GLSL_SAMPLER_DIM_1D:
      dims = 1;
      break;
   case GLSL_SAMPLER_DIM_2D:
   case GLSL_SAMPLER_DIM_RECT:
   case GLSL_SAMPLER_DIM_CUBE:
      dims = 2;
      break;
   case GLSL_SAMPLER_DIM_3D:
      dims = 3;
      break;
   default:
      unreachable("Should not get here: invalid sampler dimensionality");
   }

   if (type->sampler_array)
      dims++;

   return glsl_type::get_instance(GLSL_TYPE_INT, dims, 1);
}

ir_visitor_status
lower_texture_grad_visitor::visit_leave(ir_texture *ir)
{
   /* Only lower textureGrad with cube maps or shadow samplers */
   if (ir->op != ir_txd ||
      (ir->sampler->type->sampler_dimensionality != GLSL_SAMPLER_DIM_CUBE &&
       !ir->shadow_comparitor))
      return visit_continue;

   /* Lower textureGrad() with samplerCube* even if we have the sample_d_c
    * message.  GLSL provides gradients for the 'r' coordinate.  Unfortunately:
    *
    * From the Ivybridge PRM, Volume 4, Part 1, sample_d message description:
    * "The r coordinate contains the faceid, and the r gradients are ignored
    *  by hardware."
    */
   bool need_lowering = !has_sample_d_c ||
      ir->sampler->type->sampler_dimensionality == GLSL_SAMPLER_DIM_CUBE;

   if (!need_lowering)
      return visit_continue;

   void *mem_ctx = ralloc_parent(ir);

   const glsl_type *grad_type = ir->lod_info.grad.dPdx->type;

   /* Use textureSize() to get the width and height of LOD 0; swizzle away
    * the depth/number of array slices.
    */
   ir_texture *txs = new(mem_ctx) ir_texture(ir_txs);
   txs->set_sampler(ir->sampler->clone(mem_ctx, NULL),
		    txs_type(ir->sampler->type));
   txs->lod_info.lod = new(mem_ctx) ir_constant(0);
   ir_variable *size =
      new(mem_ctx) ir_variable(grad_type, "size", ir_var_temporary);
   if (ir->sampler->type->sampler_dimensionality == GLSL_SAMPLER_DIM_CUBE) {
      base_ir->insert_before(size);
      base_ir->insert_before(assign(size,
                                    swizzle_for_size(expr(ir_unop_i2f, txs), 2),
                                    WRITEMASK_XY));
      base_ir->insert_before(assign(size, new(mem_ctx) ir_constant(1.0f), WRITEMASK_Z));
   } else {
      emit(size, expr(ir_unop_i2f,
                      swizzle_for_size(txs, grad_type->vector_elements)));
   }

   /* Scale the gradients by width and height.  Effectively, the incoming
    * gradients are s'(x,y), t'(x,y), and r'(x,y) from equation 3.19 in the
    * GL 3.0 spec; we want u'(x,y), which is w_t * s'(x,y).
    */
   ir_variable *dPdx =
      new(mem_ctx) ir_variable(grad_type, "dPdx", ir_var_temporary);
   emit(dPdx, mul(size, ir->lod_info.grad.dPdx));

   ir_variable *dPdy =
      new(mem_ctx) ir_variable(grad_type, "dPdy", ir_var_temporary);
   emit(dPdy, mul(size, ir->lod_info.grad.dPdy));

   ir->op = ir_txl;
   if (ir->sampler->type->sampler_dimensionality == GLSL_SAMPLER_DIM_CUBE) {
      /* Cubemap texture lookups first generate a texture coordinate normalized
       * to [-1, 1] on the appropiate face. The appropiate face is determined
       * by which component has largest magnitude and its sign. The texture
       * coordinate is the quotient of the remaining texture coordinates against
       * that absolute value of the component of largest magnitude. This
       * division requires that the computing of the derivative of the texel
       * coordinate must use the quotient rule. The high level GLSL code is as
       * follows:
       *
       * Step 1: selection
       *
       * vec3 abs_p, Q, dQdx, dQdy;
       * abs_p = abs(ir->coordinate);
       * if (abs_p.x >= max(abs_p.y, abs_p.z)) {
       *    Q = ir->coordinate.yzx;
       *    dQdx = ir->lod_info.grad.dPdx.yzx;
       *    dQdy = ir->lod_info.grad.dPdy.yzx;
       * }
       * if (abs_p.y >= max(abs_p.x, abs_p.z)) {
       *    Q = ir->coordinate.xzy;
       *    dQdx = ir->lod_info.grad.dPdx.xzy;
       *    dQdy = ir->lod_info.grad.dPdy.xzy;
       * }
       * if (abs_p.z >= max(abs_p.x, abs_p.y)) {
       *    Q = ir->coordinate;
       *    dQdx = ir->lod_info.grad.dPdx;
       *    dQdy = ir->lod_info.grad.dPdy;
       * }
       *
       * Step 2: use quotient rule to compute derivative. The normalized to
       * [-1, 1] texel coordinate is given by Q.xy / (sign(Q.z) * Q.z). We are
       * only concerned with the magnitudes of the derivatives whose values are
       * not affected by the sign. We drop the sign from the computation.
       *
       * vec2 dx, dy;
       * float recip;
       *
       * recip = 1.0 / Q.z;
       * dx = recip * ( dQdx.xy - Q.xy * (dQdx.z * recip) );
       * dy = recip * ( dQdy.xy - Q.xy * (dQdy.z * recip) );
       *
       * Step 3: compute LOD. At this point we have the derivatives of the
       * texture coordinates normalized to [-1,1]. We take the LOD to be
       *  result = log2(max(sqrt(dot(dx, dx)), sqrt(dy, dy)) * 0.5 * L)
       *         = -1.0 + log2(max(sqrt(dot(dx, dx)), sqrt(dy, dy)) * L)
       *         = -1.0 + log2(sqrt(max(dot(dx, dx), dot(dy,dy))) * L)
       *         = -1.0 + log2(sqrt(L * L * max(dot(dx, dx), dot(dy,dy))))
       *         = -1.0 + 0.5 * log2(L * L * max(dot(dx, dx), dot(dy,dy)))
       * where L is the dimension of the cubemap. The code is:
       *
       * float M, result;
       * M = max(dot(dx, dx), dot(dy, dy));
       * L = textureSize(sampler, 0).x;
       * result = -1.0 + 0.5 * log2(L * L * M);
       */

/* Helpers to make code more human readable. */
#define EMIT(instr) base_ir->insert_before(instr)
#define THEN(irif, instr) irif->then_instructions.push_tail(instr)
#define CLONE(x) x->clone(mem_ctx, NULL)

      ir_variable *abs_p = temp(mem_ctx, glsl_type::vec3_type, "abs_p");

      EMIT(assign(abs_p, swizzle_for_size(abs(CLONE(ir->coordinate)), 3)));

      ir_variable *Q = temp(mem_ctx, glsl_type::vec3_type, "Q");
      ir_variable *dQdx = temp(mem_ctx, glsl_type::vec3_type, "dQdx");
      ir_variable *dQdy = temp(mem_ctx, glsl_type::vec3_type, "dQdy");

      /* unmodified dPdx, dPdy values */
      ir_rvalue *dPdx = ir->lod_info.grad.dPdx;
      ir_rvalue *dPdy = ir->lod_info.grad.dPdy;

      /* 1. compute selector */

      /* if (abs_p.x >= max(abs_p.y, abs_p.z))  ... */
      ir_if *branch_x =
         new(mem_ctx) ir_if(gequal(swizzle_x(abs_p),
                                   max2(swizzle_y(abs_p), swizzle_z(abs_p))));

      /* Q = p.yzx;
       * dQdx = dPdx.yzx;
       * dQdy = dPdy.yzx;
       */
      int yzx = MAKE_SWIZZLE4(SWIZZLE_Y, SWIZZLE_Z, SWIZZLE_X, 0);
      THEN(branch_x, assign(Q, swizzle(CLONE(ir->coordinate), yzx, 3)));
      THEN(branch_x, assign(dQdx, swizzle(CLONE(dPdx), yzx, 3)));
      THEN(branch_x, assign(dQdy, swizzle(CLONE(dPdy), yzx, 3)));
      EMIT(branch_x);

      /* if (abs_p.y >= max(abs_p.x, abs_p.z)) */
      ir_if *branch_y =
         new(mem_ctx) ir_if(gequal(swizzle_y(abs_p),
                                   max2(swizzle_x(abs_p), swizzle_z(abs_p))));

      /* Q = p.xzy;
       * dQdx = dPdx.xzy;
       * dQdy = dPdy.xzy;
       */
      int xzy = MAKE_SWIZZLE4(SWIZZLE_X, SWIZZLE_Z, SWIZZLE_Y, 0);
      THEN(branch_y, assign(Q, swizzle(CLONE(ir->coordinate), xzy, 3)));
      THEN(branch_y, assign(dQdx, swizzle(CLONE(dPdx), xzy, 3)));
      THEN(branch_y, assign(dQdy, swizzle(CLONE(dPdy), xzy, 3)));
      EMIT(branch_y);

      /* if (abs_p.z >= max(abs_p.x, abs_p.y)) */
      ir_if *branch_z =
         new(mem_ctx) ir_if(gequal(swizzle_z(abs_p),
                            max2(swizzle_x(abs_p), swizzle_y(abs_p))));

      /* Q = p;
       * dQdx = dPdx;
       * dQdy = dPdy;
       */
      THEN(branch_z, assign(Q, swizzle_for_size(CLONE(ir->coordinate), 3)));
      THEN(branch_z, assign(dQdx, CLONE(dPdx)));
      THEN(branch_z, assign(dQdy, CLONE(dPdy)));
      EMIT(branch_z);

      /* 2. quotient rule */
      ir_variable *recip = temp(mem_ctx, glsl_type::float_type, "recip");
      EMIT(assign(recip, div(new(mem_ctx) ir_constant(1.0f), swizzle_z(Q))));

      ir_variable *dx = temp(mem_ctx, glsl_type::vec2_type, "dx");
      ir_variable *dy = temp(mem_ctx, glsl_type::vec2_type, "dy");

      /* tmp = Q.xy * recip;
       * dx = recip * ( dQdx.xy - (tmp * dQdx.z) );
       * dy = recip * ( dQdy.xy - (tmp * dQdy.z) );
       */
      ir_variable *tmp = temp(mem_ctx, glsl_type::vec2_type, "tmp");
      EMIT(assign(tmp, mul(swizzle_xy(Q), recip)));
      EMIT(assign(dx, mul(recip, sub(swizzle_xy(dQdx),
                                     mul(tmp, swizzle_z(dQdx))))));
      EMIT(assign(dy, mul(recip, sub(swizzle_xy(dQdy),
                                     mul(tmp, swizzle_z(dQdy))))));

      /* M = max(dot(dx, dx), dot(dy, dy)); */
      ir_variable *M = temp(mem_ctx, glsl_type::float_type, "M");
      EMIT(assign(M, max2(dot(dx, dx), dot(dy, dy))));

      /* size has textureSize() of LOD 0 */
      ir_variable *L = temp(mem_ctx, glsl_type::float_type, "L");
      EMIT(assign(L, swizzle_x(size)));

      ir_variable *result = temp(mem_ctx, glsl_type::float_type, "result");

      /* result = -1.0 + 0.5 * log2(L * L * M); */
      EMIT(assign(result,
                  add(new(mem_ctx)ir_constant(-1.0f),
                      mul(new(mem_ctx)ir_constant(0.5f),
                          expr(ir_unop_log2, mul(mul(L, L), M))))));

      /* 3. final assignment of parameters to textureLod call */
      ir->lod_info.lod = new (mem_ctx) ir_dereference_variable(result);

#undef THEN
#undef EMIT

   } else {
      /* Calculate rho from equation 3.20 of the GL 3.0 specification. */
      ir_rvalue *rho;
      if (dPdx->type->is_scalar()) {
         rho = expr(ir_binop_max, expr(ir_unop_abs, dPdx),
                    expr(ir_unop_abs, dPdy));
      } else {
         rho = expr(ir_binop_max, expr(ir_unop_sqrt, dot(dPdx, dPdx)),
                    expr(ir_unop_sqrt, dot(dPdy, dPdy)));
      }

      /* lambda_base = log2(rho).  We're ignoring GL state biases for now. */
      ir->lod_info.lod = expr(ir_unop_log2, rho);
   }

   progress = true;
   return visit_continue;
}

extern "C" {

bool
brw_lower_texture_gradients(struct brw_context *brw,
                            struct exec_list *instructions)
{
   bool has_sample_d_c = brw->gen >= 8 || brw->is_haswell;
   lower_texture_grad_visitor v(has_sample_d_c);

   visit_list_elements(&v, instructions);

   return v.progress;
}

}
