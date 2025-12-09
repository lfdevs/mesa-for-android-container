/*
 * Copyright © 2017 Google
 * Copyright © 2019 Red Hat
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/* Rules for device selection.
 * Is there an X or wayland connection open (or DISPLAY set).
 * If no - try and find which device was the boot_vga device.
 * If yes - try and work out which device is the connection primary,
 * DRI_PRIME tagged overrides only work if bus info, =1 will just pick an alternate.
 */

#include <vulkan/vk_layer.h>
#include <vulkan/vulkan.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "util/hash_table.h"
#include "util/simple_mtx.h"
#include "util/u_debug.h"
#include "device_select.h"
#include "vk_util.h"

static struct hash_table *device_select_instance_ht = NULL;
static simple_mtx_t device_select_mutex = SIMPLE_MTX_INITIALIZER;

static void
device_select_layer_add_instance(VkInstance instance, struct instance_info *info)
{
   simple_mtx_lock(&device_select_mutex);

   if (!device_select_instance_ht)
      device_select_instance_ht = _mesa_pointer_hash_table_create(NULL);
   _mesa_hash_table_insert(device_select_instance_ht, instance, info);

   simple_mtx_unlock(&device_select_mutex);
}

static struct instance_info *
device_select_layer_get_instance(VkInstance instance)
{
   struct hash_entry *entry;
   struct instance_info *info = NULL;
   simple_mtx_lock(&device_select_mutex);
   entry = _mesa_hash_table_search(device_select_instance_ht, (void *)instance);
   if (entry)
      info = (struct instance_info *)entry->data;
   simple_mtx_unlock(&device_select_mutex);
   return info;
}

static void
device_select_layer_remove_instance(VkInstance instance)
{
   simple_mtx_lock(&device_select_mutex);

   _mesa_hash_table_remove_key(device_select_instance_ht, instance);

   if (_mesa_hash_table_num_entries(device_select_instance_ht) == 0) {
      _mesa_hash_table_destroy(device_select_instance_ht, NULL);
      device_select_instance_ht = NULL;
   }

   simple_mtx_unlock(&device_select_mutex);
}

