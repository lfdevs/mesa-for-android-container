#!/bin/bash

set -ex

CROSVM_VERSION=d2b6a64dd31c92a284a905c0f2483d0b222b1220
git clone --single-branch -b for-mesa-ci --no-checkout https://gitlab.freedesktop.org/tomeu/crosvm.git /platform/crosvm
pushd /platform/crosvm
git checkout "$CROSVM_VERSION"
git submodule update --init

VIRGLRENDERER_VERSION=e420a5aab92de8fb42fad50762f0ac3b5fcb3bfb
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

rm -rf $PLATFORM2_ROOT $AOSP_EXTERNAL_ROOT/minijail $THIRD_PARTY_ROOT/adhd $THIRD_PARTY_ROOT/rust-vmm /platform/crosvm
