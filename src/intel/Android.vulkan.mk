# Copyright © 2017 Intel Corporation
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

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
include $(LOCAL_PATH)/Makefile.sources

VK_ENTRYPOINTS_GEN_SCRIPT := $(MESA_TOP)/src/vulkan/util/vk_entrypoints_gen.py
VULKAN_API_XML := $(MESA_TOP)/src/vulkan/registry/vk.xml

VULKAN_COMMON_INCLUDES := \
	$(MESA_TOP)/include \
	$(MESA_TOP)/src/mapi \
	$(MESA_TOP)/src/gallium/auxiliary \
	$(MESA_TOP)/src/gallium/include \
	$(MESA_TOP)/src/mesa \
	$(MESA_TOP)/src/vulkan/wsi \
	$(MESA_TOP)/src/vulkan/util \
	$(MESA_TOP)/src/intel \
	$(MESA_TOP)/src/intel/vulkan \
	frameworks/native/vulkan/include

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 27; echo $$?), 0)
VULKAN_COMMON_INCLUDES += \
	frameworks/native/vulkan/include \
	frameworks/native/libs/nativebase/include \
	frameworks/native/libs/nativewindow/include \
	frameworks/native/libs/arect/include

VULKAN_COMMON_HEADER_LIBRARIES := \
	libcutils_headers \
	libhardware_headers
endif

ANV_STATIC_LIBRARIES := \
	libmesa_vulkan_util \
	libmesa_vulkan_common \
	libmesa_genxml \
	libmesa_nir

ANV_SHARED_LIBRARIES := libdrm

ifeq ($(filter $(MESA_ANDROID_MAJOR_VERSION), 4 5 6 7),)
ANV_SHARED_LIBRARIES += libnativewindow
endif

#
# libanv for gfx7
#

include $(CLEAR_VARS)
LOCAL_MODULE := libmesa_anv_gfx7
LOCAL_MODULE_CLASS := STATIC_LIBRARIES

LOCAL_SRC_FILES := $(VULKAN_GFX7_FILES)
LOCAL_CFLAGS := -DGFX_VERx10=70

LOCAL_C_INCLUDES := $(VULKAN_COMMON_INCLUDES)

LOCAL_STATIC_LIBRARIES := $(ANV_STATIC_LIBRARIES)

LOCAL_SHARED_LIBRARIES := $(ANV_SHARED_LIBRARIES)
LOCAL_HEADER_LIBRARIES += $(VULKAN_COMMON_HEADER_LIBRARIES)

include $(MESA_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)

#
# libanv for gfx75
#

include $(CLEAR_VARS)
LOCAL_MODULE := libmesa_anv_gfx75
LOCAL_MODULE_CLASS := STATIC_LIBRARIES

LOCAL_SRC_FILES := $(VULKAN_GFX75_FILES)
LOCAL_CFLAGS := -DGFX_VERx10=75

LOCAL_C_INCLUDES := $(VULKAN_COMMON_INCLUDES)

LOCAL_STATIC_LIBRARIES := $(ANV_STATIC_LIBRARIES)

LOCAL_SHARED_LIBRARIES := $(ANV_SHARED_LIBRARIES)
LOCAL_HEADER_LIBRARIES += $(VULKAN_COMMON_HEADER_LIBRARIES)

include $(MESA_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)

#
# libanv for gfx8
#

include $(CLEAR_VARS)
LOCAL_MODULE := libmesa_anv_gfx8
LOCAL_MODULE_CLASS := STATIC_LIBRARIES

LOCAL_SRC_FILES := $(VULKAN_GFX8_FILES)
LOCAL_CFLAGS := -DGFX_VERx10=80

LOCAL_C_INCLUDES := $(VULKAN_COMMON_INCLUDES)

LOCAL_STATIC_LIBRARIES := $(ANV_STATIC_LIBRARIES)

LOCAL_SHARED_LIBRARIES := $(ANV_SHARED_LIBRARIES)
LOCAL_HEADER_LIBRARIES += $(VULKAN_COMMON_HEADER_LIBRARIES)

include $(MESA_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)

#
# libanv for gfx9
#

include $(CLEAR_VARS)
LOCAL_MODULE := libmesa_anv_gfx9
LOCAL_MODULE_CLASS := STATIC_LIBRARIES

LOCAL_SRC_FILES := $(VULKAN_GFX9_FILES)
LOCAL_CFLAGS := -DGFX_VERx10=90

LOCAL_C_INCLUDES := $(VULKAN_COMMON_INCLUDES)

LOCAL_STATIC_LIBRARIES := $(ANV_STATIC_LIBRARIES)

LOCAL_SHARED_LIBRARIES := $(ANV_SHARED_LIBRARIES)
LOCAL_HEADER_LIBRARIES += $(VULKAN_COMMON_HEADER_LIBRARIES)

include $(MESA_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)

#
# libanv for gfx11
#

include $(CLEAR_VARS)
LOCAL_MODULE := libmesa_anv_gfx11
LOCAL_MODULE_CLASS := STATIC_LIBRARIES

LOCAL_SRC_FILES := $(VULKAN_GFX11_FILES)
LOCAL_CFLAGS := -DGFX_VERx10=110

LOCAL_C_INCLUDES := $(VULKAN_COMMON_INCLUDES)

LOCAL_STATIC_LIBRARIES := $(ANV_STATIC_LIBRARIES)

LOCAL_SHARED_LIBRARIES := $(ANV_SHARED_LIBRARIES)
LOCAL_HEADER_LIBRARIES += $(VULKAN_COMMON_HEADER_LIBRARIES)

