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

#include "pipe_headers.h"

#include "gallium/include/frontend/sw_winsys.h"
#include "gallium/winsys/sw/null/null_sw_winsys.h"
#include "util/u_video.h"
#include "vl/vl_winsys.h"

// directx/xxx has the latest headers from DirectX-Headers dependency
// and must be included _before_ any Windows SDK headers (e.g d3d11.h, etc)
#include <d3d11.h>
#include <directx/d3d12.h>
#include <directx/d3d12video.h>
#include <d3d11_4.h>

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>   // for IMFVideoSampleAllocatorEx
#include <mutex>
#include <vector>
#include "gallium/drivers/d3d12/d3d12_interop_public.h"
#include <c11/threads.h>
#include <wrl/client.h>
#include <wrl/implements.h>

#include "macros.h"

#include "stats_buffer_manager.h"

// Use the Windows SDK dxcore include (e.g directx/dxcore uses DirectX-Headers)
#include <dxcore.h>

using namespace Microsoft::WRL;
using Microsoft::WRL::ComPtr;
using namespace std;

typedef union
{
   struct
   {
      // Driver version format is part1.part2.part3.part4 (e.g 31.0.15.5019)
      uint16_t part1 : 16;
      uint16_t part2 : 16;
      uint16_t part3 : 16;
      uint16_t part4 : 16;
   };
   uint64_t version;   // bits field
} MFAdapterDriverVersion;

struct mft_context_queue_priority_manager
{
   struct d3d12_context_queue_priority_manager base;
   std::vector<ID3D12CommandQueue *> m_registeredQueues;
   mtx_t m_lock;
};

class CMFD3DManager
{
 public:
   CMFD3DManager( void *logId );
   ~CMFD3DManager();

   HRESULT Initialize( D3D12_VIDEO_ENCODER_CODEC codec );
   HRESULT Shutdown( bool bReleaseDeviceManager = true );

   // Set D3D manager, use in ProcessMessage
   HRESULT xOnSetD3DManager( ULONG_PTR ulParam );

 protected:
   HRESULT xReopenDeviceManager( bool bNewDevice );
   HRESULT GetDeviceInfo();
   void UpdateGPUFeatureFlags();

   ComPtr<IMFDXGIDeviceManager> m_spDeviceManager;
   ComPtr<ID3D11Device5> m_spDevice11;
   ComPtr<ID3D12Device> m_spDevice;
   ComPtr<ID3D12VideoDevice> m_spVideoDevice;
   ComPtr<ID3D12CommandQueue> m_spStagingQueue;
   ComPtr<IMFVideoSampleAllocatorEx> m_spVideoSampleAllocator;   // Used for software input samples that need to be copied

   ComPtr<stats_buffer_manager> m_spSatdStatsBufferPool;
   ComPtr<stats_buffer_manager> m_spBitsUsedStatsBufferPool;
   ComPtr<stats_buffer_manager> m_spQPMapStatsBufferPool;
   ComPtr<stats_buffer_manager> m_spReconstructedPictureBufferPool;

   UINT32 m_uiResetToken = 0;
   HANDLE m_hDevice = NULL;
   struct vl_screen *m_pVlScreen = nullptr;
   struct sw_winsys *m_pWinsys = nullptr;
   struct pipe_context *m_pPipeContext = nullptr;
   struct d3d12_interop_device_info1 m_ScreenInteropInfo = {};

   struct mft_context_queue_priority_manager m_ContextPriorityMgr = {};

   uint32_t m_deviceVendorId {};
   uint32_t m_deviceDeviceId {};
   MFAdapterDriverVersion m_deviceDriverVersion {};

   // MFT features that are dependent on GPU / version (ensure these are named to be false by default so we can easily reset this
   // struct)
   struct GPUFeatureFlags
   {
      bool m_bDisableAsync = false;
      bool m_bH264SendUnwrappedPOC = false;
   };
   GPUFeatureFlags m_gpuFeatureFlags;

 private:
   D3D12_VIDEO_ENCODER_CODEC m_codec;
   const void *m_logId = {};
};
