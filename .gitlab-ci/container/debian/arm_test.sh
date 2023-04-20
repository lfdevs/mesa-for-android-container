#!/bin/bash
# shellcheck disable=SC2154 # arch is assigned in previous scripts

set -e
set -o xtrace

############### Install packages for baremetal testing
apt-get install -y ca-certificates
sed -i -e 's/http:\/\/deb/https:\/\/deb/g' /etc/apt/sources.list
apt-get update

apt-get install -y --no-remove \
        cpio \
        curl \
        fastboot \
        netcat \
        procps \
        python3-distutils \
        python3-minimal \
        python3-serial \
        rsync \
        snmp \
        zstd

# setup SNMPv2 SMI MIB
curl -L --retry 4 -f --retry-all-errors --retry-delay 60 \
    https://raw.githubusercontent.com/net-snmp/net-snmp/master/mibs/SNMPv2-SMI.txt \
    -o /usr/share/snmp/mibs/SNMPv2-SMI.txt

. .gitlab-ci/container/baremetal_build.sh

if [[ "$arch" == "arm64" ]]; then
    # This firmware file from Debian bullseye causes hangs
    curl -L --retry 4 -f --retry-all-errors --retry-delay 60 \
      "https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain/qcom/a530_pfp.fw?id=d5f9eea5a251d43412b07f5295d03e97b89ac4a5" \
      -o /rootfs-arm64/lib/firmware/qcom/a530_pfp.fw
fi

mkdir -p /baremetal-files/jetson-nano/boot/
ln -s \
    /baremetal-files/Image \
    /baremetal-files/tegra210-p3450-0000.dtb \
    /baremetal-files/jetson-nano/boot/

mkdir -p /baremetal-files/jetson-tk1/boot/
ln -s \
    /baremetal-files/zImage \
    /baremetal-files/tegra124-jetson-tk1.dtb \
    /baremetal-files/jetson-tk1/boot/
