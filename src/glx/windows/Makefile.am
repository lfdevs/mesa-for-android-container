noinst_LTLIBRARIES = libwindowsdri.la libwindowsglx.la

# protocol defines for the Windows-DRI server extension
windowsdriincludedir = $(includedir)/X11/extensions
windowsdriinclude_HEADERS = windowsdriconst.h windowsdristr.h

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = windowsdriproto.pc

# library for using the Windows-DRI server extension
libwindowsdri_la_SOURCES = xwindowsdri.c xwindowsdri.h

# native rendering GL for windows
libwindowsglx_la_SOURCES = \
	windowsgl.c \
	windowsgl.h \
	windowsgl_internal.h \
	windows_drawable.c \
	wgl.c

libwindowsglx_la_CFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/src/glx \
	-I$(top_srcdir)/src/mapi \
	-I$(top_srcdir)/src/mapi/glapi \
	-I$(top_builddir)/src/mapi/glapi \
	$(VISIBILITY_CFLAGS) \
	$(SHARED_GLAPI_CFLAGS) \
	$(DEFINES) \
	$(X11_INCLUDES)
