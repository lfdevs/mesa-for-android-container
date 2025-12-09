#!/usr/bin/env bash
# shellcheck disable=SC1091 # paths only become valid at runtime

. "${SCRIPTS_DIR}/setup-test-env.sh"

set -eu -o pipefail

comma_separated() {
  local IFS=,
  echo "$*"
}

if [[ -z "$VK_DRIVER" ]]; then
    printf "VK_DRIVER is not defined\n"
    exit 1
fi

if [ -z "$VKD3D_PROTON_TAG" ]; then
    echo "VKD3D_PROTON_TAG must be set to the conditional build tag"
    exit 1
fi

# Are we using the right vkd3d-proton version?
ci_tag_test_time_check "VKD3D_PROTON_TAG"

INSTALL=$(realpath -s "$PWD"/install)

# Set up the driver environment.
# Modifiying here directly LD_LIBRARY_PATH may cause problems when
# using a command wrapper. Hence, we will just set it when running the
# command.
export LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}:$INSTALL/lib/:/vkd3d-proton-tests/lib/"


# Set the Vulkan driver to use.
ARCH=$(uname -m)
export VK_DRIVER_FILES="$INSTALL/share/vulkan/icd.d/${VK_DRIVER}_icd.$ARCH.json"

# Set environment for Wine.
export WINEDEBUG="-all"
export WINEPREFIX="/vkd3d-proton-wine64"
export WINEESYNC=1

if [ -f "$INSTALL/$GPU_VERSION-vkd3d-skips.txt" ]; then
  mapfile -t skips < <(grep -vE '^#|^$' "$INSTALL/$GPU_VERSION-vkd3d-skips.txt")
  VKD3D_TEST_EXCLUDE=$(comma_separated "${skips[@]}")
  printf 'VKD3D_TEST_EXCLUDE=%s\n' "$VKD3D_TEST_EXCLUDE"
  export VKD3D_TEST_EXCLUDE
fi

# Sanity check to ensure that our environment is sufficient to make our tests
# run against the Mesa built by CI, rather than any installed distro version.
MESA_VERSION=$(cat "$INSTALL/VERSION")
if ! vulkaninfo | grep driverInfo | tee /tmp/version.txt | grep -qF "Mesa $MESA_VERSION"; then
    printf "%s\n" "Found $(cat /tmp/version.txt), expected $MESA_VERSION"
    exit 1
fi

# Gather the list expected failures
EXPECTATIONFILE="$RESULTS_DIR/$GPU_VERSION-vkd3d-fails.txt"
if [ -f "$INSTALL/$GPU_VERSION-vkd3d-fails.txt" ]; then
    # Ignore the grep "failure" if the file exists but contains only comments
    # or empty lines; the expectation file used will be empty in this case,
    # which is not a problem.
    grep -vE '^(#|$)' "$INSTALL/$GPU_VERSION-vkd3d-fails.txt" | sort > "$EXPECTATIONFILE" || true
else
    printf "%s\n" "$GPU_VERSION-vkd3d-fails.txt not found, assuming a \"no failures\" baseline."
    touch "$EXPECTATIONFILE"
fi

if [ -f "$INSTALL/$GPU_VERSION-vkd3d-flakes.txt" ]; then
  mapfile -t flakes < <(grep -vE '^#|^$' "$INSTALL/$GPU_VERSION-vkd3d-flakes.txt")
else
  flakes=()
fi

# Some sanity checks before we start
mapfile -t flakes_dups < <(
  [ ${#flakes[@]} -eq 0 ] ||
  printf '%s\n' "${flakes[@]}" | sort | uniq -d
)
if [ ${#flakes_dups[@]} -gt 0 ]; then
  printf >&2 'Duplicate flakes lines:\n'
  printf >&2 '  %s\n' "${flakes_dups[@]}"
  exit 1
fi

flakes_in_baseline=()
for flake in "${flakes[@]}"; do
  if grep -qF "$flake" "$EXPECTATIONFILE"; then
    flakes_in_baseline+=("$flake")
  fi
done
if [ ${#flakes_in_baseline[@]} -gt 0 ]; then
  printf >&2 "Flakes found in %s:\n" "$EXPECTATIONFILE"
  printf >&2 '  %s\n' "${flakes_in_baseline[@]}"
  exit 1
fi

printf "%s\n" "Running vkd3d-proton testsuite..."

LOGFILE="$RESULTS_DIR/vkd3d-proton-log.txt"
TEST_LOGS="/test-logs"
pushd /vkd3d-proton-tests
tests/test-runner.sh ./d3d12 --jobs "${FDO_CI_CONCURRENT:-4}" --output-dir "$TEST_LOGS" | tee "$LOGFILE" || true
popd

printf '\n\n'

# Print list of flakes seen this time
flakes_seen=()
for flake in "${flakes[@]}"; do
  if grep -qF "FAILED $flake" "$LOGFILE"; then
    flakes_seen+=("$flake")
  fi
done
if [ ${#flakes_seen[@]} -gt 0 ]; then
  # Keep this string and output format in line with the corresponding
  # deqp-runner message
  printf >&2 '\nSome known flakes found:\n'
  printf >&2 '  %s\n' "${flakes_seen[@]}"
fi

# Collect all the failures; ignore grep "failure" if there are none
fails_lines=$(grep -oE "^FAILED .+$" "$LOGFILE" | cut -d' ' -f2 | sort) || true
if [ -n "$fails_lines" ]; then
  mapfile -t fails < <(echo "$fails_lines")
else
  fails=()
fi

# Save test output for failed tests (before excluding flakes)
for failed_test in "${fails[@]}"; do
  cp "$TEST_LOGS/$failed_test.log" "$RESULTS_DIR/$failed_test.log"
done

# Ignore flakes when comparing
for flake in "${flakes[@]}"; do
  for idx in "${!fails[@]}"; do
    grep -qF "$flake" <<< "${fails[$idx]}" && unset -v 'fails[$idx]'
  done
done

RESULTSFILE="$RESULTS_DIR/$GPU_VERSION.txt"
for failed_test in "${fails[@]}"; do
  if ! grep -qE "$failed_test end" "$RESULTS_DIR/$failed_test.log"; then
    test_status=Crash
  elif grep -qE "Test failed:" "$RESULTS_DIR/$failed_test.log"; then
    test_status=Fail
  else
    test_status=Unknown
  fi
  printf '%s,%s\n' "$failed_test" "$test_status"
done > "$RESULTSFILE"

# Catch tests listed but not executed or not failing
mapfile -t expected_fail_lines < "$EXPECTATIONFILE"
for expected_fail_line in "${expected_fail_lines[@]}"; do
  test_name=$(cut -d, -f1 <<< "$expected_fail_line")
  if [ ! -f "$TEST_LOGS/$test_name.log" ]; then
    test_status='UnexpectedImprovement(Skip)'
  elif [ ! -f "$RESULTS_DIR/$test_name.log" ]; then
    test_status='UnexpectedImprovement(Pass)'
  else
    continue
  fi
  printf '%s,%s\n' "$test_name" "$test_status"
done >> "$RESULTSFILE"

mapfile -t unexpected_results < <(comm -23 <(sort "$RESULTSFILE") <(sort "$EXPECTATIONFILE"))
if [ ${#unexpected_results[@]} -gt 0 ]; then
  printf >&2 '\nUnexpected results:\n'
  printf >&2 '  %s\n' "${unexpected_results[@]}"
  exit 1
fi

exit 0
