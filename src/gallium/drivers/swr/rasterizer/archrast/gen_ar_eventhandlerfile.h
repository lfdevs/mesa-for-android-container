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
* @file gen_ar_eventhandlerfile.h
*
* @brief Event handler interface.  auto-generated file
* 
* DO NOT EDIT
* 
******************************************************************************/
#pragma once

#include "common/os.h"
#include "gen_ar_eventhandler.h"
#include <fstream>
#include <sstream>

namespace ArchRast
{
    //////////////////////////////////////////////////////////////////////////
    /// EventHandlerFile - interface for handling events.
    //////////////////////////////////////////////////////////////////////////
    class EventHandlerFile : public EventHandler
    {
    public:
        EventHandlerFile(uint32_t id)
        : mBufOffset(0)
        {
#if defined(_WIN32)
            DWORD pid = GetCurrentProcessId();
            TCHAR procname[MAX_PATH];
            GetModuleFileName(NULL, procname, MAX_PATH);
            const char* pBaseName = strrchr(procname, '\\');
            std::stringstream outDir;
            outDir << KNOB_DEBUG_OUTPUT_DIR << pBaseName << "_" << pid << std::ends;
            CreateDirectory(outDir.str().c_str(), NULL);

            char buf[255];
            // There could be multiple threads creating thread pools. We
            // want to make sure they are uniquly identified by adding in
            // the creator's thread id into the filename.
            sprintf(buf, "%s\\ar_event%d_%d.bin", outDir.str().c_str(), GetCurrentThreadId(), id);
            mFilename = std::string(buf);
#else
            char buf[255];
            // There could be multiple threads creating thread pools. We
            // want to make sure they are uniquly identified by adding in
            // the creator's thread id into the filename.
            sprintf(buf, "%s/ar_event%d_%d.bin", "/tmp", GetCurrentThreadId(), id);
            mFilename = std::string(buf);
#endif
        }

        virtual ~EventHandlerFile()
        {
            FlushBuffer();
        }

        //////////////////////////////////////////////////////////////////////////
        /// @brief Flush buffer to file.
        bool FlushBuffer()
        {
            if (mBufOffset > 0)
            {
                if (mBufOffset == mHeaderBufOffset)
                {
                    // Nothing to flush. Only header has been generated.
                    return false;
                }

                std::ofstream file;
                file.open(mFilename, std::ios::out | std::ios::app | std::ios::binary);

                if (!file.is_open())
                {
                    SWR_ASSERT(0, "ArchRast: Could not open event file!");
                    return false;
                }

                file.write((char*)mBuffer, mBufOffset);
                file.close();

                mBufOffset = 0;
                mHeaderBufOffset = 0; // Reset header offset so its no longer considered.
            }
            return true;
        }

        //////////////////////////////////////////////////////////////////////////
        /// @brief Write event and its payload to the memory buffer.
        void Write(uint32_t eventId, const char* pBlock, uint32_t size)
        {
            if ((mBufOffset + size + sizeof(eventId)) > mBufferSize)
            {
                if (!FlushBuffer())
                {
                    // Don't corrupt what's already in the buffer?
                    /// @todo Maybe add corrupt marker to buffer here in case we can open file in future?
                    return;
                }
            }

            memcpy(&mBuffer[mBufOffset], (char*)&eventId, sizeof(eventId));
            mBufOffset += sizeof(eventId);
            memcpy(&mBuffer[mBufOffset], pBlock, size);
            mBufOffset += size;
        }

