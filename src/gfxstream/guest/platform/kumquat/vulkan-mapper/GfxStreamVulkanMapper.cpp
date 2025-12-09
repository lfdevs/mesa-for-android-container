/*
 * Copyright 2025 Mesa3D authors
 * SPDX-License-Identifier: MIT
 */

#include "GfxStreamVulkanMapper.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "util/detect_os.h"
#include "util/log.h"
#include "util/os_misc.h"
#include "virtgpu_kumquat_ffi.h"

#if DETECT_OS_WINDOWS
#include <io.h>

#include "vulkan/vulkan_win32.h"
#define VK_LIBNAME "vulkan-1.dll"
#else
#include <unistd.h>
#if DETECT_OS_APPLE
#define VK_LIBNAME "libvulkan.1.dylib"
#elif DETECT_OS_ANDROID
#define VK_LIBNAME "libvulkan.so"
#else
#define VK_LIBNAME "libvulkan.so.1"
#endif
#endif

constexpr char VK_DRIVER_FILES[] = "VK_DRIVER_FILES";
constexpr uint32_t kNvidiaVendorId = 0x10de;

#define GET_PROC_ADDR_INSTANCE_LOCAL(x) \
    PFN_vk##x vk_##x = (PFN_vk##x)vk_GetInstanceProcAddr(nullptr, "vk" #x)

std::unique_ptr<GfxStreamVulkanMapper> sVkMapper;

GfxStreamVulkanMapper::GfxStreamVulkanMapper() {}
GfxStreamVulkanMapper::~GfxStreamVulkanMapper() {}

static uint32_t chooseGfxQueueFamily(vk_uncompacted_dispatch_table* vk, VkPhysicalDevice phys_dev) {
    uint32_t family_idx = UINT32_MAX;
    uint32_t nProps = 0;

    vk->GetPhysicalDeviceQueueFamilyProperties(phys_dev, &nProps, NULL);
    std::vector<VkQueueFamilyProperties> props(nProps, VkQueueFamilyProperties{});
    vk->GetPhysicalDeviceQueueFamilyProperties(phys_dev, &nProps, props.data());
    // Choose the first graphics queue.
    for (uint32_t i = 0; i < nProps; ++i) {
        if ((props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && props[i].queueCount > 0) {
            family_idx = i;
            break;
        }
    }

    return family_idx;
}

bool GfxStreamVulkanMapper::initialize(DeviceId& deviceId) {
    mLoaderLib = util_dl_open(VK_LIBNAME);
    if (!mLoaderLib) {
        mesa_loge("failed to get loader library");
        return false;
    }

    VkResult res;

    auto vk_GetInstanceProcAddr =
        (PFN_vkGetInstanceProcAddr)util_dl_get_proc_address(mLoaderLib, "vkGetInstanceProcAddr");
    auto vk_GetDeviceProcAddr =
        (PFN_vkGetDeviceProcAddr)util_dl_get_proc_address(mLoaderLib, "vkGetDeviceProcAddr");

    if (!vk_GetInstanceProcAddr || !vk_GetDeviceProcAddr) {
        mesa_loge("failed to get devuce or instance ProcAddr");
        return false;
    }

    std::vector<const char*> externalMemoryInstanceExtNames = {
        VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
    };

    VkInstanceCreateInfo instCi = {
        VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, 0, 0, nullptr, 0, nullptr, 0, nullptr,
    };

    VkApplicationInfo appInfo = {
        VK_STRUCTURE_TYPE_APPLICATION_INFO,
        0,
        "gfxstream_vk_mapper",
        1,
        "gfxstream_vk_mapper",
        1,
        VK_API_VERSION_1_1,
    };

    instCi.pApplicationInfo = &appInfo;
    instCi.enabledExtensionCount = static_cast<uint32_t>(externalMemoryInstanceExtNames.size());
    instCi.ppEnabledExtensionNames = externalMemoryInstanceExtNames.data();

    GET_PROC_ADDR_INSTANCE_LOCAL(CreateInstance);
    res = vk_CreateInstance(&instCi, nullptr, &mInstance);
    if (res != VK_SUCCESS) {
        mesa_loge("failed to create VkMapper Instance");
        return false;
    }

    vk_instance_uncompacted_dispatch_table_load(&mVk.instance, vk_GetInstanceProcAddr, mInstance);
    vk_physical_device_uncompacted_dispatch_table_load(&mVk.physical_device, vk_GetInstanceProcAddr,
                                                       mInstance);

    uint32_t physicalDeviceCount = 0;
    std::vector<VkPhysicalDevice> physicalDevices;
    res = mVk.EnumeratePhysicalDevices(mInstance, &physicalDeviceCount, nullptr);
    if (res != VK_SUCCESS) {
        mesa_loge("failed to get physical device count");
        return false;
    }

    physicalDevices.resize(physicalDeviceCount);
    res = mVk.EnumeratePhysicalDevices(mInstance, &physicalDeviceCount, physicalDevices.data());
    if (res != VK_SUCCESS) {
        mesa_loge("failed to enumerate physical devices");
        return false;
    }

    for (uint32_t i = 0; i < physicalDeviceCount; i++) {
        VkPhysicalDeviceIDProperties idProps = {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES_KHR,
        };

        VkPhysicalDeviceProperties2 deviceProps = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2_KHR,
            .pNext = reinterpret_cast<void*>(&idProps),
        };

        mVk.GetPhysicalDeviceProperties2(physicalDevices[i], &deviceProps);
        if (memcmp(&idProps.deviceUUID[0], &deviceId.deviceUUID[0], VK_UUID_SIZE)) {
            continue;
        }

        if (memcmp(&idProps.driverUUID[0], &deviceId.driverUUID[0], VK_UUID_SIZE)) {
            continue;
        }

        std::vector<const char*> externalMemoryDeviceExtNames;
        externalMemoryDeviceExtNames.push_back(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);

#if DETECT_OS_WINDOWS
        externalMemoryDeviceExtNames.push_back(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME);
#elif DETECT_OS_LINUX
        externalMemoryDeviceExtNames.push_back(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);

        // Tesla V-100 doesn't work with dma-buf
        if (deviceProps.properties.vendorID != kNvidiaVendorId) {
            externalMemoryDeviceExtNames.push_back(VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME);
        }
#endif

        VkPhysicalDevice physDev = physicalDevices[i];
        uint32_t gfxQueueFamilyIdx = chooseGfxQueueFamily(&mVk, physDev);
        if (gfxQueueFamilyIdx == UINT32_MAX) {
            mesa_loge("failed to get gfx queue family idx");
            return false;
        }

        float priority = 1.0f;
        VkDeviceQueueCreateInfo dqCi = {
            VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, 0, 0, gfxQueueFamilyIdx, 1, &priority,
        };

        VkDeviceCreateInfo dCi = {};
        dCi.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        dCi.queueCreateInfoCount = 1;
        dCi.pQueueCreateInfos = &dqCi;
        dCi.enabledExtensionCount = static_cast<uint32_t>(externalMemoryDeviceExtNames.size());
        dCi.ppEnabledExtensionNames = externalMemoryDeviceExtNames.data();
        res = mVk.CreateDevice(physDev, &dCi, nullptr, &mDevice);
        if (res != VK_SUCCESS) {
            mesa_loge("failed to create device");
            return false;
        }
    }

    vk_device_uncompacted_dispatch_table_load(&mVk.device, vk_GetDeviceProcAddr, mDevice);

    return true;
}

