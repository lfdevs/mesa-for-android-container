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
* @file fetch_jit.cpp
*
* @brief Implementation of the fetch jitter
*
* Notes:
*
******************************************************************************/
#include "jit_api.h"
#include "fetch_jit.h"
#include "builder.h"
#include "state_llvm.h"
#include <sstream>
#include <tuple>

//#define FETCH_DUMP_VERTEX 1
using namespace llvm;
using namespace SwrJit;

bool isComponentEnabled(ComponentEnable enableMask, uint8_t component);

enum ConversionType
{
    CONVERT_NONE,
    CONVERT_NORMALIZED,
    CONVERT_USCALED,
    CONVERT_SSCALED,
    CONVERT_SFIXED,
};

//////////////////////////////////////////////////////////////////////////
/// Interface to Jitting a fetch shader
//////////////////////////////////////////////////////////////////////////
struct FetchJit : public Builder
{
    FetchJit(JitManager* pJitMgr) : Builder(pJitMgr){};

    Function* Create(const FETCH_COMPILE_STATE& fetchState);
    Value* GetSimdValid32bitIndices(Value* vIndices, Value* pLastIndex);
    Value* GetSimdValid16bitIndices(Value* vIndices, Value* pLastIndex);
    Value* GetSimdValid8bitIndices(Value* vIndices, Value* pLastIndex);

    // package up Shuffle*bpcGatherd args into a tuple for convenience
    typedef std::tuple<Value*&, Value*, const Instruction::CastOps, const ConversionType,
        uint32_t&, uint32_t&, const ComponentEnable, const ComponentControl(&)[4], Value*(&)[4],
        const uint32_t(&)[4]> Shuffle8bpcArgs;
    void Shuffle8bpcGatherd(Shuffle8bpcArgs &args);

    typedef std::tuple<Value*(&)[2], Value*, const Instruction::CastOps, const ConversionType,
        uint32_t&, uint32_t&, const ComponentEnable, const ComponentControl(&)[4], Value*(&)[4]> Shuffle16bpcArgs;
    void Shuffle16bpcGather(Shuffle16bpcArgs &args);

    void StoreVertexElements(Value* pVtxOut, const uint32_t outputElt, const uint32_t numEltsToStore, Value* (&vVertexElements)[4]);

    Value* GenerateCompCtrlVector(const ComponentControl ctrl);

    void JitLoadVertices(const FETCH_COMPILE_STATE &fetchState, Value* streams, Value* vIndices, Value* pVtxOut);
    void JitGatherVertices(const FETCH_COMPILE_STATE &fetchState, Value* streams, Value* vIndices, Value* pVtxOut);

    bool IsOddFormat(SWR_FORMAT format);
    bool IsUniformFormat(SWR_FORMAT format);
    void UnpackComponents(SWR_FORMAT format, Value* vInput, Value* result[4]);
    void CreateGatherOddFormats(SWR_FORMAT format, Value* pMask, Value* pBase, Value* offsets, Value* result[4]);
    void ConvertFormat(SWR_FORMAT format, Value *texels[4]);

    Value* mpFetchInfo;
};

Function* FetchJit::Create(const FETCH_COMPILE_STATE& fetchState)
{
    static std::size_t fetchNum = 0;

    std::stringstream fnName("FetchShader", std::ios_base::in | std::ios_base::out | std::ios_base::ate);
    fnName << fetchNum++;

    Function*    fetch = Function::Create(JM()->mFetchShaderTy, GlobalValue::ExternalLinkage, fnName.str(), JM()->mpCurrentModule);
    BasicBlock*    entry = BasicBlock::Create(JM()->mContext, "entry", fetch);

    IRB()->SetInsertPoint(entry);

    auto    argitr = fetch->arg_begin();

    // Fetch shader arguments
    mpFetchInfo = &*argitr; ++argitr;
    mpFetchInfo->setName("fetchInfo");
    Value*    pVtxOut = &*argitr;
    pVtxOut->setName("vtxOutput");
    // this is just shorthand to tell LLVM to get a pointer to the base address of simdvertex
    // index 0(just the pointer to the simdvertex structure
    // index 1(which element of the simdvertex structure to offset to(in this case 0)
    // so the indices being i32's doesn't matter
    // TODO: generated this GEP with a VECTOR structure type so this makes sense
    std::vector<Value*>    vtxInputIndices(2, C(0));
    // GEP
    pVtxOut = GEP(pVtxOut, C(0));
    pVtxOut = BITCAST(pVtxOut, PointerType::get(VectorType::get(mFP32Ty, mVWidth), 0));

    // SWR_FETCH_CONTEXT::pStreams
    Value*    streams = LOAD(mpFetchInfo,{0, SWR_FETCH_CONTEXT_pStreams});
    streams->setName("pStreams");

    // SWR_FETCH_CONTEXT::pIndices
    Value*    indices = LOAD(mpFetchInfo,{0, SWR_FETCH_CONTEXT_pIndices});
    indices->setName("pIndices");

    // SWR_FETCH_CONTEXT::pLastIndex
    Value*    pLastIndex = LOAD(mpFetchInfo,{0, SWR_FETCH_CONTEXT_pLastIndex});
    pLastIndex->setName("pLastIndex");
    

    Value* vIndices;
    switch(fetchState.indexType)
    {
        case R8_UINT:
            indices = BITCAST(indices, Type::getInt8PtrTy(JM()->mContext, 0));
            if(fetchState.bDisableIndexOOBCheck){
                vIndices = LOAD(BITCAST(indices, PointerType::get(VectorType::get(mInt8Ty, mpJitMgr->mVWidth), 0)), {(uint32_t)0});
                vIndices = Z_EXT(vIndices, mSimdInt32Ty);
            }
            else{
                pLastIndex = BITCAST(pLastIndex, Type::getInt8PtrTy(JM()->mContext, 0));
                vIndices = GetSimdValid8bitIndices(indices, pLastIndex);
            }
            break;
        case R16_UINT: 
            indices = BITCAST(indices, Type::getInt16PtrTy(JM()->mContext, 0)); 
            if(fetchState.bDisableIndexOOBCheck){
                vIndices = LOAD(BITCAST(indices, PointerType::get(VectorType::get(mInt16Ty, mpJitMgr->mVWidth), 0)), {(uint32_t)0});
                vIndices = Z_EXT(vIndices, mSimdInt32Ty);
            }
            else{
                pLastIndex = BITCAST(pLastIndex, Type::getInt16PtrTy(JM()->mContext, 0));
                vIndices = GetSimdValid16bitIndices(indices, pLastIndex);
            }
            break;
        case R32_UINT:
            (fetchState.bDisableIndexOOBCheck) ? vIndices = LOAD(BITCAST(indices, PointerType::get(mSimdInt32Ty,0)),{(uint32_t)0})
                                               : vIndices = GetSimdValid32bitIndices(indices, pLastIndex);
            break; // incoming type is already 32bit int
        default: SWR_ASSERT(0, "Unsupported index type"); vIndices = nullptr; break;
    }

    Value* vVertexId = vIndices;
    if (fetchState.bVertexIDOffsetEnable)
    {
        // Assuming one of baseVertex or startVertex is 0, so adding both should be functionally correct
        Value* vBaseVertex = VBROADCAST(LOAD(mpFetchInfo, { 0, SWR_FETCH_CONTEXT_BaseVertex }));
        Value* vStartVertex = VBROADCAST(LOAD(mpFetchInfo, { 0, SWR_FETCH_CONTEXT_StartVertex }));
        vVertexId = ADD(vIndices, vBaseVertex);
        vVertexId = ADD(vVertexId, vStartVertex);
    }

    // store out vertex IDs
    STORE(vVertexId, GEP(mpFetchInfo, { 0, SWR_FETCH_CONTEXT_VertexID }));

    // store out cut mask if enabled
    if (fetchState.bEnableCutIndex)
    {
        Value* vCutIndex = VIMMED1(fetchState.cutIndex);
        Value* cutMask = VMASK(ICMP_EQ(vIndices, vCutIndex));
        STORE(cutMask, GEP(mpFetchInfo, { 0, SWR_FETCH_CONTEXT_CutMask }));
    }

    // Fetch attributes from memory and output to a simdvertex struct
    // since VGATHER has a perf penalty on HSW vs BDW, allow client to choose which fetch method to use
    (fetchState.bDisableVGATHER) ? JitLoadVertices(fetchState, streams, vIndices, pVtxOut)
                                 : JitGatherVertices(fetchState, streams, vIndices, pVtxOut);

    RET_VOID();

    JitManager::DumpToFile(fetch, "src");

#if defined(_DEBUG)
    verifyFunction(*fetch);
#endif

    ::FunctionPassManager setupPasses(JM()->mpCurrentModule);

    ///@todo We don't need the CFG passes for fetch. (e.g. BreakCriticalEdges and CFGSimplification)
    setupPasses.add(createBreakCriticalEdgesPass());
    setupPasses.add(createCFGSimplificationPass());
    setupPasses.add(createEarlyCSEPass());
    setupPasses.add(createPromoteMemoryToRegisterPass());

    setupPasses.run(*fetch);

    JitManager::DumpToFile(fetch, "se");

    ::FunctionPassManager optPasses(JM()->mpCurrentModule);

    ///@todo Haven't touched these either. Need to remove some of these and add others.
    optPasses.add(createCFGSimplificationPass());
    optPasses.add(createEarlyCSEPass());
    optPasses.add(createInstructionCombiningPass());
    optPasses.add(createInstructionSimplifierPass());
    optPasses.add(createConstantPropagationPass());
    optPasses.add(createSCCPPass());
    optPasses.add(createAggressiveDCEPass());

    optPasses.run(*fetch);
    optPasses.run(*fetch);

    JitManager::DumpToFile(fetch, "opt");

    return fetch;
}

