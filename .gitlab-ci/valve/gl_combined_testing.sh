#!/bin/bash

set -eu

function execute_testsuite {
    local RESULTS_FOLDER EXEC_DONE_FILE

    RESULTS_FOLDER="results/$1"
    EXEC_DONE_FILE="$RESULTS_FOLDER/.done"

    if [ ! -f "$EXEC_DONE_FILE" ]; then
        DEQP_RESULTS_DIR="$RESULTS_FOLDER" PIGLIT_RESULTS_DIR="$RESULTS_FOLDER" $2
        touch "$EXEC_DONE_FILE"
    else
        echo "--> Skipped, as it already was executed"
    fi
}

echo -e "\n# GL CTS testing"
DEQP_VER=gl46 execute_testsuite gl ./install/deqp-runner.sh

echo -e "\n# GLES CTS testing"
DEQP_SUITE=zink-radv execute_testsuite gles ./install/deqp-runner.sh

echo -e "\n# Piglit testing"
execute_testsuite piglit ./install/piglit/piglit-runner.sh
