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
* @file gen_ar_event.h
*
* @brief Definitions for events.  auto-generated file
* 
* DO NOT EDIT
* 
******************************************************************************/
#pragma once

#include "common/os.h"
#include "core/state.h"

namespace ArchRast
{
    enum GroupType
    {
        APIClearRenderTarget,
        APIDraw,
        APIDrawWakeAllThreads,
        APIDrawIndexed,
        APIDispatch,
        APIStoreTiles,
        APIGetDrawContext,
        APISync,
        APIWaitForIdle,
        FEProcessDraw,
        FEProcessDrawIndexed,
        FEFetchShader,
        FEVertexShader,
        FEHullShader,
        FETessellation,
        FEDomainShader,
        FEGeometryShader,
        FEStreamout,
        FEPAAssemble,
        FEBinPoints,
        FEBinLines,
        FEBinTriangles,
        FETriangleSetup,
        FEViewportCull,
        FEGuardbandClip,
        FEClipPoints,
        FEClipLines,
        FEClipTriangles,
        FECullZeroAreaAndBackface,
        FECullBetweenCenters,
        FEProcessStoreTiles,
        FEProcessInvalidateTiles,
        WorkerWorkOnFifoBE,
        WorkerFoundWork,
        BELoadTiles,
        BEDispatch,
        BEClear,
        BERasterizeLine,
        BERasterizeTriangle,
        BETriangleSetup,
        BEStepSetup,
        BECullZeroArea,
        BEEmptyTriangle,
        BETrivialAccept,
        BETrivialReject,
        BERasterizePartial,
        BEPixelBackend,
        BESetup,
        BEBarycentric,
        BEEarlyDepthTest,
        BEPixelShader,
        BESingleSampleBackend,
        BEPixelRateBackend,
        BESampleRateBackend,
        BENullBackend,
        BELateDepthTest,
        BEOutputMerger,
        BEStoreTiles,
        BEEndTile,
        WorkerWaitForThreadEvent,
    };

    //Forward decl
    class EventHandler;

    //////////////////////////////////////////////////////////////////////////
    /// Event - interface for handling events.
    //////////////////////////////////////////////////////////////////////////
    struct Event
    {
        virtual void accept(EventHandler* pHandler) = 0;
    };

    //////////////////////////////////////////////////////////////////////////
    /// StartData
    //////////////////////////////////////////////////////////////////////////
#pragma pack(push, 1)
    struct StartData
    {
        // Fields
        GroupType type;
        uint32_t id;
    };
#pragma pack(pop)

    //////////////////////////////////////////////////////////////////////////
    /// Start
    //////////////////////////////////////////////////////////////////////////
    struct Start : Event
    {
        StartData data;

        // Constructor
        Start(
            GroupType type,
            uint32_t id)
        {
            data.type = type;
            data.id = id;
        }

        virtual void accept(EventHandler* pHandler);
    };

    //////////////////////////////////////////////////////////////////////////
    /// EndData
    //////////////////////////////////////////////////////////////////////////
#pragma pack(push, 1)
    struct EndData
    {
        // Fields
        GroupType type;
        uint32_t count;
    };
#pragma pack(pop)

    //////////////////////////////////////////////////////////////////////////
    /// End
    //////////////////////////////////////////////////////////////////////////
    struct End : Event
    {
        EndData data;

        // Constructor
        End(
            GroupType type,
            uint32_t count)
        {
            data.type = type;
            data.count = count;
        }

        virtual void accept(EventHandler* pHandler);
    };

    //////////////////////////////////////////////////////////////////////////
    /// DrawInstancedEventData
    //////////////////////////////////////////////////////////////////////////
#pragma pack(push, 1)
    struct DrawInstancedEventData
    {
        // Fields
        uint32_t drawId;
        uint32_t topology;
        uint32_t numVertices;
        int32_t startVertex;
        uint32_t numInstances;
        uint32_t startInstance;
    };
#pragma pack(pop)

