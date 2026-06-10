#!/usr/bin/env bash
# The relative paths in this file only become valid at runtime.
# shellcheck disable=SC1091
#
# When changing this file, you need to bump the following
# .gitlab-ci/image-tags.yml tags:
# DEBIAN_TEST_ANDROID_TAG

set -e

. .gitlab-ci/setup-test-env.sh

set -o xtrace

section_start debian_setup "Base Debian system setup"

export DEBIAN_FRONTEND=noninteractive

# Google provides Android NDK for x86_64 hosts only; there is no arm64-native NDK for Linux.
# Thus, we must cross-compile arm64 test tools on x86_64 runners.

# Container Variants Overview:
# - x86_64_test-android: Comprehensive environment for x86_64 testing.
# - arm64_test-android-tools: Used to cross-compile arm64 binaries (dEQP, ANGLE, etc)
#   on x86_64 runners because the Android NDK only provides x86_64 host binaries.
# - arm64_test-android: Fetches components built by the 'tools' container to run
#   tests on arm64 devices where full builds are unsupported.
#
# | Component            | x86_64_test-android | arm64_test-android-tools | arm64_test-android |
# | :------------------: | :-----------------: | :----------------------: | :----------------: |
# | Runner arch          | x86_64              | x86_64                   | arm64              |
# | Android NDK          | ✔                   | ✔                        |                    |
# | ANGLE / dEQP         | ✔ (Built)           | ✔ (Built)                | ✔ (Downloaded)     |
# | eglinfo / vulkaninfo | ✔                   |                          | ✔                  |
# | Cuttlefish / CTS     | ✔ (Included)        |                          |   (LAVA overlay)   |
# | Runtime Deps (AAPT)  | ✔                   |                          | ✔                  |
#
# | Logic Variable       | x86_64_test-android | arm64_test-android-tools | arm64_test-android |
# | :------------------: | :-----------------: | :----------------------: | :----------------: |
# | BUILD_CONTAINER      | ✔                   | ✔                        |                    |
# | TEST_CONTAINER       | ✔                   |                          | ✔                  |

if "${BUILD_CONTAINER}"; then
  # Ephemeral packages (installed for this script and removed again at the end)
  EPHEMERAL=(
      binutils-aarch64-linux-gnu
      build-essential:native
      ccache
      cmake
      config-package-dev
      debhelper-compat
      dpkg-dev
      ninja-build
      unzip
  )
else
  # We only need the build tools in build containers
  EPHEMERAL=(
      unzip
  )
fi

if "${TEST_CONTAINER}"; then
  # We only need the Cuttlefish runtime dependencies in test containers
  DEPS=(
      aapt
      cuttlefish-base
      cuttlefish-user
      iproute2
  )
  # Android CTS only comes with x86_64 Java bundled, so we need to install it on arm64
  if [ "${DEBIAN_ARCH}" = "arm64" ]; then
    DEPS+=(openjdk-21-jdk-headless)
  fi
else
  DEPS=()
fi

apt-get install -y --no-remove --no-install-recommends \
      "${DEPS[@]}" "${EPHEMERAL[@]}"

############### Building ...

. .gitlab-ci/container/container_pre_build.sh

section_end debian_setup

if [ "${ANDROID_ARCH}" = "x86_64" ]; then
  export RUST_TARGET=x86_64-linux-android
  export ANDROID_ABI=x86_64
  export ANGLE_ARCH=x64
fi

if [ "${ANDROID_ARCH}" = "arm64" ]; then
  export RUST_TARGET=aarch64-linux-android
  export ANDROID_ABI=arm64-v8a
  export ANGLE_ARCH=arm64
  export STRIP_CMD=aarch64-linux-gnu-strip
fi

############### Downloading Android tools

section_start android-tools "Downloading Android tools"

# Download pre-built Android utility binaries (eglinfo, vulkaninfo) for test environments
if "${TEST_CONTAINER}"; then
  mkdir /android-tools
  pushd /android-tools

  curl -L --retry 4 -f --retry-all-errors --retry-delay 60 \
    -o eglinfo "https://${S3_HOST}/${S3_ANDROID_BUCKET}/mesa/mesa/${DATA_STORAGE_PATH}/eglinfo-android-${ANDROID_ARCH}"
  chmod +x eglinfo

  curl -L --retry 4 -f --retry-all-errors --retry-delay 60 \
    -o vulkaninfo "https://${S3_HOST}/${S3_ANDROID_BUCKET}/mesa/mesa/${DATA_STORAGE_PATH}/vulkaninfo-android-${ANDROID_ARCH}"
  chmod +x vulkaninfo

  popd
fi

# Fetch cross-compiled test tools (ANGLE, dEQP) for the arm64 runtime.
# These were built in the 'tools' container due to NDK host limitations.
if ! "${BUILD_CONTAINER}"; then
  curl-with-retry -O "https://${S3_BASE_PATH}/${CI_PROJECT_PATH}/${DEBIAN_TEST_ANDROID_TAG}/android-tools-arm64.tar.zst"
  tar --zstd -xf android-tools-arm64.tar.zst -C /
  rm android-tools-arm64.tar.zst
fi

section_end android-tools

############### Downloading NDK for native builds for the guest ...

