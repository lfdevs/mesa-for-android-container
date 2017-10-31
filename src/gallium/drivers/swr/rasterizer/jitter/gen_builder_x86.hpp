//============================================================================
// Copyright (C) 2014-2017 Intel Corporation.   All Rights Reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice (including the next
// paragraph) shall be included in all copies or substantial portions of the
// Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
//
// @file gen_builder_x86.hpp
//
// @brief auto-generated file
//
// DO NOT EDIT
//
// Generation Command Line:
//  ./rasterizer/codegen/gen_llvm_ir_macros.py
//    --output
//    rasterizer/jitter
//    --gen_x86_h
//
//============================================================================
#pragma once

//============================================================================
// Auto-generated x86 intrinsics
//============================================================================

Value* VGATHERPD(Value* src, Value* pBase, Value* indices, Value* mask, Value* scale)
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx2_gather_d_pd_256);
    return CALL(pFunc, std::initializer_list<Value*>{src, pBase, indices, mask, scale});
}

Value* VGATHERPS(Value* src, Value* pBase, Value* indices, Value* mask, Value* scale)
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx2_gather_d_ps_256);
    return CALL(pFunc, std::initializer_list<Value*>{src, pBase, indices, mask, scale});
}

Value* VGATHERDD(Value* src, Value* pBase, Value* indices, Value* mask, Value* scale)
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx2_gather_d_d_256);
    return CALL(pFunc, std::initializer_list<Value*>{src, pBase, indices, mask, scale});
}

Value* VPSRLI(Value* src, Value* imm)
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx2_psrli_d);
    return CALL(pFunc, std::initializer_list<Value*>{src, imm});
}

Value* VSQRTPS(Value* a)
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx_sqrt_ps_256);
    return CALL(pFunc, std::initializer_list<Value*>{a});
}

Value* VRSQRTPS(Value* a)
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx_rsqrt_ps_256);
    return CALL(pFunc, std::initializer_list<Value*>{a});
}

Value* VRCPPS(Value* a)
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx_rcp_ps_256);
    return CALL(pFunc, std::initializer_list<Value*>{a});
}

Value* VMINPS(Value* a, Value* b)
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx_min_ps_256);
    return CALL(pFunc, std::initializer_list<Value*>{a, b});
}

Value* VMAXPS(Value* a, Value* b)
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx_max_ps_256);
    return CALL(pFunc, std::initializer_list<Value*>{a, b});
}

Value* VROUND(Value* a, Value* rounding)
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx_round_ps_256);
    return CALL(pFunc, std::initializer_list<Value*>{a, rounding});
}

Value* VCMPPS(Value* a, Value* b, Value* cmpop)
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx_cmp_ps_256);
    return CALL(pFunc, std::initializer_list<Value*>{a, b, cmpop});
}

Value* VBLENDVPS(Value* a, Value* b, Value* mask)
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx_blendv_ps_256);
    return CALL(pFunc, std::initializer_list<Value*>{a, b, mask});
}

Value* BEXTR_32(Value* src, Value* control)
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_bmi_bextr_32);
    return CALL(pFunc, std::initializer_list<Value*>{src, control});
}

Value* VMASKLOADD(Value* src, Value* mask)
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx2_maskload_d_256);
    return CALL(pFunc, std::initializer_list<Value*>{src, mask});
}

Value* VMASKMOVPS(Value* src, Value* mask)
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx_maskload_ps_256);
    return CALL(pFunc, std::initializer_list<Value*>{src, mask});
}

Value* VMASKSTOREPS(Value* src, Value* mask, Value* val)
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx_maskstore_ps_256);
    return CALL(pFunc, std::initializer_list<Value*>{src, mask, val});
}

Value* VPSHUFB(Value* a, Value* b)
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx2_pshuf_b);
    return CALL(pFunc, std::initializer_list<Value*>{a, b});
}

Value* VPERMD(Value* a, Value* idx)
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx2_permd);
    return CALL(pFunc, std::initializer_list<Value*>{a, idx});
}

Value* VPERMPS(Value* idx, Value* a)
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx2_permps);
    return CALL(pFunc, std::initializer_list<Value*>{idx, a});
}

Value* VCVTPD2PS(Value* a)
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx_cvt_pd2_ps_256);
    return CALL(pFunc, std::initializer_list<Value*>{a});
}

Value* VCVTPH2PS(Value* a)
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_vcvtph2ps_256);
    return CALL(pFunc, std::initializer_list<Value*>{a});
}

Value* VCVTPS2PH(Value* a, Value* round)
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_vcvtps2ph_256);
    return CALL(pFunc, std::initializer_list<Value*>{a, round});
}

Value* VHSUBPS(Value* a, Value* b)
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx_hsub_ps_256);
    return CALL(pFunc, std::initializer_list<Value*>{a, b});
}

Value* VPTESTC(Value* a, Value* b)
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx_ptestc_256);
    return CALL(pFunc, std::initializer_list<Value*>{a, b});
}

Value* VPTESTZ(Value* a, Value* b)
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx_ptestz_256);
    return CALL(pFunc, std::initializer_list<Value*>{a, b});
}

Value* VFMADDPS(Value* a, Value* b, Value* c)
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_fma_vfmadd_ps_256);
    return CALL(pFunc, std::initializer_list<Value*>{a, b, c});
}

Value* VMOVMSKPS(Value* a)
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx_movmsk_ps_256);
    return CALL(pFunc, std::initializer_list<Value*>{a});
}

Value* INTERRUPT(Value* a)
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_int);
    return CALL(pFunc, std::initializer_list<Value*>{a});
}

