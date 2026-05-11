/*
 * Copyright 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ANDROID_VNDK_NATIVEWINDOW_AHARDWAREBUFFER_H
#define ANDROID_VNDK_NATIVEWINDOW_AHARDWAREBUFFER_H

// vndk is a superset of the NDK
#include <android/hardware_buffer.h>

#include <cutils/native_handle.h>
#include <errno.h>

__BEGIN_DECLS

/**
 * Get the native handle from an AHardwareBuffer.
 *
 * \return a non-NULL native handle on success, NULL if \a buffer is nullptr or the operation fails
 * for any reason.
 */
const native_handle_t* _Nullable AHardwareBuffer_getNativeHandle(
        const AHardwareBuffer* _Nonnull buffer);

enum CreateFromHandleMethod {
    // enum values chosen to match internal GraphicBuffer::HandleWrapMethod
    AHARDWAREBUFFER_CREATE_FROM_HANDLE_METHOD_REGISTER = 2,
    AHARDWAREBUFFER_CREATE_FROM_HANDLE_METHOD_CLONE = 3,
};

/**
 * Create an AHardwareBuffer from a native handle.
 *
 * This function wraps a native handle in an AHardwareBuffer suitable for use by applications or
 * other parts of the system. The contents of desc will be returned by AHardwareBuffer_describe().
 *
 * If method is AHARDWAREBUFFER_CREATE_FROM_HANDLE_METHOD_REGISTER, the handle is assumed to be
 * unregistered, and it will be registered/imported before being wrapped in the AHardwareBuffer.
 * If successful, the AHardwareBuffer will own the handle.
 *
 * If method is AHARDWAREBUFFER_CREATE_FROM_HANDLE_METHOD_CLONE, the handle will be cloned and the
 * clone registered. The AHardwareBuffer will own the cloned handle but not the original.
 *
 * \return 0 on success, -EINVAL if \a desc or \a handle or outBuffer is NULL, or an error number if
 * the operation fails for any reason.
 */
int AHardwareBuffer_createFromHandle(const AHardwareBuffer_Desc* _Nonnull desc,
                                     const native_handle_t* _Nonnull handle, int32_t method,
                                     AHardwareBuffer* _Nullable* _Nonnull outBuffer);

/**
 * Buffer pixel formats.
 */
enum {
    /* for future proofing, keep these in sync with system/graphics-base.h */

    /* same as HAL_PIXEL_FORMAT_BGRA_8888 */
    AHARDWAREBUFFER_FORMAT_B8G8R8A8_UNORM           = 5,
    /* same as HAL_PIXEL_FORMAT_YV12 */
    AHARDWAREBUFFER_FORMAT_YV12                     = 0x32315659,
    /* same as HAL_PIXEL_FORMAT_Y8 */
    AHARDWAREBUFFER_FORMAT_Y8                       = 0x20203859,
    /* same as HAL_PIXEL_FORMAT_Y16 */
    AHARDWAREBUFFER_FORMAT_Y16                      = 0x20363159,
    /* same as HAL_PIXEL_FORMAT_RAW16 */
    AHARDWAREBUFFER_FORMAT_RAW16                    = 0x20,
    /* same as HAL_PIXEL_FORMAT_RAW10 */
    AHARDWAREBUFFER_FORMAT_RAW10                    = 0x25,
    /* same as HAL_PIXEL_FORMAT_RAW12 */
    AHARDWAREBUFFER_FORMAT_RAW12                    = 0x26,
    /* same as HAL_PIXEL_FORMAT_RAW_OPAQUE */
    AHARDWAREBUFFER_FORMAT_RAW_OPAQUE               = 0x24,
    /* same as HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED */
    AHARDWAREBUFFER_FORMAT_IMPLEMENTATION_DEFINED   = 0x22,
    /* same as HAL_PIXEL_FORMAT_YCBCR_422_SP */
    AHARDWAREBUFFER_FORMAT_YCbCr_422_SP             = 0x10,
    /* same as HAL_PIXEL_FORMAT_YCRCB_420_SP */
    AHARDWAREBUFFER_FORMAT_YCrCb_420_SP             = 0x11,
    /* same as HAL_PIXEL_FORMAT_YCBCR_422_I */
    AHARDWAREBUFFER_FORMAT_YCbCr_422_I              = 0x14,
};

/**
 * Buffer usage flags.
 */
