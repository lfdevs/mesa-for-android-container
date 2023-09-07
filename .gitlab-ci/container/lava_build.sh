#!/usr/bin/env bash
# shellcheck disable=SC1091 # The relative paths in this file only become valid at runtime.
# shellcheck disable=SC2034 # Variables are used in scripts called from here
# shellcheck disable=SC2086 # we want word splitting
# When changing this file, you need to bump the following
# .gitlab-ci/image-tags.yml tags:
# KERNEL_ROOTFS_TAG

set -e
set -o xtrace

export DEBIAN_FRONTEND=noninteractive
export LLVM_VERSION="${LLVM_VERSION:=15}"

check_minio()
{
    S3_PATH="${S3_HOST}/mesa-lava/$1/${DISTRIBUTION_TAG}/${DEBIAN_ARCH}"
    if curl -L --retry 4 -f --retry-delay 60 -s -X HEAD \
      "https://${S3_PATH}/done"; then
        echo "Remote files are up-to-date, skip rebuilding them."
        exit
    fi
}

check_minio "${FDO_UPSTREAM_REPO}"
check_minio "${CI_PROJECT_PATH}"

. .gitlab-ci/container/container_pre_build.sh

# Install rust, which we'll be using for deqp-runner.  It will be cleaned up at the end.
. .gitlab-ci/container/build-rust.sh

if [[ "$DEBIAN_ARCH" = "arm64" ]]; then
    GCC_ARCH="aarch64-linux-gnu"
    KERNEL_ARCH="arm64"
    SKQP_ARCH="arm64"
    DEFCONFIG="arch/arm64/configs/defconfig"
    DEVICE_TREES="arch/arm64/boot/dts/rockchip/rk3399-gru-kevin.dtb"
    DEVICE_TREES+=" arch/arm64/boot/dts/amlogic/meson-g12b-a311d-khadas-vim3.dtb"
    DEVICE_TREES+=" arch/arm64/boot/dts/amlogic/meson-gxl-s805x-libretech-ac.dtb"
    DEVICE_TREES+=" arch/arm64/boot/dts/amlogic/meson-gxm-khadas-vim2.dtb"
    DEVICE_TREES+=" arch/arm64/boot/dts/allwinner/sun50i-h6-pine-h64.dtb"
    DEVICE_TREES+=" arch/arm64/boot/dts/freescale/imx8mq-nitrogen.dtb"
    DEVICE_TREES+=" arch/arm64/boot/dts/mediatek/mt8192-asurada-spherion-r0.dtb"
    DEVICE_TREES+=" arch/arm64/boot/dts/mediatek/mt8183-kukui-jacuzzi-juniper-sku16.dtb"
    DEVICE_TREES+=" arch/arm64/boot/dts/nvidia/tegra210-p3450-0000.dtb"
    DEVICE_TREES+=" arch/arm64/boot/dts/qcom/apq8016-sbc.dtb"
    DEVICE_TREES+=" arch/arm64/boot/dts/qcom/apq8096-db820c.dtb"
    DEVICE_TREES+=" arch/arm64/boot/dts/qcom/sc7180-trogdor-lazor-limozeen-nots-r5.dtb"
    DEVICE_TREES+=" arch/arm64/boot/dts/qcom/sc7180-trogdor-kingoftown-r1.dtb"
    DEVICE_TREES+=" arch/arm64/boot/dts/qcom/sm8350-hdk.dtb"
    KERNEL_IMAGE_NAME="Image"

elif [[ "$DEBIAN_ARCH" = "armhf" ]]; then
    GCC_ARCH="arm-linux-gnueabihf"
    KERNEL_ARCH="arm"
    SKQP_ARCH="arm"
    DEFCONFIG="arch/arm/configs/multi_v7_defconfig"
    DEVICE_TREES="arch/arm/boot/dts/rk3288-veyron-jaq.dtb"
    DEVICE_TREES+=" arch/arm/boot/dts/sun8i-h3-libretech-all-h3-cc.dtb"
    DEVICE_TREES+=" arch/arm/boot/dts/imx6q-cubox-i.dtb"
    DEVICE_TREES+=" arch/arm/boot/dts/tegra124-jetson-tk1.dtb"
    KERNEL_IMAGE_NAME="zImage"
    . .gitlab-ci/container/create-cross-file.sh armhf