include $(MESA_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)

#
# libanv for gfx12
#

include $(CLEAR_VARS)
LOCAL_MODULE := libmesa_anv_gfx12
LOCAL_MODULE_CLASS := STATIC_LIBRARIES

LOCAL_SRC_FILES := $(VULKAN_GFX12_FILES)
LOCAL_CFLAGS := -DGFX_VERx10=120

LOCAL_C_INCLUDES := $(VULKAN_COMMON_INCLUDES)

LOCAL_STATIC_LIBRARIES := $(ANV_STATIC_LIBRARIES)

LOCAL_SHARED_LIBRARIES := $(ANV_SHARED_LIBRARIES)
LOCAL_HEADER_LIBRARIES += $(VULKAN_COMMON_HEADER_LIBRARIES)

include $(MESA_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)

#
# libanv for gfx125
#

include $(CLEAR_VARS)
LOCAL_MODULE := libmesa_anv_gfx125
LOCAL_MODULE_CLASS := STATIC_LIBRARIES

LOCAL_SRC_FILES := $(VULKAN_GFX125_FILES)
LOCAL_CFLAGS := -DGFX_VERx10=125

LOCAL_C_INCLUDES := $(VULKAN_COMMON_INCLUDES)

LOCAL_STATIC_LIBRARIES := $(ANV_STATIC_LIBRARIES)

LOCAL_SHARED_LIBRARIES := $(ANV_SHARED_LIBRARIES)
LOCAL_HEADER_LIBRARIES += $(VULKAN_COMMON_HEADER_LIBRARIES)

include $(MESA_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)


#
# libmesa_vulkan_common
#

include $(CLEAR_VARS)
LOCAL_MODULE := libmesa_vulkan_common
LOCAL_MODULE_CLASS := STATIC_LIBRARIES

intermediates := $(call local-generated-sources-dir)

LOCAL_SRC_FILES := $(VULKAN_FILES)

LOCAL_EXPORT_C_INCLUDE_DIRS := $(intermediates)/vulkan

LOCAL_C_INCLUDES := \
	$(LOCAL_EXPORT_C_INCLUDE_DIRS) \
	$(VULKAN_COMMON_INCLUDES)

LOCAL_STATIC_LIBRARIES := \
	libmesa_nir \
	libmesa_genxml \
	libmesa_git_sha1 \
	libmesa_vulkan_util \
	libmesa_util

LOCAL_GENERATED_SOURCES := $(addprefix $(intermediates)/,$(VULKAN_GENERATED_FILES))

ANV_VK_ENTRYPOINTS_GEN_ARGS= \
	--proto --weak --prefix anv \
	--device-prefix gfx7 --device-prefix gfx75 \
	--device-prefix gfx8 --device-prefix gfx9 \
	--device-prefix gfx11 --device-prefix gfx12 \
	--device-prefix gfx125

$(intermediates)/vulkan/anv_entrypoints.c: $(VK_ENTRYPOINTS_GEN_SCRIPT) \
					   $(VULKAN_API_XML)
	@mkdir -p $(dir $@)
	$(MESA_PYTHON2) $(VK_ENTRYPOINTS_GEN_SCRIPT) \
		--xml $(VULKAN_API_XML) \
		$(ANV_VK_ENTRYPOINTS_GEN_ARGS) \
		--out-c $@ --out-h $(dir $@)/anv_entrypoints.h

$(intermediates)/vulkan/anv_entrypoints.h: $(intermediates)/vulkan/anv_entrypoints.c

LOCAL_SHARED_LIBRARIES := $(ANV_SHARED_LIBRARIES)
LOCAL_HEADER_LIBRARIES += $(VULKAN_COMMON_HEADER_LIBRARIES)

include $(MESA_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)


#
# libvulkan_intel
#

include $(CLEAR_VARS)

LOCAL_MODULE := vulkan.$(TARGET_BOARD_PLATFORM)
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_PROPRIETARY_MODULE := true
LOCAL_MODULE_RELATIVE_PATH := hw

LOCAL_LDFLAGS += -Wl,--build-id=sha1

LOCAL_SRC_FILES := \
	$(VULKAN_GEM_FILES) \
	$(VULKAN_ANDROID_FILES)

LOCAL_C_INCLUDES := \
	$(VULKAN_COMMON_INCLUDES) \

LOCAL_WHOLE_STATIC_LIBRARIES := \
	libmesa_nir \
	libmesa_isl \
	libmesa_glsl \
	libmesa_util \
	libmesa_blorp \
	libmesa_compiler \
	libmesa_intel_common \
	libmesa_intel_dev \
	libmesa_intel_perf \
	libmesa_vulkan_common \
	libmesa_vulkan_util \
	libmesa_anv_gfx7 \
	libmesa_anv_gfx75 \
	libmesa_anv_gfx8 \
	libmesa_anv_gfx9 \
	libmesa_anv_gfx11 \
	libmesa_anv_gfx12 \
	libmesa_anv_gfx125 \
	libmesa_intel_compiler

LOCAL_SHARED_LIBRARIES := $(ANV_SHARED_LIBRARIES) libz libsync liblog libcutils
LOCAL_HEADER_LIBRARIES += $(VULKAN_COMMON_HEADER_LIBRARIES)

# If Android version >=8 MESA should static link libexpat else should dynamic link
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 27; echo $$?), 0)
LOCAL_STATIC_LIBRARIES := \
       libexpat
else
 LOCAL_SHARED_LIBRARIES += \
        libexpat
endif

include $(MESA_COMMON_MK)
include $(BUILD_SHARED_LIBRARY)
