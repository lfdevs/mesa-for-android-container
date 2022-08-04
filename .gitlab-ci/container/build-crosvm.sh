#!/bin/bash

set -ex

SCRIPT_DIR="$(pwd)"

CROSVM_VERSION=c7cd0e0114c8363b884ba56d8e12adee718dcc93
git clone --single-branch -b main --no-checkout https://chromium.googlesource.com/chromiumos/platform/crosvm /platform/crosvm
pushd /platform/crosvm
git checkout "$CROSVM_VERSION"
git submodule update --init
# Apply all crosvm patches for Mesa CI
cat "$SCRIPT_DIR"/.gitlab-ci/container/build-crosvm_*.patch |
    patch -p1

VIRGLRENDERER_VERSION=dd301caf7e05ec9c09634fb7872067542aad89b7
rm -rf third_party/virglrenderer
git clone --single-branch -b master --no-checkout https://gitlab.freedesktop.org/virgl/virglrenderer.git third_party/virglrenderer
pushd third_party/virglrenderer
git checkout "$VIRGLRENDERER_VERSION"
meson build/ $EXTRA_MESON_ARGS
ninja -C build install
popd

RUSTFLAGS='-L native=/usr/local/lib' cargo install \
  bindgen \
  -j ${FDO_CI_CONCURRENT:-4} \
  --root /usr/local \
  $EXTRA_CARGO_ARGS

RUSTFLAGS='-L native=/usr/local/lib' cargo install \
  -j ${FDO_CI_CONCURRENT:-4} \
  --locked \
  --features 'default-no-sandbox gpu x virgl_renderer virgl_renderer_next' \
  --path . \
  --root /usr/local \
  $EXTRA_CARGO_ARGS

popd

rm -rf /platform/crosvm
