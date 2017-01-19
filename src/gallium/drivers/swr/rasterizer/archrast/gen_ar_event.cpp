/****************************************************************************
* Copyright (C) 2016 Intel Corporation.   All Rights Reserved.
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
* @file gen_ar_event.cpp
*
* @brief Implementation for events.  auto-generated file
* 
* DO NOT EDIT
*
******************************************************************************/
#include "common/os.h"
#include "gen_ar_event.h"
#include "gen_ar_eventhandler.h"

using namespace ArchRast;

void Start::Accept(EventHandler* pHandler)
{
    pHandler->Handle(*this);
}

void End::Accept(EventHandler* pHandler)
{
    pHandler->Handle(*this);
}

void ThreadStartApiEvent::Accept(EventHandler* pHandler)
{
    pHandler->Handle(*this);
}

void ThreadStartWorkerEvent::Accept(EventHandler* pHandler)
{
    pHandler->Handle(*this);
}

void DrawInstancedEvent::Accept(EventHandler* pHandler)
{
    pHandler->Handle(*this);
}

void DrawIndexedInstancedEvent::Accept(EventHandler* pHandler)
{
    pHandler->Handle(*this);
}

void DispatchEvent::Accept(EventHandler* pHandler)
{
    pHandler->Handle(*this);
}

void FrameEndEvent::Accept(EventHandler* pHandler)
{
    pHandler->Handle(*this);
}

void FrontendStatsEvent::Accept(EventHandler* pHandler)
{
    pHandler->Handle(*this);
}

void BackendStatsEvent::Accept(EventHandler* pHandler)
{
    pHandler->Handle(*this);
}

void EarlyDepthStencilInfoSingleSample::Accept(EventHandler* pHandler)
{
    pHandler->Handle(*this);
}

void EarlyDepthStencilInfoSampleRate::Accept(EventHandler* pHandler)
{
    pHandler->Handle(*this);
}

void EarlyDepthStencilInfoNullPS::Accept(EventHandler* pHandler)
{
    pHandler->Handle(*this);
}

void LateDepthStencilInfoSingleSample::Accept(EventHandler* pHandler)
{
    pHandler->Handle(*this);
}

void LateDepthStencilInfoSampleRate::Accept(EventHandler* pHandler)
{
    pHandler->Handle(*this);
}

void LateDepthStencilInfoNullPS::Accept(EventHandler* pHandler)
{
    pHandler->Handle(*this);
}

void EarlyDepthInfoPixelRate::Accept(EventHandler* pHandler)
{
    pHandler->Handle(*this);
}

void LateDepthInfoPixelRate::Accept(EventHandler* pHandler)
{
    pHandler->Handle(*this);
}

void BackendDrawEndEvent::Accept(EventHandler* pHandler)
{
    pHandler->Handle(*this);
}

void FrontendDrawEndEvent::Accept(EventHandler* pHandler)
{
    pHandler->Handle(*this);
}

void EarlyZSingleSample::Accept(EventHandler* pHandler)
{
    pHandler->Handle(*this);
}

void LateZSingleSample::Accept(EventHandler* pHandler)
{
    pHandler->Handle(*this);
}

void EarlyStencilSingleSample::Accept(EventHandler* pHandler)
{
    pHandler->Handle(*this);
}

void LateStencilSingleSample::Accept(EventHandler* pHandler)
{
    pHandler->Handle(*this);
}

void EarlyZSampleRate::Accept(EventHandler* pHandler)
{
    pHandler->Handle(*this);
}

void LateZSampleRate::Accept(EventHandler* pHandler)
{
    pHandler->Handle(*this);
}

void EarlyStencilSampleRate::Accept(EventHandler* pHandler)
{
    pHandler->Handle(*this);
}

void LateStencilSampleRate::Accept(EventHandler* pHandler)
{
    pHandler->Handle(*this);
}

void EarlyZNullPS::Accept(EventHandler* pHandler)
{
    pHandler->Handle(*this);
}

void EarlyStencilNullPS::Accept(EventHandler* pHandler)
{
    pHandler->Handle(*this);
}

void EarlyZPixelRate::Accept(EventHandler* pHandler)
{
    pHandler->Handle(*this);
}

void LateZPixelRate::Accept(EventHandler* pHandler)
{
    pHandler->Handle(*this);
}

void EarlyOmZ::Accept(EventHandler* pHandler)
{
    pHandler->Handle(*this);
}

void EarlyOmStencil::Accept(EventHandler* pHandler)
{
    pHandler->Handle(*this);
}

void LateOmZ::Accept(EventHandler* pHandler)
{
    pHandler->Handle(*this);
}

void LateOmStencil::Accept(EventHandler* pHandler)
{
    pHandler->Handle(*this);
}

void GSPrimInfo::Accept(EventHandler* pHandler)
{
    pHandler->Handle(*this);
}

void GSInputPrims::Accept(EventHandler* pHandler)
{
    pHandler->Handle(*this);
}

void GSPrimsGen::Accept(EventHandler* pHandler)
{
    pHandler->Handle(*this);
}

void GSVertsInput::Accept(EventHandler* pHandler)
{
    pHandler->Handle(*this);
}

void ClipVertexCount::Accept(EventHandler* pHandler)
{
    pHandler->Handle(*this);
}

void FlushVertClip::Accept(EventHandler* pHandler)
{
    pHandler->Handle(*this);
}

void VertsClipped::Accept(EventHandler* pHandler)
{
    pHandler->Handle(*this);
}

void TessPrimCount::Accept(EventHandler* pHandler)
{
    pHandler->Handle(*this);
}

void TessPrimFlush::Accept(EventHandler* pHandler)
{
    pHandler->Handle(*this);
}

void TessPrims::Accept(EventHandler* pHandler)
{
    pHandler->Handle(*this);
}
