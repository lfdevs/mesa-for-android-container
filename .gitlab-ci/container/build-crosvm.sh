#!/bin/bash
# shellcheck disable=SC2086 # we want word splitting

set -ex

git config --global user.email "mesa@example.com"
git config --global user.name "Mesa CI"

CROSVM_VERSION=00af43e1b565e1ae0047ba84b970da5e089e4f48
git clone --single-branch -b main --no-checkout https://chromium.googlesource.com/crosvm/crosvm /platform/crosvm
pushd /platform/crosvm
git checkout "$CROSVM_VERSION"
git submodule update --init

VIRGLRENDERER_VERSION=fc2ad36998f8af8ea3cc68fb9c747dfec9cb4635
rm -rf third_party/virglrenderer
git clone --single-branch -b master --no-checkout https://gitlab.freedesktop.org/virgl/virglrenderer.git third_party/virglrenderer
pushd third_party/virglrenderer
git checkout "$VIRGLRENDERER_VERSION"
meson build/ -Drender-server-worker=process -Dvenus-experimental=true $EXTRA_MESON_ARGS
ninja -C build install
popd

RUSTFLAGS='-L native=/usr/local/lib' cargo install \
  bindgen-cli \
  -j ${FDO_CI_CONCURRENT:-4} \
  --root /usr/local \
  --version 0.63.0 \
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
