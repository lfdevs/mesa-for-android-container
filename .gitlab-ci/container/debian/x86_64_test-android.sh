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

# Ephemeral packages (installed for this script and removed again at the end)
EPHEMERAL=(
    build-essential:native
    ccache
    cmake
    config-package-dev
    debhelper-compat
    dpkg-dev
    ninja-build
    sudo
    unzip
)

DEPS=(
    aapt
    cuttlefish-base
    cuttlefish-user
    iproute2
)
apt-get install -y --no-remove --no-install-recommends \
      "${DEPS[@]}" "${EPHEMERAL[@]}"

############### Building ...

. .gitlab-ci/container/container_pre_build.sh

section_end debian_setup

############### Downloading Android tools

section_start android-tools "Downloading Android tools"

mkdir /android-tools
pushd /android-tools

curl -L --retry 4 -f --retry-all-errors --retry-delay 60 \
  -o eglinfo "https://${S3_HOST}/${S3_ANDROID_BUCKET}/mesa/mesa/${DATA_STORAGE_PATH}/eglinfo-android-x86_64"
chmod +x eglinfo

curl -L --retry 4 -f --retry-all-errors --retry-delay 60 \
  -o vulkaninfo "https://${S3_HOST}/${S3_ANDROID_BUCKET}/mesa/mesa/${DATA_STORAGE_PATH}/vulkaninfo-android-x86_64"
chmod +x vulkaninfo

popd

section_end android-tools

############### Downloading NDK for native builds for the guest ...

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
DEBIAN_ARCH=amd64 \
. .gitlab-ci/container/build-angle.sh

############### Build dEQP runner

export ANDROID_NDK_HOME=/$ndk
export RUST_TARGET=x86_64-linux-android
. .gitlab-ci/container/build-rust.sh test
. .gitlab-ci/container/build-deqp-runner.sh

# Properly uninstall rustup including cargo and init scripts on shells
rustup self uninstall -y

############### Build dEQP

DEQP_API=tools \
DEQP_TARGET="android" \
EXTRA_CMAKE_ARGS="-DDEQP_ANDROID_EXE=ON -DDEQP_TARGET_TOOLCHAIN=ndk-modern -DANDROID_NDK_PATH=/$ndk -DANDROID_ABI=x86_64 -DDE_ANDROID_API=$ANDROID_SDK_VERSION" \
. .gitlab-ci/container/build-deqp.sh

DEQP_API=GLES \
DEQP_TARGET="android" \
EXTRA_CMAKE_ARGS="-DDEQP_ANDROID_EXE=ON -DDEQP_TARGET_TOOLCHAIN=ndk-modern -DANDROID_NDK_PATH=/$ndk -DANDROID_ABI=x86_64 -DDE_ANDROID_API=$ANDROID_SDK_VERSION" \
. .gitlab-ci/container/build-deqp.sh

DEQP_API=VK \
DEQP_TARGET="android" \
EXTRA_CMAKE_ARGS="-DDEQP_ANDROID_EXE=ON -DDEQP_TARGET_TOOLCHAIN=ndk-modern -DANDROID_NDK_PATH=/$ndk -DANDROID_ABI=x86_64 -DDE_ANDROID_API=$ANDROID_SDK_VERSION" \
. .gitlab-ci/container/build-deqp.sh

rm -rf /VK-GL-CTS

############### Downloading Cuttlefish resources ...

section_start cuttlefish "Downloading and setting up Cuttlefish"

mkdir /cuttlefish
pushd /cuttlefish

curl -L --retry 4 -f --retry-all-errors --retry-delay 60 \
  -O "https://${S3_HOST}/${S3_ANDROID_BUCKET}/${CUTTLEFISH_PROJECT_PATH}/aosp-${CUTTLEFISH_BUILD_VERSION_TAGS}.${CUTTLEFISH_BUILD_NUMBER}/aosp_cf_x86_64_only_phone-img-${CUTTLEFISH_BUILD_NUMBER}.tar.zst"

tar --zstd -xvf aosp_cf_x86_64_only_phone-img-"$CUTTLEFISH_BUILD_NUMBER".tar.zst
rm aosp_cf_x86_64_only_phone-img-"$CUTTLEFISH_BUILD_NUMBER".tar.zst
ls -lhS ./*

curl -L --retry 4 -f --retry-all-errors --retry-delay 60 \
  -O "https://${S3_HOST}/${S3_ANDROID_BUCKET}/${CUTTLEFISH_PROJECT_PATH}/aosp-${CUTTLEFISH_BUILD_VERSION_TAGS}.${CUTTLEFISH_BUILD_NUMBER}/cvd-host_package-x86_64.tar.zst"
tar --zst -xvf cvd-host_package-x86_64.tar.zst
rm cvd-host_package-x86_64.tar.zst

curl -L --retry 4 -f --retry-all-errors --retry-delay 60 \
  -O "https://${S3_HOST}/${S3_ANDROID_BUCKET}/${AOSP_KERNEL_PROJECT_PATH}/aosp-kernel-common-${AOSP_KERNEL_BUILD_VERSION_TAGS}.${AOSP_KERNEL_BUILD_NUMBER}/bzImage"
curl -L --retry 4 -f --retry-all-errors --retry-delay 60 \
  -O "https://${S3_HOST}/${S3_ANDROID_BUCKET}/${AOSP_KERNEL_PROJECT_PATH}/aosp-kernel-common-${AOSP_KERNEL_BUILD_VERSION_TAGS}.${AOSP_KERNEL_BUILD_NUMBER}/initramfs.img"

popd

addgroup --system kvm
usermod -a -G kvm,cvdnetwork root

section_end cuttlefish

############### Downloading Android CTS

. .gitlab-ci/container/build-android-cts.sh

############### Uninstall the build software

section_switch debian_cleanup "Cleaning up base Debian system"

rm -rf "/${ndk:?}"

export SUDO_FORCE_REMOVE=yes
apt-get purge -y "${EPHEMERAL[@]}"

. .gitlab-ci/container/container_post_build.sh

section_end debian_cleanup

############### Remove unused packages

. .gitlab-ci/container/strip-rootfs.sh
