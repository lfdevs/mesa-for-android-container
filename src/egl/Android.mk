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
	-D_EGL_BUILT_IN_DRIVER_DRI2 \
	-DHAVE_ANDROID_PLATFORM

LOCAL_C_INCLUDES := \
	$(MESA_TOP)/src/egl/main \
	$(MESA_TOP)/src/egl/drivers/dri2 \

LOCAL_STATIC_LIBRARIES := \
	libmesa_loader

LOCAL_SHARED_LIBRARIES := \
	libdl \
	libhardware \
	liblog \
	libcutils \
	libgralloc_drm \

ifeq ($(shell echo "$(MESA_ANDROID_VERSION) >= 4.2" | bc),1)
LOCAL_SHARED_LIBRARIES += libsync
endif

# add libdrm if there are hardware drivers
ifneq ($(filter-out swrast,$(MESA_GPU_DRIVERS)),)
LOCAL_CFLAGS += -DHAVE_LIBDRM
LOCAL_SHARED_LIBRARIES += libdrm
endif

ifeq ($(strip $(MESA_BUILD_CLASSIC)),true)
# require i915_dri and/or i965_dri
LOCAL_REQUIRED_MODULES += \
	$(addsuffix _dri, $(filter i915 i965, $(MESA_GPU_DRIVERS)))
endif # MESA_BUILD_CLASSIC

ifeq ($(strip $(MESA_BUILD_GALLIUM)),true)
LOCAL_REQUIRED_MODULES += gallium_dri
endif # MESA_BUILD_GALLIUM


LOCAL_MODULE := libGLES_mesa
ifeq ($(MESA_LOLLIPOP_BUILD),true)
LOCAL_MODULE_RELATIVE_PATH := egl
else
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/egl
endif

include $(MESA_COMMON_MK)
include $(BUILD_SHARED_LIBRARY)
