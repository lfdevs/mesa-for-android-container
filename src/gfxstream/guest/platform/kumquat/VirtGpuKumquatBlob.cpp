/*
 * Copyright 2022 Google
 * SPDX-License-Identifier: MIT
 */

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "GfxStreamVulkanMapper.h"
#include "VirtGpuKumquat.h"
#include "util/log.h"

VirtGpuKumquatResource::VirtGpuKumquatResource(struct virtgpu_kumquat* virtGpu, uint32_t blobHandle,
                                               uint32_t resourceHandle, uint64_t size)
    : mVirtGpu(virtGpu), mBlobHandle(blobHandle), mResourceHandle(resourceHandle), mSize(size) {}

VirtGpuKumquatResource::~VirtGpuKumquatResource() {
    struct drm_kumquat_resource_unref unref {
        .bo_handle = mBlobHandle, .pad = 0,
    };

    int ret = virtgpu_kumquat_resource_unref(mVirtGpu, &unref);
    if (ret) {
        mesa_loge("Closed failed with : [%s, blobHandle %u, resourceHandle: %u]", strerror(errno),
                  mBlobHandle, mResourceHandle);
    }
}

uint32_t VirtGpuKumquatResource::getBlobHandle() const { return mBlobHandle; }

uint32_t VirtGpuKumquatResource::getResourceHandle() const { return mResourceHandle; }

uint64_t VirtGpuKumquatResource::getSize() const { return mSize; }

VirtGpuResourceMappingPtr VirtGpuKumquatResource::createMapping() {
    int ret;
    struct drm_kumquat_resource_info info = {};

    struct drm_kumquat_map map {
        .bo_handle = mBlobHandle, .ptr = NULL, .size = mSize,
    };

    info.bo_handle = mBlobHandle;
    ret = virtgpu_kumquat_resource_info(mVirtGpu, &info);

    if (info.vulkan_info.device_id.device_uuid[0]) {
        struct drm_kumquat_resource_export resource_export = {};
        struct DeviceId deviceId = {};

        resource_export.bo_handle = mBlobHandle;

        ret = virtgpu_kumquat_resource_export(mVirtGpu, &resource_export);
        if (ret) {
            mesa_loge("External memory export from kumquat failed: %s", strerror(errno));
            return nullptr;
        }

        memcpy(&deviceId, &info.vulkan_info.device_id, sizeof(struct DeviceId));
        std::optional<DeviceId> deviceIdOpt{deviceId};
        auto mapper = GfxStreamVulkanMapper::getInstance(deviceIdOpt);
        struct VulkanMapperData mapData = {0};

        mapData.handle = resource_export.os_handle;
        mapData.handleType = resource_export.handle_type;
        mapData.memoryIdx = info.vulkan_info.memory_idx;
        mapData.size = mSize;

        ret = mapper->map(&mapData);
        if (ret < 0) {
            mesa_loge("Mapping failed with %s for resource %u blob %u", strerror(errno),
                      mResourceHandle, mBlobHandle);
            return nullptr;
        }

        return std::make_shared<VirtGpuKumquatResourceMapping>(shared_from_this(), mVirtGpu,
                                                               mapData, mSize);
    } else {
        ret = virtgpu_kumquat_resource_map(mVirtGpu, &map);
        if (ret < 0) {
            mesa_loge("Mapping failed with %s for resource %u blob %u", strerror(errno),
                      mResourceHandle, mBlobHandle);
            return nullptr;
        }

        return std::make_shared<VirtGpuKumquatResourceMapping>(shared_from_this(), mVirtGpu,
                                                               (uint8_t*)map.ptr, mSize);
    }
}

int VirtGpuKumquatResource::exportBlob(struct VirtGpuExternalHandle& handle) {
    int ret;
    struct drm_kumquat_resource_export exp = {0};

    exp.bo_handle = mBlobHandle;

    ret = virtgpu_kumquat_resource_export(mVirtGpu, &exp);
    if (ret) {
        mesa_loge("Failed to export blob with %s", strerror(errno));
        return ret;
    }

    handle.osHandle = static_cast<int64_t>(exp.os_handle);
    handle.type = static_cast<VirtGpuHandleType>(exp.handle_type);
    return 0;
}

int VirtGpuKumquatResource::wait() {
    int ret;
    struct drm_kumquat_wait wait = {
        .handle = mBlobHandle,
        .flags = 0,
    };

    ret = virtgpu_kumquat_wait(mVirtGpu, &wait);
    if (ret < 0) {
        mesa_loge("Wait failed with %s", strerror(errno));
        return ret;
    }

    return 0;
}

int VirtGpuKumquatResource::transferToHost(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    int ret;
    struct drm_kumquat_transfer_to_host xfer = {0};

    xfer.box.x = x;
    xfer.box.y = y;
    xfer.box.w = w;
    xfer.box.h = h;
    xfer.box.d = 1;
    xfer.bo_handle = mBlobHandle;

    ret = virtgpu_kumquat_transfer_to_host(mVirtGpu, &xfer);
    if (ret < 0) {
        mesa_loge("Transfer to host failed with %s", strerror(errno));
        return ret;
    }

    return 0;
}

int VirtGpuKumquatResource::transferFromHost(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    int ret;
    struct drm_kumquat_transfer_from_host xfer = {0};

    xfer.box.x = x;
    xfer.box.y = y;
    xfer.box.w = w;
    xfer.box.h = h;
    xfer.box.d = 1;
    xfer.bo_handle = mBlobHandle;

    ret = virtgpu_kumquat_transfer_from_host(mVirtGpu, &xfer);
    if (ret < 0) {
        mesa_loge("Transfer from host failed with %s", strerror(errno));
        return ret;
    }

    return 0;
}
