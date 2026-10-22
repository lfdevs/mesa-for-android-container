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
# BC6H UF16 -> ASTC 4x4 HDR transcoder (csf-v11/202, compute only).
glslangValidator -V --target-env vulkan1.1 --vn panvk_bc_astc_spv \
   -o panvk_bc_astc_spv.h panvk_bc_astc.comp
sed -i 's/^const uint32_t/static const uint32_t/' panvk_bc_*_spv.h
# BC6H block decode into the U-interleaved shadow (csf-v11/210, compute only).
glslangValidator -V --target-env vulkan1.1 -DPANVK_BC_BLOCK \
   --vn panvk_bc_bc6_blk_spv -o panvk_bc_bc6_blk_spv.h panvk_bc_bc6.comp
sed -i 's/^const uint32_t/static const uint32_t/' panvk_bc_bc6_blk_spv.h
# BC lowmem shadow -> shadow block copy (csf-v11/207, compute only).
glslangValidator -V --target-env vulkan1.1 --vn panvk_bc_scopy_spv \
   -o panvk_bc_scopy_spv.h panvk_bc_scopy.comp
sed -i 's/^const uint32_t/static const uint32_t/' panvk_bc_scopy_spv.h
