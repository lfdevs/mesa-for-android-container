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

ANV_ENTRYPOINTS_GEN_SCRIPT := $(LOCAL_PATH)/vulkan/anv_entrypoints_gen.py
ANV_EXTENSIONS_GEN_SCRIPT := $(LOCAL_PATH)/vulkan/anv_extensions_gen.py
ANV_EXTENSIONS_SCRIPT := $(LOCAL_PATH)/vulkan/anv_extensions.py
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
	libmesa_vulkan_common \
	libmesa_genxml \
	libmesa_nir

ANV_SHARED_LIBRARIES := libdrm

ifeq ($(filter $(MESA_ANDROID_MAJOR_VERSION), 4 5 6 7),)
ANV_SHARED_LIBRARIES += libnativewindow
endif

#
# libanv for gen7
#

include $(CLEAR_VARS)
LOCAL_MODULE := libmesa_anv_gen7
LOCAL_MODULE_CLASS := STATIC_LIBRARIES

LOCAL_SRC_FILES := $(VULKAN_GEN7_FILES)
LOCAL_CFLAGS := -DGEN_VERSIONx10=70

LOCAL_C_INCLUDES := $(VULKAN_COMMON_INCLUDES)

LOCAL_STATIC_LIBRARIES := $(ANV_STATIC_LIBRARIES)

LOCAL_SHARED_LIBRARIES := $(ANV_SHARED_LIBRARIES)
LOCAL_HEADER_LIBRARIES += $(VULKAN_COMMON_HEADER_LIBRARIES)

include $(MESA_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)

#
# libanv for gen75
#

include $(CLEAR_VARS)
LOCAL_MODULE := libmesa_anv_gen75
LOCAL_MODULE_CLASS := STATIC_LIBRARIES

LOCAL_SRC_FILES := $(VULKAN_GEN75_FILES)
LOCAL_CFLAGS := -DGEN_VERSIONx10=75

LOCAL_C_INCLUDES := $(VULKAN_COMMON_INCLUDES)

LOCAL_STATIC_LIBRARIES := $(ANV_STATIC_LIBRARIES)

LOCAL_SHARED_LIBRARIES := $(ANV_SHARED_LIBRARIES)
LOCAL_HEADER_LIBRARIES += $(VULKAN_COMMON_HEADER_LIBRARIES)

include $(MESA_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)

#
# libanv for gen8
#

include $(CLEAR_VARS)
LOCAL_MODULE := libmesa_anv_gen8
LOCAL_MODULE_CLASS := STATIC_LIBRARIES

LOCAL_SRC_FILES := $(VULKAN_GEN8_FILES)
LOCAL_CFLAGS := -DGEN_VERSIONx10=80

LOCAL_C_INCLUDES := $(VULKAN_COMMON_INCLUDES)

LOCAL_STATIC_LIBRARIES := $(ANV_STATIC_LIBRARIES)

LOCAL_SHARED_LIBRARIES := $(ANV_SHARED_LIBRARIES)
LOCAL_HEADER_LIBRARIES += $(VULKAN_COMMON_HEADER_LIBRARIES)

include $(MESA_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)

#
# libanv for gen9
#

include $(CLEAR_VARS)
LOCAL_MODULE := libmesa_anv_gen9
LOCAL_MODULE_CLASS := STATIC_LIBRARIES

LOCAL_SRC_FILES := $(VULKAN_GEN9_FILES)
LOCAL_CFLAGS := -DGEN_VERSIONx10=90

LOCAL_C_INCLUDES := $(VULKAN_COMMON_INCLUDES)

LOCAL_STATIC_LIBRARIES := $(ANV_STATIC_LIBRARIES)

LOCAL_SHARED_LIBRARIES := $(ANV_SHARED_LIBRARIES)
LOCAL_HEADER_LIBRARIES += $(VULKAN_COMMON_HEADER_LIBRARIES)

include $(MESA_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)

#
# libanv for gen11
#

