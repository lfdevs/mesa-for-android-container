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

using namespace Microsoft::WRL;
using Microsoft::WRL::ComPtr;

struct pipe_resource *
AllocatePipeResourceFromD3D12Resource( ID3D12Resource *pD3D12Resource,
                                       struct pipe_screen *pScreen,
                                       const struct pipe_resource *templ );

class CD3D12BitstreamMFBufferManager;

class CD3D12ResourceHolder : public IUnknown
{
 public:
   HRESULT __stdcall QueryInterface( const IID &riid, void **ppvObject ) override;
   ULONG __stdcall AddRef() override;
   ULONG __stdcall Release() override;

   static HRESULT Create( const void *logId,
                          ID3D12Resource *pD3D12Resource,
                          const struct pipe_resource &templ,
                          CD3D12BitstreamMFBufferManager *pD3D12BitstreamMFBufferManager,
                          CD3D12ResourceHolder **ppInstance );

   struct pipe_resource *GetPipeResource( struct vl_screen *pVlScreen )
   {
      return AllocatePipeResourceFromD3D12Resource( m_spD3D12Resource.Get(), pVlScreen->pscreen, &m_template );
   }

   ID3D12Resource *GetResource()
   {
      return m_spD3D12Resource.Get();
   }

 private:
   CD3D12ResourceHolder( const void *logId,
                         ID3D12Resource *pD3D12Resource,
                         const struct pipe_resource &templ,
                         CD3D12BitstreamMFBufferManager *pD3D12BitstreamMFBufferManager );
   ~CD3D12ResourceHolder();

   const void *m_logId = {};
   std::mutex m_lock;
   ULONG m_refCount = 0;

   ComPtr<ID3D12Resource> m_spD3D12Resource;
   struct pipe_resource m_template = {};

   ComPtr<CD3D12BitstreamMFBufferManager> m_spD3D12BitstreamMFBufferManager;
};

class CD3D12BitstreamMFBufferManager : public IUnknown
{
 public:
   // retrieve a new tracked buffer from the pool
   CD3D12ResourceHolder *get_new_tracked_buffer();

   // release a tracked buffer back to the pool
   void release_tracked_buffer( void *target );

   HRESULT __stdcall QueryInterface( const IID &riid, void **ppvObject ) override;
   ULONG __stdcall AddRef() override;
   ULONG __stdcall Release() override;

   static HRESULT Create( void *logId,
                          ID3D12Device *pD3D12Device,
                          const struct pipe_resource &m_template,
                          unsigned initial_pool_size,
                          unsigned max_pool_size,
                          CD3D12BitstreamMFBufferManager **ppInstance );

   const uint32_t GetWidth0()
   {
      return m_template.width0;
   }

 private:
   CD3D12BitstreamMFBufferManager( void *logId,
                                   ID3D12Device *pD3D12Device,
                                   const struct pipe_resource &m_template,
                                   unsigned initial_pool_size,
                                   unsigned max_pool_size,
                                   HRESULT &hr );

   ~CD3D12BitstreamMFBufferManager();

   const void *m_logId = {};
   std::mutex m_lock;
   ULONG m_refCount = 0;

   ULONG m_outstandingTrackedBuffers {};

   ComPtr<ID3D12Device> m_spD3D12Device = nullptr;
   struct pipe_resource m_template = {};

   struct d3d12_mediabuffer_manager_pool_entry
   {
      ComPtr<ID3D12Resource> d3d12_resource {};
      bool used {};
   };

   HRESULT CreateSample( d3d12_mediabuffer_manager_pool_entry &entry );
   std::vector<struct d3d12_mediabuffer_manager_pool_entry> m_pool;
};
