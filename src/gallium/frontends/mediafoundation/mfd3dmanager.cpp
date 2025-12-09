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

#include "mfd3dmanager.h"
#include <algorithm>
#include <iterator>
#include <map>
#include <vector>
#include "wpptrace.h"

#include "mfd3dmanager.tmh"


CMFD3DManager::CMFD3DManager( void *logId ) : m_logId( logId )
{ }

CMFD3DManager::~CMFD3DManager()
{
   Shutdown();
}

// #define ENABLE_D3D12_DEBUG_LAYER

HRESULT
CMFD3DManager::Initialize( D3D12_VIDEO_ENCODER_CODEC codec )
{
#ifdef ENABLE_D3D12_DEBUG_LAYER
   ComPtr<ID3D12Debug> spDebugController;
   if( SUCCEEDED( D3D12GetDebugInterface( IID_PPV_ARGS( &spDebugController ) ) ) )
   {
      spDebugController->EnableDebugLayer();
   }
#endif
   m_codec = codec;
   return S_OK;
}

HRESULT
CMFD3DManager::Shutdown( bool bReleaseDeviceManager )
{
   HRESULT hr = S_OK;

   m_spDevice = nullptr;
   m_spVideoDevice = nullptr;
   m_spDevice11 = nullptr;
   m_spStagingQueue = nullptr;

   if( m_spVideoSampleAllocator )
   {
      m_spVideoSampleAllocator->UninitializeSampleAllocator();
      m_spVideoSampleAllocator = nullptr;
   }

   if( m_spSatdStatsBufferPool )
   {
      m_spSatdStatsBufferPool.Reset();
   }

   if( m_spBitsUsedStatsBufferPool )
   {
      m_spBitsUsedStatsBufferPool.Reset();
   }

   if( m_spQPMapStatsBufferPool )
   {
      m_spQPMapStatsBufferPool.Reset();
   }

   if( m_spReconstructedPictureBufferPool )
   {
      m_spReconstructedPictureBufferPool.Reset();
   }

   if( m_spDeviceManager != nullptr )
   {
      if( m_hDevice != NULL )
      {
         m_spDeviceManager->CloseDeviceHandle( m_hDevice );
         m_hDevice = NULL;
      }
      if( bReleaseDeviceManager )
         m_spDeviceManager = nullptr;
   }

   if( m_pPipeContext )
   {
      m_pPipeContext->destroy( m_pPipeContext );
      m_pPipeContext = nullptr;
   }

   if( m_pVlScreen )
   {
      m_pVlScreen->destroy( this->m_pVlScreen );
      m_pVlScreen = nullptr;
      m_deviceVendorId = {};
      m_deviceDeviceId = {};
      m_deviceDriverVersion = {};
      m_gpuFeatureFlags = {};
   }

   if( m_pWinsys )
   {
      m_pWinsys->destroy( this->m_pWinsys );
      m_pWinsys = nullptr;
   }
   if( m_ContextPriorityMgr.base.register_work_queue )
   {
      mtx_destroy( &m_ContextPriorityMgr.m_lock );
      m_ContextPriorityMgr.base.register_work_queue = nullptr;
   }

   return hr;
}

static inline HRESULT
CreateD3D12DeviceWithMinimumSupportedFeatureLevel( IUnknown *pAdapter, ComPtr<ID3D12Device> &spDevice )
{
   static const D3D_FEATURE_LEVEL levels[] = {
#if ( D3D12_SDK_VERSION >= 611 )
      D3D_FEATURE_LEVEL_1_0_GENERIC,
#endif
      D3D_FEATURE_LEVEL_1_0_CORE,
      D3D_FEATURE_LEVEL_11_0,
   };

   for( uint32_t i = 0; i < ARRAYSIZE( levels ); i++ )
      if( SUCCEEDED( D3D12CreateDevice( pAdapter, levels[i], IID_PPV_ARGS( spDevice.ReleaseAndGetAddressOf() ) ) ) )
         return S_OK;

   return E_FAIL;
}