//////////////////////////////////////////////////////////////////////////
/// @brief Loads attributes from memory using LOADs, shuffling the 
/// components into SOA form. 
/// *Note* currently does not support component control,
/// component packing, instancing
/// @param fetchState - info about attributes to be fetched from memory
/// @param streams - value pointer to the current vertex stream
/// @param vIndices - vector value of indices to load
/// @param pVtxOut - value pointer to output simdvertex struct
void FetchJit::JitLoadVertices(const FETCH_COMPILE_STATE &fetchState, Value* streams, Value* vIndices, Value* pVtxOut)
{
    // Zack shuffles; a variant of the Charleston.

    std::vector<Value*> vectors(16);
    std::vector<Constant*>    pMask(mVWidth);
    for(uint32_t i = 0; i < mVWidth; ++i)
    {
        pMask[i] = (C(i < 4 ? i : 4));
    }
    Constant* promoteMask = ConstantVector::get(pMask);
    Constant* uwvec = UndefValue::get(VectorType::get(mFP32Ty, 4));

    Value* startVertex = LOAD(mpFetchInfo, {0, SWR_FETCH_CONTEXT_StartVertex});
    Value* startInstance = LOAD(mpFetchInfo, {0, SWR_FETCH_CONTEXT_StartInstance});
    Value* curInstance = LOAD(mpFetchInfo, {0, SWR_FETCH_CONTEXT_CurInstance});
    Value* vBaseVertex = VBROADCAST(LOAD(mpFetchInfo, {0, SWR_FETCH_CONTEXT_BaseVertex}));
    curInstance->setName("curInstance");

    for(uint32_t nelt = 0; nelt < fetchState.numAttribs; ++nelt)
    {
        Value*    elements[4] = {0};
        const INPUT_ELEMENT_DESC& ied = fetchState.layout[nelt];
        const SWR_FORMAT_INFO &info = GetFormatInfo((SWR_FORMAT)ied.Format);
        SWR_ASSERT((info.bpp != 0), "Unsupported format in JitLoadVertices.");
        uint32_t    numComponents = info.numComps;
        uint32_t bpc = info.bpp / info.numComps;  ///@todo Code below assumes all components are same size. Need to fix.

        // load path doesn't support component packing
        SWR_ASSERT(ied.ComponentPacking == ComponentEnable::XYZW, "Fetch load path doesn't support component packing.");

        vectors.clear();

        Value *vCurIndices;
        Value *startOffset;
        if(ied.InstanceEnable)
        {
            Value* stepRate = C(ied.InstanceDataStepRate);

            // prevent a div by 0 for 0 step rate
            Value* isNonZeroStep = ICMP_UGT(stepRate, C(0));
            stepRate = SELECT(isNonZeroStep, stepRate, C(1));

            // calc the current offset into instanced data buffer
            Value* calcInstance = UDIV(curInstance, stepRate);

            // if step rate is 0, every instance gets instance 0
            calcInstance = SELECT(isNonZeroStep, calcInstance, C(0));

            vCurIndices = VBROADCAST(calcInstance);

            startOffset = startInstance;
        }
        else
        {
            // offset indices by baseVertex
            vCurIndices = ADD(vIndices, vBaseVertex);

            startOffset = startVertex;
        }

        // load SWR_VERTEX_BUFFER_STATE::pData
        Value *stream = LOAD(streams, {ied.StreamIndex, SWR_VERTEX_BUFFER_STATE_pData});

        // load SWR_VERTEX_BUFFER_STATE::pitch
        Value *stride = LOAD(streams, {ied.StreamIndex, SWR_VERTEX_BUFFER_STATE_pitch});
        stride = Z_EXT(stride, mInt64Ty);

        // load SWR_VERTEX_BUFFER_STATE::size
        Value *size = LOAD(streams, {ied.StreamIndex, SWR_VERTEX_BUFFER_STATE_size});
        size = Z_EXT(size, mInt64Ty);

        Value* startVertexOffset = MUL(Z_EXT(startOffset, mInt64Ty), stride);

        // Load from the stream.
        for(uint32_t lane = 0; lane < mVWidth; ++lane)
        {
            // Get index
            Value* index = VEXTRACT(vCurIndices, C(lane));
            index = Z_EXT(index, mInt64Ty);

            Value*    offset = MUL(index, stride);
            offset = ADD(offset, C((int64_t)ied.AlignedByteOffset));
            offset = ADD(offset, startVertexOffset);

            if (!fetchState.bDisableIndexOOBCheck) {
                // check for out of bound access, including partial OOB, and mask them to 0
                Value *endOffset = ADD(offset, C((int64_t)info.Bpp));
                Value *oob = ICMP_ULE(endOffset, size);
                offset = SELECT(oob, offset, ConstantInt::get(mInt64Ty, 0));
            }

            Value*    pointer = GEP(stream, offset);
            // We use a full-lane, but don't actually care.
            Value*    vptr = 0;

            // get a pointer to a 4 component attrib in default address space
            switch(bpc)
            {
                case 8: vptr = BITCAST(pointer, PointerType::get(VectorType::get(mInt8Ty, 4), 0)); break;
                case 16: vptr = BITCAST(pointer, PointerType::get(VectorType::get(mInt16Ty, 4), 0)); break;
                case 32: vptr = BITCAST(pointer, PointerType::get(VectorType::get(mFP32Ty, 4), 0)); break;
                default: SWR_ASSERT(false, "Unsupported underlying bpp!");
            }

            // load 4 components of attribute
            Value*    vec = ALIGNED_LOAD(vptr, 1, false);

            // Convert To FP32 internally
            switch(info.type[0])
            {
                case SWR_TYPE_UNORM:
                    switch(bpc)
                    {
                        case 8:
                            vec = UI_TO_FP(vec, VectorType::get(mFP32Ty, 4));
                            vec = FMUL(vec, ConstantVector::get(std::vector<Constant*>(4, ConstantFP::get(mFP32Ty, 1.0 / 255.0))));
                            break;
                        case 16:
                            vec = UI_TO_FP(vec, VectorType::get(mFP32Ty, 4));
                            vec = FMUL(vec, ConstantVector::get(std::vector<Constant*>(4, ConstantFP::get(mFP32Ty, 1.0 / 65535.0))));
                            break;
                        default:
                            SWR_ASSERT(false, "Unsupported underlying type!");
                            break;
                    }
                    break;
                case SWR_TYPE_SNORM:
                    switch(bpc)
                    {
                        case 8:
                            vec = SI_TO_FP(vec, VectorType::get(mFP32Ty, 4));
                            vec = FMUL(vec, ConstantVector::get(std::vector<Constant*>(4, ConstantFP::get(mFP32Ty, 1.0 / 128.0))));
                            break;
                        case 16:
                            vec = SI_TO_FP(vec, VectorType::get(mFP32Ty, 4));
                            vec = FMUL(vec, ConstantVector::get(std::vector<Constant*>(4, ConstantFP::get(mFP32Ty, 1.0 / 32768.0))));
                            break;
                        default:
                            SWR_ASSERT(false, "Unsupported underlying type!");
                            break;
                    }
                    break;
                case SWR_TYPE_UINT:
                    // Zero extend uint32_t types.
                    switch(bpc)
                    {
                        case 8:
                        case 16:
                            vec = Z_EXT(vec, VectorType::get(mInt32Ty, 4));
                            vec = BITCAST(vec, VectorType::get(mFP32Ty, 4));
                            break;
                        case 32:
                            break; // Pass through unchanged.
                        default:
                            SWR_ASSERT(false, "Unsupported underlying type!");
                            break;
                    }
                    break;
                case SWR_TYPE_SINT:
                    // Sign extend SINT types.
                    switch(bpc)
                    {
                        case 8:
                        case 16:
                            vec = S_EXT(vec, VectorType::get(mInt32Ty, 4));
                            vec = BITCAST(vec, VectorType::get(mFP32Ty, 4));
                            break;
                        case 32:
                            break; // Pass through unchanged.
                        default:
                            SWR_ASSERT(false, "Unsupported underlying type!");
                            break;
                    }
                    break;
                case SWR_TYPE_FLOAT:
                    switch(bpc)
                    {
                        case 32:
                            break; // Pass through unchanged.
                        default:
                            SWR_ASSERT(false, "Unsupported underlying type!");
                    }
                    break;
                case SWR_TYPE_USCALED:
                    vec = UI_TO_FP(vec, VectorType::get(mFP32Ty, 4));
                    break;
                case SWR_TYPE_SSCALED:
                    vec = SI_TO_FP(vec, VectorType::get(mFP32Ty, 4));
                    break;
                case SWR_TYPE_SFIXED:
                    vec = FMUL(SI_TO_FP(vec, VectorType::get(mFP32Ty, 4)), VBROADCAST(C(1/65536.0f)));
                    break;
                case SWR_TYPE_UNKNOWN:
                case SWR_TYPE_UNUSED:
                    SWR_ASSERT(false, "Unsupported type %d!", info.type[0]);
            }

            // promote mask: sse(0,1,2,3) | avx(0,1,2,3,4,4,4,4)
            // uwvec: 4 x F32, undef value
            Value*    wvec = VSHUFFLE(vec, uwvec, promoteMask);
            vectors.push_back(wvec);
        }

        std::vector<Constant*>        v01Mask(mVWidth);
        std::vector<Constant*>        v23Mask(mVWidth);
        std::vector<Constant*>        v02Mask(mVWidth);
        std::vector<Constant*>        v13Mask(mVWidth);

        // Concatenate the vectors together.
        elements[0] = VUNDEF_F(); 
        elements[1] = VUNDEF_F(); 
        elements[2] = VUNDEF_F(); 
        elements[3] = VUNDEF_F(); 
        for(uint32_t b = 0, num4Wide = mVWidth / 4; b < num4Wide; ++b)
        {
            v01Mask[4 * b + 0] = C(0 + 4 * b);
            v01Mask[4 * b + 1] = C(1 + 4 * b);
            v01Mask[4 * b + 2] = C(0 + 4 * b + mVWidth);
            v01Mask[4 * b + 3] = C(1 + 4 * b + mVWidth);

            v23Mask[4 * b + 0] = C(2 + 4 * b);
            v23Mask[4 * b + 1] = C(3 + 4 * b);
            v23Mask[4 * b + 2] = C(2 + 4 * b + mVWidth);
            v23Mask[4 * b + 3] = C(3 + 4 * b + mVWidth);

            v02Mask[4 * b + 0] = C(0 + 4 * b);
            v02Mask[4 * b + 1] = C(2 + 4 * b);
            v02Mask[4 * b + 2] = C(0 + 4 * b + mVWidth);
            v02Mask[4 * b + 3] = C(2 + 4 * b + mVWidth);

            v13Mask[4 * b + 0] = C(1 + 4 * b);
            v13Mask[4 * b + 1] = C(3 + 4 * b);
            v13Mask[4 * b + 2] = C(1 + 4 * b + mVWidth);
            v13Mask[4 * b + 3] = C(3 + 4 * b + mVWidth);

            std::vector<Constant*>    iMask(mVWidth);
            for(uint32_t i = 0; i < mVWidth; ++i)
            {
                if(((4 * b) <= i) && (i < (4 * (b + 1))))
                {
                    iMask[i] = C(i % 4 + mVWidth);
                }
                else
                {
                    iMask[i] = C(i);
                }
            }
            Constant* insertMask = ConstantVector::get(iMask);
            elements[0] = VSHUFFLE(elements[0], vectors[4 * b + 0], insertMask);
            elements[1] = VSHUFFLE(elements[1], vectors[4 * b + 1], insertMask);
            elements[2] = VSHUFFLE(elements[2], vectors[4 * b + 2], insertMask);
            elements[3] = VSHUFFLE(elements[3], vectors[4 * b + 3], insertMask);
        }

        Value* x0y0x1y1 = VSHUFFLE(elements[0], elements[1], ConstantVector::get(v01Mask));
        Value* x2y2x3y3 = VSHUFFLE(elements[2], elements[3], ConstantVector::get(v01Mask));
        Value* z0w0z1w1 = VSHUFFLE(elements[0], elements[1], ConstantVector::get(v23Mask));
        Value* z2w3z2w3 = VSHUFFLE(elements[2], elements[3], ConstantVector::get(v23Mask));
        elements[0] = VSHUFFLE(x0y0x1y1, x2y2x3y3, ConstantVector::get(v02Mask));
        elements[1] = VSHUFFLE(x0y0x1y1, x2y2x3y3, ConstantVector::get(v13Mask));
        elements[2] = VSHUFFLE(z0w0z1w1, z2w3z2w3, ConstantVector::get(v02Mask));
        elements[3] = VSHUFFLE(z0w0z1w1, z2w3z2w3, ConstantVector::get(v13Mask));

        switch(numComponents + 1)
        {
            case    1: elements[0] = VIMMED1(0.0f);
            case    2: elements[1] = VIMMED1(0.0f);
            case    3: elements[2] = VIMMED1(0.0f);
            case    4: elements[3] = VIMMED1(1.0f);
        }

        for(uint32_t c = 0; c < 4; ++c)
        {
            Value* dest = GEP(pVtxOut, C(nelt * 4 + c), "destGEP");
            STORE(elements[c], dest);
        }
    }
}

