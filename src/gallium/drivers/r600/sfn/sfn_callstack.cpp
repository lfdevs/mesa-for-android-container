/* -*- mesa-c++  -*-
 *
 * Copyright (c) 2019 Collabora LTD
 *
 * Author: Gert Wollny <gert.wollny@collabora.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "sfn_callstack.h"

namespace r600 {

CallStack::CallStack(r600_bytecode& bc):
   m_bc(bc)
{

}

CallStack::~CallStack()
{
}

int CallStack::push(unsigned type)
{
   switch (type) {
   case FC_PUSH_VPM:
      ++m_bc.stack.push;
      break;
   case FC_PUSH_WQM:
      ++m_bc.stack.push_wqm;
      break;
   case FC_LOOP:
      ++m_bc.stack.loop;
      break;
   default:
      assert(0);
	}

   return update_max_depth(type);
}

void CallStack::pop(unsigned type)
{
   switch(type) {
   case FC_PUSH_VPM:
      --m_bc.stack.push;
      assert(m_bc.stack.push >= 0);
      break;
   case FC_PUSH_WQM:
      --m_bc.stack.push_wqm;
      assert(m_bc.stack.push_wqm >= 0);
      break;
   case FC_LOOP:
      --m_bc.stack.loop;
      assert(m_bc.stack.loop >= 0);
      break;
   default:
      assert(0);
      break;
   }
}

int CallStack::update_max_depth(unsigned type)
{

   r600_stack_info& stack = m_bc.stack;
   int elements;
   int entries;

   int entry_size = stack.entry_size;

   elements = (stack.loop + stack.push_wqm ) * entry_size;
   elements += stack.push;

   /* These next three lines are EVERGREEN specific and should
    * be moved to a virtual function when other chipsets are to
    * be supported */
   assert(m_bc.chip_class == EVERGREEN);
   if (type == FC_PUSH_VPM || stack.push > 0) {
      elements += 1;
   }

   entry_size = 4;

   entries = (elements + (entry_size - 1)) / entry_size;

   if (entries > stack.max_entries)
      stack.max_entries = entries;

   return elements;
}

}
