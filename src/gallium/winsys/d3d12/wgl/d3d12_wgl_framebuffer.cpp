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

#include "d3d12_wgl_public.h"

#include <new>

#include <windows.h>
#include <dxgi1_4.h>
#include <directx/d3d12.h>
#include <wrl.h>
#include <dxguids/dxguids.h>

#include "util/u_memory.h"
#include "util/u_inlines.h"
#include "frontend/api.h"
#include "frontend/winsys_handle.h"

#include "stw_device.h"
#include "stw_pixelformat.h"
#include "stw_winsys.h"

#include "d3d12/d3d12_format.h"
#include "d3d12/d3d12_resource.h"
#include "d3d12/d3d12_screen.h"

#include <dcomp.h>

using Microsoft::WRL::ComPtr;
constexpr uint32_t num_buffers = 2;

struct d3d12_wgl_framebuffer {
   struct stw_winsys_framebuffer base;

   struct d3d12_screen *screen;
   enum pipe_format pformat;
   HWND window;
   ComPtr<IDCompositionDevice> dcomp;
   ComPtr<IDCompositionTarget> target;
   ComPtr<IDCompositionVisual> visual;
   ComPtr<IDXGISwapChain3> swapchain;
   bool need_fillrect = true;
   HANDLE waitable_object;
   int latency = 2;
   struct pipe_resource *buffers[num_buffers];
   bool single_buffered;
   struct pipe_resource *offscreen_buffer;
};

static struct d3d12_wgl_framebuffer *
d3d12_wgl_framebuffer(struct stw_winsys_framebuffer *fb)
{
   return (struct d3d12_wgl_framebuffer *)fb;
}

static void
d3d12_wgl_framebuffer_drain_queue(struct d3d12_wgl_framebuffer *framebuffer)
{
   /* Fully drain */
   mtx_lock(&framebuffer->screen->submit_mutex);
   UINT64 value = ++framebuffer->screen->fence_value;
   framebuffer->screen->cmdqueue->Signal(framebuffer->screen->fence, value);
   mtx_unlock(&framebuffer->screen->submit_mutex);
   framebuffer->screen->fence->SetEventOnCompletion(value, nullptr);
}

static void
d3d12_wgl_framebuffer_destroy(struct stw_winsys_framebuffer *fb,
                              pipe_context *ctx)
{
   struct d3d12_wgl_framebuffer *framebuffer = d3d12_wgl_framebuffer(fb);
   struct pipe_fence_handle *fence = NULL;

   if (ctx) {
      /* Ensure all resources are flushed */
      ctx->flush(ctx, &fence, PIPE_FLUSH_HINT_FINISH);
      if (fence) {
         ctx->screen->fence_finish(ctx->screen, ctx, fence, OS_TIMEOUT_INFINITE);
         ctx->screen->fence_reference(ctx->screen, &fence, NULL);
      }
   }

   d3d12_wgl_framebuffer_drain_queue(framebuffer);

   for (uint32_t i = 0; i < num_buffers; ++i) {
      if (framebuffer->buffers[i]) {
         d3d12_resource_release(d3d12_resource(framebuffer->buffers[i]));
         pipe_resource_reference(&framebuffer->buffers[i], NULL);
      }
   }

   if (framebuffer->offscreen_buffer) {
      pipe_resource_reference(&framebuffer->offscreen_buffer, NULL);
   }

   if (framebuffer->waitable_object)
      CloseHandle(framebuffer->waitable_object);

   delete framebuffer;
}

static IDCompositionDevice *
dcomp_get_device()
{
   HMODULE dcomp_mod = LoadLibraryA("DComp.DLL");
   if (!dcomp_mod) {
      return NULL;
   }

   typedef HRESULT (STDAPICALLTYPE *PFN_DCOMP_CREATE_DEVICE)(IDXGIDevice *, REFIID, void **);
   PFN_DCOMP_CREATE_DEVICE DCompositionCreateDevice;

   DCompositionCreateDevice = (PFN_DCOMP_CREATE_DEVICE)GetProcAddress(dcomp_mod, "DCompositionCreateDevice");
   if (!DCompositionCreateDevice) {
      return NULL;
   }

   IDCompositionDevice *device;
   HRESULT hr = DCompositionCreateDevice(NULL, IID_PPV_ARGS(&device));
   if (FAILED(hr)) {
      return NULL;
   }

   return device;
}

