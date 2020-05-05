/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 2014 Adrián Arroyo Calle <adrian.arroyocalle@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <errno.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>

#include "eglconfig.h"
#include "eglcontext.h"
#include "egldevice.h"
#include "egldisplay.h"
#include "egldriver.h"
#include "eglcurrent.h"
#include "egllog.h"
#include "eglsurface.h"
#include "eglimage.h"
#include "egltypedefs.h"

#include <InterfaceKit.h>
#include <OpenGLKit.h>


#ifdef DEBUG
#	define TRACE(x...) printf("egl_haiku: " x)
#	define CALLED() TRACE("CALLED: %s\n", __PRETTY_FUNCTION__)
#else
#	define TRACE(x...)
#	define CALLED()
#endif
#define ERROR(x...) printf("egl_haiku: " x)


_EGL_DRIVER_STANDARD_TYPECASTS(haiku_egl)


struct haiku_egl_config
{
	_EGLConfig         base;
};

struct haiku_egl_context
{
	_EGLContext	ctx;
};

struct haiku_egl_surface
{
	_EGLSurface surf;
	BGLView* gl;
};


/**
 * Called via eglCreateWindowSurface(), drv->API.CreateWindowSurface().
 */
static _EGLSurface *
haiku_create_window_surface(_EGLDriver *drv, _EGLDisplay *disp,
	_EGLConfig *conf, void *native_window, const EGLint *attrib_list)
{
	CALLED();

	struct haiku_egl_surface* surface;
	surface = (struct haiku_egl_surface*) calloc(1, sizeof (*surface));
	if (!surface) {
		_eglError(EGL_BAD_ALLOC, "haiku_create_window_surface");
		return NULL;
	}

	if (!_eglInitSurface(&surface->surf, disp, EGL_WINDOW_BIT,
		conf, attrib_list, native_window)) {
		free(surface);
		return NULL;
	}

	(&surface->surf)->SwapInterval = 1;

	TRACE("Creating window\n");
	BWindow* win = (BWindow*)native_window;

	TRACE("Creating GL view\n");
	surface->gl = new BGLView(win->Bounds(), "OpenGL", B_FOLLOW_ALL_SIDES, 0,
		BGL_RGB | BGL_DOUBLE | BGL_ALPHA);

	TRACE("Adding GL\n");
	win->AddChild(surface->gl);

	TRACE("Showing window\n");
	win->Show();
	return &surface->surf;
}


static _EGLSurface *
haiku_create_pixmap_surface(_EGLDriver *drv, _EGLDisplay *disp,
	_EGLConfig *conf, void *native_pixmap, const EGLint *attrib_list)
{
	return NULL;
}


static _EGLSurface *
haiku_create_pbuffer_surface(_EGLDriver *drv, _EGLDisplay *disp,
	_EGLConfig *conf, const EGLint *attrib_list)
{
	return NULL;
}


static EGLBoolean
haiku_destroy_surface(_EGLDriver *drv, _EGLDisplay *disp, _EGLSurface *surf)
{
	if (_eglPutSurface(surf)) {
		// XXX: detach haiku_egl_surface::gl from the native window and destroy it
		free(surf);
	}
	return EGL_TRUE;
}


static EGLBoolean
haiku_add_configs_for_visuals(_EGLDisplay *disp)
{
	CALLED();

	struct haiku_egl_config* conf;
	conf = (struct haiku_egl_config*) calloc(1, sizeof (*conf));
	if (!conf)
		return _eglError(EGL_BAD_ALLOC, "haiku_add_configs_for_visuals");

	_eglInitConfig(&conf->base, disp, 1);
	TRACE("Config inited\n");

	conf->base.RedSize = 8;
	conf->base.BlueSize = 8;
	conf->base.GreenSize = 8;
	conf->base.LuminanceSize = 0;
	conf->base.AlphaSize = 8;
	conf->base.ColorBufferType = EGL_RGB_BUFFER;
	conf->base.BufferSize = conf->base.RedSize
	                      + conf->base.GreenSize
	                      + conf->base.BlueSize
	                      + conf->base.AlphaSize;
	conf->base.ConfigCaveat = EGL_NONE;
	conf->base.ConfigId = 1;
	conf->base.BindToTextureRGB = EGL_FALSE;
	conf->base.BindToTextureRGBA = EGL_FALSE;
	conf->base.StencilSize = 0;
	conf->base.TransparentType = EGL_NONE;
	conf->base.NativeRenderable = EGL_TRUE; // Let's say yes
	conf->base.NativeVisualID = 0; // No visual
	conf->base.NativeVisualType = EGL_NONE; // No visual
	conf->base.RenderableType = 0x8;
	conf->base.SampleBuffers = 0; // TODO: How to get the right value ?
	conf->base.Samples = conf->base.SampleBuffers == 0 ? 0 : 0;
	conf->base.DepthSize = 24; // TODO: How to get the right value ?
	conf->base.Level = 0;
	conf->base.MaxPbufferWidth = 0; // TODO: How to get the right value ?
	conf->base.MaxPbufferHeight = 0; // TODO: How to get the right value ?
	conf->base.MaxPbufferPixels = 0; // TODO: How to get the right value ?
	conf->base.SurfaceType = EGL_WINDOW_BIT /*| EGL_PIXMAP_BIT | EGL_PBUFFER_BIT*/;

	TRACE("Config configuated\n");
	if (!_eglValidateConfig(&conf->base, EGL_FALSE)) {
		_eglLog(_EGL_DEBUG, "Haiku: failed to validate config");
		goto cleanup;
	}
	TRACE("Validated config\n");

	_eglLinkConfig(&conf->base);
	if (!_eglGetArraySize(disp->Configs)) {
		_eglLog(_EGL_WARNING, "Haiku: failed to create any config");
		goto cleanup;
	}
	TRACE("Config successfull\n");

	return EGL_TRUE;

cleanup:
	free(conf);
	return EGL_FALSE;
}


