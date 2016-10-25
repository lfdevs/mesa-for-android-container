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
        virtual void handle(Start& event) {}
        virtual void handle(End& event) {}
        virtual void handle(DrawInstancedEvent& event) {}
        virtual void handle(DrawIndexedInstancedEvent& event) {}
        virtual void handle(DispatchEvent& event) {}
        virtual void handle(FrameEndEvent& event) {}
        virtual void handle(FrontendStatsEvent& event) {}
        virtual void handle(BackendStatsEvent& event) {}
    };
}
