include $(top_srcdir)/src/gallium/Automake.inc

AM_CFLAGS = \
	$(PTHREAD_CFLAGS)

AM_CXXFLAGS = \
	$(GALLIUM_DRIVER_CXXFLAGS) \
	-std=c++11

AM_CPPFLAGS = \
	-I$(top_srcdir)/src/gtest/include \
	-I$(top_srcdir)/src/mapi \
	-I$(top_builddir)/src/mesa \
	-I$(top_srcdir)/src/mesa \
	-I$(top_builddir)/src/compiler/glsl \
	$(DEFINES)

TESTS = st-renumerate-test
check_PROGRAMS = st-renumerate-test

st_renumerate_test_SOURCES =			\
	test_glsl_to_tgsi_lifetime.cpp

st_renumerate_test_LDFLAGS = \
	$(LLVM_LDFLAGS)

st_renumerate_test_LDADD = \
	$(top_builddir)/src/mesa/libmesagallium.la \
	$(top_builddir)/src/mapi/shared-glapi/libglapi.la \
	$(top_builddir)/src/gallium/auxiliary/libgallium.la \
	$(top_builddir)/src/util/libmesautil.la \
	$(top_builddir)/src/gtest/libgtest.la \
	$(GALLIUM_COMMON_LIB_DEPS) \
	$(LLVM_LIBS)
