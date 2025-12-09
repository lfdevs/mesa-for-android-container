/*
 * Copyright © Microsoft Corporation
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#pragma once

#include <vector>

#include "gallium/include/frontend/sw_winsys.h"
#include "gallium/winsys/sw/null/null_sw_winsys.h"
#include "util/u_video.h"
#include "vl/vl_winsys.h"

#include <unknwn.h>
#include <windows.h>

#include <d3d12.h>
#include <mfobjects.h>
#include <mutex>
#include "pipe_headers.h"
#include "staticasynccallback.h"


class stats_buffer_manager : public IUnknown
{
 public:
   // retrieve a new tracked buffer from the pool
   struct pipe_resource *get_new_tracked_buffer();

   // release a tracked buffer back to the pool
   void release_tracked_buffer( void *target );

   HRESULT __stdcall QueryInterface( const IID &riid, void **ppvObject ) override;
   ULONG __stdcall AddRef() override;
   ULONG __stdcall Release() override;

   HRESULT
   AttachPipeResourceAsSampleExtension( struct pipe_resource *pPipeRes, ID3D12CommandQueue *pSyncObjectQueue, IMFSample *pSample );

   static HRESULT Create( void *logId,
                          struct vl_screen *pVlScreen,
                          struct pipe_context *pPipeContext,
                          REFGUID guidExtension,
                          uint32_t width,
                          uint16_t height,
                          enum pipe_format buffer_format,
                          unsigned initial_pool_size,
                          unsigned max_pool_size,
                          stats_buffer_manager **ppInstance );

 private:
   stats_buffer_manager( void *logId,
                         struct vl_screen *m_pVlScreen,
                         pipe_context *pPipeContext,
                         REFGUID guidExtension,
                         uint32_t width,
                         uint16_t height,
                         enum pipe_format buffer_format,
                         unsigned initial_pool_size,
                         unsigned max_pool_size,
                         HRESULT &hr );

   ~stats_buffer_manager();

   STDMETHOD( OnSampleAvailable )( __in IMFAsyncResult *pAsyncResult );
   METHODASYNCCALLBACKEX( OnSampleAvailable, stats_buffer_manager, 0, MFASYNC_CALLBACK_QUEUE_MULTITHREADED );

   const void *m_logId = {};
   std::mutex m_lock;
   GUID m_resourceGUID {};
   ULONG m_refCount = 0;

   struct vl_screen *m_pVlScreen = nullptr;
   struct pipe_context *m_pPipeContext = nullptr;
   struct pipe_resource m_template = {};

   struct stats_buffer_manager_pool_entry
   {
      struct pipe_resource *buffer {};
      bool used {};
   };

   std::vector<struct stats_buffer_manager_pool_entry> m_pool;
};
