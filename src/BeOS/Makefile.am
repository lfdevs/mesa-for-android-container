## Process this file with automake to produce Makefile.in

INCLUDES = -I$(top_srcdir)/include -I$(top_srcdir)/src

if HAVE_BEOS
# Build a libtool convenience library.
noinst_LTLIBRARIES = libMesaBeOS.la
endif

libMesaBeOS_la_SOURCES = GLView.cpp

libMesaBeOS_la_LIBADD =
include $(top_srcdir)/common_rules.make
