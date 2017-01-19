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
* @file gen_ar_eventhandler.h
*
* @brief Event handler interface.  auto-generated file
* 
* DO NOT EDIT
* 
******************************************************************************/
#pragma once

#include "gen_ar_event.h"

namespace ArchRast
{
    //////////////////////////////////////////////////////////////////////////
    /// EventHandler - interface for handling events.
    //////////////////////////////////////////////////////////////////////////
    class EventHandler
    {
    public:
        EventHandler() {}
        virtual ~EventHandler() {}

        virtual void Handle(Start event) {}
        virtual void Handle(End event) {}
        virtual void Handle(ThreadStartApiEvent event) {}
        virtual void Handle(ThreadStartWorkerEvent event) {}
        virtual void Handle(DrawInstancedEvent event) {}
        virtual void Handle(DrawIndexedInstancedEvent event) {}
        virtual void Handle(DispatchEvent event) {}
        virtual void Handle(FrameEndEvent event) {}
        virtual void Handle(FrontendStatsEvent event) {}
        virtual void Handle(BackendStatsEvent event) {}
        virtual void Handle(EarlyDepthStencilInfoSingleSample event) {}
        virtual void Handle(EarlyDepthStencilInfoSampleRate event) {}
        virtual void Handle(EarlyDepthStencilInfoNullPS event) {}
        virtual void Handle(LateDepthStencilInfoSingleSample event) {}
        virtual void Handle(LateDepthStencilInfoSampleRate event) {}
        virtual void Handle(LateDepthStencilInfoNullPS event) {}
        virtual void Handle(EarlyDepthInfoPixelRate event) {}
        virtual void Handle(LateDepthInfoPixelRate event) {}
        virtual void Handle(BackendDrawEndEvent event) {}
        virtual void Handle(FrontendDrawEndEvent event) {}
        virtual void Handle(EarlyZSingleSample event) {}
        virtual void Handle(LateZSingleSample event) {}
        virtual void Handle(EarlyStencilSingleSample event) {}
        virtual void Handle(LateStencilSingleSample event) {}
        virtual void Handle(EarlyZSampleRate event) {}
        virtual void Handle(LateZSampleRate event) {}
        virtual void Handle(EarlyStencilSampleRate event) {}
        virtual void Handle(LateStencilSampleRate event) {}
        virtual void Handle(EarlyZNullPS event) {}
        virtual void Handle(EarlyStencilNullPS event) {}
        virtual void Handle(EarlyZPixelRate event) {}
        virtual void Handle(LateZPixelRate event) {}
        virtual void Handle(EarlyOmZ event) {}
        virtual void Handle(EarlyOmStencil event) {}
        virtual void Handle(LateOmZ event) {}
        virtual void Handle(LateOmStencil event) {}
        virtual void Handle(GSPrimInfo event) {}
        virtual void Handle(GSInputPrims event) {}
        virtual void Handle(GSPrimsGen event) {}
        virtual void Handle(GSVertsInput event) {}
        virtual void Handle(ClipVertexCount event) {}
        virtual void Handle(FlushVertClip event) {}
        virtual void Handle(VertsClipped event) {}
        virtual void Handle(TessPrimCount event) {}
        virtual void Handle(TessPrimFlush event) {}
        virtual void Handle(TessPrims event) {}
    };
}
