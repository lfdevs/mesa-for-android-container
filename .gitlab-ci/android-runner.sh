#!/usr/bin/env bash
# shellcheck disable=SC2086 # we want word splitting
# shellcheck disable=SC1091 # paths only become valid at runtime

set -uex

# Set default ADB command if not set already

: "${ADB:=adb}"

$ADB wait-for-device root
sleep 1

# overlay 

REMOUNT_PATHS="/vendor"
if [ "$ANDROID_VERSION" -ge 15 ]; then
  REMOUNT_PATHS="$REMOUNT_PATHS /system"
fi

OV_TMPFS="/data/overlay-remount"
$ADB shell mkdir -p "$OV_TMPFS"
$ADB shell mount -t tmpfs none "$OV_TMPFS"

for path in $REMOUNT_PATHS; do
  $ADB shell mkdir -p "${OV_TMPFS}${path}-upper"
  $ADB shell mkdir -p "${OV_TMPFS}${path}-work"

  opts="lowerdir=${path},upperdir=${OV_TMPFS}${path}-upper,workdir=${OV_TMPFS}${path}-work"
  $ADB shell mount -t overlay -o "$opts" none ${path}
done

$ADB shell setenforce 0

$ADB push /android-tools/eglinfo /data
$ADB push /android-tools/vulkaninfo /data

get_gles_runtime_renderer() {
  while [ "$($ADB shell XDG_CACHE_HOME=/data/local/tmp /data/eglinfo | grep 'OpenGL ES profile renderer':)" = "" ] ; do sleep 1; done
  $ADB shell XDG_CACHE_HOME=/data/local/tmp /data/eglinfo | grep 'OpenGL ES profile renderer' | head -1
}

get_gles_runtime_version() {
  while [ "$($ADB shell XDG_CACHE_HOME=/data/local/tmp /data/eglinfo | grep 'OpenGL ES profile version:')" = "" ] ; do sleep 1; done
  $ADB shell XDG_CACHE_HOME=/data/local/tmp /data/eglinfo | grep 'OpenGL ES profile version:' | head -1
}

get_vk_runtime_device_name() {
  $ADB shell XDG_CACHE_HOME=/data/local/tmp /data/vulkaninfo | grep deviceName | head -1
}

get_vk_runtime_version() {
  $ADB shell XDG_CACHE_HOME=/data/local/tmp /data/vulkaninfo | grep driverInfo | head -1
}

# Check what GLES & VK implementation is used before uploading the new libraries
get_gles_runtime_renderer
get_gles_runtime_version
get_vk_runtime_device_name
get_vk_runtime_version

# replace libraries

$ADB shell rm -f /vendor/lib64/libgallium_dri.so*
$ADB shell rm -f /vendor/lib64/egl/libEGL_mesa.so*
$ADB shell rm -f /vendor/lib64/egl/libGLESv1_CM_mesa.so*
$ADB shell rm -f /vendor/lib64/egl/libGLESv2_mesa.so*

$ADB push "$INSTALL/lib/libgallium_dri.so" /vendor/lib64/libgallium_dri.so
$ADB push "$INSTALL/lib/libEGL.so" /vendor/lib64/egl/libEGL_mesa.so
$ADB push "$INSTALL/lib/libGLESv1_CM.so" /vendor/lib64/egl/libGLESv1_CM_mesa.so
$ADB push "$INSTALL/lib/libGLESv2.so" /vendor/lib64/egl/libGLESv2_mesa.so

# Remove and replace Vulkan drivers:
# - For Cuttlefish virtual machines, replace lavapipe or venus
# - For Android hardware, replace the Vulkan driver specified by VK_DRIVER
if [ -n "${CUTTLEFISH_GPU_MODE:-}" ]; then
  if [ "$CUTTLEFISH_GPU_MODE" = "mesa_swrast" ] || [ "$CUTTLEFISH_GPU_MODE" = "mesa_swrast_guest_angle" ]; then
    $ADB shell rm -f /vendor/lib64/hw/vulkan.lvp.so*
    $ADB push "$INSTALL/lib/libvulkan_lvp.so" /vendor/lib64/hw/vulkan.lvp.so
  else
    $ADB shell rm -f /vendor/lib64/hw/vulkan.virtio.so*
    $ADB push "$INSTALL/lib/libvulkan_virtio.so" /vendor/lib64/hw/vulkan.virtio.so
  fi
