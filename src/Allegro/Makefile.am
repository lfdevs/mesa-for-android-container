## Process this file with automake to produce Makefile.in

INCLUDES = -I$(top_srcdir)/include -I$(top_srcdir)/src $(ALLEGRO_CFLAGS)

if HAVE_ALLEGRO
# Build a libtool convenience library.
noinst_LTLIBRARIES = libMesaAllegro.la
endif

libMesaAllegro_la_SOURCES = amesa.c direct.h generic.h

libMesaAllegro_la_LIBADD =
include $(top_srcdir)/common_rules.make
