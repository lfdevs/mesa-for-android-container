# Copyright © 2016 Intel Corporation
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

# The gallium includes are for the util/u_math.h include from main/macros.h
AM_CPPFLAGS = \
	$(INTEL_CFLAGS) \
	$(VALGRIND_CFLAGS) \
	$(DEFINES) \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/src \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/src/mapi \
	-I$(top_srcdir)/src/mesa \
	-I$(top_srcdir)/src/mesa/drivers/dri/common \
	-I$(top_srcdir)/src/mesa/drivers/dri/i965 \
	-I$(top_srcdir)/src/gallium/auxiliary \
	-I$(top_srcdir)/src/gallium/include \
	-I$(top_builddir)/src/intel \
	-I$(top_srcdir)/src/intel

aubinator_DEPS = \
	$(top_builddir)/src/intel/common/libintel_common.la \
	$(top_builddir)/src/mesa/drivers/dri/i965/libi965_compiler.la \
	$(top_builddir)/src/util/libmesautil.la \
	$(PER_GEN_LIBS) \
	$(PTHREAD_LIBS) \
	$(DLOPEN_LIBS) \
	-lm

noinst_PROGRAMS = aubinator

aubinator_SOURCES = \
	aubinator.c \
	decoder.c \
	decoder.h \
	disasm.c \
	gen_disasm.h

aubinator_LDADD = \
	$(aubinator_DEPS) \
	$(EXPAT_LIBS)

aubinator_CFLAGS = \
	$(AM_CFLAGS) \
	$(EXPAT_CFLAGS) \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/src
