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
****************************************************************************/

#include <stdio.h>
#include <thread>
#include <algorithm>
#include <float.h>
#include <vector>
#include <utility>
#include <fstream>
#include <string>

#if defined(__linux__) || defined(__gnu_linux__)
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#endif

#include "common/os.h"
#include "context.h"
#include "frontend.h"
#include "backend.h"
#include "rasterizer.h"
#include "rdtsc_core.h"
#include "tilemgr.h"




// ThreadId
struct Core
{
    uint32_t                procGroup = 0;
    std::vector<uint32_t>   threadIds;
};

struct NumaNode
{
    std::vector<Core> cores;
};

typedef std::vector<NumaNode> CPUNumaNodes;

void CalculateProcessorTopology(CPUNumaNodes& out_nodes, uint32_t& out_numThreadsPerProcGroup)
{
    out_nodes.clear();
    out_numThreadsPerProcGroup = 0;

#if defined(_WIN32)

    std::vector<KAFFINITY> threadMaskPerProcGroup;

    static std::mutex m;
    std::lock_guard<std::mutex> l(m);

    DWORD bufSize = 0;

    BOOL ret = GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bufSize);
    SWR_ASSERT(ret == FALSE && GetLastError() == ERROR_INSUFFICIENT_BUFFER);

    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX pBufferMem = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)malloc(bufSize);
    SWR_ASSERT(pBufferMem);

    ret = GetLogicalProcessorInformationEx(RelationProcessorCore, pBufferMem, &bufSize);
    SWR_ASSERT(ret != FALSE, "Failed to get Processor Topology Information");

    uint32_t count = bufSize / pBufferMem->Size;
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX pBuffer = pBufferMem;

    for (uint32_t i = 0; i < count; ++i)
    {
        SWR_ASSERT(pBuffer->Relationship == RelationProcessorCore);
        for (uint32_t g = 0; g < pBuffer->Processor.GroupCount; ++g)
        {
            auto& gmask = pBuffer->Processor.GroupMask[g];
            uint32_t threadId = 0;
            uint32_t procGroup = gmask.Group;

            Core* pCore = nullptr;

            uint32_t numThreads = (uint32_t)_mm_popcount_sizeT(gmask.Mask);

            while (BitScanForwardSizeT((unsigned long*)&threadId, gmask.Mask))
            {
                // clear mask
                KAFFINITY threadMask = KAFFINITY(1) << threadId;
                gmask.Mask &= ~threadMask;

                if (procGroup >= threadMaskPerProcGroup.size())
                {
                    threadMaskPerProcGroup.resize(procGroup + 1);
                }

                if (threadMaskPerProcGroup[procGroup] & threadMask)
                {
                    // Already seen this mask.  This means that we are in 32-bit mode and
                    // have seen more than 32 HW threads for this procGroup
                    // Don't use it
#if defined(_WIN64)
                    SWR_ASSERT(false, "Shouldn't get here in 64-bit mode");
#endif
                    continue;
                }

                threadMaskPerProcGroup[procGroup] |= (KAFFINITY(1) << threadId);

                // Find Numa Node
                uint32_t numaId = 0;
                PROCESSOR_NUMBER procNum = {};
                procNum.Group = WORD(procGroup);
                procNum.Number = UCHAR(threadId);

                ret = GetNumaProcessorNodeEx(&procNum, (PUSHORT)&numaId);
                SWR_ASSERT(ret);

                // Store data
                if (out_nodes.size() <= numaId) out_nodes.resize(numaId + 1);
                auto& numaNode = out_nodes[numaId];

                uint32_t coreId = 0;

                if (nullptr == pCore)
                {
                    numaNode.cores.push_back(Core());
                    pCore = &numaNode.cores.back();
                    pCore->procGroup = procGroup;
                }
                pCore->threadIds.push_back(threadId);
                if (procGroup == 0)
                {
                    out_numThreadsPerProcGroup++;
                }
            }
        }
        pBuffer = PtrAdd(pBuffer, pBuffer->Size);
    }

    free(pBufferMem);


