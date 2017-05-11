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
* @file archrast.cpp
*
* @brief Implementation for archrast.
*
******************************************************************************/
#include <atomic>

#include "common/os.h"
#include "archrast/archrast.h"
#include "archrast/eventmanager.h"
#include "gen_ar_eventhandlerfile.hpp"

namespace ArchRast
{
    //////////////////////////////////////////////////////////////////////////
    /// @brief struct that keeps track of depth and stencil event information
    struct DepthStencilStats
    {
        uint32_t earlyZTestPassCount = 0;
        uint32_t earlyZTestFailCount = 0;
        uint32_t lateZTestPassCount = 0;
        uint32_t lateZTestFailCount = 0;
        uint32_t earlyStencilTestPassCount = 0;
        uint32_t earlyStencilTestFailCount = 0;
        uint32_t lateStencilTestPassCount = 0;
        uint32_t lateStencilTestFailCount = 0;
    };

    struct CStats
    {
        uint32_t clippedVerts = 0;
    };

    struct TEStats
    {
        uint32_t inputPrims = 0;
        //@todo:: Change this to numPatches. Assumed: 1 patch per prim. If holds, its fine.
    };

    struct GSStats
    {
        uint32_t inputPrimCount;
        uint32_t primGeneratedCount;
        uint32_t vertsInput;
    };

    //////////////////////////////////////////////////////////////////////////
    /// @brief Event handler that saves stat events to event files. This
    ///        handler filters out unwanted events.
    class EventHandlerStatsFile : public EventHandlerFile
    {
    public:
        EventHandlerStatsFile(uint32_t id) : EventHandlerFile(id), mNeedFlush(false) {}

        // These are events that we're not interested in saving in stats event files.
        virtual void Handle(const Start& event) {}
        virtual void Handle(const End& event) {}

        virtual void Handle(const EarlyDepthStencilInfoSingleSample& event)
        {
            //earlyZ test compute
            mDSSingleSample.earlyZTestPassCount += _mm_popcnt_u32(event.data.depthPassMask);
            mDSSingleSample.earlyZTestFailCount += _mm_popcnt_u32((!event.data.depthPassMask) & event.data.coverageMask);

            //earlyStencil test compute
            mDSSingleSample.earlyStencilTestPassCount += _mm_popcnt_u32(event.data.stencilPassMask);
            mDSSingleSample.earlyStencilTestFailCount += _mm_popcnt_u32((!event.data.stencilPassMask) & event.data.coverageMask);
            mNeedFlush = true;
        }

        virtual void Handle(const EarlyDepthStencilInfoSampleRate& event)
        {
            //earlyZ test compute
            mDSSampleRate.earlyZTestPassCount += _mm_popcnt_u32(event.data.depthPassMask);
            mDSSampleRate.earlyZTestFailCount += _mm_popcnt_u32((!event.data.depthPassMask) & event.data.coverageMask);

            //earlyStencil test compute
            mDSSampleRate.earlyStencilTestPassCount += _mm_popcnt_u32(event.data.stencilPassMask);
            mDSSampleRate.earlyStencilTestFailCount += _mm_popcnt_u32((!event.data.stencilPassMask) & event.data.coverageMask);
            mNeedFlush = true;
        }

        virtual void Handle(const EarlyDepthStencilInfoNullPS& event)
        {
            //earlyZ test compute
            mDSNullPS.earlyZTestPassCount += _mm_popcnt_u32(event.data.depthPassMask);
            mDSNullPS.earlyZTestFailCount += _mm_popcnt_u32((!event.data.depthPassMask) & event.data.coverageMask);

            //earlyStencil test compute
            mDSNullPS.earlyStencilTestPassCount += _mm_popcnt_u32(event.data.stencilPassMask);
            mDSNullPS.earlyStencilTestFailCount += _mm_popcnt_u32((!event.data.stencilPassMask) & event.data.coverageMask);
            mNeedFlush = true;
        }