else
    GCC_ARCH="x86_64-linux-gnu"
    KERNEL_ARCH="x86_64"
    SKQP_ARCH="x64"
    DEFCONFIG="arch/x86/configs/x86_64_defconfig"
    DEVICE_TREES=""
    KERNEL_IMAGE_NAME="bzImage"
    ARCH_PACKAGES="libasound2-dev libcap-dev libfdt-dev libva-dev wayland-protocols p7zip"
fi

# Determine if we're in a cross build.
if [[ -e /cross_file-$DEBIAN_ARCH.txt ]]; then
    EXTRA_MESON_ARGS="--cross-file /cross_file-$DEBIAN_ARCH.txt"
    EXTRA_CMAKE_ARGS="-DCMAKE_TOOLCHAIN_FILE=/toolchain-$DEBIAN_ARCH.cmake"

    if [ $DEBIAN_ARCH = arm64 ]; then
        RUST_TARGET="aarch64-unknown-linux-gnu"
    elif [ $DEBIAN_ARCH = armhf ]; then
        RUST_TARGET="armv7-unknown-linux-gnueabihf"
    fi
    rustup target add $RUST_TARGET
    export EXTRA_CARGO_ARGS="--target $RUST_TARGET"

    export ARCH=${KERNEL_ARCH}
    export CROSS_COMPILE="${GCC_ARCH}-"
fi

apt-get update
apt-get install -y --no-remove \
		   -o Dpkg::Options::='--force-confdef' -o Dpkg::Options::='--force-confold' \
                   ${ARCH_PACKAGES} \
                   automake \
                   bc \
                   clang-${LLVM_VERSION} \
                   cmake \
		   curl \
                   mmdebstrap \
                   git \
                   glslang-tools \
                   libdrm-dev \
                   libegl1-mesa-dev \
                   libxext-dev \
                   libfontconfig-dev \
                   libgbm-dev \
                   libgl-dev \
                   libgles2-mesa-dev \
                   libglu1-mesa-dev \
                   libglx-dev \
                   libpng-dev \
                   libssl-dev \
                   libudev-dev \
                   libvulkan-dev \
                   libwaffle-dev \
                   libwayland-dev \
                   libx11-xcb-dev \
                   libxcb-dri2-0-dev \
                   libxkbcommon-dev \
                   libwayland-dev \
                   ninja-build \
                   openssh-server \
                   patch \
                   protobuf-compiler \
                   python-is-python3 \
                   python3-distutils \
                   python3-mako \
                   python3-numpy \
                   python3-serial \
                   unzip \
                   zstd


if [[ "$DEBIAN_ARCH" = "armhf" ]]; then
    apt-get install -y --no-remove \
                       libegl1-mesa-dev:armhf \
                       libelf-dev:armhf \
                       libgbm-dev:armhf \
                       libgles2-mesa-dev:armhf \
                       libpng-dev:armhf \
                       libudev-dev:armhf \
                       libvulkan-dev:armhf \
                       libwaffle-dev:armhf \
                       libwayland-dev:armhf \
                       libx11-xcb-dev:armhf \
                       libxkbcommon-dev:armhf
fi

ROOTFS=/lava-files/rootfs-${DEBIAN_ARCH}
mkdir -p "$ROOTFS"

