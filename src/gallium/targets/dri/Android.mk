# Mesa 3-D graphics library
#
# Copyright (C) 2015 Chih-Wei Huang <cwhuang@linux.org.tw>
# Copyright (C) 2015 Android-x86 Open Source Project
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

LOCAL_MODULE := gallium_dri

ifeq ($(MESA_LOLLIPOP_BUILD),true)
LOCAL_MODULE_RELATIVE_PATH := $(MESA_DRI_MODULE_REL_PATH)
else
LOCAL_MODULE_PATH := $(MESA_DRI_MODULE_PATH)
LOCAL_UNSTRIPPED_PATH := $(MESA_DRI_MODULE_UNSTRIPPED_PATH)
endif

LOCAL_SRC_FILES := target.c

LOCAL_CFLAGS :=

LOCAL_SHARED_LIBRARIES := \
	libdl \
	libglapi \
	libexpat \

ifneq ($(filter-out swrast,$(MESA_GPU_DRIVERS)),)
LOCAL_CFLAGS += -DHAVE_LIBDRM
LOCAL_SHARED_LIBRARIES += libdrm
endif

ifneq ($(filter freedreno,$(MESA_GPU_DRIVERS)),)
LOCAL_CFLAGS += -DGALLIUM_FREEDRENO
gallium_DRIVERS += libmesa_winsys_freedreno libmesa_pipe_freedreno
LOCAL_SHARED_LIBRARIES += libdrm_freedreno
endif
ifneq ($(filter i915g,$(MESA_GPU_DRIVERS)),)
gallium_DRIVERS += libmesa_winsys_i915 libmesa_pipe_i915
LOCAL_SHARED_LIBRARIES += libdrm_intel
LOCAL_CFLAGS += -DGALLIUM_I915
endif
ifneq ($(filter ilo,$(MESA_GPU_DRIVERS)),)
gallium_DRIVERS += libmesa_winsys_intel libmesa_pipe_ilo
LOCAL_SHARED_LIBRARIES += libdrm_intel
LOCAL_CFLAGS += -DGALLIUM_ILO
endif
ifneq ($(filter nouveau,$(MESA_GPU_DRIVERS)),)
gallium_DRIVERS +=  libmesa_winsys_nouveau libmesa_pipe_nouveau
LOCAL_CFLAGS += -DGALLIUM_NOUVEAU
LOCAL_SHARED_LIBRARIES += libdrm_nouveau
endif
ifneq ($(filter r%,$(MESA_GPU_DRIVERS)),)
ifneq ($(filter r300g,$(MESA_GPU_DRIVERS)),)
gallium_DRIVERS += libmesa_pipe_r300
LOCAL_CFLAGS += -DGALLIUM_R300
endif
ifneq ($(filter r600g,$(MESA_GPU_DRIVERS)),)
gallium_DRIVERS += libmesa_pipe_r600
LOCAL_CFLAGS += -DGALLIUM_R600
endif
ifneq ($(filter radeonsi,$(MESA_GPU_DRIVERS)),)
gallium_DRIVERS += libmesa_pipe_radeonsi libmesa_winsys_amdgpu
LOCAL_SHARED_LIBRARIES += libLLVM libdrm_amdgpu
LOCAL_CFLAGS += -DGALLIUM_RADEONSI
endif
gallium_DRIVERS += libmesa_winsys_radeon libmesa_pipe_radeon
LOCAL_SHARED_LIBRARIES += libdrm_radeon
endif
ifneq ($(filter swrast,$(MESA_GPU_DRIVERS)),)
gallium_DRIVERS += libmesa_pipe_softpipe libmesa_winsys_sw_dri
LOCAL_CFLAGS += -DGALLIUM_SOFTPIPE
endif
ifneq ($(filter vc4,$(MESA_GPU_DRIVERS)),)
LOCAL_CFLAGS += -DGALLIUM_VC4
gallium_DRIVERS += libmesa_winsys_vc4 libmesa_pipe_vc4
endif
ifneq ($(filter virgl,$(MESA_GPU_DRIVERS)),)
LOCAL_CFLAGS += -DGALLIUM_VIRGL
gallium_DRIVERS += libmesa_winsys_virgl libmesa_winsys_virgl_vtest libmesa_pipe_virgl
endif
ifneq ($(filter vmwgfx,$(MESA_GPU_DRIVERS)),)
gallium_DRIVERS += libmesa_winsys_svga libmesa_pipe_svga
LOCAL_CFLAGS += -DGALLIUM_VMWGFX
endif
ifneq ($(filter nouveau r600g,$(MESA_GPU_DRIVERS)),)
LOCAL_SHARED_LIBRARIES += $(if $(filter true,$(MESA_LOLLIPOP_BUILD)),libc++,libstlport)
endif

LOCAL_WHOLE_STATIC_LIBRARIES := \
	$(gallium_DRIVERS) \
	libmesa_st_dri \
	libmesa_st_mesa \
	libmesa_glsl \
	libmesa_compiler \
	libmesa_nir \
	libmesa_dri_common \
	libmesa_megadriver_stub \
	libmesa_gallium \
	libmesa_pipe_loader \
	libmesa_util \
	libmesa_loader \

LOCAL_STATIC_LIBRARIES :=

ifeq ($(MESA_ENABLE_LLVM),true)
LOCAL_STATIC_LIBRARIES += \
	libLLVMR600CodeGen \
	libLLVMR600Desc \
	libLLVMR600Info \
	libLLVMR600AsmPrinter \
	libelf
LOCAL_LDLIBS += $(if $(filter true,$(MESA_LOLLIPOP_BUILD)),-lgcc)
endif

include $(GALLIUM_COMMON_MK)
include $(BUILD_SHARED_LIBRARY)
