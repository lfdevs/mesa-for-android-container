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
#include "videobufferlock.h"

// utility functions to retrieve plane information from IMFMediaType
HRESULT
MFTypeToBitmapInfo(
   IMFMediaType *pType, UINT32 *pcbActualBytesPerLine, UINT32 *punLines, UINT32 *pcbSBytesPerLine, UINT32 *punSLines )
{
   HRESULT hr = S_OK;
   LONG lMinPitch;
   UINT32 unWidth, unHeight;
   UINT32 cbImageSize, cbActualBytesPerLine, unLines;
   GUID format;
   CHECKHR_GOTO( pType->GetGUID( MF_MT_SUBTYPE, &format ), done );
   CHECKHR_GOTO( MFGetAttributeSize( pType, MF_MT_FRAME_SIZE, &unWidth, &unHeight ), done );
   CHECKHR_GOTO( MFCalculateImageSize( format, unWidth, unHeight, &cbImageSize ), done );
   CHECKHR_GOTO( MFGetStrideForBitmapInfoHeader( format.Data1, unWidth, &lMinPitch ), done );

   cbActualBytesPerLine = abs( lMinPitch );

   // dual half-planar formats
   if( format == MFVideoFormat_YV12 || format == MFVideoFormat_IYUV || format == MFVideoFormat_I420 )
   {
      unLines = cbImageSize / ( cbActualBytesPerLine + cbActualBytesPerLine / 2 );
      if( nullptr != pcbSBytesPerLine )
         *pcbSBytesPerLine = cbActualBytesPerLine / 2;
      // processing 2 half planes with half lines, should process double lines
      if( nullptr != punSLines )
         *punSLines = unLines;
   }
   else
   {
      unLines = cbImageSize / cbActualBytesPerLine;
      if( nullptr != pcbSBytesPerLine )
         *pcbSBytesPerLine = 0;
      if( nullptr != punSLines )
         *punSLines = 0;
   }

   if( nullptr != pcbActualBytesPerLine )
      *pcbActualBytesPerLine = cbActualBytesPerLine;
   if( nullptr != punLines )
      *punLines = unLines;
done:
   return hr;
}

// utility function to retrieve default image size from IMFMediaType
HRESULT
MFTypeToImageSize( IMFMediaType *pType, UINT32 *pcbSize )
{
   HRESULT hr = S_OK;
   UINT32 unWidth, unHeight;
   UINT32 cbImageSize;
   GUID format;
   CHECKHR_GOTO( pType->GetGUID( MF_MT_SUBTYPE, &format ), done );
   CHECKHR_GOTO( MFGetAttributeSize( pType, MF_MT_FRAME_SIZE, &unWidth, &unHeight ), done );
   CHECKHR_GOTO( MFCalculateImageSize( format, unWidth, unHeight, &cbImageSize ), done );
   *pcbSize = cbImageSize;
done:
   return hr;
}

// utility function to retrieve D3D11 texture from IMFMediaBuffer
HRESULT
MFBufferToDXType( _In_ IMFMediaBuffer *pBuffer, _Outptr_ ID3D11Texture2D **ppTexture, _Out_ UINT *uiViewIndex )
{
   HRESULT hr = S_OK;
   ComPtr<IMFDXGIBuffer> spDXGIBuffer;

   hr = pBuffer->QueryInterface( __uuidof( IMFDXGIBuffer ), (LPVOID *) ( &spDXGIBuffer ) );
   if( SUCCEEDED( hr ) )
   {
      hr = spDXGIBuffer->GetResource( __uuidof( ID3D11Texture2D ), (LPVOID *) ppTexture );
   }
   if( SUCCEEDED( hr ) && uiViewIndex != nullptr )
   {
      hr = spDXGIBuffer->GetSubresourceIndex( uiViewIndex );
   }

   return hr;
}

// utility function to retrieve D3D12 texture from IMFMediaBuffer
HRESULT
MFBufferToDXType( _In_ IMFMediaBuffer *pBuffer, _Outptr_ ID3D12Resource **ppTexture, _Out_ UINT *uiViewIndex )
{
   HRESULT hr = S_OK;
   ComPtr<IMFDXGIBuffer> spDXGIBuffer;

   hr = pBuffer->QueryInterface( __uuidof( IMFDXGIBuffer ), (LPVOID *) ( &spDXGIBuffer ) );
   if( SUCCEEDED( hr ) )
   {
      hr = spDXGIBuffer->GetResource( __uuidof( ID3D12Resource ), (LPVOID *) ppTexture );
   }
   if( SUCCEEDED( hr ) && uiViewIndex != nullptr )
   {
      hr = spDXGIBuffer->GetSubresourceIndex( uiViewIndex );
   }

   return hr;
}

