

# Copyright © 2011 Intel Corporation
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.

include Makefile.sources

AM_CFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/src/ \
	-I$(top_srcdir)/src/mapi \
	-I$(top_srcdir)/src/mesa/ \
	-I$(top_srcdir)/src/gallium/include \
	-I$(top_srcdir)/src/gallium/auxiliary \
	-I$(top_builddir)/src/mesa/drivers/dri/common \
	-I$(top_srcdir)/src/mesa/drivers/dri/common \
	-I$(top_srcdir)/src/gtest/include \
	-I$(top_builddir)/src/compiler/glsl \
	-I$(top_builddir)/src/compiler/nir \
	-I$(top_srcdir)/src/compiler/nir \
	-I$(top_builddir)/src/intel \
	-I$(top_srcdir)/src/intel \
	-I$(top_srcdir)/include/drm-uapi \
	$(DEFINES) \
	$(VISIBILITY_CFLAGS) \
	$(LIBDRM_CFLAGS) \
	$(VALGRIND_CFLAGS) \
	-msse2

AM_CXXFLAGS = $(AM_CFLAGS)

I965_PERGEN_LIBS = \
	libi965_gen4.la \
	libi965_gen45.la \
	libi965_gen5.la \
	libi965_gen6.la \
	libi965_gen7.la \
	libi965_gen75.la \
	libi965_gen8.la \
	libi965_gen9.la \
	libi965_gen10.la

libi965_gen4_la_SOURCES = $(i965_gen4_FILES)
libi965_gen4_la_CFLAGS = $(AM_CFLAGS) -DGEN_VERSIONx10=40

libi965_gen45_la_SOURCES = $(i965_gen45_FILES)
libi965_gen45_la_CFLAGS = $(AM_CFLAGS) -DGEN_VERSIONx10=45

libi965_gen5_la_SOURCES = $(i965_gen5_FILES)
libi965_gen5_la_CFLAGS = $(AM_CFLAGS) -DGEN_VERSIONx10=50

libi965_gen6_la_SOURCES = $(i965_gen6_FILES)
libi965_gen6_la_CFLAGS = $(AM_CFLAGS) -DGEN_VERSIONx10=60

libi965_gen7_la_SOURCES = $(i965_gen7_FILES)
libi965_gen7_la_CFLAGS = $(AM_CFLAGS) -DGEN_VERSIONx10=70

libi965_gen75_la_SOURCES = $(i965_gen75_FILES)
libi965_gen75_la_CFLAGS = $(AM_CFLAGS) -DGEN_VERSIONx10=75

libi965_gen8_la_SOURCES = $(i965_gen8_FILES)
libi965_gen8_la_CFLAGS = $(AM_CFLAGS) -DGEN_VERSIONx10=80

libi965_gen9_la_SOURCES = $(i965_gen9_FILES)
libi965_gen9_la_CFLAGS = $(AM_CFLAGS) -DGEN_VERSIONx10=90

libi965_gen10_la_SOURCES = $(i965_gen10_FILES)
libi965_gen10_la_CFLAGS = $(AM_CFLAGS) -DGEN_VERSIONx10=100

noinst_LTLIBRARIES = \
	libi965_dri.la \
	$(I965_PERGEN_LIBS)

libi965_dri_la_SOURCES = \
	$(i965_FILES) \
	$(i965_oa_GENERATED_FILES)
libi965_dri_la_LIBADD = \
	$(top_builddir)/src/intel/common/libintel_common.la \
	$(top_builddir)/src/intel/isl/libisl.la \
	$(top_builddir)/src/intel/compiler/libintel_compiler.la \
	$(top_builddir)/src/intel/blorp/libblorp.la \
	$(I965_PERGEN_LIBS) \
	$(LIBDRM_LIBS)

BUILT_SOURCES = $(i965_oa_GENERATED_FILES)
CLEANFILES = $(BUILT_SOURCES)

EXTRA_DIST = \
	brw_oa_hsw.xml \
	brw_oa_bdw.xml \
	brw_oa_chv.xml \
	brw_oa_sklgt2.xml \
	brw_oa_sklgt3.xml \
	brw_oa_sklgt4.xml \
	brw_oa_bxt.xml \
	brw_oa_kblgt2.xml \
	brw_oa_kblgt3.xml \
	brw_oa_glk.xml \
	brw_oa.py

# Note: we avoid using a multi target rule here and outputting both the
# .c and .h files in one go so we don't hit problems with parallel
# make and multiple invocations of the same script trying to write
# to the same files.
brw_oa_%.h: brw_oa.py brw_oa_%.xml Makefile.am
	$(PYTHON2) $(PYTHON_FLAGS) $(srcdir)/brw_oa.py --header=$(builddir)/brw_oa_$(*).h --chipset=$(*) $(srcdir)/brw_oa_$(*).xml
brw_oa_%.c: brw_oa.py brw_oa_%.xml Makefile.am
	$(PYTHON2) $(PYTHON_FLAGS) $(srcdir)/brw_oa.py --code=$(builddir)/brw_oa_$(*).c --chipset=$(*) $(srcdir)/brw_oa_$(*).xml
