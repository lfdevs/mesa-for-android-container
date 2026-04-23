#!/usr/bin/env bash
# shellcheck disable=SC2086 # we want word splitting
# shellcheck disable=SC1091 # paths only become valid at runtime

. "${SCRIPTS_DIR}/setup-test-env.sh"

section_start traces_prepare "traces: preparing test setup"

set -ex

# Our rootfs may not have "less", which apitrace uses during apitrace dump
export PAGER=cat  # FIXME: export everywhere

INSTALL=$(realpath -s "$PWD"/install)

if [ -n "${LAVA_HTTP_CACHE_URI:-}" ]; then
  export EXTRA_ARGS="${EXTRA_ARGS} --download-caching-proxy=${LAVA_HTTP_CACHE_URI}"
elif [ -n "${CI_TRON_JOB_HTTP_SERVER:-}" ]; then
  # The caching proxy doesn't appear to be working.
  # export EXTRA_ARGS="${EXTRA_ARGS} --download-caching-proxy=${CI_TRON_JOB_HTTP_SERVER}/caching_proxy/"
  true
elif [ -n "${FDO_HTTP_CACHE_URI:-}" ]; then
  # FIXME: remove when there is no baremetal traces job anymore.
  export EXTRA_ARGS="${EXTRA_ARGS} --download-caching-proxy=${FDO_HTTP_CACHE_URI}"
fi

if [ $GITLAB_USER_LOGIN == "marge-bot" ]; then
    # When merging the MR, uploading to the permanent storage for .pngs must
    # succeed.
    export EXTRA_ARGS="${EXTRA_ARGS} --snapshot-url-must-work"
fi

# Set up the environment.
export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:$INSTALL/lib/"
if [ -n "${VK_DRIVER}" ]; then
  ARCH=$(uname -m)
  export VK_DRIVER_FILES="$INSTALL/share/vulkan/icd.d/${VK_DRIVER}_icd.$ARCH.json"
fi

MESA_VERSION=$(head -1 "$INSTALL/VERSION" | sed 's/\./\\./g')

# Set environment for replay tool executables.
export PATH="/apitrace/build:/gfxreconstruct/build/bin:$PATH"

echo "Version:"
apitrace version 2>/dev/null || echo "apitrace not found (Linux)"

if [ "$GALLIUM_DRIVER" = "virpipe" ]; then
    # replay is to use virpipe, and virgl_test_server llvmpipe
    export GALLIUM_DRIVER="$GALLIUM_DRIVER"

    GALLIUM_DRIVER=llvmpipe \
    VTEST_USE_EGL_SURFACELESS=1 \
    VTEST_USE_GLES=1 \
    virgl_test_server >"$RESULTS_DIR"/vtest-log.txt 2>&1 &

    sleep 1
fi

cd $RESULTS_DIR && rm -rf ..?* .[!.]* ./*

if [ -n "$WINE_TAG" ]; then
  # Are we using the right wine version?
  ci_tag_test_time_check "WINE_TAG"

  # Set environment for Wine.
  export WINEDEBUG="-all"
  export WINEPREFIX="/wineprefix"
  export WINEESYNC=1
  export WINEPATH="/apitrace-msvc-win64/bin"

  # This may be useful if you're debugging DXVK loading.
  #export WINEDEBUG="+loaddll,+module"
fi

# Disable using fast-linked GPL shaders in DXVK.  Otherwise, we may end up with
# (subtly, hopefully) flaky rendering when the optimized pipeline gets swapped
# in.
export DXVK_CONFIG="dxvk.enableGraphicsPipelineLibrary=False"

# ANGLE: download compiled ANGLE runtime and the compiled restricted traces
# (all-in-one package).
if [ -n "$ANGLE_TRACE_FILES_TAG" ]; then
  ANGLE_DIR="${INSTALL}/traces-db/angle"
  mkdir -p "${ANGLE_DIR}"

  if [ "$(uname -m)" = "aarch64" ]; then
    ANGLE_ARCH=arm64
  else
    ANGLE_ARCH=x64
  fi

  FILE="angle-bin-${ANGLE_ARCH}-${ANGLE_TRACE_FILES_TAG}.tar.zst"
  curl --location --fail --retry-all-errors --retry 4 --retry-delay 60 \
    --header "Authorization: Bearer $(cat "${S3_JWT_FILE}")" \
    "https://s3.freedesktop.org/mesa-tracie-private/${FILE}" --output "${FILE}"
  tar --zstd -xf ${FILE} -C "${ANGLE_DIR}"
  rm ${FILE}

  EXTRA_ARGS="${EXTRA_ARGS} --traces-db ${INSTALL}/traces-db"
fi

# Sanity check to ensure that our environment is sufficient to make our tests
# run against the Mesa built by CI, rather than any installed distro version.
if [ -z "${VK_DRIVER}" ]; then
  wflinfo -a gles2 -p wayland | tee /tmp/version.txt | grep "Mesa $MESA_VERSION\(\s\|$\)"
else
  vulkaninfo | grep driverInfo | tee /tmp/version.txt | grep "Mesa $MESA_VERSION\(\s\|$\)"
fi

uncollapsed_section_switch traces "traces: run traces"

# This gets lost in uncollapsed_section_switch.
set -x

# wrapper to supress +x to avoid spamming the log
quiet() {
    set +x
    "$@"
    set -x
}

report_failure() {
    echo "Review the image changes and get a checksums patch at: ${ARTIFACTS_BASE_URL}/results/index.html"
    echo "If the new traces look correct to you, you can update the checksums"
    echo "locally by running:"
    echo "    ./bin/ci/update_traces_checksum.sh"
    echo "and resubmit this merge request."
    exit 1
}
report_success() {
    echo "All image checksums matched.  Results can be viewed at ${ARTIFACTS_BASE_URL}/results/index.html"
}

if gpu-trace-perf replay \
  -j ${FDO_CI_CONCURRENT:-4} \
  --fraction-start ${CI_NODE_INDEX:-1} \
  --fraction ${CI_NODE_TOTAL} \
  --output $RESULTS_DIR \
  --cache-dir $CACHE_DIR \
  --config $INSTALL/$REPLAY_CONFIG \
  --device $GPU_VERSION \
  --jwt "${S3_JWT_FILE}" \
  --snapshots-url "https://$PIGLIT_REPLAY_REFERENCE_IMAGES_BASE/" \
  --job-url "https://$JOB_ARTIFACTS_BASE/" \
  $EXTRA_ARGS \
  ; then
    quiet report_success
else
    quiet report_failure
fi
