#define native_handle_t __native_handle_t
#define buffer_handle_t __buffer_handle_t
#include "wrapper_private.h"
#include "wrapper_entrypoints.h"
#include "vk_common_entrypoints.h"
#undef native_handle_t
#undef buffer_handle_t
#include "util/os_file.h"
#include "vk_util.h"

#include <android/hardware_buffer.h>
#include <vndk/hardware_buffer.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/dma-heap.h>

static int
safe_ioctl(int fd, unsigned long request, void *arg)
{
   int ret;

   do {
      ret = ioctl(fd, request, arg);
   } while (ret == -1 && (errno == EINTR || errno == EAGAIN));

   return ret;
}

static int
dma_heap_alloc(int heap_fd, size_t size) {
   struct dma_heap_allocation_data alloc_data = {
      .len = size,
      .fd_flags = O_RDWR | O_CLOEXEC,
   };
   if (safe_ioctl(heap_fd, DMA_HEAP_IOCTL_ALLOC, &alloc_data) < 0)
      return -1;

   return alloc_data.fd;
}

static int
ion_heap_alloc(int heap_fd, size_t size) {
   struct ion_allocation_data {
      __u64 len;
      __u32 heap_id_mask;
      __u32 flags;
      __u32 fd;
      __u32 unused;
   } alloc_data = {
      .len = size,
      /* ION_HEAP_SYSTEM | ION_SYSTEM_HEAP_ID */
      .heap_id_mask = (1U << 0) | (1U << 25),
      .flags = 0, /* uncached */
   };

   if (safe_ioctl(heap_fd, _IOWR('I', 0, struct ion_allocation_data),
                  &alloc_data) < 0)
      return -1;

   return alloc_data.fd;
}

static int
wrapper_dmabuf_alloc(struct wrapper_device *device, size_t size)
{
   int fd;

   fd = dma_heap_alloc(device->physical->dma_heap_fd, size);

   if (fd < 0)
      fd = ion_heap_alloc(device->physical->dma_heap_fd, size);

   return fd;
}


uint32_t
wrapper_select_device_memory_type(struct wrapper_device *device,
                                  VkMemoryPropertyFlags flags) {
   VkPhysicalDeviceMemoryProperties *props =
      &device->physical->memory_properties;
   int idx;

   for (idx = 0; idx < props->memoryTypeCount; idx ++) {
      if (props->memoryTypes[idx].propertyFlags & flags) {
         break;
      }
   }
   return idx < props->memoryTypeCount ? idx : UINT32_MAX;
}

static VkResult
wrapper_allocate_memory_dmaheap(struct wrapper_device *device,
                                const VkMemoryAllocateInfo* pAllocateInfo,
                                const VkAllocationCallbacks* pAllocator,
                                VkDeviceMemory* pMemory,
                                int *out_fd) {
   VkImportMemoryFdInfoKHR import_fd_info;
   VkMemoryAllocateInfo allocate_info;
   VkResult result;

   *out_fd = wrapper_dmabuf_alloc(device, pAllocateInfo->allocationSize);
   if (*out_fd < 0)
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;

   VkMemoryFdPropertiesKHR memory_fd_props = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR,
      .pNext = NULL,
   };
   result = device->dispatch_table.GetMemoryFdPropertiesKHR(
      device->dispatch_handle, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
         *out_fd, &memory_fd_props);

   if (result != VK_SUCCESS)
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;

   import_fd_info = (VkImportMemoryFdInfoKHR) {
      .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
      .pNext = pAllocateInfo->pNext,
      .fd = os_dupfd_cloexec(*out_fd),
      .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
   };
   allocate_info = *pAllocateInfo;
   allocate_info.pNext = &import_fd_info;
   allocate_info.memoryTypeIndex =
      wrapper_select_device_memory_type(device,
         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
         memory_fd_props.memoryTypeBits);

   result = device->dispatch_table.AllocateMemory(
      device->dispatch_handle, &allocate_info,
         pAllocator, pMemory);

   if (result != VK_SUCCESS && import_fd_info.fd != -1)
      close(import_fd_info.fd);

   return result;
}

