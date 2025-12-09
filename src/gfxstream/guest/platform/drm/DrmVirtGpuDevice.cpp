/*
 * Copyright 2022 Google
 * SPDX-License-Identifier: MIT
 */

#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <xf86drm.h>

#include <cerrno>
#include <cstring>
#include <fstream>
#include <string>

#include "DrmVirtGpu.h"
#include "drm-uapi/virtgpu_drm.h"
#include "util/detect_os.h"
#include "util/log.h"
#include "util/os_misc.h"

#ifdef MAJOR_IN_MKDEV
#include <sys/mkdev.h>
#endif
#ifdef MAJOR_IN_SYSMACROS
#include <sys/sysmacros.h>
#endif

#define VIRTGPU_PCI_VENDOR_ID 0x1af4
#define VIRTGPU_PCI_DEVICE_ID 0x1050

#define VIRTGPU_PARAM_CREATE_FENCE_PASSING 9  /* Fence passing */
#define VIRTGPU_PARAM_CREATE_GUEST_HANDLE 10  /* Host OS handle can be created from guest memory. */

#define PARAM(x) \
    (struct VirtGpuParam) { x, #x, 0 }

static inline uint32_t align_up(uint32_t n, uint32_t a) { return ((n + a - 1) / a) * a; }

int32_t DrmVirtGpuDevice::openDevice() {
    drmDevicePtr devs[8];
    int32_t ret = -EINVAL;
    int count = drmGetDevices2(0, devs, ARRAY_SIZE(devs));
    if (count <= 0) {
        mesa_logd("failed to enumerate DRM devices");
        goto out;
    }

    for (int i = 0; i < count; i++) {
        drmDevicePtr dev = devs[i];
        bool supported_bus = false;

        switch (dev->bustype) {
            case DRM_BUS_PCI:
                if (dev->deviceinfo.pci->vendor_id == VIRTGPU_PCI_VENDOR_ID &&
                    dev->deviceinfo.pci->device_id == VIRTGPU_PCI_DEVICE_ID)
                    supported_bus = true;
                break;
            case DRM_BUS_PLATFORM:
                supported_bus = true;
                break;
            default:
                break;
        }

        if (!supported_bus || !(dev->available_nodes & (1 << DRM_NODE_RENDER))) {
            const char* name = "unknown";
            for (uint32_t i = 0; i < DRM_NODE_MAX; i++) {
                if (dev->available_nodes & (1 << i)) {
                    name = dev->nodes[i];
                    break;
                }
            }

            mesa_logd("skipping DRM device %s", name);
            continue;
        }

        const char* primary_path = dev->nodes[DRM_NODE_PRIMARY];
        const char* node_path = dev->nodes[DRM_NODE_RENDER];

        int fd = open(node_path, O_RDWR | O_CLOEXEC);
        if (fd < 0) {
            mesa_logd("failed to open %s", node_path);
            ret = -errno;
            goto out;
        }

        drmVersionPtr version = drmGetVersion(fd);
        if (!version || strcmp(version->name, "virtio_gpu") || version->version_major != 0) {
            if (version) {
                mesa_logd("unknown DRM driver %s version %d", version->name,
                          version->version_major);
            } else {
                mesa_logd("failed to get DRM driver version");
            }

            if (version) {
                drmFreeVersion(version);
            }

            close(fd);
            ret = -errno;
            goto out;
        }

        struct stat st;
        if (stat(primary_path, &st) == 0) {
            mHasPrimary = true;
            mPrimaryMajor = major(st.st_rdev);
            mPrimaryMinor = minor(st.st_rdev);
        } else {
            mHasPrimary = false;
            mPrimaryMajor = 0;
            mPrimaryMinor = 0;
        }

        stat(node_path, &st);
        mRenderMajor = major(st.st_rdev);
        mRenderMinor = minor(st.st_rdev);

        mBusType = dev->bustype;
        if (dev->bustype == DRM_BUS_PCI) {
            mPciBusInfo = *dev->businfo.pci;
        }

        drmFreeVersion(version);
        mesa_logd("using DRM device %s", node_path);

        mDeviceHandle = static_cast<int64_t>(fd);
        ret = 0;
        break;
    }

out:
    drmFreeDevices(devs, count);
    return ret;
}

DrmVirtGpuDevice::DrmVirtGpuDevice(enum VirtGpuCapset capset) : VirtGpuDevice(capset) {}

int32_t DrmVirtGpuDevice::init(int32_t descriptor) {
    struct VirtGpuParam params[] = {
        PARAM(VIRTGPU_PARAM_3D_FEATURES),          PARAM(VIRTGPU_PARAM_CAPSET_QUERY_FIX),
        PARAM(VIRTGPU_PARAM_RESOURCE_BLOB),        PARAM(VIRTGPU_PARAM_HOST_VISIBLE),
        PARAM(VIRTGPU_PARAM_CROSS_DEVICE),         PARAM(VIRTGPU_PARAM_CONTEXT_INIT),
        PARAM(VIRTGPU_PARAM_SUPPORTED_CAPSET_IDs), PARAM(VIRTGPU_PARAM_EXPLICIT_DEBUG_NAME),
        PARAM(VIRTGPU_PARAM_CREATE_FENCE_PASSING),
        PARAM(VIRTGPU_PARAM_CREATE_GUEST_HANDLE),
    };

    int ret = -EINVAL;
    struct drm_virtgpu_get_caps get_caps = {0};
    struct drm_virtgpu_context_init init = {0};
    struct drm_virtgpu_context_set_param ctx_set_params[3] = {{0}};
    const char* processName = nullptr;

    memset(&mCaps, 0, sizeof(struct VirtGpuCaps));

#if DETECT_OS_ANDROID
    processName = getprogname();
#endif

    if (descriptor < 0) {
#if DETECT_OS_ANDROID
        // Somebody needs to modify CF's SELinux rules to account for the syscalls used by
        // drmGetDevices2().
        mDeviceHandle = static_cast<int64_t>(drmOpenRender(128));
        if (mDeviceHandle < 0) {
            mesa_loge("Failed to open rendernode: %s", strerror(errno));
            return -EINVAL;
        }
#else
        ret = openDevice();
        if (ret < 0) {
            mesa_logd("no virtio_gpu devices found");
            return ret;
        }
#endif
    } else {
        mDeviceHandle = dup(descriptor);
        if (mDeviceHandle < 0) {
            mesa_loge("Failed to dup rendernode: %s", strerror(errno));
            return ret;
        }
    }

    for (uint32_t i = 0; i < kParamMax; i++) {
        struct drm_virtgpu_getparam get_param = {0};
        get_param.param = params[i].param;
        get_param.value = (uint64_t)(uintptr_t)&params[i].value;

        ret = drmIoctl(mDeviceHandle, DRM_IOCTL_VIRTGPU_GETPARAM, &get_param);
        if (ret) {
            mesa_logd("Could not query virtgpu param from device: %s", params[i].name);
            continue;
        }

        mCaps.params[i] = params[i].value;
    }

#if !DETECT_OS_ANDROID
    if ((mCaps.params[kParamSupportedCapsetIds] & (1 << VIRTGPU_DRM_CAPSET_GFXSTREAM_VULKAN)) ==
        0) {
        return -EINVAL;
    }
#endif

    auto capset = getCapset();
    get_caps.cap_set_id = static_cast<uint32_t>(capset);
    switch (capset) {
        case kCapsetGfxStreamVulkan:
            get_caps.size = sizeof(struct vulkanCapset);
            get_caps.addr = (unsigned long long)&mCaps.vulkanCapset;
            break;
        case kCapsetGfxStreamGles:
            get_caps.size = sizeof(struct glesCapset);
            get_caps.addr = (unsigned long long)&mCaps.glesCapset;
            break;
        case kCapsetGfxStreamComposer:
            get_caps.size = sizeof(struct composerCapset);
            get_caps.addr = (unsigned long long)&mCaps.composerCapset;
            break;
        default:
            get_caps.size = 0;
    }

    ret = drmIoctl(mDeviceHandle, DRM_IOCTL_VIRTGPU_GET_CAPS, &get_caps);
    if (ret) {
        // Don't fail get capabilities just yet, AEMU doesn't use this API
        // yet (b/272121235);
        mesa_loge("DRM_IOCTL_VIRTGPU_GET_CAPS failed with %s", strerror(errno));
    }

    // We always need an ASG blob in some cases, so always define blobAlignment
    if (!mCaps.vulkanCapset.blobAlignment) {
        mCaps.vulkanCapset.blobAlignment = 4096;
    }

    uint64_t guest_page_size = 4096;
    os_get_page_size(&guest_page_size);
    mCaps.vulkanCapset.blobAlignment =
        std::max((uint32_t)guest_page_size, mCaps.vulkanCapset.blobAlignment);

    ctx_set_params[0].param = VIRTGPU_CONTEXT_PARAM_NUM_RINGS;
    ctx_set_params[0].value = 2;
    init.num_params = 1;

    if (capset != kCapsetNone) {
        ctx_set_params[init.num_params].param = VIRTGPU_CONTEXT_PARAM_CAPSET_ID;
        ctx_set_params[init.num_params].value = static_cast<uint32_t>(capset);
        init.num_params++;
    }

    if (mCaps.params[kParamExplicitDebugName] && processName) {
        ctx_set_params[init.num_params].param = VIRTGPU_CONTEXT_PARAM_DEBUG_NAME;
        ctx_set_params[init.num_params].value = reinterpret_cast<uint64_t>(processName);
        init.num_params++;
    }

    init.ctx_set_params = (unsigned long long)&ctx_set_params[0];
    ret = drmIoctl(mDeviceHandle, DRM_IOCTL_VIRTGPU_CONTEXT_INIT, &init);
    if (ret) {
        mesa_loge("DRM_IOCTL_VIRTGPU_CONTEXT_INIT failed with %s, continuing without context...",
                  strerror(errno));
    }

    return 0;
}

DrmVirtGpuDevice::~DrmVirtGpuDevice() { close(mDeviceHandle); }

struct VirtGpuCaps DrmVirtGpuDevice::getCaps(void) { return mCaps; }

int64_t DrmVirtGpuDevice::getDeviceHandle(void) { return mDeviceHandle; }

VirtGpuResourcePtr DrmVirtGpuDevice::createResource(uint32_t width, uint32_t height,
                                                      uint32_t stride, uint32_t size,
                                                      uint32_t virglFormat, uint32_t target,
                                                      uint32_t bind) {
    drm_virtgpu_resource_create create = {
        .target = target,
        .format = virglFormat,
        .bind = bind,
        .width = width,
        .height = height,
        .depth = 1U,
        .array_size = 1U,
        .last_level = 0,
        .nr_samples = 0,
        .size = size,
        .stride = stride,
    };

    int ret = drmIoctl(mDeviceHandle, DRM_IOCTL_VIRTGPU_RESOURCE_CREATE, &create);
    if (ret) {
        mesa_loge("DRM_IOCTL_VIRTGPU_RESOURCE_CREATE failed with %s", strerror(errno));
        return nullptr;
    }

    return std::make_shared<DrmVirtGpuResource>(
        mDeviceHandle, create.bo_handle, create.res_handle, static_cast<uint64_t>(create.size));
}

VirtGpuResourcePtr DrmVirtGpuDevice::createBlob(const struct VirtGpuCreateBlob& blobCreate) {
    int ret;
    struct drm_virtgpu_resource_create_blob create = {0};

    create.size = blobCreate.size;
    create.blob_mem = blobCreate.blobMem;
    create.blob_flags = blobCreate.flags;
    create.blob_id = blobCreate.blobId;
    create.cmd = (uint64_t)(uintptr_t)blobCreate.blobCmd;
    create.cmd_size = blobCreate.blobCmdSize;

    ret = drmIoctl(mDeviceHandle, DRM_IOCTL_VIRTGPU_RESOURCE_CREATE_BLOB, &create);
    if (ret < 0) {
        mesa_loge("DRM_VIRTGPU_RESOURCE_CREATE_BLOB failed with %s", strerror(errno));
        return nullptr;
    }

    return std::make_shared<DrmVirtGpuResource>(mDeviceHandle, create.bo_handle,
                                                  create.res_handle, blobCreate.size);
}

VirtGpuResourcePtr DrmVirtGpuDevice::importBlob(const struct VirtGpuExternalHandle& handle) {
    struct drm_virtgpu_resource_info info = {0};
    uint32_t blobHandle;
    int ret;

    ret = drmPrimeFDToHandle(mDeviceHandle, handle.osHandle, &blobHandle);
    close(handle.osHandle);
    if (ret) {
        mesa_loge("DRM_IOCTL_PRIME_FD_TO_HANDLE failed: %s", strerror(errno));
        return nullptr;
    }

    info.bo_handle = blobHandle;
    ret = drmIoctl(mDeviceHandle, DRM_IOCTL_VIRTGPU_RESOURCE_INFO, &info);
    if (ret) {
        mesa_loge("DRM_IOCTL_VIRTGPU_RESOURCE_INFO failed: %s", strerror(errno));
        return nullptr;
    }

    return std::make_shared<DrmVirtGpuResource>(mDeviceHandle, blobHandle, info.res_handle,
                                                  static_cast<uint64_t>(info.size));
}

int DrmVirtGpuDevice::execBuffer(struct VirtGpuExecBuffer& execbuffer,
                                   const VirtGpuResource* blob) {
    int ret;
    struct drm_virtgpu_execbuffer exec = {0};
    uint32_t blobHandle;

    exec.flags = execbuffer.flags;
    exec.size = execbuffer.command_size;
    exec.ring_idx = execbuffer.ring_idx;
    exec.command = (uint64_t)(uintptr_t)(execbuffer.command);
    exec.fence_fd = -1;

    if (blob) {
        blobHandle = blob->getBlobHandle();
        exec.bo_handles = (uint64_t)(uintptr_t)(&blobHandle);
        exec.num_bo_handles = 1;
    }

    ret = drmIoctl(mDeviceHandle, DRM_IOCTL_VIRTGPU_EXECBUFFER, &exec);
    if (ret) {
        mesa_loge("DRM_IOCTL_VIRTGPU_EXECBUFFER failed: %s", strerror(errno));
        return ret;
    }

    if (execbuffer.flags & kFenceOut) {
        execbuffer.handle.osHandle = exec.fence_fd;
        execbuffer.handle.type = kFenceHandleSyncFd;
    }

    return 0;
}

VirtGpuDevice* osCreateVirtGpuDevice(enum VirtGpuCapset capset, int32_t descriptor) {
    auto device = new DrmVirtGpuDevice(capset);
    int32_t ret = device->init(descriptor);
    if (ret) {
        delete device;
        return nullptr;
    }

    return device;
}

bool DrmVirtGpuDevice::getDrmInfo(VirtGpuDrmInfo* drmInfo) {
    drmInfo->hasPrimary = mHasPrimary;
    drmInfo->hasRender = true;
    drmInfo->primaryMajor = mPrimaryMajor;
    drmInfo->primaryMinor = mPrimaryMinor;
    drmInfo->renderMajor = mRenderMajor;
    drmInfo->renderMinor = mRenderMinor;
    return true;
}

bool DrmVirtGpuDevice::getPciBusInfo(VirtGpuPciBusInfo* pciBusInfo) {
    if (mBusType != DRM_BUS_PCI) {
        return false;
    }

    pciBusInfo->domain = mPciBusInfo.domain;
    pciBusInfo->bus = mPciBusInfo.bus;
    pciBusInfo->device = mPciBusInfo.dev;
    pciBusInfo->function = mPciBusInfo.func;
    return true;
}
