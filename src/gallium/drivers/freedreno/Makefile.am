include Makefile.sources
include $(top_srcdir)/src/gallium/Automake.inc

AM_CFLAGS = \
	-Wno-packed-bitfield-compat \
	-I$(top_srcdir)/include/drm-uapi \
	-I$(top_srcdir)/src/gallium/drivers/freedreno/ir3 \
	-I$(top_srcdir)/src/freedreno \
	-I$(top_srcdir)/src/freedreno/registers \
	-I$(top_builddir)/src/compiler/nir \
	-I$(top_srcdir)/src/compiler/nir \
	$(LIBDRM_CFLAGS) \
	$(GALLIUM_DRIVER_CFLAGS)

noinst_LTLIBRARIES = libfreedreno.la

libfreedreno_la_SOURCES = \
	$(C_SOURCES) \
	$(a2xx_SOURCES) \
	$(a3xx_SOURCES) \
	$(a4xx_SOURCES) \
	$(a5xx_SOURCES) \
	$(a6xx_SOURCES) \
	$(ir3_SOURCES)

EXTRA_DIST = \
	ir3/ir3_cmdline.c \
	meson.build
