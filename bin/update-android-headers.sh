#!/bin/sh

set -eu

if [ ! -e .git ]; then
    echo must run from top-level directory;
    exit 1
fi

if [ ! -d platform-hardware-libhardware ]; then
    git clone --depth 1 https://android.googlesource.com/platform/frameworks/native platform-frameworks-native
    git clone --depth 1 https://android.googlesource.com/platform/hardware/libhardware platform-hardware-libhardware
    git clone --depth 1 https://android.googlesource.com/platform/system/core platform-system-core
    git clone --depth 1 https://android.googlesource.com/platform/system/logging platform-system-logging
fi

dest=include/android_stub

# Persist the frozen Android N system/window.h for backward compatibility

cp -av ${dest}/system/window.h platform-system-core/libsystem/include/system

# Persist the frozen libbacktrace header for backward compatibility, since the
# support has been dropped in Android 15.
#
# TODO: add support for libunwindstack
mkdir -p platform-system-unwinding/libbacktrace/include
cp -av ${dest}/backtrace platform-system-unwinding/libbacktrace/include/

rm -rf ${dest}
mkdir ${dest}


# These directories contains only the files we need, so copy wholesale

cp -av                                                                  \
    platform-frameworks-native/libs/nativewindow/include/vndk           \
    platform-frameworks-native/libs/nativebase/include/nativebase       \
    platform-system-core/libsync/include/ndk                            \
    platform-system-core/libsync/include/sync                           \
    platform-system-unwinding/libbacktrace/include/backtrace            \
    ${dest}


# We only need a few files from these big directories so just copy those

mkdir ${dest}/hardware
cp -av platform-hardware-libhardware/include_all/hardware/{hardware,gralloc,gralloc1,fb}.h ${dest}/hardware
cp -av platform-frameworks-native/vulkan/include/hardware/hwvulkan.h ${dest}/hardware

mkdir ${dest}/cutils
cp -av platform-system-core/libcutils/include/cutils/native_handle.h ${dest}/cutils

mkdir ${dest}/system
cp -av platform-system-core/libsystem/include/system/{graphics*,window.h} ${dest}/system

# include/android has files from a few different projects

mkdir ${dest}/android
cp -av                                                                  \
    platform-frameworks-native/libs/nativewindow/include/android/{data_space,hardware_buffer,native_window}.h \
    platform-frameworks-native/libs/arect/include/android/rect.h        \
    platform-system-core/libsync/include/android/sync.h                 \
    platform-system-logging/liblog/include/android/log.h                \
    ${dest}/android

