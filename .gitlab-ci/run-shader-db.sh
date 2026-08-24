#!/usr/bin/env bash
# shellcheck disable=SC1091 # paths only become valid at runtime

set -e

. "${SCRIPTS_DIR}/setup-test-env.sh"

ARTIFACTSDIR=$(pwd)/shader-db
mkdir -p "$ARTIFACTSDIR"
export DRM_SHIM_DEBUG=true

LIBDIR=$(pwd)/install/lib
export LD_LIBRARY_PATH=$LIBDIR

cd /usr/local/shader-db

for driver in freedreno lima vc4; do
    section_start shader-db-${driver} "Running shader-db for $driver"
    env LD_PRELOAD="$LIBDIR/lib${driver}_noop_drm_shim.so" \
        ./run -j"${FDO_CI_CONCURRENT:-4}" ./shaders \
            > "$ARTIFACTSDIR/${driver}-shader-db.txt"
    section_end shader-db-${driver}
done

# Run shader-db over a number of supported versions for v3d
for gpu in 7.1 4.2; do
    section_start "shader-db-v3d-${gpu//.}" "Running shader-db for v3d - ${gpu}"
    env LD_PRELOAD="$LIBDIR/libv3d_noop_drm_shim.so" \
        V3D_GPU_ID="${gpu//.}" \
        ./run -j"${FDO_CI_CONCURRENT:-4}" ./shaders \
            > "$ARTIFACTSDIR/v3d-${gpu//.}-shader-db.txt"
    section_end "shader-db-v3d-${gpu//.}"
done

# Run shader-db over a number of supported GPUs for etnaviv. The shim takes an
# identity of the form model:revision[:product:customer:eco], see
# src/etnaviv/drm-shim/README.md.
for gpu in 2000:5108 \
           3000:5450 \
           7000:6204:70003:11 \
           7000:6214:70002:30; do
    IFS=: read -r model revision _ <<< "$gpu"
    model_revision="gc$model-r$revision"
    section_start shader-db-etnaviv-"$model_revision" "Running shader-db for etnaviv - $model_revision"
    env LD_PRELOAD="$LIBDIR/libetnaviv_noop_drm_shim.so" \
        ETNA_SHIM_GPU="$gpu" \
        ./run -j"${FDO_CI_CONCURRENT:-4}" -o etnaviv ./shaders \
            > "$ARTIFACTSDIR/etnaviv-$model_revision-shader-db.txt"
    section_end shader-db-etnaviv-"$model_revision"
done

# Run shader-db over a number of supported platforms for crocus/iris
for platform in hsw bdw skl mtl lnl ptl; do
    section_start "shader-db-intel-${platform}" "Running shader-db for intel - ${platform}"
    env LD_PRELOAD="$LIBDIR/libintel_noop_drm_shim.so" \
        INTEL_STUB_GPU_PLATFORM="${platform}" \
        ./run -j"${FDO_CI_CONCURRENT:-4}" ./shaders \
            > "$ARTIFACTSDIR/intel-${platform}-shader-db.txt"
    section_end "shader-db-intel-${platform}"
done

# Run shader-db over a number of supported chipsets for nouveau
for chipset in 40 a3 c0 e4 f0 134 162; do
    section_start shader-db-nouveau-${chipset} "Running shader-db for nouveau - ${chipset}"
    env LD_PRELOAD="$LIBDIR/libnouveau_noop_drm_shim.so" \
        NOUVEAU_CHIPSET=${chipset} \
        ./run -j"${FDO_CI_CONCURRENT:-4}" ./shaders \
            > "$ARTIFACTSDIR/nouveau-${chipset}-shader-db.txt"
    section_end shader-db-nouveau-${chipset}
done

# Run shader-db for r300 (RV370 and RV515)
for chipset in 0x5460 0x7140; do
    section_start shader-db-r300-${chipset} "Running shader-db for r300 - ${chipset}"
    env LD_PRELOAD="$LIBDIR/libradeon_noop_drm_shim.so" \
        RADEON_GPU_ID=${chipset} \
        ./run -j"${FDO_CI_CONCURRENT:-4}" -o r300 ./shaders \
            > "$ARTIFACTSDIR/r300-${chipset}-shader-db.txt"
    section_end shader-db-r300-${chipset}
done

# Run shader-db for radeonsi
for device in pitcairn bonaire navi21 navi31 gfx1150 gfx1201; do
    section_start shader-db-radeonsi-${device} "Running shader-db for radeonsi - ${device}"
    env LD_PRELOAD="$LIBDIR/libamdgpu_noop_drm_shim.so" \
        RADEON_GPU_ID=${device} \
        ./run -j"${FDO_CI_CONCURRENT:-4}" -o radeonsi ./shaders \
            > "$ARTIFACTSDIR/radeonsi-${device}-shader-db.txt"
    section_end shader-db-radeonsi-${device}
done
