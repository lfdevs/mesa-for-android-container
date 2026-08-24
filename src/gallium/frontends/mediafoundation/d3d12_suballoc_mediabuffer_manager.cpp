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

#include <wrl/client.h>
#include "macros.h"
#include "wpptrace.h"

#include "d3d12_suballoc_mediabuffer_manager.h"

#include "d3d12_suballoc_mediabuffer_manager.tmh"

using namespace Microsoft::WRL;

HRESULT __stdcall CD3D12BitstreamMFBufferManager::QueryInterface( const IID &riid, void **ppvObject )
{
   if( riid == IID_IUnknown )
   {
      // If the requested IID is supported, return 'this' pointer
      *ppvObject = static_cast<IUnknown *>( this );
      // Must call AddRef because we are returning a valid interface pointer
      AddRef();
      return S_OK;
   }

   // Interface not supported
   *ppvObject = NULL;
   return E_NOINTERFACE;
}

ULONG __stdcall CD3D12BitstreamMFBufferManager::AddRef()
{
   return InterlockedIncrement( &m_refCount );
}

ULONG __stdcall CD3D12BitstreamMFBufferManager::Release()
{
   ULONG ulCount = InterlockedDecrement( &m_refCount );
   if( ulCount == 0 )
   {
      debug_printf( "[dx12 hmft 0x%p] destroying CD3D12BitstreamMFBufferManager\n", m_logId );
      delete this;
   }
   return ulCount;
}

HRESULT
CD3D12BitstreamMFBufferManager::CreateSample( d3d12_mediabuffer_manager_pool_entry &entry )
{
   HRESULT hr = S_OK;
   HMFT_ETW_EVENT_START( "CD3D12BitstreamMFBufferManagerCreateSample", this );
   D3D12_RESOURCE_DESC desc;
   // sliced buffers + notifications with multiple individual buffers per slice
   // or, full frame bitstream (when num_output_buffers == 1)
   desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
   desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
   desc.Width = m_template.width0;
   desc.Height = 1;
   desc.DepthOrArraySize = 1;
   desc.MipLevels = 1;
   desc.Format = DXGI_FORMAT_UNKNOWN;
   desc.SampleDesc.Count = 1;
   desc.SampleDesc.Quality = 0;
   desc.Flags = D3D12_RESOURCE_FLAG_NONE;
   desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

   D3D12_HEAP_PROPERTIES heap_props = m_spD3D12Device->GetCustomHeapProperties( 0, D3D12_HEAP_TYPE_READBACK );

   hr = m_spD3D12Device->CreateCommittedResource( &heap_props,
                                                  D3D12_HEAP_FLAG_NONE,
                                                  &desc,
                                                  D3D12_RESOURCE_STATE_COMMON,
                                                  nullptr,
                                                  IID_PPV_ARGS( &entry.d3d12_resource ) );
   HMFT_ETW_EVENT_STOP( "CD3D12BitstreamMFBufferManagerCreateSample", this );
   if( FAILED( hr ) )
   {
      MFE_ERROR( "[dx12 hmft 0x%p] CreateCommittedResource failed hr=0x%08x", m_logId, hr );
   }
   return hr;
}

HRESULT
CD3D12BitstreamMFBufferManager::Create( void *logId,
                                        ID3D12Device *pD3D12Device,
                                        const struct pipe_resource &templ,
                                        unsigned initial_pool_size,
                                        unsigned max_pool_size,
                                        CD3D12BitstreamMFBufferManager **ppInstance )
{
   if( !ppInstance )
      return E_INVALIDARG;

   *ppInstance = nullptr;

   assert( initial_pool_size <= max_pool_size );

   HRESULT hr;
   auto pInstance =
      new ( std::nothrow ) CD3D12BitstreamMFBufferManager( logId, pD3D12Device, templ, initial_pool_size, max_pool_size, hr );
   if( !pInstance )
      return E_OUTOFMEMORY;

   if( FAILED( hr ) )
   {
      delete pInstance;
      return hr;
   }

   pInstance->m_refCount = 1;
   *ppInstance = pInstance;
   return S_OK;
}

// retrieve a buffer from the pool
CD3D12ResourceHolder *
CD3D12BitstreamMFBufferManager::get_new_tracked_buffer()
{
   auto lock = std::lock_guard<std::mutex>( m_lock );
   bool found = false;
   CD3D12ResourceHolder *pResourceHolder = nullptr;
   for( auto &entry : m_pool )
   {
      if( !entry.used )
      {
         if( !entry.d3d12_resource )
         {
            HRESULT hr = CD3D12BitstreamMFBufferManager::CreateSample( entry );
            HMFT_ETW_EVENT_INFO( "D3D12BitstreamMFBufferManagerCreateSample", this );
            if( FAILED( hr ) )
            {
               MFE_ERROR( "[dx12 hmft 0x%p] CreateSample failed", m_logId );
               return NULL;
            }
         }
         HRESULT hr = CD3D12ResourceHolder::Create( m_logId, entry.d3d12_resource.Get(), m_template, this, &pResourceHolder );
         if( FAILED( hr ) )
         {
            MFE_ERROR( "[dx12 hmft 0x%p] CD3D12ResourceHolder::Create failed hr=0x%08x", m_logId, hr );
            return NULL;
         }
         m_outstandingTrackedBuffers++;
         entry.used = true;
         found = true;

         break;
      }
   }

   debug_printf( "[dx12 hmft 0x%p] get_new_tracked_buffer: %d buffers in use\n", m_logId, m_outstandingTrackedBuffers );
   if( !found )
   {
      HMFT_ETW_EVENT_INFO( "D3D12BitstreamMFBufferManagerFailedToFindSample", this );
      MFE_ERROR( "[dx12 hmft 0x%p] failed to find a free buffer in pool", m_logId );
   }
   return pResourceHolder;
}