#elif defined(__linux__) || defined (__gnu_linux__)

    // Parse /proc/cpuinfo to get full topology
    std::ifstream input("/proc/cpuinfo");
    std::string line;
    char* c;
    uint32_t threadId = uint32_t(-1);
    uint32_t coreId = uint32_t(-1);
    uint32_t numaId = uint32_t(-1);

    while (std::getline(input, line))
    {
        if (line.find("processor") != std::string::npos)
        {
            if (threadId != uint32_t(-1))
            {
                // Save information.
                if (out_nodes.size() <= numaId) out_nodes.resize(numaId + 1);
                auto& numaNode = out_nodes[numaId];
                if (numaNode.cores.size() <= coreId) numaNode.cores.resize(coreId + 1);
                auto& core = numaNode.cores[coreId];

                core.procGroup = coreId;
                core.threadIds.push_back(threadId);

                out_numThreadsPerProcGroup++;
            }

            auto data_start = line.find(": ") + 2;
            threadId = std::strtoul(&line.c_str()[data_start], &c, 10);
            continue;
        }
        if (line.find("core id") != std::string::npos)
        {
            auto data_start = line.find(": ") + 2;
            coreId = std::strtoul(&line.c_str()[data_start], &c, 10);
            continue;
        }
        if (line.find("physical id") != std::string::npos)
        {
            auto data_start = line.find(": ") + 2;
            numaId = std::strtoul(&line.c_str()[data_start], &c, 10);
            continue;
        }
    }

    if (threadId != uint32_t(-1))
    {
        // Save information.
        if (out_nodes.size() <= numaId) out_nodes.resize(numaId + 1);
        auto& numaNode = out_nodes[numaId];
        if (numaNode.cores.size() <= coreId) numaNode.cores.resize(coreId + 1);
        auto& core = numaNode.cores[coreId];

        core.procGroup = coreId;
        core.threadIds.push_back(threadId);
        out_numThreadsPerProcGroup++;
    }

    /* Prune empty numa nodes */
    for (auto it = out_nodes.begin(); it != out_nodes.end(); ) {
       if ((*it).cores.size() == 0)
          it = out_nodes.erase(it);
       else
          ++it;
    }

    /* Prune empty core nodes */
    for (uint32_t node = 0; node < out_nodes.size(); node++) {
        auto& numaNode = out_nodes[node];
        auto it = numaNode.cores.begin();
        for ( ; it != numaNode.cores.end(); ) {
            if (it->threadIds.size() == 0)
                numaNode.cores.erase(it);
            else
                ++it;
        }
    }

#else

#error Unsupported platform

#endif
}


void bindThread(SWR_CONTEXT* pContext, uint32_t threadId, uint32_t procGroupId = 0, bool bindProcGroup=false)
{
    // Only bind threads when MAX_WORKER_THREADS isn't set.
    if (pContext->threadInfo.MAX_WORKER_THREADS && bindProcGroup == false)
    {
        return;
    }

#if defined(_WIN32)

    GROUP_AFFINITY affinity = {};
    affinity.Group = procGroupId;

#if !defined(_WIN64)
    if (threadId >= 32)
    {
        // Hopefully we don't get here.  Logic in CreateThreadPool should prevent this.
        SWR_REL_ASSERT(false, "Shouldn't get here");

        // In a 32-bit process on Windows it is impossible to bind
        // to logical processors 32-63 within a processor group.
        // In this case set the mask to 0 and let the system assign
        // the processor.  Hopefully it will make smart choices.
        affinity.Mask = 0;
    }
    else
#endif
    {
        // If MAX_WORKER_THREADS is set, only bind to the proc group,
        // Not the individual HW thread.
        if (!pContext->threadInfo.MAX_WORKER_THREADS)
        {
            affinity.Mask = KAFFINITY(1) << threadId;
        }
    }

    SetThreadGroupAffinity(GetCurrentThread(), &affinity, nullptr);

#else

    cpu_set_t cpuset;
    pthread_t thread = pthread_self();
    CPU_ZERO(&cpuset);
    CPU_SET(threadId, &cpuset);

    pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);

#endif
}

INLINE
uint32_t GetEnqueuedDraw(SWR_CONTEXT *pContext)
{
    return pContext->dcRing.GetHead();
}

INLINE
DRAW_CONTEXT *GetDC(SWR_CONTEXT *pContext, uint32_t drawId)
{
    return &pContext->dcRing[(drawId-1) % KNOB_MAX_DRAWS_IN_FLIGHT];
}

INLINE
bool IDComparesLess(uint32_t a, uint32_t b)
{
    // Use signed delta to ensure that wrap-around to 0 is correctly handled.
    int32_t delta = int32_t(a - b);
    return (delta < 0);
}

// returns true if dependency not met
INLINE
bool CheckDependency(SWR_CONTEXT *pContext, DRAW_CONTEXT *pDC, uint32_t lastRetiredDraw)
{
    return pDC->dependent && IDComparesLess(lastRetiredDraw, pDC->drawId - 1);
}