        //////////////////////////////////////////////////////////////////////////
        /// @brief Handle Start event
        virtual void Handle(Start event)
        {
            Write(1, (char*)&event.data, sizeof(event.data));
        }
        //////////////////////////////////////////////////////////////////////////
        /// @brief Handle End event
        virtual void Handle(End event)
        {
            Write(2, (char*)&event.data, sizeof(event.data));
        }
        //////////////////////////////////////////////////////////////////////////
        /// @brief Handle ThreadStartApiEvent event
        virtual void Handle(ThreadStartApiEvent event)
        {
            Write(3, (char*)&event.data, 0);
        }
        //////////////////////////////////////////////////////////////////////////
        /// @brief Handle ThreadStartWorkerEvent event
        virtual void Handle(ThreadStartWorkerEvent event)
        {
            Write(4, (char*)&event.data, 0);
        }
        //////////////////////////////////////////////////////////////////////////
        /// @brief Handle DrawInstancedEvent event
        virtual void Handle(DrawInstancedEvent event)
        {
            Write(5, (char*)&event.data, sizeof(event.data));
        }
        //////////////////////////////////////////////////////////////////////////
        /// @brief Handle DrawIndexedInstancedEvent event
        virtual void Handle(DrawIndexedInstancedEvent event)
        {
            Write(6, (char*)&event.data, sizeof(event.data));
        }
        //////////////////////////////////////////////////////////////////////////
        /// @brief Handle DispatchEvent event
        virtual void Handle(DispatchEvent event)
        {
            Write(7, (char*)&event.data, sizeof(event.data));
        }
        //////////////////////////////////////////////////////////////////////////
        /// @brief Handle FrameEndEvent event
        virtual void Handle(FrameEndEvent event)
        {
            Write(8, (char*)&event.data, sizeof(event.data));
        }
        //////////////////////////////////////////////////////////////////////////
        /// @brief Handle FrontendStatsEvent event
        virtual void Handle(FrontendStatsEvent event)
        {
            Write(9, (char*)&event.data, sizeof(event.data));
        }
        //////////////////////////////////////////////////////////////////////////
        /// @brief Handle BackendStatsEvent event
        virtual void Handle(BackendStatsEvent event)
        {
            Write(10, (char*)&event.data, sizeof(event.data));
        }
        //////////////////////////////////////////////////////////////////////////
        /// @brief Handle EarlyDepthStencilInfoSingleSample event
        virtual void Handle(EarlyDepthStencilInfoSingleSample event)
        {
            Write(11, (char*)&event.data, sizeof(event.data));
        }
        //////////////////////////////////////////////////////////////////////////
        /// @brief Handle EarlyDepthStencilInfoSampleRate event
        virtual void Handle(EarlyDepthStencilInfoSampleRate event)
        {
            Write(12, (char*)&event.data, sizeof(event.data));
        }
        //////////////////////////////////////////////////////////////////////////
        /// @brief Handle EarlyDepthStencilInfoNullPS event
        virtual void Handle(EarlyDepthStencilInfoNullPS event)
        {
            Write(13, (char*)&event.data, sizeof(event.data));
        }
        //////////////////////////////////////////////////////////////////////////
        /// @brief Handle LateDepthStencilInfoSingleSample event
        virtual void Handle(LateDepthStencilInfoSingleSample event)
        {
            Write(14, (char*)&event.data, sizeof(event.data));
        }
        //////////////////////////////////////////////////////////////////////////
        /// @brief Handle LateDepthStencilInfoSampleRate event
        virtual void Handle(LateDepthStencilInfoSampleRate event)
        {
            Write(15, (char*)&event.data, sizeof(event.data));
        }
        //////////////////////////////////////////////////////////////////////////
        /// @brief Handle LateDepthStencilInfoNullPS event
        virtual void Handle(LateDepthStencilInfoNullPS event)
        {
            Write(16, (char*)&event.data, sizeof(event.data));
        }
        //////////////////////////////////////////////////////////////////////////
        /// @brief Handle EarlyDepthInfoPixelRate event
        virtual void Handle(EarlyDepthInfoPixelRate event)
        {
            Write(17, (char*)&event.data, sizeof(event.data));
        }
        //////////////////////////////////////////////////////////////////////////
        /// @brief Handle LateDepthInfoPixelRate event
        virtual void Handle(LateDepthInfoPixelRate event)
        {
            Write(18, (char*)&event.data, sizeof(event.data));
        }
        //////////////////////////////////////////////////////////////////////////
        /// @brief Handle BackendDrawEndEvent event
        virtual void Handle(BackendDrawEndEvent event)
        {
            Write(19, (char*)&event.data, sizeof(event.data));
        }
        //////////////////////////////////////////////////////////////////////////
        /// @brief Handle FrontendDrawEndEvent event
        virtual void Handle(FrontendDrawEndEvent event)
        {
            Write(20, (char*)&event.data, sizeof(event.data));
        }
        //////////////////////////////////////////////////////////////////////////
        /// @brief Handle EarlyZSingleSample event
        virtual void Handle(EarlyZSingleSample event)
        {
            Write(21, (char*)&event.data, sizeof(event.data));
        }
        //////////////////////////////////////////////////////////////////////////
        /// @brief Handle LateZSingleSample event
        virtual void Handle(LateZSingleSample event)
        {
            Write(22, (char*)&event.data, sizeof(event.data));
        }
        //////////////////////////////////////////////////////////////////////////
        /// @brief Handle EarlyStencilSingleSample event
        virtual void Handle(EarlyStencilSingleSample event)
        {
            Write(23, (char*)&event.data, sizeof(event.data));
        }
        //////////////////////////////////////////////////////////////////////////
        /// @brief Handle LateStencilSingleSample event
        virtual void Handle(LateStencilSingleSample event)
        {
            Write(24, (char*)&event.data, sizeof(event.data));
        }
        //////////////////////////////////////////////////////////////////////////
        /// @brief Handle EarlyZSampleRate event
        virtual void Handle(EarlyZSampleRate event)
        {
            Write(25, (char*)&event.data, sizeof(event.data));
        }
        //////////////////////////////////////////////////////////////////////////
        /// @brief Handle LateZSampleRate event
        virtual void Handle(LateZSampleRate event)
        {
            Write(26, (char*)&event.data, sizeof(event.data));
        }
        //////////////////////////////////////////////////////////////////////////
        /// @brief Handle EarlyStencilSampleRate event
        virtual void Handle(EarlyStencilSampleRate event)
        {
            Write(27, (char*)&event.data, sizeof(event.data));
        }
        //////////////////////////////////////////////////////////////////////////
        /// @brief Handle LateStencilSampleRate event
        virtual void Handle(LateStencilSampleRate event)
        {
            Write(28, (char*)&event.data, sizeof(event.data));
        }
        //////////////////////////////////////////////////////////////////////////
        /// @brief Handle EarlyZNullPS event
        virtual void Handle(EarlyZNullPS event)
        {
            Write(29, (char*)&event.data, sizeof(event.data));
        }
        //////////////////////////////////////////////////////////////////////////
        /// @brief Handle EarlyStencilNullPS event
        virtual void Handle(EarlyStencilNullPS event)
        {
            Write(30, (char*)&event.data, sizeof(event.data));
        }
        //////////////////////////////////////////////////////////////////////////
        /// @brief Handle EarlyZPixelRate event
        virtual void Handle(EarlyZPixelRate event)
        {
            Write(31, (char*)&event.data, sizeof(event.data));
        }
        //////////////////////////////////////////////////////////////////////////
        /// @brief Handle LateZPixelRate event
        virtual void Handle(LateZPixelRate event)
        {
            Write(32, (char*)&event.data, sizeof(event.data));
        }
        //////////////////////////////////////////////////////////////////////////
        /// @brief Handle EarlyOmZ event
        virtual void Handle(EarlyOmZ event)
        {
            Write(33, (char*)&event.data, sizeof(event.data));
        }
        //////////////////////////////////////////////////////////////////////////
        /// @brief Handle EarlyOmStencil event
        virtual void Handle(EarlyOmStencil event)
        {
            Write(34, (char*)&event.data, sizeof(event.data));
        }
        //////////////////////////////////////////////////////////////////////////
        /// @brief Handle LateOmZ event
        virtual void Handle(LateOmZ event)
        {
            Write(35, (char*)&event.data, sizeof(event.data));
        }
        //////////////////////////////////////////////////////////////////////////
        /// @brief Handle LateOmStencil event
        virtual void Handle(LateOmStencil event)
        {
            Write(36, (char*)&event.data, sizeof(event.data));
        }
        //////////////////////////////////////////////////////////////////////////
        /// @brief Handle GSPrimInfo event
        virtual void Handle(GSPrimInfo event)
        {
            Write(37, (char*)&event.data, sizeof(event.data));
        }
        //////////////////////////////////////////////////////////////////////////
        /// @brief Handle GSInputPrims event
        virtual void Handle(GSInputPrims event)
        {
            Write(38, (char*)&event.data, sizeof(event.data));
        }
        //////////////////////////////////////////////////////////////////////////
        /// @brief Handle GSPrimsGen event
        virtual void Handle(GSPrimsGen event)
        {
            Write(39, (char*)&event.data, sizeof(event.data));
        }
        //////////////////////////////////////////////////////////////////////////
        /// @brief Handle GSVertsInput event
        virtual void Handle(GSVertsInput event)
        {
            Write(40, (char*)&event.data, sizeof(event.data));
        }
        //////////////////////////////////////////////////////////////////////////
        /// @brief Handle ClipVertexCount event
        virtual void Handle(ClipVertexCount event)
        {
            Write(41, (char*)&event.data, sizeof(event.data));
        }
        //////////////////////////////////////////////////////////////////////////
        /// @brief Handle FlushVertClip event
        virtual void Handle(FlushVertClip event)
        {
            Write(42, (char*)&event.data, sizeof(event.data));
        }
        //////////////////////////////////////////////////////////////////////////
        /// @brief Handle VertsClipped event
        virtual void Handle(VertsClipped event)
        {
            Write(43, (char*)&event.data, sizeof(event.data));
        }
        //////////////////////////////////////////////////////////////////////////
        /// @brief Handle TessPrimCount event
        virtual void Handle(TessPrimCount event)
        {
            Write(44, (char*)&event.data, sizeof(event.data));
        }
        //////////////////////////////////////////////////////////////////////////
        /// @brief Handle TessPrimFlush event
        virtual void Handle(TessPrimFlush event)
        {
            Write(45, (char*)&event.data, sizeof(event.data));
        }
        //////////////////////////////////////////////////////////////////////////
        /// @brief Handle TessPrims event
        virtual void Handle(TessPrims event)
        {
            Write(46, (char*)&event.data, sizeof(event.data));
        }

        //////////////////////////////////////////////////////////////////////////
        /// @brief Everything written to buffer this point is the header.
        virtual void MarkHeader()
        {
            mHeaderBufOffset = mBufOffset;
        }

        std::string mFilename;

        static const uint32_t mBufferSize = 1024;
        uint8_t mBuffer[mBufferSize];
        uint32_t mBufOffset{0};
        uint32_t mHeaderBufOffset{0};
    };
}