    //////////////////////////////////////////////////////////////////////////
    /// DrawInstancedEvent
    //////////////////////////////////////////////////////////////////////////
    struct DrawInstancedEvent : Event
    {
        DrawInstancedEventData data;

        // Constructor
        DrawInstancedEvent(
            uint32_t drawId,
            uint32_t topology,
            uint32_t numVertices,
            int32_t startVertex,
            uint32_t numInstances,
            uint32_t startInstance)
        {
            data.drawId = drawId;
            data.topology = topology;
            data.numVertices = numVertices;
            data.startVertex = startVertex;
            data.numInstances = numInstances;
            data.startInstance = startInstance;
        }

        virtual void accept(EventHandler* pHandler);
    };

    //////////////////////////////////////////////////////////////////////////
    /// DrawIndexedInstancedEventData
    //////////////////////////////////////////////////////////////////////////
#pragma pack(push, 1)
    struct DrawIndexedInstancedEventData
    {
        // Fields
        uint32_t drawId;
        uint32_t topology;
        uint32_t numIndices;
        int32_t indexOffset;
        int32_t baseVertex;
        uint32_t numInstances;
        uint32_t startInstance;
    };
#pragma pack(pop)

    //////////////////////////////////////////////////////////////////////////
    /// DrawIndexedInstancedEvent
    //////////////////////////////////////////////////////////////////////////
    struct DrawIndexedInstancedEvent : Event
    {
        DrawIndexedInstancedEventData data;

        // Constructor
        DrawIndexedInstancedEvent(
            uint32_t drawId,
            uint32_t topology,
            uint32_t numIndices,
            int32_t indexOffset,
            int32_t baseVertex,
            uint32_t numInstances,
            uint32_t startInstance)
        {
            data.drawId = drawId;
            data.topology = topology;
            data.numIndices = numIndices;
            data.indexOffset = indexOffset;
            data.baseVertex = baseVertex;
            data.numInstances = numInstances;
            data.startInstance = startInstance;
        }

        virtual void accept(EventHandler* pHandler);
    };

    //////////////////////////////////////////////////////////////////////////
    /// DispatchEventData
    //////////////////////////////////////////////////////////////////////////
#pragma pack(push, 1)
    struct DispatchEventData
    {
        // Fields
        uint32_t drawId;
        uint32_t threadGroupCountX;
        uint32_t threadGroupCountY;
        uint32_t threadGroupCountZ;
    };
#pragma pack(pop)

    //////////////////////////////////////////////////////////////////////////
    /// DispatchEvent
    //////////////////////////////////////////////////////////////////////////
    struct DispatchEvent : Event
    {
        DispatchEventData data;

        // Constructor
        DispatchEvent(
            uint32_t drawId,
            uint32_t threadGroupCountX,
            uint32_t threadGroupCountY,
            uint32_t threadGroupCountZ)
        {
            data.drawId = drawId;
            data.threadGroupCountX = threadGroupCountX;
            data.threadGroupCountY = threadGroupCountY;
            data.threadGroupCountZ = threadGroupCountZ;
        }

        virtual void accept(EventHandler* pHandler);
    };

    //////////////////////////////////////////////////////////////////////////
    /// FrameEndEventData
    //////////////////////////////////////////////////////////////////////////
#pragma pack(push, 1)
    struct FrameEndEventData
    {
        // Fields
        uint32_t frameId;
        uint32_t nextDrawId;
    };
#pragma pack(pop)

    //////////////////////////////////////////////////////////////////////////
    /// FrameEndEvent
    //////////////////////////////////////////////////////////////////////////
    struct FrameEndEvent : Event
    {
        FrameEndEventData data;

        // Constructor
        FrameEndEvent(
            uint32_t frameId,
            uint32_t nextDrawId)
        {
            data.frameId = frameId;
            data.nextDrawId = nextDrawId;
        }

        virtual void accept(EventHandler* pHandler);
    };