bool CheckDependencyFE(SWR_CONTEXT *pContext, DRAW_CONTEXT *pDC, uint32_t lastRetiredDraw)
{
    return pDC->dependentFE && IDComparesLess(lastRetiredDraw, pDC->drawId - 1);
}

//////////////////////////////////////////////////////////////////////////
/// @brief Update client stats.
INLINE void UpdateClientStats(SWR_CONTEXT* pContext, uint32_t workerId, DRAW_CONTEXT* pDC)
{
    if ((pContext->pfnUpdateStats == nullptr) || (GetApiState(pDC).enableStatsBE == false))
    {
        return;
    }

    DRAW_DYNAMIC_STATE& dynState = pDC->dynState;
    SWR_STATS stats{ 0 };

    // Sum up stats across all workers before sending to client.
    for (uint32_t i = 0; i < pContext->NumWorkerThreads; ++i)
    {
        stats.DepthPassCount += dynState.pStats[i].DepthPassCount;

        stats.PsInvocations  += dynState.pStats[i].PsInvocations;
        stats.CsInvocations  += dynState.pStats[i].CsInvocations;
    }


    pContext->pfnUpdateStats(GetPrivateState(pDC), &stats);
}

INLINE void ExecuteCallbacks(SWR_CONTEXT* pContext, uint32_t workerId, DRAW_CONTEXT* pDC)
{
    UpdateClientStats(pContext, workerId, pDC);

    if (pDC->retireCallback.pfnCallbackFunc)
    {
        pDC->retireCallback.pfnCallbackFunc(pDC->retireCallback.userData,
            pDC->retireCallback.userData2,
            pDC->retireCallback.userData3);
    }
}

// inlined-only version
INLINE int32_t CompleteDrawContextInl(SWR_CONTEXT* pContext, uint32_t workerId, DRAW_CONTEXT* pDC)
{
    int32_t result = InterlockedDecrement((volatile LONG*)&pDC->threadsDone);
    SWR_ASSERT(result >= 0);

    if (result == 0)
    {
        ExecuteCallbacks(pContext, workerId, pDC);

        // Cleanup memory allocations
        pDC->pArena->Reset(true);
        if (!pDC->isCompute)
        {
            pDC->pTileMgr->initialize();
        }
        if (pDC->cleanupState)
        {
            pDC->pState->pArena->Reset(true);
        }

        _ReadWriteBarrier();

        pContext->dcRing.Dequeue();  // Remove from tail
    }

    return result;
}

// available to other translation modules
int32_t CompleteDrawContext(SWR_CONTEXT* pContext, DRAW_CONTEXT* pDC)
{
    return CompleteDrawContextInl(pContext, 0, pDC);
}

INLINE bool FindFirstIncompleteDraw(SWR_CONTEXT* pContext, uint32_t workerId, uint32_t& curDrawBE, uint32_t& drawEnqueued)
{
    // increment our current draw id to the first incomplete draw
    drawEnqueued = GetEnqueuedDraw(pContext);
    while (IDComparesLess(curDrawBE, drawEnqueued))
    {
        DRAW_CONTEXT *pDC = &pContext->dcRing[curDrawBE % KNOB_MAX_DRAWS_IN_FLIGHT];

        // If its not compute and FE is not done then break out of loop.
        if (!pDC->doneFE && !pDC->isCompute) break;

        bool isWorkComplete = pDC->isCompute ?
            pDC->pDispatch->isWorkComplete() :
            pDC->pTileMgr->isWorkComplete();

        if (isWorkComplete)
        {
            curDrawBE++;
            CompleteDrawContextInl(pContext, workerId, pDC);
        }
        else
        {
            break;
        }
    }

    // If there are no more incomplete draws then return false.
    return IDComparesLess(curDrawBE, drawEnqueued);
}

