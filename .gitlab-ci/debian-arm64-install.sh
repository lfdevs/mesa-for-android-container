#!/bin/bash

set -e
set -o xtrace

############### Install packages for building
apt-get -y install ca-certificates
sed -i -e 's/http:\/\/deb/https:\/\/deb/g' /etc/apt/sources.list
echo 'deb https://deb.debian.org/debian buster-backports main' >/etc/apt/sources.list.d/backports.list
dpkg --add-architecture armhf
apt-get update
apt-get -y install \
	bc \
	bison \
	bzip2 \
	ccache \
	cmake \
	crossbuild-essential-armhf \
	curl \
	flex \
	g++ \
	gettext \
	git \
	libdrm-dev \
	libdrm-dev:armhf \
	libelf-dev \
	libelf-dev:armhf \
	libexpat1-dev \
	libexpat1-dev:armhf \
	libgbm-dev \
	libgles2-mesa-dev \
	libpng-dev \
	libssl-dev \
	llvm-7-dev:armhf \
	llvm-8-dev \
	meson \
	ninja-build \
	pkg-config \
	procps \
	python \
	python3-mako \
	wget \
	zlib1g-dev

############### Generate cross build file for Meson

cross_file="/cross_file-armhf.txt"
/usr/share/meson/debcrossgen --arch armhf -o "$cross_file"
# Explicitly set ccache path for cross compilers
sed -i "s|/usr/bin/\([^-]*\)-linux-gnu\([^-]*\)-g|/usr/lib/ccache/\\1-linux-gnu\\2-g|g" "$cross_file"
# Don't need wrapper for armhf executables
sed -i -e '/\[properties\]/a\' -e "needs_exe_wrapper = False" "$cross_file"

export             LIBDRM_VERSION=libdrm-2.4.99

############### Build libdrm

wget https://dri.freedesktop.org/libdrm/$LIBDRM_VERSION.tar.bz2
tar -xvf $LIBDRM_VERSION.tar.bz2 && rm $LIBDRM_VERSION.tar.bz2
cd $LIBDRM_VERSION; meson build/ -Detnaviv=true; ninja -C build/ install; cd ..
rm -rf $LIBDRM_VERSION

############### Build dEQP

git config --global user.email "mesa@example.com"
git config --global user.name "Mesa CI"
# XXX: Use --depth 1 once we can drop the cherry-picks.
git clone \
    https://github.com/KhronosGroup/VK-GL-CTS.git \
    -b opengl-es-cts-3.2.5.1 \
    /VK-GL-CTS
cd /VK-GL-CTS
# Fix surfaceless build
git cherry-pick -x 22f41e5e321c6dcd8569c4dad91bce89f06b3670
git cherry-pick -x 1daa8dff73161ea60ead965bd6c9f2a0a2165648

# surfaceless links against libkms and such despite not using it.
sed -i '/gbm/d' targets/surfaceless/surfaceless.cmake
sed -i '/libkms/d' targets/surfaceless/surfaceless.cmake
sed -i '/libgbm/d' targets/surfaceless/surfaceless.cmake

# --insecure is due to SSL cert failures hitting sourceforge for zlib and
# libpng (sigh).  The archives get their checksums checked anyway, and git
# always goes through ssh or https.
python3 external/fetch_sources.py --insecure

mkdir -p /deqp
cd /deqp
cmake -G Ninja \
      -DDEQP_TARGET=surfaceless               \
      -DCMAKE_BUILD_TYPE=Release              \
      /VK-GL-CTS
ninja

# Copy out the mustpass lists we want from a bunch of other junk.
mkdir /deqp/mustpass
for gles in gles2 gles3 gles31; do
    cp \
        /deqp/external/openglcts/modules/gl_cts/data/mustpass/gles/aosp_mustpass/3.2.5.x/$gles-master.txt \
        /deqp/mustpass/$gles-master.txt
done

rm -rf /deqp/external
rm -rf /deqp/modules/internal
rm -rf /deqp/executor
rm -rf /deqp/execserver
rm -rf /deqp/modules/egl
rm -rf /deqp/framework
du -sh *
rm -rf /VK-GL-CTS

############### Uninstall the build software

apt-get purge -y \
        cmake \
        git \
        libgbm-dev \
        libgles2-mesa-dev \
        wget

apt-get autoremove -y --purge
