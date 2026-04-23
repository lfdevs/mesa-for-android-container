#!/usr/bin/env bash
# shellcheck disable=SC2086 # we want word splitting

# When changing this file, you need to bump the following
# .gitlab-ci/image-tags.yml tags:
# DEBIAN_TEST_BASE_TAG

set -uex

section_start gpu-trace-perf "Building gpu-trace-perf"

GPU_TRACE_PERF_VERSION=1.8.2

commits_to_backport=(
)

patch_files=(
)

GPU_TRACE_PERF_GIT_URL="${GPU_TRACE_PERF_GIT_URL:-https://gitlab.freedesktop.org/anholt/gpu-trace-perf.git}"
if [ -n "${GPU_TRACE_PERF_GIT_TAG:-}" ]; then
    GPU_TRACE_PERF_GIT_CHECKOUT="$GPU_TRACE_PERF_GIT_TAG"
elif [ -n "${GPU_TRACE_PERF_GIT_REV:-}" ]; then
    GPU_TRACE_PERF_GIT_CHECKOUT="$GPU_TRACE_PERF_GIT_REV"
else
    GPU_TRACE_PERF_GIT_CHECKOUT="v$GPU_TRACE_PERF_VERSION"
fi

BASE_PWD=$PWD

mkdir -p /gpu-trace-perf
pushd /gpu-trace-perf
mkdir gpu-trace-perf-git
pushd gpu-trace-perf-git
git init
git remote add origin "$GPU_TRACE_PERF_GIT_URL"
git fetch --depth 1 origin "$GPU_TRACE_PERF_GIT_CHECKOUT"
git checkout FETCH_HEAD

for commit in "${commits_to_backport[@]}"
do
  PATCH_URL="https://gitlab.freedesktop.org/anholt/gpu-trace-perf/-/commit/$commit.patch"
  echo "Backport gpu-trace-perf commit $commit from $PATCH_URL"
  curl -L --retry 4 -f --retry-all-errors --retry-delay 60 $PATCH_URL | git am
done

for patch in "${patch_files[@]}"
do
  echo "Apply patch to gpu-trace-perf from $patch"
  git am "$BASE_PWD/.gitlab-ci/container/patches/$patch"
done

if [ -z "${RUST_TARGET:-}" ]; then
    RUST_TARGET=""
fi

if [[ "$RUST_TARGET" != *-android ]]; then
    # When CC (/usr/lib/ccache/gcc) variable is set, the rust compiler uses
    # this variable when cross-compiling arm32 and build fails for zsys-sys.
    # So unset the CC variable when cross-compiling for arm32.
    SAVEDCC=${CC:-}
    if [ "$RUST_TARGET" = "armv7-unknown-linux-gnueabihf" ]; then
        unset CC
    fi
    cargo install --locked  \
        -j ${FDO_CI_CONCURRENT:-4} \
        --root /usr/local \
        ${EXTRA_CARGO_ARGS:-} \
        --path .
    CC=$SAVEDCC
else
    cargo install --locked  \
        -j ${FDO_CI_CONCURRENT:-4} \
        --root /usr/local --version 2.10.0 \
        cargo-ndk

    rustup target add $RUST_TARGET
    RUSTFLAGS='-C target-feature=+crt-static' cargo ndk --target $RUST_TARGET build --release

    mv target/$RUST_TARGET/release/gpu-trace-perf /gpu-trace-perf

    cargo uninstall --locked  \
        --root /usr/local \
        cargo-ndk
fi

popd
rm -rf gpu-trace-perf-git
popd

section_end gpu-trace-perf