include $(CLEAR_VARS)
LOCAL_MODULE := libmesa_anv_gen11
LOCAL_MODULE_CLASS := STATIC_LIBRARIES

LOCAL_SRC_FILES := $(VULKAN_GEN11_FILES)
LOCAL_CFLAGS := -DGEN_VERSIONx10=110

LOCAL_C_INCLUDES := $(VULKAN_COMMON_INCLUDES)

LOCAL_STATIC_LIBRARIES := $(ANV_STATIC_LIBRARIES)

LOCAL_SHARED_LIBRARIES := $(ANV_SHARED_LIBRARIES)
LOCAL_HEADER_LIBRARIES += $(VULKAN_COMMON_HEADER_LIBRARIES)

include $(MESA_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)

#
# libanv for gen12
#

include $(CLEAR_VARS)
LOCAL_MODULE := libmesa_anv_gen12
LOCAL_MODULE_CLASS := STATIC_LIBRARIES

LOCAL_SRC_FILES := $(VULKAN_GEN12_FILES)
LOCAL_CFLAGS := -DGEN_VERSIONx10=120

LOCAL_C_INCLUDES := $(VULKAN_COMMON_INCLUDES)

LOCAL_STATIC_LIBRARIES := $(ANV_STATIC_LIBRARIES)

LOCAL_SHARED_LIBRARIES := $(ANV_SHARED_LIBRARIES)
LOCAL_HEADER_LIBRARIES += $(VULKAN_COMMON_HEADER_LIBRARIES)

include $(MESA_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)

#
# libanv for gen125
#

include $(CLEAR_VARS)
LOCAL_MODULE := libmesa_anv_gen125
LOCAL_MODULE_CLASS := STATIC_LIBRARIES

LOCAL_SRC_FILES := $(VULKAN_GEN125_FILES)
LOCAL_CFLAGS := -DGEN_VERSIONx10=125

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

# The rule generates both C and H files, but due to some strange
# reason generating the files once leads to link-time issues.
# Work around create them here as well - we're safe from race
# conditions since they are stored in another location.

LOCAL_GENERATED_SOURCES := $(addprefix $(intermediates)/,$(VULKAN_GENERATED_FILES))

$(intermediates)/vulkan/anv_entrypoints.c: $(ANV_ENTRYPOINTS_GEN_SCRIPT) \
					   $(ANV_EXTENSIONS_SCRIPT) \
					   $(VULKAN_API_XML)
	@mkdir -p $(dir $@)
	$(MESA_PYTHON2) $(ANV_ENTRYPOINTS_GEN_SCRIPT) \
		--xml $(VULKAN_API_XML) \
		--outdir $(dir $@)

$(intermediates)/vulkan/anv_entrypoints.h: $(intermediates)/vulkan/anv_entrypoints.c

$(intermediates)/vulkan/anv_extensions.c: $(ANV_EXTENSIONS_GEN_SCRIPT) \
					  $(ANV_EXTENSIONS_SCRIPT) \
					  $(VULKAN_API_XML)
	@mkdir -p $(dir $@)
	$(MESA_PYTHON2) $(ANV_EXTENSIONS_GEN_SCRIPT) \
		--xml $(VULKAN_API_XML) \
		--out-c $@

$(intermediates)/vulkan/anv_extensions.h: $(ANV_EXTENSIONS_GEN_SCRIPT) \
					   $(ANV_EXTENSIONS_SCRIPT) \
					   $(VULKAN_API_XML)
	@mkdir -p $(dir $@)
	$(MESA_PYTHON2) $(ANV_EXTENSIONS_GEN_SCRIPT) \
		--xml $(VULKAN_API_XML) \
		--out-h $@

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
	libmesa_anv_gen7 \
	libmesa_anv_gen75 \
	libmesa_anv_gen8 \
	libmesa_anv_gen9 \
	libmesa_anv_gen11 \
	libmesa_anv_gen12 \
	libmesa_anv_gen125 \
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
