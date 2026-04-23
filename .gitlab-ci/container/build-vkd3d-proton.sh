#!/bin/bash

# When changing this file, you need to bump the following
# .gitlab-ci/image-tags.yml tags:
# DEBIAN_TEST_VK_TAG
set -ex

section_start vkd3d-proton "Building vkd3d-proton"

# Do a very early check to make sure the tag is correct without the need of
# setting up the environment variables locally
ci_tag_build_time_check "VKD3D_PROTON_TAG"

VKD3D_PROTON_COMMIT="a83811bf228ec4e7fc09727388521c725126d868"

VKD3D_PROTON_DST_DIR="/vkd3d-proton-tests"
VKD3D_PROTON_SRC_DIR="/vkd3d-proton-src"
VKD3D_PROTON_S3_ARTIFACT="vkd3d-proton.tar.zst"

mkdir -p "$VKD3D_PROTON_SRC_DIR"
pushd "$VKD3D_PROTON_SRC_DIR"
git init
git remote add origin https://github.com/HansKristian-Work/vkd3d-proton.git
git fetch --depth 1 origin "$VKD3D_PROTON_COMMIT"
git checkout FETCH_HEAD
git submodule update --init --recursive

meson setup build --buildtype release -D enable_tests=true \
      -D c_args=-Wno-error=incompatible-pointer-types
# Build and install specifically the bits we want; gets rid of 24MB of
# unwanted files, keeping only 7.6MB of the three useful files.
for file in \
  libs/d3d12/libvkd3d-proton-d3d12.so \
  libs/d3d12core/libvkd3d-proton-d3d12core.so \
  tests/d3d12 \
; do
  ninja -C build "$file"
  strip build/"$file"
  install -Dm755 -t "${VKD3D_PROTON_DST_DIR}/$(dirname "$file")" build/"$file"
done

install -m755 -t "${VKD3D_PROTON_DST_DIR}/tests" tests/test-runner.sh
popd

# Archive and upload vkd3d-proton for use as a LAVA overlay, if the archive doesn't exist yet
ARTIFACT_PATH="${DATA_STORAGE_PATH}/vkd3d-proton/${VKD3D_PROTON_TAG}/${CI_JOB_NAME}/${VKD3D_PROTON_S3_ARTIFACT}"
if FOUND_ARTIFACT_URL="$(find_s3_project_artifact "${ARTIFACT_PATH}")"; then
  echo "Found vkd3d-proton at: ${FOUND_ARTIFACT_URL}, skipping upload"
else
  echo "Uploaded vkd3d-proton not found, reuploading..."
  tar --zstd -cf "$VKD3D_PROTON_S3_ARTIFACT" -C / "${VKD3D_PROTON_DST_DIR#/}"
  ci-fairy s3cp --token-file "${S3_JWT_FILE}" "$VKD3D_PROTON_S3_ARTIFACT" \
    "https://${S3_BASE_PATH}/${CI_PROJECT_PATH}/${ARTIFACT_PATH}"
  rm "$VKD3D_PROTON_S3_ARTIFACT"
fi

rm -rf "$VKD3D_PROTON_SRC_DIR"

section_end vkd3d-proton
