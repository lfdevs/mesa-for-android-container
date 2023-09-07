#!/bin/bash
# shellcheck disable=SC2086 # we want word splitting

set -ex

REV="5036601c43fff63f7be5cd8ad7b319a5c1f6652c"

git clone https://gitlab.freedesktop.org/mesa/piglit.git --single-branch --no-checkout /piglit
pushd /piglit
git checkout "$REV"
patch -p1 <$OLDPWD/.gitlab-ci/piglit/disable-vs_in.diff
cmake -S . -B . -G Ninja -DCMAKE_BUILD_TYPE=Release $PIGLIT_OPTS $EXTRA_CMAKE_ARGS
ninja $PIGLIT_BUILD_TARGETS
# shellcheck disable=SC2038,SC2185 # TODO: rewrite find
find -name .git -o -name '*ninja*' -o -iname '*cmake*' -o -name '*.[chao]' | xargs rm -rf
rm -rf target_api
if [ "$PIGLIT_BUILD_TARGETS" = "piglit_replayer" ]; then
    # shellcheck disable=SC2038,SC2185 # TODO: rewrite find
    find ! -regex "^\.$" \
         ! -regex "^\.\/piglit.*" \
         ! -regex "^\.\/framework.*" \
         ! -regex "^\.\/bin$" \
         ! -regex "^\.\/bin\/replayer\.py" \
         ! -regex "^\.\/templates.*" \
         ! -regex "^\.\/tests$" \
         ! -regex "^\.\/tests\/replay\.py" 2>/dev/null | xargs rm -rf
fi
popd