static VkResult
device_select_CreateInstance(const VkInstanceCreateInfo *pCreateInfo,
                             const VkAllocationCallbacks *pAllocator, VkInstance *pInstance)
{
   VkLayerInstanceCreateInfo *chain_info;
   for (chain_info = (VkLayerInstanceCreateInfo *)pCreateInfo->pNext; chain_info;
        chain_info = (VkLayerInstanceCreateInfo *)chain_info->pNext)
      if (chain_info->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO &&
          chain_info->function == VK_LAYER_LINK_INFO)
         break;

   assert(chain_info->u.pLayerInfo);

   PFN_vkGetInstanceProcAddr GetInstanceProcAddr =
      chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
   PFN_vkCreateInstance fpCreateInstance =
      (PFN_vkCreateInstance)GetInstanceProcAddr(NULL, "vkCreateInstance");
   if (fpCreateInstance == NULL) {
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;

   const char *engineName =
      pCreateInfo->pApplicationInfo && pCreateInfo->pApplicationInfo->pEngineName
         ? pCreateInfo->pApplicationInfo->pEngineName
         : "";
   const char *applicationName =
      pCreateInfo->pApplicationInfo && pCreateInfo->pApplicationInfo->pApplicationName
         ? pCreateInfo->pApplicationInfo->pApplicationName
         : "";
   VkResult result = fpCreateInstance(pCreateInfo, pAllocator, pInstance);
   if (result != VK_SUCCESS) {
      return result;
   }

   struct instance_info *info = (struct instance_info *)calloc(1, sizeof(struct instance_info));
   info->GetInstanceProcAddr = GetInstanceProcAddr;
   info->zink = !strcmp(engineName, "mesa zink");
   info->xwayland = !strcmp(applicationName, "Xwayland");
   info->xserver = !strcmp(applicationName, "Xorg") || !strcmp(applicationName, "Xephyr");

#ifdef VK_USE_PLATFORM_WAYLAND_KHR
   bool has_wayland = os_get_option("WAYLAND_DISPLAY") || os_get_option("WAYLAND_SOCKET");
#endif
#ifdef VK_USE_PLATFORM_XCB_KHR
   bool has_xcb = !!os_get_option("DISPLAY");
#endif

   for (unsigned i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
      if (!strcmp(pCreateInfo->ppEnabledExtensionNames[i], VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME) &&
          has_wayland)
         info->has_wayland = true;
#endif
#ifdef VK_USE_PLATFORM_XCB_KHR
      if (!strcmp(pCreateInfo->ppEnabledExtensionNames[i], VK_KHR_XCB_SURFACE_EXTENSION_NAME) &&
          has_xcb)
         info->has_xcb = !info->xserver || !info->zink;
#endif
   }

   /*
    * The loader is currently not able to handle GetPhysicalDeviceProperties2KHR calls in
    * EnumeratePhysicalDevices when there are other layers present. To avoid mysterious crashes
    * for users just use only the vulkan version for now.
    */
   info->has_vulkan11 = pCreateInfo->pApplicationInfo &&
                        pCreateInfo->pApplicationInfo->apiVersion >= VK_MAKE_VERSION(1, 1, 0);

   info->debug = debug_get_bool_option("MESA_VK_DEVICE_SELECT_DEBUG", false) ||
                 debug_get_bool_option("DRI_PRIME_DEBUG", false);
   info->selection = os_get_option_dup("MESA_VK_DEVICE_SELECT");
   info->dri_prime = os_get_option_dup("DRI_PRIME");
   info->force_default_device =
      debug_get_bool_option("MESA_VK_DEVICE_SELECT_FORCE_DEFAULT_DEVICE", false);

#define DEVSEL_GET_CB(func)                                                                        \
   info->func = (PFN_vk##func)info->GetInstanceProcAddr(*pInstance, "vk" #func)
   DEVSEL_GET_CB(DestroyInstance);
   DEVSEL_GET_CB(EnumeratePhysicalDevices);
   DEVSEL_GET_CB(EnumeratePhysicalDeviceGroups);
   DEVSEL_GET_CB(GetPhysicalDeviceProperties);
   DEVSEL_GET_CB(EnumerateDeviceExtensionProperties);
   if (info->has_vulkan11)
      DEVSEL_GET_CB(GetPhysicalDeviceProperties2);
#undef DEVSEL_GET_CB

   device_select_layer_add_instance(*pInstance, info);

   return VK_SUCCESS;
}

static void
device_select_DestroyInstance(VkInstance instance, const VkAllocationCallbacks *pAllocator)
{
   struct instance_info *info = device_select_layer_get_instance(instance);

   device_select_layer_remove_instance(instance);
   info->DestroyInstance(instance, pAllocator);
   free(info->dri_prime);
   free(info->selection);
   free(info);
}

void
device_select_get_properties(const struct instance_info *info, VkPhysicalDevice device,
                             VkPhysicalDeviceProperties2 *properties)
{
   info->GetPhysicalDeviceProperties(device, &properties->properties);

   if (info->GetPhysicalDeviceProperties2 &&
       properties->properties.apiVersion >= VK_API_VERSION_1_1)
      info->GetPhysicalDeviceProperties2(device, properties);
}

static void
print_gpu(const struct instance_info *info, unsigned index, VkPhysicalDevice device)
{
   const char *type = "";
   VkPhysicalDevicePCIBusInfoPropertiesEXT ext_pci_properties =
      (VkPhysicalDevicePCIBusInfoPropertiesEXT){
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT,
      };
   VkPhysicalDeviceProperties2 properties = (VkPhysicalDeviceProperties2){
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
   };
   if (info->has_vulkan11 && info->has_pci_bus)
      properties.pNext = &ext_pci_properties;
   device_select_get_properties(info, device, &properties);

   switch (properties.properties.deviceType) {
   case VK_PHYSICAL_DEVICE_TYPE_OTHER:
   default:
      type = "other";
      break;
   case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
      type = "integrated GPU";
      break;
   case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
      type = "discrete GPU";
      break;
   case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
      type = "virtual GPU";
      break;
   case VK_PHYSICAL_DEVICE_TYPE_CPU:
      type = "CPU";
      break;
   }
   fprintf(stderr, "  GPU %d: %x:%x \"%s\" %s", index, properties.properties.vendorID,
           properties.properties.deviceID, properties.properties.deviceName, type);
   if (info->has_vulkan11 && info->has_pci_bus)
      fprintf(stderr, " %04x:%02x:%02x.%x", ext_pci_properties.pciDomain, ext_pci_properties.pciBus,
              ext_pci_properties.pciDevice, ext_pci_properties.pciFunction);
   fprintf(stderr, "\n");
}

static VkResult
device_select_EnumeratePhysicalDevices(VkInstance instance, uint32_t *pPhysicalDeviceCount,
                                       VkPhysicalDevice *pPhysicalDevices)
{
   struct instance_info *info = device_select_layer_get_instance(instance);
   uint32_t physical_device_count = 0;
   uint32_t selected_physical_device_count = 0;
   bool expose_only_one_dev = false;
   if (info->zink && info->xwayland)
      return info->EnumeratePhysicalDevices(instance, pPhysicalDeviceCount, pPhysicalDevices);
   VkResult result = info->EnumeratePhysicalDevices(instance, &physical_device_count, NULL);
   VK_OUTARRAY_MAKE_TYPED(VkPhysicalDevice, out, pPhysicalDevices, pPhysicalDeviceCount);
   if (result != VK_SUCCESS)
      return result;

   VkPhysicalDevice *physical_devices =
      (VkPhysicalDevice *)calloc(sizeof(VkPhysicalDevice), physical_device_count);
   VkPhysicalDevice *selected_physical_devices =
      (VkPhysicalDevice *)calloc(sizeof(VkPhysicalDevice), physical_device_count);

   if (!physical_devices || !selected_physical_devices) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto out;
   }

   result = info->EnumeratePhysicalDevices(instance, &physical_device_count, physical_devices);
   if (result != VK_SUCCESS)
      goto out;

   for (unsigned i = 0; i < physical_device_count; i++) {
      uint32_t count;
      info->EnumerateDeviceExtensionProperties(physical_devices[i], NULL, &count, NULL);
      if (count > 0) {
         VkExtensionProperties *extensions = calloc(count, sizeof(VkExtensionProperties));
         if (info->EnumerateDeviceExtensionProperties(physical_devices[i], NULL, &count,
                                                      extensions) == VK_SUCCESS) {
            for (unsigned j = 0; j < count; j++) {
               if (!strcmp(extensions[j].extensionName, VK_EXT_PCI_BUS_INFO_EXTENSION_NAME))
                  info->has_pci_bus = true;
            }
         }
         free(extensions);
      }
   }
   if (info->debug || (info->selection && strcmp(info->selection, "list") == 0)) {
      fprintf(stderr, "selectable devices:\n");
      for (unsigned i = 0; i < physical_device_count; ++i)
         print_gpu(info, i, physical_devices[i]);

      if (info->selection && strcmp(info->selection, "list") == 0)
         exit(0);
   }

   unsigned selected_index =
      device_select_get_first(info, physical_device_count, physical_devices, &expose_only_one_dev);
   selected_physical_device_count = physical_device_count;
   selected_physical_devices[0] = physical_devices[selected_index];
   for (unsigned i = 0; i < physical_device_count - 1; ++i) {
      unsigned this_idx = i < selected_index ? i : i + 1;
      selected_physical_devices[i + 1] = physical_devices[this_idx];
   }

   if (selected_physical_device_count == 0) {
      fprintf(stderr, "WARNING: selected no devices with MESA_VK_DEVICE_SELECT\n");
   }

   assert(result == VK_SUCCESS);

   /* do not give multiple device option to app if force default device */
   if (info->force_default_device && selected_physical_device_count != 0)
      expose_only_one_dev = true;

   if (expose_only_one_dev)
      selected_physical_device_count = 1;

   for (unsigned i = 0; i < selected_physical_device_count; i++) {
      vk_outarray_append_typed(VkPhysicalDevice, &out, ent)
      {
         *ent = selected_physical_devices[i];
      }
   }
   result = vk_outarray_status(&out);
out:
   free(physical_devices);
   free(selected_physical_devices);
   return result;
}

static VkResult
device_select_EnumeratePhysicalDeviceGroups(VkInstance instance,
                                            uint32_t *pPhysicalDeviceGroupCount,
                                            VkPhysicalDeviceGroupProperties *pPhysicalDeviceGroups)
{
   struct instance_info *info = device_select_layer_get_instance(instance);
   uint32_t physical_device_group_count = 0;
   uint32_t selected_physical_device_group_count = 0;
   if (info->zink && info->xwayland)
      return info->EnumeratePhysicalDeviceGroups(instance, pPhysicalDeviceGroupCount,
                                                 pPhysicalDeviceGroups);
   VkResult result =
      info->EnumeratePhysicalDeviceGroups(instance, &physical_device_group_count, NULL);
   VK_OUTARRAY_MAKE_TYPED(VkPhysicalDeviceGroupProperties, out, pPhysicalDeviceGroups,
                          pPhysicalDeviceGroupCount);

   if (result != VK_SUCCESS)
      return result;

   VkPhysicalDeviceGroupProperties *physical_device_groups =
      (VkPhysicalDeviceGroupProperties *)calloc(sizeof(VkPhysicalDeviceGroupProperties),
                                                physical_device_group_count);
   VkPhysicalDeviceGroupProperties *selected_physical_device_groups =
      (VkPhysicalDeviceGroupProperties *)calloc(sizeof(VkPhysicalDeviceGroupProperties),
                                                physical_device_group_count);

   if (!physical_device_groups || !selected_physical_device_groups) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto out;
   }

   for (unsigned i = 0; i < physical_device_group_count; i++)
      physical_device_groups[i].sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GROUP_PROPERTIES;

   result = info->EnumeratePhysicalDeviceGroups(instance, &physical_device_group_count,
                                                physical_device_groups);
   if (result != VK_SUCCESS)
      goto out;

   /* just sort groups with CPU devices to the end? - assume nobody will mix these */
   int num_gpu_groups = 0;
   int num_cpu_groups = 0;
   selected_physical_device_group_count = physical_device_group_count;
   for (unsigned i = 0; i < physical_device_group_count; i++) {
      bool group_has_cpu_device = false;
      for (unsigned j = 0; j < physical_device_groups[i].physicalDeviceCount; j++) {
         VkPhysicalDevice physical_device = physical_device_groups[i].physicalDevices[j];
         VkPhysicalDeviceProperties2 properties = (VkPhysicalDeviceProperties2){
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
         };
         info->GetPhysicalDeviceProperties(physical_device, &properties.properties);
         group_has_cpu_device = properties.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
      }

      if (group_has_cpu_device) {
         selected_physical_device_groups[physical_device_group_count - num_cpu_groups - 1] =
            physical_device_groups[i];
         num_cpu_groups++;
      } else {
         selected_physical_device_groups[num_gpu_groups] = physical_device_groups[i];
         num_gpu_groups++;
      }
   }

   assert(result == VK_SUCCESS);

   for (unsigned i = 0; i < selected_physical_device_group_count; i++) {
      vk_outarray_append_typed(VkPhysicalDeviceGroupProperties, &out, ent)
      {
         *ent = selected_physical_device_groups[i];
      }
   }
   result = vk_outarray_status(&out);
out:
   free(physical_device_groups);
   free(selected_physical_device_groups);
   return result;
}

static void (*get_instance_proc_addr(VkInstance instance, const char *name))()
{
   if (strcmp(name, "vkGetInstanceProcAddr") == 0)
      return (void (*)())get_instance_proc_addr;
   if (strcmp(name, "vkCreateInstance") == 0)
      return (void (*)())device_select_CreateInstance;
   if (strcmp(name, "vkDestroyInstance") == 0)
      return (void (*)())device_select_DestroyInstance;
   if (strcmp(name, "vkEnumeratePhysicalDevices") == 0)
      return (void (*)())device_select_EnumeratePhysicalDevices;
   if (strcmp(name, "vkEnumeratePhysicalDeviceGroups") == 0)
      return (void (*)())device_select_EnumeratePhysicalDeviceGroups;

   struct instance_info *info = device_select_layer_get_instance(instance);
   return info->GetInstanceProcAddr(instance, name);
}

PUBLIC VkResult
vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface *pVersionStruct)
{
   if (pVersionStruct->loaderLayerInterfaceVersion < 2)
      return VK_ERROR_INITIALIZATION_FAILED;
   pVersionStruct->loaderLayerInterfaceVersion = 2;

   pVersionStruct->pfnGetInstanceProcAddr = get_instance_proc_addr;

   return VK_SUCCESS;
}