    //////////////////////////////////////////////////////////////////////////
    /// FrontendStatsEventData
    //////////////////////////////////////////////////////////////////////////
#pragma pack(push, 1)
    struct FrontendStatsEventData
    {
        // Fields
        uint32_t drawId;
        uint64_t IaVertices;
        uint64_t IaPrimitives;
        uint64_t VsInvocations;
        uint64_t HsInvocations;
        uint64_t DsInvocations;
        uint64_t GsInvocations;
        uint64_t GsPrimitives;
        uint64_t CInvocations;
        uint64_t CPrimitives;
        uint64_t SoPrimStorageNeeded0;
        uint64_t SoPrimStorageNeeded1;
        uint64_t SoPrimStorageNeeded2;
        uint64_t SoPrimStorageNeeded3;
        uint64_t SoNumPrimsWritten0;
        uint64_t SoNumPrimsWritten1;
        uint64_t SoNumPrimsWritten2;
        uint64_t SoNumPrimsWritten3;
    };
#pragma pack(pop)

    //////////////////////////////////////////////////////////////////////////
    /// FrontendStatsEvent
    //////////////////////////////////////////////////////////////////////////
    struct FrontendStatsEvent : Event
    {
        FrontendStatsEventData data;

        // Constructor
        FrontendStatsEvent(
            uint32_t drawId,
            uint64_t IaVertices,
            uint64_t IaPrimitives,
            uint64_t VsInvocations,
            uint64_t HsInvocations,
            uint64_t DsInvocations,
            uint64_t GsInvocations,
            uint64_t GsPrimitives,
            uint64_t CInvocations,
            uint64_t CPrimitives,
            uint64_t SoPrimStorageNeeded0,
            uint64_t SoPrimStorageNeeded1,
            uint64_t SoPrimStorageNeeded2,
            uint64_t SoPrimStorageNeeded3,
            uint64_t SoNumPrimsWritten0,
            uint64_t SoNumPrimsWritten1,
            uint64_t SoNumPrimsWritten2,
            uint64_t SoNumPrimsWritten3)
        {
            data.drawId = drawId;
            data.IaVertices = IaVertices;
            data.IaPrimitives = IaPrimitives;
            data.VsInvocations = VsInvocations;
            data.HsInvocations = HsInvocations;
            data.DsInvocations = DsInvocations;
            data.GsInvocations = GsInvocations;
            data.GsPrimitives = GsPrimitives;
            data.CInvocations = CInvocations;
            data.CPrimitives = CPrimitives;
            data.SoPrimStorageNeeded0 = SoPrimStorageNeeded0;
            data.SoPrimStorageNeeded1 = SoPrimStorageNeeded1;
            data.SoPrimStorageNeeded2 = SoPrimStorageNeeded2;
            data.SoPrimStorageNeeded3 = SoPrimStorageNeeded3;
            data.SoNumPrimsWritten0 = SoNumPrimsWritten0;
            data.SoNumPrimsWritten1 = SoNumPrimsWritten1;
            data.SoNumPrimsWritten2 = SoNumPrimsWritten2;
            data.SoNumPrimsWritten3 = SoNumPrimsWritten3;
        }

        virtual void accept(EventHandler* pHandler);
    };

    //////////////////////////////////////////////////////////////////////////
    /// BackendStatsEventData
    //////////////////////////////////////////////////////////////////////////
#pragma pack(push, 1)
    struct BackendStatsEventData
    {
        // Fields
        uint32_t drawId;
        uint64_t DepthPassCount;
        uint64_t PsInvocations;
        uint64_t CsInvocations;
    };
#pragma pack(pop)

    //////////////////////////////////////////////////////////////////////////
    /// BackendStatsEvent
    //////////////////////////////////////////////////////////////////////////
    struct BackendStatsEvent : Event
    {
        BackendStatsEventData data;

        // Constructor
        BackendStatsEvent(
            uint32_t drawId,
            uint64_t DepthPassCount,
            uint64_t PsInvocations,
            uint64_t CsInvocations)
        {
            data.drawId = drawId;
            data.DepthPassCount = DepthPassCount;
            data.PsInvocations = PsInvocations;
            data.CsInvocations = CsInvocations;
        }

        virtual void accept(EventHandler* pHandler);
    };
}