static VkResult
wrapper_allocate_memory_dmabuf(struct wrapper_device *device,
                               const VkMemoryAllocateInfo* pAllocateInfo,
                               const VkAllocationCallbacks* pAllocator,
                               VkDeviceMemory* pMemory,
                               int *out_fd) {
   VkExportMemoryAllocateInfo export_memory_info;
   VkMemoryAllocateInfo allocate_info;
   VkResult result;

   export_memory_info = (VkExportMemoryAllocateInfo) {
      .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
      .pNext = pAllocateInfo->pNext,
      .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
   };
   allocate_info = *pAllocateInfo;
   allocate_info.pNext = &export_memory_info;

   result = device->dispatch_table.AllocateMemory(device->dispatch_handle,
                                                  &allocate_info,
                                                  pAllocator,
                                                  pMemory);
   if (result != VK_SUCCESS)
      return result;

   result = device->dispatch_table.GetMemoryFdKHR(
      device->dispatch_handle,
      &(VkMemoryGetFdInfoKHR) {
         .sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
         .memory = *pMemory,
         .handleType =
            VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
      },
      out_fd);

   if (result != VK_SUCCESS)
      return result;

   if (lseek(*out_fd, 0, SEEK_SET) ||
       lseek(*out_fd, 0, SEEK_END) < pAllocateInfo->allocationSize)
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;

   lseek(*out_fd, 0, SEEK_SET);
   return VK_SUCCESS;
}

static VkResult
wrapper_allocate_memory_ahardware_buffer(struct wrapper_device *device,
                                         const VkMemoryAllocateInfo* pAllocateInfo,
                                         const VkAllocationCallbacks* pAllocator,
                                         VkDeviceMemory* pMemory,
                                         AHardwareBuffer **pAHardwareBuffer) {
   VkExportMemoryAllocateInfo export_memory_info;
   VkMemoryAllocateInfo allocate_info;
   VkResult result;

   export_memory_info = (VkExportMemoryAllocateInfo) {
      .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
      .pNext = pAllocateInfo->pNext,
      .handleTypes =
         VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID,
   };
   allocate_info = *pAllocateInfo;
   allocate_info.pNext = &export_memory_info;

   result = device->dispatch_table.AllocateMemory(device->dispatch_handle,
                                                  &allocate_info,
                                                  pAllocator,
                                                  pMemory);
   if (result != VK_SUCCESS)
      return result;

   result = device->dispatch_table.GetMemoryAndroidHardwareBufferANDROID(
      device->dispatch_handle,
      &(VkMemoryGetAndroidHardwareBufferInfoANDROID) {
         .sType =
            VK_STRUCTURE_TYPE_MEMORY_GET_ANDROID_HARDWARE_BUFFER_INFO_ANDROID,
         .memory = *pMemory,
      },
      pAHardwareBuffer);

   if (result != VK_SUCCESS)
      return result;
   
   if (AHardwareBuffer_getNativeHandle(*pAHardwareBuffer) == NULL)
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;

   return VK_SUCCESS;
}

static void
wrapper_memory_data_reset(struct wrapper_memory_data *data) {
   struct wrapper_device *device = data->device;
   if (data->ahardware_buffer) {
      AHardwareBuffer_release(data->ahardware_buffer);
      data->ahardware_buffer = NULL;
   }
   if (data->dmabuf_fd != -1) {
      close(data->dmabuf_fd);
      data->dmabuf_fd = -1;
   }
   if (data->map_address && data->map_size) {
      munmap(data->map_address, data->map_size);
      data->map_address = NULL;
   }
   if (data->memory != VK_NULL_HANDLE) {
      device->dispatch_table.FreeMemory(device->dispatch_handle,
         data->memory, data->alloc);
      data->memory = VK_NULL_HANDLE;
   }
}

