# Copyright (C) 2019 Icenowy Zheng <icenowy@aosc.io>
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

LOCAL_SRC_FILES := \
	ir/gp/codegen.c \
	ir/gp/codegen.h \
	ir/gp/disasm.c \
	ir/gp/gpir.h \
	ir/gp/instr.c \
	ir/gp/lower.c \
	ir/gp/nir.c \
	ir/gp/node.c \
	ir/gp/physical_regalloc.c \
	ir/gp/reduce_scheduler.c \
	ir/gp/scheduler.c \
	ir/gp/value_regalloc.c \
	ir/lima_ir.h \
	ir/lima_nir_lower_uniform_to_scalar.c \
	ir/pp/codegen.c \
	ir/pp/codegen.h \
	ir/pp/disasm.c \
	ir/pp/instr.c \
	ir/pp/lower.c \
	ir/pp/nir.c \
	ir/pp/node.c \
	ir/pp/node_to_instr.c \
	ir/pp/ppir.h \
	ir/pp/regalloc.c \
	ir/pp/scheduler.c \
	lima_bo.c \
	lima_bo.h \
	lima_context.c \
	lima_context.h \
	lima_draw.c \
	lima_fence.c \
	lima_fence.h \
	lima_program.c \
	lima_program.h \
	lima_query.c \
	lima_resource.c \
	lima_resource.h \
	lima_screen.c \
	lima_screen.h \
	lima_state.c \
	lima_submit.c \
	lima_submit.h \
	lima_texture.c \
	lima_texture.h \
	lima_tiling.c \
	lima_tiling.h \
	lima_util.c \
	lima_util.h

LOCAL_MODULE := libmesa_pipe_lima

LOCAL_SHARED_LIBRARIES := libdrm

LOCAL_STATIC_LIBRARIES := libmesa_nir

include $(GALLIUM_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)

ifneq ($(HAVE_GALLIUM_LIMA),)
GALLIUM_TARGET_DRIVERS += lima
$(eval GALLIUM_LIBS += $(LOCAL_MODULE) libmesa_winsys_lima)
$(eval GALLIUM_SHARED_LIBS += $(LOCAL_SHARED_LIBRARIES))
endif
