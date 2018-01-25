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

Value* VGATHERPD(Value* src, Value* pBase, Value* indices, Value* mask, Value* scale, const llvm::Twine& name = "")
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx2_gather_d_pd_256);
    return CALL(pFunc, std::initializer_list<Value*>{src, pBase, indices, mask, scale}, name);
}

Value* VGATHERPS(Value* src, Value* pBase, Value* indices, Value* mask, Value* scale, const llvm::Twine& name = "")
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx2_gather_d_ps_256);
    return CALL(pFunc, std::initializer_list<Value*>{src, pBase, indices, mask, scale}, name);
}

Value* VGATHERPS_16(Value* src, Value* pBase, Value* indices, Value* mask, Value* scale, const llvm::Twine& name = "")
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx512_gather_dps_512);
    return CALL(pFunc, std::initializer_list<Value*>{src, pBase, indices, mask, scale}, name);
}

Value* VGATHERDD(Value* src, Value* pBase, Value* indices, Value* mask, Value* scale, const llvm::Twine& name = "")
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx2_gather_d_d_256);
    return CALL(pFunc, std::initializer_list<Value*>{src, pBase, indices, mask, scale}, name);
}

Value* VGATHERDD_16(Value* src, Value* pBase, Value* indices, Value* mask, Value* scale, const llvm::Twine& name = "")
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx512_gather_dpi_512);
    return CALL(pFunc, std::initializer_list<Value*>{src, pBase, indices, mask, scale}, name);
}

Value* VSQRTPS(Value* a, const llvm::Twine& name = "")
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx_sqrt_ps_256);
    return CALL(pFunc, std::initializer_list<Value*>{a}, name);
}

Value* VRSQRTPS(Value* a, const llvm::Twine& name = "")
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx_rsqrt_ps_256);
    return CALL(pFunc, std::initializer_list<Value*>{a}, name);
}

Value* VRCPPS(Value* a, const llvm::Twine& name = "")
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx_rcp_ps_256);
    return CALL(pFunc, std::initializer_list<Value*>{a}, name);
}

Value* VMINPS(Value* a, Value* b, const llvm::Twine& name = "")
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx_min_ps_256);
    return CALL(pFunc, std::initializer_list<Value*>{a, b}, name);
}

Value* VMAXPS(Value* a, Value* b, const llvm::Twine& name = "")
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx_max_ps_256);
    return CALL(pFunc, std::initializer_list<Value*>{a, b}, name);
}

Value* VROUND(Value* a, Value* rounding, const llvm::Twine& name = "")
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx_round_ps_256);
    return CALL(pFunc, std::initializer_list<Value*>{a, rounding}, name);
}

Value* VCMPPS(Value* a, Value* b, Value* cmpop, const llvm::Twine& name = "")
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx_cmp_ps_256);
    return CALL(pFunc, std::initializer_list<Value*>{a, b, cmpop}, name);
}

Value* VBLENDVPS(Value* a, Value* b, Value* mask, const llvm::Twine& name = "")
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx_blendv_ps_256);
    return CALL(pFunc, std::initializer_list<Value*>{a, b, mask}, name);
}

Value* BEXTR_32(Value* src, Value* control, const llvm::Twine& name = "")
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_bmi_bextr_32);
    return CALL(pFunc, std::initializer_list<Value*>{src, control}, name);
}

Value* VMASKLOADD(Value* src, Value* mask, const llvm::Twine& name = "")
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx2_maskload_d_256);
    return CALL(pFunc, std::initializer_list<Value*>{src, mask}, name);
}

Value* VMASKMOVPS(Value* src, Value* mask, const llvm::Twine& name = "")
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx_maskload_ps_256);
    return CALL(pFunc, std::initializer_list<Value*>{src, mask}, name);
}

Value* VMASKSTOREPS(Value* src, Value* mask, Value* val, const llvm::Twine& name = "")
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx_maskstore_ps_256);
    return CALL(pFunc, std::initializer_list<Value*>{src, mask, val}, name);
}

Value* VPSHUFB(Value* a, Value* b, const llvm::Twine& name = "")
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx2_pshuf_b);
    return CALL(pFunc, std::initializer_list<Value*>{a, b}, name);
}

Value* VPERMD(Value* a, Value* idx, const llvm::Twine& name = "")
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx2_permd);
    return CALL(pFunc, std::initializer_list<Value*>{a, idx}, name);
}

Value* VPERMPS(Value* idx, Value* a, const llvm::Twine& name = "")
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx2_permps);
    return CALL(pFunc, std::initializer_list<Value*>{idx, a}, name);
}

Value* VCVTPD2PS(Value* a, const llvm::Twine& name = "")
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx_cvt_pd2_ps_256);
    return CALL(pFunc, std::initializer_list<Value*>{a}, name);
}

Value* VCVTPH2PS(Value* a, const llvm::Twine& name = "")
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_vcvtph2ps_256);
    return CALL(pFunc, std::initializer_list<Value*>{a}, name);
}

Value* VCVTPS2PH(Value* a, Value* round, const llvm::Twine& name = "")
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_vcvtps2ph_256);
    return CALL(pFunc, std::initializer_list<Value*>{a, round}, name);
}

Value* VHSUBPS(Value* a, Value* b, const llvm::Twine& name = "")
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx_hsub_ps_256);
    return CALL(pFunc, std::initializer_list<Value*>{a, b}, name);
}

Value* VPTESTC(Value* a, Value* b, const llvm::Twine& name = "")
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx_ptestc_256);
    return CALL(pFunc, std::initializer_list<Value*>{a, b}, name);
}

Value* VPTESTZ(Value* a, Value* b, const llvm::Twine& name = "")
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx_ptestz_256);
    return CALL(pFunc, std::initializer_list<Value*>{a, b}, name);
}

Value* VFMADDPS(Value* a, Value* b, Value* c, const llvm::Twine& name = "")
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_fma_vfmadd_ps_256);
    return CALL(pFunc, std::initializer_list<Value*>{a, b, c}, name);
}

Value* VMOVMSKPS(Value* a, const llvm::Twine& name = "")
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_avx_movmsk_ps_256);
    return CALL(pFunc, std::initializer_list<Value*>{a}, name);
}

Value* INTERRUPT(Value* a, const llvm::Twine& name = "")
{
    Function *pFunc = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::x86_int);
    return CALL(pFunc, std::initializer_list<Value*>{a}, name);
}

