#!/usr/bin/env bash
# shellcheck disable=SC2086 # we want word splitting

set -uex

section_start opencl-cts "Building OpenCL-CTS"

# Changing this file changes CONDITIONAL_BUILD_OPENCL_CTS_TAG. Bump the GL image
# tag too so CI rebuilds the image and uploads the matching OpenCL-CTS artifact:
# DEBIAN_TEST_GL_TAG

ci_tag_build_time_check "OPENCL_CTS_TAG"

REV="43111c0c7c7ffcdbacc5775a6baff8016bada9e2"
MESA_SOURCE_DIR="$PWD"
OPENCL_CTS_S3_ARTIFACT="opencl-cts.tar.zst"
ARTIFACT_PATH="${DATA_STORAGE_PATH}/opencl-cts/${DEBIAN_ARCH}/${OPENCL_CTS_TAG}.tar.zst"

if FOUND_ARTIFACT_URL="$(find_s3_project_artifact "${ARTIFACT_PATH}")"; then
  echo "Found OpenCL-CTS at: ${FOUND_ARTIFACT_URL}"
  curl-with-retry "${FOUND_ARTIFACT_URL}" | tar --zstd -x -C /
else
  mkdir /OpenCL-CTS
  pushd /OpenCL-CTS
  git init
  git remote add origin https://github.com/KhronosGroup/OpenCL-CTS.git
  git fetch --depth 1 origin "$REV"
  git checkout FETCH_HEAD
  mkdir -p /opencl-cts/test_conformance
  echo "$REV" > /opencl-cts/opencl-cts-version

  mkdir build
  pushd build

  cmake -S .. -B . -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCL_INCLUDE_DIR="$MESA_SOURCE_DIR/include" \
    -DCL_LIB_DIR="/usr/lib/$(dpkg-architecture -qDEB_HOST_MULTIARCH)" \
    -DGL_IS_SUPPORTED=ON \
    -DGLES_IS_SUPPORTED=ON \
    -DVULKAN_IS_SUPPORTED=ON \
    -DOPENCL_LIBRARIES=OpenCL \
    -DSPIRV_INCLUDE_DIR=/usr \
    -DSPIRV_TOOLS_DIR=/usr/bin

  ninja -j ${FDO_CI_CONCURRENT:-4}

  cp -R test_conformance /opencl-cts/
  find /opencl-cts/test_conformance -depth \
    \( \
      -name CMakeFiles \
      -o -name cmake_install.cmake \
      -o -name CMakeCache.txt \
      -o -name build.ninja \
      -o -name '*.ninja' \
    \) \
    -exec rm -rf {} +

  popd
  popd
  rm -rf /OpenCL-CTS

  tar --zstd -cf "$OPENCL_CTS_S3_ARTIFACT" -C / opencl-cts
  ci-fairy s3cp --token-file "${S3_JWT_FILE}" "$OPENCL_CTS_S3_ARTIFACT" \
    "https://${S3_BASE_PATH}/${CI_PROJECT_PATH}/${ARTIFACT_PATH}"
  rm "$OPENCL_CTS_S3_ARTIFACT"
fi

section_end opencl-cts
