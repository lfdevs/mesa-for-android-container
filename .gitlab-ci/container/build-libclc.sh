#!/usr/bin/env bash

set -uex

section_start libclc "Building libclc"

MESA_LIBCLC_PROJECT_ID=27606
MESA_LIBCLC_VERSION=22.1.8.3

mkdir /libclc
for file in spirv64-mesa3d-.spv spirv-mesa3d-.spv mesa-libclc.pc.in; do
  curl-with-retry "https://gitlab.freedesktop.org/api/v4/projects/${MESA_LIBCLC_PROJECT_ID}/packages/generic/mesa-libclc/${MESA_LIBCLC_VERSION}/${file}" -o /libclc/${file}
done

sed /libclc/mesa-libclc.pc.in -e 's/libexecdir=.*/libexecdir=\/libclc/g' > /usr/share/pkgconfig/mesa-libclc.pc
rm /libclc/mesa-libclc.pc.in

section_end libclc