else
  $ADB shell rm -f /vendor/lib64/hw/vulkan.${VK_DRIVER}.so*
  $ADB push "$INSTALL/lib/libvulkan_${VK_DRIVER}.so" /vendor/lib64/hw/vulkan.${VK_DRIVER}.so
fi

$ADB shell rm -f /vendor/lib64/egl/libEGL_emulation.so*
$ADB shell rm -f /vendor/lib64/egl/libGLESv1_CM_emulation.so*
$ADB shell rm -f /vendor/lib64/egl/libGLESv2_emulation.so*

if [ -n "${ANGLE_TAG:-}" ]; then
  ANGLE_DEST_PATH=/vendor/lib64/egl
  if [ "$ANDROID_VERSION" -ge 15 ]; then
    ANGLE_DEST_PATH=/system/lib64
  fi

  $ADB shell rm -f "$ANGLE_DEST_PATH/libEGL_angle.so"*
  $ADB shell rm -f "$ANGLE_DEST_PATH/libGLESv1_CM_angle.so"*
  $ADB shell rm -f "$ANGLE_DEST_PATH/libGLESv2_angle.so"*

  $ADB push /angle/libEGL_angle.so "$ANGLE_DEST_PATH/libEGL_angle.so"
  $ADB push /angle/libGLESv1_CM_angle.so "$ANGLE_DEST_PATH/libGLESv1_CM_angle.so"
  $ADB push /angle/libGLESv2_angle.so "$ANGLE_DEST_PATH/libGLESv2_angle.so"
fi

# Check what GLES & VK implementation is used after uploading the new libraries
MESA_BUILD_VERSION=$(cat "$INSTALL/VERSION")
get_gles_runtime_renderer
GLES_RUNTIME_VERSION="$(get_gles_runtime_version)"
get_vk_runtime_device_name
VK_RUNTIME_VERSION="$(get_vk_runtime_version)"

if [ -n "${ANGLE_TAG:-}" ]; then
  # Note: we are injecting the ANGLE libs too, so we need to check if the
  #       new ANGLE libs are being used.
  ANGLE_HASH=$(head -c 12 /angle/version)
  if ! printf "%s" "$GLES_RUNTIME_VERSION" | grep --quiet "${ANGLE_HASH}"; then
    echo "Fatal: Android is loading a wrong version of the ANGLE libs: ${ANGLE_HASH}" 1>&2
    exit 1
  fi
fi

if ! printf "%s" "$VK_RUNTIME_VERSION" | grep -Fq -- "${MESA_BUILD_VERSION}"; then
     echo "Fatal: Android is loading a wrong version of the Mesa3D Vulkan libs: ${VK_RUNTIME_VERSION}" 1>&2
     exit 1
fi

get_surfaceflinger_pid() {
  while [ "$($ADB shell dumpsys -l | grep 'SurfaceFlinger$')" = "" ] ; do sleep 1; done
  $ADB shell ps -A | grep -i surfaceflinger | tr -s ' ' | cut -d ' ' -f 2
}

OLD_SF_PID=$(get_surfaceflinger_pid)

# restart Android shell, so that services use the new libraries
$ADB shell stop
$ADB shell start

# Check that SurfaceFlinger restarted, to ensure that new libraries have been picked up
NEW_SF_PID=$(get_surfaceflinger_pid)

if [ "$OLD_SF_PID" == "$NEW_SF_PID" ]; then
     echo "Fatal: check that SurfaceFlinger restarted" 1>&2
     exit 1
fi

# These should be the last commands of the script in order to correctly
# propagate the exit code.
if [ -n "${ANDROID_CTS_TAG:-}" ]; then
  . "$(dirname "$0")/android-cts-runner.sh"
else
  . "$(dirname "$0")/android-deqp-runner.sh"
fi
