#!/bin/bash

# Note that this script is not actually "building" rust, but build- is the
# convention for the shared helpers for putting stuff in our containers.

set -ex

section_start rust "Building Rust toolchain"

# When changing this file, you need to bump the following
# .gitlab-ci/image-tags.yml tags:
# DEBIAN_BUILD_BASE_TAG
# DEBIAN_TEST_BASE_TAG

# This version number should match what we require in meson.build so we catch
# build issues from patches relying on new features in newer Rust versions.
# Keep this is sync with the `rustc.version()` check in meson.build, and with
# the `rustup default` line in .gitlab-ci/meson/build.sh
MINIMUM_SUPPORTED_RUST_VERSION=1.82.0

# This version number can be bumped freely, to benefit from the latest
# diagnostics in CI `build-only` jobs, and for building external CI
# components.
LATEST_RUST_VERSION=1.90.0

# For rust in Mesa, we use rustup to install.  This lets us pick an arbitrary
# version of the compiler, rather than whatever the container's Debian comes
# with.
curl -L --retry 4 -f --retry-all-errors --retry-delay 60 \
    --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- \
      --default-toolchain $LATEST_RUST_VERSION \
      --profile minimal \
      --component clippy,rustfmt \
      -y

# Make rustup tools available in the PATH environment variable
# shellcheck disable=SC1091
. "$HOME/.cargo/env"

if [ "$1" = "build" ]
then
  rustup toolchain install --profile minimal --component clippy,rustfmt $MINIMUM_SUPPORTED_RUST_VERSION
fi

find "$HOME"/.rustup/toolchains/*/lib -type f -name "*.so" -exec strip {} \;
find "$HOME"/.rustup/toolchains -type f -executable -exec strip {} \;

# Set up a config script for cross compiling -- cargo needs your system cc for
# linking in cross builds, but doesn't know what you want to use for system cc.
cat > "$HOME/.cargo/config" <<EOF
[target.armv7-unknown-linux-gnueabihf]
linker = "arm-linux-gnueabihf-gcc"

[target.aarch64-unknown-linux-gnu]
linker = "aarch64-linux-gnu-gcc"
EOF

section_end rust
