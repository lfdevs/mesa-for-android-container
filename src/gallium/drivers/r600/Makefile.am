include Makefile.sources
include $(top_srcdir)/src/gallium/Automake.inc

AM_CFLAGS = \
	$(GALLIUM_DRIVER_CFLAGS) \
	$(RADEON_CFLAGS) \
	-I$(top_srcdir)/src/amd/common

AM_CXXFLAGS = \
	$(GALLIUM_DRIVER_CXXFLAGS) \
	$(RADEON_CFLAGS) \
	-I$(top_srcdir)/src/amd/common

noinst_LTLIBRARIES = libr600.la

libr600_la_SOURCES = \
	$(C_SOURCES) \
	$(CXX_SOURCES)

if NEED_RADEON_LLVM

AM_CFLAGS += \
	$(LLVM_CFLAGS) \
	-I$(top_srcdir)/src/gallium/drivers/radeon/

endif

if HAVE_GALLIUM_COMPUTE
AM_CFLAGS += \
	-DHAVE_OPENCL
endif

EXTRA_DIST = \
	sb/notes.markdown \
	sb/sb_bc_fmt_def.inc
