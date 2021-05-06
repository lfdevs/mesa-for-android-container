# Copyright © 2016 Intel Corporation
# Copyright © 2016 Mauro Rossi <issor.oruam@gmail.com>
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
#

# ---------------------------------------
# libmesa_isl_gen* common variables
# ---------------------------------------

LIBISL_GENX_COMMON_INCLUDES := \
	$(MESA_TOP)/src/ \
	$(MESA_TOP)/src/gallium/include/

# ---------------------------------------
# Build libmesa_isl_gfx4
# ---------------------------------------

include $(CLEAR_VARS)

LOCAL_MODULE := libmesa_isl_gfx4

LOCAL_SRC_FILES := $(ISL_GFX4_FILES)

LOCAL_CFLAGS := -DGFX_VERx10=40

LOCAL_C_INCLUDES := $(LIBISL_GENX_COMMON_INCLUDES)

LOCAL_WHOLE_STATIC_LIBRARIES := libmesa_genxml

include $(MESA_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)

# ---------------------------------------
# Build libmesa_isl_gfx5
# ---------------------------------------

include $(CLEAR_VARS)

LOCAL_MODULE := libmesa_isl_gfx5

LOCAL_SRC_FILES := $(ISL_GFX5_FILES)

LOCAL_CFLAGS := -DGFX_VERx10=50

LOCAL_C_INCLUDES := $(LIBISL_GENX_COMMON_INCLUDES)

LOCAL_WHOLE_STATIC_LIBRARIES := libmesa_genxml

include $(MESA_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)

# ---------------------------------------
# Build libmesa_isl_gfx6
# ---------------------------------------

include $(CLEAR_VARS)

LOCAL_MODULE := libmesa_isl_gfx6

LOCAL_SRC_FILES := $(ISL_GFX6_FILES)

LOCAL_CFLAGS := -DGFX_VERx10=60

LOCAL_C_INCLUDES := $(LIBISL_GENX_COMMON_INCLUDES)

LOCAL_WHOLE_STATIC_LIBRARIES := libmesa_genxml

include $(MESA_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)

# ---------------------------------------
# Build libmesa_isl_gfx7
# ---------------------------------------

include $(CLEAR_VARS)

LOCAL_MODULE := libmesa_isl_gfx7

LOCAL_SRC_FILES := $(ISL_GFX7_FILES)

LOCAL_CFLAGS := -DGFX_VERx10=70

LOCAL_C_INCLUDES := $(LIBISL_GENX_COMMON_INCLUDES)

LOCAL_WHOLE_STATIC_LIBRARIES := libmesa_genxml

include $(MESA_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)

# ---------------------------------------
# Build libmesa_isl_gfx75
# ---------------------------------------

include $(CLEAR_VARS)

LOCAL_MODULE := libmesa_isl_gfx75

LOCAL_SRC_FILES := $(ISL_GFX75_FILES)

LOCAL_CFLAGS := -DGFX_VERx10=75

LOCAL_C_INCLUDES := $(LIBISL_GENX_COMMON_INCLUDES)

LOCAL_WHOLE_STATIC_LIBRARIES := libmesa_genxml

include $(MESA_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)

# ---------------------------------------
# Build libmesa_isl_gfx8
# ---------------------------------------

include $(CLEAR_VARS)

LOCAL_MODULE := libmesa_isl_gfx8

LOCAL_SRC_FILES := $(ISL_GFX8_FILES)

LOCAL_CFLAGS := -DGFX_VERx10=80

LOCAL_C_INCLUDES := $(LIBISL_GENX_COMMON_INCLUDES)

LOCAL_WHOLE_STATIC_LIBRARIES := libmesa_genxml

include $(MESA_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)

# ---------------------------------------
# Build libmesa_isl_gfx9
# ---------------------------------------

include $(CLEAR_VARS)

LOCAL_MODULE := libmesa_isl_gfx9

LOCAL_SRC_FILES := $(ISL_GFX9_FILES)

LOCAL_CFLAGS := -DGFX_VERx10=90

LOCAL_C_INCLUDES := $(LIBISL_GENX_COMMON_INCLUDES)

LOCAL_WHOLE_STATIC_LIBRARIES := libmesa_genxml

include $(MESA_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)

# ---------------------------------------
# Build libmesa_isl_gfx11
# ---------------------------------------

include $(CLEAR_VARS)

LOCAL_MODULE := libmesa_isl_gfx11

LOCAL_SRC_FILES := $(ISL_GFX11_FILES)

LOCAL_CFLAGS := -DGFX_VERx10=110

LOCAL_C_INCLUDES := $(LIBISL_GENX_COMMON_INCLUDES)

LOCAL_WHOLE_STATIC_LIBRARIES := libmesa_genxml

include $(MESA_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)

# ---------------------------------------
# Build libmesa_isl_gfx12
# ---------------------------------------

