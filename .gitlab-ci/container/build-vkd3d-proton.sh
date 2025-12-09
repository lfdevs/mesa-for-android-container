#!/bin/bash

# When changing this file, you need to bump the following
# .gitlab-ci/image-tags.yml tags:
# DEBIAN_TEST_VK_TAG
set -ex

section_start vkd3d-proton "Building vkd3d-proton"

# Do a very early check to make sure the tag is correct without the need of
# setting up the environment variables locally
ci_tag_build_time_check "VKD3D_PROTON_TAG"

VKD3D_PROTON_COMMIT="33a41f9d14460f998c5ce8a4aab42ca1cce8dcc6"

VKD3D_PROTON_DST_DIR="/vkd3d-proton-tests"
VKD3D_PROTON_SRC_DIR="/vkd3d-proton-src"
VKD3D_PROTON_BUILD_DIR="/vkd3d-proton-build"
VKD3D_PROTON_WINE_DIR="/vkd3d-proton-wine64"
VKD3D_PROTON_S3_ARTIFACT="vkd3d-proton.tar.zst"

if [ ! -d "$VKD3D_PROTON_WINE_DIR" ]; then
  echo "Fatal: Directory '$VKD3D_PROTON_WINE_DIR' does not exist. Aborting."
  exit 1
fi

git clone https://github.com/HansKristian-Work/vkd3d-proton.git --single-branch -b master --no-checkout "$VKD3D_PROTON_SRC_DIR"
pushd "$VKD3D_PROTON_SRC_DIR"
git checkout "$VKD3D_PROTON_COMMIT"
git submodule update --init --recursive
git submodule update --recursive

meson setup                                           \
      -D c_args=-Wno-error=incompatible-pointer-types \
      -D enable_tests=true                            \
      --buildtype release                             \
      --prefix "$VKD3D_PROTON_DST_DIR"                \
      --strip                                         \
      --libdir "lib"                                  \
      "$VKD3D_PROTON_BUILD_DIR/build"

ninja -C "$VKD3D_PROTON_BUILD_DIR/build" install

install -m755 -t "${VKD3D_PROTON_DST_DIR}/" "$VKD3D_PROTON_BUILD_DIR/build/tests/d3d12"

mkdir "$VKD3D_PROTON_DST_DIR/tests"
cp \
  "tests/test-runner.sh" \
  "tests/d3d12_tests.h" \
  "$VKD3D_PROTON_DST_DIR/tests/"
popd

# Archive and upload vkd3d-proton for use as a LAVA overlay, if the archive doesn't exist yet
ARTIFACT_PATH="${DATA_STORAGE_PATH}/vkd3d-proton/${VKD3D_PROTON_TAG}/${CI_JOB_NAME}/${VKD3D_PROTON_S3_ARTIFACT}"
if FOUND_ARTIFACT_URL="$(find_s3_project_artifact "${ARTIFACT_PATH}")"; then
  echo "Found vkd3d-proton at: ${FOUND_ARTIFACT_URL}, skipping upload"
else
  echo "Uploaded vkd3d-proton not found, reuploading..."
  tar --zstd -cf "$VKD3D_PROTON_S3_ARTIFACT" -C / "${VKD3D_PROTON_DST_DIR#/}" "${VKD3D_PROTON_WINE_DIR#/}"
  ci-fairy s3cp --token-file "${S3_JWT_FILE}" "$VKD3D_PROTON_S3_ARTIFACT" \
    "https://${S3_BASE_PATH}/${CI_PROJECT_PATH}/${ARTIFACT_PATH}"
  rm "$VKD3D_PROTON_S3_ARTIFACT"
fi

rm -rf "$VKD3D_PROTON_BUILD_DIR"
rm -rf "$VKD3D_PROTON_SRC_DIR"

section_end vkd3d-proton
