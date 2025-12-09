/*
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
#include "util/macros.h"
#include <wayland-client.h>
#ifdef HAVE_BIND_WL_DISPLAY
#include "wayland-drm-client-protocol.h"
#endif
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <xf86drm.h>
#include "device_select.h"
#include "linux-dmabuf-unstable-v1-client-protocol.h"

struct device_select_wayland_info {
#ifdef HAVE_BIND_WL_DISPLAY
   struct wl_drm *wl_drm;
   drmDevicePtr drm_dev_info;
#endif

   struct zwp_linux_dmabuf_v1 *wl_dmabuf;
   struct zwp_linux_dmabuf_feedback_v1 *wl_dmabuf_feedback;
   drmDevicePtr dmabuf_dev_info;
};

#ifdef HAVE_BIND_WL_DISPLAY
static void
device_select_drm_handle_device(void *data, struct wl_drm *drm, const char *device)
{
   struct device_select_wayland_info *info = data;

   int fd = open(device, O_RDWR | O_CLOEXEC);
   if (fd == -1)
      return;

   drmGetDevice2(fd, 0, &info->drm_dev_info);
   close(fd);
}

static void
device_select_drm_handle_format(void *data, struct wl_drm *drm, uint32_t format)
{
   /* ignore this event */
}

static void
device_select_drm_handle_authenticated(void *data, struct wl_drm *drm)
{
   /* ignore this event */
}

static void
device_select_drm_handle_capabilities(void *data, struct wl_drm *drm, uint32_t value)
{
   /* ignore this event */
}

static const struct wl_drm_listener ds_drm_listener = {
   .device = device_select_drm_handle_device,
   .format = device_select_drm_handle_format,
   .authenticated = device_select_drm_handle_authenticated,
   .capabilities = device_select_drm_handle_capabilities,
};
#endif

static void
default_dmabuf_feedback_format_table(
   void *data, struct zwp_linux_dmabuf_feedback_v1 *zwp_linux_dmabuf_feedback_v1, int32_t fd,
   uint32_t size)
{

   /* ignore this event */
   close(fd);
}

static void
default_dmabuf_feedback_main_device(void *data,
                                    struct zwp_linux_dmabuf_feedback_v1 *dmabuf_feedback,
                                    struct wl_array *device)
{

   struct device_select_wayland_info *info = data;

   dev_t dev_id;
   assert(device->size == sizeof(dev_id));
   memcpy(&dev_id, device->data, device->size);

   drmGetDeviceFromDevId(dev_id, 0, &info->dmabuf_dev_info);
   return;
}

static void
default_dmabuf_feedback_tranche_target_device(void *data,
                                              struct zwp_linux_dmabuf_feedback_v1 *dmabuf_feedback,
                                              struct wl_array *device)
{
   /* ignore this event */
}

static void
default_dmabuf_feedback_tranche_flags(void *data,
                                      struct zwp_linux_dmabuf_feedback_v1 *dmabuf_feedback,
                                      uint32_t flags)
{
   /* ignore this event */
}

static void
default_dmabuf_feedback_tranche_formats(void *data,
                                        struct zwp_linux_dmabuf_feedback_v1 *dmabuf_feedback,
                                        struct wl_array *indices)
{
   /* ignore this event */
}

static void
default_dmabuf_feedback_tranche_done(void *data,
                                     struct zwp_linux_dmabuf_feedback_v1 *dmabuf_feedback)
{
   /* ignore this event */
}

static void
default_dmabuf_feedback_done(void *data, struct zwp_linux_dmabuf_feedback_v1 *dmabuf_feedback)
{
   /* ignore this event */
}

static const struct zwp_linux_dmabuf_feedback_v1_listener dmabuf_feedback_listener = {
   .format_table = default_dmabuf_feedback_format_table,
   .main_device = default_dmabuf_feedback_main_device,
   .tranche_target_device = default_dmabuf_feedback_tranche_target_device,
   .tranche_flags = default_dmabuf_feedback_tranche_flags,
   .tranche_formats = default_dmabuf_feedback_tranche_formats,
   .tranche_done = default_dmabuf_feedback_tranche_done,
   .done = default_dmabuf_feedback_done,
};