extern "C"
EGLBoolean
init_haiku(_EGLDriver *drv, _EGLDisplay *disp)
{
	_EGLDevice *dev;
	CALLED();

	dev = _eglAddDevice(-1, true);
	if (!dev) {
		_eglError(EGL_NOT_INITIALIZED, "DRI2: failed to find EGLDevice");
		return EGL_FALSE;
	}
	disp->Device = dev;

	TRACE("Add configs\n");
	if (!haiku_add_configs_for_visuals(disp))
		return EGL_FALSE;

	disp->Version = 14;

	TRACE("Initialization finished\n");

	return EGL_TRUE;
}


extern "C"
EGLBoolean
haiku_terminate(_EGLDriver* drv,_EGLDisplay *disp)
{
	return EGL_TRUE;
}


extern "C"
_EGLContext*
haiku_create_context(_EGLDriver *drv, _EGLDisplay *disp, _EGLConfig *conf,
	_EGLContext *share_list, const EGLint *attrib_list)
{
	CALLED();

	struct haiku_egl_context* context;
	context = (struct haiku_egl_context*) calloc(1, sizeof (*context));
	if (!context) {
		_eglError(EGL_BAD_ALLOC, "haiku_create_context");
		return NULL;
	}

	if (!_eglInitContext(&context->ctx, disp, conf, attrib_list))
		goto cleanup;

	TRACE("Context created\n");
	return &context->ctx;

cleanup:
	free(context);
	return NULL;
}


extern "C"
EGLBoolean
haiku_destroy_context(_EGLDriver* drv, _EGLDisplay *disp, _EGLContext* ctx)
{
	struct haiku_egl_context* context = haiku_egl_context(ctx);

	if (_eglPutContext(ctx)) {
		// XXX: teardown the context ?
		free(context);
		ctx = NULL;
	}
	return EGL_TRUE;
}


extern "C"
EGLBoolean
haiku_make_current(_EGLDriver* drv, _EGLDisplay *disp, _EGLSurface *dsurf,
	_EGLSurface *rsurf, _EGLContext *ctx)
{
	CALLED();

	struct haiku_egl_context* cont = haiku_egl_context(ctx);
	struct haiku_egl_surface* surf = haiku_egl_surface(dsurf);
	_EGLContext *old_ctx;
	_EGLSurface *old_dsurf, *old_rsurf;

	if (!_eglBindContext(ctx, dsurf, rsurf, &old_ctx, &old_dsurf, &old_rsurf))
		return EGL_FALSE;

	//cont->ctx.DrawSurface=&surf->surf;
	surf->gl->LockGL();
	return EGL_TRUE;
}


extern "C"
EGLBoolean
haiku_swap_buffers(_EGLDriver *drv, _EGLDisplay *disp, _EGLSurface *surf)
{
	struct haiku_egl_surface* surface = haiku_egl_surface(surf);

	surface->gl->SwapBuffers();
	//gl->Render();
	return EGL_TRUE;
}


/**
 * This is the main entrypoint into the driver, called by libEGL.
 * Gets an _EGLDriver object and init its dispatch table.
 */
extern "C"
void
_eglInitDriver(_EGLDriver *driver)
{
	CALLED();

	driver->API.Initialize = init_haiku;
	driver->API.Terminate = haiku_terminate;
	driver->API.CreateContext = haiku_create_context;
	driver->API.DestroyContext = haiku_destroy_context;
	driver->API.MakeCurrent = haiku_make_current;
	driver->API.CreateWindowSurface = haiku_create_window_surface;
	driver->API.CreatePixmapSurface = haiku_create_pixmap_surface;
	driver->API.CreatePbufferSurface = haiku_create_pbuffer_surface;
	driver->API.DestroySurface = haiku_destroy_surface;

	driver->API.SwapBuffers = haiku_swap_buffers;

	TRACE("API Calls defined\n");
}
