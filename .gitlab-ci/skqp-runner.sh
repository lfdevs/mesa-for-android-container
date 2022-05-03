#!/bin/sh
#
# Copyright (C) 2022 Collabora Limited
# Author: Guilherme Gallo <guilherme.gallo@collabora.com>
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.


copy_tests_files() (
    # Copy either unit test or render test files from a specific driver given by
    # GPU VERSION variable.
    # If there is no test file at the expected location, this function will
    # return error_code 1
    SKQP_BACKEND="${1}"
    SKQP_FILE_PREFIX="${INSTALL}/${GPU_VERSION}-skqp"

    if echo "${SKQP_BACKEND}" | grep -qE 'vk|gl(es)?'
    then
        SKQP_RENDER_TESTS_FILE="${SKQP_FILE_PREFIX}-${SKQP_BACKEND}_rendertests.txt"
        [ -f "${SKQP_RENDER_TESTS_FILE}" ] || return 1
        cp "${SKQP_RENDER_TESTS_FILE}" "${SKQP_ASSETS_DIR}"/skqp/rendertests.txt
        return 0
    fi

    # The unittests.txt path is hardcoded inside assets directory,
    # that is why it needs to be a special case.
    if echo "${SKQP_BACKEND}" | grep -qE "unitTest"
    then
        SKQP_UNIT_TESTS_FILE="${SKQP_FILE_PREFIX}_unittests.txt"
        [ -f "${SKQP_UNIT_TESTS_FILE}" ] || return 1
        cp "${SKQP_UNIT_TESTS_FILE}" "${SKQP_ASSETS_DIR}"/skqp/unittests.txt
    fi
)

test_vk_backend() {
    if echo "${SKQP_BACKENDS}" | grep -qE 'vk'
    then
        if [ -n "$VK_DRIVER" ]; then
            return 0
        fi

        echo "VK_DRIVER environment variable is missing."
        VK_DRIVERS=$(ls "$INSTALL"/share/vulkan/icd.d/ | cut -f 1 -d '_')
        if [ -n "${VK_DRIVERS}" ]
        then
            echo "Please set VK_DRIVER to the correct driver from the list:"
            echo "${VK_DRIVERS}"
        fi
        echo "No Vulkan tests will be executed, but it was requested in SKQP_BACKENDS variable. Exiting."
        exit 2
    fi

    # Vulkan environment is not configured, but it was not requested by the job
    return 1
}

setup_backends() {
    if test_vk_backend
    then
        export VK_ICD_FILENAMES="$INSTALL"/share/vulkan/icd.d/"$VK_DRIVER"_icd."${VK_CPU:-$(uname -m)}".json
    fi
}

set -ex

# Needed so configuration files can contain paths to files in /install
ln -sf "$CI_PROJECT_DIR"/install /install
INSTALL=${PWD}/install

if [ -z "$GPU_VERSION" ]; then
    echo 'GPU_VERSION must be set to something like "llvmpipe" or
"freedreno-a630" (it will serve as a component to find the path for files
residing in src/**/ci/*.txt)'
    exit 1
fi

LD_LIBRARY_PATH=$INSTALL:$LD_LIBRARY_PATH
setup_backends

SKQP_ASSETS_DIR=/skqp/assets
SKQP_RESULTS_DIR="${SKQP_RESULTS_DIR:-$PWD/results}"

mkdir -p "${SKQP_ASSETS_DIR}"/skqp

SKQP_EXITCODE=0
for SKQP_BACKEND in ${SKQP_BACKENDS}
do
    set -e
    if !  copy_tests_files "${SKQP_BACKEND}"
    then
        echo "No override test file found for ${SKQP_BACKEND}. Using the default one."
    fi

    set +e
    SKQP_BACKEND_RESULTS_DIR="${SKQP_RESULTS_DIR}"/"${SKQP_BACKEND}"
    mkdir -p "${SKQP_BACKEND_RESULTS_DIR}"
    /skqp/skqp "${SKQP_ASSETS_DIR}" "${SKQP_BACKEND_RESULTS_DIR}" "${SKQP_BACKEND}_"
    BACKEND_EXITCODE=$?

    if [ ! $BACKEND_EXITCODE -eq 0 ]
    then
        echo "skqp failed on ${SKQP_BACKEND} tests with ${BACKEND_EXITCODE} exit code."
    fi

    # Propagate error codes to leverage the final job result
    SKQP_EXITCODE=$(( SKQP_EXITCODE | BACKEND_EXITCODE ))
done

set +x

# Unit tests produce empty HTML reports, guide the user to check the TXT file.
if echo "${SKQP_BACKENDS}" | grep -qE "unitTest"
then
    # Remove the empty HTML report to avoid confusion
    rm -f "${SKQP_RESULTS_DIR}"/unitTest/report.html

    echo "See skqp unit test results at:"
    echo "https://$CI_PROJECT_ROOT_NAMESPACE.pages.freedesktop.org/-/$CI_PROJECT_NAME/-/jobs/$CI_JOB_ID/artifacts/${SKQP_RESULTS_DIR}/unitTest/unit_tests.txt"
fi

REPORT_FILES=$(mktemp)
find "${SKQP_RESULTS_DIR}"/**/report.html -type f > "${REPORT_FILES}"
while read -r REPORT
do
    BACKEND_NAME=$(echo "${REPORT}" | sed  's@.*/\([^/]*\)/report.html@\1@')
    echo "See skqp ${BACKEND_NAME} render tests report at:"
    echo "https://$CI_PROJECT_ROOT_NAMESPACE.pages.freedesktop.org/-/$CI_PROJECT_NAME/-/jobs/$CI_JOB_ID/artifacts/${REPORT}"
done < "${REPORT_FILES}"

# If there is no report available, tell the user that something is wrong.
if [ ! -s "${REPORT_FILES}" ]
then
    echo "No skqp report available. Probably some fatal error has occured during the skqp execution."
fi

exit $SKQP_EXITCODE