//////////////////////////////////////////////////////////////////////////
/// @brief If there is any BE work then go work on it.
/// @param pContext - pointer to SWR context.
/// @param workerId - The unique worker ID that is assigned to this thread.
/// @param curDrawBE - This tracks the draw contexts that this thread has processed. Each worker thread
///                    has its own curDrawBE counter and this ensures that each worker processes all the
///                    draws in order.
/// @param lockedTiles - This is the set of tiles locked by other threads. Each thread maintains its
///                      own set and each time it fails to lock a macrotile, because its already locked,
///                      then it will add that tile to the lockedTiles set. As a worker begins to work
///                      on future draws the lockedTiles ensure that it doesn't work on tiles that may
///                      still have work pending in a previous draw. Additionally, the lockedTiles is
///                      hueristic that can steer a worker back to the same macrotile that it had been
///                      working on in a previous draw.
/// @returns        true if worker thread should shutdown
bool WorkOnFifoBE(
    SWR_CONTEXT *pContext,
    uint32_t workerId,
    uint32_t &curDrawBE,
    TileSet& lockedTiles,
    uint32_t numaNode,
    uint32_t numaMask)
{
    bool bShutdown = false;

    // Find the first incomplete draw that has pending work. If no such draw is found then
    // return. FindFirstIncompleteDraw is responsible for incrementing the curDrawBE.
    uint32_t drawEnqueued = 0;
    if (FindFirstIncompleteDraw(pContext, workerId, curDrawBE, drawEnqueued) == false)
    {
        return false;
    }

    uint32_t lastRetiredDraw = pContext->dcRing[curDrawBE % KNOB_MAX_DRAWS_IN_FLIGHT].drawId - 1;

    // Reset our history for locked tiles. We'll have to re-learn which tiles are locked.
    lockedTiles.clear();

    // Try to work on each draw in order of the available draws in flight.
    //   1. If we're on curDrawBE, we can work on any macrotile that is available.
    //   2. If we're trying to work on draws after curDrawBE, we are restricted to 
    //      working on those macrotiles that are known to be complete in the prior draw to
    //      maintain order. The locked tiles provides the history to ensures this.
    for (uint32_t i = curDrawBE; IDComparesLess(i, drawEnqueued); ++i)
    {
        DRAW_CONTEXT *pDC = &pContext->dcRing[i % KNOB_MAX_DRAWS_IN_FLIGHT];

        if (pDC->isCompute) return false; // We don't look at compute work.

        // First wait for FE to be finished with this draw. This keeps threading model simple
        // but if there are lots of bubbles between draws then serializing FE and BE may
        // need to be revisited.
        if (!pDC->doneFE) return false;
        
        // If this draw is dependent on a previous draw then we need to bail.
        if (CheckDependency(pContext, pDC, lastRetiredDraw))
        {
            return false;
        }

        // Grab the list of all dirty macrotiles. A tile is dirty if it has work queued to it.
        auto &macroTiles = pDC->pTileMgr->getDirtyTiles();

        for (auto tile : macroTiles)
        {
            uint32_t tileID = tile->mId;

            // Only work on tiles for this numa node
            uint32_t x, y;
            pDC->pTileMgr->getTileIndices(tileID, x, y);
            if (((x ^ y) & numaMask) != numaNode)
            {
                continue;
            }

            if (!tile->getNumQueued())
            {
                continue;
            }

            // can only work on this draw if it's not in use by other threads
            if (lockedTiles.find(tileID) != lockedTiles.end())
            {
                continue;
            }

            if (tile->tryLock())
            {
                BE_WORK *pWork;

                AR_BEGIN(WorkerFoundWork, pDC->drawId);

                uint32_t numWorkItems = tile->getNumQueued();
                SWR_ASSERT(numWorkItems);

                pWork = tile->peek();
                SWR_ASSERT(pWork);
                if (pWork->type == DRAW)
                {
                    pContext->pHotTileMgr->InitializeHotTiles(pContext, pDC, workerId, tileID);
                }
                else if (pWork->type == SHUTDOWN)
                {
                    bShutdown = true;
                }

                while ((pWork = tile->peek()) != nullptr)
                {
                    pWork->pfnWork(pDC, workerId, tileID, &pWork->desc);
                    tile->dequeue();
                }
                AR_END(WorkerFoundWork, numWorkItems);

                _ReadWriteBarrier();

                pDC->pTileMgr->markTileComplete(tileID);

                // Optimization: If the draw is complete and we're the last one to have worked on it then
                // we can reset the locked list as we know that all previous draws before the next are guaranteed to be complete.
                if ((curDrawBE == i) && (bShutdown || pDC->pTileMgr->isWorkComplete()))
                {
                    // We can increment the current BE and safely move to next draw since we know this draw is complete.
                    curDrawBE++;
                    CompleteDrawContextInl(pContext, workerId, pDC);

                    lastRetiredDraw++;

                    lockedTiles.clear();
                    break;
                }

                if (bShutdown)
                {
                    break;
                }
            }
            else
            {
                // This tile is already locked. So let's add it to our locked tiles set. This way we don't try locking this one again.
                lockedTiles.insert(tileID);
            }
        }
    }

    return bShutdown;
}

