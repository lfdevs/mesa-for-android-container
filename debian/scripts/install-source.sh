#!/bin/sh

if [ -n "$1" ]; then
	TOP="$1"
else
	TOP=`pwd`
fi

SOURCE_DIRS='
	include/GL/internal
	src/glx/x11
	src/mesa/drivers/common
	src/mesa/drivers/dri/common
	src/mesa/drivers/dri/glcore
	src/mesa/drivers/x11
	src/mesa/glapi
	src/mesa/main
	src/mesa/math
	src/mesa/ppc
	src/mesa/shader
	src/mesa/sparc
	src/mesa/swrast_setup
	src/mesa/swrast
	src/mesa/tnl_dd
	src/mesa/tnl
	src/mesa/vbo
	src/mesa/x86-64
	src/mesa/x86
'

FILTER="-not -path '*/.svn*'"
TARGET=${TOP}/debian/tmp/usr/share/mesa-source

(
	find $SOURCE_DIRS $FILTER -name '*.[ch]';
	find include/GL $FILTER -name 'xmesa*.h';
) | \
	while read x; do
		DIRNAME=`dirname "$x"`
		mkdir -p "$TARGET/$DIRNAME"
		cp -lf "$x" "$TARGET/$DIRNAME"
	done

# fix permissions
find "$TARGET" -type f | xargs chmod 0644