// returns true for odd formats that require special state.gather handling
bool FetchJit::IsOddFormat(SWR_FORMAT format)
{
    const SWR_FORMAT_INFO& info = GetFormatInfo(format);
    if (info.bpc[0] != 8 && info.bpc[0] != 16 && info.bpc[0] != 32 && info.bpc[0] != 64)
    {
        return true;
    }
    return false;
}

// format is uniform if all components are the same size and type
bool FetchJit::IsUniformFormat(SWR_FORMAT format)
{
    const SWR_FORMAT_INFO& info = GetFormatInfo(format);
    uint32_t bpc0 = info.bpc[0];
    uint32_t type0 = info.type[0];

    for (uint32_t c = 1; c < info.numComps; ++c)
    {
        if (bpc0 != info.bpc[c] || type0 != info.type[c])
        {
            return false;
        }
    }
    return true;
}

// unpacks components based on format
// foreach component in the pixel
//   mask off everything but this component
//   shift component to LSB
void FetchJit::UnpackComponents(SWR_FORMAT format, Value* vInput, Value* result[4])
{
    const SWR_FORMAT_INFO& info = GetFormatInfo(format);

    uint32_t bitOffset = 0;
    for (uint32_t c = 0; c < info.numComps; ++c)
    {
        uint32_t swizzledIndex = info.swizzle[c];
        uint32_t compBits = info.bpc[c];
        uint32_t bitmask = ((1 << compBits) - 1) << bitOffset;
        Value* comp = AND(vInput, bitmask);
        comp = LSHR(comp, bitOffset);

        result[swizzledIndex] = comp;
        bitOffset += compBits;
    }
}

// gather for odd component size formats
// gather SIMD full pixels per lane then shift/mask to move each component to their
// own vector
void FetchJit::CreateGatherOddFormats(SWR_FORMAT format, Value* pMask, Value* pBase, Value* offsets, Value* result[4])
{
    const SWR_FORMAT_INFO &info = GetFormatInfo(format);

    // only works if pixel size is <= 32bits
    SWR_ASSERT(info.bpp <= 32);

    Value* gather = VUNDEF_I();

    // assign defaults
    for (uint32_t comp = 0; comp < 4; ++comp)
    {
        result[comp] = VIMMED1((int)info.defaults[comp]);
    }

    // load the proper amount of data based on component size
    PointerType* pLoadTy = nullptr;
    switch (info.bpp)
    {
    case 8: pLoadTy = Type::getInt8PtrTy(JM()->mContext); break;
    case 16: pLoadTy = Type::getInt16PtrTy(JM()->mContext); break;
    case 24:
    case 32: pLoadTy = Type::getInt32PtrTy(JM()->mContext); break;
    default: SWR_ASSERT(0);
    }

    // allocate temporary memory for masked off lanes
    Value* pTmp = ALLOCA(pLoadTy->getElementType());

    // gather SIMD pixels
    for (uint32_t e = 0; e < JM()->mVWidth; ++e)
    {
        Value* pElemOffset = VEXTRACT(offsets, C(e));
        Value* pLoad = GEP(pBase, pElemOffset);
        Value* pLaneMask = VEXTRACT(pMask, C(e));

        pLoad = POINTER_CAST(pLoad, pLoadTy);

        // mask in tmp pointer for disabled lanes
        pLoad = SELECT(pLaneMask, pLoad, pTmp);

        // load pixel
        Value *val = LOAD(pLoad);

        // zero extend to 32bit integer
        val = INT_CAST(val, mInt32Ty, false);

        // store in simd lane
        gather = VINSERT(gather, val, C(e));
    }

    UnpackComponents(format, gather, result);

    // cast to fp32
    result[0] = BITCAST(result[0], mSimdFP32Ty);
    result[1] = BITCAST(result[1], mSimdFP32Ty);
    result[2] = BITCAST(result[2], mSimdFP32Ty);
    result[3] = BITCAST(result[3], mSimdFP32Ty);
}

void FetchJit::ConvertFormat(SWR_FORMAT format, Value *texels[4])
{
    const SWR_FORMAT_INFO &info = GetFormatInfo(format);

    for (uint32_t c = 0; c < info.numComps; ++c)
    {
        uint32_t compIndex = info.swizzle[c];

        // skip any conversion on UNUSED components
        if (info.type[c] == SWR_TYPE_UNUSED)
        {
            continue;
        }

        if (info.isNormalized[c])
        {
            if (info.type[c] == SWR_TYPE_SNORM)
            {
                /// @todo The most-negative value maps to -1.0f. e.g. the 5-bit value 10000 maps to -1.0f.

                /// result = c * (1.0f / (2^(n-1) - 1);
                uint32_t n = info.bpc[c];
                uint32_t pow2 = 1 << (n - 1);
                float scale = 1.0f / (float)(pow2 - 1);
                Value *vScale = VIMMED1(scale);
                texels[compIndex] = BITCAST(texels[compIndex], mSimdInt32Ty);
                texels[compIndex] = SI_TO_FP(texels[compIndex], mSimdFP32Ty);
                texels[compIndex] = FMUL(texels[compIndex], vScale);
            }
            else
            {
                SWR_ASSERT(info.type[c] == SWR_TYPE_UNORM);

                /// result = c * (1.0f / (2^n - 1))
                uint32_t n = info.bpc[c];
                uint32_t pow2 = 1 << n;
                // special case 24bit unorm format, which requires a full divide to meet ULP requirement
                if (n == 24)
                {
                    float scale = (float)(pow2 - 1);
                    Value* vScale = VIMMED1(scale);
                    texels[compIndex] = BITCAST(texels[compIndex], mSimdInt32Ty);
                    texels[compIndex] = SI_TO_FP(texels[compIndex], mSimdFP32Ty);
                    texels[compIndex] = FDIV(texels[compIndex], vScale);
                }
                else
                {
                    float scale = 1.0f / (float)(pow2 - 1);
                    Value *vScale = VIMMED1(scale);
                    texels[compIndex] = BITCAST(texels[compIndex], mSimdInt32Ty);
                    texels[compIndex] = UI_TO_FP(texels[compIndex], mSimdFP32Ty);
                    texels[compIndex] = FMUL(texels[compIndex], vScale);
                }
            }
            continue;
        }
    }
}

