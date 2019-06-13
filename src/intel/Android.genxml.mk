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
# Build libmesa_genxml
# ---------------------------------------

include $(CLEAR_VARS)

LOCAL_MODULE := libmesa_genxml

LOCAL_MODULE_CLASS := STATIC_LIBRARIES

intermediates := $(call local-generated-sources-dir)

# dummy.c source file is generated to meet the build system's rules.
LOCAL_GENERATED_SOURCES += $(intermediates)/dummy.c

$(intermediates)/dummy.c:
	@mkdir -p $(dir $@)
	@echo "Gen Dummy: $(PRIVATE_MODULE) <= $(notdir $(@))"
	$(hide) touch $@

# This is the list of auto-generated files headers
LOCAL_GENERATED_SOURCES += $(addprefix $(intermediates)/, $(GENXML_GENERATED_FILES))

define header-gen
	@mkdir -p $(dir $@)
	@echo "Gen Header: $(PRIVATE_MODULE) <= $(notdir $(@))"
	$(hide) $(PRIVATE_SCRIPT) $(PRIVATE_SCRIPT_FLAGS) $(PRIVATE_XML) > $@
endef

$(intermediates)/genxml/genX_bits.h: PRIVATE_SCRIPT := $(MESA_PYTHON2) $(LOCAL_PATH)/genxml/gen_bits_header.py
$(intermediates)/genxml/genX_bits.h: PRIVATE_SCRIPT_FLAGS := --cpp-guard=GENX_BITS_H
$(intermediates)/genxml/genX_bits.h: PRIVATE_XML := $(addprefix $(LOCAL_PATH)/,$(GENXML_XML_FILES))
$(intermediates)/genxml/genX_bits.h: $(LOCAL_PATH)/genxml/gen_bits_header.py
$(intermediates)/genxml/genX_bits.h: $(addprefix $(LOCAL_PATH)/,$(GENXML_XML_FILES))
	$(call header-gen)

$(intermediates)/genxml/gen4_pack.h: PRIVATE_SCRIPT := $(MESA_PYTHON2) $(LOCAL_PATH)/genxml/gen_pack_header.py
$(intermediates)/genxml/gen4_pack.h: PRIVATE_XML := $(LOCAL_PATH)/genxml/gen4.xml
$(intermediates)/genxml/gen4_pack.h: $(LOCAL_PATH)/genxml/gen4.xml $(LOCAL_PATH)/genxml/gen_pack_header.py
	$(call header-gen)

$(intermediates)/genxml/gen45_pack.h: PRIVATE_SCRIPT := $(MESA_PYTHON2) $(LOCAL_PATH)/genxml/gen_pack_header.py
$(intermediates)/genxml/gen45_pack.h: PRIVATE_XML := $(LOCAL_PATH)/genxml/gen45.xml
$(intermediates)/genxml/gen45_pack.h: $(LOCAL_PATH)/genxml/gen45.xml $(LOCAL_PATH)/genxml/gen_pack_header.py
	$(call header-gen)

$(intermediates)/genxml/gen5_pack.h: PRIVATE_SCRIPT := $(MESA_PYTHON2) $(LOCAL_PATH)/genxml/gen_pack_header.py
$(intermediates)/genxml/gen5_pack.h: PRIVATE_XML := $(LOCAL_PATH)/genxml/gen5.xml
$(intermediates)/genxml/gen5_pack.h: $(LOCAL_PATH)/genxml/gen5.xml $(LOCAL_PATH)/genxml/gen_pack_header.py
	$(call header-gen)

$(intermediates)/genxml/gen6_pack.h: PRIVATE_SCRIPT := $(MESA_PYTHON2) $(LOCAL_PATH)/genxml/gen_pack_header.py
$(intermediates)/genxml/gen6_pack.h: PRIVATE_XML := $(LOCAL_PATH)/genxml/gen6.xml
$(intermediates)/genxml/gen6_pack.h: $(LOCAL_PATH)/genxml/gen6.xml $(LOCAL_PATH)/genxml/gen_pack_header.py
	$(call header-gen)

