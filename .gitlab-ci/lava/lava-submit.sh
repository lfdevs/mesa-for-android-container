#!/bin/bash

set -e
set -x

# Try to use the kernel and rootfs built in mainline first, so we're more
# likely to hit cache
if curl -s -X HEAD -L --retry 4 -f --retry-all-errors --retry-delay 60 \
    "https://${BASE_SYSTEM_MAINLINE_HOST_PATH}/done"; then
	BASE_SYSTEM_HOST_PATH="${BASE_SYSTEM_MAINLINE_HOST_PATH}"
else
	BASE_SYSTEM_HOST_PATH="${BASE_SYSTEM_FORK_HOST_PATH}"
fi

rm -rf results
mkdir -p results/job-rootfs-overlay/

cp artifacts/ci-common/capture-devcoredump.sh results/job-rootfs-overlay/
cp artifacts/ci-common/init-*.sh results/job-rootfs-overlay/
cp artifacts/ci-common/intel-gpu-freq.sh results/job-rootfs-overlay/
cp "$SCRIPTS_DIR"/setup-test-env.sh results/job-rootfs-overlay/

# Prepare env vars for upload.
KERNEL_IMAGE_BASE_URL="https://${BASE_SYSTEM_HOST_PATH}" \
	artifacts/ci-common/generate-env.sh > results/job-rootfs-overlay/set-job-env-vars.sh
section_start variables "Variables passed through:"
cat results/job-rootfs-overlay/set-job-env-vars.sh
section_end variables

tar zcf job-rootfs-overlay.tar.gz -C results/job-rootfs-overlay/ .
ci-fairy s3cp --token-file "${CI_JOB_JWT_FILE}" job-rootfs-overlay.tar.gz "https://${JOB_ROOTFS_OVERLAY_PATH}"

ARTIFACT_URL="${FDO_HTTP_CACHE_URI:-}https://${BUILD_PATH}"
# Make it take the mesa build from MINIO_ARTIFACT_NAME, if it is specified in
# the environment. This will make the LAVA behavior consistent with the
# baremetal jobs.
if [ -n "${MINIO_ARTIFACT_NAME}" ]
then
	ARTIFACT_URL="${FDO_HTTP_CACHE_URI:-}https://${PIPELINE_ARTIFACTS_BASE}/${MINIO_ARTIFACT_NAME}.tar.zst"
fi

touch results/lava.log
tail -f results/lava.log &
PYTHONPATH=artifacts/ artifacts/lava/lava_job_submitter.py \
	--dump-yaml \
	--pipeline-info "$CI_JOB_NAME: $CI_PIPELINE_URL on $CI_COMMIT_REF_NAME ${CI_NODE_INDEX}/${CI_NODE_TOTAL}" \
	--rootfs-url-prefix "https://${BASE_SYSTEM_HOST_PATH}" \
	--kernel-url-prefix "https://${BASE_SYSTEM_HOST_PATH}" \
	--build-url "${ARTIFACT_URL}" \
	--job-rootfs-overlay-url "${FDO_HTTP_CACHE_URI:-}https://${JOB_ROOTFS_OVERLAY_PATH}" \
	--job-timeout ${JOB_TIMEOUT:-30} \
	--first-stage-init artifacts/ci-common/init-stage1.sh \
	--ci-project-dir ${CI_PROJECT_DIR} \
	--device-type ${DEVICE_TYPE} \
	--dtb ${DTB} \
	--jwt-file "${CI_JOB_JWT_FILE}" \
	--kernel-image-name ${KERNEL_IMAGE_NAME} \
	--kernel-image-type "${KERNEL_IMAGE_TYPE}" \
	--boot-method ${BOOT_METHOD} \
	--visibility-group ${VISIBILITY_GROUP} \
	--lava-tags "${LAVA_TAGS}" \
	--mesa-job-name "$CI_JOB_NAME" \
	>> results/lava.log