//////////////////////////////////////////////////////////////////////////
/// @brief Loads attributes from memory using AVX2 GATHER(s)
/// @param fetchState - info about attributes to be fetched from memory
/// @param streams - value pointer to the current vertex stream
/// @param vIndices - vector value of indices to gather
/// @param pVtxOut - value pointer to output simdvertex struct
void FetchJit::JitGatherVertices(const FETCH_COMPILE_STATE &fetchState,
                                 Value* streams, Value* vIndices, Value* pVtxOut)
{
    uint32_t currentVertexElement = 0;
    uint32_t outputElt = 0;
    Value* vVertexElements[4];

    Value* startVertex = LOAD(mpFetchInfo, {0, SWR_FETCH_CONTEXT_StartVertex});
    Value* startInstance = LOAD(mpFetchInfo, {0, SWR_FETCH_CONTEXT_StartInstance});
    Value* curInstance = LOAD(mpFetchInfo, {0, SWR_FETCH_CONTEXT_CurInstance});
    Value* vBaseVertex = VBROADCAST(LOAD(mpFetchInfo, {0, SWR_FETCH_CONTEXT_BaseVertex}));
    curInstance->setName("curInstance");

    for(uint32_t nInputElt = 0; nInputElt < fetchState.numAttribs; ++nInputElt)
    {
        const INPUT_ELEMENT_DESC& ied = fetchState.layout[nInputElt];

        // skip element if all components are disabled
        if (ied.ComponentPacking == ComponentEnable::NONE)
        {
            continue;
        }

        const SWR_FORMAT_INFO &info = GetFormatInfo((SWR_FORMAT)ied.Format);
        SWR_ASSERT((info.bpp != 0), "Unsupported format in JitGatherVertices.");
        uint32_t bpc = info.bpp / info.numComps;  ///@todo Code below assumes all components are same size. Need to fix.

        Value *stream = LOAD(streams, {ied.StreamIndex, SWR_VERTEX_BUFFER_STATE_pData});

        // VGATHER* takes an *i8 src pointer
        Value* pStreamBase = BITCAST(stream, PointerType::get(mInt8Ty, 0));

        Value *stride = LOAD(streams, {ied.StreamIndex, SWR_VERTEX_BUFFER_STATE_pitch});
        Value *vStride = VBROADCAST(stride);

        // max vertex index that is fully in bounds
        Value *maxVertex = GEP(streams, {C(ied.StreamIndex), C(SWR_VERTEX_BUFFER_STATE_maxVertex)});
        maxVertex = LOAD(maxVertex);

        Value *vCurIndices;
        Value *startOffset;
        if(ied.InstanceEnable)
        {
            Value* stepRate = C(ied.InstanceDataStepRate);

            // prevent a div by 0 for 0 step rate
            Value* isNonZeroStep = ICMP_UGT(stepRate, C(0));
            stepRate = SELECT(isNonZeroStep, stepRate, C(1));

            // calc the current offset into instanced data buffer
            Value* calcInstance = UDIV(curInstance, stepRate);

            // if step rate is 0, every instance gets instance 0
            calcInstance = SELECT(isNonZeroStep, calcInstance, C(0));

            vCurIndices = VBROADCAST(calcInstance);

            startOffset = startInstance;
        }
        else
        {
            // offset indices by baseVertex            
            vCurIndices = ADD(vIndices, vBaseVertex);

            startOffset = startVertex;
        }

        // All of the OOB calculations are in vertices, not VB offsets, to prevent having to 
        // do 64bit address offset calculations.

        // calculate byte offset to the start of the VB
        Value* baseOffset = MUL(Z_EXT(startOffset, mInt64Ty), Z_EXT(stride, mInt64Ty));
        pStreamBase = GEP(pStreamBase, baseOffset);

        // if we have a start offset, subtract from max vertex. Used for OOB check
        maxVertex = SUB(Z_EXT(maxVertex, mInt64Ty), Z_EXT(startOffset, mInt64Ty));
        Value* neg = ICMP_SLT(maxVertex, C((int64_t)0));
        // if we have a negative value, we're already OOB. clamp at 0.
        maxVertex = SELECT(neg, C(0), TRUNC(maxVertex, mInt32Ty));

        // Load the in bounds size of a partially valid vertex
        Value *partialInboundsSize = GEP(streams, {C(ied.StreamIndex), C(SWR_VERTEX_BUFFER_STATE_partialInboundsSize)});
        partialInboundsSize = LOAD(partialInboundsSize);
        Value* vPartialVertexSize = VBROADCAST(partialInboundsSize);
        Value* vBpp = VBROADCAST(C(info.Bpp));
        Value* vAlignmentOffsets = VBROADCAST(C(ied.AlignedByteOffset));

        // is the element is <= the partially valid size
        Value* vElementInBoundsMask = ICMP_SLE(vBpp, SUB(vPartialVertexSize, vAlignmentOffsets));

        // override cur indices with 0 if pitch is 0
        Value* pZeroPitchMask = ICMP_EQ(vStride, VIMMED1(0));
        vCurIndices = SELECT(pZeroPitchMask, VIMMED1(0), vCurIndices);

        // are vertices partially OOB?
        Value* vMaxVertex = VBROADCAST(maxVertex);
        Value* vPartialOOBMask = ICMP_EQ(vCurIndices, vMaxVertex);

        // are vertices are fully in bounds?
        Value* vGatherMask = ICMP_ULT(vCurIndices, vMaxVertex);

        // blend in any partially OOB indices that have valid elements
        vGatherMask = SELECT(vPartialOOBMask, vElementInBoundsMask, vGatherMask);
        Value* pMask = vGatherMask;
        vGatherMask = VMASK(vGatherMask);

        // calculate the actual offsets into the VB
        Value* vOffsets = MUL(vCurIndices, vStride);
        vOffsets = ADD(vOffsets, vAlignmentOffsets);

        // Packing and component control 
        ComponentEnable compMask = (ComponentEnable)ied.ComponentPacking;
        const ComponentControl compCtrl[4] { (ComponentControl)ied.ComponentControl0, (ComponentControl)ied.ComponentControl1, 
                                             (ComponentControl)ied.ComponentControl2, (ComponentControl)ied.ComponentControl3}; 

        // Special gather/conversion for formats without equal component sizes
        if (IsOddFormat((SWR_FORMAT)ied.Format))
        {
            Value* pResults[4];
            CreateGatherOddFormats((SWR_FORMAT)ied.Format, pMask, pStreamBase, vOffsets, pResults);
            ConvertFormat((SWR_FORMAT)ied.Format, pResults);

            for (uint32_t c = 0; c < 4; ++c)
            {
                if (isComponentEnabled(compMask, c))
                {
                    vVertexElements[currentVertexElement++] = pResults[c];
                    if (currentVertexElement > 3)
                    {
                        StoreVertexElements(pVtxOut, outputElt++, 4, vVertexElements);
                        // reset to the next vVertexElement to output
                        currentVertexElement = 0;
                    }
                }
            }
        }
        else if(info.type[0] == SWR_TYPE_FLOAT)
        {
            ///@todo: support 64 bit vb accesses
            Value* gatherSrc = VIMMED1(0.0f);

            SWR_ASSERT(IsUniformFormat((SWR_FORMAT)ied.Format), 
                "Unsupported format for standard gather fetch.");

            // Gather components from memory to store in a simdvertex structure
            switch(bpc)
            {
                case 16:
                {
                    Value* vGatherResult[2];
                    Value *vMask;

                    // if we have at least one component out of x or y to fetch
                    if(isComponentEnabled(compMask, 0) || isComponentEnabled(compMask, 1)){
                        // save mask as it is zero'd out after each gather
                        vMask = vGatherMask;

                        vGatherResult[0] = GATHERPS(gatherSrc, pStreamBase, vOffsets, vMask, C((char)1));
                        // e.g. result of first 8x32bit integer gather for 16bit components
                        // 256i - 0    1    2    3    4    5    6    7
                        //        xyxy xyxy xyxy xyxy xyxy xyxy xyxy xyxy
                        //
                    }

                    // if we have at least one component out of z or w to fetch
                    if(isComponentEnabled(compMask, 2) || isComponentEnabled(compMask, 3)){
                        // offset base to the next components(zw) in the vertex to gather
                        pStreamBase = GEP(pStreamBase, C((char)4));
                        vMask = vGatherMask;

                        vGatherResult[1] = GATHERPS(gatherSrc, pStreamBase, vOffsets, vMask, C((char)1));
                        // e.g. result of second 8x32bit integer gather for 16bit components
                        // 256i - 0    1    2    3    4    5    6    7
                        //        zwzw zwzw zwzw zwzw zwzw zwzw zwzw zwzw 
                        //
                    }

                    // if we have at least one component to shuffle into place
                    if(compMask){
                        Shuffle16bpcArgs args = std::forward_as_tuple(vGatherResult, pVtxOut, Instruction::CastOps::FPExt, CONVERT_NONE,
                            currentVertexElement, outputElt, compMask, compCtrl, vVertexElements);

                        // Shuffle gathered components into place in simdvertex struct
                        Shuffle16bpcGather(args);  // outputs to vVertexElements ref
                    }
                }
                    break;
                case 32:
                {
                    for (uint32_t i = 0; i < 4; i++)
                    {
                        if (isComponentEnabled(compMask, i))
                        {
                            // if we need to gather the component
                            if (compCtrl[i] == StoreSrc)
                            {
                                // save mask as it is zero'd out after each gather
                                Value *vMask = vGatherMask;

                                // Gather a SIMD of vertices
                                vVertexElements[currentVertexElement++] = GATHERPS(gatherSrc, pStreamBase, vOffsets, vMask, C((char)1));
                            }
                            else
                            {
                                vVertexElements[currentVertexElement++] = GenerateCompCtrlVector(compCtrl[i]);
                            }

                            if (currentVertexElement > 3)
                            {
                                StoreVertexElements(pVtxOut, outputElt++, 4, vVertexElements);
                                // reset to the next vVertexElement to output
                                currentVertexElement = 0;
                            }

                        }

                        // offset base to the next component in the vertex to gather
                        pStreamBase = GEP(pStreamBase, C((char)4));
                    }
                }
                    break;
                case 64:
                {
                    for (uint32_t i = 0; i < 4; i++)
                    {
                        if (isComponentEnabled(compMask, i))
                        {
                            // if we need to gather the component
                            if (compCtrl[i] == StoreSrc)
                            {
                                Value *vMaskLo = VSHUFFLE(pMask, VUNDEF(mInt1Ty, 8), C({0, 1, 2, 3}));
                                Value *vMaskHi = VSHUFFLE(pMask, VUNDEF(mInt1Ty, 8), C({4, 5, 6, 7}));
                                vMaskLo = S_EXT(vMaskLo, VectorType::get(mInt64Ty, 4));
                                vMaskHi = S_EXT(vMaskHi, VectorType::get(mInt64Ty, 4));
                                vMaskLo = BITCAST(vMaskLo, VectorType::get(mDoubleTy, 4));
                                vMaskHi = BITCAST(vMaskHi, VectorType::get(mDoubleTy, 4));

                                Value *vOffsetsLo = VEXTRACTI128(vOffsets, C(0));
                                Value *vOffsetsHi = VEXTRACTI128(vOffsets, C(1));

                                Value *vZeroDouble = VECTOR_SPLAT(4, ConstantFP::get(IRB()->getDoubleTy(), 0.0f));

                                Value* pGatherLo = GATHERPD(vZeroDouble,
                                                            pStreamBase, vOffsetsLo, vMaskLo, C((char)1));
                                Value* pGatherHi = GATHERPD(vZeroDouble,
                                                            pStreamBase, vOffsetsHi, vMaskHi, C((char)1));

                                pGatherLo = VCVTPD2PS(pGatherLo);
                                pGatherHi = VCVTPD2PS(pGatherHi);

                                Value *pGather = VSHUFFLE(pGatherLo, pGatherHi, C({0, 1, 2, 3, 4, 5, 6, 7}));

                                vVertexElements[currentVertexElement++] = pGather;
                            }
                            else
                            {
                                vVertexElements[currentVertexElement++] = GenerateCompCtrlVector(compCtrl[i]);
                            }

                            if (currentVertexElement > 3)
                            {
                                StoreVertexElements(pVtxOut, outputElt++, 4, vVertexElements);
                                // reset to the next vVertexElement to output
                                currentVertexElement = 0;
                            }

                        }

                        // offset base to the next component  in the vertex to gather
                        pStreamBase = GEP(pStreamBase, C((char)8));
                    }
                }
                    break;
                default:
                    SWR_ASSERT(0, "Tried to fetch invalid FP format");
                    break;
            }
        }
        else
        {
            Instruction::CastOps extendCastType = Instruction::CastOps::CastOpsEnd;
            ConversionType conversionType = CONVERT_NONE;

            SWR_ASSERT(IsUniformFormat((SWR_FORMAT)ied.Format), 
                "Unsupported format for standard gather fetch.");

            switch(info.type[0])
            {
                case SWR_TYPE_UNORM: 
                    conversionType = CONVERT_NORMALIZED;
                case SWR_TYPE_UINT:
                    extendCastType = Instruction::CastOps::ZExt;
                    break;
                case SWR_TYPE_SNORM:
                    conversionType = CONVERT_NORMALIZED;
                case SWR_TYPE_SINT:
                    extendCastType = Instruction::CastOps::SExt;
                    break;
                case SWR_TYPE_USCALED:
                    conversionType = CONVERT_USCALED;
                    extendCastType = Instruction::CastOps::UIToFP;
                    break;
                case SWR_TYPE_SSCALED:
                    conversionType = CONVERT_SSCALED;
                    extendCastType = Instruction::CastOps::SIToFP;
                    break;
                case SWR_TYPE_SFIXED:
                    conversionType = CONVERT_SFIXED;
                    extendCastType = Instruction::CastOps::SExt;
                    break;
                default:
                    break;
            }

            // value substituted when component of gather is masked
            Value* gatherSrc = VIMMED1(0);

            // Gather components from memory to store in a simdvertex structure
            switch (bpc)
            {
                case 8:
                {
                    // if we have at least one component to fetch
                    if(compMask)
                    {
                        Value* vGatherResult = GATHERDD(gatherSrc, pStreamBase, vOffsets, vGatherMask, C((char)1));
                        // e.g. result of an 8x32bit integer gather for 8bit components
                        // 256i - 0    1    2    3    4    5    6    7
                        //        xyzw xyzw xyzw xyzw xyzw xyzw xyzw xyzw 

                        Shuffle8bpcArgs args = std::forward_as_tuple(vGatherResult, pVtxOut, extendCastType, conversionType,
                            currentVertexElement, outputElt, compMask, compCtrl, vVertexElements, info.swizzle);

                        // Shuffle gathered components into place in simdvertex struct
                        Shuffle8bpcGatherd(args); // outputs to vVertexElements ref
                    }
                }
                break;
                case 16:
                {
                    Value* vGatherResult[2];
                    Value *vMask;

                    // if we have at least one component out of x or y to fetch
                    if(isComponentEnabled(compMask, 0) || isComponentEnabled(compMask, 1)){
                        // save mask as it is zero'd out after each gather
                        vMask = vGatherMask;

                        vGatherResult[0] = GATHERDD(gatherSrc, pStreamBase, vOffsets, vMask, C((char)1));
                        // e.g. result of first 8x32bit integer gather for 16bit components
                        // 256i - 0    1    2    3    4    5    6    7
                        //        xyxy xyxy xyxy xyxy xyxy xyxy xyxy xyxy
                        //
                    }

                    // if we have at least one component out of z or w to fetch
                    if(isComponentEnabled(compMask, 2) || isComponentEnabled(compMask, 3)){
                        // offset base to the next components(zw) in the vertex to gather
                        pStreamBase = GEP(pStreamBase, C((char)4));
                        vMask = vGatherMask;

                        vGatherResult[1] = GATHERDD(gatherSrc, pStreamBase, vOffsets, vMask, C((char)1));
                        // e.g. result of second 8x32bit integer gather for 16bit components
                        // 256i - 0    1    2    3    4    5    6    7
                        //        zwzw zwzw zwzw zwzw zwzw zwzw zwzw zwzw 
                        //
                    }

                    // if we have at least one component to shuffle into place
                    if(compMask){
                        Shuffle16bpcArgs args = std::forward_as_tuple(vGatherResult, pVtxOut, extendCastType, conversionType,
                            currentVertexElement, outputElt, compMask, compCtrl, vVertexElements);

                        // Shuffle gathered components into place in simdvertex struct
                        Shuffle16bpcGather(args);  // outputs to vVertexElements ref
                    }
                }
                break;
                case 32:
                {
                    // Gathered components into place in simdvertex struct
                    for (uint32_t i = 0; i < 4; i++)
                    {
                        if (isComponentEnabled(compMask, i))
                        {
                            // if we need to gather the component
                            if (compCtrl[i] == StoreSrc)
                            {
                                // save mask as it is zero'd out after each gather
                                Value *vMask = vGatherMask;

                                Value* pGather = GATHERDD(gatherSrc, pStreamBase, vOffsets, vMask, C((char)1));

                                if (conversionType == CONVERT_USCALED)
                                {
                                    pGather = UI_TO_FP(pGather, mSimdFP32Ty);
                                }
                                else if (conversionType == CONVERT_SSCALED)
                                {
                                    pGather = SI_TO_FP(pGather, mSimdFP32Ty);
                                }
                                else if (conversionType == CONVERT_SFIXED)
                                {
                                    pGather = FMUL(SI_TO_FP(pGather, mSimdFP32Ty), VBROADCAST(C(1/65536.0f)));
                                }

                                vVertexElements[currentVertexElement++] = pGather;
                                // e.g. result of a single 8x32bit integer gather for 32bit components
                                // 256i - 0    1    2    3    4    5    6    7
                                //        xxxx xxxx xxxx xxxx xxxx xxxx xxxx xxxx 
                            }
                            else
                            {
                                vVertexElements[currentVertexElement++] = GenerateCompCtrlVector(compCtrl[i]);
                            }

                            if (currentVertexElement > 3)
                            {
                                StoreVertexElements(pVtxOut, outputElt++, 4, vVertexElements);
                                // reset to the next vVertexElement to output
                                currentVertexElement = 0;
                            }

                        }

                        // offset base to the next component  in the vertex to gather
                        pStreamBase = GEP(pStreamBase, C((char)4));
                    }
                }
                break;
            }
        }
    }

    // if we have a partially filled vVertexElement struct, output it
    if(currentVertexElement > 0){
        StoreVertexElements(pVtxOut, outputElt++, currentVertexElement, vVertexElements);
    }
}

