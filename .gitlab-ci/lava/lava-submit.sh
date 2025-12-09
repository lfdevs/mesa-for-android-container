#!/usr/bin/env bash
# shellcheck disable=SC2086 # we want word splitting
# shellcheck disable=SC1091 # paths only become valid at runtime

# shellcheck disable=SC1090
source "${FDO_CI_BASH_HELPERS}"

fdo_log_section_start_collapsed prepare_rootfs "Preparing root filesystem"

set -ex

# If we run in the fork (not from mesa or Marge-bot), reuse mainline kernel and rootfs, if exist.
ROOTFS_URL="$(fdo_find_s3_path "$LAVA_ROOTFS_PATH")" ||
{
	set +x
	fdo_log_section_error "Sorry, I couldn't find a viable built path for ${LAVA_ROOTFS_PATH} in either mainline or a fork." >&2
	echo "" >&2
	echo "If you're working on CI, this probably means that you're missing a dependency:" >&2
	echo "this job ran ahead of the job which was supposed to upload that artifact." >&2
	echo "" >&2
	echo "If you aren't working on CI, please ping @mesa/ci-helpers to see if we can help." >&2
	echo "" >&2
	echo "This job is going to fail, because I can't find the resources I need. Sorry." >&2
	set -x
	exit 1
}

rm -rf results
mkdir results

fdo_filter_env_vars > dut-env-vars.sh
# Set SCRIPTS_DIR to point to the Mesa install we download for the DUT
echo "export SCRIPTS_DIR='$CI_PROJECT_DIR/install'" >> dut-env-vars.sh

fdo_log_section_end prepare_rootfs

# Prepare env vars for upload.
fdo_log_section_start_collapsed variables "Environment variables passed through to device:"
cat dut-env-vars.sh
fdo_log_section_end variables

fdo_log_section_start_collapsed lava_submit "Submitting job for scheduling"

touch results/lava.log
tail -f results/lava.log &
# Ensure that we are printing the commands that are being executed,
# making it easier to debug the job in case it fails.
set -x

# List of optional overlays
LAVA_EXTRA_OVERLAYS=()
if [ -n "${LAVA_FIRMWARE:-}" ]; then
    for fw in $LAVA_FIRMWARE; do
        LAVA_EXTRA_OVERLAYS+=(
            - append-overlay
              --name=linux-firmware
              --url="https://${S3_BASE_PATH}/${FIRMWARE_REPO}/${fw}-${FIRMWARE_TAG}.tar"
              --path="/"
              --format=tar
        )
    done
fi
if [ -n "${HWCI_KERNEL_MODULES:-}" ]; then
	LAVA_EXTRA_OVERLAYS+=(
		- append-overlay
		  --name=kernel-modules
		  --url="${KERNEL_IMAGE_BASE}/${DEBIAN_ARCH}/modules.tar"
		  --path="/"
		  --format=tar
	)
fi
if [ -n "${ANDROID_CTS_TAG:-}" ]; then
	LAVA_EXTRA_OVERLAYS+=(
		- append-overlay
		  --name=android-cts
		  --url="$(fdo_find_s3_path "${DATA_STORAGE_PATH}/android-cts/${ANDROID_CTS_TAG}.tar.zst")"
		  --path="/"
		  --format=tar
		  --compression=zstd
	)
fi
if [ -n "${FLUSTER_TAG:-}" ]; then
	LAVA_EXTRA_OVERLAYS+=(
		- append-overlay
		  --name=vkd3d-proton
		  --url="$(fdo_find_s3_path "${DATA_STORAGE_PATH}/fluster/${FLUSTER_TAG}/vectors.tar.zst")"
		  --path="/"
		  --format=tar
		  --compression=zstd
	)
fi
if [ -n "${VKD3D_PROTON_TAG:-}" ]; then
	LAVA_EXTRA_OVERLAYS+=(
		- append-overlay
		  --name=vkd3d-proton
		  --url="$(fdo_find_s3_path "${DATA_STORAGE_PATH}/vkd3d-proton/${VKD3D_PROTON_TAG}/${MESA_IMAGE_PATH}/vkd3d-proton.tar.zst")"
		  --path="/"
		  --format=tar
		  --compression=zstd
	)
