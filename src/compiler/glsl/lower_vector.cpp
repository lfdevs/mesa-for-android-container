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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/**
 * \file lower_vector.cpp
 * IR lowering pass to remove some types of ir_quadop_vector
 *
 * \author Ian Romanick <ian.d.romanick@intel.com>
 */

#include "ir.h"
#include "ir_rvalue_visitor.h"

namespace {

class lower_vector_visitor : public ir_rvalue_visitor {
public:
   lower_vector_visitor() : progress(false)
   {
      /* empty */
   }

   void handle_rvalue(ir_rvalue **rvalue);

   bool progress;
};

} /* anonymous namespace */

void
lower_vector_visitor::handle_rvalue(ir_rvalue **rvalue)
{
   if (!*rvalue)
      return;

   ir_expression *expr = (*rvalue)->as_expression();
   if ((expr == NULL) || (expr->operation != ir_quadop_vector))
      return;

   /* FINISHME: Is this the right thing to use for the ralloc context?
    */
   void *const mem_ctx = expr;

   assert(expr->type->vector_elements == expr->num_operands);

   /* Generate a temporary with the same type as the ir_quadop_operation.
    */
   ir_variable *const temp =
      new(mem_ctx) ir_variable(expr->type, "vecop_tmp", ir_var_temporary);

   this->base_ir->insert_before(temp);

   /* Counter of the number of components collected so far.
    */
   unsigned assigned;

   /* Write-mask in the destination that receives counted by 'assigned'.
    */
   unsigned write_mask;


   /* Generate upto four assignments to that variable.  Try to group component
    * assignments together:
    *
    * - All constant components can be assigned at once.
    * - All assigments of components from a single variable with the same
    *   unary operator can be assigned at once.
    */
   ir_constant_data d = { { 0 } };

   assigned = 0;
   write_mask = 0;
   for (unsigned i = 0; i < expr->type->vector_elements; i++) {
      const ir_constant *const c = expr->operands[i]->as_constant();

      if (c == NULL)
	 continue;

      switch (expr->type->base_type) {
      case GLSL_TYPE_UINT:  d.u[assigned] = c->value.u[0]; break;
      case GLSL_TYPE_INT:   d.i[assigned] = c->value.i[0]; break;
      case GLSL_TYPE_FLOAT: d.f[assigned] = c->value.f[0]; break;
      case GLSL_TYPE_BOOL:  d.b[assigned] = c->value.b[0]; break;
      default:              assert(!"Should not get here."); break;
      }

      write_mask |= (1U << i);
      assigned++;
   }

   assert((write_mask == 0) == (assigned == 0));

   /* If there were constant values, generate an assignment.
    */
   if (assigned > 0) {
      ir_constant *const c =
	 new(mem_ctx) ir_constant(glsl_type::get_instance(expr->type->base_type,
							  assigned, 1),
				  &d);
      ir_dereference *const lhs = new(mem_ctx) ir_dereference_variable(temp);
      ir_assignment *const assign =
	 new(mem_ctx) ir_assignment(lhs, c, write_mask);

      this->base_ir->insert_before(assign);
   }

   /* FINISHME: This should try to coalesce assignments.
    */
   for (unsigned i = 0; i < expr->type->vector_elements; i++) {
      if (expr->operands[i]->ir_type == ir_type_constant)
	 continue;

      ir_dereference *const lhs = new(mem_ctx) ir_dereference_variable(temp);
      ir_assignment *const assign =
	 new(mem_ctx) ir_assignment(lhs, expr->operands[i], 1U << i);

      this->base_ir->insert_before(assign);
      assigned++;
   }

   assert(assigned == expr->type->vector_elements);

   *rvalue = new(mem_ctx) ir_dereference_variable(temp);
   this->progress = true;
}

bool
lower_quadop_vector(exec_list *instructions)
{
   lower_vector_visitor v;

   visit_list_elements(&v, instructions);

   return v.progress;
}