//////////////////////////////////////////////////////////////////////////
/// @brief Loads a simd of valid indices. OOB indices are set to 0
/// *Note* have to do 16bit index checking in scalar until we have AVX-512
/// support
/// @param pIndices - pointer to 8 bit indices
/// @param pLastIndex - pointer to last valid index
Value* FetchJit::GetSimdValid8bitIndices(Value* pIndices, Value* pLastIndex)
{
    // can fit 2 16 bit integers per vWidth lane
    Value* vIndices =  VUNDEF_I();

    // store 0 index on stack to be used to conditionally load from if index address is OOB
    Value* pZeroIndex = ALLOCA(mInt8Ty);
    STORE(C((uint8_t)0), pZeroIndex);

    // Load a SIMD of index pointers
    for(int64_t lane = 0; lane < mVWidth; lane++)
    {
        // Calculate the address of the requested index
        Value *pIndex = GEP(pIndices, C(lane));

        // check if the address is less than the max index, 
        Value* mask = ICMP_ULT(pIndex, pLastIndex);

        // if valid, load the index. if not, load 0 from the stack
        Value* pValid = SELECT(mask, pIndex, pZeroIndex);
        Value *index = LOAD(pValid, "valid index");

        // zero extended index to 32 bits and insert into the correct simd lane
        index = Z_EXT(index, mInt32Ty);
        vIndices = VINSERT(vIndices, index, lane);
    }
    return vIndices;
}