HRESULT
CMFD3DManager::xReopenDeviceManager( bool bNewDevice )
{
   HRESULT hr = S_OK;
   D3D12_COMMAND_QUEUE_DESC commandQueueDesc = { D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE };
   D3D12_FEATURE_DATA_VIDEO_ENCODER_CODEC capCodecData = { 0U, m_codec, false };
   Shutdown( false );
   CHECKHR_GOTO( m_spDeviceManager->OpenDeviceHandle( &m_hDevice ), done );
   if( FAILED( m_spDeviceManager->GetVideoService( m_hDevice, IID_ID3D12Device, &m_spDevice ) ) )
   {
      ComPtr<IDXGIDevice> spDXGIDevice;
      ComPtr<IDXGIAdapter> spDXGIAdapter;
      ComPtr<IUnknown> spAdapter;
      ComPtr<ID3D11DeviceContext> spImmediaContext;
      ComPtr<ID3D11Multithread> spMultithread;
      CHECKHR_GOTO( m_spDeviceManager->GetVideoService( m_hDevice, IID_ID3D11Device, &m_spDevice11 ), done );
      CHECKHR_GOTO( m_spDevice11.As( &spDXGIDevice ), done );
      CHECKHR_GOTO( spDXGIDevice->GetAdapter( &spDXGIAdapter ), done );
      CHECKHR_GOTO( spDXGIAdapter.As( &spAdapter ), done );
      // Create a D3D12 device off of the same adapter this 11 device is on
      CHECKHR_GOTO( CreateD3D12DeviceWithMinimumSupportedFeatureLevel( spAdapter.Get(), m_spDevice ), done );

      // check if the D3D11 device is multithread protected and issue warning
      m_spDevice11->GetImmediateContext( &spImmediaContext );
      CHECKHR_GOTO( spImmediaContext.As( &spMultithread ), done );
      if( !spMultithread->GetMultithreadProtected() )
      {
         MFE_WARNING( "[dx12 hmft 0x%p] D3D11 device was created without multithread protected\n", m_logId );
      }
   }
   // Create a staging queue for MF to signal on input texture GPU completion
   CHECKHR_GOTO( m_spDevice->CreateCommandQueue( &commandQueueDesc, IID_PPV_ARGS( &m_spStagingQueue ) ), done );
   CHECKHR_GOTO( m_spDevice.As( &m_spVideoDevice ), done );
   CHECKHR_GOTO( m_spVideoDevice->CheckFeatureSupport( D3D12_FEATURE_VIDEO_ENCODER_CODEC, &capCodecData, sizeof( capCodecData ) ),
                 done );
   CHECKBOOL_GOTO( capCodecData.IsSupported, MF_E_UNSUPPORTED_D3D_TYPE, done );
done:
   return hr;
}

enum
{
   MFT_HW_VENDOR_AMD = 0x1002,
   MFT_HW_VENDOR_INTEL = 0x8086,
   MFT_HW_VENDOR_MICROSOFT = 0x1414,
   MFT_HW_VENDOR_NVIDIA = 0x10de,
};

static const char *
VendorIDToString( uint32_t vendorId )
{
   switch( vendorId )
   {
      case MFT_HW_VENDOR_MICROSOFT:
         return "Microsoft";
      case MFT_HW_VENDOR_AMD:
         return "AMD";
      case MFT_HW_VENDOR_NVIDIA:
         return "NVIDIA";
      case MFT_HW_VENDOR_INTEL:
         return "Intel";
      default:
         return "Unknown";
   }
}

inline UINT16
ExtractDriverVersionComponent( const size_t index, const LARGE_INTEGER &driverVersion )
{
   return (UINT16) ( driverVersion.QuadPart >> ( index * 8 * 2 ) ) & 0xffff;
}

