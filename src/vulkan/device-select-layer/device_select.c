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

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util/macros.h"
#include "device_select.h"

static bool
fill_drm_device_info(const struct instance_info *info, struct device_pci_info *drm_device,
                     VkPhysicalDevice device)
{
   VkPhysicalDevicePCIBusInfoPropertiesEXT ext_pci_properties =
      (VkPhysicalDevicePCIBusInfoPropertiesEXT){
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT};

   VkPhysicalDeviceProperties2 properties =
      (VkPhysicalDeviceProperties2){.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};

   if (info->has_vulkan11 && info->has_pci_bus)
      properties.pNext = &ext_pci_properties;
   device_select_get_properties(info, device, &properties);

   drm_device->device_type = properties.properties.deviceType;
   drm_device->dev_info.vendor_id = properties.properties.vendorID;
   drm_device->dev_info.device_id = properties.properties.deviceID;
   if (info->has_vulkan11 && info->has_pci_bus) {
      drm_device->has_bus_info = true;
      drm_device->bus_info.domain = ext_pci_properties.pciDomain;
      drm_device->bus_info.bus = ext_pci_properties.pciBus;
      drm_device->bus_info.dev = ext_pci_properties.pciDevice;
      drm_device->bus_info.func = ext_pci_properties.pciFunction;
   }
   return drm_device->device_type == VK_PHYSICAL_DEVICE_TYPE_CPU;
}

static int
device_select_find_explicit_default(struct device_pci_info *pci_infos, uint32_t device_count,
                                    const char *selection)
{
   int default_idx = -1;
   unsigned vendor_id, device_id;
   int matched = sscanf(selection, "%x:%x", &vendor_id, &device_id);
   if (matched != 2)
      return default_idx;

   for (unsigned i = 0; i < device_count; ++i) {
      if (pci_infos[i].dev_info.vendor_id == vendor_id &&
          pci_infos[i].dev_info.device_id == device_id)
         default_idx = i;
   }
   return default_idx;
}

static int
device_select_find_typed_default(struct device_pci_info *pci_infos, uint32_t device_count,
                                 const char *selection)
{
   static struct {
      const char *name;
      VkPhysicalDeviceType type;
   } names[] = {
      {"other", VK_PHYSICAL_DEVICE_TYPE_OTHER},
      {"integrated", VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU},
      {"igpu", VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU},
      {"discrete", VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU},
      {"dgpu", VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU},
      {"virtual", VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU},
      {"vgpu", VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU},
      {"cpu", VK_PHYSICAL_DEVICE_TYPE_CPU},
   };

   for (unsigned i = 0; i < ARRAY_SIZE(names); ++i) {
      if (strncasecmp(names[i].name, selection, strlen(names[i].name)) == 0) {
         for (unsigned j = 0; j < device_count; ++j) {
            if (pci_infos[j].device_type == names[i].type)
               return j;
         }
      }
   }

   return -1;
}

static int
device_select_find_dri_prime_tag_default(struct device_pci_info *pci_infos, uint32_t device_count,
                                         const char *dri_prime)
{
   int default_idx = -1;

   /* Drop the trailing '!' if present. */
   int ref = strlen("pci-xxxx_yy_zz_w");
   int n = strlen(dri_prime);
   if (n < ref)
      return default_idx;
   if (n == ref + 1 && dri_prime[n - 1] == '!')
      n--;

   for (unsigned i = 0; i < device_count; ++i) {
      char *tag = NULL;
      if (asprintf(&tag, "pci-%04x_%02x_%02x_%1u", pci_infos[i].bus_info.domain,
                   pci_infos[i].bus_info.bus, pci_infos[i].bus_info.dev,
                   pci_infos[i].bus_info.func) >= 0) {
         if (strncmp(dri_prime, tag, n) == 0)
            default_idx = i;
      }
      free(tag);
   }
   return default_idx;
}

