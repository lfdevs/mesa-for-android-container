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
#include <d3d12.h>
#include <mfidl.h>

// Given a media type report plane information
//  pType - type to get info for
//  pcbActualBytesPerLine - line byte count for first plane
//  punLines - line count for first plane
//  pcbSBytesPerLine - line byte count for second plane (may be null)
//  punSLines - line count for second plane (may be null)
HRESULT
MFTypeToBitmapInfo( IMFMediaType *pType,
                    UINT32 *pcbActualBytesPerLine,
                    UINT32 *punLines,
                    UINT32 *pcbSBytesPerLine = nullptr,
                    UINT32 *punSLines = nullptr );

// Gets the default size of an image for pmt
HRESULT
MFTypeToImageSize( IMFMediaType *pType, UINT32 *pcbSize );

// Copy a sample from src to dst for the given media type (copies buffer contents)
HRESULT
MFCopySample( IMFSample *dest, IMFSample *src, IMFMediaType *pmt );

// Converts a Gallium pipe_resource into a D3D12 resource and wraps it as an IMFMediaBuffer,
// then attaches it as a sample extension on an IMFSample using the specified GUID.
HRESULT
MFAttachPipeResourceAsSampleExtension( struct pipe_context *pPipeContext,
                                       struct pipe_resource *pPipeRes,
                                       UINT PipeResourceReconstructedPictureSubresource,
                                       ID3D12CommandQueue *pSyncObjectQueue,
                                       REFGUID guidExtension,
                                       IMFSample *pSample );