enum {
    /* for future proofing, keep these in sync with hardware/gralloc.h */

    /* The buffer will be written by the HW camera pipeline. */
    AHARDWAREBUFFER_USAGE_CAMERA_WRITE              = 2UL << 16,
    /* The buffer will be read by the HW camera pipeline. */
    AHARDWAREBUFFER_USAGE_CAMERA_READ               = 4UL << 16,
    /* Mask for the camera access values. */
    AHARDWAREBUFFER_USAGE_CAMERA_MASK               = 6UL << 16,
};

/**
 * Additional options for AHardwareBuffer_allocateWithOptions. These correspond to
 * android.hardware.graphics.common.ExtendableType
 */
typedef struct {
    const char* _Nonnull name;
    int64_t value;
} AHardwareBufferLongOptions;

enum AHardwareBufferStatus : int32_t {
    /* Success, no error */
    AHARDWAREBUFFER_STATUS_OK = 0,
    /* There's insufficient memory to satisfy the request */
    AHARDWAREBUFFER_STATUS_NO_MEMORY = -ENOMEM,
    /* The given argument is invalid */
    AHARDWAREBUFFER_STATUS_BAD_VALUE = -EINVAL,
    /* The requested operation is not supported by the device */
    AHARDWAREBUFFER_STATUS_UNSUPPORTED = -ENOSYS,
    /* An unknown error occurred */
    AHARDWAREBUFFER_STATUS_UNKNOWN_ERROR = (-2147483647 - 1),
};

/**
 * Allocates a buffer that matches the passed AHardwareBuffer_Desc with additional options
 *
 * If allocation succeeds, the buffer can be used according to the
 * usage flags specified in its description. If a buffer is used in ways
 * not compatible with its usage flags, the results are undefined and
 * may include program termination.
 *
 * @param desc The AHardwareBuffer_Desc that describes the allocation to request. Note that `stride`
 *             is ignored.
 * @param additionalOptions A pointer to an array of AHardwareBufferLongOptions with additional
 *                          string key + long value options that may be specified. May be null if
 *                          `additionalOptionsSize` is 0
 * @param additionalOptionsSize The number of additional options to pass
 * @param outBuffer The resulting buffer allocation
 * @return AHARDWAREBUFFER_STATUS_OK on success
 *         AHARDWAREBUFFER_STATUS_NO_MEMORY if there's insufficient resources for the allocation
 *         AHARDWAREBUFFER_STATUS_BAD_VALUE if the provided description & options are not supported
 *         by the device
 *         AHARDWAREBUFFER_STATUS_UNKNOWN_ERROR for any other error
 * any reason. The returned buffer has a reference count of 1.
 */
enum AHardwareBufferStatus AHardwareBuffer_allocateWithOptions(
        const AHardwareBuffer_Desc* _Nonnull desc,
        const AHardwareBufferLongOptions* _Nullable additionalOptions, size_t additionalOptionsSize,
        AHardwareBuffer* _Nullable* _Nonnull outBuffer) __INTRODUCED_IN(__ANDROID_API_V__);

/**
 * Queries the dataspace of the given AHardwareBuffer.
 *
 * @param buffer The non-null buffer for which to query the Dataspace
 * @return The dataspace of the buffer, or ADATASPACE_UNKNOWN if one hasn't been set
 */
enum ADataSpace AHardwareBuffer_getDataSpace(const AHardwareBuffer* _Nonnull buffer)
        __INTRODUCED_IN(__ANDROID_API_V__);

/**
 * Sets the dataspace of the given AHardwareBuffer
 * @param buffer The non-null buffer for which to set the dataspace
 * @param dataSpace The dataspace to set
 * @return AHARDWAREBUFFER_STATUS_OK on success,
 *         AHARDWAREBUFFER_STATUS_UNSUPPORTED if the device doesn't support setting the dataspace,
 *         AHARDWAREBUFFER_STATUS_UNKNOWN_ERROR for any other failure.
 */
enum AHardwareBufferStatus AHardwareBuffer_setDataSpace(AHardwareBuffer* _Nonnull buffer,
                                                        enum ADataSpace dataSpace)
        __INTRODUCED_IN(__ANDROID_API_V__);

__END_DECLS

#endif /* ANDROID_VNDK_NATIVEWINDOW_AHARDWAREBUFFER_H */
