FROM debian:testing

ENV DEBIAN_FRONTEND=noninteractive

RUN echo 'path-exclude=/usr/share/doc/*' > /etc/dpkg/dpkg.cfg.d/99-exclude-cruft
RUN echo 'path-exclude=/usr/share/man/*' >> /etc/dpkg/dpkg.cfg.d/99-exclude-cruft
RUN echo '#!/bin/sh' > /usr/sbin/policy-rc.d
RUN echo 'exit 101' >> /usr/sbin/policy-rc.d
RUN chmod +x /usr/sbin/policy-rc.d

RUN dpkg --add-architecture arm64
RUN echo deb-src http://deb.debian.org/debian testing main >> /etc/apt/sources.list
RUN apt-get update && \
    apt-get -y install ca-certificates && \
    apt-get -y install --no-install-recommends \
      crossbuild-essential-arm64 \
      meson \
      g++ \
      git \
      ccache \
      pkg-config \
      python3-mako \
      python-numpy \
      python-six \
      python-mako \
      python3-pip \
      python3-setuptools \
      python3-six \
      python3-wheel \
      python3-jinja2 \
      bison \
      flex \
      libwayland-dev \
      gettext \
      cmake \
      bc \
      libssl-dev \
      lavacli \
      csvkit \
      curl \
      unzip \
      wget \
      debootstrap \
      procps \
      qemu-user-static \
      cpio \
      \
      libdrm-dev:arm64 \
      libx11-dev:arm64 \
      libxxf86vm-dev:arm64 \
      libexpat1-dev:arm64 \
      libsensors-dev:arm64 \
      libxfixes-dev:arm64 \
      libxdamage-dev:arm64 \
      libxext-dev:arm64 \
      x11proto-dev:arm64 \
      libx11-xcb-dev:arm64 \
      libxcb-dri2-0-dev:arm64 \
      libxcb-glx0-dev:arm64 \
      libxcb-xfixes0-dev:arm64 \
      libxcb-dri3-dev:arm64 \
      libxcb-present-dev:arm64 \
      libxcb-randr0-dev:arm64 \
      libxcb-sync-dev:arm64 \
      libxrandr-dev:arm64 \
      libxshmfence-dev:arm64 \
      libelf-dev:arm64 \
      libwayland-dev:arm64 \
      libwayland-egl-backend-dev:arm64 \
      libclang-7-dev:arm64 \
      zlib1g-dev:arm64 \
      libglvnd-core-dev:arm64 \
      wayland-protocols:arm64 \
      libpng-dev:arm64 && \
    rm -rf /var/lib/apt/lists

RUN mkdir -p /artifacts/rootfs/deqp                                             && \
  wget https://github.com/KhronosGroup/VK-GL-CTS/archive/opengl-es-cts-3.2.5.0.zip && \
  unzip opengl-es-cts-3.2.5.0.zip -d /                                          && \
  rm opengl-es-cts-3.2.5.0.zip                                                  && \
  cd /VK-GL-CTS-opengl-es-cts-3.2.5.0                                           && \
  python3 external/fetch_sources.py                                             && \
  cd /artifacts/rootfs/deqp                                                     && \
  cmake -DDEQP_TARGET=wayland                                                      \
    -DCMAKE_BUILD_TYPE=Release                                                     \
    -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc                                       \
    -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++                                     \
    /VK-GL-CTS-opengl-es-cts-3.2.5.0                                            && \
  make -j$(nproc)                                                               && \
  rm -rf /artifacts/rootfs/deqp/external                                        && \
  rm -rf /artifacts/rootfs/deqp/modules/gles3                                   && \
  rm -rf /artifacts/rootfs/deqp/modules/gles31                                  && \
  rm -rf /artifacts/rootfs/deqp/modules/internal                                && \
  rm -rf /artifacts/rootfs/deqp/executor                                        && \
  rm -rf /artifacts/rootfs/deqp/execserver                                      && \
  rm -rf /artifacts/rootfs/deqp/modules/egl                                     && \
  rm -rf /artifacts/rootfs/deqp/framework                                       && \
  find . -name CMakeFiles | xargs rm -rf                                        && \
  find . -name lib\*.a | xargs rm -rf                                           && \
  du -sh *                                                                      && \
  rm -rf /VK-GL-CTS-opengl-es-cts-3.2.5.0

# TODO: Switch to 5.2-rc* when the time comes
COPY arm64.config /panfrost-ci/
RUN mkdir -p /kernel                                                                   && \
  wget https://github.com/freedesktop/drm-misc/archive/drm-misc-next-2019-04-18.tar.gz && \
  tar xvfz drm-misc-next-2019-04-18.tar.gz -C /kernel --strip-components=1             && \
  rm drm-misc-next-2019-04-18.tar.gz                                                   && \
  cd /kernel                                                                           && \
  ARCH=arm64 CROSS_COMPILE="aarch64-linux-gnu-" ./scripts/kconfig/merge_config.sh arch/arm64/configs/defconfig /panfrost-ci/arm64.config && \
  ARCH=arm64 CROSS_COMPILE="aarch64-linux-gnu-" make -j12 Image dtbs                   && \
  cp arch/arm64/boot/Image /artifacts/.                                                && \
  cp arch/arm64/boot/dts/rockchip/rk3399-gru-kevin.dtb /artifacts/.                    && \
  rm -rf /kernel

COPY create-rootfs.sh /artifacts/rootfs/
RUN debootstrap --variant=minbase --arch=arm64 testing /artifacts/rootfs/ http://deb.debian.org/debian && \
    chroot /artifacts/rootfs sh /create-rootfs.sh                                                      && \
    rm /artifacts/rootfs/create-rootfs.sh

ENTRYPOINT [""]