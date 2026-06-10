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

#include <strmif.h>
#include "../hmft_entrypoints.h"
#include <gtest/gtest.h>
#include "hmft_test_helpers.h"

TEST( MediaFoundationEntrypoint, VerifyBasicCodecAPI )
{
   HRESULT hr = S_OK;
   ComPtr<CDX12EncHMFT> spMFT {};
   ComPtr<IMFDXGIDeviceManager> spDXGIMan {};
   ComPtr<IMFAttributes> spAttrs {};
   ComPtr<ICodecAPI> spCodecAPI {};

   CHECKHR_GOTO( CDX12EncHMFT::CreateInstance( &spMFT ), done );
   CHECKHR_GOTO( CreateD3D12Manager( &spDXGIMan, 0 ), done );
   CHECKHR_GOTO( spMFT->GetAttributes( &spAttrs ), done );

   UINT32 bAsync;
   if( S_OK == spAttrs->GetUINT32( MF_TRANSFORM_ASYNC, &bAsync ) && bAsync )
   {
      CHECKHR_GOTO(spAttrs->SetUINT32( MF_TRANSFORM_ASYNC_UNLOCK, TRUE ), done );
   }

   CHECKHR_GOTO(spMFT->ProcessMessage( MFT_MESSAGE_SET_D3D_MANAGER, (ULONG_PTR) spDXGIMan.Get() ), done);
   CHECKHR_GOTO(spMFT.As( &spCodecAPI ), done);

   {
      VARIANT vValue, vGetValue;
      vValue.vt = VT_UI4;
      vValue.ulVal = 8;
      CHECKHR_GOTO( spCodecAPI->SetValue( &CODECAPI_AVEncCommonQualityVsSpeed, &vValue ), done );
      CHECKHR_GOTO( spCodecAPI->GetValue( &CODECAPI_AVEncCommonQualityVsSpeed, &vGetValue ), done );
      ASSERT_EQ( vValue.ulVal, vGetValue.ulVal );
   }
   {
      VARIANT *pValues;
      ULONG ulValuesCount;
      CHECKHR_GOTO( spCodecAPI->GetParameterValues( &CODECAPI_AVEncVideoLTRBufferControl, &pValues, &ulValuesCount ), done );
   }
   {
      VARIANT vMin;
      VARIANT vMax;
      VARIANT vDelta;
      hr = spCodecAPI->IsSupported( &CODECAPI_AVEncSliceControlMode );
      if( SUCCEEDED( hr ) )
      {
         CHECKHR_GOTO( spCodecAPI->GetParameterRange( &CODECAPI_AVEncSliceControlMode, &vMin, &vMax, &vDelta ), done );
         ASSERT_TRUE( vMin.vt == VT_UI4 && vMax.vt == VT_UI4 && vDelta.vt == VT_UI4 );
         ASSERT_TRUE( vMin.ulVal <= 2u && vMax.ulVal <= 2u );
         ASSERT_TRUE( vMin.ulVal <= vMax.ulVal );
         ASSERT_TRUE( vDelta.ulVal >= 1u && vDelta.ulVal <= 2u );
      }
   }

done:
   ASSERT_HRESULT_SUCCEEDED( hr );
}
