#!/usr/bin/env bash
#
# When changing this file, you need to bump the following
# .gitlab-ci/image-tags.yml tags:
# DEBIAN_TEST_ANDROID_TAG

# This script runs in a container to:
# 1. Download the Android CTS (Compatibility Test Suite)
# 2. Filter out unneeded test modules
# 3. Compress and upload the stripped version to S3
# Note: The 'build-' prefix in the filename is only to make it compatible
# with the bin/ci/update_tag.py script.

set -euo pipefail

section_start android-cts "Downloading Android CTS"

# xtrace is getting lost with the section switching
set -x

# Do a very early check to make sure the tag is correct without the need of
# setting up the environment variables locally
ci_tag_build_time_check "ANDROID_CTS_TAG"

# List of all CTS modules we might want to run in CI
# This should be the union of all modules required by our CI jobs
# Specific modules to run are selected via the ${GPU_VERSION}-android-cts-include.txt files
ANDROID_CTS_MODULES=(
    "CtsGraphicsTestCases"
    "CtsNativeHardwareTestCases"
    "CtsSkQPTestCases"
)

ANDROID_CTS_VERSION="${ANDROID_VERSION}_r3"
ANDROID_CTS_DEVICE_ARCH="x86"

# Download the stripped CTS from S3, because the CTS download from Google can take 20 minutes
CTS_FILENAME="android-cts-${ANDROID_CTS_VERSION}-linux_x86-${ANDROID_CTS_DEVICE_ARCH}"
ARTIFACT_PATH="${DATA_STORAGE_PATH}/android-cts/${ANDROID_CTS_TAG}.tar.zst"

if FOUND_ARTIFACT_URL="$(find_s3_project_artifact "${ARTIFACT_PATH}")"; then
    echo "Found Android CTS at: ${FOUND_ARTIFACT_URL}"
    curl-with-retry "${FOUND_ARTIFACT_URL}" | tar --zstd -x -C /
else
    echo "No cached CTS found, downloading from Google and uploading to S3..."
    curl-with-retry --remote-name "https://dl.google.com/dl/android/cts/${CTS_FILENAME}.zip"

    # Disable zipbomb detection, because the CTS zip file is too big
    # At least locally, it is detected as a zipbomb
    UNZIP_DISABLE_ZIPBOMB_DETECTION=true \
        unzip -q -d / "${CTS_FILENAME}.zip"
    rm "${CTS_FILENAME}.zip"

    # Keep only the interesting tests to save space
    # shellcheck disable=SC2086 # we want word splitting
    ANDROID_CTS_MODULES_KEEP_EXPRESSION=$(printf "%s|" "${ANDROID_CTS_MODULES[@]}" | sed -e 's/|$//g')
    find /android-cts/testcases/ -mindepth 1 -type d | grep -v -E "$ANDROID_CTS_MODULES_KEEP_EXPRESSION" | xargs rm -rf

    # Using zstd compressed tarball instead of zip, the compression ratio is almost the same, but
    # the extraction is faster, also LAVA overlays don't support zip compression.
    tar --zstd -cf "${CTS_FILENAME}.tar.zst" /android-cts
    ci-fairy s3cp --token-file "${S3_JWT_FILE}" "${CTS_FILENAME}.tar.zst" \
        "https://${S3_BASE_PATH}/${CI_PROJECT_PATH}/${ARTIFACT_PATH}"
fi

section_end android-cts