static void
device_select_registry_global(void *data, struct wl_registry *registry, uint32_t name,
                              const char *interface, uint32_t version)
{
   struct device_select_wayland_info *info = data;
#if HAVE_BIND_WL_DISPLAY
   if (strcmp(interface, wl_drm_interface.name) == 0) {
      info->wl_drm = wl_registry_bind(registry, name, &wl_drm_interface, MIN2(version, 2));
      wl_drm_add_listener(info->wl_drm, &ds_drm_listener, data);
   } else
#endif
      if (strcmp(interface, zwp_linux_dmabuf_v1_interface.name) == 0 &&
          version >= ZWP_LINUX_DMABUF_V1_GET_DEFAULT_FEEDBACK_SINCE_VERSION) {
      info->wl_dmabuf = wl_registry_bind(registry, name, &zwp_linux_dmabuf_v1_interface,
                                         ZWP_LINUX_DMABUF_V1_GET_DEFAULT_FEEDBACK_SINCE_VERSION);
      info->wl_dmabuf_feedback = zwp_linux_dmabuf_v1_get_default_feedback(info->wl_dmabuf);
      zwp_linux_dmabuf_feedback_v1_add_listener(info->wl_dmabuf_feedback, &dmabuf_feedback_listener,
                                                data);
   }
}

static void
device_select_registry_global_remove_cb(void *data, struct wl_registry *registry, uint32_t name)
{
}

int
device_select_find_wayland_pci_default(struct device_pci_info *devices, uint32_t device_count)
{
   struct wl_display *display;
   struct wl_registry *registry = NULL;
   unsigned default_idx = -1;
   struct device_select_wayland_info info = {};

   display = wl_display_connect(NULL);
   if (!display)
      return -1;

   registry = wl_display_get_registry(display);
   if (!registry) {
      wl_display_disconnect(display);
      return -1;
   }

   static const struct wl_registry_listener registry_listener = {
      device_select_registry_global, device_select_registry_global_remove_cb};

   wl_registry_add_listener(registry, &registry_listener, &info);
   wl_display_dispatch(display);
   wl_display_roundtrip(display);

   drmDevicePtr target = NULL;
   if (info.dmabuf_dev_info != NULL) {
      target = info.dmabuf_dev_info;
   }
#ifdef HAVE_BIND_WL_DISPLAY
   if (target == NULL && info.drm_dev_info != NULL) {
      target = info.drm_dev_info;
   }
#endif
   if (target == NULL) {
      goto done;
   }

   for (unsigned i = 0; i < device_count; i++) {
      if (devices[i].has_bus_info) {
         if (target->businfo.pci->domain == devices[i].bus_info.domain &&
             target->businfo.pci->bus == devices[i].bus_info.bus &&
             target->businfo.pci->dev == devices[i].bus_info.dev &&
             target->businfo.pci->func == devices[i].bus_info.func) {
            default_idx = i;
            break;
         }
      } else {
         if (target->deviceinfo.pci->vendor_id == devices[i].dev_info.vendor_id &&
             target->deviceinfo.pci->device_id == devices[i].dev_info.device_id) {
            default_idx = i;
            break;
         }
      }
   }

done:
   if (info.dmabuf_dev_info != NULL)
      drmFreeDevice(&info.dmabuf_dev_info);
#ifdef HAVE_BIND_WL_DISPLAY
   if (info.drm_dev_info != NULL)
      drmFreeDevice(&info.drm_dev_info);
#endif

   if (info.wl_dmabuf_feedback)
      zwp_linux_dmabuf_feedback_v1_destroy(info.wl_dmabuf_feedback);
   if (info.wl_dmabuf)
      zwp_linux_dmabuf_v1_destroy(info.wl_dmabuf);
#ifdef HAVE_BIND_WL_DISPLAY
   if (info.wl_drm)
      wl_drm_destroy(info.wl_drm);
#endif

   wl_registry_destroy(registry);
   wl_display_disconnect(display);
   return default_idx;
}
