#!/usr/bin/env bash

# shellcheck disable=SC1091 # The relative paths in this file only become valid at runtime.
# shellcheck disable=SC2034 # Variables are used in scripts called from here
# shellcheck disable=SC2086 # we want word splitting

# When changing this file, you need to bump the following
# .gitlab-ci/image-tags.yml tags:
# DEBIAN_TEST_VIDEO_TAG

# Install fluster in /fluster.

set -uex

section_start fluster "Installing Fluster"

# Do a very early check to make sure the tag is correct without the need of
# setting up the environment variables locally
ci_tag_build_time_check "FLUSTER_TAG"

FLUSTER_REVISION="1864188e358464e77c889390fff259ef3b2c1159"

git clone https://github.com/fluendo/fluster.git --single-branch --no-checkout

pushd fluster || exit
git checkout "${FLUSTER_REVISION}"
popd || exit

ARTIFACT_PATH="${DATA_STORAGE_PATH}/fluster/${FLUSTER_TAG}/vectors.tar.zst"

if FOUND_ARTIFACT_URL="$(find_s3_project_artifact "${ARTIFACT_PATH}")"; then
    echo "Found fluster vectors at: ${FOUND_ARTIFACT_URL}"
    mv fluster/ /
    curl-with-retry "${FOUND_ARTIFACT_URL}" | tar --zstd -x -C /
else
    echo "No cached vectors found, rebuilding..."
    # Download the necessary vectors
    fluster/fluster.py download -j ${FDO_CI_CONCURRENT:-4} --codec H.264,H.265,VP9

    # Build fluster vectors archive and upload it
    tar --zstd -cf "vectors.tar.zst" fluster/resources/
    ci-fairy s3cp --token-file "${S3_JWT_FILE}" "vectors.tar.zst" \
        "https://${S3_BASE_PATH}/${CI_PROJECT_PATH}/${ARTIFACT_PATH}"
    mv fluster/ /
fi

section_end fluster