static void
d3d12_wgl_framebuffer_resize(stw_winsys_framebuffer *fb,
                             pipe_context *ctx,
                             pipe_resource *templ)
{
   struct d3d12_wgl_framebuffer *framebuffer = d3d12_wgl_framebuffer(fb);
   struct d3d12_dxgi_screen *screen = d3d12_dxgi_screen(framebuffer->screen);

   DXGI_SWAP_CHAIN_DESC1 desc = {};
   desc.BufferCount = num_buffers;
   desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT;
   desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING | DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
   desc.Format = d3d12_get_format(templ->format);
   if (desc.Format == DXGI_FORMAT_B8G8R8X8_UNORM)
      desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
   desc.Width = templ->width0;
   desc.Height = templ->height0;
   desc.SampleDesc.Count = 1;
   desc.AlphaMode = util_format_has_alpha(templ->format) ?
      DXGI_ALPHA_MODE_PREMULTIPLIED : DXGI_ALPHA_MODE_IGNORE;
   desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
   desc.Scaling = DXGI_SCALING_STRETCH;

   framebuffer->pformat = templ->format;
   switch (templ->format) {
   case PIPE_FORMAT_B8G8R8X8_UNORM:
      framebuffer->pformat = PIPE_FORMAT_B8G8R8A8_UNORM;
      break;
   case PIPE_FORMAT_R8G8B8X8_UNORM:
      framebuffer->pformat = PIPE_FORMAT_R8G8B8A8_UNORM;
      break;
   default:
      break;
   }

   if (desc.AlphaMode != DXGI_ALPHA_MODE_IGNORE) {
      if (!framebuffer->dcomp) {
         framebuffer->dcomp.Attach(dcomp_get_device());
         if (!framebuffer->dcomp) {
            debug_printf("D3D12: failed to create dcomp device\n");
            return;
         }
      }

      if (!framebuffer->target) {
         if (FAILED(framebuffer->dcomp->CreateTargetForHwnd(framebuffer->window, TRUE, &framebuffer->target))) {
            debug_printf("D3D12: failed to create dcomp target\n");
            return;
         }
      }
   }

   if (!framebuffer->swapchain) {
      ComPtr<IDXGISwapChain1> swapchain1;
      if (desc.AlphaMode != DXGI_ALPHA_MODE_IGNORE) {
         if (FAILED(screen->factory->CreateSwapChainForComposition(
            screen->base.cmdqueue,
            &desc,
            nullptr,
            &swapchain1))) {
            debug_printf("D3D12: failed to create swapchain\n");
            return;
         }
      } else {
         if (FAILED(screen->factory->CreateSwapChainForHwnd(
            screen->base.cmdqueue,
            framebuffer->window,
            &desc,
            nullptr,
            nullptr,
            &swapchain1))) {
            debug_printf("D3D12: failed to create swapchain\n");
            return;
         }
      }

      swapchain1.As(&framebuffer->swapchain);

      framebuffer->waitable_object = framebuffer->swapchain->GetFrameLatencyWaitableObject();
      WaitForSingleObject(framebuffer->waitable_object, INFINITE);

      framebuffer->swapchain->SetMaximumFrameLatency(framebuffer->latency);
   }
   else {
      struct pipe_fence_handle *fence = NULL;

      /* Ensure all resources are flushed */
      ctx->flush(ctx, &fence, PIPE_FLUSH_HINT_FINISH);
      if (fence) {
         ctx->screen->fence_finish(ctx->screen, ctx, fence, OS_TIMEOUT_INFINITE);
         ctx->screen->fence_reference(ctx->screen, &fence, NULL);
      }

      d3d12_wgl_framebuffer_drain_queue(framebuffer);

      for (uint32_t i = 0; i < num_buffers; ++i) {
         if (framebuffer->buffers[i]) {
            d3d12_resource_release(d3d12_resource(framebuffer->buffers[i]));
            pipe_resource_reference(&framebuffer->buffers[i], NULL);
         }
      }
      if (FAILED(framebuffer->swapchain->ResizeBuffers(num_buffers, desc.Width, desc.Height, desc.Format, desc.Flags))) {
         debug_printf("D3D12: failed to resize swapchain\n");
      }
   }

   if (!framebuffer->visual && desc.AlphaMode != DXGI_ALPHA_MODE_IGNORE) {
      if (FAILED(framebuffer->dcomp->CreateVisual(&framebuffer->visual)) ||
          FAILED(framebuffer->target->SetRoot(framebuffer->visual.Get())) ||
          FAILED(framebuffer->visual->SetContent(framebuffer->swapchain.Get())) ||
          FAILED(framebuffer->dcomp->Commit())) {
         debug_printf("D3D12: failed to configure visual\n");
         return;
      }
   }

   for (uint32_t i = 0; i < num_buffers; ++i) {
      ID3D12Resource *res;
      framebuffer->swapchain->GetBuffer(i, IID_PPV_ARGS(&res));
      if (!res)
         continue;

      struct winsys_handle handle;
      memset(&handle, 0, sizeof(handle));
      handle.type = WINSYS_HANDLE_TYPE_D3D12_RES;
      handle.format = framebuffer->pformat;
      handle.com_obj = res;

      D3D12_RESOURCE_DESC res_desc = GetDesc(res);

      struct pipe_resource templ;
      memset(&templ, 0, sizeof(templ));
      templ.target = PIPE_TEXTURE_2D;
      templ.format = framebuffer->pformat;
      templ.width0 = res_desc.Width;
      templ.height0 = res_desc.Height;
      templ.depth0 = 1;
      templ.array_size = res_desc.DepthOrArraySize;
      templ.nr_samples = res_desc.SampleDesc.Count;
      templ.last_level = res_desc.MipLevels - 1;
      templ.bind = PIPE_BIND_DISPLAY_TARGET | PIPE_BIND_RENDER_TARGET;
      templ.usage = PIPE_USAGE_DEFAULT;
      templ.flags = 0;

      pipe_resource_reference(&framebuffer->buffers[i],
                              screen->base.base.resource_from_handle(&screen->base.base, &templ, &handle,
                                                                     PIPE_HANDLE_USAGE_FRAMEBUFFER_WRITE));
   }

   if (framebuffer->single_buffered) {
      if (framebuffer->offscreen_buffer) {
         pipe_resource_reference(&framebuffer->offscreen_buffer, NULL);
      }
      struct pipe_resource local_templ = *templ;
      local_templ.bind = PIPE_BIND_RENDER_TARGET | PIPE_BIND_SAMPLER_VIEW;
      framebuffer->offscreen_buffer = screen->base.base.resource_create(&screen->base.base, &local_templ);
   }

   framebuffer->need_fillrect = util_format_has_alpha(templ->format);
}

