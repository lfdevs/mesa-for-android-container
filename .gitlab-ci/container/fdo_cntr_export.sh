#!/usr/bin/env bash

# This script exports the container image to a rootfs tarball and uploads it to
# S3.
#
# Usage:
#
#   ./fdo_cntr_export.sh <container-image-url>
#
# The container image URL is the URL of the container image to export. It can be
# a local path or a remote URL.

# Example:
# ./fdo_cntr_export.sh registry.freedesktop.org/mesa/mesa/debian/x86_64_test-android:tag

# When changing this file, you need to bump the following
# .gitlab-ci/image-tags.yml tags:
# DEBIAN_TEST_ANDROID_TAG
# DEBIAN_TEST_GL_TAG
# DEBIAN_TEST_VIDEO_TAG
# DEBIAN_TEST_VK_TAG

set -eux -o pipefail

: "${CONTAINER:=$1}"
IMAGE_URL="${CONTAINER}"
readonly IMAGE_URL

container=$(buildah from "$IMAGE_URL")
readonly container
readonly ROOTFSTAR=lava-rootfs.tar.zst
touch "$ROOTFSTAR"

buildah copy "$container" ".gitlab-ci/container/setup-rootfs.sh" /root/setup-rootfs.sh

# Using --isolation chroot to ensure proper execution in CI/CD environments
buildah run --isolation chroot "$container" -- /root/setup-rootfs.sh

buildah_export() {
    # Mount the volume
    mountpoint=$(buildah mount "$1")

    if [ ! -d "$mountpoint" ]; then
        echo "Mount point not found: $mountpoint" >/dev/stderr
        exit 1
    fi

    # These components will be provided via LAVA overlays,
    # so remove them from the core rootfs
    rm -rf "${mountpoint}/android-cts"
    rm -rf "${mountpoint}/cuttlefish"
    rm -rf "${mountpoint}/fluster/resources"
    rm -rf "${mountpoint}/vkd3d-proton-tests"
    rm -rf "${mountpoint}/vkd3d-proton-wine64"

    # Compress to zstd
    ZSTD_CLEVEL=10 tar -C "$mountpoint" -I zstd -cf "$2" .
}

# When hacking on it locally, the script might not be executed as root.
# In CI it's always root.
if (($(id -u) != 0)); then
    # Run unshare for rootless envs
    buildah unshare -- bash -c "$(declare -f buildah_export); buildah_export '$container' '$ROOTFSTAR'"
else
    # Run directly
    buildah_export "$container" "$ROOTFSTAR"
fi

# Unmount the container
buildah umount "$container"

# Remove the container
buildah rm "$container"

# Upload the rootfs tarball to S3.
# The URL format matches the registry format, making it easier to match this URL later.
curl --fail --retry-connrefused --retry 4 --retry-delay 30 \
  --header "Authorization: Bearer $(cat "${S3_JWT_FILE}")" \
  -X PUT --form file=@"$ROOTFSTAR" \
  "https://${S3_HOST}/${S3_KERNEL_BUCKET}/${CI_PROJECT_PATH}/${CI_JOB_NAME}:${FDO_DISTRIBUTION_TAG}"
