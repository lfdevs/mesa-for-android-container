#!/usr/bin/env bash

# When changing this file, you need to bump the following
# .gitlab-ci/image-tags.yml tags:
# DEBIAN_TEST_VIDEO_TAG

set -uex

section_start gstreamer "Building gstreamer"

GSTREAMER_VERSION=1.29.2

mkdir /gstreamer-build
pushd /gstreamer-build
git init
git fetch --depth 1 https://gitlab.freedesktop.org/gstreamer/gstreamer.git "$GSTREAMER_VERSION"
git checkout FETCH_HEAD

# FIXME: drop this when a release includes either fix:
# https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/9568
# https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10895
git fetch --depth 2 https://gitlab.freedesktop.org/gstreamer/gstreamer.git \
  62c43771825c10804c490c78edacb0a8362733ea 968cbebb8d6516c955de75b15746bc8c6324a92a
git cherry-pick 62c43771825c10804c490c78edacb0a8362733ea 968cbebb8d6516c955de75b15746bc8c6324a92a

# FIXME: `--libdir` should not be needed, meson is supposed to detect that this
# is debian and set the libdir to the debian format, but something's going wrong
libdir="lib/$(dpkg-architecture -qDEB_TARGET_MULTIARCH)"
meson setup build \
  --buildtype release \
  --prefix /usr \
  --libdir "$libdir" \
  -D gst-plugins-good:ximagesrc=disabled \
  -D gst-plugins-bad:vulkan=enabled \
  -D gst-plugins-bad:vulkan-video=enabled

# FIXME: gstreamer installs tons of other libs and files, some of which
# overwrite existing files and break unrelated functionality (eg. curl), so we
# need to filter and actually install only the gstreamer files.
DESTDIR="$PWD/install" ninja -C build install
for file in \
  install/usr/bin/gst-* \
  install/usr/"$libdir"/libgst* \
  install/usr/"$libdir"/gstreamer-1.0/ \
  install/usr/libexec/gstreamer-1.0/ \
  install/usr/share/gstreamer-1.0/ \
; do
  mv -v "$file" "${file#install}"
done

popd
rm -r /gstreamer-build

section_end gstreamer
