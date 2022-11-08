/*
 * Copyright 2022 Red Hat.
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

#ifndef LP_BLD_JIT_TYPES_H
#define LP_BLD_JIT_TYPES_H

struct lp_jit_buffer
{
   union {
      const uint32_t *u;
      const float *f;
   };
   uint32_t num_elements;
};

enum {
   LP_JIT_BUFFER_BASE = 0,
   LP_JIT_BUFFER_NUM_ELEMENTS,
   LP_JIT_BUFFER_NUM_FIELDS,
};

LLVMTypeRef
lp_build_create_jit_buffer_type(struct gallivm_state *gallivm);

LLVMValueRef
lp_llvm_buffer_base(struct gallivm_state *gallivm,
                    LLVMValueRef buffers_ptr,
                    LLVMValueRef buffers_offset, unsigned buffers_limit);

LLVMValueRef
lp_llvm_buffer_num_elements(struct gallivm_state *gallivm,
                            LLVMValueRef buffers_ptr,
                            LLVMValueRef buffers_offset, unsigned buffers_limit);


#endif