//////////////////////////////////////////////////////////////////////////
/// @brief Loads a simd of valid indices. OOB indices are set to 0
/// *Note* have to do 16bit index checking in scalar until we have AVX-512
/// support
/// @param pIndices - pointer to 16 bit indices
/// @param pLastIndex - pointer to last valid index
Value* FetchJit::GetSimdValid16bitIndices(Value* pIndices, Value* pLastIndex)
{
    // can fit 2 16 bit integers per vWidth lane
    Value* vIndices =  VUNDEF_I();

    // store 0 index on stack to be used to conditionally load from if index address is OOB
    Value* pZeroIndex = ALLOCA(mInt16Ty);
    STORE(C((uint16_t)0), pZeroIndex);

    // Load a SIMD of index pointers
    for(int64_t lane = 0; lane < mVWidth; lane++)
    {
        // Calculate the address of the requested index
        Value *pIndex = GEP(pIndices, C(lane));

        // check if the address is less than the max index, 
        Value* mask = ICMP_ULT(pIndex, pLastIndex);

        // if valid, load the index. if not, load 0 from the stack
        Value* pValid = SELECT(mask, pIndex, pZeroIndex);
        Value *index = LOAD(pValid, "valid index");

        // zero extended index to 32 bits and insert into the correct simd lane
        index = Z_EXT(index, mInt32Ty);
        vIndices = VINSERT(vIndices, index, lane);
    }
    return vIndices;
}

//////////////////////////////////////////////////////////////////////////
/// @brief Loads a simd of valid indices. OOB indices are set to 0
/// @param pIndices - pointer to 32 bit indices
/// @param pLastIndex - pointer to last valid index
Value* FetchJit::GetSimdValid32bitIndices(Value* pIndices, Value* pLastIndex)
{
    DataLayout dL(JM()->mpCurrentModule);
    unsigned int ptrSize = dL.getPointerSize() * 8;  // ptr size in bits
    Value* iLastIndex = PTR_TO_INT(pLastIndex, Type::getIntNTy(JM()->mContext, ptrSize));
    Value* iIndices = PTR_TO_INT(pIndices, Type::getIntNTy(JM()->mContext, ptrSize));

    // get the number of indices left in the buffer (endPtr - curPtr) / sizeof(index)
    Value* numIndicesLeft = SUB(iLastIndex,iIndices);
    numIndicesLeft = TRUNC(numIndicesLeft, mInt32Ty);
    numIndicesLeft = SDIV(numIndicesLeft, C(4));

    // create a vector of index counts from the base index ptr passed into the fetch
    const std::vector<Constant*> vecIndices {C(0), C(1), C(2), C(3), C(4), C(5), C(6), C(7)};
    Constant* vIndexOffsets = ConstantVector::get(vecIndices);

    // compare index count to the max valid index
    // e.g vMaxIndex      4 4 4 4 4 4 4 4 : 4 indices left to load
    //     vIndexOffsets  0 1 2 3 4 5 6 7
    //     ------------------------------
    //     vIndexMask    -1-1-1-1 0 0 0 0 : offsets < max pass
    //     vLoadedIndices 0 1 2 3 0 0 0 0 : offsets >= max masked to 0
    Value* vMaxIndex = VBROADCAST(numIndicesLeft);
    Value* vIndexMask = VPCMPGTD(vMaxIndex,vIndexOffsets);

    // VMASKLOAD takes an *i8 src pointer
    pIndices = BITCAST(pIndices,PointerType::get(mInt8Ty,0));

    // Load the indices; OOB loads 0
    return MASKLOADD(pIndices,vIndexMask);
}

//////////////////////////////////////////////////////////////////////////
/// @brief Takes a SIMD of gathered 8bpc verts, zero or sign extends, 
/// denormalizes if needed, converts to F32 if needed, and positions in 
//  the proper SIMD rows to be output to the simdvertex structure
/// @param args: (tuple of args, listed below)
///   @param vGatherResult - 8 gathered 8bpc vertices
///   @param pVtxOut - base pointer to output simdvertex struct
///   @param extendType - sign extend or zero extend
///   @param bNormalized - do we need to denormalize?
///   @param currentVertexElement - reference to the current vVertexElement
///   @param outputElt - reference to the current offset from simdvertex we're o
///   @param compMask - component packing mask
///   @param compCtrl - component control val
///   @param vVertexElements[4] - vertex components to output
///   @param swizzle[4] - component swizzle location
void FetchJit::Shuffle8bpcGatherd(Shuffle8bpcArgs &args)
{
    // Unpack tuple args
    Value*& vGatherResult = std::get<0>(args);
    Value* pVtxOut = std::get<1>(args);
    const Instruction::CastOps extendType = std::get<2>(args);
    const ConversionType conversionType = std::get<3>(args);
    uint32_t &currentVertexElement = std::get<4>(args);
    uint32_t &outputElt =  std::get<5>(args);
    const ComponentEnable compMask = std::get<6>(args);
    const ComponentControl (&compCtrl)[4] = std::get<7>(args);
    Value* (&vVertexElements)[4] = std::get<8>(args);
    const uint32_t (&swizzle)[4] = std::get<9>(args);

    // cast types
    Type* vGatherTy = mSimdInt32Ty;
    Type* v32x8Ty =  VectorType::get(mInt8Ty, mVWidth * 4 ); // vwidth is units of 32 bits

    // have to do extra work for sign extending
    if ((extendType == Instruction::CastOps::SExt) || (extendType == Instruction::CastOps::SIToFP)){
        Type* v16x8Ty = VectorType::get(mInt8Ty, mVWidth * 2); // 8x16bit ints in a 128bit lane
        Type* v128Ty = VectorType::get(IntegerType::getIntNTy(JM()->mContext, 128), mVWidth / 4); // vwidth is units of 32 bits

        // shuffle mask, including any swizzling
        const char x = (char)swizzle[0]; const char y = (char)swizzle[1];
        const char z = (char)swizzle[2]; const char w = (char)swizzle[3];
        Value* vConstMask = C<char>({char(x), char(x+4), char(x+8), char(x+12),
                    char(y), char(y+4), char(y+8), char(y+12),
                    char(z), char(z+4), char(z+8), char(z+12),
                    char(w), char(w+4), char(w+8), char(w+12),
                    char(x), char(x+4), char(x+8), char(x+12),
                    char(y), char(y+4), char(y+8), char(y+12),
                    char(z), char(z+4), char(z+8), char(z+12),
                    char(w), char(w+4), char(w+8), char(w+12)});

        Value* vShufResult = BITCAST(PSHUFB(BITCAST(vGatherResult, v32x8Ty), vConstMask), vGatherTy);
        // after pshufb: group components together in each 128bit lane
        // 256i - 0    1    2    3    4    5    6    7
        //        xxxx yyyy zzzz wwww xxxx yyyy zzzz wwww

        Value* vi128XY = nullptr;
        if(isComponentEnabled(compMask, 0) || isComponentEnabled(compMask, 1)){
            vi128XY = BITCAST(PERMD(vShufResult, C<int32_t>({0, 4, 0, 0, 1, 5, 0, 0})), v128Ty);
            // after PERMD: move and pack xy and zw components in low 64 bits of each 128bit lane
            // 256i - 0    1    2    3    4    5    6    7
            //        xxxx xxxx dcdc dcdc yyyy yyyy dcdc dcdc (dc - don't care)
        }

        // do the same for zw components
        Value* vi128ZW = nullptr;
        if(isComponentEnabled(compMask, 2) || isComponentEnabled(compMask, 3)){
            vi128ZW = BITCAST(PERMD(vShufResult, C<int32_t>({2, 6, 0, 0, 3, 7, 0, 0})), v128Ty);
        }

        // init denormalize variables if needed
        Instruction::CastOps fpCast;
        Value* conversionFactor;

        switch (conversionType)
        {
        case CONVERT_NORMALIZED:
            fpCast = Instruction::CastOps::SIToFP;
            conversionFactor = VIMMED1((float)(1.0 / 127.0));
            break;
        case CONVERT_SSCALED:
            fpCast = Instruction::CastOps::SIToFP;
            conversionFactor = VIMMED1((float)(1.0));
            break;
        case CONVERT_USCALED:
            SWR_ASSERT(0, "Type should not be sign extended!");
            conversionFactor = nullptr;
            break;
        default:
            SWR_ASSERT(conversionType == CONVERT_NONE);
            conversionFactor = nullptr;
            break;
        }

        // sign extend all enabled components. If we have a fill vVertexElements, output to current simdvertex
        for (uint32_t i = 0; i < 4; i++)
        {
            if (isComponentEnabled(compMask, i))
            {
                if (compCtrl[i] == ComponentControl::StoreSrc)
                {
                    // if x or z, extract 128bits from lane 0, else for y or w, extract from lane 1
                    uint32_t lane = ((i == 0) || (i == 2)) ? 0 : 1;
                    // if x or y, use vi128XY permute result, else use vi128ZW
                    Value* selectedPermute = (i < 2) ? vi128XY : vi128ZW;

                    // sign extend
                    vVertexElements[currentVertexElement] = PMOVSXBD(BITCAST(VEXTRACT(selectedPermute, C(lane)), v16x8Ty));

                    // denormalize if needed
                    if (conversionType != CONVERT_NONE)
                    {
                        vVertexElements[currentVertexElement] = FMUL(CAST(fpCast, vVertexElements[currentVertexElement], mSimdFP32Ty), conversionFactor);
                    }
                    currentVertexElement++;
                }
                else
                {
                    vVertexElements[currentVertexElement++] = GenerateCompCtrlVector(compCtrl[i]);
                }

                if (currentVertexElement > 3)
                {
                    StoreVertexElements(pVtxOut, outputElt++, 4, vVertexElements);
                    // reset to the next vVertexElement to output
                    currentVertexElement = 0;
                }
            }
        }
    }
    // else zero extend
    else if ((extendType == Instruction::CastOps::ZExt) || (extendType == Instruction::CastOps::UIToFP))
    {
        // init denormalize variables if needed
        Instruction::CastOps fpCast;
        Value* conversionFactor;

        switch (conversionType)
        {
        case CONVERT_NORMALIZED:
            fpCast = Instruction::CastOps::UIToFP;
            conversionFactor = VIMMED1((float)(1.0 / 255.0));
            break;
        case CONVERT_USCALED:
            fpCast = Instruction::CastOps::UIToFP;
            conversionFactor = VIMMED1((float)(1.0));
            break;
        case CONVERT_SSCALED:
            SWR_ASSERT(0, "Type should not be zero extended!");
            conversionFactor = nullptr;
            break;
        default:
            SWR_ASSERT(conversionType == CONVERT_NONE);
            conversionFactor = nullptr;
            break;
        }

        // shuffle enabled components into lower byte of each 32bit lane, 0 extending to 32 bits
        for (uint32_t i = 0; i < 4; i++)
        {
            if (isComponentEnabled(compMask, i))
            {
                if (compCtrl[i] == ComponentControl::StoreSrc)
                {
                    // pshufb masks for each component
                    Value* vConstMask;
                    switch (swizzle[i])
                    {
                    case 0:
                        // x shuffle mask
                        vConstMask = C<char>({ 0, -1, -1, -1, 4, -1, -1, -1, 8, -1, -1, -1, 12, -1, -1, -1,
                                               0, -1, -1, -1, 4, -1, -1, -1, 8, -1, -1, -1, 12, -1, -1, -1 });
                        break;
                    case 1:
                        // y shuffle mask
                        vConstMask = C<char>({ 1, -1, -1, -1, 5, -1, -1, -1, 9, -1, -1, -1, 13, -1, -1, -1,
                                               1, -1, -1, -1, 5, -1, -1, -1, 9, -1, -1, -1, 13, -1, -1, -1 });
                        break;
                    case 2:
                        // z shuffle mask
                        vConstMask = C<char>({ 2, -1, -1, -1, 6, -1, -1, -1, 10, -1, -1, -1, 14, -1, -1, -1,
                                               2, -1, -1, -1, 6, -1, -1, -1, 10, -1, -1, -1, 14, -1, -1, -1 });
                        break;
                    case 3:
                        // w shuffle mask
                        vConstMask = C<char>({ 3, -1, -1, -1, 7, -1, -1, -1, 11, -1, -1, -1, 15, -1, -1, -1,
                                               3, -1, -1, -1, 7, -1, -1, -1, 11, -1, -1, -1, 15, -1, -1, -1 });
                        break;
                    default:
                        vConstMask = nullptr;
                        break;
                    }

                    vVertexElements[currentVertexElement] = BITCAST(PSHUFB(BITCAST(vGatherResult, v32x8Ty), vConstMask), vGatherTy);
                    // after pshufb for x channel
                    // 256i - 0    1    2    3    4    5    6    7
                    //        x000 x000 x000 x000 x000 x000 x000 x000 

                    // denormalize if needed
                    if (conversionType != CONVERT_NONE)
                    {
                        vVertexElements[currentVertexElement] = FMUL(CAST(fpCast, vVertexElements[currentVertexElement], mSimdFP32Ty), conversionFactor);
                    }
                    currentVertexElement++;
                }
                else
                {
                    vVertexElements[currentVertexElement++] = GenerateCompCtrlVector(compCtrl[i]);
                }

                if (currentVertexElement > 3)
                {
                    StoreVertexElements(pVtxOut, outputElt++, 4, vVertexElements);
                    // reset to the next vVertexElement to output
                    currentVertexElement = 0;
                }
            }
        }
    }
    else
    {
        SWR_ASSERT(0, "Unsupported conversion type");
    }
}

