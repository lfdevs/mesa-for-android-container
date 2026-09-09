/*
 * Copyright © 2026 lfdevs
 * SPDX-License-Identifier: MIT
 *
 * This compatibility shim lets Chromium's native Wayland backend use the
 * KGSL device exposed by a DRM-less PRoot container.  It is intended to be
 * loaded into Chromium with LD_PRELOAD, not installed as a system libdrm
 * replacement.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <xf86drm.h>

static const char *const kgsl_path = "/dev/kgsl-3d0";

#if defined(__GLIBC__) || defined(__BIONIC__)
typedef unsigned long tva_ioctl_request_t;
#else
/* musl declares ioctl(2)'s request argument as int. */
typedef int tva_ioctl_request_t;
#endif

static drmVersionPtr (*real_drmGetVersion)(int);
static void (*real_drmFreeVersion)(drmVersionPtr);
static int (*real_drmGetNodeTypeFromFd)(int);
static int (*real_drmGetDevices2)(uint32_t, drmDevicePtr[], int);
static int (*real_drmGetDevice2)(int, uint32_t, drmDevicePtr *);
static int (*real_drmGetDevice)(int, drmDevicePtr *);
static void (*real_drmFreeDevice)(drmDevicePtr *);
static void (*real_drmFreeDevices)(drmDevicePtr[], int);
static int (*real_drmGetDeviceFromDevId)(dev_t, uint32_t, drmDevicePtr *);
static int (*real_drmGetNodeTypeFromDevId)(dev_t);
static int (*real_drmIoctl)(int, unsigned long, void *);
static int (*real_ioctl)(int, tva_ioctl_request_t, ...);
static char *(*real_drmGetDeviceNameFromFd2)(int);
static char *(*real_drmGetDeviceNameFromFd)(int);
static char *(*real_drmGetRenderDeviceNameFromFd)(int);
static char *(*real_drmGetPrimaryDeviceNameFromFd)(int);

static bool is_kgsl_fd(int fd);