//////////////////////////////////////////////////////////////////////////
/// @brief Called when FE work is complete for this DC.
INLINE void CompleteDrawFE(SWR_CONTEXT* pContext, uint32_t workerId, DRAW_CONTEXT* pDC)
{
    if (pContext->pfnUpdateStatsFE && GetApiState(pDC).enableStatsFE)
    {
        SWR_STATS_FE& stats = pDC->dynState.statsFE;

        AR_EVENT(FrontendStatsEvent(pDC->drawId,
            stats.IaVertices, stats.IaPrimitives, stats.VsInvocations, stats.HsInvocations,
            stats.DsInvocations, stats.GsInvocations, stats.GsPrimitives, stats.CInvocations, stats.CPrimitives,
            stats.SoPrimStorageNeeded[0], stats.SoPrimStorageNeeded[1], stats.SoPrimStorageNeeded[2], stats.SoPrimStorageNeeded[3],
            stats.SoNumPrimsWritten[0], stats.SoNumPrimsWritten[1], stats.SoNumPrimsWritten[2], stats.SoNumPrimsWritten[3]
        ));
		AR_EVENT(FrontendDrawEndEvent(pDC->drawId));

        pContext->pfnUpdateStatsFE(GetPrivateState(pDC), &stats);
    }

    if (pContext->pfnUpdateSoWriteOffset)
    {
        for (uint32_t i = 0; i < MAX_SO_BUFFERS; ++i)
        {
            if ((pDC->dynState.SoWriteOffsetDirty[i]) &&
                (pDC->pState->state.soBuffer[i].soWriteEnable))
            {
                pContext->pfnUpdateSoWriteOffset(GetPrivateState(pDC), i, pDC->dynState.SoWriteOffset[i]);
            }
        }
    }

    // Ensure all streaming writes are globally visible before marking this FE done
    _mm_mfence();
    pDC->doneFE = true;

    InterlockedDecrement((volatile LONG*)&pContext->drawsOutstandingFE);
}

void WorkOnFifoFE(SWR_CONTEXT *pContext, uint32_t workerId, uint32_t &curDrawFE)
{
    // Try to grab the next DC from the ring
    uint32_t drawEnqueued = GetEnqueuedDraw(pContext);
    while (IDComparesLess(curDrawFE, drawEnqueued))
    {
        uint32_t dcSlot = curDrawFE % KNOB_MAX_DRAWS_IN_FLIGHT;
        DRAW_CONTEXT *pDC = &pContext->dcRing[dcSlot];
        if (pDC->isCompute || pDC->doneFE)
        {
            CompleteDrawContextInl(pContext, workerId, pDC);
            curDrawFE++;
        }
        else
        {
            break;
        }
    }

    uint32_t lastRetiredFE = curDrawFE - 1;
    uint32_t curDraw = curDrawFE;
    while (IDComparesLess(curDraw, drawEnqueued))
    {
        uint32_t dcSlot = curDraw % KNOB_MAX_DRAWS_IN_FLIGHT;
        DRAW_CONTEXT *pDC = &pContext->dcRing[dcSlot];

        if (!pDC->isCompute && !pDC->FeLock)
        {
            if (CheckDependencyFE(pContext, pDC, lastRetiredFE))
            {
                return;
            }

            uint32_t initial = InterlockedCompareExchange((volatile uint32_t*)&pDC->FeLock, 1, 0);
            if (initial == 0)
            {
                // successfully grabbed the DC, now run the FE
                pDC->FeWork.pfnWork(pContext, pDC, workerId, &pDC->FeWork.desc);

                CompleteDrawFE(pContext, workerId, pDC);
            }
        }
        curDraw++;
    }
}

//////////////////////////////////////////////////////////////////////////
/// @brief If there is any compute work then go work on it.
/// @param pContext - pointer to SWR context.
/// @param workerId - The unique worker ID that is assigned to this thread.
/// @param curDrawBE - This tracks the draw contexts that this thread has processed. Each worker thread
///                    has its own curDrawBE counter and this ensures that each worker processes all the
///                    draws in order.
void WorkOnCompute(
    SWR_CONTEXT *pContext,
    uint32_t workerId,
    uint32_t& curDrawBE)
{
    uint32_t drawEnqueued = 0;
    if (FindFirstIncompleteDraw(pContext, workerId, curDrawBE, drawEnqueued) == false)
    {
        return;
    }

    uint32_t lastRetiredDraw = pContext->dcRing[curDrawBE % KNOB_MAX_DRAWS_IN_FLIGHT].drawId - 1;

    for (uint64_t i = curDrawBE; IDComparesLess(i, drawEnqueued); ++i)
    {
        DRAW_CONTEXT *pDC = &pContext->dcRing[i % KNOB_MAX_DRAWS_IN_FLIGHT];
        if (pDC->isCompute == false) return;

        // check dependencies
        if (CheckDependency(pContext, pDC, lastRetiredDraw))
        {
            return;
        }

        SWR_ASSERT(pDC->pDispatch != nullptr);
        DispatchQueue& queue = *pDC->pDispatch;

        // Is there any work remaining?
        if (queue.getNumQueued() > 0)
        {
            void* pSpillFillBuffer = nullptr;
            uint32_t threadGroupId = 0;
            while (queue.getWork(threadGroupId))
            {
                queue.dispatch(pDC, workerId, threadGroupId, pSpillFillBuffer);
                queue.finishedWork();
            }

            // Ensure all streaming writes are globally visible before moving onto the next draw
            _mm_mfence();
        }
    }
}