include $(CLEAR_VARS)

LOCAL_MODULE := libmesa_isl_gfx12

LOCAL_SRC_FILES := $(ISL_GFX12_FILES)

LOCAL_CFLAGS := -DGFX_VERx10=120

LOCAL_C_INCLUDES := $(LIBISL_GENX_COMMON_INCLUDES)

LOCAL_WHOLE_STATIC_LIBRARIES := libmesa_genxml

include $(MESA_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)

# ---------------------------------------
# Build libmesa_isl_gfx125
# ---------------------------------------

include $(CLEAR_VARS)

LOCAL_MODULE := libmesa_isl_gfx125

LOCAL_SRC_FILES := $(ISL_GFX125_FILES)

LOCAL_CFLAGS := -DGFX_VERx10=125

LOCAL_C_INCLUDES := $(LIBISL_GENX_COMMON_INCLUDES)

LOCAL_WHOLE_STATIC_LIBRARIES := libmesa_genxml

include $(MESA_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)

# ---------------------------------------
# Build libmesa_isl_tiled_memcpy
# ---------------------------------------

include $(CLEAR_VARS)

LOCAL_MODULE := libmesa_isl_tiled_memcpy

LOCAL_C_INCLUDES := \
	$(MESA_TOP)/src/gallium/include \
	$(MESA_TOP)/src/mapi \
	$(MESA_TOP)/src/mesa

LOCAL_SRC_FILES := $(ISL_TILED_MEMCPY_FILES)

include $(MESA_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)

# ---------------------------------------
# Build libmesa_isl_tiled_memcpy_sse41
# ---------------------------------------

ifeq ($(ARCH_X86_HAVE_SSE4_1),true)
include $(CLEAR_VARS)

LOCAL_MODULE := libmesa_isl_tiled_memcpy_sse41

LOCAL_C_INCLUDES := \
	$(MESA_TOP)/src/gallium/include \
	$(MESA_TOP)/src/mapi \
	$(MESA_TOP)/src/mesa

LOCAL_SRC_FILES := $(ISL_TILED_MEMCPY_SSE41_FILES)

LOCAL_CFLAGS += \
        -DUSE_SSE41 -msse4.1 -mstackrealign

include $(MESA_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)
endif

# ---------------------------------------
# Build libmesa_isl
# ---------------------------------------

include $(CLEAR_VARS)

LOCAL_MODULE := libmesa_isl

LOCAL_SRC_FILES := $(ISL_FILES)

LOCAL_C_INCLUDES := \
	$(MESA_TOP)/src/gallium/include \
	$(MESA_TOP)/src/gallium/auxiliary \
	$(MESA_TOP)/src/mapi \
	$(MESA_TOP)/src/mesa \
	$(MESA_TOP)/src/intel

LOCAL_EXPORT_C_INCLUDE_DIRS := $(MESA_TOP)/src/intel

LOCAL_WHOLE_STATIC_LIBRARIES := \
	libmesa_isl_gfx4 \
	libmesa_isl_gfx5 \
	libmesa_isl_gfx6 \
	libmesa_isl_gfx7 \
	libmesa_isl_gfx75 \
	libmesa_isl_gfx8 \
	libmesa_isl_gfx9 \
	libmesa_isl_gfx11 \
	libmesa_isl_gfx12 \
	libmesa_isl_gfx125 \
	libmesa_genxml \
	libmesa_isl_tiled_memcpy

ifeq ($(ARCH_X86_HAVE_SSE4_1),true)
LOCAL_CFLAGS += \
        -DUSE_SSE41
LOCAL_WHOLE_STATIC_LIBRARIES += \
        libmesa_isl_tiled_memcpy_sse41
endif

# Autogenerated sources

LOCAL_MODULE_CLASS := STATIC_LIBRARIES

intermediates := $(call local-generated-sources-dir)

LOCAL_GENERATED_SOURCES += $(addprefix $(intermediates)/, $(ISL_GENERATED_FILES))

define bash-gen
	@mkdir -p $(dir $@)
	@echo "Gen Bash: $(PRIVATE_MODULE) <= $(notdir $(@))"
	$(hide) $(PRIVATE_SCRIPT) --csv $(PRIVATE_CSV) --out $@
endef

isl_format_layout_deps := \
	$(LOCAL_PATH)/isl/gen_format_layout.py \
	$(LOCAL_PATH)/isl/isl_format_layout.csv

$(intermediates)/isl/isl_format_layout.c: PRIVATE_SCRIPT := $(MESA_PYTHON2) $(LOCAL_PATH)/isl/gen_format_layout.py
$(intermediates)/isl/isl_format_layout.c: PRIVATE_CSV := $(LOCAL_PATH)/isl/isl_format_layout.csv
$(intermediates)/isl/isl_format_layout.c: $(isl_format_layout_deps)
	$(call bash-gen)

include $(MESA_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)