static VkResult
wrapper_memory_data_create(struct wrapper_device *device,
                           const VkAllocationCallbacks *alloc,
                           struct wrapper_memory_data **out_data)
{
   *out_data = vk_zalloc2(&device->vk.alloc, alloc,
                          sizeof(struct wrapper_memory_data),
                          8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (*out_data == NULL)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   (*out_data)->dmabuf_fd = -1;
   (*out_data)->device = device;
   (*out_data)->alloc = alloc ? alloc : &device->vk.alloc;
   list_add(&(*out_data)->link, &device->memory_data_list);
   return VK_SUCCESS;
}

static void
wrapper_memory_data_destroy(struct wrapper_memory_data *data) {
   wrapper_memory_data_reset(data);
   list_del(&data->link);
   vk_free2(&data->device->vk.alloc, data->alloc, data);
}

static struct wrapper_memory_data *
wrapper_device_memory_data(struct wrapper_device *device,
                           VkDeviceMemory memory) {
   struct wrapper_memory_data *result = NULL;

   simple_mtx_lock(&device->resource_mutex);

   list_for_each_entry(struct wrapper_memory_data, data,
                       &device->memory_data_list, link) {
      if (data->memory == memory) {
         result = data;
      }
   }

   simple_mtx_unlock(&device->resource_mutex);
   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
wrapper_AllocateMemory(VkDevice _device,
                       const VkMemoryAllocateInfo* pAllocateInfo,
                       const VkAllocationCallbacks* pAllocator,
                       VkDeviceMemory* pMemory) {
   VK_FROM_HANDLE(wrapper_device, device, _device);
   struct wrapper_memory_data *data;
   VkResult result;

   VkMemoryPropertyFlags property_flags =
      device->physical->memory_properties.memoryTypes[
         pAllocateInfo->memoryTypeIndex].propertyFlags;

   if (!device->vk.enabled_features.memoryMapPlaced ||
       !device->vk.enabled_extensions.EXT_map_memory_placed)
      goto fallback;

   if (!(property_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
      goto fallback;

   if (vk_find_struct_const(pAllocateInfo, IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID))
      goto fallback;

   if (vk_find_struct_const(pAllocateInfo, IMPORT_MEMORY_FD_INFO_KHR))
      goto fallback;

   if (vk_find_struct_const(pAllocateInfo, EXPORT_MEMORY_ALLOCATE_INFO))
      goto fallback;

   simple_mtx_lock(&device->resource_mutex);

   result = wrapper_memory_data_create(device, pAllocator, &data);
   if (result != VK_SUCCESS) {
      vk_error(device, result);
      goto out;
   }

   result = wrapper_allocate_memory_dmabuf(device,
      pAllocateInfo, pAllocator, &data->memory, &data->dmabuf_fd);

   if (result != VK_SUCCESS) {
      wrapper_memory_data_reset(data);
      result = wrapper_allocate_memory_dmaheap(device,
         pAllocateInfo, pAllocator, &data->memory, &data->dmabuf_fd);
   }

   if (result != VK_SUCCESS) {
      wrapper_memory_data_reset(data);
      result = wrapper_allocate_memory_ahardware_buffer(device,
         pAllocateInfo, pAllocator, &data->memory, &data->ahardware_buffer);
   }

   if (result != VK_SUCCESS) {
      wrapper_memory_data_destroy(data);
      vk_error(device, result);
   } else {
      *pMemory = data->memory;
   }

out:
   simple_mtx_unlock(&data->device->resource_mutex);
   return result;

fallback:
   return device->dispatch_table.AllocateMemory(device->dispatch_handle,
      pAllocateInfo, pAllocator, pMemory);
}

VKAPI_ATTR void VKAPI_CALL
wrapper_FreeMemory(VkDevice _device, VkDeviceMemory _memory,
                   const VkAllocationCallbacks* pAllocator)
{
   VK_FROM_HANDLE(wrapper_device, device, _device);
   struct wrapper_memory_data *data;

   data = wrapper_device_memory_data(device, _memory);
   if (data) {
      data->alloc = pAllocator;
      return wrapper_memory_data_destroy(data);
   }

   device->dispatch_table.FreeMemory(device->dispatch_handle,
                                     _memory,
                                     pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL
wrapper_MapMemory2KHR(VkDevice _device,
                      const VkMemoryMapInfoKHR* pMemoryMapInfo,
                      void** ppData)
{
   VK_FROM_HANDLE(wrapper_device, device, _device);
   const VkMemoryMapPlacedInfoEXT *placed_info = NULL;
   struct wrapper_memory_data *data;
   int fd;

   if (pMemoryMapInfo->flags & VK_MEMORY_MAP_PLACED_BIT_EXT)
      placed_info = vk_find_struct_const(pMemoryMapInfo->pNext,
         MEMORY_MAP_PLACED_INFO_EXT);

   data = wrapper_device_memory_data(device, pMemoryMapInfo->memory);
   if (!placed_info || !data)
      return device->dispatch_table.MapMemory(device->dispatch_handle,
         pMemoryMapInfo->memory, pMemoryMapInfo->offset, pMemoryMapInfo->size,
            0, ppData);

   if (data->map_address) {
      if (placed_info->pPlacedAddress != data->map_address) {
         return VK_ERROR_MEMORY_MAP_FAILED;
      } else {
         *ppData = (char *)data->map_address
            + pMemoryMapInfo->offset;
         return VK_SUCCESS;
      }
   }
   assert(data->dmabuf_fd >= 0 || data->ahardware_buffer != NULL);

   if (data->ahardware_buffer) {
      const native_handle_t *handle;
      const int *handle_fds;

      handle = AHardwareBuffer_getNativeHandle(data->ahardware_buffer);
      handle_fds = &handle->data[0];

      int idx;
      for (idx = 0; idx < handle->numFds; idx++) {
         size_t size = lseek(handle_fds[idx], 0, SEEK_END);
         if (size >= data->alloc_size) {
            break;
         }
      }
      assert(idx < handle->numFds);
      fd = handle_fds[idx];
   } else {
      fd = data->dmabuf_fd;
   }

   if (pMemoryMapInfo->size == VK_WHOLE_SIZE)
      data->map_size = data->alloc_size > 0 ?
         data->alloc_size : lseek(fd, 0, SEEK_END);
   else
      data->map_size = pMemoryMapInfo->size;

   data->map_address = mmap(placed_info->pPlacedAddress,
      data->map_size, PROT_READ | PROT_WRITE,
         MAP_SHARED | MAP_FIXED, fd, 0);

   if (data->map_address == MAP_FAILED) {
      data->map_address = NULL;
      data->map_size = 0;
      fprintf(stderr, "%s: mmap failed\n", __func__);
      return vk_error(device, VK_ERROR_MEMORY_MAP_FAILED);
   }

   *ppData = (char *)data->map_address + pMemoryMapInfo->offset;

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
wrapper_UnmapMemory(VkDevice _device, VkDeviceMemory _memory) {
   vk_common_UnmapMemory(_device, _memory);
}

VKAPI_ATTR VkResult VKAPI_CALL
wrapper_UnmapMemory2KHR(VkDevice _device,
                        const VkMemoryUnmapInfoKHR* pMemoryUnmapInfo)
{
   VK_FROM_HANDLE(wrapper_device, device, _device);
   struct wrapper_memory_data *data;

   data = wrapper_device_memory_data(device, pMemoryUnmapInfo->memory);
   if (!data) {
      device->dispatch_table.UnmapMemory(device->dispatch_handle,
         pMemoryUnmapInfo->memory);
      return VK_SUCCESS;
   }

   if (pMemoryUnmapInfo->flags & VK_MEMORY_UNMAP_RESERVE_BIT_EXT) {
      data->map_address = mmap(data->map_address, data->map_size,
         PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
      if (data->map_address == MAP_FAILED) {
         fprintf(stderr, "Failed to replace mapping with reserved memory");
         return vk_error(device, VK_ERROR_MEMORY_MAP_FAILED);
      }
   } else {
      munmap(data->map_address, data->map_size);
   }

   data->map_size = 0;
   data->map_address = NULL;
   return VK_SUCCESS;
}

