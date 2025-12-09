#!/usr/bin/env bash
# shellcheck disable=SC2086 # we want word splitting
# shellcheck disable=SC1091 # paths only become valid at runtime

. "${SCRIPTS_DIR}/setup-test-env.sh"

ci_tag_test_time_check "ANDROID_CTS_TAG"

export PATH=/android-cts/jdk/bin/:$PATH
export JAVA_HOME=/android-cts/jdk

# Wait for the appops service to show up
while [ "$($ADB shell dumpsys -l | grep appops)" = "" ] ; do sleep 1; done

SKIP_FILE="$INSTALL/${GPU_VERSION}-android-cts-skips.txt"

EXCLUDE_FILTERS=""
if [ -e "$SKIP_FILE" ]; then
  EXCLUDE_FILTERS="$(grep -v -E "(^#|^[[:space:]]*$)" "$SKIP_FILE" | sed -e 's/\s*$//g' -e 's/.*/--exclude-filter "\0" /g')"
fi

INCLUDE_FILE="$INSTALL/${GPU_VERSION}-android-cts-include.txt"

if [ ! -e "$INCLUDE_FILE" ]; then
  set +x
  echo "ERROR: No include file (${GPU_VERSION}-android-cts-include.txt) found."
  echo "This means that we are running the all available CTS modules."
  echo "But the time to run it might be too long, please provide an include file instead."
  exit 1
fi

INCLUDE_FILTERS="$(grep -v -E "(^#|^[[:space:]]*$)" "$INCLUDE_FILE" | sed -e 's/\s*$//g' -e 's/.*/--include-filter "\0" /g')"

if [ -n "${ANDROID_CTS_PREPARE_COMMAND:-}" ]; then
  eval "$ANDROID_CTS_PREPARE_COMMAND"
fi

uncollapsed_section_switch android_cts_test "Android CTS: testing"

set +e
eval "/android-cts/tools/cts-tradefed" run commandAndExit cts-dev \
  $INCLUDE_FILTERS \
  $EXCLUDE_FILTERS

SUMMARY_FILE=/android-cts/results/latest/invocation_summary.txt

# Parse a line like `x/y modules completed` to check that all modules completed
COMPLETED_MODULES=$(sed -n -e '/modules completed/s/^\([0-9]\+\)\/\([0-9]\+\) .*$/\1/p' "$SUMMARY_FILE")
AVAILABLE_MODULES=$(sed -n -e '/modules completed/s/^\([0-9]\+\)\/\([0-9]\+\) .*$/\2/p' "$SUMMARY_FILE")
[ "$COMPLETED_MODULES" = "$AVAILABLE_MODULES" ]
# shellcheck disable=SC2319  # False-positive see https://github.com/koalaman/shellcheck/issues/2937#issuecomment-2660891195
MODULES_FAILED=$?

# Parse a line like `FAILED            : x` to check that no tests failed
[ "$(grep "^FAILED" "$SUMMARY_FILE" | tr -d ' ' | cut -d ':' -f 2)" = "0" ]
# shellcheck disable=SC2319  # False-positive see https://github.com/koalaman/shellcheck/issues/2937#issuecomment-2660891195
TESTS_FAILED=$?

[ "$MODULES_FAILED" = "0" ] && [ "$TESTS_FAILED" = "0" ]

# shellcheck disable=SC2034 # EXIT_CODE is used by the script that sources this one
EXIT_CODE=$?
set -e

mkdir "${RESULTS_DIR}/android-cts"
cp -r "/android-cts/results/latest/" "${RESULTS_DIR}/android-cts/results"
cp -r "/android-cts/logs/latest/" "${RESULTS_DIR}/android-cts/logs"

if [ -n "${ARTIFACTS_BASE_URL:-}" ]; then
  echo "============================================"
  echo "Review the Android CTS test results at: ${ARTIFACTS_BASE_URL}/results/android-cts/results/test_result.html"
fi

section_end android_cts_test

exit $EXIT_CODE