// retrieve device information such as vendor id, device id, driver version.  we'll use this info later on
// to do block list and driver version dependent operations.
HRESULT
CMFD3DManager::GetDeviceInfo()
{
   HRESULT hr = S_OK;
   ComPtr<IDXGIFactory4> factory;
   ComPtr<IDXGIAdapter2> adaptor;
   DXGI_ADAPTER_DESC desc;
   LARGE_INTEGER driverVersion;
   LUID luid = m_spDevice->GetAdapterLuid();

   CHECKHR_GOTO( CreateDXGIFactory( IID_PPV_ARGS( &factory ) ), done );
   CHECKHR_GOTO( factory->EnumAdapterByLuid( luid, IID_PPV_ARGS( &adaptor ) ), done );
   CHECKHR_GOTO( adaptor->GetDesc( &desc ), done );
   CHECKHR_GOTO( adaptor->CheckInterfaceSupport( __uuidof( IDXGIDevice ), &driverVersion ), done );

   m_deviceVendorId = desc.VendorId;
   m_deviceDeviceId = desc.DeviceId;
   m_deviceDriverVersion.part1 = ExtractDriverVersionComponent( 3, driverVersion );
   m_deviceDriverVersion.part2 = ExtractDriverVersionComponent( 2, driverVersion );
   m_deviceDriverVersion.part3 = ExtractDriverVersionComponent( 1, driverVersion );
   m_deviceDriverVersion.part4 = ExtractDriverVersionComponent( 0, driverVersion );

   MFE_INFO( "[dx12 hmft 0x%p] D3DManager: device vendor = %s\n", m_logId, VendorIDToString( m_deviceVendorId ) );
   MFE_INFO( "[dx12 hmft 0x%p] D3DManager: device vendor id = %x\n", m_logId, m_deviceVendorId );
   MFE_INFO( "[dx12 hmft 0x%p] D3DManager: device device id = %x\n", m_logId, m_deviceDeviceId );
   MFE_INFO( "[dx12 hmft 0x%p] D3DManager: %S", m_logId, desc.Description );
   MFE_INFO( "[dx12 hmft 0x%p] D3DManager: device driver version = %d.%d.%d.%d\n",
             m_logId,
             m_deviceDriverVersion.part1,
             m_deviceDriverVersion.part2,
             m_deviceDriverVersion.part3,
             m_deviceDriverVersion.part4 );
done:
   return hr;
}

void
CMFD3DManager::UpdateGPUFeatureFlags()
{
   if( m_deviceVendorId == MFT_HW_VENDOR_AMD )
   {
      m_gpuFeatureFlags.m_bDisableAsync = true;
      MFE_INFO( "[dx12 hmft 0x%p] D3DManager: GPUFeature m_bDisableAsync is set to true\n", m_logId );
   }

   /*
   if( m_deviceVendorId == MFT_HW_VENDOR_NVIDIA )
   {
      if( m_deviceDriverVersion.part1 >= 32 && m_deviceDriverVersion.part2 >= 0 && m_deviceDriverVersion.part3 >= 15 &&
          m_deviceDriverVersion.part4 >= 9002 )
      {
         m_gpuFeatureFlags.m_bH264SendUnwrappedPOC = true;
         MFE_INFO( "[dx12 hmft 0x%p] D3DManager: GPUFeature m_bH264SendUnwrappedPOC is set to true\n", m_logId );
      }
   }
   */
}

int
MFTRegisterWorkQueue( struct d3d12_context_queue_priority_manager *manager, ID3D12CommandQueue *queue )
{
   mft_context_queue_priority_manager *mft_mgr = (mft_context_queue_priority_manager *) manager;
   mtx_lock( &mft_mgr->m_lock );

   ComPtr<IUnknown> queue_unknown;
   if( FAILED( queue->QueryInterface( IID_PPV_ARGS( &queue_unknown ) ) ) )
   {
      mtx_unlock( &mft_mgr->m_lock );
      return -1;
   }

   // Only register the queue if not already registered
   auto it = std::find( mft_mgr->m_registeredQueues.begin(), mft_mgr->m_registeredQueues.end(), queue );
   if( it == mft_mgr->m_registeredQueues.end() )
   {
      //
      // Register the queue_unknown with the MFT.
      //

      mft_mgr->m_registeredQueues.push_back( queue );
   }

   mtx_unlock( &mft_mgr->m_lock );
   return 0;
}

