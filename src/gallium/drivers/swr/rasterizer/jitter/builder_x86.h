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
* @file builder_x86.h
* 
* @brief auto-generated file
* 
* DO NOT EDIT
* 
******************************************************************************/

#pragma once

//////////////////////////////////////////////////////////////////////////
/// Auto-generated x86 intrinsics
//////////////////////////////////////////////////////////////////////////
Value *VGATHERPD(Value* src, Value* pBase, Value* indices, Value* mask, Value* scale);
Value *VGATHERPS(Value* src, Value* pBase, Value* indices, Value* mask, Value* scale);
Value *VGATHERDD(Value* src, Value* pBase, Value* indices, Value* mask, Value* scale);
Value *VSQRTPS(Value* a);
Value *VRSQRTPS(Value* a);
Value *VRCPPS(Value* a);
Value *VMINPS(Value* a, Value* b);
Value *VMAXPS(Value* a, Value* b);
Value *VROUND(Value* a, Value* rounding);
Value *VCMPPS(Value* a, Value* b, Value* cmpop);
Value *VBLENDVPS(Value* a, Value* b, Value* mask);
Value *BEXTR_32(Value* src, Value* control);
Value *VMASKLOADD(Value* src, Value* mask);
Value *VMASKMOVPS(Value* src, Value* mask);
Value *VMASKSTOREPS(Value* src, Value* mask, Value* val);
Value *VPSHUFB(Value* a, Value* b);
Value *VPERMD(Value* a, Value* idx);
Value *VPERMPS(Value* idx, Value* a);
Value *VCVTPD2PS(Value* a);
Value *VCVTPH2PS(Value* a);
Value *VCVTPS2PH(Value* a, Value* round);
Value *VHSUBPS(Value* a, Value* b);
Value *VPTESTC(Value* a, Value* b);
Value *VPTESTZ(Value* a, Value* b);
Value *VFMADDPS(Value* a, Value* b, Value* c);
Value *VMOVMSKPS(Value* a);
Value *INTERRUPT(Value* a);
