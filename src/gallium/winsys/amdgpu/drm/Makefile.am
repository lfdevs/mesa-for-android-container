include Makefile.sources
include $(top_srcdir)/src/gallium/Automake.inc

AM_CFLAGS = \
	$(GALLIUM_WINSYS_CFLAGS) \
	$(AMDGPU_CFLAGS) \
	-I$(srcdir)/addrlib \
	-I$(srcdir)/addrlib/core \
	-I$(srcdir)/addrlib/inc/chip/r800 \
	-I$(srcdir)/addrlib/r800/chip \
	-DBRAHMA_BUILD=1

AM_CXXFLAGS = $(AM_CFLAGS)

noinst_LTLIBRARIES = libamdgpuwinsys.la

libamdgpuwinsys_la_SOURCES = $(C_SOURCES)
