/****************************************************************************
* Copyright (C) 2014-2016 Intel Corporation.   All Rights Reserved.
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
* 
* @file builder_x86.cpp
* 
* @brief auto-generated file
* 
* DO NOT EDIT
* 
******************************************************************************/

#include "builder.h"

namespace SwrJit
{
    using namespace llvm;

    //////////////////////////////////////////////////////////////////////////
    Value *Builder::VGATHERPS(Value* src, Value* pBase, Value* indices, Value* mask, Value* scale)
    {
        Function *func = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx2_gather_d_ps_256);
        return CALL(func, std::initializer_list<Value*>{src, pBase, indices, mask, scale});
    }

    //////////////////////////////////////////////////////////////////////////
    Value *Builder::VGATHERDD(Value* src, Value* pBase, Value* indices, Value* mask, Value* scale)
    {
        Function *func = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx2_gather_d_d_256);
        return CALL(func, std::initializer_list<Value*>{src, pBase, indices, mask, scale});
    }

    //////////////////////////////////////////////////////////////////////////
    Value *Builder::VSQRTPS(Value* a)
    {
        Function *func = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx_sqrt_ps_256);
        return CALL(func, std::initializer_list<Value*>{a});
    }

    //////////////////////////////////////////////////////////////////////////
    Value *Builder::VRSQRTPS(Value* a)
    {
        Function *func = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx_rsqrt_ps_256);
        return CALL(func, std::initializer_list<Value*>{a});
    }

    //////////////////////////////////////////////////////////////////////////
    Value *Builder::VRCPPS(Value* a)
    {
        Function *func = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx_rcp_ps_256);
        return CALL(func, std::initializer_list<Value*>{a});
    }

    //////////////////////////////////////////////////////////////////////////
    Value *Builder::VMINPS(Value* a, Value* b)
    {
        Function *func = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx_min_ps_256);
        return CALL(func, std::initializer_list<Value*>{a, b});
    }

    //////////////////////////////////////////////////////////////////////////
    Value *Builder::VMAXPS(Value* a, Value* b)
    {
        Function *func = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx_max_ps_256);
        return CALL(func, std::initializer_list<Value*>{a, b});
    }

    //////////////////////////////////////////////////////////////////////////
    Value *Builder::VROUND(Value* a, Value* rounding)
    {
        Function *func = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx_round_ps_256);
        return CALL(func, std::initializer_list<Value*>{a, rounding});
    }

    //////////////////////////////////////////////////////////////////////////
    Value *Builder::VCMPPS(Value* a, Value* b, Value* cmpop)
    {
        Function *func = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx_cmp_ps_256);
        return CALL(func, std::initializer_list<Value*>{a, b, cmpop});
    }

    //////////////////////////////////////////////////////////////////////////
    Value *Builder::VBLENDVPS(Value* a, Value* b, Value* mask)
    {
        Function *func = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx_blendv_ps_256);
        return CALL(func, std::initializer_list<Value*>{a, b, mask});
    }

    //////////////////////////////////////////////////////////////////////////
    Value *Builder::BEXTR_32(Value* src, Value* control)
    {
        Function *func = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_bmi_bextr_32);
        return CALL(func, std::initializer_list<Value*>{src, control});
    }

    //////////////////////////////////////////////////////////////////////////
    Value *Builder::VMASKLOADD(Value* src, Value* mask)
    {
        Function *func = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx2_maskload_d_256);
        return CALL(func, std::initializer_list<Value*>{src, mask});
    }

    //////////////////////////////////////////////////////////////////////////
    Value *Builder::VMASKMOVPS(Value* src, Value* mask)
    {
        Function *func = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx_maskload_ps_256);
        return CALL(func, std::initializer_list<Value*>{src, mask});
    }

    //////////////////////////////////////////////////////////////////////////
    Value *Builder::VMASKSTOREPS(Value* src, Value* mask, Value* val)
    {
        Function *func = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx_maskstore_ps_256);
        return CALL(func, std::initializer_list<Value*>{src, mask, val});
    }

    //////////////////////////////////////////////////////////////////////////
    Value *Builder::VPSHUFB(Value* a, Value* b)
    {
        Function *func = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx2_pshuf_b);
        return CALL(func, std::initializer_list<Value*>{a, b});
    }

    //////////////////////////////////////////////////////////////////////////
    Value *Builder::VPERMD(Value* a, Value* idx)
    {
        Function *func = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx2_permd);
#if (HAVE_LLVM == 0x306) && (LLVM_VERSION_PATCH == 0)
        return CALL(func, std::initializer_list<Value*>{idx, a});
#else
        return CALL(func, std::initializer_list<Value*>{a, idx});
#endif
    }

    //////////////////////////////////////////////////////////////////////////
    Value *Builder::VPERMPS(Value* idx, Value* a)
    {
        Function *func = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx2_permps);
        return CALL(func, std::initializer_list<Value*>{idx, a});
    }

    //////////////////////////////////////////////////////////////////////////
    Value *Builder::VCVTPH2PS(Value* a)
    {
        Function *func = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_vcvtph2ps_256);
        return CALL(func, std::initializer_list<Value*>{a});
    }

    //////////////////////////////////////////////////////////////////////////
    Value *Builder::VCVTPS2PH(Value* a, Value* round)
    {
        Function *func = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_vcvtps2ph_256);
        return CALL(func, std::initializer_list<Value*>{a, round});
    }

    //////////////////////////////////////////////////////////////////////////
    Value *Builder::VHSUBPS(Value* a, Value* b)
    {
        Function *func = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx_hsub_ps_256);
        return CALL(func, std::initializer_list<Value*>{a, b});
    }

    //////////////////////////////////////////////////////////////////////////
    Value *Builder::VPTESTC(Value* a, Value* b)
    {
        Function *func = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx_ptestc_256);
        return CALL(func, std::initializer_list<Value*>{a, b});
    }

    //////////////////////////////////////////////////////////////////////////
    Value *Builder::VPTESTZ(Value* a, Value* b)
    {
        Function *func = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx_ptestz_256);
        return CALL(func, std::initializer_list<Value*>{a, b});
    }

    //////////////////////////////////////////////////////////////////////////
    Value *Builder::VFMADDPS(Value* a, Value* b, Value* c)
    {
        Function *func = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_fma_vfmadd_ps_256);
        return CALL(func, std::initializer_list<Value*>{a, b, c});
    }

    //////////////////////////////////////////////////////////////////////////
    Value *Builder::VMOVMSKPS(Value* a)
    {
        Function *func = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx_movmsk_ps_256);
        return CALL(func, std::initializer_list<Value*>{a});
    }

    //////////////////////////////////////////////////////////////////////////
    Value *Builder::INTERRUPT(Value* a)
    {
        Function *func = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_int);
        return CALL(func, std::initializer_list<Value*>{a});
    }

}