static int
device_select_find_boot_vga_vid_did(struct device_pci_info *pci_infos, uint32_t device_count)
{
   char path[1024];
   int fd;
   int default_idx = -1;
   uint8_t boot_vga = 0;
   ssize_t size_ret;
#pragma pack(push, 1)
   struct id {
      uint16_t vid;
      uint16_t did;
   } id;
#pragma pack(pop)

   for (unsigned i = 0; i < 64; i++) {
      snprintf(path, 1023, "/sys/class/drm/card%d/device/boot_vga", i);
      fd = open(path, O_RDONLY);
      if (fd != -1) {
         uint8_t val;
         size_ret = read(fd, &val, 1);
         close(fd);
         if (size_ret == 1 && val == '1')
            boot_vga = 1;
      } else {
         return default_idx;
      }

      if (boot_vga) {
         snprintf(path, 1023, "/sys/class/drm/card%d/device/config", i);
         fd = open(path, O_RDONLY);
         if (fd != -1) {
            size_ret = read(fd, &id, 4);
            close(fd);
            if (size_ret != 4)
               return default_idx;
         } else {
            return default_idx;
         }
         break;
      }
   }

   if (!boot_vga)
      return default_idx;

   for (unsigned i = 0; i < device_count; ++i) {
      if (id.vid == pci_infos[i].dev_info.vendor_id && id.did == pci_infos[i].dev_info.device_id) {
         default_idx = i;
         break;
      }
   }

   return default_idx;
}

static int
device_select_find_boot_vga_default(struct device_pci_info *pci_infos, uint32_t device_count)
{
   char boot_vga_path[1024];
   int default_idx = -1;
   for (unsigned i = 0; i < device_count; ++i) {
      /* fallback to probing the pci bus boot_vga device. */
      snprintf(boot_vga_path, 1023, "/sys/bus/pci/devices/%04x:%02x:%02x.%x/boot_vga",
               pci_infos[i].bus_info.domain, pci_infos[i].bus_info.bus, pci_infos[i].bus_info.dev,
               pci_infos[i].bus_info.func);
      int fd = open(boot_vga_path, O_RDONLY);
      if (fd != -1) {
         uint8_t val;
         if (read(fd, &val, 1) == 1) {
            if (val == '1')
               default_idx = i;
         }
         close(fd);
      }
      if (default_idx != -1)
         break;
   }
   return default_idx;
}

static int
device_select_find_non_cpu(struct device_pci_info *pci_infos, uint32_t device_count)
{
   int default_idx = -1;

   /* pick first GPU device */
   for (unsigned i = 0; i < device_count; ++i) {
      if (pci_infos[i].device_type != VK_PHYSICAL_DEVICE_TYPE_CPU) {
         default_idx = i;
         break;
      }
   }
   return default_idx;
}

static int
find_non_cpu_skip(struct device_pci_info *pci_infos, uint32_t device_count, int skip_idx,
                  int skip_count)
{
   for (unsigned i = 0; i < device_count; ++i) {
      if (i == skip_idx)
         continue;
      if (pci_infos[i].device_type == VK_PHYSICAL_DEVICE_TYPE_CPU)
         continue;
      skip_count--;
      if (skip_count > 0)
         continue;

      return i;
   }
   return -1;
}

static bool
ends_with_exclamation_mark(const char *str)
{
   size_t n = strlen(str);
   return n > 1 && str[n - 1] == '!';
}

static int
get_selected(const struct instance_info *info, uint32_t count, struct device_pci_info *pci_infos,
             const char *dri_prime, bool *expose_only_one_dev)
{
   int default_idx = -1;

   if (info->selection)
      default_idx = device_select_find_typed_default(pci_infos, count, info->selection);
   if (default_idx != -1) {
      if (info->debug)
         fprintf(stderr,
                 "device-select: device_select_find_typed_default for MESA_VK_DEVICE_SELECT "
                 "selected %i\n",
                 default_idx);
      *expose_only_one_dev = ends_with_exclamation_mark(info->selection);
      return default_idx;
   }

   if (info->selection)
      default_idx = device_select_find_explicit_default(pci_infos, count, info->selection);
   if (default_idx != -1) {
      if (info->debug)
         fprintf(stderr,
                 "device-select: device_select_find_explicit_default for MESA_VK_DEVICE_SELECT "
                 "selected %i\n",
                 default_idx);
      *expose_only_one_dev = ends_with_exclamation_mark(info->selection);
      return default_idx;
   }

   if (!dri_prime)
      return default_idx;

   /* Try DRI_PRIME=vendor_id:device_id */
   default_idx = device_select_find_explicit_default(pci_infos, count, dri_prime);
   if (default_idx != -1) {
      if (info->debug)
         fprintf(stderr, "device-select: device_select_find_explicit_default selected %i\n",
                 default_idx);
      *expose_only_one_dev = ends_with_exclamation_mark(dri_prime);
      return default_idx;
   }

   /* Try DRI_PRIME=pci-xxxx_yy_zz_w */
   if (!info->has_vulkan11 && !info->has_pci_bus)
      fprintf(stderr, "device-select: cannot correctly use DRI_PRIME tag\n");
   else
      default_idx = device_select_find_dri_prime_tag_default(pci_infos, count, dri_prime);

   if (default_idx != -1) {
      if (info->debug)
         fprintf(stderr, "device-select: device_select_find_dri_prime_tag_default selected %i\n",
                 default_idx);
      *expose_only_one_dev = ends_with_exclamation_mark(dri_prime);
   }

   return default_idx;
}

