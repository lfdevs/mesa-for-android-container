#!/bin/sh
# © 2011 Cyril Brulebois <kibi@debian.org>
set -e

### First phase, files known to make dpkg-source unhappy.

# List broken symlinks:
symlinks=$(find -type l)

# Symlinks vs. directories:
dirs_vs_symlinks=''

# Modified binaries:
binaries='
src/gallium/state_trackers/d3d1x/progs/bin/d3d10tri.exe
src/gallium/state_trackers/d3d1x/progs/bin/d3d11gears.exe
src/gallium/state_trackers/d3d1x/progs/bin/d3d11spikysphere.exe
src/gallium/state_trackers/d3d1x/progs/bin/d3d11tex.exe
src/gallium/state_trackers/d3d1x/progs/bin/d3d11tri.exe
src/gallium/state_trackers/python/tests/regress/fragment-shader/frag-abs.png
docs/gears.png
'

case $1 in
  "")   clean=0; echo "I: No parameter given, listing only (-f to remove).";;
  "-f") clean=1; echo "I: Removing files.";;
  *)    clean=0; echo "I: Unknown parameter given, listing only (-f to remove).";;
esac

# Readibility:
echo

for x in $symlinks $dirs_vs_symlinks $binaries; do
  # Do not fail if the file went away already, only warn:
  if [ -e $x -o -L $x ]; then
    if [ $clean = 1 ]; then
      git rm $x
    else
      echo "I: Would remove $x"
    fi
  else
   echo "W: Unable to remove non-existing: $x"
  fi
done

### Second phase, kill all files in git not in the tarball
version=$(dpkg-parsechangelog|awk '/Version: / {print $2}'|sed 's/-.*$//')
tarball="../mesa_$version.orig.tar.gz"
if [ ! -f $tarball ]; then
  echo "E: Missing tarball ($tarball), you could use: uscan --download-current --rename"
  exit 1
fi

# Be lazy for now, temporary files would be better:
one=1
two=2

# Strip one directory, Mesa-$version/ is the top-level:
tar tfz $tarball | sed 's,[^/]*/,,' | sort > $two
# List all files known to git, except those under debian/:
git ls-files | grep -v ^debian/ | sort > $one

for x in $(diff -u $one $two|tail -n +3|grep ^-|sed 's/^-//'); do
  if [ -e $x -o -L $x ]; then
    if [ $clean = 1 ]; then
      git rm $x
    else
      echo "I: Would remove $x"
    fi
  else
    echo "W: Unable to remove non-existing: $x (maybe gone during 1st phase)"
  fi
done

rm $one $two
