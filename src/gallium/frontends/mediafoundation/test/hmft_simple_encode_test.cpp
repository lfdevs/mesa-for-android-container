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

TEST( MediaFoundationEntrypoint, VerifySimpleEncodeSoftwareSample )
{
   HRESULT hr = S_OK;
   MFStartupHelper mfStartupHelper( hr );
   ASSERT_HRESULT_SUCCEEDED( hr );
   CFrameGenerator frameGenerator;

   ComPtr<CDX12EncHMFT> spMFT {};
   ComPtr<IMFDXGIDeviceManager> spDXGIMan {};
   ComPtr<IMFAttributes> spAttrs {};
   ComPtr<ICodecAPI> spCodecAPI {};
   ComPtr<IMFMediaEvent> spEvent {};
   ComPtr<IMFMediaEventGenerator> spEventGenerator {};
   uint32_t uiNeedInputCount = 0;
   uint32_t uiProcessInputCount = 0;
   uint32_t uiProcessOutputCount = 0;
   uint32_t uiSampleCount = 0;

   bool bIsMFTAllocator = true;
   bool bEndOfStream = false;

   const uint32_t frameRateDiv = frameGenerator.m_uiDiv;
   const uint32_t frameRateNum = frameGenerator.m_uiNum;
   const uint32_t width = frameGenerator.m_uiWidth;
   const uint32_t height = frameGenerator.m_uiHeight;
   const uint32_t bitRate = frameGenerator.m_uiBitRate;

   ComPtr<IMFSample> spInSample;
   ComPtr<IMFSample> spOutSample;
   ComPtr<IMFMediaBuffer> spOutBuffer;
   ComPtr<IMFMediaType> spInType = CreateVideoMT( width, height, FOURCC_NV12, FALSE, frameRateNum, frameRateDiv );

#if MFT_CODEC_H264ENC
   ComPtr<IMFMediaType> spOutType = CreateVideoMT( width, height, FOURCC_H264, FALSE, frameRateNum, frameRateDiv, bitRate * 1024 );
#elif MFT_CODEC_H265ENC
   ComPtr<IMFMediaType> spOutType = CreateVideoMT( width, height, FOURCC_HEVC, FALSE, frameRateNum, frameRateDiv, bitRate * 1024 );
#elif MFT_CODEC_AV1ENC
   ComPtr<IMFMediaType> spOutType = CreateVideoMT( width, height, FOURCC_AV01, FALSE, frameRateNum, frameRateDiv, bitRate * 1024 );
#else
#error MFT_CODEC_xxx must be defined
#endif

   GUID m_guidOutputVideoSubtype( MFVideoFormat_IYUV );

   CHECKHR_GOTO( CDX12EncHMFT::CreateInstance( &spMFT ), done );
   CHECKHR_GOTO( CreateD3D12Manager( &spDXGIMan, 0 ), done );
   CHECKHR_GOTO( spMFT->GetAttributes( &spAttrs ), done );

   UINT32 bAsync;
   if( S_OK == spAttrs->GetUINT32( MF_TRANSFORM_ASYNC, &bAsync ) && bAsync )
   {
      CHECKHR_GOTO( spAttrs->SetUINT32( MF_TRANSFORM_ASYNC_UNLOCK, TRUE ), done );
   }
   CHECKHR_GOTO( spMFT->ProcessMessage( MFT_MESSAGE_SET_D3D_MANAGER, (ULONG_PTR) spDXGIMan.Get() ), done );
   CHECKHR_GOTO( spMFT.As( &spCodecAPI ), done );

   {
      VARIANT *pValues;
      ULONG ulValuesCount;
      CHECKHR_GOTO( spCodecAPI->GetParameterValues( &CODECAPI_AVEncVideoLTRBufferControl, &pValues, &ulValuesCount ), done );
   }

   CHECKHR_GOTO( spMFT->SetOutputType( 0, spOutType.Get(), 0 ), done );
   CHECKHR_GOTO( spMFT->SetInputType( 0, spInType.Get(), 0 ), done );

   CHECKHR_GOTO( MFCreateSample( &spOutSample ), done );

   ASSERT_HRESULT_SUCCEEDED( spMFT->QueryInterface( IID_IMFMediaEventGenerator, &spEventGenerator ) );

   ASSERT_HRESULT_SUCCEEDED( spMFT->ProcessMessage( MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0 ) );
   ASSERT_HRESULT_SUCCEEDED( spMFT->ProcessMessage( MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0 ) );

   for( ;; )
   {
      MediaEventType eventType = 0;
      DWORD status;
      MFT_OUTPUT_DATA_BUFFER OutputDataBuf = { 0 };
      OutputDataBuf.pSample = nullptr;   // spOutSample.Get();

      if( bEndOfStream )
      {
         if( uiProcessInputCount == uiProcessOutputCount )
         {
            break;
         }
         CHECKHR_GOTO( spEventGenerator->GetEvent( 0, &spEvent ), done );
         CHECKHR_GOTO( spEvent->GetType( &eventType ), done );
         if( eventType != METransformHaveOutput && eventType != METransformDrainComplete )
         {
            // Only respond to METransformHaveOutput or METransformDrainComplete events after source is at EOS
            continue;
         }
      }
      else if( uiNeedInputCount > uiProcessInputCount && uiProcessInputCount == uiProcessOutputCount )
      {
         // We already counted this so decrement to avoid counting twice
         uiNeedInputCount--;
         // The MFT has requested more samples than we have given so try to feed a sample
         eventType = METransformNeedInput;
      }
      else
      {
         CHECKHR_GOTO( spEventGenerator->GetEvent( 0, &spEvent ), done );
         CHECKHR_GOTO( spEvent->GetType( &eventType ), done );
      }

      if( eventType == METransformNeedInput )
      {
         uiNeedInputCount++;
         frameGenerator.GenerateSoftwareFrame( &spInSample, &bEndOfStream );

         if( !bEndOfStream )
         {
            CHECKHR_GOTO( spMFT->ProcessInput( 0, spInSample.Get(), 0 ), done );
            uiProcessInputCount++;
         }
         else
         {
            CHECKHR_GOTO( spMFT->ProcessMessage( MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0 ), done );
            CHECKHR_GOTO( spMFT->ProcessMessage( MFT_MESSAGE_COMMAND_DRAIN, 0 ), done );
         }
      }
      else if( eventType == METransformHaveOutput )
      {
         if( bIsMFTAllocator )
         {
            OutputDataBuf.pSample = NULL;
         }
         status = 0;
         hr = spMFT->ProcessOutput( 0, 1, &OutputDataBuf, &status );

         if( SUCCEEDED( hr ) )
         {
            uiProcessOutputCount++;
            uiSampleCount++;

            if( OutputDataBuf.pSample != NULL )
            {
               CHECKHR_GOTO( OutputDataBuf.pSample->GetBufferByIndex( 0, &spOutBuffer ), done );

               DWORD dwLen;
               BYTE *pBuf;
               CHECKHR_GOTO( spOutBuffer->Lock( &pBuf, NULL, &dwLen ), done );
               printf( "Received %d\n", dwLen );

// #define DUMP
#if defined( DUMP )
               {
#if MFT_CODEC_H264ENC
                  FILE *fp = fopen( "d:\\test\\output.h264", "ab" );
#elif MFT_CODEC_H265ENC
                  FILE *fp = fopen( "d:\\test\\output.h265", "ab" );
#elif MFT_CODEC_AV1ENC
                  FILE *fp = fopen( "d:\\test\\output.obu", "ab" );
#endif
                  fwrite( pBuf, 1, dwLen, fp );
                  fclose( fp );
               }
#endif
               CHECKHR_GOTO( spOutBuffer->Unlock(), done );
               spOutBuffer.Reset();
               OutputDataBuf.pSample->Release();
            }
            else
            {
               CHECKHR_GOTO( E_FAIL, done );
            }
         }
         else if( MF_E_TRANSFORM_STREAM_CHANGE == hr )
         {
            ComPtr<IMFMediaType> spNewOutputType;
            CHECKHR_GOTO( spMFT->GetOutputAvailableType( 0, 0, &spNewOutputType ), done );
            CHECKHR_GOTO( spMFT->SetOutputType( 0, spNewOutputType.Get(), 0 ), done );
         }
         else
         {
            CHECKHR_GOTO( hr, done );
         }
      }
      else if( eventType == METransformDrainComplete )
      {
         CHECKHR_GOTO( spMFT->ProcessMessage( MFT_MESSAGE_COMMAND_FLUSH, 0 ), done );
         break;
      }
      else
      {
         CHECKHR_GOTO( E_FAIL, done );   // unexpected event type.
      }
   }

   CHECKHR_GOTO( spMFT->ProcessMessage( MFT_MESSAGE_SET_D3D_MANAGER, (ULONG_PTR) NULL ), done );

done:
   ASSERT_HRESULT_SUCCEEDED( hr );
}