fi
if [ -n "${S3_ANDROID_ARTIFACT_NAME:-}" ]; then
	LAVA_EXTRA_OVERLAYS+=(
		- append-overlay
		  --name=android-cf-image
		  --url="https://${S3_BASE_PATH}/${CUTTLEFISH_PROJECT_PATH}/aosp-${CUTTLEFISH_BUILD_VERSION_TAGS}.${CUTTLEFISH_BUILD_NUMBER}/aosp_cf_${ARCH}_only_phone-img-${CUTTLEFISH_BUILD_NUMBER}.tar.zst"
		  --path="/cuttlefish"
		  --format=tar
		  --compression=zstd
		- append-overlay
		  --name=android-cvd-host-package
		  --url="https://${S3_BASE_PATH}/${CUTTLEFISH_PROJECT_PATH}/aosp-${CUTTLEFISH_BUILD_VERSION_TAGS}.${CUTTLEFISH_BUILD_NUMBER}/cvd-host_package-${ARCH}.tar.zst"
		  --path="/cuttlefish"
		  --format=tar
		  --compression=zstd
		- append-overlay
		  --name=android-kernel
		  --url="https://${S3_BASE_PATH}/${AOSP_KERNEL_PROJECT_PATH}/aosp-kernel-common-${AOSP_KERNEL_BUILD_VERSION_TAGS}.${AOSP_KERNEL_BUILD_NUMBER}/bzImage"
		  --path="/cuttlefish"
		  --format=file
		- append-overlay
		  --name=android-initramfs
		  --url="https://${S3_BASE_PATH}/${AOSP_KERNEL_PROJECT_PATH}/aosp-kernel-common-${AOSP_KERNEL_BUILD_VERSION_TAGS}.${AOSP_KERNEL_BUILD_NUMBER}/initramfs.img"
		  --path="/cuttlefish"
		  --format=file
	)
fi

lava-job-submitter \
	--farm "${FARM}" \
	--device-type "${DEVICE_TYPE}" \
	--boot-method "${BOOT_METHOD}" \
	--job-timeout-min $((CI_JOB_TIMEOUT/60 - 5)) \
	--dump-yaml \
	--pipeline-info "$CI_JOB_NAME: $CI_PIPELINE_URL on $CI_COMMIT_REF_NAME ${CI_NODE_INDEX}/${CI_NODE_TOTAL}" \
	--rootfs-url "${ROOTFS_URL}" \
	--kernel-url-prefix "${KERNEL_IMAGE_BASE}/${DEBIAN_ARCH}" \
	--dtb-filename "${DTB}" \
	--env-file dut-env-vars.sh \
	--jwt-file "${S3_JWT_FILE}" \
	--kernel-image-name "${KERNEL_IMAGE_NAME}" \
	--kernel-image-type "${KERNEL_IMAGE_TYPE}" \
	--visibility-group "${VISIBILITY_GROUP}" \
	--lava-tags "${LAVA_TAGS}" \
	--mesa-job-name "$CI_JOB_NAME" \
	--structured-log-file "results/lava_job_detail.json" \
	--ssh-client-image "${LAVA_SSH_CLIENT_IMAGE}" \
	--project-dir "${CI_PROJECT_DIR}" \
	--project-name "${CI_PROJECT_NAME}" \
	--starting-section lava_submit \
	--job-submitted-at "${CI_JOB_STARTED_AT}" \
	- append-overlay \
		--name=mesa-build \
		--url="https://${PIPELINE_ARTIFACTS_BASE}/${S3_ARTIFACT_NAME:?}.tar.zst" \
		--compression=zstd \
		--path="${CI_PROJECT_DIR}" \
		--format=tar \
	"${LAVA_EXTRA_OVERLAYS[@]}" \
	- submit \
	>> results/lava.log
