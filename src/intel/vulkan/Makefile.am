# Copyright © 2015 Intel Corporation
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

vulkan_includedir = $(includedir)/vulkan

noinst_HEADERS = \
	$(top_srcdir)/include/vulkan/vk_platform.h \
	$(top_srcdir)/include/vulkan/vulkan.h

vulkan_include_HEADERS = \
	$(top_srcdir)/include/vulkan/vulkan_intel.h

lib_LTLIBRARIES = libvulkan_intel.la

check_LTLIBRARIES = libvulkan-test.la

PER_GEN_LIBS = \
	libanv-gen7.la \
	libanv-gen75.la \
	libanv-gen8.la \
	libanv-gen9.la

noinst_LTLIBRARIES = $(PER_GEN_LIBS)

# The gallium includes are for the util/u_math.h include from main/macros.h

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/src \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/src/vulkan/wsi \
	-I$(top_builddir)/src/compiler \
	-I$(top_srcdir)/src/compiler \
	-I$(top_builddir)/src/compiler/nir \
	-I$(top_srcdir)/src/mapi \
	-I$(top_srcdir)/src/mesa \
	-I$(top_srcdir)/src/mesa/drivers/dri/common \
	-I$(top_srcdir)/src/mesa/drivers/dri/i965 \
	-I$(top_srcdir)/src/gallium/auxiliary \
	-I$(top_srcdir)/src/gallium/include \
	-I$(top_builddir)/src/intel \
	-I$(top_srcdir)/src/intel

if HAVE_PLATFORM_WAYLAND
AM_CPPFLAGS += \
	-I$(top_builddir)/src/egl/wayland/wayland-drm \
	-I$(top_srcdir)/src/egl/wayland/wayland-drm
endif

AM_CPPFLAGS += \
	$(INTEL_CFLAGS) \
	$(VALGRIND_CFLAGS) \
	$(DEFINES)

AM_CFLAGS = \
	$(VISIBILITY_CFLAGS) \
	-Wno-override-init -msse2

libanv_gen7_la_CFLAGS = $(AM_CFLAGS) -DGEN_VERSIONx10=70
libanv_gen7_la_SOURCES = $(GEN7_FILES)

libanv_gen75_la_CFLAGS = $(AM_CFLAGS) -DGEN_VERSIONx10=75
libanv_gen75_la_SOURCES = $(GEN75_FILES)

libanv_gen8_la_CFLAGS = $(AM_CFLAGS) -DGEN_VERSIONx10=80
libanv_gen8_la_SOURCES = $(GEN8_FILES)

libanv_gen9_la_CFLAGS = $(AM_CFLAGS) -DGEN_VERSIONx10=90
libanv_gen9_la_SOURCES = $(GEN9_FILES)


VULKAN_SOURCES = \
	$(VULKAN_GENERATED_FILES) \
	$(VULKAN_FILES)

VULKAN_LIB_DEPS =

if HAVE_PLATFORM_X11
AM_CPPFLAGS += \
	$(XCB_DRI3_CFLAGS) \
	-DVK_USE_PLATFORM_XCB_KHR \
	-DVK_USE_PLATFORM_XLIB_KHR

VULKAN_SOURCES += $(VULKAN_WSI_X11_FILES)

# FIXME: Use pkg-config for X11-xcb ldflags.
VULKAN_LIB_DEPS += $(XCB_DRI3_LIBS) -lX11-xcb
endif


if HAVE_PLATFORM_WAYLAND
AM_CPPFLAGS += \
	$(WAYLAND_CFLAGS) \
	-DVK_USE_PLATFORM_WAYLAND_KHR

VULKAN_SOURCES += $(VULKAN_WSI_WAYLAND_FILES)

VULKAN_LIB_DEPS += \
	$(top_builddir)/src/egl/wayland/wayland-drm/libwayland-drm.la \
	$(WAYLAND_LIBS)
endif

noinst_LTLIBRARIES += libvulkan_common.la
libvulkan_common_la_SOURCES = $(VULKAN_SOURCES)

VULKAN_LIB_DEPS += \
	libvulkan_common.la \
	$(top_builddir)/src/vulkan/wsi/libvulkan_wsi.la \
	$(top_builddir)/src/mesa/drivers/dri/i965/libi965_compiler.la \
	$(top_builddir)/src/compiler/nir/libnir.la \
	$(top_builddir)/src/util/libmesautil.la \
	$(top_builddir)/src/intel/common/libintel_common.la \
	$(top_builddir)/src/intel/isl/libisl.la \
	$(top_builddir)/src/intel/blorp/libblorp.la \
	$(PER_GEN_LIBS) \
	$(PTHREAD_LIBS) \
	$(DLOPEN_LIBS) \
	-lm

nodist_EXTRA_libvulkan_intel_la_SOURCES = dummy.cpp
libvulkan_intel_la_SOURCES = $(VULKAN_GEM_FILES)

vulkan_api_xml = $(top_srcdir)/src/vulkan/registry/vk.xml

anv_entrypoints.h : anv_entrypoints_gen.py $(vulkan_api_xml)
	$(AM_V_GEN) cat $(vulkan_api_xml) |\
	$(PYTHON2) $(srcdir)/anv_entrypoints_gen.py header > $@

anv_entrypoints.c : anv_entrypoints_gen.py $(vulkan_api_xml)
	$(AM_V_GEN) cat $(vulkan_api_xml) |\
	$(PYTHON2) $(srcdir)/anv_entrypoints_gen.py code > $@

BUILT_SOURCES = $(VULKAN_GENERATED_FILES)
CLEANFILES = $(BUILT_SOURCES) dev_icd.json intel_icd.@host_cpu@.json
EXTRA_DIST = \
	$(top_srcdir)/include/vulkan/vk_icd.h \
	anv_entrypoints_gen.py \
	dev_icd.json.in \
	intel_icd.json.in

libvulkan_intel_la_LIBADD = $(VULKAN_LIB_DEPS)

libvulkan_intel_la_LDFLAGS = \
	-shared \
	-module \
	-no-undefined \
	-avoid-version \
	$(BSYMBOLIC) \
	$(GC_SECTIONS) \
	$(LD_NO_UNDEFINED)


icdconfdir = @VULKAN_ICD_INSTALL_DIR@
icdconf_DATA = intel_icd.@host_cpu@.json
# The following is used for development purposes, by setting VK_ICD_FILENAMES.
noinst_DATA = dev_icd.json

dev_icd.json : dev_icd.json.in
	$(AM_V_GEN) $(SED) \
		-e "s#@build_libdir@#${abs_top_builddir}/${LIB_DIR}#" \
		< $(srcdir)/dev_icd.json.in > $@

intel_icd.@host_cpu@.json : intel_icd.json.in
	$(AM_V_GEN) $(SED) \
		-e "s#@install_libdir@#${libdir}#" \
		< $(srcdir)/intel_icd.json.in > $@

# Libvulkan with dummy gem. Used for unit tests.
libvulkan_test_la_SOURCES = $(VULKAN_GEM_STUB_FILES)
libvulkan_test_la_LIBADD = $(VULKAN_LIB_DEPS)

include $(top_srcdir)/install-lib-links.mk

noinst_HEADERS += \
	tests/state_pool_test_helper.h

LDADD = \
	libvulkan-test.la \
	$(PTHREAD_LIBS) -lm -lstdc++

check_PROGRAMS = \
	tests/block_pool_no_free \
	tests/state_pool_no_free \
	tests/state_pool_free_list_only \
	tests/state_pool

TESTS = $(check_PROGRAMS)
