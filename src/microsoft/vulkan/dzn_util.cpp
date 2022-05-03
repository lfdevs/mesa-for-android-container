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

#include "dzn_private.h"

#include "vk_enum_to_str.h"

#include <stdarg.h>
#include <stdio.h>

#include <directx/d3d12sdklayers.h>

IDXGIFactory4 *
dxgi_get_factory(bool debug)
{
   static const GUID IID_IDXGIFactory4 = {
      0x1bc6ea02, 0xef36, 0x464f,
      { 0xbf, 0x0c, 0x21, 0xca, 0x39, 0xe5, 0x16, 0x8a }
   };

   HMODULE dxgi_mod = LoadLibraryA("DXGI.DLL");
   if (!dxgi_mod) {
      mesa_loge("failed to load DXGI.DLL\n");
      return NULL;
   }

   typedef HRESULT(WINAPI *PFN_CREATE_DXGI_FACTORY2)(UINT flags, REFIID riid, void **ppFactory);
   PFN_CREATE_DXGI_FACTORY2 CreateDXGIFactory2;

   CreateDXGIFactory2 = (PFN_CREATE_DXGI_FACTORY2)GetProcAddress(dxgi_mod, "CreateDXGIFactory2");
   if (!CreateDXGIFactory2) {
      mesa_loge("failed to load CreateDXGIFactory2 from DXGI.DLL\n");
      return NULL;
   }

   UINT flags = 0;
   if (debug)
      flags |= DXGI_CREATE_FACTORY_DEBUG;

   IDXGIFactory4 *factory;
   HRESULT hr = CreateDXGIFactory2(flags, IID_IDXGIFactory4, (void **)&factory);
   if (FAILED(hr)) {
      mesa_loge("CreateDXGIFactory2 failed: %08x\n", hr);
      return NULL;
   }

   return factory;
}

static ComPtr<ID3D12Debug>
get_debug_interface()
{
   typedef HRESULT(WINAPI *PFN_D3D12_GET_DEBUG_INTERFACE)(REFIID riid, void **ppFactory);
   PFN_D3D12_GET_DEBUG_INTERFACE D3D12GetDebugInterface;

   HMODULE d3d12_mod = LoadLibraryA("D3D12.DLL");
   if (!d3d12_mod) {
      mesa_loge("failed to load D3D12.DLL\n");
      return NULL;
   }

   D3D12GetDebugInterface = (PFN_D3D12_GET_DEBUG_INTERFACE)GetProcAddress(d3d12_mod, "D3D12GetDebugInterface");
   if (!D3D12GetDebugInterface) {
      mesa_loge("failed to load D3D12GetDebugInterface from D3D12.DLL\n");
      return NULL;
   }

   ComPtr<ID3D12Debug> debug;
   if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
      mesa_loge("D3D12GetDebugInterface failed\n");
      return NULL;
   }

   return debug;
}

void
d3d12_enable_debug_layer()
{
   ComPtr<ID3D12Debug> debug = get_debug_interface();
   if (debug)
      debug->EnableDebugLayer();
}

void
d3d12_enable_gpu_validation()
{
   ComPtr<ID3D12Debug> debug = get_debug_interface();
   ComPtr<ID3D12Debug3> debug3;
   if (debug &&
       SUCCEEDED(debug->QueryInterface(IID_PPV_ARGS(&debug3))))
      debug3->SetEnableGPUBasedValidation(true);
}

ID3D12Device1 *
d3d12_create_device(IUnknown *adapter, bool experimental_features)
{
   typedef HRESULT(WINAPI *PFN_D3D12CREATEDEVICE)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
   PFN_D3D12CREATEDEVICE D3D12CreateDevice;

   HMODULE d3d12_mod = LoadLibraryA("D3D12.DLL");
   if (!d3d12_mod) {
      mesa_loge("failed to load D3D12.DLL\n");
      return NULL;
   }

#ifdef _WIN32
   if (experimental_features)
#endif
   {
      typedef HRESULT(WINAPI *PFN_D3D12ENABLEEXPERIMENTALFEATURES)(UINT, const IID*, void*, UINT*);
      PFN_D3D12ENABLEEXPERIMENTALFEATURES D3D12EnableExperimentalFeatures =
         (PFN_D3D12ENABLEEXPERIMENTALFEATURES)GetProcAddress(d3d12_mod, "D3D12EnableExperimentalFeatures");
      if (FAILED(D3D12EnableExperimentalFeatures(1, &D3D12ExperimentalShaderModels, NULL, NULL))) {
         mesa_loge("failed to enable experimental shader models\n");
         return nullptr;
      }
   }

   D3D12CreateDevice = (PFN_D3D12CREATEDEVICE)GetProcAddress(d3d12_mod, "D3D12CreateDevice");
   if (!D3D12CreateDevice) {
      mesa_loge("failed to load D3D12CreateDevice from D3D12.DLL\n");
      return NULL;
   }

   ID3D12Device1 *dev;
   if (SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0,
                 IID_PPV_ARGS(&dev))))
      return dev;

   mesa_loge("D3D12CreateDevice failed\n");
   return NULL;
}

PFN_D3D12_SERIALIZE_VERSIONED_ROOT_SIGNATURE
d3d12_get_serialize_root_sig(void)
{
   HMODULE d3d12_mod = LoadLibraryA("d3d12.dll");
   if (!d3d12_mod) {
      mesa_loge("failed to load d3d12.dll\n");
      return NULL;
   }

   return (PFN_D3D12_SERIALIZE_VERSIONED_ROOT_SIGNATURE)
      GetProcAddress(d3d12_mod, "D3D12SerializeVersionedRootSignature");
}