# rootfs packages
PKG_BASE=(
  tzdata mount
)
PKG_CI=(
  firmware-realtek
  bash ca-certificates curl
  initramfs-tools jq netcat-openbsd dropbear openssh-server
  libasan8
  git
  python3-dev python3-pip python3-setuptools python3-wheel
  weston # Wayland
  xinit xserver-xorg-core xwayland # X11
)
PKG_MESA_DEP=(
  libdrm2 libsensors5 libexpat1 # common
  libvulkan1 # vulkan
  libx11-6 libx11-xcb1 libxcb-dri2-0 libxcb-dri3-0 libxcb-glx0 libxcb-present0 libxcb-randr0 libxcb-shm0 libxcb-sync1 libxcb-xfixes0 libxdamage1 libxext6 libxfixes3 libxkbcommon0 libxrender1 libxshmfence1 libxxf86vm1 # X11
)
PKG_DEP=(
  libpng16-16
  libwaffle-1-0
  libpython3.11 python3 python3-lxml python3-mako python3-numpy python3-packaging python3-pil python3-renderdoc python3-requests python3-simplejson python3-yaml # Python
  sntp
  strace
  waffle-utils
  zstd
)
# arch dependent rootfs packages
[ "$DEBIAN_ARCH" = "arm64" ] && PKG_ARCH=(
  libgl1 libglu1-mesa
  libvulkan-dev
  firmware-linux-nonfree firmware-qcom-media
  libfontconfig1
)
[ "$DEBIAN_ARCH" = "amd64" ] && PKG_ARCH=(
  firmware-amd-graphics
  libgl1 libglu1-mesa
  inetutils-syslogd iptables libcap2
  libfontconfig1
  spirv-tools
  libelf1 libfdt1 "libllvm${LLVM_VERSION}"
  libva2 libva-drm2
  libvulkan-dev
  socat
  sysvinit-core
  wine
)
[ "$DEBIAN_ARCH" = "armhf" ] && PKG_ARCH=(
  firmware-misc-nonfree
)

mmdebstrap \
    --variant=apt \
    --arch="${DEBIAN_ARCH}" \
    --components main,contrib,non-free-firmware \
    --include "${PKG_BASE[*]} ${PKG_CI[*]} ${PKG_DEP[*]} ${PKG_MESA_DEP[*]} ${PKG_ARCH[*]}" \
    bookworm \
    "$ROOTFS/" \
    "http://deb.debian.org/debian"

############### Setuping
if [ "$DEBIAN_ARCH" = "amd64" ]; then
  . .gitlab-ci/container/setup-wine.sh "/dxvk-wine64"
  . .gitlab-ci/container/install-wine-dxvk.sh
  mv /dxvk-wine64 $ROOTFS
fi

############### Installing
if [ "$DEBIAN_ARCH" = "amd64" ]; then
  . .gitlab-ci/container/install-wine-apitrace.sh
  mkdir -p "$ROOTFS/apitrace-msvc-win64"
  mv /apitrace-msvc-win64/bin "$ROOTFS/apitrace-msvc-win64"
  rm -rf /apitrace-msvc-win64
fi

############### Building
STRIP_CMD="${GCC_ARCH}-strip"
mkdir -p $ROOTFS/usr/lib/$GCC_ARCH