static bool
d3d12_wgl_framebuffer_present(stw_winsys_framebuffer *fb, int interval)
{
   auto framebuffer = d3d12_wgl_framebuffer(fb);
   if (!framebuffer->swapchain) {
      debug_printf("D3D12: Cannot present; no swapchain");
      return false;
   }

   if (framebuffer->need_fillrect && IsWindowVisible(framebuffer->window)) {
      HDC hdc = GetWindowDC(framebuffer->window);
      RECT rect;
      GetWindowRect(framebuffer->window, &rect);
      rect.right -= rect.left;
      rect.bottom -= rect.top;
      rect.left = 0;
      rect.top = 0;
      FillRect(hdc, &rect, (HBRUSH)GetStockObject(BLACK_BRUSH));
      ReleaseDC(framebuffer->window, hdc);
      framebuffer->need_fillrect = false;
   }

   HRESULT hr;
   if (interval < 1)
      hr = framebuffer->swapchain->Present(0, DXGI_PRESENT_ALLOW_TEARING);
   else
      hr = framebuffer->swapchain->Present(interval, 0);

   if (hr == S_OK)
      return WaitForSingleObject(framebuffer->waitable_object, 2000) == WAIT_OBJECT_0;
   return false;
}

static struct pipe_resource *
d3d12_wgl_framebuffer_get_resource(struct stw_winsys_framebuffer *pframebuffer,
                                   st_attachment_type statt)
{
   auto framebuffer = d3d12_wgl_framebuffer(pframebuffer);

   if (!framebuffer->swapchain)
      return nullptr;

   if (framebuffer->single_buffered) {
      assert(statt == ST_ATTACHMENT_FRONT_LEFT);
      assert(framebuffer->offscreen_buffer);
      pipe_reference(NULL, &framebuffer->offscreen_buffer->reference);
      return framebuffer->offscreen_buffer;
   }

   UINT index = framebuffer->swapchain->GetCurrentBackBufferIndex();
   if (statt == ST_ATTACHMENT_FRONT_LEFT)
      index = !index;

   assert(framebuffer->buffers[index]);
   pipe_reference(NULL, &framebuffer->buffers[index]->reference);
   return framebuffer->buffers[index];
}

