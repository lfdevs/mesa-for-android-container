# Mesa 3-D graphics library
#
# Copyright (C) 2010-2011 Chia-I Wu <olvaffe@gmail.com>
# Copyright (C) 2010-2011 LunarG Inc.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.

# Android.mk for libGLES_mesa

LOCAL_PATH := $(call my-dir)

include $(LOCAL_PATH)/Makefile.sources

# ---------------------------------------
# Build libGLES_mesa
# ---------------------------------------

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	$(LIBEGL_C_FILES) \
	$(dri2_backend_core_FILES) \
	drivers/dri2/platform_android.c

LOCAL_CFLAGS := \
	-D_EGL_NATIVE_PLATFORM=_EGL_PLATFORM_ANDROID \
	-DHAVE_ANDROID_PLATFORM

LOCAL_C_INCLUDES := \
	$(MESA_TOP)/include \
	$(MESA_TOP)/src/egl/main \
	$(MESA_TOP)/src/egl/drivers/dri2

LOCAL_STATIC_LIBRARIES := \
	libmesa_util \
	libmesa_loader

LOCAL_SHARED_LIBRARIES := \
	libdl \
	libglapi \
	libhardware \
	liblog \
	libcutils \
	libsync

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 27; echo $$?), 0)
LOCAL_C_INCLUDES += \
	frameworks/native/libs/nativewindow/include \
	frameworks/native/libs/arect/include
LOCAL_HEADER_LIBRARIES += libnativebase_headers
endif

ifeq ($(BOARD_USES_DRM_GRALLOC),true)
	LOCAL_CFLAGS += -DHAVE_DRM_GRALLOC
	LOCAL_SHARED_LIBRARIES += libgralloc_drm
endif

ifeq ($(filter $(MESA_ANDROID_MAJOR_VERSION), 4 5 6 7),)
LOCAL_SHARED_LIBRARIES += libnativewindow
endif

# This controls enabling building of driver libraries
ifneq ($(HAVE_I915_DRI),)
LOCAL_REQUIRED_MODULES += i915_dri
endif
ifneq ($(HAVE_I965_DRI),)
LOCAL_REQUIRED_MODULES += i965_dri
endif
ifneq ($(MESA_BUILD_GALLIUM),)
LOCAL_REQUIRED_MODULES += gallium_dri
endif

LOCAL_MODULE := libGLES_mesa
LOCAL_MODULE_RELATIVE_PATH := egl

include $(MESA_COMMON_MK)
include $(BUILD_SHARED_LIBRARY)
