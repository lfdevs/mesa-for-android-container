#!/bin/sh
# Copyright 2025 Collabora Ltd.
# SPDX-License-Identifier: MIT

# Usage: debian/merge-vulkan-driver-manifests.sh debian/tmp
# DEB_HOST_MULTIARCH must be set in the environment.
#
# If the JSON manifest describing a Vulkan driver contains for example
# "library_path": "/usr/lib/x86_64-linux-gnu/libvulkan_lvp.so"
# then replace it with
# "library_path": "libvulkan_lvp.so"
# to get the same content for each architecture, and rename from for example
# "lvp_icd.x86_64.json"
# to
# "lvp_icd.json"
# so that the same JSON manifest will be shared between all multiarch
# architectures.
#
# This avoids multiarch collisions on pairs of architectures where the
# Meson CPU name is the same but the library path is different, notably
# armel/armhf. https://bugs.debian.org/980148

set -eu

DESTDIR="$1"

for file in "$DESTDIR"/usr/share/vulkan/icd.d/*.*.json; do
	if grep -q 'library_path.*/usr/lib/'"${DEB_HOST_MULTIARCH}"'/lib[^/"]*\.so' "${file}"; then
		replacement="${file%.*.json}.json"
		sed -E -e '/library_path/ s,/usr/lib/'"${DEB_HOST_MULTIARCH}"'/(lib[^/"]*\.so),\1,' \
			< "${file}" > "${replacement}"
		diff -s -u "${file}" "${replacement}" || true
		rm "${file}"
	else
		echo "multiarch library path not found in $file, leaving it as-is"
	fi
done