static void
init_real(void)
{
#define LOAD(name) \
   do { \
      if (!real_##name) \
         real_##name = dlsym(RTLD_NEXT, #name); \
   } while (0)
   LOAD(drmGetVersion);
   LOAD(drmFreeVersion);
   LOAD(drmGetNodeTypeFromFd);
   LOAD(drmGetDevices2);
   LOAD(drmGetDevice2);
   LOAD(drmGetDevice);
   LOAD(drmFreeDevice);
   LOAD(drmFreeDevices);
   LOAD(drmGetDeviceFromDevId);
   LOAD(drmGetNodeTypeFromDevId);
   LOAD(drmIoctl);
   LOAD(drmGetDeviceNameFromFd2);
   LOAD(drmGetDeviceNameFromFd);
   LOAD(drmGetRenderDeviceNameFromFd);
   LOAD(drmGetPrimaryDeviceNameFromFd);
#undef LOAD
}

static void
fill_fake_drm_version(struct drm_version *version)
{
   int name_capacity = version->name_len;

   version->version_major = 1;
   version->version_minor = 0;
   version->version_patchlevel = 0;
   version->name_len = 4;
   version->date_len = 0;
   version->desc_len = 0;
   if (version->name && name_capacity >= 4)
      memcpy(version->name, "kgsl", 4);
}

/* Chromium's Wayland Ozone code issues this ioctl directly instead of going
 * through libdrm.  KGSL is not a DRM device, so provide only the version
 * response needed by its render-node handle validation. */
int
ioctl(int fd, tva_ioctl_request_t request, ...)
{
   init_real();

   va_list ap;
   va_start(ap, request);
   void *arg = va_arg(ap, void *);
   va_end(ap);

   if (is_kgsl_fd(fd) &&
       request == (tva_ioctl_request_t) DRM_IOCTL_VERSION && arg) {
      fill_fake_drm_version(arg);
      if (getenv("TERMUX_VA_DRM_SHIM_LOG"))
         fprintf(stderr, "tva-drm-shim: ioctl DRM_IOCTL_VERSION fd=%d\n", fd);
      return 0;
   }

   if (!real_ioctl)
      real_ioctl = dlsym(RTLD_NEXT, "ioctl");
   if (real_ioctl)
      return real_ioctl(fd, request, arg);

   errno = ENOSYS;
   return -1;
}

static bool
is_kgsl_fd(int fd)
{
   struct stat fd_st;
   struct stat kgsl_st;

   return fstat(fd, &fd_st) == 0 && stat(kgsl_path, &kgsl_st) == 0 &&
          S_ISCHR(fd_st.st_mode) && fd_st.st_rdev == kgsl_st.st_rdev;
}

static bool
is_kgsl_dev(dev_t dev)
{
   struct stat st;

   return stat(kgsl_path, &st) == 0 && st.st_rdev == dev;
}

int
drmIoctl(int fd, unsigned long request, void *arg)
{
   init_real();
   if (is_kgsl_fd(fd) && request == DRM_IOCTL_VERSION && arg) {
      fill_fake_drm_version(arg);
      return 0;
   }

   if (real_drmIoctl)
      return real_drmIoctl(fd, request, arg);

   errno = ENOSYS;
   return -1;
}

static drmDevicePtr
make_fake_device(void)
{
   drmDevicePtr device = calloc(1, sizeof(*device));
   if (!device)
      return NULL;

   device->nodes = calloc(DRM_NODE_MAX, sizeof(*device->nodes));
   device->businfo.pci = calloc(1, sizeof(*device->businfo.pci));
   device->deviceinfo.pci = calloc(1, sizeof(*device->deviceinfo.pci));
   if (!device->nodes || !device->businfo.pci || !device->deviceinfo.pci)
      goto fail;

   device->nodes[DRM_NODE_RENDER] = strdup(kgsl_path);
   device->nodes[DRM_NODE_PRIMARY] = strdup(kgsl_path);
   if (!device->nodes[DRM_NODE_RENDER] || !device->nodes[DRM_NODE_PRIMARY])
      goto fail;

   /* Chromium's VA-API discovery currently filters out non-PCI devices.
    * Keep the KGSL path while presenting the same neutral PCI identity that
    * the container's ANGLE setup uses. */
   device->businfo.pci->domain = 0;
   device->businfo.pci->bus = 0;
   device->businfo.pci->dev = 0;
   device->businfo.pci->func = 0;
   device->deviceinfo.pci->vendor_id = 0;
   device->deviceinfo.pci->device_id = 0;
   device->available_nodes = (1 << DRM_NODE_RENDER) | (1 << DRM_NODE_PRIMARY);
   device->bustype = DRM_BUS_PCI;
   return device;

fail:
   free(device->deviceinfo.pci);
   free(device->businfo.pci);
   if (device->nodes) {
      free(device->nodes[DRM_NODE_RENDER]);
      free(device->nodes[DRM_NODE_PRIMARY]);
      free(device->nodes);
   }
   free(device);
   return NULL;
}

static bool
is_fake_device(drmDevicePtr device)
{
   return device && device->nodes && device->nodes[DRM_NODE_RENDER] &&
          strcmp(device->nodes[DRM_NODE_RENDER], kgsl_path) == 0;
}

static void
free_fake_device(drmDevicePtr device)
{
   if (!device)
      return;

   free(device->deviceinfo.pci);
   free(device->businfo.pci);
   if (device->nodes) {
      free(device->nodes[DRM_NODE_RENDER]);
      free(device->nodes[DRM_NODE_PRIMARY]);
      free(device->nodes);
   }
   free(device);
}

static int
fake_device_result(drmDevicePtr *out)
{
   if (!out)
      return -EINVAL;

   *out = make_fake_device();
   if (!*out)
      return -ENOMEM;

   if (getenv("TERMUX_VA_DRM_SHIM_LOG"))
      fprintf(stderr, "tva-drm-shim: exposing %s as DRM render node\n",
              kgsl_path);
   return 0;
}

int
drmGetDevices2(uint32_t flags, drmDevicePtr devices[], int max_devices)
{
   (void)flags;
   init_real();

   /* Chromium's GPU process may not preserve all application environment
    * variables.  The presence of the KGSL node is therefore the opt-in for
    * this process-local compatibility path. */
   struct stat st;
   int stat_rc = stat(kgsl_path, &st);
   if (getenv("TERMUX_VA_DRM_SHIM_LOG"))
      fprintf(stderr, "tva-drm-shim: drmGetDevices2 stat=%d errno=%d max=%d\n",
              stat_rc, errno, max_devices);
   if (stat_rc != 0) {
      if (real_drmGetDevices2)
         return real_drmGetDevices2(flags, devices, max_devices);
      errno = ENOSYS;
      return -1;
   }

   if (!devices || max_devices == 0)
      return 1;
   if (max_devices < 1)
      return 0;

   devices[0] = make_fake_device();
   return devices[0] ? 1 : -ENOMEM;
}

int
drmGetDevices(drmDevicePtr devices[], int max_devices)
{
   return drmGetDevices2(0, devices, max_devices);
}

void
drmFreeDevices(drmDevicePtr devices[], int count)
{
   init_real();
   for (int i = 0; i < count; i++) {
      if (!devices || !devices[i])
         continue;
      if (is_fake_device(devices[i]))
         free_fake_device(devices[i]);
      else if (real_drmFreeDevice)
         real_drmFreeDevice(&devices[i]);
   }
}

void
drmFreeDevice(drmDevicePtr *device)
{
   init_real();
   if (device && is_fake_device(*device)) {
      free_fake_device(*device);
      *device = NULL;
      return;
   }

   if (real_drmFreeDevice)
      real_drmFreeDevice(device);
}

int
drmGetDeviceFromDevId(dev_t dev_id, uint32_t flags, drmDevicePtr *device)
{
   init_real();
   if (is_kgsl_dev(dev_id))
      return fake_device_result(device);
   if (real_drmGetDeviceFromDevId)
      return real_drmGetDeviceFromDevId(dev_id, flags, device);
   errno = ENOSYS;
   return -1;
}

int
drmGetNodeTypeFromDevId(dev_t dev_id)
{
   init_real();
   if (is_kgsl_dev(dev_id))
      return DRM_NODE_RENDER;
   if (real_drmGetNodeTypeFromDevId)
      return real_drmGetNodeTypeFromDevId(dev_id);
   errno = ENOSYS;
   return -1;
}

int
drmGetDevice2(int fd, uint32_t flags, drmDevicePtr *device)
{
   init_real();
   if (is_kgsl_fd(fd))
      return fake_device_result(device);
   if (real_drmGetDevice2)
      return real_drmGetDevice2(fd, flags, device);
   errno = ENOSYS;
   return -1;
}

int
drmGetDevice(int fd, drmDevicePtr *device)
{
   return drmGetDevice2(fd, 0, device);
}

int
drmGetNodeTypeFromFd(int fd)
{
   init_real();
   if (is_kgsl_fd(fd))
      return DRM_NODE_RENDER;
   if (real_drmGetNodeTypeFromFd)
      return real_drmGetNodeTypeFromFd(fd);
   errno = ENOSYS;
   return -1;
}

static char *
device_name(int fd)
{
   return is_kgsl_fd(fd) ? strdup(kgsl_path) : NULL;
}

char *
drmGetDeviceNameFromFd2(int fd)
{
   init_real();
   char *name = device_name(fd);
   return name ? name : (real_drmGetDeviceNameFromFd2 ?
                         real_drmGetDeviceNameFromFd2(fd) : NULL);
}

char *
drmGetDeviceNameFromFd(int fd)
{
   init_real();
   char *name = device_name(fd);
   return name ? name : (real_drmGetDeviceNameFromFd ?
                         real_drmGetDeviceNameFromFd(fd) : NULL);
}

char *
drmGetRenderDeviceNameFromFd(int fd)
{
   init_real();
   char *name = device_name(fd);
   return name ? name : (real_drmGetRenderDeviceNameFromFd ?
                         real_drmGetRenderDeviceNameFromFd(fd) : NULL);
}

char *
drmGetPrimaryDeviceNameFromFd(int fd)
{
   init_real();
   char *name = device_name(fd);
   return name ? name : (real_drmGetPrimaryDeviceNameFromFd ?
                         real_drmGetPrimaryDeviceNameFromFd(fd) : NULL);
}

drmVersionPtr
drmGetVersion(int fd)
{
   init_real();
   if (!is_kgsl_fd(fd))
      return real_drmGetVersion ? real_drmGetVersion(fd) : NULL;

   drmVersionPtr version = calloc(1, sizeof(*version));
   if (!version)
      return NULL;

   version->version_major = 1;
   version->name_len = 4;
   version->name = strdup("kgsl");
   if (!version->name) {
      free(version);
      return NULL;
   }
   return version;
}

void
drmFreeVersion(drmVersionPtr version)
{
   init_real();
   if (version && version->name && version->name_len == 4 &&
       memcmp(version->name, "kgsl", 4) == 0) {
      free(version->name);
      free(version);
      return;
   }

   if (real_drmFreeVersion)
      real_drmFreeVersion(version);
}