$(intermediates)/genxml/gen7_pack.h: PRIVATE_SCRIPT := $(MESA_PYTHON2) $(LOCAL_PATH)/genxml/gen_pack_header.py
$(intermediates)/genxml/gen7_pack.h: PRIVATE_XML := $(LOCAL_PATH)/genxml/gen7.xml
$(intermediates)/genxml/gen7_pack.h: $(LOCAL_PATH)/genxml/gen7.xml $(LOCAL_PATH)/genxml/gen_pack_header.py
	$(call header-gen)

$(intermediates)/genxml/gen75_pack.h: PRIVATE_SCRIPT := $(MESA_PYTHON2) $(LOCAL_PATH)/genxml/gen_pack_header.py
$(intermediates)/genxml/gen75_pack.h: PRIVATE_XML := $(LOCAL_PATH)/genxml/gen75.xml
$(intermediates)/genxml/gen75_pack.h: $(LOCAL_PATH)/genxml/gen75.xml $(LOCAL_PATH)/genxml/gen_pack_header.py
	$(call header-gen)

$(intermediates)/genxml/gen8_pack.h: PRIVATE_SCRIPT := $(MESA_PYTHON2) $(LOCAL_PATH)/genxml/gen_pack_header.py
$(intermediates)/genxml/gen8_pack.h: PRIVATE_XML := $(LOCAL_PATH)/genxml/gen8.xml
$(intermediates)/genxml/gen8_pack.h: $(LOCAL_PATH)/genxml/gen8.xml $(LOCAL_PATH)/genxml/gen_pack_header.py
	$(call header-gen)

$(intermediates)/genxml/gen9_pack.h: PRIVATE_SCRIPT := $(MESA_PYTHON2) $(LOCAL_PATH)/genxml/gen_pack_header.py
$(intermediates)/genxml/gen9_pack.h: PRIVATE_XML := $(LOCAL_PATH)/genxml/gen9.xml
$(intermediates)/genxml/gen9_pack.h: $(LOCAL_PATH)/genxml/gen9.xml $(LOCAL_PATH)/genxml/gen_pack_header.py
	$(call header-gen)

$(intermediates)/genxml/gen10_pack.h: PRIVATE_SCRIPT := $(MESA_PYTHON2) $(LOCAL_PATH)/genxml/gen_pack_header.py
$(intermediates)/genxml/gen10_pack.h: PRIVATE_XML := $(LOCAL_PATH)/genxml/gen10.xml
$(intermediates)/genxml/gen10_pack.h: $(LOCAL_PATH)/genxml/gen10.xml $(LOCAL_PATH)/genxml/gen_pack_header.py
	$(call header-gen)

$(intermediates)/genxml/gen11_pack.h: PRIVATE_SCRIPT := $(MESA_PYTHON2) $(LOCAL_PATH)/genxml/gen_pack_header.py
$(intermediates)/genxml/gen11_pack.h: PRIVATE_XML := $(LOCAL_PATH)/genxml/gen11.xml
$(intermediates)/genxml/gen11_pack.h: $(LOCAL_PATH)/genxml/gen11.xml $(LOCAL_PATH)/genxml/gen_pack_header.py
	$(call header-gen)

$(intermediates)/genxml/genX_xml.h: $(addprefix $(MESA_TOP)/src/intel/,$(GENXML_XML_FILES)) $(MESA_TOP)/src/intel/genxml/gen_zipped_file.py
	@mkdir -p $(dir $@)
	@echo "Gen Header: $(PRIVATE_MODULE) <= $(notdir $(@))"
	$(hide) $(MESA_PYTHON2) $(MESA_TOP)/src/intel/genxml/gen_zipped_file.py $(addprefix $(MESA_TOP)/src/intel/,$(GENXML_XML_FILES)) > $@ || (rm -f $@; false)

LOCAL_EXPORT_C_INCLUDE_DIRS := \
	$(MESA_TOP)/src/intel \
	$(intermediates)

include $(MESA_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)
