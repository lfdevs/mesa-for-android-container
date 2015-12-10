# Copyright © 2012 Intel Corporation
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.

include Makefile.sources

AM_CFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/src/egl/main \
	-I$(top_srcdir)/src/gbm/main \
	-I$(top_srcdir)/src \
	$(DEFINES) \
	$(VISIBILITY_CFLAGS) \
	$(LIBDRM_CFLAGS) \
	$(EGL_CFLAGS) \
	-D_EGL_NATIVE_PLATFORM=$(EGL_NATIVE_PLATFORM)

lib_LTLIBRARIES = libEGL.la

libEGL_la_SOURCES = \
	$(LIBEGL_C_FILES)

libEGL_la_LIBADD = \
	$(EGL_LIB_DEPS)
libEGL_la_LDFLAGS = \
	-no-undefined \
	-version-number 1:0 \
	$(BSYMBOLIC) \
	$(GC_SECTIONS) \
	$(LD_NO_UNDEFINED)

dri2_backend_FILES =
dri3_backend_FILES =

if HAVE_EGL_PLATFORM_X11
AM_CFLAGS += -DHAVE_X11_PLATFORM
AM_CFLAGS += $(XCB_DRI2_CFLAGS)
libEGL_la_LIBADD += $(XCB_DRI2_LIBS)
dri2_backend_FILES += drivers/dri2/platform_x11.c

if HAVE_DRI3
dri3_backend_FILES += \
	drivers/dri2/platform_x11_dri3.c \
	drivers/dri2/platform_x11_dri3.h

libEGL_la_LIBADD += $(top_builddir)/src/loader/libloader_dri3_helper.la
endif
endif

if HAVE_EGL_PLATFORM_WAYLAND
AM_CFLAGS += -DHAVE_WAYLAND_PLATFORM
AM_CFLAGS += $(WAYLAND_CFLAGS)
libEGL_la_LIBADD += $(WAYLAND_LIBS)
libEGL_la_LIBADD += $(LIBDRM_LIBS)
libEGL_la_LIBADD += $(top_builddir)/src/egl/wayland/wayland-drm/libwayland-drm.la
dri2_backend_FILES += drivers/dri2/platform_wayland.c
endif

if HAVE_EGL_PLATFORM_DRM
AM_CFLAGS += -DHAVE_DRM_PLATFORM
libEGL_la_LIBADD += $(top_builddir)/src/gbm/libgbm.la
dri2_backend_FILES += drivers/dri2/platform_drm.c
endif

if HAVE_EGL_PLATFORM_SURFACELESS
AM_CFLAGS += -DHAVE_SURFACELESS_PLATFORM
dri2_backend_FILES += drivers/dri2/platform_surfaceless.c
endif

if HAVE_EGL_DRIVER_DRI2
AM_CFLAGS += \
	-I$(top_srcdir)/src/loader \
	-I$(top_srcdir)/src/egl/drivers/dri2 \
	-I$(top_srcdir)/src/gbm/backends/dri \
	-I$(top_srcdir)/src/egl/wayland/wayland-egl \
	-I$(top_srcdir)/src/egl/wayland/wayland-drm \
	-I$(top_builddir)/src/egl/wayland/wayland-drm \
	-DDEFAULT_DRIVER_DIR=\"$(DRI_DRIVER_SEARCH_DIR)\" \
	-D_EGL_BUILT_IN_DRIVER_DRI2

libEGL_la_SOURCES += \
	$(dri2_backend_core_FILES) \
	$(dri2_backend_FILES) \
	$(dri3_backend_FILES)

libEGL_la_LIBADD += $(top_builddir)/src/loader/libloader.la
libEGL_la_LIBADD += $(DLOPEN_LIBS) $(LIBDRM_LIBS)
endif

include $(top_srcdir)/install-lib-links.mk

pkgconfigdir = $(libdir)/pkgconfig

pkgconfig_DATA = main/egl.pc

khrdir = $(includedir)/KHR
khr_HEADERS = $(top_srcdir)/include/KHR/khrplatform.h

egldir = $(includedir)/EGL
egl_HEADERS = \
	$(top_srcdir)/include/EGL/eglext.h \
	$(top_srcdir)/include/EGL/egl.h \
	$(top_srcdir)/include/EGL/eglextchromium.h \
	$(top_srcdir)/include/EGL/eglmesaext.h \
	$(top_srcdir)/include/EGL/eglplatform.h

TESTS = egl-symbols-check

EXTRA_DIST = \
	egl-symbols-check \
	SConscript \
	drivers/haiku \
	docs \
	main/egl.def \
	main/README.txt