        virtual void Handle(const LateDepthStencilInfoSingleSample& event)
        {
            //lateZ test compute
            mDSSingleSample.lateZTestPassCount += _mm_popcnt_u32(event.data.depthPassMask);
            mDSSingleSample.lateZTestFailCount += _mm_popcnt_u32((!event.data.depthPassMask) & event.data.coverageMask);

            //lateStencil test compute
            mDSSingleSample.lateStencilTestPassCount += _mm_popcnt_u32(event.data.stencilPassMask);
            mDSSingleSample.lateStencilTestFailCount += _mm_popcnt_u32((!event.data.stencilPassMask) & event.data.coverageMask);
            mNeedFlush = true;
        }

        virtual void Handle(const LateDepthStencilInfoSampleRate& event)
        {
            //lateZ test compute
            mDSSampleRate.lateZTestPassCount += _mm_popcnt_u32(event.data.depthPassMask);
            mDSSampleRate.lateZTestFailCount += _mm_popcnt_u32((!event.data.depthPassMask) & event.data.coverageMask);

            //lateStencil test compute
            mDSSampleRate.lateStencilTestPassCount += _mm_popcnt_u32(event.data.stencilPassMask);
            mDSSampleRate.lateStencilTestFailCount += _mm_popcnt_u32((!event.data.stencilPassMask) & event.data.coverageMask);
            mNeedFlush = true;
        }

        virtual void Handle(const LateDepthStencilInfoNullPS& event)
        {
            //lateZ test compute
            mDSNullPS.lateZTestPassCount += _mm_popcnt_u32(event.data.depthPassMask);
            mDSNullPS.lateZTestFailCount += _mm_popcnt_u32((!event.data.depthPassMask) & event.data.coverageMask);

            //lateStencil test compute
            mDSNullPS.lateStencilTestPassCount += _mm_popcnt_u32(event.data.stencilPassMask);
            mDSNullPS.lateStencilTestFailCount += _mm_popcnt_u32((!event.data.stencilPassMask) & event.data.coverageMask);
            mNeedFlush = true;
        }

        virtual void Handle(const EarlyDepthInfoPixelRate& event)
        {
            //earlyZ test compute
            mDSPixelRate.earlyZTestPassCount += event.data.depthPassCount;
            mDSPixelRate.earlyZTestFailCount += (_mm_popcnt_u32(event.data.activeLanes) - event.data.depthPassCount);
            mNeedFlush = true;
        }


        virtual void Handle(const LateDepthInfoPixelRate& event)
        {
            //lateZ test compute
            mDSPixelRate.lateZTestPassCount += event.data.depthPassCount;
            mDSPixelRate.lateZTestFailCount += (_mm_popcnt_u32(event.data.activeLanes) - event.data.depthPassCount);
            mNeedFlush = true;
        }


        // Flush cached events for this draw
        virtual void FlushDraw(uint32_t drawId)
        {
            if (mNeedFlush == false) return;

            //singleSample
            EventHandlerFile::Handle(EarlyZSingleSample(drawId, mDSSingleSample.earlyZTestPassCount, mDSSingleSample.earlyZTestFailCount));
            EventHandlerFile::Handle(LateZSingleSample(drawId, mDSSingleSample.lateZTestPassCount, mDSSingleSample.lateZTestFailCount));
            EventHandlerFile::Handle(EarlyStencilSingleSample(drawId, mDSSingleSample.earlyStencilTestPassCount, mDSSingleSample.earlyStencilTestFailCount));
            EventHandlerFile::Handle(LateStencilSingleSample(drawId, mDSSingleSample.lateStencilTestPassCount, mDSSingleSample.lateStencilTestFailCount));

            //sampleRate
            EventHandlerFile::Handle(EarlyZSampleRate(drawId, mDSSampleRate.earlyZTestPassCount, mDSSampleRate.earlyZTestFailCount));
            EventHandlerFile::Handle(LateZSampleRate(drawId, mDSSampleRate.lateZTestPassCount, mDSSampleRate.lateZTestFailCount));
            EventHandlerFile::Handle(EarlyStencilSampleRate(drawId, mDSSampleRate.earlyStencilTestPassCount, mDSSampleRate.earlyStencilTestFailCount));
            EventHandlerFile::Handle(LateStencilSampleRate(drawId, mDSSampleRate.lateStencilTestPassCount, mDSSampleRate.lateStencilTestFailCount));

            //pixelRate
            EventHandlerFile::Handle(EarlyZPixelRate(drawId, mDSPixelRate.earlyZTestPassCount, mDSPixelRate.earlyZTestFailCount));
            EventHandlerFile::Handle(LateZPixelRate(drawId, mDSPixelRate.lateZTestPassCount, mDSPixelRate.lateZTestFailCount));


            //NullPS
            EventHandlerFile::Handle(EarlyZNullPS(drawId, mDSNullPS.earlyZTestPassCount, mDSNullPS.earlyZTestFailCount));
            EventHandlerFile::Handle(EarlyStencilNullPS(drawId, mDSNullPS.earlyStencilTestPassCount, mDSNullPS.earlyStencilTestFailCount));

            //Reset Internal Counters
            mDSSingleSample = {};
            mDSSampleRate = {};
            mDSPixelRate = {};
            mDSNullPS = {};

            mNeedFlush = false;
        }

