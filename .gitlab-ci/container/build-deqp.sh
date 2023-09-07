#!/usr/bin/env bash
# shellcheck disable=SC2086 # we want word splitting

# When changing this file, you need to bump the following
# .gitlab-ci/image-tags.yml tags:
# DEBIAN_X86_64_TEST_ANDROID_TAG
# DEBIAN_X86_64_TEST_GL_TAG
# DEBIAN_X86_64_TEST_VK_TAG
# KERNEL_ROOTFS_TAG

set -ex

git config --global user.email "mesa@example.com"
git config --global user.name "Mesa CI"
git clone \
    https://github.com/KhronosGroup/VK-GL-CTS.git \
    -b vulkan-cts-1.3.5.2 \
    --depth 1 \
    /VK-GL-CTS
pushd /VK-GL-CTS

# Patches to VulkanCTS may come from commits in their repo (listed in
# cts_commits_to_backport) or patch files stored in our repo (in the patch
# directory `$OLDPWD/.gitlab-ci/container/patches/` listed in cts_patch_files).
# Both list variables would have comments explaining the reasons behind the
# patches.

cts_commits_to_backport=(
        # sync fix for SSBO writes
        44f1be32fe6bd2a7de7b9169fc71cc44e0b26124

        # sync fix for KHR-GL46.multi_bind.dispatch_bind_image_textures
        db6c9e295ab38054ace425cb75ff966719ccc609

        # VK robustness barriers fix
        6052f21c4d6077438d644f525c10cc58dcdf25bf

        # correctness fixes for zink validation fails
        1923cbc89ed3969a3afe7c6926124b51157902e1
        af3a979c49dc65f8809c27660405ae3a76c7da4a

        # GL/GLES vertex_attrib_binding.advanced-largeStrideAndOffsetsNewAndLegacyAPI fix
        bdb456dcf85e34fced872ebdaf06f6b73451f99c

        # KHR-GLES31.core.compute_shader.max fix
        7aa3ebb49d07982f5c44edd4799edb5a894567e9

        # GL arrays_of_arrays perf fix
        b481dada59734e8e34050fe884ba6d627d9e5c54

        # GL shadow samplers require depth compares fix
        a8bc242ec234bf8d7df8b4eec1eeccab4e401288

        # GL PolygonOffsetClamp fix
        1f2feb2388da88b4e46eba55547d50856467cc20

        # KHR-GL46.texture_view.view_sampling fix
        aca29fb9553ebe28094513ce18bb46bad138cf46

        # video validation fails
        4cc3980a86ba5b7fe6e76b559cc1a9cb5fd1b253
        a7a2ce442db51ca058ce051de7e09d62db44ae81

        # Check for robustness before testing it
        ee7138d8adf5ed3c4845e5ac2553c4f9697be9d8

        # dEQP-VK.wsi.acquire_drm_display.*invalid_fd
        98ad9402e7d94030d1689fd59135da7a2f52384c
)

for commit in "${cts_commits_to_backport[@]}"
do
  PATCH_URL="https://github.com/KhronosGroup/VK-GL-CTS/commit/$commit.patch"
  echo "Apply patch to VK-GL-CTS from $PATCH_URL"
  curl -L --retry 4 -f --retry-all-errors --retry-delay 60 $PATCH_URL | \
    git am -
done

cts_patch_files=(
  # Android specific patches.
  build-deqp_Allow-running-on-Android-from-the-command-line.patch
  build-deqp_Android-prints-to-stdout-instead-of-logcat.patch
)

for patch in "${cts_patch_files[@]}"
do
  echo "Apply patch to VK-GL-CTS from $patch"
  git am < $OLDPWD/.gitlab-ci/container/patches/$patch
done

# --insecure is due to SSL cert failures hitting sourceforge for zlib and
# libpng (sigh).  The archives get their checksums checked anyway, and git
# always goes through ssh or https.
python3 external/fetch_sources.py --insecure

mkdir -p /deqp

# Save the testlog stylesheets:
cp doc/testlog-stylesheet/testlog.{css,xsl} /deqp
popd

