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
* @file fetch_jit.h
*
* @brief Definition of the fetch jitter
*
* Notes:
*
******************************************************************************/
#pragma once

#include "common/formats.h"
#include "core/state.h"

//////////////////////////////////////////////////////////////////////////
/// INPUT_ELEMENT_DESC
//////////////////////////////////////////////////////////////////////////
struct INPUT_ELEMENT_DESC
{
    union
    {
        struct
        {
            uint32_t            AlignedByteOffset : 12;
            uint32_t            Format : 10;
            uint32_t            StreamIndex : 6;
            uint32_t            InstanceEnable : 1;
            uint32_t            ComponentControl0 : 3;
            uint32_t            ComponentControl1 : 3;
            uint32_t            ComponentControl2 : 3;
            uint32_t            ComponentControl3 : 3;
            uint32_t            ComponentPacking : 4;
            uint32_t            _reserved : 19;
        };
        uint64_t bits;
    };
    uint32_t InstanceDataStepRate;
};

// used to set ComponentPacking
enum ComponentEnable
{
    NONE = 0x0,
    X    = 0x1,
    Y    = 0x2,
    XY   = 0x3,
    Z    = 0x4,
    XZ   = 0x5,
    YZ   = 0x6,
    XYZ  = 0x7,
    W    = 0x8,
    XW   = 0x9,
    YW   = 0xA,
    XYW  = 0xB,
    ZW   = 0xC,
    XZW  = 0xD,
    YZW  = 0xE,
    XYZW = 0xF,
};

enum ComponentControl
{
    NoStore     = 0,
    StoreSrc    = 1,
    Store0      = 2,
    Store1Fp    = 3,
    Store1Int   = 4,
};

//////////////////////////////////////////////////////////////////////////
/// State required for fetch shader jit compile.
//////////////////////////////////////////////////////////////////////////
struct FETCH_COMPILE_STATE
{
    uint32_t numAttribs;
    INPUT_ELEMENT_DESC layout[KNOB_NUM_ATTRIBUTES];
    SWR_FORMAT indexType;
    uint32_t cutIndex{ 0xffffffff };

    bool                InstanceIdEnable;
    uint32_t            InstanceIdElementOffset;
    uint32_t            InstanceIdComponentNumber;
    bool                VertexIdEnable;
    uint32_t            VertexIdElementOffset;
    uint32_t            VertexIdComponentNumber;

    // Options that effect the JIT'd code
    bool bDisableVGATHER;           // if enabled, FetchJit will generate loads/shuffles instead of VGATHERs
    bool bDisableIndexOOBCheck;     // if enabled, FetchJit will exclude index OOB check
    bool bEnableCutIndex{ false };  // compares indices with the cut index and returns a cut mask

    FETCH_COMPILE_STATE(bool disableVGATHER = false, bool diableIndexOOBCheck = false):
        bDisableVGATHER(disableVGATHER), bDisableIndexOOBCheck(diableIndexOOBCheck){ };

    bool operator==(const FETCH_COMPILE_STATE &other) const
    {
        if (numAttribs != other.numAttribs) return false;
        if (indexType != other.indexType) return false;
        if (bDisableVGATHER != other.bDisableVGATHER) return false;
        if (bDisableIndexOOBCheck != other.bDisableIndexOOBCheck) return false;
        if (bEnableCutIndex != other.bEnableCutIndex) return false;
        if (cutIndex != other.cutIndex) return false;

        if (InstanceIdEnable != other.InstanceIdEnable) return false;
        if (InstanceIdEnable)
        {
            if (InstanceIdComponentNumber != other.InstanceIdComponentNumber) return false;
            if (InstanceIdElementOffset != other.InstanceIdElementOffset) return false;
        }
        if (VertexIdEnable != other.VertexIdEnable) return false;
        if (VertexIdEnable)
        {
            if (VertexIdComponentNumber != other.VertexIdComponentNumber) return false;
            if (VertexIdElementOffset != other.VertexIdElementOffset) return false;
        }

        for(uint32_t i = 0; i < numAttribs; ++i)
        {
            if((layout[i].bits != other.layout[i].bits) ||
               ((layout[i].InstanceEnable == 1) &&
                (layout[i].InstanceDataStepRate != other.layout[i].InstanceDataStepRate))){
                return false;
            }
        }

        return true;
    }
};
