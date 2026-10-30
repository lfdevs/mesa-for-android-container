#!/bin/sh
# Regenerate the BC decoder SPIR-V headers (compute + fragment variants).
set -eu
cd "$(dirname "$0")"
for s in s3tc rgtc bc6 bc7; do
   # 117: the BC7 compute (block) decoder uses the fast decoders.
   fast=""; [ $s = bc7 ] && fast=-DPANVK_BC_FAST
   glslangValidator -V --target-env vulkan1.1 $fast --vn panvk_bc_${s}_spv \
      -o panvk_bc_${s}_spv.h panvk_bc_${s}.comp
   # 0170: the per-texel compute decoder of 116, used until the block one
   # is compiled.
   glslangValidator -V --target-env vulkan1.1 -DPANVK_BC_TEXEL \
      --vn panvk_bc_${s}_texel_spv -o panvk_bc_${s}_texel_spv.h panvk_bc_${s}.comp
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
# BC7 fast per-texel fragment decode (csf-v11/211, PANVK_BC_BC7_DECODE=fast).
glslangValidator -V --target-env vulkan1.1 -S frag -DPANVK_BC_FS -DPANVK_BC_FAST \
   --vn panvk_bc_bc7_fs_fast_spv -o panvk_bc_bc7_fs_fast_spv.h panvk_bc_bc7.comp
sed -i 's/^const uint32_t/static const uint32_t/' panvk_bc_bc7_fs_fast_spv.h
# BC lowmem shadow -> shadow block copy (csf-v11/207, compute only).
glslangValidator -V --target-env vulkan1.1 --vn panvk_bc_scopy_spv \
   -o panvk_bc_scopy_spv.h panvk_bc_scopy.comp
sed -i 's/^const uint32_t/static const uint32_t/' panvk_bc_scopy_spv.h
# 0172: per-mode ETC2 transcoders.
sh ./gen_etc2_fixed.sh