pushd /deqp

if [ "${DEQP_TARGET}" != 'android' ]; then
    # When including EGL/X11 testing, do that build first and save off its
    # deqp-egl binary.
    cmake -S /VK-GL-CTS -B . -G Ninja \
        -DDEQP_TARGET=x11_egl_glx \
        -DCMAKE_BUILD_TYPE=Release \
        $EXTRA_CMAKE_ARGS
    ninja modules/egl/deqp-egl
    mv /deqp/modules/egl/deqp-egl /deqp/modules/egl/deqp-egl-x11

    cmake -S /VK-GL-CTS -B . -G Ninja \
        -DDEQP_TARGET=wayland \
        -DCMAKE_BUILD_TYPE=Release \
        $EXTRA_CMAKE_ARGS
    ninja modules/egl/deqp-egl
    mv /deqp/modules/egl/deqp-egl /deqp/modules/egl/deqp-egl-wayland
fi

cmake -S /VK-GL-CTS -B . -G Ninja \
      -DDEQP_TARGET=${DEQP_TARGET:-x11_glx} \
      -DCMAKE_BUILD_TYPE=Release \
      $EXTRA_CMAKE_ARGS
ninja

if [ "${DEQP_TARGET}" = 'android' ]; then
    mv /deqp/modules/egl/deqp-egl /deqp/modules/egl/deqp-egl-android
fi

# Copy out the mustpass lists we want.
mkdir /deqp/mustpass
for mustpass in $(< /VK-GL-CTS/external/vulkancts/mustpass/main/vk-default.txt) ; do
    cat /VK-GL-CTS/external/vulkancts/mustpass/main/$mustpass \
        >> /deqp/mustpass/vk-master.txt
done

if [ "${DEQP_TARGET}" != 'android' ]; then
    cp \
        /deqp/external/openglcts/modules/gl_cts/data/mustpass/gles/aosp_mustpass/3.2.6.x/*.txt \
        /deqp/mustpass/.
    cp \
        /deqp/external/openglcts/modules/gl_cts/data/mustpass/egl/aosp_mustpass/3.2.6.x/egl-master.txt \
        /deqp/mustpass/.
    cp \
        /deqp/external/openglcts/modules/gl_cts/data/mustpass/gles/khronos_mustpass/3.2.6.x/*-master.txt \
        /deqp/mustpass/.
    cp \
        /deqp/external/openglcts/modules/gl_cts/data/mustpass/gl/khronos_mustpass/4.6.1.x/*-master.txt \
        /deqp/mustpass/.
    cp \
        /deqp/external/openglcts/modules/gl_cts/data/mustpass/gl/khronos_mustpass_single/4.6.1.x/*-single.txt \
        /deqp/mustpass/.

    # Save *some* executor utils, but otherwise strip things down
    # to reduct deqp build size:
    mkdir /deqp/executor.save
    cp /deqp/executor/testlog-to-* /deqp/executor.save
    rm -rf /deqp/executor
    mv /deqp/executor.save /deqp/executor
fi

# Remove other mustpass files, since we saved off the ones we wanted to conventient locations above.
rm -rf /deqp/external/openglcts/modules/gl_cts/data/mustpass
rm -rf /deqp/external/vulkancts/modules/vulkan/vk-master*
rm -rf /deqp/external/vulkancts/modules/vulkan/vk-default

rm -rf /deqp/external/openglcts/modules/cts-runner
rm -rf /deqp/modules/internal
rm -rf /deqp/execserver
rm -rf /deqp/framework
# shellcheck disable=SC2038,SC2185 # TODO: rewrite find
find -iname '*cmake*' -o -name '*ninja*' -o -name '*.o' -o -name '*.a' | xargs rm -rf
${STRIP_CMD:-strip} external/vulkancts/modules/vulkan/deqp-vk
${STRIP_CMD:-strip} external/openglcts/modules/glcts
${STRIP_CMD:-strip} modules/*/deqp-*
du -sh ./*
rm -rf /VK-GL-CTS
popd