//////////////////////////////////////////////////////////////////////////
/// @brief Takes a SIMD of gathered 16bpc verts, zero or sign extends, 
/// denormalizes if needed, converts to F32 if needed, and positions in 
//  the proper SIMD rows to be output to the simdvertex structure
/// @param args: (tuple of args, listed below)
///   @param vGatherResult[2] - array of gathered 16bpc vertices, 4 per index
///   @param pVtxOut - base pointer to output simdvertex struct
///   @param extendType - sign extend or zero extend
///   @param bNormalized - do we need to denormalize?
///   @param currentVertexElement - reference to the current vVertexElement
///   @param outputElt - reference to the current offset from simdvertex we're o
///   @param compMask - component packing mask
///   @param compCtrl - component control val
///   @param vVertexElements[4] - vertex components to output
void FetchJit::Shuffle16bpcGather(Shuffle16bpcArgs &args)
{
    // Unpack tuple args
    Value* (&vGatherResult)[2] = std::get<0>(args);
    Value* pVtxOut = std::get<1>(args);
    const Instruction::CastOps extendType = std::get<2>(args);
    const ConversionType conversionType = std::get<3>(args);
    uint32_t &currentVertexElement = std::get<4>(args);
    uint32_t &outputElt = std::get<5>(args);
    const ComponentEnable compMask = std::get<6>(args);
    const ComponentControl(&compCtrl)[4] = std::get<7>(args);
    Value* (&vVertexElements)[4] = std::get<8>(args);

    // cast types
    Type* vGatherTy = VectorType::get(IntegerType::getInt32Ty(JM()->mContext), mVWidth);
    Type* v32x8Ty = VectorType::get(mInt8Ty, mVWidth * 4); // vwidth is units of 32 bits

    // have to do extra work for sign extending
    if ((extendType == Instruction::CastOps::SExt) || (extendType == Instruction::CastOps::SIToFP)||
        (extendType == Instruction::CastOps::FPExt))
    {
        // is this PP float?
        bool bFP = (extendType == Instruction::CastOps::FPExt) ? true : false;

        Type* v8x16Ty = VectorType::get(mInt16Ty, 8); // 8x16bit in a 128bit lane
        Type* v128bitTy = VectorType::get(IntegerType::getIntNTy(JM()->mContext, 128), mVWidth / 4); // vwidth is units of 32 bits

        // shuffle mask
        Value* vConstMask = C<char>({0, 1, 4, 5, 8, 9, 12, 13, 2, 3, 6, 7, 10, 11, 14, 15,
                                     0, 1, 4, 5, 8, 9, 12, 13, 2, 3, 6, 7, 10, 11, 14, 15});
        Value* vi128XY = nullptr;
        if(isComponentEnabled(compMask, 0) || isComponentEnabled(compMask, 1)){
            Value* vShufResult = BITCAST(PSHUFB(BITCAST(vGatherResult[0], v32x8Ty), vConstMask), vGatherTy);
            // after pshufb: group components together in each 128bit lane
            // 256i - 0    1    2    3    4    5    6    7
            //        xxxx xxxx yyyy yyyy xxxx xxxx yyyy yyyy

            vi128XY = BITCAST(PERMD(vShufResult, C<int32_t>({0, 1, 4, 5, 2, 3, 6, 7})), v128bitTy);
            // after PERMD: move and pack xy components into each 128bit lane
            // 256i - 0    1    2    3    4    5    6    7
            //        xxxx xxxx xxxx xxxx yyyy yyyy yyyy yyyy
        }

        // do the same for zw components
        Value* vi128ZW = nullptr;
        if(isComponentEnabled(compMask, 2) || isComponentEnabled(compMask, 3)){
            Value* vShufResult = BITCAST(PSHUFB(BITCAST(vGatherResult[1], v32x8Ty), vConstMask), vGatherTy);
            vi128ZW = BITCAST(PERMD(vShufResult, C<int32_t>({0, 1, 4, 5, 2, 3, 6, 7})), v128bitTy);
        }

        // init denormalize variables if needed
        Instruction::CastOps IntToFpCast;
        Value* conversionFactor;

        switch (conversionType)
        {
        case CONVERT_NORMALIZED:
            IntToFpCast = Instruction::CastOps::SIToFP;
            conversionFactor = VIMMED1((float)(1.0 / 32767.0));
            break;
        case CONVERT_SSCALED:
            IntToFpCast = Instruction::CastOps::SIToFP;
            conversionFactor = VIMMED1((float)(1.0));
            break;
        case CONVERT_USCALED:
            SWR_ASSERT(0, "Type should not be sign extended!");
            conversionFactor = nullptr;
            break;
        default:
            SWR_ASSERT(conversionType == CONVERT_NONE);
            conversionFactor = nullptr;
            break;
        }

        // sign extend all enabled components. If we have a fill vVertexElements, output to current simdvertex
        for (uint32_t i = 0; i < 4; i++)
        {
            if (isComponentEnabled(compMask, i))
            {
                if (compCtrl[i] == ComponentControl::StoreSrc)
                {
                    // if x or z, extract 128bits from lane 0, else for y or w, extract from lane 1
                    uint32_t lane = ((i == 0) || (i == 2)) ? 0 : 1;
                    // if x or y, use vi128XY permute result, else use vi128ZW
                    Value* selectedPermute = (i < 2) ? vi128XY : vi128ZW;

                    if (bFP) {
                        // extract 128 bit lanes to sign extend each component
                        vVertexElements[currentVertexElement] = CVTPH2PS(BITCAST(VEXTRACT(selectedPermute, C(lane)), v8x16Ty));
                    }
                    else {
                        // extract 128 bit lanes to sign extend each component
                        vVertexElements[currentVertexElement] = PMOVSXWD(BITCAST(VEXTRACT(selectedPermute, C(lane)), v8x16Ty));

                        // denormalize if needed
                        if (conversionType != CONVERT_NONE) {
                            vVertexElements[currentVertexElement] = FMUL(CAST(IntToFpCast, vVertexElements[currentVertexElement], mSimdFP32Ty), conversionFactor);
                        }
                    }
                    currentVertexElement++;
                }
                else
                {
                    vVertexElements[currentVertexElement++] = GenerateCompCtrlVector(compCtrl[i]);
                }

                if (currentVertexElement > 3)
                {
                    StoreVertexElements(pVtxOut, outputElt++, 4, vVertexElements);
                    // reset to the next vVertexElement to output
                    currentVertexElement = 0;
                }
            }
        }
    }
    // else zero extend
    else if ((extendType == Instruction::CastOps::ZExt) || (extendType == Instruction::CastOps::UIToFP))
    {
        // pshufb masks for each component
        Value* vConstMask[2];
        if(isComponentEnabled(compMask, 0) || isComponentEnabled(compMask, 2)){
            // x/z shuffle mask
            vConstMask[0] = C<char>({0, 1, -1, -1, 4, 5, -1, -1, 8, 9, -1, -1, 12, 13, -1, -1,
                                     0, 1, -1, -1, 4, 5, -1, -1, 8, 9, -1, -1, 12, 13, -1, -1, });
        }
        
        if(isComponentEnabled(compMask, 1) || isComponentEnabled(compMask, 3)){
            // y/w shuffle mask
            vConstMask[1] = C<char>({2, 3, -1, -1, 6, 7, -1, -1, 10, 11, -1, -1, 14, 15, -1, -1,
                                     2, 3, -1, -1, 6, 7, -1, -1, 10, 11, -1, -1, 14, 15, -1, -1});
        }

        // init denormalize variables if needed
        Instruction::CastOps fpCast;
        Value* conversionFactor;

        switch (conversionType)
        {
        case CONVERT_NORMALIZED:
            fpCast = Instruction::CastOps::UIToFP;
            conversionFactor = VIMMED1((float)(1.0 / 65535.0));
            break;
        case CONVERT_USCALED:
            fpCast = Instruction::CastOps::UIToFP;
            conversionFactor = VIMMED1((float)(1.0f));
            break;
        case CONVERT_SSCALED:
            SWR_ASSERT(0, "Type should not be zero extended!");
            conversionFactor = nullptr;
            break;
        default:
            SWR_ASSERT(conversionType == CONVERT_NONE);
            conversionFactor = nullptr;
            break;
        }

        // shuffle enabled components into lower word of each 32bit lane, 0 extending to 32 bits
        for (uint32_t i = 0; i < 4; i++)
        {
            if (isComponentEnabled(compMask, i))
            {
                if (compCtrl[i] == ComponentControl::StoreSrc)
                {
                    // select correct constMask for x/z or y/w pshufb
                    uint32_t selectedMask = ((i == 0) || (i == 2)) ? 0 : 1;
                    // if x or y, use vi128XY permute result, else use vi128ZW
                    uint32_t selectedGather = (i < 2) ? 0 : 1;

                    vVertexElements[currentVertexElement] = BITCAST(PSHUFB(BITCAST(vGatherResult[selectedGather], v32x8Ty), vConstMask[selectedMask]), vGatherTy);
                    // after pshufb mask for x channel; z uses the same shuffle from the second gather
                    // 256i - 0    1    2    3    4    5    6    7
                    //        xx00 xx00 xx00 xx00 xx00 xx00 xx00 xx00 

                    // denormalize if needed
                    if (conversionType != CONVERT_NONE)
                    {
                        vVertexElements[currentVertexElement] = FMUL(CAST(fpCast, vVertexElements[currentVertexElement], mSimdFP32Ty), conversionFactor);
                    }
                    currentVertexElement++;
                }
                else
                {
                    vVertexElements[currentVertexElement++] = GenerateCompCtrlVector(compCtrl[i]);
                }

                if (currentVertexElement > 3)
                {
                    StoreVertexElements(pVtxOut, outputElt++, 4, vVertexElements);
                    // reset to the next vVertexElement to output
                    currentVertexElement = 0;
                }
            }
        }
    }
    else
    {
        SWR_ASSERT(0, "Unsupported conversion type");
    }
}

