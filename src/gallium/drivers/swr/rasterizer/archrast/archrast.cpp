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
* @file archrast.h
*
* @brief Definitions for archrast.
*
******************************************************************************/
#include <atomic>

#include "common/os.h"
#include "archrast/archrast.h"
#include "archrast/eventmanager.h"
#include "gen_ar_eventhandlerfile.h"

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
        uint32_t earlyZTestCount = 0;
        uint32_t lateZTestCount = 0;
        uint32_t earlyStencilTestCount = 0;
        uint32_t lateStencilTestCount = 0;
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
        DepthStencilStats DSSingleSample = {};
        DepthStencilStats DSSampleRate = {};
        DepthStencilStats DSPixelRate = {};
        DepthStencilStats DSNullPS = {};
        DepthStencilStats DSOmZ = {};
        CStats CS = {};
        TEStats TS = {};
        GSStats GS = {};

        EventHandlerStatsFile(uint32_t id) : EventHandlerFile(id) {}

        // These are events that we're not interested in saving in stats event files.
        virtual void Handle(Start& event) {}
        virtual void Handle(End& event) {}

        virtual void Handle(EarlyDepthStencilInfoSingleSample& event)
        {
            //earlyZ test compute
            DSSingleSample.earlyZTestPassCount += _mm_popcnt_u32(event.data.depthPassMask);
            DSSingleSample.earlyZTestFailCount += _mm_popcnt_u32((!event.data.depthPassMask) & event.data.coverageMask);
            DSSingleSample.earlyZTestCount += (_mm_popcnt_u32(event.data.depthPassMask) + _mm_popcnt_u32((!event.data.depthPassMask) & event.data.coverageMask));

            //earlyStencil test compute
            DSSingleSample.earlyStencilTestPassCount += _mm_popcnt_u32(event.data.stencilPassMask);
            DSSingleSample.earlyStencilTestFailCount += _mm_popcnt_u32((!event.data.stencilPassMask) & event.data.coverageMask);
            DSSingleSample.earlyStencilTestCount += (_mm_popcnt_u32(event.data.stencilPassMask) + _mm_popcnt_u32((!event.data.stencilPassMask) & event.data.coverageMask));

            //outputerMerger test compute
            DSOmZ.earlyZTestPassCount += DSSingleSample.earlyZTestPassCount;
            DSOmZ.earlyZTestFailCount += DSSingleSample.earlyZTestFailCount;
            DSOmZ.earlyZTestCount += DSSingleSample.earlyZTestCount;
            DSOmZ.earlyStencilTestPassCount += DSSingleSample.earlyStencilTestPassCount;
            DSOmZ.earlyStencilTestFailCount += DSSingleSample.earlyStencilTestFailCount;
            DSOmZ.earlyStencilTestCount += DSSingleSample.earlyStencilTestCount;
        }

        virtual void Handle(EarlyDepthStencilInfoSampleRate& event)
        {
            //earlyZ test compute
            DSSampleRate.earlyZTestPassCount += _mm_popcnt_u32(event.data.depthPassMask);
            DSSampleRate.earlyZTestFailCount += _mm_popcnt_u32((!event.data.depthPassMask) & event.data.coverageMask);
            DSSampleRate.earlyZTestCount += (_mm_popcnt_u32(event.data.depthPassMask) + _mm_popcnt_u32((!event.data.depthPassMask) & event.data.coverageMask));

            //earlyStencil test compute
            DSSampleRate.earlyStencilTestPassCount += _mm_popcnt_u32(event.data.stencilPassMask);
            DSSampleRate.earlyStencilTestFailCount += _mm_popcnt_u32((!event.data.stencilPassMask) & event.data.coverageMask);
            DSSampleRate.earlyStencilTestCount += (_mm_popcnt_u32(event.data.stencilPassMask) + _mm_popcnt_u32((!event.data.stencilPassMask) & event.data.coverageMask));

            //outputerMerger test compute
            DSOmZ.earlyZTestPassCount += DSSampleRate.earlyZTestPassCount;
            DSOmZ.earlyZTestFailCount += DSSampleRate.earlyZTestFailCount;
            DSOmZ.earlyZTestCount += DSSampleRate.earlyZTestCount;
            DSOmZ.earlyStencilTestPassCount += DSSampleRate.earlyStencilTestPassCount;
            DSOmZ.earlyStencilTestFailCount += DSSampleRate.earlyStencilTestFailCount;
            DSOmZ.earlyStencilTestCount += DSSampleRate.earlyStencilTestCount;
        }

        virtual void Handle(EarlyDepthStencilInfoNullPS& event)
        {
            //earlyZ test compute
            DSNullPS.earlyZTestPassCount += _mm_popcnt_u32(event.data.depthPassMask);
            DSNullPS.earlyZTestFailCount += _mm_popcnt_u32((!event.data.depthPassMask) & event.data.coverageMask);
            DSNullPS.earlyZTestCount += (_mm_popcnt_u32(event.data.depthPassMask) + _mm_popcnt_u32((!event.data.depthPassMask) & event.data.coverageMask));

            //earlyStencil test compute
            DSNullPS.earlyStencilTestPassCount += _mm_popcnt_u32(event.data.stencilPassMask);
            DSNullPS.earlyStencilTestFailCount += _mm_popcnt_u32((!event.data.stencilPassMask) & event.data.coverageMask);
            DSNullPS.earlyStencilTestCount += (_mm_popcnt_u32(event.data.stencilPassMask) + _mm_popcnt_u32((!event.data.stencilPassMask) & event.data.coverageMask));

            //outputerMerger test compute
            DSOmZ.earlyZTestPassCount += DSNullPS.earlyZTestPassCount;
            DSOmZ.earlyZTestFailCount += DSNullPS.earlyZTestFailCount;
            DSOmZ.earlyZTestCount += DSNullPS.earlyZTestCount;
            DSOmZ.earlyStencilTestPassCount += DSNullPS.earlyStencilTestPassCount;
            DSOmZ.earlyStencilTestFailCount += DSNullPS.earlyStencilTestFailCount;
            DSOmZ.earlyStencilTestCount += DSNullPS.earlyStencilTestCount;
        }

        virtual void Handle(LateDepthStencilInfoSingleSample& event)
        {
            //lateZ test compute
            DSSingleSample.lateZTestPassCount += _mm_popcnt_u32(event.data.depthPassMask);
            DSSingleSample.lateZTestFailCount += _mm_popcnt_u32((!event.data.depthPassMask) & event.data.coverageMask);
            DSSingleSample.lateZTestCount += (_mm_popcnt_u32(event.data.depthPassMask) + _mm_popcnt_u32((!event.data.depthPassMask) & event.data.coverageMask));

            //lateStencil test compute
            DSSingleSample.lateStencilTestPassCount += _mm_popcnt_u32(event.data.stencilPassMask);
            DSSingleSample.lateStencilTestFailCount += _mm_popcnt_u32((!event.data.stencilPassMask) & event.data.coverageMask);
            DSSingleSample.lateStencilTestCount += (_mm_popcnt_u32(event.data.stencilPassMask) + _mm_popcnt_u32((!event.data.stencilPassMask) & event.data.coverageMask));

            //outputerMerger test compute
            DSOmZ.lateZTestPassCount += DSSingleSample.lateZTestPassCount;
            DSOmZ.lateZTestFailCount += DSSingleSample.lateZTestFailCount;
            DSOmZ.lateZTestCount += DSSingleSample.lateZTestCount;
            DSOmZ.lateStencilTestPassCount += DSSingleSample.lateStencilTestPassCount;
            DSOmZ.lateStencilTestFailCount += DSSingleSample.lateStencilTestFailCount;
            DSOmZ.lateStencilTestCount += DSSingleSample.lateStencilTestCount;
        }

        virtual void Handle(LateDepthStencilInfoSampleRate& event)
        {
            //lateZ test compute
            DSSampleRate.lateZTestPassCount += _mm_popcnt_u32(event.data.depthPassMask);
            DSSampleRate.lateZTestFailCount += _mm_popcnt_u32((!event.data.depthPassMask) & event.data.coverageMask);
            DSSampleRate.lateZTestCount += (_mm_popcnt_u32(event.data.depthPassMask) + _mm_popcnt_u32((!event.data.depthPassMask) & event.data.coverageMask));

            //lateStencil test compute
            DSSampleRate.lateStencilTestPassCount += _mm_popcnt_u32(event.data.stencilPassMask);
            DSSampleRate.lateStencilTestFailCount += _mm_popcnt_u32((!event.data.stencilPassMask) & event.data.coverageMask);
            DSSampleRate.lateStencilTestCount += (_mm_popcnt_u32(event.data.stencilPassMask) + _mm_popcnt_u32((!event.data.stencilPassMask) & event.data.coverageMask));

            //outputerMerger test compute
            DSOmZ.lateZTestPassCount += DSSampleRate.lateZTestPassCount;
            DSOmZ.lateZTestFailCount += DSSampleRate.lateZTestFailCount;
            DSOmZ.lateZTestCount += DSSampleRate.lateZTestCount;
            DSOmZ.lateStencilTestPassCount += DSSampleRate.lateStencilTestPassCount;
            DSOmZ.lateStencilTestFailCount += DSSampleRate.lateStencilTestFailCount;
            DSOmZ.lateStencilTestCount += DSSampleRate.lateStencilTestCount;
        }

        virtual void Handle(LateDepthStencilInfoNullPS& event)
        {
            //lateZ test compute
            DSNullPS.lateZTestPassCount += _mm_popcnt_u32(event.data.depthPassMask);
            DSNullPS.lateZTestFailCount += _mm_popcnt_u32((!event.data.depthPassMask) & event.data.coverageMask);
            DSNullPS.lateZTestCount += (_mm_popcnt_u32(event.data.depthPassMask) + _mm_popcnt_u32((!event.data.depthPassMask) & event.data.coverageMask));

            //lateStencil test compute
            DSNullPS.lateStencilTestPassCount += _mm_popcnt_u32(event.data.stencilPassMask);
            DSNullPS.lateStencilTestFailCount += _mm_popcnt_u32((!event.data.stencilPassMask) & event.data.coverageMask);
            DSNullPS.lateStencilTestCount += (_mm_popcnt_u32(event.data.stencilPassMask) + _mm_popcnt_u32((!event.data.stencilPassMask) & event.data.coverageMask));

            //outputerMerger test compute
            DSOmZ.lateZTestPassCount += DSNullPS.lateZTestPassCount;
            DSOmZ.lateZTestFailCount += DSNullPS.lateZTestFailCount;
            DSOmZ.lateZTestCount += DSNullPS.lateZTestCount;
            DSOmZ.lateStencilTestPassCount += DSNullPS.lateStencilTestPassCount;
            DSOmZ.lateStencilTestFailCount += DSNullPS.lateStencilTestFailCount;
            DSOmZ.lateStencilTestCount += DSNullPS.lateStencilTestCount;
        }

        virtual void Handle(EarlyDepthInfoPixelRate& event)
        {
            //earlyZ test compute
            DSPixelRate.earlyZTestCount += _mm_popcnt_u32(event.data.activeLanes);
            DSPixelRate.earlyZTestPassCount += event.data.depthPassCount;
            DSPixelRate.earlyZTestFailCount += (_mm_popcnt_u32(event.data.activeLanes) - event.data.depthPassCount);

            //outputerMerger test compute
            DSOmZ.earlyZTestPassCount += DSPixelRate.earlyZTestPassCount;
            DSOmZ.earlyZTestFailCount += DSPixelRate.earlyZTestFailCount;
            DSOmZ.earlyZTestCount += DSPixelRate.earlyZTestCount;
        }


        virtual void Handle(LateDepthInfoPixelRate& event)
        {
            //lateZ test compute
            DSPixelRate.lateZTestCount += _mm_popcnt_u32(event.data.activeLanes);
            DSPixelRate.lateZTestPassCount += event.data.depthPassCount;
            DSPixelRate.lateZTestFailCount += (_mm_popcnt_u32(event.data.activeLanes) - event.data.depthPassCount);

            //outputerMerger test compute
            DSOmZ.lateZTestPassCount += DSPixelRate.lateZTestPassCount;
            DSOmZ.lateZTestFailCount += DSPixelRate.lateZTestFailCount;
            DSOmZ.lateZTestCount += DSPixelRate.lateZTestCount;

        }


        virtual void Handle(BackendDrawEndEvent& event)
        {
            //singleSample
            EventHandlerFile::Handle(EarlyZSingleSample(event.data.drawId, DSSingleSample.earlyZTestPassCount, DSSingleSample.earlyZTestFailCount, DSSingleSample.earlyZTestCount));
            EventHandlerFile::Handle(LateZSingleSample(event.data.drawId, DSSingleSample.lateZTestPassCount, DSSingleSample.lateZTestFailCount, DSSingleSample.lateZTestCount));
            EventHandlerFile::Handle(EarlyStencilSingleSample(event.data.drawId, DSSingleSample.earlyStencilTestPassCount, DSSingleSample.earlyStencilTestFailCount, DSSingleSample.earlyStencilTestCount));
            EventHandlerFile::Handle(LateStencilSingleSample(event.data.drawId, DSSingleSample.lateStencilTestPassCount, DSSingleSample.lateStencilTestFailCount, DSSingleSample.lateStencilTestCount));

            //sampleRate
            EventHandlerFile::Handle(EarlyZSampleRate(event.data.drawId, DSSampleRate.earlyZTestPassCount, DSSampleRate.earlyZTestFailCount, DSSampleRate.earlyZTestCount));
            EventHandlerFile::Handle(LateZSampleRate(event.data.drawId, DSSampleRate.lateZTestPassCount, DSSampleRate.lateZTestFailCount, DSSampleRate.lateZTestCount));
            EventHandlerFile::Handle(EarlyStencilSampleRate(event.data.drawId, DSSampleRate.earlyStencilTestPassCount, DSSampleRate.earlyStencilTestFailCount, DSSampleRate.earlyStencilTestCount));
            EventHandlerFile::Handle(LateStencilSampleRate(event.data.drawId, DSSampleRate.lateStencilTestPassCount, DSSampleRate.lateStencilTestFailCount, DSSampleRate.lateStencilTestCount));

            //pixelRate
            EventHandlerFile::Handle(EarlyZPixelRate(event.data.drawId, DSPixelRate.earlyZTestPassCount, DSPixelRate.earlyZTestFailCount, DSPixelRate.earlyZTestCount));
            EventHandlerFile::Handle(LateZPixelRate(event.data.drawId, DSPixelRate.lateZTestPassCount, DSPixelRate.lateZTestFailCount, DSPixelRate.lateZTestCount));


            //NullPS
            EventHandlerFile::Handle(EarlyZNullPS(event.data.drawId, DSNullPS.earlyZTestPassCount, DSNullPS.earlyZTestFailCount, DSNullPS.earlyZTestCount));
            EventHandlerFile::Handle(EarlyStencilNullPS(event.data.drawId, DSNullPS.earlyStencilTestPassCount, DSNullPS.earlyStencilTestFailCount, DSNullPS.earlyStencilTestCount));

            //OmZ
            EventHandlerFile::Handle(EarlyOmZ(event.data.drawId, DSOmZ.earlyZTestPassCount, DSOmZ.earlyZTestFailCount, DSOmZ.earlyZTestCount));
            EventHandlerFile::Handle(EarlyOmStencil(event.data.drawId, DSOmZ.earlyStencilTestPassCount, DSOmZ.earlyStencilTestFailCount, DSOmZ.earlyStencilTestCount));
            EventHandlerFile::Handle(LateOmZ(event.data.drawId, DSOmZ.lateZTestPassCount, DSOmZ.lateZTestFailCount, DSOmZ.lateZTestCount));
            EventHandlerFile::Handle(LateOmStencil(event.data.drawId, DSOmZ.lateStencilTestPassCount, DSOmZ.lateStencilTestFailCount, DSOmZ.lateStencilTestCount));

            //Reset Internal Counters
            DSSingleSample = {};
            DSSampleRate = {};
            DSPixelRate = {};
            DSNullPS = {};
            DSOmZ = {};
        }

        virtual void Handle(FrontendDrawEndEvent& event)
        {
            //Clipper
            EventHandlerFile::Handle(VertsClipped(event.data.drawId, CS.clippedVerts));

            //Tesselator
            EventHandlerFile::Handle(TessPrims(event.data.drawId, TS.inputPrims));

            //Geometry Shader
            EventHandlerFile::Handle(GSInputPrims(event.data.drawId, GS.inputPrimCount));
            EventHandlerFile::Handle(GSPrimsGen(event.data.drawId, GS.primGeneratedCount));
            EventHandlerFile::Handle(GSVertsInput(event.data.drawId, GS.vertsInput));

            //Reset Internal Counters
            CS = {};
            TS = {};
            GS = {};
        }

        virtual void Handle(GSPrimInfo& event)
        {
            GS.inputPrimCount += event.data.inputPrimCount;
            GS.primGeneratedCount += event.data.primGeneratedCount;
            GS.vertsInput += event.data.vertsInput;
        }

        virtual void Handle(ClipVertexCount& event)
        {
            CS.clippedVerts += (_mm_popcnt_u32(event.data.primMask) * event.data.vertsPerPrim);
        }

        virtual void Handle(TessPrimCount& event)
        {
            TS.inputPrims += event.data.primCount;
        }
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

        SWR_ASSERT(0, "Failed to register thread.");
        return nullptr;
    }

    void DestroyThreadContext(HANDLE hThreadContext)
    {
        EventManager* pManager = FromHandle(hThreadContext);
        SWR_ASSERT(pManager != nullptr);

        delete pManager;
    }

    // Dispatch event for this thread.
    void Dispatch(HANDLE hThreadContext, Event& event)
    {
        EventManager* pManager = FromHandle(hThreadContext);
        SWR_ASSERT(pManager != nullptr);

        pManager->Dispatch(event);
    }
}
