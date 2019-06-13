# Copyright © 2018 Intel Corporation
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
# Build libmesa_intel_perf
# ---------------------------------------

include $(CLEAR_VARS)

LOCAL_MODULE := libmesa_intel_perf

LOCAL_MODULE_CLASS := STATIC_LIBRARIES

intermediates := $(call local-generated-sources-dir)

LOCAL_C_INCLUDES := $(MESA_TOP)/include/drm-uapi

LOCAL_SRC_FILES := $(GEN_PERF_FILES)

LOCAL_GENERATED_SOURCES += $(addprefix $(intermediates)/, \
	$(GEN_PERF_GENERATED_FILES))

$(intermediates)/perf/gen_perf_metrics.c: $(LOCAL_PATH)/perf/gen_perf.py $(addprefix $(MESA_TOP)/src/intel/,$(GEN_PERF_XML_FILES))
	@echo "target Generated: $(PRIVATE_MODULE) <= $(notdir $(@))"
	@mkdir -p $(dir $@)
	$(hide) $(MESA_PYTHON2) $< \
	--code=$@ \
	--header=$(@:%.c=%.h) \
	$(addprefix $(MESA_TOP)/src/intel/,$(GEN_PERF_XML_FILES))

$(intermediates)/perf/gen_perf_metrics.h: $(intermediates)/perf/gen_perf_metrics.c

include $(MESA_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)