# Skip NDK setup and builds for test-only runtimes
if "${BUILD_CONTAINER}"; then
  section_start android-ndk "Downloading Android NDK"

  # Fetch the NDK and extract just the toolchain we want.
  ndk="android-ndk-${ANDROID_NDK_VERSION}"
  curl -L --retry 4 -f --retry-all-errors --retry-delay 60 \
    -o "$ndk.zip" "https://dl.google.com/android/repository/$ndk-linux.zip"
  unzip -q -d / "$ndk.zip"
    rm "$ndk.zip"

  section_end android-ndk

  ############### Build ANGLE

  ANGLE_TARGET=android \
  . .gitlab-ci/container/build-angle.sh

  ############### Build dEQP runner

  export ANDROID_NDK_HOME=/$ndk
  . .gitlab-ci/container/build-rust.sh test
  . .gitlab-ci/container/build-deqp-runner.sh

  # Properly uninstall rustup including cargo and init scripts on shells
  rustup self uninstall -y

  ############### Build dEQP

  DEQP_API=tools \
  DEQP_TARGET="android" \
  EXTRA_CMAKE_ARGS="-DDEQP_ANDROID_EXE=ON -DDEQP_TARGET_TOOLCHAIN=ndk-modern -DANDROID_NDK_PATH=/$ndk -DANDROID_ABI=${ANDROID_ABI} -DDE_ANDROID_API=$ANDROID_SDK_VERSION" \
  . .gitlab-ci/container/build-deqp.sh

  DEQP_API=GLES \
  DEQP_TARGET="android" \
  EXTRA_CMAKE_ARGS="-DDEQP_ANDROID_EXE=ON -DDEQP_ANDROID_EXE_LOGCAT=ON -DDEQP_TARGET_TOOLCHAIN=ndk-modern -DANDROID_NDK_PATH=/$ndk -DANDROID_ABI=${ANDROID_ABI} -DDE_ANDROID_API=$ANDROID_SDK_VERSION" \
  . .gitlab-ci/container/build-deqp.sh

  DEQP_API=VK \
  DEQP_TARGET="android" \
  EXTRA_CMAKE_ARGS="-DDEQP_ANDROID_EXE=ON -DDEQP_ANDROID_EXE_LOGCAT=ON -DDEQP_TARGET_TOOLCHAIN=ndk-modern -DANDROID_NDK_PATH=/$ndk -DANDROID_ABI=${ANDROID_ABI} -DDE_ANDROID_API=$ANDROID_SDK_VERSION" \
  . .gitlab-ci/container/build-deqp.sh

  rm -rf /VK-GL-CTS
fi

############### Downloading Cuttlefish resources ...

# We only need to download the Cuttlefish image and host tools for the shared runners on x86_64, otherwise they are deployed as LAVA overlays
if [ "${ANDROID_ARCH}" = "x86_64" ]; then
  section_start cuttlefish "Downloading and setting up Cuttlefish"

  mkdir /cuttlefish
  pushd /cuttlefish

  curl -L --retry 4 -f --retry-all-errors --retry-delay 60 \
    -O "https://${S3_HOST}/${S3_ANDROID_BUCKET}/${CUTTLEFISH_PROJECT_PATH}/aosp-${CUTTLEFISH_BUILD_VERSION_TAGS}.${CUTTLEFISH_BUILD_NUMBER}/aosp_cf_${ANDROID_ARCH}_only_phone-img-${CUTTLEFISH_BUILD_NUMBER}.tar.zst"

  tar --zstd -xvf "aosp_cf_${ANDROID_ARCH}_only_phone-img-${CUTTLEFISH_BUILD_NUMBER}.tar.zst"
  rm "aosp_cf_${ANDROID_ARCH}_only_phone-img-${CUTTLEFISH_BUILD_NUMBER}.tar.zst"

  curl -L --retry 4 -f --retry-all-errors --retry-delay 60 \
    -O "https://${S3_HOST}/${S3_ANDROID_BUCKET}/${CUTTLEFISH_PROJECT_PATH}/aosp-${CUTTLEFISH_BUILD_VERSION_TAGS}.${CUTTLEFISH_BUILD_NUMBER}/cvd-host_package-${ANDROID_ARCH}.tar.zst"
  tar --zst -xvf "cvd-host_package-${ANDROID_ARCH}.tar.zst"
  rm "cvd-host_package-${ANDROID_ARCH}.tar.zst"

  popd

  section_end cuttlefish
fi

if "${TEST_CONTAINER}"; then
  addgroup --system kvm
  usermod -a -G kvm,cvdnetwork root
fi

############### Downloading Android CTS

if "${TEST_CONTAINER}"; then
. .gitlab-ci/container/build-android-cts.sh
fi

############### Packaging arm64 tools for S3 upload

# Upload cross-compiled arm64 binaries to S3 for consumption by the test runtime container
if ! "${TEST_CONTAINER}"; then
  section_start android-tools-arm64 "Uploading Android tools"

  TOOL_DIRS=(
      /angle
      /deqp-gles
      /deqp-runner
      /deqp-tools
      /deqp-vk
      /mesa-ci-build-tag
  )

  tar --zstd -cf android-tools-arm64.tar.zst "${TOOL_DIRS[@]}"

  ci-fairy s3cp --token-file "${S3_JWT_FILE}" "android-tools-arm64.tar.zst" \
    "https://${S3_BASE_PATH}/${CI_PROJECT_PATH}/${DEBIAN_TEST_ANDROID_TAG}/android-tools-arm64.tar.zst"

  section_end android-tools-arm64
fi

############### Uninstall the build software

section_switch debian_cleanup "Cleaning up base Debian system"

if "${BUILD_CONTAINER}"; then
  rm -rf "/${ndk:?}"
fi

# We currently only have LAVA jobs on arm64, where we deploy Android CTS as a LAVA overlay.
# Remove it from the container image to save some space.
if "${TEST_CONTAINER}" &&  [ "${DEBIAN_ARCH}" = "arm64" ]; then
  rm -rf "/android-cts"
fi

apt-get purge -y "${EPHEMERAL[@]}"

. .gitlab-ci/container/container_post_build.sh

section_end debian_cleanup

############### Remove unused packages

. .gitlab-ci/container/strip-rootfs.sh