static int
get_default(const struct instance_info *info, uint32_t count, struct device_pci_info *pci_infos)
{
   int default_idx = -1;

   bool has_cpu = false;
   for (unsigned i = 0; i < count; ++i)
      has_cpu |= pci_infos[i].device_type == VK_PHYSICAL_DEVICE_TYPE_CPU;

   if (default_idx == -1 && info->has_wayland) {
      default_idx = device_select_find_wayland_pci_default(pci_infos, count);
      if (info->debug && default_idx != -1)
         fprintf(stderr, "device-select: device_select_find_wayland_pci_default selected %i\n",
                 default_idx);
   }

   if (default_idx == -1 && info->has_xcb) {
      default_idx = device_select_find_xcb_pci_default(pci_infos, count);
      if (info->debug && default_idx != -1)
         fprintf(stderr, "device-select: device_select_find_xcb_pci_default selected %i\n",
                 default_idx);
   }

   if (default_idx == -1) {
      if (info->has_vulkan11 && info->has_pci_bus)
         default_idx = device_select_find_boot_vga_default(pci_infos, count);
      else
         default_idx = device_select_find_boot_vga_vid_did(pci_infos, count);
      if (info->debug && default_idx != -1)
         fprintf(stderr, "device-select: device_select_find_boot_vga selected %i\n", default_idx);
   }

   /* If no GPU has been selected so far, select the first non-CPU device. If none are available,
    * pick the first CPU device.
    */
   if (default_idx == -1) {
      default_idx = device_select_find_non_cpu(pci_infos, count);
      if (info->debug && default_idx != -1)
         fprintf(stderr, "device-select: device_select_find_non_cpu selected %i\n", default_idx);
   }

   if (default_idx == -1 && has_cpu)
      default_idx = 0;

   return default_idx;
}

uint32_t
device_select_get_first(const struct instance_info *info, uint32_t physical_device_count,
                        VkPhysicalDevice *pPhysicalDevices, bool *expose_only_one_dev)
{
   int default_idx = -1;
   int dri_prime_as_int = -1;
   int cpu_count = 0;
   if (info->dri_prime) {
      if (strchr(info->dri_prime, ':') == NULL)
         dri_prime_as_int = atoi(info->dri_prime);

      if (dri_prime_as_int < 0)
         dri_prime_as_int = 0;
   }

   struct device_pci_info *pci_infos =
      (struct device_pci_info *)calloc(physical_device_count, sizeof(struct device_pci_info));
   if (!pci_infos)
      return 0;

   for (unsigned i = 0; i < physical_device_count; ++i)
      cpu_count += fill_drm_device_info(info, &pci_infos[i], pPhysicalDevices[i]) ? 1 : 0;

   default_idx = get_selected(info, physical_device_count, pci_infos,
                              dri_prime_as_int ? NULL : info->dri_prime, expose_only_one_dev);
   if (default_idx == -1)
      default_idx = get_default(info, physical_device_count, pci_infos);

   /* DRI_PRIME=n handling - pick any other device than default. */
   if (dri_prime_as_int > 0 && info->debug)
      fprintf(stderr, "device-select: DRI_PRIME=%d, default_idx so far: %i\n", dri_prime_as_int,
              default_idx);
   if (dri_prime_as_int > 0 && (physical_device_count - cpu_count) > 1 &&
       (default_idx == 0 || default_idx == 1)) {
      default_idx =
         find_non_cpu_skip(pci_infos, physical_device_count, default_idx, dri_prime_as_int);
      if (default_idx != -1) {
         if (info->debug)
            fprintf(stderr, "device-select: find_non_cpu_skip selected %i\n", default_idx);
         *expose_only_one_dev = ends_with_exclamation_mark(info->dri_prime);
      }
   }

   free(pci_infos);
   return default_idx == -1 ? 0 : default_idx;
}