//////////////////////////////////////////////////////////////////////////
/// @brief Output a simdvertex worth of elements to the current outputElt
/// @param pVtxOut - base address of VIN output struct
/// @param outputElt - simdvertex offset in VIN to write to
/// @param numEltsToStore - number of simdvertex rows to write out
/// @param vVertexElements - LLVM Value*[] simdvertex to write out
void FetchJit::StoreVertexElements(Value* pVtxOut, const uint32_t outputElt, const uint32_t numEltsToStore, Value* (&vVertexElements)[4])
{
    SWR_ASSERT(numEltsToStore <= 4, "Invalid element count.");

    for(uint32_t c = 0; c < numEltsToStore; ++c)
    {
        // STORE expects FP32 x vWidth type, just bitcast if needed
        if(!vVertexElements[c]->getType()->getScalarType()->isFloatTy()){
#if FETCH_DUMP_VERTEX
            PRINT("vVertexElements[%d]: 0x%x\n", {C(c), vVertexElements[c]});
#endif
            vVertexElements[c] = BITCAST(vVertexElements[c], mSimdFP32Ty);
        }
#if FETCH_DUMP_VERTEX
        else
        {
            PRINT("vVertexElements[%d]: %f\n", {C(c), vVertexElements[c]});
        }
#endif
        // outputElt * 4 = offsetting by the size of a simdvertex
        // + c offsets to a 32bit x vWidth row within the current vertex
        Value* dest = GEP(pVtxOut, C(outputElt * 4 + c), "destGEP");
        STORE(vVertexElements[c], dest);
    }
}

//////////////////////////////////////////////////////////////////////////
/// @brief Generates a constant vector of values based on the 
/// ComponentControl value
/// @param ctrl - ComponentControl value
Value* FetchJit::GenerateCompCtrlVector(const ComponentControl ctrl)
{
    switch(ctrl)
    {
        case NoStore:   return VUNDEF_I();
        case Store0:    return VIMMED1(0);
        case Store1Fp:  return VIMMED1(1.0f);
        case Store1Int: return VIMMED1(1);
        case StoreVertexId:
        {
            Value* pId = BITCAST(LOAD(GEP(mpFetchInfo, { 0, SWR_FETCH_CONTEXT_VertexID })), mSimdFP32Ty);
            return VBROADCAST(pId);
        }
        case StoreInstanceId:
        {
            Value* pId = BITCAST(LOAD(GEP(mpFetchInfo, { 0, SWR_FETCH_CONTEXT_CurInstance })), mFP32Ty);
            return VBROADCAST(pId);
        }
        case StoreSrc:
        default:        SWR_ASSERT(0, "Invalid component control"); return VUNDEF_I();
    }
}

//////////////////////////////////////////////////////////////////////////
/// @brief Returns the enable mask for the specified component.
/// @param enableMask - enable bits
/// @param component - component to check if enabled.
bool isComponentEnabled(ComponentEnable enableMask, uint8_t component)
{
    switch (component)
    {
        // X
    case 0: return (enableMask & ComponentEnable::X);
        // Y
    case 1: return (enableMask & ComponentEnable::Y);
        // Z
    case 2: return (enableMask & ComponentEnable::Z);
        // W
    case 3: return (enableMask & ComponentEnable::W);

    default: return false;
    }
}


//////////////////////////////////////////////////////////////////////////
/// @brief JITs from fetch shader IR
/// @param hJitMgr - JitManager handle
/// @param func   - LLVM function IR
/// @return PFN_FETCH_FUNC - pointer to fetch code
PFN_FETCH_FUNC JitFetchFunc(HANDLE hJitMgr, const HANDLE hFunc)
{
    const llvm::Function* func = (const llvm::Function*)hFunc;
    JitManager* pJitMgr = reinterpret_cast<JitManager*>(hJitMgr);
    PFN_FETCH_FUNC pfnFetch;

    pfnFetch = (PFN_FETCH_FUNC)(pJitMgr->mpExec->getFunctionAddress(func->getName().str()));
    // MCJIT finalizes modules the first time you JIT code from them. After finalized, you cannot add new IR to the module
    pJitMgr->mIsModuleFinalized = true;

#if defined(KNOB_SWRC_TRACING)
    char fName[1024];
    const char *funcName = func->getName().data();
    sprintf(fName, "%s.bin", funcName);
    FILE *fd = fopen(fName, "wb");
    fwrite((void *)pfnFetch, 1, 2048, fd);
    fclose(fd);
#endif

    pJitMgr->DumpAsm(const_cast<llvm::Function*>(func), "final");

    return pfnFetch;
}

//////////////////////////////////////////////////////////////////////////
/// @brief JIT compiles fetch shader
/// @param hJitMgr - JitManager handle
/// @param state   - fetch state to build function from
extern "C" PFN_FETCH_FUNC JITCALL JitCompileFetch(HANDLE hJitMgr, const FETCH_COMPILE_STATE& state)
{
    JitManager* pJitMgr = reinterpret_cast<JitManager*>(hJitMgr);

    pJitMgr->SetupNewModule();

    FetchJit theJit(pJitMgr);
    HANDLE hFunc = theJit.Create(state);

    return JitFetchFunc(hJitMgr, hFunc);
}
