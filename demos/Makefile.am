## Process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS =

INCLUDES = -I$(top_srcdir)/include $(GLUT_CFLAGS) -I$(top_srcdir)/util

check_PROGRAMS = bounce \
	anisotropic \
	clearspd \
	cubemap \
	drawpix \
	fire \
	gamma \
	gears \
	geartrain \
	glinfo \
	gloss \
	gltestperf \
	glutfx \
	isosurf \
	ipers \
	lodbias \
	morph3d \
	multiarb \
	occlude \
	osdemo \
	paltex \
	pixeltex \
	pointblast \
	ray \
	readpix \
	reflect \
	renormal \
	shadowtex \
	spectex \
	stex3d \
	teapot \
	terrain \
	tessdemo \
	texcyl \
	texdown \
	texenv \
	texobj \
	trispd \
	tunnel \
	tunnel2 \
	winpos

CLEANFILES = test.ppm

LDADD = $(top_builddir)/$(GLU_DIR)/libGLU.la \
	$(GLUT_LIBS) \
	$(top_builddir)/src/libGL.la -lm
#LDFLAGS = -no-install

EXTRA_DIST = isosurf.dat particles.cxx particles.h tunneldat.h

# use double buffering if possible
BUFFER = -db

# execute all programs
exec: $(check_PROGRAMS)
	@for prog in $(check_PROGRAMS);	\
	do				\
	 echo -n "Running $$prog ..." ;	\
	 case "$$prog" in		\
	 osdemo) ./$$prog test.ppm ;; 	\
	 stex3d) ./$$prog $(BUFFER) ;; 	\
	 *) ./$$prog ;;			\
	 esac ;				\
	 echo ;				\
	done


include $(top_srcdir)/common_rules.make
