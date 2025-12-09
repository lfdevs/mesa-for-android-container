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

#include <mfapi.h>
#include <mfd3d12.h>
#include <mfidl.h>
#include <wrl/client.h>
#include "wpptrace.h"

#include "macros.h"
#include "stats_buffer_manager.h"
#include "stats_buffer_manager.tmh"

using namespace Microsoft::WRL;

HRESULT __stdcall stats_buffer_manager::QueryInterface( const IID &riid, void **ppvObject )
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

ULONG __stdcall stats_buffer_manager::AddRef()
{
   return InterlockedIncrement( &m_refCount );
}

ULONG __stdcall stats_buffer_manager::Release()
{
   ULONG ulCount = InterlockedDecrement( &m_refCount );
   if( ulCount == 0 )
   {
      delete this;
   }
   return ulCount;
}

HRESULT
stats_buffer_manager::Create( void *logId,
                              struct vl_screen *pVlScreen,
                              struct pipe_context *pPipeContext,
                              REFGUID guidExtension,
                              uint32_t width,
                              uint16_t height,
                              enum pipe_format buffer_format,
                              unsigned initial_pool_size,
                              unsigned max_pool_size,
                              stats_buffer_manager **ppInstance )
{
   if( !ppInstance )
      return E_INVALIDARG;

   *ppInstance = nullptr;

   assert( initial_pool_size <= max_pool_size );

   HRESULT hr;
   auto pInstance = new ( std::nothrow ) stats_buffer_manager( logId,
                                                               pVlScreen,
                                                               pPipeContext,
                                                               guidExtension,
                                                               width,
                                                               height,
                                                               buffer_format,
                                                               initial_pool_size,
                                                               max_pool_size,
                                                               hr );
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
struct pipe_resource *
stats_buffer_manager::get_new_tracked_buffer()
{
   auto lock = std::lock_guard<std::mutex>( m_lock );
   for( auto &entry : m_pool )
   {
      if( !entry.used )
      {
         if( !entry.buffer )
         {
            entry.buffer = m_pVlScreen->pscreen->resource_create( m_pVlScreen->pscreen, &m_template );
            if( !entry.buffer )
            {
               MFE_ERROR( "[dx12 hmft 0x%p] dynamic resource_create failed", m_logId );
               break;
            }
         }
         entry.used = true;
         return entry.buffer;
      }
   }

   MFE_ERROR( "[dx12 hmft 0x%p] failed to find a free buffer", m_logId );
   assert( false );   // Did not find an unused buffer
   return NULL;
}

// release a buffer back to the pool
void
stats_buffer_manager::release_tracked_buffer( void *target )
{
   auto lock = std::lock_guard<std::mutex>( m_lock );
   bool found = false;
   for( auto &entry : m_pool )
   {
      bool ret;
      struct winsys_handle whandle = {};
      whandle.type = WINSYS_HANDLE_TYPE_D3D12_RES;
      ret = m_pVlScreen->pscreen->resource_get_handle( m_pVlScreen->pscreen, m_pPipeContext, entry.buffer, &whandle, 0u );
      if( !ret )
      {
         MFE_ERROR( "[dx12 hmft 0x%p] resource_get_handle failed", m_logId );
         break;
      }
      if( whandle.com_obj == target )
      {
         entry.used = false;
         found = true;
         break;
      }
   }

   if( !found )
   {
      MFE_ERROR( "[dx12 hmft 0x%p] returned buffer was not found in the pool", m_logId );
   }
}

stats_buffer_manager::stats_buffer_manager( void *logId,
                                            struct vl_screen *pVlScreen,
                                            struct pipe_context *pPipeContext,
                                            REFGUID resourceGUID,
                                            uint32_t width,
                                            uint16_t height,
                                            enum pipe_format buffer_format,
                                            unsigned initial_pool_size,
                                            unsigned max_pool_size,
                                            HRESULT &hr )
   : m_logId( logId ),
     m_pVlScreen( pVlScreen ),
     m_pPipeContext( pPipeContext ),
     m_resourceGUID( resourceGUID ),
     m_pool( max_pool_size, { NULL, false } )
{
   hr = S_OK;
   m_template.target = PIPE_TEXTURE_2D;
   m_template.usage = PIPE_USAGE_DEFAULT;
   m_template.depth0 = 1;
   m_template.array_size = 1;

   m_template.width0 = width;
   m_template.height0 = height;
   m_template.format = buffer_format;

   unsigned buffer_count = 0;
   for( auto &entry : m_pool )
   {
      if( buffer_count < initial_pool_size )
      {
         entry.buffer = m_pVlScreen->pscreen->resource_create( m_pVlScreen->pscreen, &m_template );
         if( !entry.buffer )
         {
            MFE_ERROR( "[dx12 hmft 0x%p] resource_create failed", m_logId );
            assert( false );
            hr = E_FAIL;
            break;
         }
         buffer_count++;
      }
   }

   if( FAILED( hr ) )
   {
      for( auto &entry : m_pool )
      {
         if( entry.buffer )
         {
            m_pVlScreen->pscreen->resource_destroy( m_pVlScreen->pscreen, entry.buffer );
         }
      }
   }
}

stats_buffer_manager::~stats_buffer_manager()
{
   for( auto &entry : m_pool )
   {
      if( entry.buffer )
      {
         m_pVlScreen->pscreen->resource_destroy( m_pVlScreen->pscreen, entry.buffer );
      }
   }
}

// callback from IMFTrackSample (i.e. application)
IFACEMETHODIMP
stats_buffer_manager::OnSampleAvailable( IMFAsyncResult *pResult )
{
   HRESULT hr = S_OK;
   ComPtr<IUnknown> spUnk;
   ComPtr<IMFSample> spSample;
   ComPtr<IMFMediaBuffer> spMediaBuffer;
   ComPtr<IMFDXGIBuffer> spDXGIBuffer;
   ComPtr<IMFD3D12SynchronizationObjectCommands> spOutputSync;
   ComPtr<IMFD3D12SynchronizationObject> spSyncObj;
   ComPtr<ID3D12Resource> spDXGISurface;
   HANDLE hFree = NULL;
   HMFT_ETW_EVENT_START( "OnSampleAvailable", this );

   CHECKHR_GOTO( pResult->GetState( &spUnk ), done );
   CHECKHR_GOTO( spUnk.As( &spSample ), done );
   CHECKHR_GOTO( spSample->GetBufferByIndex( 0, &spMediaBuffer ), done );
   CHECKHR_GOTO( spMediaBuffer.As( &spDXGIBuffer ), done );
   CHECKHR_GOTO( spDXGIBuffer->GetResource( IID_PPV_ARGS( &spDXGISurface ) ), done );

   CHECKHR_GOTO( spDXGIBuffer->GetUnknown( MF_D3D12_SYNCHRONIZATION_OBJECT, IID_PPV_ARGS( &spOutputSync ) ), done );
   CHECKHR_GOTO( spOutputSync.As( &spSyncObj ), done );
   hFree = CreateEvent( nullptr, FALSE, FALSE, nullptr );
   if( !hFree )
   {
      CHECKHR_GOTO( HRESULT_FROM_WIN32( GetLastError() ), done );
   }
   CHECKHR_GOTO( spSyncObj->SignalEventOnFinalResourceRelease( hFree ), done );
   (void) WaitForSingleObject( hFree, INFINITE );
   spOutputSync.Reset();

   release_tracked_buffer( (void *) spDXGISurface.Get() );

done:
   if( hFree )
   {
      CloseHandle( hFree );
   }
   HMFT_ETW_EVENT_STOP( "OnSampleAvailable", this );
   return hr;
}

// Converts a Gallium pipe_resource into a D3D12 resource and wraps it as an IMFMediaBuffer,
// then attaches it as a sample extension on an IMFSample using the specified GUID.
HRESULT
stats_buffer_manager::AttachPipeResourceAsSampleExtension( struct pipe_resource *pPipeRes,
                                                           ID3D12CommandQueue *pSyncObjectQueue,
                                                           IMFSample *pSample )
{
   HRESULT hr = S_OK;
   if( !pPipeRes || !pSample || !pSyncObjectQueue )
   {
      return E_INVALIDARG;
   }

   struct winsys_handle whandle = {};
   whandle.type = WINSYS_HANDLE_TYPE_D3D12_RES;

   if( !m_pPipeContext->screen->resource_get_handle( m_pPipeContext->screen, m_pPipeContext, pPipeRes, &whandle, 0u ) )
   {
      return E_FAIL;
   }

   if( !whandle.com_obj )
   {
      return E_POINTER;
   }

   ComPtr<IMFTrackedSample> spTrackedSample;
   hr = MFCreateTrackedSample( &spTrackedSample );
   if( FAILED( hr ) )
   {
      return hr;
   }
   hr = spTrackedSample->SetAllocator( &m_xOnSampleAvailable, spTrackedSample.Get() );
   if( FAILED( hr ) )
   {
      return hr;
   }

   ComPtr<IMFSample> spSample;
   hr = spTrackedSample.As( &spSample );
   if( FAILED( hr ) )
   {
      return hr;
   }

   ID3D12Resource *pD3D12Res = static_cast<ID3D12Resource *>( whandle.com_obj );
   ComPtr<IMFMediaBuffer> spMediaBuffer;
   hr = MFCreateDXGISurfaceBuffer( __uuidof( ID3D12Resource ), pD3D12Res, 0, FALSE, &spMediaBuffer );

   if( FAILED( hr ) )
   {
      return hr;
   }

   // Tell MF that this buffer is ready to use.
   ComPtr<IMFD3D12SynchronizationObjectCommands> spOutputSync;   // needed to call Lock() for IMFMediaBuffer.
   ComPtr<IMFDXGIBuffer> spDxgiBuffer;
   hr = spMediaBuffer->QueryInterface( IID_PPV_ARGS( &spDxgiBuffer ) );
   if( SUCCEEDED( hr ) )
   {
      hr = spDxgiBuffer->GetUnknown( MF_D3D12_SYNCHRONIZATION_OBJECT, IID_PPV_ARGS( &spOutputSync ) );
      if( SUCCEEDED( hr ) )
      {
         hr = spOutputSync->EnqueueResourceReady( pSyncObjectQueue );

         spSample->AddBuffer( spMediaBuffer.Get() );

         if( FAILED( hr ) )
            return hr;
      }
      else
      {
         return hr;
      }
   }
   else
   {
      return hr;
   }

   return pSample->SetUnknown( m_resourceGUID, spMediaBuffer.Get() );
}
