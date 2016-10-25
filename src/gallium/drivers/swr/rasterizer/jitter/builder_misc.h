/****************************************************************************
* Copyright (C) 2014-2015 Intel Corporation.   All Rights Reserved.
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
* @file builder_misc.h
* 
* @brief miscellaneous builder functions
* 
* Notes:
* 
******************************************************************************/
#pragma once

Constant *C(bool i);
Constant *C(char i);
Constant *C(uint8_t i);
Constant *C(int i);
Constant *C(int64_t i);
Constant *C(uint16_t i);
Constant *C(uint32_t i);
Constant *C(float i);

template<typename Ty>
Constant *C(const std::initializer_list<Ty> &constList)
{
    std::vector<Constant*> vConsts;
    for(auto i : constList) {

        vConsts.push_back(C((Ty)i));
    }
    return ConstantVector::get(vConsts);
}

Constant *PRED(bool pred);
Value *VIMMED1(int i);
Value *VIMMED1(uint32_t i);
Value *VIMMED1(float i);
Value *VIMMED1(bool i);
Value *VUNDEF(Type* t);
Value *VUNDEF_F();
Value *VUNDEF_I();
Value *VUNDEF(Type* ty, uint32_t size);
Value *VUNDEF_IPTR();
#if HAVE_LLVM == 0x306
Value *VINSERT(Value *vec, Value *val, uint64_t index);
#endif
Value *VBROADCAST(Value *src);
Value *VRCP(Value *va);
Value *VPLANEPS(Value* vA, Value* vB, Value* vC, Value* &vX, Value* &vY);

uint32_t IMMED(Value* i);
int32_t S_IMMED(Value* i);

Value *GEP(Value* ptr, const std::initializer_list<Value*> &indexList);
Value *GEP(Value* ptr, const std::initializer_list<uint32_t> &indexList);
CallInst *CALL(Value *Callee, const std::initializer_list<Value*> &args);
#if HAVE_LLVM > 0x306
CallInst *CALL(Value *Callee) { return CALLA(Callee); }
CallInst *CALL(Value *Callee, Value* arg);
CallInst *CALL2(Value *Callee, Value* arg1, Value* arg2);
CallInst *CALL3(Value *Callee, Value* arg1, Value* arg2, Value* arg3);
#endif

LoadInst *LOAD(Value *BasePtr, const std::initializer_list<uint32_t> &offset, const llvm::Twine& name = "");
LoadInst *LOADV(Value *BasePtr, const std::initializer_list<Value*> &offset, const llvm::Twine& name = "");
StoreInst *STORE(Value *Val, Value *BasePtr, const std::initializer_list<uint32_t> &offset);
StoreInst *STOREV(Value *Val, Value *BasePtr, const std::initializer_list<Value*> &offset);

Value *VCMPPS_EQ(Value* a, Value* b)    { return VCMPPS(a, b, C((uint8_t)_CMP_EQ_OQ)); }
Value *VCMPPS_LT(Value* a, Value* b)    { return VCMPPS(a, b, C((uint8_t)_CMP_LT_OQ)); }
Value *VCMPPS_LE(Value* a, Value* b)    { return VCMPPS(a, b, C((uint8_t)_CMP_LE_OQ)); }
Value *VCMPPS_ISNAN(Value* a, Value* b) { return VCMPPS(a, b, C((uint8_t)_CMP_UNORD_Q)); }
Value *VCMPPS_NEQ(Value* a, Value* b)   { return VCMPPS(a, b, C((uint8_t)_CMP_NEQ_OQ)); }
Value *VCMPPS_GE(Value* a, Value* b)    { return VCMPPS(a, b, C((uint8_t)_CMP_GE_OQ)); }
Value *VCMPPS_GT(Value* a, Value* b)    { return VCMPPS(a, b, C((uint8_t)_CMP_GT_OQ)); }
Value *VCMPPS_NOTNAN(Value* a, Value* b){ return VCMPPS(a, b, C((uint8_t)_CMP_ORD_Q)); }

Value *MASK(Value* vmask);
Value *VMASK(Value* mask);

//////////////////////////////////////////////////////////////////////////
/// @brief functions that build IR to call x86 intrinsics directly, or
/// emulate them with other instructions if not available on the host
//////////////////////////////////////////////////////////////////////////
Value *MASKLOADD(Value* src, Value* mask);

void Gather4(const SWR_FORMAT format, Value* pSrcBase, Value* byteOffsets,
                      Value* mask, Value* vGatherComponents[], bool bPackedOutput);

Value *GATHERPS(Value* src, Value* pBase, Value* indices, Value* mask, Value* scale);
void GATHER4PS(const SWR_FORMAT_INFO &info, Value* pSrcBase, Value* byteOffsets,
               Value* mask, Value* vGatherComponents[], bool bPackedOutput);

Value *GATHERDD(Value* src, Value* pBase, Value* indices, Value* mask, Value* scale);
void GATHER4DD(const SWR_FORMAT_INFO &info, Value* pSrcBase, Value* byteOffsets,
               Value* mask, Value* vGatherComponents[], bool bPackedOutput);

void SCATTERPS(Value* pDst, Value* vSrc, Value* vOffsets, Value* vMask);

void Shuffle8bpcGather4(const SWR_FORMAT_INFO &info, Value* vGatherInput, Value* vGatherOutput[], bool bPackedOutput);
void Shuffle16bpcGather4(const SWR_FORMAT_INFO &info, Value* vGatherInput[], Value* vGatherOutput[], bool bPackedOutput);

Value *PSHUFB(Value* a, Value* b);
Value *PMOVSXBD(Value* a);
Value *PMOVSXWD(Value* a);
Value *PERMD(Value* a, Value* idx);
Value *PERMPS(Value* a, Value* idx);
Value *CVTPH2PS(Value* a);
Value *CVTPS2PH(Value* a, Value* rounding);
Value *PMAXSD(Value* a, Value* b);
Value *PMINSD(Value* a, Value* b);
Value *VABSPS(Value* a);
Value *FMADDPS(Value* a, Value* b, Value* c);

// LLVM removed VPCMPGTD x86 intrinsic.  This emulates that behavior
Value *VPCMPGTD(Value* a, Value* b)
{
    Value* vIndexMask = ICMP_UGT(a,b);

    // need to set the high bit for x86 intrinsic masks
    return S_EXT(vIndexMask,VectorType::get(mInt32Ty,JM()->mVWidth));
}

Value *ICLAMP(Value* src, Value* low, Value* high);
Value *FCLAMP(Value* src, Value* low, Value* high);
Value *FCLAMP(Value* src, float low, float high);

CallInst *PRINT(const std::string &printStr);
CallInst *PRINT(const std::string &printStr,const std::initializer_list<Value*> &printArgs);
Value* STACKSAVE();
void STACKRESTORE(Value* pSaved);

Value* POPCNT(Value* a);

Value* INT3() { return INTERRUPT(C((uint8_t)3)); }


Value *VEXTRACTI128(Value* a, Constant* imm8);
Value *VINSERTI128(Value* a, Value* b, Constant* imm8);

// rdtsc buckets macros
void RDTSC_START(Value* pBucketMgr, Value* pId);
void RDTSC_STOP(Value* pBucketMgr, Value* pId);

Value* CreateEntryAlloca(Function* pFunc, Type* pType);

// Static stack allocations for scatter operations
Value* pScatterStackSrc{ nullptr };
Value* pScatterStackOffsets{ nullptr };

