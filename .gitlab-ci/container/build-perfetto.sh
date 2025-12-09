#!/usr/bin/env bash

set -uex

section_start perfetto "Building perfetto"

BASE_PWD=$PWD

PERFETTO_REVISION=$(grep 'revision =' subprojects/perfetto.wrap | cut -d ' ' -f3)

patch_files=(
  "build-perfetto-Fix-C-standard-library-build-errors-with-Debian-13.patch"
)

# Set PERFETTO_ARCH based on DEBIAN_ARCH
if [[ -z "${PERFETTO_ARCH:-}" ]]; then
  case "$DEBIAN_ARCH" in
    amd64) PERFETTO_ARCH=x64;;
    arm64) PERFETTO_ARCH=arm64;;
  esac
fi

git clone --branch "$PERFETTO_REVISION" --depth 1 https://github.com/google/perfetto /perfetto

pushd /perfetto

for patch in "${patch_files[@]}"; do
  echo "Applying patch: $patch"
  git am "$BASE_PWD/.gitlab-ci/container/patches/$patch"
done

# Base GN args
mkdir -p _build
cat >_build/args.gn <<EOF
is_debug=false
target_cpu="${PERFETTO_ARCH}"
target_os="${PERFETTO_TARGET}"
EOF

case "$PERFETTO_TARGET" in
linux)
        # Override Perfetto’s default toolchain selection here, as the bundled
        # arm64 toolchain is an x86-64 -> arm64 cross-compiler.
        cat >>_build/args.gn <<EOF
is_system_compiler = true
is_hermetic_clang = false
ar = "ar"
cc = "clang-${LLVM_VERSION}"
cxx = "clang++-${LLVM_VERSION}"
extra_ldflags = "-fuse-ld=lld-${LLVM_VERSION} -lpthread -ldl"
EOF
        ./tools/install-build-deps
        ;;
android)
        # No additional args needed when cross-building for Android
        ./tools/install-build-deps --android
        ;;
*)
        echo "Unexpected PERFETTO_TARGET value: $PERFETTO_TARGET"
        exit 1
        ;;
esac

./tools/gn gen _build/
./tools/ninja -C _build/ tracebox

mkdir -p build
cp _build/tracebox build/

"${STRIP_CMD:-strip}" build/tracebox || true

# Cleanup everything except build/
find . -mindepth 1 -maxdepth 1 ! -name build -exec rm -rf {} +

popd

section_end perfetto