static void
d3d12_wgl_framebuffer_flush_frontbuffer(struct stw_winsys_framebuffer *pframebuffer,
                                        struct pipe_context *pipe)
{
   auto framebuffer = d3d12_wgl_framebuffer(pframebuffer);
   struct pipe_blit_info blit;

   memset(&blit, 0, sizeof(blit));
   uint32_t index = framebuffer->swapchain->GetCurrentBackBufferIndex();
   blit.dst.resource = framebuffer->buffers[index];
   blit.dst.box.width = blit.dst.resource->width0;
   blit.dst.box.height = blit.dst.resource->height0;
   blit.dst.box.depth = 1;
   blit.dst.format = blit.dst.resource->format;
   blit.src.resource = framebuffer->offscreen_buffer;
   blit.src.box.width = blit.src.resource->width0;
   blit.src.box.height = blit.src.resource->height0;
   blit.src.box.depth = 1;
   blit.src.format = blit.src.resource->format;
   blit.mask = PIPE_MASK_RGBA;
   blit.filter = PIPE_TEX_FILTER_NEAREST;

   pipe->blit(pipe, &blit);
   pipe->flush_resource(pipe, blit.dst.resource);
   pipe->flush(pipe, NULL, 0);
}

static void
d3d12_wgl_framebuffer_set_latency(struct stw_winsys_framebuffer *pframebuffer,
                                  int latency)
{
   if (latency < 1)
      return;

   auto framebuffer = d3d12_wgl_framebuffer(pframebuffer);
   int delta = latency - framebuffer->latency;
   while (delta < 0 && framebuffer->waitable_object) {
      WaitForSingleObject(framebuffer->waitable_object, INFINITE);
      ++delta;
   }
   framebuffer->latency = latency;
   if (framebuffer->swapchain)
      framebuffer->swapchain->SetMaximumFrameLatency(latency);
}

struct stw_winsys_framebuffer *
d3d12_wgl_create_framebuffer(struct pipe_screen *screen,
                             HWND hWnd,
                             int iPixelFormat)
{
   const struct stw_pixelformat_info *pfi =
      stw_pixelformat_get_info(iPixelFormat);
   if ((pfi->pfd.dwFlags & PFD_SUPPORT_GDI))
      return NULL;

   if (pfi->stvis.color_format != PIPE_FORMAT_B8G8R8A8_UNORM &&
       pfi->stvis.color_format != PIPE_FORMAT_R8G8B8A8_UNORM &&
       pfi->stvis.color_format != PIPE_FORMAT_R10G10B10A2_UNORM &&
       pfi->stvis.color_format != PIPE_FORMAT_R16G16B16A16_FLOAT &&
       pfi->stvis.color_format != PIPE_FORMAT_B8G8R8X8_UNORM &&
       pfi->stvis.color_format != PIPE_FORMAT_R8G8B8X8_UNORM)
      return NULL;

   struct d3d12_wgl_framebuffer *fb = CALLOC_STRUCT(d3d12_wgl_framebuffer);
   if (!fb)
      return NULL;

   new (fb) struct d3d12_wgl_framebuffer();

   fb->window = hWnd;
   fb->screen = d3d12_screen(screen);
   fb->single_buffered = (pfi->pfd.dwFlags & PFD_DOUBLEBUFFER) == 0;
   fb->base.destroy = d3d12_wgl_framebuffer_destroy;
   fb->base.resize = d3d12_wgl_framebuffer_resize;
   fb->base.present = d3d12_wgl_framebuffer_present;
   fb->base.get_resource = d3d12_wgl_framebuffer_get_resource;
   fb->base.flush_frontbuffer = d3d12_wgl_framebuffer_flush_frontbuffer;
   fb->base.set_latency = d3d12_wgl_framebuffer_set_latency;

   return &fb->base;
}
