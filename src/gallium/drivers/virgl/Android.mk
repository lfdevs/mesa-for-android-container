# Copyright (C) 2014 Emil Velikov <emil.l.velikov@gmail.com>
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

# get C_SOURCES
include $(LOCAL_PATH)/Makefile.sources

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	$(C_SOURCES)

LOCAL_MODULE := libmesa_pipe_virgl

LOCAL_MODULE_CLASS := STATIC_LIBRARIES
intermediates := $(call local-generated-sources-dir)
LOCAL_GENERATED_SOURCES := $(intermediates)/virgl/virgl_driinfo.h

GEN_DRIINFO_INPUTS := \
	$(MESA_TOP)/src/gallium/auxiliary/pipe-loader/driinfo_gallium.h \
	$(LOCAL_PATH)/virgl_driinfo.h.in

MERGE_DRIINFO := $(MESA_TOP)/src/util/merge_driinfo.py

$(intermediates)/virgl/virgl_driinfo.h: $(MERGE_DRIINFO) $(GEN_DRIINFO_INPUTS)
	@mkdir -p $(dir $@)
	@echo "Gen Header: $(PRIVATE_MODULE) <= $(notdir $(@))"
	$(hide) $(MESA_PYTHON2) $(MERGE_DRIINFO) $(GEN_DRIINFO_INPUTS) > $@ || ($(RM) $@; false)

LOCAL_EXPORT_C_INCLUDE_DIRS := $(intermediates)

include $(GALLIUM_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)

ifneq ($(HAVE_GALLIUM_VIRGL),)
GALLIUM_TARGET_DRIVERS += virtio_gpu
$(eval GALLIUM_LIBS += $(LOCAL_MODULE) libmesa_winsys_virgl_common libmesa_winsys_virgl libmesa_winsys_virgl_vtest)
endif