template<bool IsFEThread, bool IsBEThread>
DWORD workerThreadMain(LPVOID pData)
{
    THREAD_DATA *pThreadData = (THREAD_DATA*)pData;
    SWR_CONTEXT *pContext = pThreadData->pContext;
    uint32_t threadId = pThreadData->threadId;
    uint32_t workerId = pThreadData->workerId;

    bindThread(pContext, threadId, pThreadData->procGroupId, pThreadData->forceBindProcGroup); 

    RDTSC_INIT(threadId);

    uint32_t numaNode = pThreadData->numaId;
    uint32_t numaMask = pContext->threadPool.numaMask;

    // flush denormals to 0
    _mm_setcsr(_mm_getcsr() | _MM_FLUSH_ZERO_ON | _MM_DENORMALS_ZERO_ON);

    // Track tiles locked by other threads. If we try to lock a macrotile and find its already
    // locked then we'll add it to this list so that we don't try and lock it again.
    TileSet lockedTiles;

    // each worker has the ability to work on any of the queued draws as long as certain
    // conditions are met. the data associated
    // with a draw is guaranteed to be active as long as a worker hasn't signaled that he 
    // has moved on to the next draw when he determines there is no more work to do. The api
    // thread will not increment the head of the dc ring until all workers have moved past the
    // current head.
    // the logic to determine what to work on is:
    // 1- try to work on the FE any draw that is queued. For now there are no dependencies
    //    on the FE work, so any worker can grab any FE and process in parallel.  Eventually
    //    we'll need dependency tracking to force serialization on FEs.  The worker will try
    //    to pick an FE by atomically incrementing a counter in the swr context.  he'll keep
    //    trying until he reaches the tail.
    // 2- BE work must be done in strict order. we accomplish this today by pulling work off
    //    the oldest draw (ie the head) of the dcRing. the worker can determine if there is
    //    any work left by comparing the total # of binned work items and the total # of completed
    //    work items. If they are equal, then there is no more work to do for this draw, and
    //    the worker can safely increment its oldestDraw counter and move on to the next draw.
    std::unique_lock<std::mutex> lock(pContext->WaitLock, std::defer_lock);

    auto threadHasWork = [&](uint32_t curDraw) { return curDraw != pContext->dcRing.GetHead(); };

    uint32_t curDrawBE = 0;
    uint32_t curDrawFE = 0;

    bool bShutdown = false;

    while (true)
    {
        if (bShutdown && !threadHasWork(curDrawBE))
        {
            break;
        }

        uint32_t loop = 0;
        while (loop++ < KNOB_WORKER_SPIN_LOOP_COUNT && !threadHasWork(curDrawBE))
        {
            _mm_pause();
        }

        if (!threadHasWork(curDrawBE))
        {
            lock.lock();

            // check for thread idle condition again under lock
            if (threadHasWork(curDrawBE))
            {
                lock.unlock();
                continue;
            }

            pContext->FifosNotEmpty.wait(lock);
            lock.unlock();
        }

        if (IsBEThread)
        {
            AR_BEGIN(WorkerWorkOnFifoBE, 0);
            bShutdown |= WorkOnFifoBE(pContext, workerId, curDrawBE, lockedTiles, numaNode, numaMask);
            AR_END(WorkerWorkOnFifoBE, 0);

            WorkOnCompute(pContext, workerId, curDrawBE);
        }

        if (IsFEThread)
        {
            WorkOnFifoFE(pContext, workerId, curDrawFE);

            if (!IsBEThread)
            {
                curDrawBE = curDrawFE;
            }
        }
    }

    return 0;
}
template<> DWORD workerThreadMain<false, false>(LPVOID) = delete;

