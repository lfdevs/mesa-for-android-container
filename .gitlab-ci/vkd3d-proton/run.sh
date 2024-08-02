#!/usr/bin/env bash

set -ex

if [[ -z "$VK_DRIVER" ]]; then
    exit 1
fi

INSTALL=$(realpath -s "$PWD"/install)

RESULTS=$(realpath -s "$PWD"/results)

# Make sure the results folder exists
mkdir -p "$RESULTS"

# Set up the driver environment.
# Modifiying here directly LD_LIBRARY_PATH may cause problems when
# using a command wrapper. Hence, we will just set it when running the
# command.
export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:$INSTALL/lib/:/vkd3d-proton-tests/x64/"


# Set the Vulkan driver to use.
ARCH=$(uname -m)
export VK_DRIVER_FILES="$INSTALL/share/vulkan/icd.d/${VK_DRIVER}_icd.$ARCH.json"

# Set environment for Wine.
export WINEDEBUG="-all"
export WINEPREFIX="/vkd3d-proton-wine64"
export WINEESYNC=1

# Sanity check to ensure that our environment is sufficient to make our tests
# run against the Mesa built by CI, rather than any installed distro version.
MESA_VERSION=$(cat "$INSTALL/VERSION")
if ! vulkaninfo | grep driverInfo | tee /tmp/version.txt | grep -F "Mesa $MESA_VERSION"; then
    printf "%s\n" "Found $(cat /tmp/version.txt), expected $MESA_VERSION"
    exit 1
fi

printf "%s\n" "Running vkd3d-proton testsuite..."

if ! /vkd3d-proton-tests/x64/bin/d3d12 > "$RESULTS/vkd3d-proton-log.txt"; then
    # Check if the executable finished (ie. no segfault).
    if ! grep "tests executed" "$RESULTS/vkd3d-proton-log.txt" > /dev/null; then
        error "Failed, see ${ARTIFACTS_BASE_URL}/results/vkd3d-proton-log.txt"
        exit 1
    fi

    # Collect all the failures
    RESULTSFILE="$RESULTS/$VKD3D_PROTON_RESULTS.txt"
    mkdir -p .gitlab-ci/vkd3d-proton
    if ! grep "Test failed" "$RESULTS"/vkd3d-proton-log.txt > "$RESULTSFILE"; then
      error "Failed to get the list of failing tests, see ${ARTIFACTS_BASE_URL}/results/vkd3d-proton-log.txt"
      exit 1
    fi

    # Gather the list expected failures
    if [ -f "$INSTALL/$VKD3D_PROTON_RESULTS-vkd3d.txt" ]; then
        cp "$INSTALL/$VKD3D_PROTON_RESULTS-vkd3d.txt" \
           ".gitlab-ci/vkd3d-proton/$VKD3D_PROTON_RESULTS.txt.baseline"
    else
        printf "%s\n" "$VKD3D_PROTON_RESULTS-vkd3d.txt not found, assuming a \"no failures\" baseline."
        touch ".gitlab-ci/vkd3d-proton/$VKD3D_PROTON_RESULTS.txt.baseline"
    fi

    # Make sure that the failures found in this run match the current expectation
    if ! diff --color=always -u ".gitlab-ci/vkd3d-proton/$VKD3D_PROTON_RESULTS.txt.baseline" "$RESULTSFILE"; then
        error "Changes found, see ${ARTIFACTS_BASE_URL}/results/vkd3d-proton-log.txt"
        exit 1
    fi
fi

exit 0