int
MFTUnregisterWorkQueue( struct d3d12_context_queue_priority_manager *manager, ID3D12CommandQueue *queue )
{
   mft_context_queue_priority_manager *mft_mgr = (mft_context_queue_priority_manager *) manager;
   mtx_lock( &mft_mgr->m_lock );

   ComPtr<IUnknown> queue_unknown;
   if( FAILED( queue->QueryInterface( IID_PPV_ARGS( &queue_unknown ) ) ) )
   {
      mtx_unlock( &mft_mgr->m_lock );
      return -1;
   }

   //
   // Unregister the queue_unknown with the MFT.
   //

   auto it = std::find( mft_mgr->m_registeredQueues.begin(), mft_mgr->m_registeredQueues.end(), queue );
   if( it != mft_mgr->m_registeredQueues.end() )
      mft_mgr->m_registeredQueues.erase( it );

   mtx_unlock( &mft_mgr->m_lock );
   return 0;
}

HRESULT
CMFD3DManager::xOnSetD3DManager( ULONG_PTR ulParam )
{
   HRESULT hr = S_OK;
   Shutdown();

   if( ulParam == 0 )
   {
      return hr;
   }
   else
   {
      // We've been given an IUnknown, make sure it is an IMFDXGIDeviceManager
      CHECKHR_GOTO( reinterpret_cast<IUnknown *>( ulParam )->QueryInterface( IID_PPV_ARGS( &m_spDeviceManager ) ), done );
   }
   CHECKHR_GOTO( xReopenDeviceManager( true ), done );

   CHECKNULL_GOTO( m_pWinsys = null_sw_create(), MF_E_DXGI_DEVICE_NOT_INITIALIZED, done );

   CHECKNULL_GOTO( m_pVlScreen = vl_win32_screen_create_from_d3d12_device( m_spDevice.Get(), m_pWinsys ),
                   MF_E_DXGI_DEVICE_NOT_INITIALIZED,
                   done );
   CHECKNULL_GOTO( m_pPipeContext = pipe_create_multimedia_context( m_pVlScreen->pscreen, false ),
                   MF_E_DXGI_DEVICE_NOT_INITIALIZED,
                   done );

   m_pVlScreen->pscreen->interop_query_device_info( m_pVlScreen->pscreen,
                                                    sizeof( d3d12_interop_device_info1 ),
                                                    &m_ScreenInteropInfo );
   assert( m_ScreenInteropInfo.set_context_queue_priority_manager != NULL );

   {
      CHECKBOOL_GOTO( thrd_success == mtx_init( &m_ContextPriorityMgr.m_lock, mtx_plain ), MF_E_DXGI_DEVICE_NOT_INITIALIZED, done );

      m_ContextPriorityMgr.base.register_work_queue = MFTRegisterWorkQueue;
      m_ContextPriorityMgr.base.unregister_work_queue = MFTUnregisterWorkQueue;

      CHECKBOOL_GOTO( m_ScreenInteropInfo.set_context_queue_priority_manager( m_pPipeContext, &m_ContextPriorityMgr.base ) == 0,
                      MF_E_DXGI_DEVICE_NOT_INITIALIZED,
                      done );

      //
      // Verify that the callbacks have been filled by the driver
      //
      CHECKNULL_GOTO( m_ContextPriorityMgr.base.context, MF_E_DXGI_DEVICE_NOT_INITIALIZED, done );
      CHECKNULL_GOTO( m_ContextPriorityMgr.base.set_queue_priority, MF_E_DXGI_DEVICE_NOT_INITIALIZED, done );
      CHECKNULL_GOTO( m_ContextPriorityMgr.base.get_queue_priority, MF_E_DXGI_DEVICE_NOT_INITIALIZED, done );
   }

   CHECKHR_GOTO( GetDeviceInfo(), done );

   UpdateGPUFeatureFlags();

done:
   if( FAILED( hr ) )
   {
      Shutdown();
   }
   return hr;
}
