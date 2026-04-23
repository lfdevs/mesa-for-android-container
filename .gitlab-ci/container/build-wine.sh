#!/bin/bash

# When changing this file, you need to bump the following
# .gitlab-ci/image-tags.yml tags:
# DEBIAN_TEST_VK_TAG
set -eux

# This script sets up the wine components and the wine prefix that will be used
# when running wine.  All of this gets bundled up into a tarball that's
# conditionally overlaid on LAVA runners when we need wine (container-based jobs
# will have the wine contents in them).  Everything that's included in the
# WINE_S3_ARTIFACT (other than the debian wine packages) must go in this script,
# because that determines the cached hash it's uploaded under for LAVA runners.

section_start wine "Setting up Wine"

# Do a very early check to make sure the tag is correct without the need of
# setting up the environment variables locally
ci_tag_build_time_check "WINE_TAG"

export WINEPREFIX="/wineprefix"
export WINEDEBUG="-all"

# We don't want crash dialogs
cat >crashdialog.reg <<EOF
Windows Registry Editor Version 5.00

[HKEY_CURRENT_USER\Software\Wine\WineDbg]
"ShowCrashDialog"=dword:00000000

EOF

# Set the wine prefix and disable the crash dialog
wine regedit crashdialog.reg
rm crashdialog.reg

# An immediate wine command may fail with: "${WINEPREFIX}: Not a
# valid wine prefix."  and that is just spit because of checking
# the existance of the system.reg file, which fails.  Just giving
# it a bit more of time for it to be created solves the problem
# ...
while ! test -f  "${WINEPREFIX}/system.reg"; do sleep 1; done

section_end wine

section_start wine-apitrace "Setting up Apitrace for Wine"

APITRACE_VERSION="14.0"
APITRACE_VERSION_DATE=""

APITRACE_ARCH=""
if [ "$DEBIAN_ARCH" == "arm64" ]; then
  APITRACE_ARCH="-arm"
fi

curl -L -O --retry 4 -f --retry-all-errors --retry-delay 60 \
  "https://github.com/apitrace/apitrace/releases/download/${APITRACE_VERSION}/apitrace-${APITRACE_VERSION}${APITRACE_VERSION_DATE}-win64${APITRACE_ARCH}.7z"
7zr x "apitrace-${APITRACE_VERSION}${APITRACE_VERSION_DATE}-win64${APITRACE_ARCH}.7z" \
      "apitrace-${APITRACE_VERSION}${APITRACE_VERSION_DATE}-win64${APITRACE_ARCH}/bin/apitrace.exe" \
      "apitrace-${APITRACE_VERSION}${APITRACE_VERSION_DATE}-win64${APITRACE_ARCH}/bin/d3dretrace.exe"
mv "apitrace-${APITRACE_VERSION}${APITRACE_VERSION_DATE}-win64${APITRACE_ARCH}" /apitrace-msvc-win64
rm "apitrace-${APITRACE_VERSION}${APITRACE_VERSION_DATE}-win64${APITRACE_ARCH}.7z"

section_end wine-apitrace

section_start DXVK "Installing DXVK"

set -uex

overrideDll() {
  if ! wine reg add 'HKEY_CURRENT_USER\Software\Wine\DllOverrides' /v "$1" /d native /f; then
    echo -e "Failed to add override for $1"
    exit 1
  fi
}

dxvk_install_release() {
    local DXVK_VERSION=${1:?}

    curl -L --retry 4 -f --retry-all-errors --retry-delay 60 \
	-O "https://github.com/doitsujin/dxvk/releases/download/v${DXVK_VERSION}/dxvk-${DXVK_VERSION}.tar.gz"
    tar xzpf dxvk-"${DXVK_VERSION}".tar.gz
    cp "dxvk-${DXVK_VERSION}"/x64/*.dll "$WINEPREFIX/drive_c/windows/system32/"
    rm -rf "dxvk-${DXVK_VERSION}"
    rm dxvk-"${DXVK_VERSION}".tar.gz
}

# DXVK upstream only builds for x64/x32, so we snag arm64 binaries out of
# another project that packages it as arm64 and arm64ec.
dxvk_install_hangover() {
    local DXVK_VERSION=${1:?}
    local HANGOVER_VERSION=${2:?}

    curl -L --retry 4 -f --retry-all-errors --retry-delay 60 \
        -o hangover_dlls.tar \
	-O "https://github.com/AndreRH/hangover/releases/download/hangover-${HANGOVER_VERSION}/hangover_${HANGOVER_VERSION}_dlls.tar"
    tar xpf hangover_dlls.tar
    rm hangover_dlls.tar

    tar xpf "dxvk-v${DXVK_VERSION}.tar.gz"
    rm "dxvk-v${DXVK_VERSION}.tar.gz"

    cp "dxvk-v${DXVK_VERSION}"/aarch64/*.dll "$WINEPREFIX/drive_c/windows/system32/"
    rm -rf "dxvk-v${DXVK_VERSION}"
}

if [ "$DEBIAN_ARCH" != arm64 ]; then
  # x32 and x64 binaries
  dxvk_install_release "2.7.1"
else
  # Use another packager's tarball for ARM binaries, since upstream DXVK doesn't
  # generate them.
  dxvk_install_hangover "2.7.1" "11.4"
fi

overrideDll d3d8
overrideDll d3d9
overrideDll d3d10core
overrideDll d3d11
overrideDll dxgi

# make sure the registry keys are flushed.
wineserver -k

section_end DXVK

# Archive and upload wine for use as a LAVA overlay, if the archive doesn't exist yet
WINE_S3_ARTIFACT="wine.tar.zst"
ARTIFACT_PATH="${DATA_STORAGE_PATH}/wine/${DEBIAN_TEST_VK_TAG}-${WINE_TAG}/${CI_JOB_NAME}/${WINE_S3_ARTIFACT}"
if FOUND_ARTIFACT_URL="$(find_s3_project_artifact "${ARTIFACT_PATH}")"; then
  echo "Found wine at: ${FOUND_ARTIFACT_URL}, skipping upload"
else
  echo "Uploaded wine not found, reuploading..."
  tar --zstd -cf "$WINE_S3_ARTIFACT" -C / \
    "${WINEPREFIX#/}" \
    /apitrace-msvc-win64 \
    /usr/lib/*/wine
  ci-fairy s3cp --token-file "${S3_JWT_FILE}" "$WINE_S3_ARTIFACT" \
    "https://${S3_BASE_PATH}/${CI_PROJECT_PATH}/${ARTIFACT_PATH}"
  rm "$WINE_S3_ARTIFACT"
fi