// release a buffer back to the pool
void
CD3D12BitstreamMFBufferManager::release_tracked_buffer( void *target )
{
   auto lock = std::lock_guard<std::mutex>( m_lock );
   bool found = false;
   for( auto &entry : m_pool )
   {
      if( entry.d3d12_resource.Get() == target )
      {
         if( !entry.used )
         {
            MFE_ERROR( "[dx12 hmft 0x%p] attempted to release buffer that was not marked as used", m_logId );
         }
         else
         {
            m_outstandingTrackedBuffers--;
            entry.used = false;
            found = true;
         }
         break;
      }
   }

   debug_printf( "[dx12 hmft 0x%p] release_tracked_buffer: %d buffers in use\n", m_logId, m_outstandingTrackedBuffers );
   if( !found )
   {
      MFE_ERROR( "[dx12 hmft 0x%p] returned buffer was not found in the pool", m_logId );
   }
}

CD3D12BitstreamMFBufferManager::CD3D12BitstreamMFBufferManager( void *logId,
                                                                ID3D12Device *pD3D12Device,
                                                                const struct pipe_resource &templ,
                                                                unsigned initial_pool_size,
                                                                unsigned max_pool_size,
                                                                HRESULT &hr )
   : m_logId( logId ), m_spD3D12Device( pD3D12Device ), m_template( templ ), m_pool( max_pool_size )
{
   hr = S_OK;
   HMFT_ETW_EVENT_INFO( "m_spD3D12BitstreamMFBufferManagerCreate", this );
   unsigned buffer_count = 0;
   for( auto &entry : m_pool )
   {
      if( buffer_count < initial_pool_size )
      {
         hr = CD3D12BitstreamMFBufferManager::CreateSample( entry );
         if( FAILED( hr ) )
         {
            MFE_ERROR( "[dx12 hmft 0x%p] CreateSample failed during pool initialization, hr=0x%08x", m_logId, hr );
            break;
         }
         buffer_count++;
      }
   }
}

CD3D12BitstreamMFBufferManager::~CD3D12BitstreamMFBufferManager()
{ }

HRESULT __stdcall CD3D12ResourceHolder::QueryInterface( const IID &riid, void **ppvObject )
{
   if( riid == IID_IUnknown )
   {
      // If the requested IID is supported, return 'this' pointer
      *ppvObject = static_cast<IUnknown *>( this );
      // Must call AddRef because we are returning a valid interface pointer
      AddRef();
      return S_OK;
   }

   // Interface not supported
   *ppvObject = NULL;
   return E_NOINTERFACE;
}

ULONG __stdcall CD3D12ResourceHolder::AddRef()
{
   return InterlockedIncrement( &m_refCount );
}

ULONG __stdcall CD3D12ResourceHolder::Release()
{
   ULONG ulCount = InterlockedDecrement( &m_refCount );
   if( ulCount == 0 )
   {
      debug_printf( "[dx12 hmft 0x%p] destroying CD3D12ResourceHolder\n", m_logId );
      delete this;
   }
   return ulCount;
}

HRESULT
CD3D12ResourceHolder::Create( const void *logId,
                              ID3D12Resource *pD3D12Resource,
                              const struct pipe_resource &templ,
                              CD3D12BitstreamMFBufferManager *pD3D12BitstreamMFBufferManager,
                              CD3D12ResourceHolder **ppInstance )
{
   if( !ppInstance )
      return E_INVALIDARG;

   *ppInstance = nullptr;

   auto pInstance = new ( std::nothrow ) CD3D12ResourceHolder( logId, pD3D12Resource, templ, pD3D12BitstreamMFBufferManager );
   if( !pInstance )
      return E_OUTOFMEMORY;

   pInstance->m_refCount = 1;
   *ppInstance = pInstance;
   return S_OK;
}

CD3D12ResourceHolder::CD3D12ResourceHolder( const void *logId,
                                            ID3D12Resource *pD3D12Resource,
                                            const struct pipe_resource &templ,
                                            CD3D12BitstreamMFBufferManager *pD3D12BitstreamMFBufferManager )
   : m_logId( logId ),
     m_spD3D12Resource( pD3D12Resource ),
     m_template( templ ),
     m_spD3D12BitstreamMFBufferManager( pD3D12BitstreamMFBufferManager )
{ }

CD3D12ResourceHolder::~CD3D12ResourceHolder()
{
   if( m_spD3D12BitstreamMFBufferManager )
   {
      m_spD3D12BitstreamMFBufferManager->release_tracked_buffer( m_spD3D12Resource.Get() );
   }
}