// The Tesla V-100 driver seems to enter a power management mode and stops being available to
// the Vulkan loader if more than a certain number of VK instances are created in the same
// process.
//
// This behavior is reproducible via:
//
// GfxstreamEnd2EndTests --gtest_filter="*MultiThreadedVkMapMemory*"
//
// Workaround this by having a singleton mapper per-process.
GfxStreamVulkanMapper* GfxStreamVulkanMapper::getInstance(std::optional<DeviceId> deviceIdOpt) {
    if (sVkMapper == nullptr && deviceIdOpt) {
        // The idea is to make sure the gfxstream ICD isn't loaded when the mapper starts
        // up. The Nvidia ICD should be loaded.
        //
        // This is mostly useful for developers.  For AOSP hermetic gfxstream end2end
        // testing, VK_DRIVER_FILES shouldn't be defined.  For deqp-vk, this is
        // useful, but not safe for multi-threaded tests.  For now, since this is only
        // used for end2end tests, we should be good.
        char* driver = os_get_option_dup(VK_DRIVER_FILES);

        os_unset_option(VK_DRIVER_FILES);
        sVkMapper = std::make_unique<GfxStreamVulkanMapper>();
        if (!sVkMapper->initialize(*deviceIdOpt)) {
            sVkMapper = nullptr;
            return nullptr;
        }

        if (driver) {
            os_set_option(VK_DRIVER_FILES, driver, true);
        }
        free(driver);
    }

    return sVkMapper.get();
}

int32_t GfxStreamVulkanMapper::map(struct VulkanMapperData* mapData) {
    VkMemoryAllocateInfo mai = {};

    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = mapData->size;
    mai.memoryTypeIndex = mapData->memoryIdx;

#if DETECT_OS_WINDOWS
    VkImportMemoryWin32HandleInfoKHR importInfo{
        VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR,
        0,
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT,
        reinterpret_cast<HANDLE>(mapData->handle),
        L"",
    };

#elif DETECT_OS_LINUX
    VkExternalMemoryHandleTypeFlagBits flagBits;
    if (mapData->handleType == VIRTGPU_KUMQUAT_HANDLE_TYPE_MEM_DMABUF) {
        flagBits = (enum VkExternalMemoryHandleTypeFlagBits)(uint32_t(VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT));
    } else {
        flagBits = (enum VkExternalMemoryHandleTypeFlagBits)(uint32_t(VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT));
    }

    VkImportMemoryFdInfoKHR importInfo{
        VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
        0,
        flagBits,
        static_cast<int>(mapData->handle),
    };
#endif

    mai.pNext = reinterpret_cast<void*>(&importInfo);

    VkResult result = mVk.AllocateMemory(mDevice, &mai, nullptr, &mapData->memory);
    if (result != VK_SUCCESS) {
        mesa_loge("failed to import memory");
        return -EINVAL;
    }

    result = mVk.MapMemory(mDevice, mapData->memory, 0, mapData->size, 0, (void**)&mapData->ptr);
    if (result != VK_SUCCESS) {
        mesa_loge("failed to map memory");
        return -EINVAL;
    }

    return 0;
}

void GfxStreamVulkanMapper::unmap(struct VulkanMapperData* mapData) {
    mVk.UnmapMemory(mDevice, mapData->memory);
    mVk.FreeMemory(mDevice, mapData->memory, nullptr);
}