// utility function to copy MFSample
HRESULT
MFCopySample( IMFSample *dest, IMFSample *src, IMFMediaType *pmt )
{
   HRESULT hr = S_OK;
   ComPtr<IMFMediaBuffer> pOutput, pInput;
   ComPtr<ID3D11Texture2D> srcTexture, dstTexture;
   ComPtr<ID3D11Device> pSrcDevice, pDstDevice;
   ComPtr<ID3D11DeviceContext> pCtx;
   UINT uiSrcIndex, uiDstIndex;
   DWORD dwBufferCount;
   UINT32 dwSize = 0;
   BOOL bUsedDX = FALSE;
   UINT32 cbActualBytesPerLine, unLines, cbSActualBytes, unSLines;

   CHECKHR_GOTO( MFTypeToBitmapInfo( pmt, &cbActualBytesPerLine, &unLines, &cbSActualBytes, &unSLines ), done );

   if( !dest || !src )
   {
      return E_POINTER;
   }

   (void) src->CopyAllItems( dest );

   LONGLONG hnsTime;
   hr = src->GetSampleTime( &hnsTime );
   if( SUCCEEDED( hr ) )
   {
      hr = dest->SetSampleTime( hnsTime );
   }
   hr = S_OK;

   LONGLONG hnsDuration;
   hr = src->GetSampleDuration( &hnsDuration );
   if( SUCCEEDED( hr ) )
   {
      hr = dest->SetSampleDuration( hnsDuration );
   }
   hr = S_OK;

   hr = src->GetBufferCount( &dwBufferCount );

   if( SUCCEEDED( hr ) )
      for( DWORD x = 0; x < dwBufferCount; x++ )
      {
         hr = src->GetBufferByIndex( x, &pInput );
         if( FAILED( hr ) )
         {
            break;
         }
         hr = dest->GetBufferByIndex( x, &pOutput );
         if( FAILED( hr ) )
         {
            hr = MFCreateMediaBufferFromMediaType( pmt, hnsDuration, 0, 0, &pOutput );
            if( FAILED( hr ) )
            {
               break;
            }
            hr = dest->AddBuffer( pOutput.Get() );
            if( FAILED( hr ) )
            {
               break;
            }
         }

         // Try to use DX if available
         if( SUCCEEDED( MFBufferToDXType( pOutput.Get(), &srcTexture, &uiSrcIndex ) ) &&
             SUCCEEDED( MFBufferToDXType( pInput.Get(), &dstTexture, &uiDstIndex ) ) )
         {
            srcTexture->GetDevice( &pSrcDevice );
            dstTexture->GetDevice( &pDstDevice );
            if( pSrcDevice != pDstDevice )
            {
               // TODO: if both are shared we can keyed mutex them across
            }
            else
            {
               pSrcDevice->GetImmediateContext( &pCtx );
               pCtx->CopySubresourceRegion( dstTexture.Get(), uiDstIndex, 0, 0, 0, srcTexture.Get(), uiSrcIndex, NULL );
               bUsedDX = TRUE;
            }

            hr = MFTypeToImageSize( pmt, &dwSize );
            if( FAILED( hr ) )
            {
               break;
            }
         }

         if( !bUsedDX )
         {
            VideoBufferLock inputLock( pInput.Get(), pmt );
            VideoBufferLock outputLock( pOutput.Get(), pmt );

            hr = inputLock.lock( MF2DBuffer_LockFlags_Read );
            if( FAILED( hr ) )
            {
               break;
            }
            hr = outputLock.lock( MF2DBuffer_LockFlags_Write );
            if( FAILED( hr ) )
            {
               break;
            }

            hr = MFCopyImage( outputLock.data(),
                              outputLock.stride(),
                              inputLock.data(),
                              inputLock.stride(),
                              cbActualBytesPerLine,
                              unLines );
            if( FAILED( hr ) )
            {
               break;
            }

            if( unSLines )
            {
               hr = MFCopyImage( outputLock.data(),
                                 outputLock.stride(),
                                 inputLock.data(),
                                 inputLock.stride(),
                                 cbSActualBytes,
                                 unSLines );
            }

            dwSize = inputLock.size();
         }

         hr = pOutput->SetCurrentLength( dwSize );
         if( FAILED( hr ) )
         {
            break;
         }
      }
done:
   return hr;
}

//
// MFAttachPipeResourceAsSampleExtension
//
// Description:
//     Converts a Gallium pipe_resource into a D3D12 resource and wraps it as an IMFMediaBuffer,
//     then attaches it as a sample extension on an IMFSample using the specified GUID.
//
// Parameters:
//     pPipeContext   - Pointer to the Gallium pipe context.
//     pPipeRes       - Pointer to the pipe_resource to be wrapped.
//     pSyncObjectQueue - Pointer to the sync object command queue.
//     guidExtension  - The GUID of the Media Foundation sample extension to attach the buffer as.
//     pSample        - The output IMFSample to attach the media buffer to.
//
// Returns:
//     S_OK if the operation was successful.
//     E_INVALIDARG if any required pointer is null.
//     E_FAIL if resource_get_handle fails.
//     E_POINTER if the returned COM object is null.
//     Other HRESULT failure codes from MFCreateDXGISurfaceBuffer or SetUnknown.
//
HRESULT
MFAttachPipeResourceAsSampleExtension( struct pipe_context *pPipeContext,
                                       struct pipe_resource *pPipeRes,
                                       UINT PipeResourceReconstructedPictureSubresource,
                                       ID3D12CommandQueue *pSyncObjectQueue,
                                       REFGUID guidExtension,
                                       IMFSample *pSample )
{
   if( !pPipeContext || !pPipeRes || !pSample || !pSyncObjectQueue )
   {
      return E_INVALIDARG;
   }

   struct winsys_handle whandle = {};
   whandle.type = WINSYS_HANDLE_TYPE_D3D12_RES;

   if( !pPipeContext->screen->resource_get_handle( pPipeContext->screen, pPipeContext, pPipeRes, &whandle, 0u ) )
   {
      return E_FAIL;
   }

   if( !whandle.com_obj )
   {
      return E_POINTER;
   }

   ID3D12Resource *pD3D12Res = static_cast<ID3D12Resource *>( whandle.com_obj );
   ComPtr<IMFMediaBuffer> spMediaBuffer;
   HRESULT hr = MFCreateDXGISurfaceBuffer( __uuidof( ID3D12Resource ),
                                           pD3D12Res,
                                           PipeResourceReconstructedPictureSubresource,
                                           FALSE,
                                           &spMediaBuffer );

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

   return pSample->SetUnknown( guidExtension, spMediaBuffer.Get() );
}
