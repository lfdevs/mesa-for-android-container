#
# Copyright (C) 2016 Linaro, Ltd., Rob Herring <robh@kernel.org>
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

ifeq ($(LOCAL_MODULE_CLASS),)
LOCAL_MODULE_CLASS := STATIC_LIBRARIES
endif

ir3_nir_trig_deps := \
	$(LOCAL_PATH)/ir3/ir3_nir_trig.py \
	$(MESA_TOP)/src/compiler/nir/nir_algebraic.py

intermediates := $(call local-generated-sources-dir)

$(intermediates)/ir3/ir3_nir_trig.c: $(ir3_nir_trig_deps)
	@mkdir -p $(dir $@)
	$(hide) PYTHONPATH=$(MESA_TOP)/src/compiler/nir $(MESA_PYTHON2) $< > $@

LOCAL_GENERATED_SOURCES += $(addprefix $(intermediates)/, \
	$(ir3_GENERATED_FILES))