template <bool IsFEThread, bool IsBEThread>
DWORD workerThreadInit(LPVOID pData)
{
#if defined(_WIN32)
    __try
#endif // _WIN32
    {
        return workerThreadMain<IsFEThread, IsBEThread>(pData);
    }

#if defined(_WIN32)
    __except(EXCEPTION_CONTINUE_SEARCH)
    {
    }

#endif // _WIN32

    return 1;
}
template<> DWORD workerThreadInit<false, false>(LPVOID pData) = delete;

//////////////////////////////////////////////////////////////////////////
/// @brief Creates thread pool info but doesn't launch threads.
/// @param pContext - pointer to context
/// @param pPool - pointer to thread pool object.
void CreateThreadPool(SWR_CONTEXT* pContext, THREAD_POOL* pPool)
{
    bindThread(pContext, 0);

    CPUNumaNodes nodes;
    uint32_t numThreadsPerProcGroup = 0;
    CalculateProcessorTopology(nodes, numThreadsPerProcGroup);

    uint32_t numHWNodes         = (uint32_t)nodes.size();
    uint32_t numHWCoresPerNode  = (uint32_t)nodes[0].cores.size();
    uint32_t numHWHyperThreads  = (uint32_t)nodes[0].cores[0].threadIds.size();

    // Calculate num HW threads.  Due to asymmetric topologies, this is not
    // a trivial multiplication.
    uint32_t numHWThreads = 0;
    for (auto& node : nodes)
    {
        for (auto& core : node.cores)
        {
            numHWThreads += (uint32_t)core.threadIds.size();
        }
    }

    uint32_t numNodes           = numHWNodes;
    uint32_t numCoresPerNode    = numHWCoresPerNode;
    uint32_t numHyperThreads    = numHWHyperThreads;

    if (pContext->threadInfo.MAX_NUMA_NODES)
    {
        numNodes = std::min(numNodes, pContext->threadInfo.MAX_NUMA_NODES);
    }

    if (pContext->threadInfo.MAX_CORES_PER_NUMA_NODE)
    {
        numCoresPerNode = std::min(numCoresPerNode, pContext->threadInfo.MAX_CORES_PER_NUMA_NODE);
    }

    if (pContext->threadInfo.MAX_THREADS_PER_CORE)
    {
        numHyperThreads = std::min(numHyperThreads, pContext->threadInfo.MAX_THREADS_PER_CORE);
    }

#if defined(_WIN32) && !defined(_WIN64)
    if (!pContext->threadInfo.MAX_WORKER_THREADS)
    {
        // Limit 32-bit windows to bindable HW threads only
        if ((numCoresPerNode * numHWHyperThreads) > 32)
        {
            numCoresPerNode = 32 / numHWHyperThreads;
        }
    }
#endif

    // Calculate numThreads
    uint32_t numThreads = numNodes * numCoresPerNode * numHyperThreads;
    numThreads = std::min(numThreads, numHWThreads);

    if (pContext->threadInfo.MAX_WORKER_THREADS)
    {
        uint32_t maxHWThreads = numHWNodes * numHWCoresPerNode * numHWHyperThreads;
        numThreads = std::min(pContext->threadInfo.MAX_WORKER_THREADS, maxHWThreads);
    }

    uint32_t numAPIReservedThreads = 1;


    if (numThreads == 1)
    {
        // If only 1 worker threads, try to move it to an available
        // HW thread.  If that fails, use the API thread.
        if (numCoresPerNode < numHWCoresPerNode)
        {
            numCoresPerNode++;
        }
        else if (numHyperThreads < numHWHyperThreads)
        {
            numHyperThreads++;
        }
        else if (numNodes < numHWNodes)
        {
            numNodes++;
        }
        else
        {
            pContext->threadInfo.SINGLE_THREADED = true;
        }
    }
    else
    {
        // Save HW threads for the API if we can
        if (numThreads > numAPIReservedThreads)
        {
            numThreads -= numAPIReservedThreads;
        }
        else
        {
            numAPIReservedThreads = 0;
        }
    }

    if (pContext->threadInfo.SINGLE_THREADED)
    {
        numThreads = 1;
    }

    // Initialize DRAW_CONTEXT's per-thread stats
    for (uint32_t dc = 0; dc < KNOB_MAX_DRAWS_IN_FLIGHT; ++dc)
    {
        pContext->dcRing[dc].dynState.pStats = new SWR_STATS[numThreads];
        memset(pContext->dcRing[dc].dynState.pStats, 0, sizeof(SWR_STATS) * numThreads);
    }

    if (pContext->threadInfo.SINGLE_THREADED)
    {
        pContext->NumWorkerThreads = 1;
        pContext->NumFEThreads = 1;
        pContext->NumBEThreads = 1;
        pPool->numThreads = 0;

        return;
    }

    pPool->numThreads = numThreads;
    pContext->NumWorkerThreads = pPool->numThreads;

    pPool->pThreadData = (THREAD_DATA *)malloc(pPool->numThreads * sizeof(THREAD_DATA));
    pPool->numaMask = 0;

    pPool->pThreads = new THREAD_PTR[pPool->numThreads];

    if (pContext->threadInfo.MAX_WORKER_THREADS)
    {
        bool bForceBindProcGroup = (numThreads > numThreadsPerProcGroup);
        uint32_t numProcGroups = (numThreads + numThreadsPerProcGroup - 1) / numThreadsPerProcGroup;
        // When MAX_WORKER_THREADS is set we don't bother to bind to specific HW threads
        // But Windows will still require binding to specific process groups
        for (uint32_t workerId = 0; workerId < numThreads; ++workerId)
        {
            pPool->pThreadData[workerId].workerId = workerId;
            pPool->pThreadData[workerId].procGroupId = workerId % numProcGroups;
            pPool->pThreadData[workerId].threadId = 0;
            pPool->pThreadData[workerId].numaId = 0;
            pPool->pThreadData[workerId].coreId = 0;
            pPool->pThreadData[workerId].htId = 0;
            pPool->pThreadData[workerId].pContext = pContext;
            pPool->pThreadData[workerId].forceBindProcGroup = bForceBindProcGroup;

            pContext->NumBEThreads++;
            pContext->NumFEThreads++;
        }
    }
    else
    {
        pPool->numaMask = numNodes - 1; // Only works for 2**n numa nodes (1, 2, 4, etc.)

        uint32_t workerId = 0;
        for (uint32_t n = 0; n < numNodes; ++n)
        {
            auto& node = nodes[n];
            uint32_t numCores = numCoresPerNode;
            for (uint32_t c = 0; c < numCores; ++c)
            {
                if (c >= node.cores.size())
                {
                    break;
                }

                auto& core = node.cores[c];
                for (uint32_t t = 0; t < numHyperThreads; ++t)
                {
                    if (t >= core.threadIds.size())
                    {
                        break;
                    }

                    if (numAPIReservedThreads)
                    {
                        --numAPIReservedThreads;
                        continue;
                    }

                    SWR_ASSERT(workerId < numThreads);

                    pPool->pThreadData[workerId].workerId = workerId;
                    pPool->pThreadData[workerId].procGroupId = core.procGroup;
                    pPool->pThreadData[workerId].threadId = core.threadIds[t];
                    pPool->pThreadData[workerId].numaId = n;
                    pPool->pThreadData[workerId].coreId = c;
                    pPool->pThreadData[workerId].htId = t;
                    pPool->pThreadData[workerId].pContext = pContext;

                    pContext->NumBEThreads++;
                    pContext->NumFEThreads++;

                    ++workerId;
                }
            }
        }
        SWR_ASSERT(workerId == pContext->NumWorkerThreads);
    }
}

