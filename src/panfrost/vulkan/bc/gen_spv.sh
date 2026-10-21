#!/bin/sh
# Regenerate the BC decoder SPIR-V headers (compute + fragment variants).
set -eu
cd "$(dirname "$0")"
for s in s3tc rgtc bc6 bc7; do
   glslangValidator -V --target-env vulkan1.1 --vn panvk_bc_${s}_spv \
      -o panvk_bc_${s}_spv.h panvk_bc_${s}.comp
   glslangValidator -V --target-env vulkan1.1 -S frag -DPANVK_BC_FS \
      --vn panvk_bc_${s}_fs_spv -o panvk_bc_${s}_fs_spv.h panvk_bc_${s}.comp
done
# BC1-BC5 -> ETC2/EAC transcoder of the lowmem path (compute only).
glslangValidator -V --target-env vulkan1.1 --vn panvk_bc_etc2_spv \
   -o panvk_bc_etc2_spv.h panvk_bc_etc2.comp
sed -i 's/^const uint32_t/static const uint32_t/' panvk_bc_*_spv.h
