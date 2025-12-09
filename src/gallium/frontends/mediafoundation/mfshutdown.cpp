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
#include "hmft_entrypoints.h"
#include "wpptrace.h"

#include "mfshutdown.tmh"

HRESULT
CDX12EncHMFT::CheckShutdown( void )
{
   if( m_bShutdown )
   {
      return MF_E_SHUTDOWN;
   }
   return S_OK;
}

// IMFShutdown::GetShutdownStatus
// https://learn.microsoft.com/en-us/windows/win32/api/mfidl/nf-mfidl-imfshutdown-getshutdownstatus
HRESULT
CDX12EncHMFT::GetShutdownStatus( MFSHUTDOWN_STATUS *pStatus )
{
   HRESULT hr = S_OK;

   CHECKNULL_GOTO( pStatus, E_POINTER, done );
   CHECKBOOL_GOTO( CheckShutdown() == MF_E_SHUTDOWN, MF_E_INVALIDREQUEST, done );
   *pStatus = MFSHUTDOWN_COMPLETED;

done:
   MFE_INFO( "[dx12 hmft 0x%p] GetShutdownStatus - hr=0x%x", this, hr );
   return hr;
}

// IMFShutdown::Shutdown
// https://learn.microsoft.com/en-us/windows/win32/api/mfidl/nf-mfidl-imfshutdown-shutdown
HRESULT
CDX12EncHMFT::Shutdown( void )
{
   HRESULT hr = S_OK;
   std::lock_guard<std::mutex> lock( m_lockShutdown );
   debug_printf( "[dx12 hmft 0x%p] Shutdown called\n", this );

   if( !m_bShutdown )
   {
      m_bShutdown = true;
      OnFlush();
      CleanupEncoder();
   }

   MFE_INFO( "[dx12 hmft 0x%p] Shutdown - hr=0x%x", this, hr );
   return hr;
}