//////////////////////////////////////////////////////////////////////////
/// @brief Launches worker threads in thread pool.
/// @param pContext - pointer to context
/// @param pPool - pointer to thread pool object.
void StartThreadPool(SWR_CONTEXT* pContext, THREAD_POOL* pPool)
{
    if (pContext->threadInfo.SINGLE_THREADED)
    {
        return;
    }

    for (uint32_t workerId = 0; workerId < pContext->NumWorkerThreads; ++workerId)
    {
        pPool->pThreads[workerId] = new std::thread(workerThreadInit<true, true>, &pPool->pThreadData[workerId]);
    }
}

//////////////////////////////////////////////////////////////////////////
/// @brief Destroys thread pool.
/// @param pContext - pointer to context
/// @param pPool - pointer to thread pool object.
void DestroyThreadPool(SWR_CONTEXT *pContext, THREAD_POOL *pPool)
{
    if (!pContext->threadInfo.SINGLE_THREADED)
    {
        // Wait for all threads to finish
        SwrWaitForIdle(pContext);

        // Wait for threads to finish and destroy them
        for (uint32_t t = 0; t < pPool->numThreads; ++t)
        {
            // Detach from thread.  Cannot join() due to possibility (in Windows) of code
            // in some DLLMain(THREAD_DETATCH case) blocking the thread until after this returns.
            pPool->pThreads[t]->detach();
            delete(pPool->pThreads[t]);
        }

        delete [] pPool->pThreads;

        // Clean up data used by threads
        free(pPool->pThreadData);
    }
}
