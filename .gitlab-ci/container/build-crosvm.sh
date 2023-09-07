#!/usr/bin/env bash
# shellcheck disable=SC2086 # we want word splitting

set -ex

git config --global user.email "mesa@example.com"
git config --global user.name "Mesa CI"

CROSVM_VERSION=d0cbf0b23eb4bd2355b011184025c7c5d8749376
git clone --single-branch -b main --no-checkout https://chromium.googlesource.com/crosvm/crosvm /platform/crosvm
pushd /platform/crosvm
git checkout "$CROSVM_VERSION"
git submodule update --init

VIRGLRENDERER_VERSION=45bb2449b81336b88c267b1c1735f3b4946c7b3a
rm -rf third_party/virglrenderer
git clone --single-branch -b main --no-checkout https://gitlab.freedesktop.org/virgl/virglrenderer.git third_party/virglrenderer
pushd third_party/virglrenderer
git checkout "$VIRGLRENDERER_VERSION"
meson setup build/ -D libdir=lib -D render-server-worker=process -D venus=true $EXTRA_MESON_ARGS
meson install -C build
popd

cargo update -p pkg-config@0.3.26 --precise 0.3.27

RUSTFLAGS='-L native=/usr/local/lib' cargo install \
  bindgen-cli \
  --locked \
  -j ${FDO_CI_CONCURRENT:-4} \
  --root /usr/local \
  --version 0.65.1 \
  $EXTRA_CARGO_ARGS

CROSVM_USE_SYSTEM_VIRGLRENDERER=1 RUSTFLAGS='-L native=/usr/local/lib' cargo install \
  -j ${FDO_CI_CONCURRENT:-4} \
  --locked \
  --features 'default-no-sandbox gpu x virgl_renderer virgl_renderer_next' \
  --path . \
  --root /usr/local \
  $EXTRA_CARGO_ARGS

popd

rm -rf /platform/crosvm