        virtual void Handle(const FrontendDrawEndEvent& event)
        {
            //Clipper
            EventHandlerFile::Handle(VertsClipped(event.data.drawId, mClipper.clippedVerts));

            //Tesselator
            EventHandlerFile::Handle(TessPrims(event.data.drawId, mTS.inputPrims));

            //Geometry Shader
            EventHandlerFile::Handle(GSInputPrims(event.data.drawId, mGS.inputPrimCount));
            EventHandlerFile::Handle(GSPrimsGen(event.data.drawId, mGS.primGeneratedCount));
            EventHandlerFile::Handle(GSVertsInput(event.data.drawId, mGS.vertsInput));

            //Reset Internal Counters
            mClipper = {};
            mTS = {};
            mGS = {};
        }

        virtual void Handle(const GSPrimInfo& event)
        {
            mGS.inputPrimCount += event.data.inputPrimCount;
            mGS.primGeneratedCount += event.data.primGeneratedCount;
            mGS.vertsInput += event.data.vertsInput;
        }

        virtual void Handle(const ClipVertexCount& event)
        {
            mClipper.clippedVerts += (_mm_popcnt_u32(event.data.primMask) * event.data.vertsPerPrim);
        }

        virtual void Handle(const TessPrimCount& event)
        {
            mTS.inputPrims += event.data.primCount;
        }

    protected:
        bool mNeedFlush;
        // Per draw stats
        DepthStencilStats mDSSingleSample = {};
        DepthStencilStats mDSSampleRate = {};
        DepthStencilStats mDSPixelRate = {};
        DepthStencilStats mDSNullPS = {};
        DepthStencilStats mDSOmZ = {};
        CStats mClipper = {};
        TEStats mTS = {};
        GSStats mGS = {};

    };

    static EventManager* FromHandle(HANDLE hThreadContext)
    {
        return reinterpret_cast<EventManager*>(hThreadContext);
    }

    // Construct an event manager and associate a handler with it.
    HANDLE CreateThreadContext(AR_THREAD type)
    {
        // Can we assume single threaded here?
        static std::atomic<uint32_t> counter(0);
        uint32_t id = counter.fetch_add(1);

        EventManager* pManager = new EventManager();
        EventHandlerFile* pHandler = new EventHandlerStatsFile(id);

        if (pManager && pHandler)
        {
            pManager->Attach(pHandler);

            if (type == AR_THREAD::API)
            {
                pHandler->Handle(ThreadStartApiEvent());
            }
            else
            {
                pHandler->Handle(ThreadStartWorkerEvent());
            }
            pHandler->MarkHeader();

            return pManager;
        }

        SWR_INVALID("Failed to register thread.");
        return nullptr;
    }

    void DestroyThreadContext(HANDLE hThreadContext)
    {
        EventManager* pManager = FromHandle(hThreadContext);
        SWR_ASSERT(pManager != nullptr);

        delete pManager;
    }

    // Dispatch event for this thread.
    void Dispatch(HANDLE hThreadContext, const Event& event)
    {
        EventManager* pManager = FromHandle(hThreadContext);
        SWR_ASSERT(pManager != nullptr);

        pManager->Dispatch(event);
    }

    // Flush for this thread.
    void FlushDraw(HANDLE hThreadContext, uint32_t drawId)
    {
        EventManager* pManager = FromHandle(hThreadContext);
        SWR_ASSERT(pManager != nullptr);

        pManager->FlushDraw(drawId);
    }
}
