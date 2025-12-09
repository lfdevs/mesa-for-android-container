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

#include <Unknwn.h>
#include <mfobjects.h>
#include <directx/d3d12.h>
#include <wrl/client.h>
#include "pipe_headers.h"

using Microsoft::WRL::ComPtr;

// Custom IMFMediaBuffer implementation for zero-copy D3D12 buffer access
class CD3D12BitstreamMFBuffer : public IMFMediaBuffer
{
 private:
   const void *m_logId = {};
   ULONG m_cRef;
   ComPtr<ID3D12Resource> m_spResource;
   DWORD m_dwLength;
   DWORD m_dwOffset;
   BYTE *m_pMappedData;
   pipe_screen *m_pScreen;
   pipe_resource *m_pOutputBitRes;

 public:
   CD3D12BitstreamMFBuffer( void *logId, pipe_context *pPipeContext, pipe_resource *pOutputBitRes, DWORD length, DWORD offset );
   ~CD3D12BitstreamMFBuffer();

   // IUnknown
   STDMETHODIMP QueryInterface( REFIID riid, void **ppv );
   STDMETHODIMP_( ULONG ) AddRef();
   STDMETHODIMP_( ULONG ) Release();

   // IMFMediaBuffer
   STDMETHODIMP Lock( BYTE **ppbBuffer, DWORD *pcbMaxLength, DWORD *pcbCurrentLength );
   STDMETHODIMP Unlock();
   STDMETHODIMP GetCurrentLength( DWORD *pcbCurrentLength );
   STDMETHODIMP SetCurrentLength( DWORD cbCurrentLength );
   STDMETHODIMP GetMaxLength( DWORD *pcbMaxLength );
};
