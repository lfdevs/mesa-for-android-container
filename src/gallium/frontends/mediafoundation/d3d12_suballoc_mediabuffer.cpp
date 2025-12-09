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

#include "d3d12_suballoc_mediabuffer.h"
#include <mferror.h>
#include "frontend/winsys_handle.h"
#include "pipe/p_context.h"
#include "pipe/p_screen.h"
#include "util/u_inlines.h"

#include "wpptrace.h"

#include "d3d12_suballoc_mediabuffer.tmh"

CD3D12BitstreamMFBuffer::CD3D12BitstreamMFBuffer(
   void *logId, pipe_context *pPipeContext, pipe_resource *pOutputBitRes, DWORD length, DWORD offset )
   : m_logId( logId ),
     m_cRef( 1 ),
     m_dwLength( length ),
     m_dwOffset( offset ),
     m_pMappedData( nullptr ),
     m_pScreen( pPipeContext->screen ),
     m_pOutputBitRes( nullptr )
{
   debug_printf( "[dx12 hmft 0x%p] CD3D12BitstreamMFBuffer created for length %u, offset %u\n", m_logId, length, offset );
   pipe_resource_reference( &m_pOutputBitRes, pOutputBitRes );

   struct winsys_handle whandle = { .type = WINSYS_HANDLE_TYPE_D3D12_RES };
   if( pPipeContext->screen->resource_get_handle( pPipeContext->screen, nullptr, pOutputBitRes, &whandle, 0u ) )
   {
      m_spResource = static_cast<ID3D12Resource *>( whandle.com_obj );
   }
}

CD3D12BitstreamMFBuffer::~CD3D12BitstreamMFBuffer()
{
   // Decrement reference count on the pipe_resource (will destroy if reference count reaches 0)
   if( m_pOutputBitRes )
   {
      pipe_resource_reference( &m_pOutputBitRes, nullptr );
   }
}

STDMETHODIMP
CD3D12BitstreamMFBuffer::QueryInterface( REFIID riid, void **ppv )
{
   if( riid == IID_IUnknown || riid == IID_IMFMediaBuffer )
   {
      *ppv = this;
      AddRef();
      return S_OK;
   }
   return E_NOINTERFACE;
}

STDMETHODIMP_( ULONG ) CD3D12BitstreamMFBuffer::AddRef()
{
   return InterlockedIncrement( &m_cRef );
}

STDMETHODIMP_( ULONG ) CD3D12BitstreamMFBuffer::Release()
{
   ULONG cRef = InterlockedDecrement( &m_cRef );
   if( !cRef )
      delete this;
   return cRef;
}

STDMETHODIMP
CD3D12BitstreamMFBuffer::Lock( BYTE **ppbBuffer, DWORD *pcbMaxLength, DWORD *pcbCurrentLength )
{
   if( !ppbBuffer )
      return E_POINTER;
   if( m_pMappedData )
      return MF_E_INVALIDREQUEST;
   D3D12_RANGE range = { m_dwOffset, m_dwOffset + m_dwLength };
   HRESULT hr = m_spResource->Map( 0, &range, (void **) &m_pMappedData );
   if( SUCCEEDED( hr ) )
   {
      *ppbBuffer = m_pMappedData + m_dwOffset;
      if( pcbMaxLength )
         *pcbMaxLength = m_dwLength;
      if( pcbCurrentLength )
         *pcbCurrentLength = m_dwLength;
   }
   return hr;
}

STDMETHODIMP
CD3D12BitstreamMFBuffer::Unlock()
{
   if( !m_pMappedData )
      return MF_E_INVALIDREQUEST;
   D3D12_RANGE range = { 0, 0 };
   m_spResource->Unmap( 0, &range );
   m_pMappedData = nullptr;
   return S_OK;
}

STDMETHODIMP
CD3D12BitstreamMFBuffer::GetCurrentLength( DWORD *pcbCurrentLength )
{
   if( !pcbCurrentLength )
      return E_POINTER;
   *pcbCurrentLength = m_dwLength;
   return S_OK;
}

STDMETHODIMP
CD3D12BitstreamMFBuffer::SetCurrentLength( DWORD cbCurrentLength )
{
   m_dwLength = cbCurrentLength;
   return S_OK;
}

STDMETHODIMP
CD3D12BitstreamMFBuffer::GetMaxLength( DWORD *pcbMaxLength )
{
   if( !pcbMaxLength )
      return E_POINTER;
   *pcbMaxLength = m_dwLength;
   return S_OK;
}
