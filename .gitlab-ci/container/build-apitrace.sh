#!/usr/bin/env bash
# shellcheck disable=SC2086 # we want word splitting

# When changing this file, you need to bump the following
# .gitlab-ci/image-tags.yml tags:
# DEBIAN_TEST_BASE_TAG

set -uex

uncollapsed_section_start apitrace "Building apitrace"

APITRACE_VERSION="45a005875d348b055d5b88dca285c109dee90457"

git clone https://github.com/apitrace/apitrace.git --single-branch --no-checkout /apitrace
pushd /apitrace
git checkout "$APITRACE_VERSION"
git submodule update --init --depth 1 --recursive
cmake -S . -B _build -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_GUI=False -DENABLE_WAFFLE=on ${EXTRA_CMAKE_ARGS:-}
cmake --build _build --parallel --target apitrace eglretrace
mkdir build
cp _build/apitrace build
cp _build/eglretrace build
${STRIP_CMD:-strip} build/*
find . -not -path './build' -not -path './build/*' -delete
popd

section_end apitrace
