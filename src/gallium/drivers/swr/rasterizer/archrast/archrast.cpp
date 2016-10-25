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
    /// @brief Event handler that saves stat events to event files. This
    ///        handler filters out unwanted events.
    class EventHandlerStatsFile : public EventHandlerFile
    {
    public:
        EventHandlerStatsFile(uint32_t id) : EventHandlerFile(id) {}

        // These are events that we're not interested in saving in stats event files.
        virtual void handle(Start& event) {}
        virtual void handle(End& event) {}
    };

    static EventManager* FromHandle(HANDLE hThreadContext)
    {
        return reinterpret_cast<EventManager*>(hThreadContext);
    }

    // Construct an event manager and associate a handler with it.
    HANDLE CreateThreadContext()
    {
        // Can we assume single threaded here?
        static std::atomic<uint32_t> counter(0);
        uint32_t id = counter.fetch_add(1);

        EventManager* pManager = new EventManager();
        EventHandler* pHandler = new EventHandlerStatsFile(id);

        if (pManager && pHandler)
        {
            pManager->attach(pHandler);

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
    void dispatch(HANDLE hThreadContext, Event& event)
    {
        EventManager* pManager = FromHandle(hThreadContext);
        SWR_ASSERT(pManager != nullptr);

        pManager->dispatch(event);
    }
}
