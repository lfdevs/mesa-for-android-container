#!/usr/bin/env bash
# shellcheck disable=SC2154 # arch is assigned in previous scripts
# When changing this file, you need to bump the following
# .gitlab-ci/image-tags.yml tags:
# DEBIAN_TEST_BASE_TAG

set -e

. .gitlab-ci/setup-test-env.sh

set -o xtrace

############### Install packages for baremetal testing
DEPS=(
    cpio
    curl
    netcat-openbsd
    openssh-server
    procps
    python3-filelock
    python3-fire
    python3-minimal
    python3-serial
    rsync
    snmp
    zstd
)

apt-get install -y ca-certificates
sed -i -e 's/http:\/\/deb/https:\/\/deb/g' /etc/apt/sources.list.d/*
echo "deb [trusted=yes] https://gitlab.freedesktop.org/gfx-ci/ci-deb-repo/-/raw/${PKG_REPO_REV}/ ${FDO_DISTRIBUTION_VERSION%-*} main" | tee /etc/apt/sources.list.d/gfx-ci_.list
apt-get update

apt-get install -y --no-remove "${DEPS[@]}"

# setup SNMPv2 SMI MIB
curl -L --retry 4 -f --retry-all-errors --retry-delay 60 \
    https://raw.githubusercontent.com/net-snmp/net-snmp/master/mibs/SNMPv2-SMI.txt \
    -o /usr/share/snmp/mibs/SNMPv2-SMI.txt

. .gitlab-ci/container/baremetal_build.sh