############### Build Vulkan validation layer (for zink)
if [ "$DEBIAN_ARCH" = "amd64" ]; then
  . .gitlab-ci/container/build-vulkan-validation.sh
  mv /usr/lib/x86_64-linux-gnu/libVkLayer_khronos_validation.so $ROOTFS/usr/lib/x86_64-linux-gnu/
  mkdir -p $ROOTFS/usr/share/vulkan/explicit_layer.d
  mv /usr/share/vulkan/explicit_layer.d/* $ROOTFS/usr/share/vulkan/explicit_layer.d/
fi

############### Build apitrace
. .gitlab-ci/container/build-apitrace.sh
mkdir -p $ROOTFS/apitrace
mv /apitrace/build $ROOTFS/apitrace
rm -rf /apitrace

############### Build ANGLE
if [[ "$DEBIAN_ARCH" = "amd64" ]]; then
  . .gitlab-ci/container/build-angle.sh
  mv /angle /lava-files/rootfs-${DEBIAN_ARCH}/.
  rm -rf /angle
fi

############### Build dEQP runner
. .gitlab-ci/container/build-deqp-runner.sh
mkdir -p $ROOTFS/usr/bin
mv /usr/local/bin/*-runner $ROOTFS/usr/bin/.


############### Build dEQP
DEQP_TARGET=surfaceless . .gitlab-ci/container/build-deqp.sh

mv /deqp $ROOTFS/.


############### Build SKQP
if [[ "$DEBIAN_ARCH" = "arm64" ]] \
  || [[ "$DEBIAN_ARCH" = "amd64" ]]; then
    . .gitlab-ci/container/build-skqp.sh
    mv /skqp $ROOTFS/.
fi

############### Build piglit
PIGLIT_OPTS="-DPIGLIT_BUILD_DMA_BUF_TESTS=ON -DPIGLIT_BUILD_GLX_TESTS=ON" . .gitlab-ci/container/build-piglit.sh
mv /piglit $ROOTFS/.

############### Build libva tests
if [[ "$DEBIAN_ARCH" = "amd64" ]]; then
    . .gitlab-ci/container/build-va-tools.sh
    mv /va/bin/* $ROOTFS/usr/bin/
fi

############### Build Crosvm
if [[ ${DEBIAN_ARCH} = "amd64" ]]; then
    . .gitlab-ci/container/build-crosvm.sh
    mv /usr/local/bin/crosvm $ROOTFS/usr/bin/
    mv /usr/local/lib/libvirglrenderer.* $ROOTFS/usr/lib/$GCC_ARCH/
    mkdir -p $ROOTFS/usr/local/libexec/
    mv /usr/local/libexec/virgl* $ROOTFS/usr/local/libexec/
fi

############### Build local stuff for use by igt and kernel testing, which
############### will reuse most of our container build process from a specific
############### hash of the Mesa tree.
if [[ -e ".gitlab-ci/local/build-rootfs.sh" ]]; then
    . .gitlab-ci/local/build-rootfs.sh
fi


############### Build kernel
. .gitlab-ci/container/build-kernel.sh

############### Delete rust, since the tests won't be compiling anything.
rm -rf /root/.cargo
rm -rf /root/.rustup

############### Fill rootfs
cp .gitlab-ci/container/setup-rootfs.sh $ROOTFS/.
cp .gitlab-ci/container/strip-rootfs.sh $ROOTFS/.
cp .gitlab-ci/container/debian/llvm-snapshot.gpg.key $ROOTFS/.
cp .gitlab-ci/container/debian/winehq.gpg.key $ROOTFS/.
chroot $ROOTFS bash /setup-rootfs.sh
rm $ROOTFS/{llvm-snapshot,winehq}.gpg.key
rm "$ROOTFS/setup-rootfs.sh"
rm "$ROOTFS/strip-rootfs.sh"
cp /etc/wgetrc $ROOTFS/etc/.

if [ "${DEBIAN_ARCH}" = "arm64" ]; then
    mkdir -p /lava-files/rootfs-arm64/lib/firmware/qcom/sm8350/  # for firmware imported later
    # Make a gzipped copy of the Image for db410c.
    gzip -k /lava-files/Image
    KERNEL_IMAGE_NAME+=" Image.gz"
fi

du -ah "$ROOTFS" | sort -h | tail -100
pushd $ROOTFS
  tar --zstd -cf /lava-files/lava-rootfs.tar.zst .
popd

. .gitlab-ci/container/container_post_build.sh

############### Upload the files!
FILES_TO_UPLOAD="lava-rootfs.tar.zst \
                 $KERNEL_IMAGE_NAME"

if [[ -n $DEVICE_TREES ]]; then
    FILES_TO_UPLOAD="$FILES_TO_UPLOAD $(basename -a $DEVICE_TREES)"
fi

for f in $FILES_TO_UPLOAD; do
    ci-fairy s3cp --token-file "${CI_JOB_JWT_FILE}" /lava-files/$f \
             https://${S3_PATH}/$f
done

touch /lava-files/done
ci-fairy s3cp --token-file "${CI_JOB_JWT_FILE}" /lava-files/done https://${S3_PATH}/done
