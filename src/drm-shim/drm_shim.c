/*
 * Copyright © 2018 Broadcom
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/**
 * @file
 *
 * Implements wrappers of libc functions to fake having a DRM device that
 * isn't actually present in the kernel.
 */

/* Prevent glibc from defining open64 when we want to alias it. */
#undef _FILE_OFFSET_BITS
#undef _TIME_BITS
#define _LARGEFILE64_SOURCE

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <stdarg.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <dirent.h>
#include <c11/threads.h>
#include <drm-uapi/drm.h>

#include "util/anon_file.h"
#include "util/set.h"
#include "util/simple_mtx.h"
#include "util/u_debug.h"
#include "drm_shim.h"

#define REAL_FUNCTION_POINTER(x) __typeof__(x) *real_##x

static simple_mtx_t shim_lock = SIMPLE_MTX_INITIALIZER;
struct set *opendir_set;
bool drm_shim_debug;

/* If /dev/dri doesn't exist, we'll need an arbitrary pointer that wouldn't be
 * returned by any other opendir() call so we can return just our fake node.
 */
DIR *fake_dev_dri = (void *)&opendir_set;

REAL_FUNCTION_POINTER(access);
REAL_FUNCTION_POINTER(close);
REAL_FUNCTION_POINTER(closedir);
REAL_FUNCTION_POINTER(dup);
REAL_FUNCTION_POINTER(fcntl);
REAL_FUNCTION_POINTER(fopen);
REAL_FUNCTION_POINTER(ioctl);
REAL_FUNCTION_POINTER(mmap);
REAL_FUNCTION_POINTER(mmap64);
REAL_FUNCTION_POINTER(open);
REAL_FUNCTION_POINTER(opendir);
REAL_FUNCTION_POINTER(readdir);
REAL_FUNCTION_POINTER(readdir64);
REAL_FUNCTION_POINTER(readlink);
REAL_FUNCTION_POINTER(realpath);

#define HAS_XSTAT __GLIBC__ == 2 && __GLIBC_MINOR__ < 33

#if HAS_XSTAT
REAL_FUNCTION_POINTER(__xstat);
REAL_FUNCTION_POINTER(__xstat64);
REAL_FUNCTION_POINTER(__fxstat);
REAL_FUNCTION_POINTER(__fxstat64);
#else
REAL_FUNCTION_POINTER(stat);
REAL_FUNCTION_POINTER(stat64);
REAL_FUNCTION_POINTER(fstat);
REAL_FUNCTION_POINTER(fstat64);
#endif

#define STRINGIZE2(x) #x
#define STRINGIZE(x) STRINGIZE2(x)

static char render_node_dir[] = "/dev/dri/";
static const char *render_node_path = "/dev/dri/renderD128";
static const char *render_node_dirent_name = "renderD128";
static const char *device_path = "/sys/dev/char/" STRINGIZE(DRM_MAJOR) ":128/device";
const int render_node_minor = 128;

struct file_override {
   const char *path;
   char *contents;
   bool is_link;
};
static struct file_override file_overrides[20];
static int file_overrides_count;

static int
nfvasprintf(char **restrict strp, const char *restrict fmt, va_list ap)
{
   int ret = vasprintf(strp, fmt, ap);
   assert(ret >= 0);
   return ret;
}

static int
nfasprintf(char **restrict strp, const char *restrict fmt, ...)
{
   va_list ap;
   va_start(ap, fmt);
   int ret = nfvasprintf(strp, fmt, ap);
   va_end(ap);
   return ret;
}

static void *get_function_pointer(const char *name)
{
   void *func = dlsym(RTLD_NEXT, name);
   if (!func) {
      fprintf(stderr, "Failed to resolve %s\n", name);
      abort();
   }
   return func;
}

#define GET_FUNCTION_POINTER(x) real_##x = get_function_pointer(#x)

void
drm_shim_override_file(const char *contents, const char *path_format, ...)
{
   assert(file_overrides_count < ARRAY_SIZE(file_overrides));

   char *path;
   va_list ap;
   va_start(ap, path_format);
   nfvasprintf(&path, path_format, ap);
   va_end(ap);

   struct file_override *override = &file_overrides[file_overrides_count++];
   override->path = path;
   override->contents = strdup(contents);
}

void
drm_shim_override_link(const char *target, const char *path_format, ...)
{
   assert(file_overrides_count < ARRAY_SIZE(file_overrides));

   char *path;
   va_list ap;
   va_start(ap, path_format);
   nfvasprintf(&path, path_format, ap);
   va_end(ap);

   struct file_override *override = &file_overrides[file_overrides_count++];
   override->path = path;
   override->contents = strdup(target);
   override->is_link = true;
}

static uint32_t inited = 0;
static simple_mtx_t init_lock = SIMPLE_MTX_INITIALIZER;

static void
get_function_pointers(void)
{
   GET_FUNCTION_POINTER(access);
   GET_FUNCTION_POINTER(close);
   GET_FUNCTION_POINTER(closedir);
   GET_FUNCTION_POINTER(dup);
   GET_FUNCTION_POINTER(fcntl);
   GET_FUNCTION_POINTER(fopen);
   GET_FUNCTION_POINTER(ioctl);
   GET_FUNCTION_POINTER(mmap);
   GET_FUNCTION_POINTER(mmap64);
   GET_FUNCTION_POINTER(open);
   GET_FUNCTION_POINTER(opendir);
   GET_FUNCTION_POINTER(readdir);
   GET_FUNCTION_POINTER(readdir64);
   GET_FUNCTION_POINTER(readlink);
   GET_FUNCTION_POINTER(realpath);

#if HAS_XSTAT
   GET_FUNCTION_POINTER(__xstat);
   GET_FUNCTION_POINTER(__xstat64);
   GET_FUNCTION_POINTER(__fxstat);
   GET_FUNCTION_POINTER(__fxstat64);
#else
   GET_FUNCTION_POINTER(stat);
   GET_FUNCTION_POINTER(stat64);
   GET_FUNCTION_POINTER(fstat);
   GET_FUNCTION_POINTER(fstat64);
#endif
}

bool
drm_shim_inited(void)
{
   return p_atomic_read(&inited);
}

static void
destroy_shim(void)
{
   _mesa_set_destroy(opendir_set, NULL);

   file_overrides_count = 0;
   p_atomic_set(&inited, 0);
}

/* Initialization, which will be called from the first general library call
 * that might need to be wrapped with the shim.
 */
static void
init_shim(void)
{
   /* Fast path once init has been completed. */
   if (p_atomic_read(&inited))
      return;

   /* Re-entry from the same thread: drm_shim_device_init() and its descendents
    * like drm_shim_driver_init() might call glibc functions that would go to
    * one of our wrappers and land back here.  We just need to be sure that
    * enough of the globals are set up to complete such calls before we call
    * down -- they don't need to get anything actually interposed in terms of
    * the device paths.
    *
    * A thread-local variable is used for this recursion check, since simple_mtx
    * doesn't support recursion.
    */
   static thread_local bool in_init = false;
   if (in_init)
      return;

   simple_mtx_lock(&init_lock);
   if (p_atomic_read(&inited)) {
      simple_mtx_unlock(&init_lock);
      return;
   }
   in_init = true;

   get_function_pointers();

   drm_shim_debug = debug_get_bool_option("DRM_SHIM_DEBUG", false);

   opendir_set = _mesa_set_create(NULL,
                                  _mesa_hash_string,
                                  _mesa_key_string_equal);

   if (drm_shim_debug) {
      fprintf(stderr, "Initializing DRM shim on %s\n",
              render_node_path);
   }

   drm_shim_device_init();

   atexit(destroy_shim);

   p_atomic_set(&inited, 1);
   in_init = false;
   simple_mtx_unlock(&init_lock);
}

static bool is_drm_device_path(const char *path)
{
   if (render_node_minor == -1)
      return false;

   static const char *drm_device_path_prefix = "/sys/dev/char/" STRINGIZE(DRM_MAJOR) ":";
   if (strncmp(path, drm_device_path_prefix, strlen(drm_device_path_prefix)) == 0)
      return true;

   /* String starts with /dev/dri/ */
   if (strncmp(path, render_node_dir, sizeof(render_node_dir) - 1) == 0)
      return true;

   return false;
}

static bool hide_drm_device_path(const char *path)
{
   /* If the path looks like our fake render node device, then don't hide it.
    */
   if (strncmp(path, device_path, strlen(device_path)) == 0 ||
       strcmp(path, render_node_path) == 0)
      return false;

   /* String looks like a device but is not the fake render node.
    * We want to hide all other drm devices for the shim.
    */
   return is_drm_device_path(path);
}

static int file_override_open(const char *path)
{
   for (int i = 0; i < file_overrides_count; i++) {
      if (strcmp(file_overrides[i].path, path) == 0) {
         if (file_overrides[i].is_link) {
            return file_override_open(file_overrides[i].contents);
         }
         int fd = os_create_anonymous_file(0, "shim file");
         write(fd, file_overrides[i].contents,
               strlen(file_overrides[i].contents));
         lseek(fd, 0, SEEK_SET);
         return fd;
      }
   }

   return -1;
}

/* Override libdrm's reading of various sysfs files for device enumeration. */
PUBLIC FILE *fopen(const char *path, const char *mode)
{
   init_shim();

   int fd = file_override_open(path);
   if (fd >= 0)
      return fdopen(fd, "r");

   return real_fopen(path, mode);
}
PUBLIC FILE *fopen64(const char *path, const char *mode)
   __attribute__((alias("fopen")));

/* Intercepts access(render_node_path) to trick drmGetMinorType */
PUBLIC int access(const char *path, int mode)
{
   init_shim();

   if (hide_drm_device_path(path)) {
      errno = ENOENT;
      return -1;
   }

   if (strcmp(path, render_node_path) != 0)
      return real_access(path, mode);

   return 0;
}

/* Intercepts open(render_node_path) to redirect it to the simulator. */
PUBLIC int open(const char *path, int flags, ...)
{
   init_shim();

   va_list ap;
   va_start(ap, flags);
   mode_t mode = va_arg(ap, mode_t);
   va_end(ap);

   int fd = file_override_open(path);
   if (fd >= 0)
      return fd;

   if (hide_drm_device_path(path)) {
      errno = ENOENT;
      return -1;
   }

   if (strcmp(path, render_node_path) != 0)
      return real_open(path, flags, mode);

   fd = real_open("/dev/null", O_RDWR, 0);

   drm_shim_fd_register(fd, NULL);

   return fd;
}
PUBLIC int open64(const char*, int, ...) __attribute__((alias("open")));

/* __open64_2 isn't declared unless _FORTIFY_SOURCE is defined. */
PUBLIC int __open64_2(const char *path, int flags);
PUBLIC int __open64_2(const char *path, int flags)
{
   return open(path, flags, 0);
}

PUBLIC int close(int fd)
{
   init_shim();

   drm_shim_fd_unregister(fd);

   return real_close(fd);
}

#if HAS_XSTAT
/* Fakes stat to return character device stuff for our fake render node. */
PUBLIC int __xstat(int ver, const char *path, struct stat *st)
{
   init_shim();

   /* Note: call real stat if we're in the process of probing for a free
    * render node!
    */
   if (render_node_minor == -1)
      return real___xstat(ver, path, st);

   if (hide_drm_device_path(path)) {
      errno = ENOENT;
      return -1;
   }

   /* Fool libdrm's probe of whether the /sys dir for this char dev is
    * there.
    */
   char *sys_dev_drm_dir;
   nfasprintf(&sys_dev_drm_dir,
              "/sys/dev/char/%d:%d/device/drm",
              DRM_MAJOR, render_node_minor);
   if (strcmp(path, sys_dev_drm_dir) == 0) {
      free(sys_dev_drm_dir);
      return 0;
   }
   free(sys_dev_drm_dir);

   if (strcmp(path, render_node_path) != 0)
      return real___xstat(ver, path, st);

   memset(st, 0, sizeof(*st));
   st->st_rdev = makedev(DRM_MAJOR, render_node_minor);
   st->st_mode = S_IFCHR;

   return 0;
}

/* Fakes stat to return character device stuff for our fake render node. */
PUBLIC int __xstat64(int ver, const char *path, struct stat64 *st)
{
   init_shim();

   /* Note: call real stat if we're in the process of probing for a free
    * render node!
    */
   if (render_node_minor == -1)
      return real___xstat64(ver, path, st);

   if (hide_drm_device_path(path)) {
      errno = ENOENT;
      return -1;
   }

   /* Fool libdrm's probe of whether the /sys dir for this char dev is
    * there.
    */
   char *sys_dev_drm_dir;
   nfasprintf(&sys_dev_drm_dir,
              "/sys/dev/char/%d:%d/device/drm",
              DRM_MAJOR, render_node_minor);
   if (strcmp(path, sys_dev_drm_dir) == 0) {
      free(sys_dev_drm_dir);
      return 0;
   }
   free(sys_dev_drm_dir);

   if (strcmp(path, render_node_path) != 0)
      return real___xstat64(ver, path, st);

   memset(st, 0, sizeof(*st));
   st->st_rdev = makedev(DRM_MAJOR, render_node_minor);
   st->st_mode = S_IFCHR;

   return 0;
}

/* Fakes fstat to return character device stuff for our fake render node. */
PUBLIC int __fxstat(int ver, int fd, struct stat *st)
{
   get_function_pointers();

   struct shim_fd *shim_fd = drm_shim_fd_lookup(fd);

   if (!shim_fd)
      return real___fxstat(ver, fd, st);

   memset(st, 0, sizeof(*st));
   st->st_rdev = makedev(DRM_MAJOR, render_node_minor);
   st->st_mode = S_IFCHR;

   return 0;
}

PUBLIC int __fxstat64(int ver, int fd, struct stat64 *st)
{
   get_function_pointers();

   struct shim_fd *shim_fd = drm_shim_fd_lookup(fd);

   if (!shim_fd)
      return real___fxstat64(ver, fd, st);

   memset(st, 0, sizeof(*st));
   st->st_rdev = makedev(DRM_MAJOR, render_node_minor);
   st->st_mode = S_IFCHR;

   return 0;
}

#else

PUBLIC int stat(const char* path, struct stat* stat_buf)
{
   init_shim();

   /* Note: call real stat if we're in the process of probing for a free
    * render node!
    */
   if (render_node_minor == -1)
      return real_stat(path, stat_buf);

   if (hide_drm_device_path(path)) {
      errno = ENOENT;
      return -1;
   }

   /* Fool libdrm's probe of whether the /sys dir for this char dev is
    * there.
    */
   char *sys_dev_drm_dir;
   nfasprintf(&sys_dev_drm_dir,
              "/sys/dev/char/%d:%d/device/drm",
              DRM_MAJOR, render_node_minor);
   if (strcmp(path, sys_dev_drm_dir) == 0) {
      free(sys_dev_drm_dir);
      return 0;
   }
   free(sys_dev_drm_dir);

   if (strcmp(path, render_node_path) != 0)
      return real_stat(path, stat_buf);

   memset(stat_buf, 0, sizeof(*stat_buf));
   stat_buf->st_rdev = makedev(DRM_MAJOR, render_node_minor);
   stat_buf->st_mode = S_IFCHR;

   return 0;
}

PUBLIC int stat64(const char* path, struct stat64* stat_buf)
{
   init_shim();

   /* Note: call real stat if we're in the process of probing for a free
    * render node!
    */
   if (render_node_minor == -1)
      return real_stat64(path, stat_buf);

   if (hide_drm_device_path(path)) {
      errno = ENOENT;
      return -1;
   }

   /* Fool libdrm's probe of whether the /sys dir for this char dev is
    * there.
    */
   char *sys_dev_drm_dir;
   nfasprintf(&sys_dev_drm_dir,
              "/sys/dev/char/%d:%d/device/drm",
              DRM_MAJOR, render_node_minor);
   if (strcmp(path, sys_dev_drm_dir) == 0) {
      free(sys_dev_drm_dir);
      return 0;
   }
   free(sys_dev_drm_dir);

   if (strcmp(path, render_node_path) != 0)
      return real_stat64(path, stat_buf);

   memset(stat_buf, 0, sizeof(*stat_buf));
   stat_buf->st_rdev = makedev(DRM_MAJOR, render_node_minor);
   stat_buf->st_mode = S_IFCHR;

   return 0;
}

PUBLIC int fstat(int fd, struct stat* stat_buf)
{
   get_function_pointers();

   struct shim_fd *shim_fd = drm_shim_fd_lookup(fd);

   if (!shim_fd)
      return real_fstat(fd, stat_buf);

   memset(stat_buf, 0, sizeof(*stat_buf));
   stat_buf->st_rdev = makedev(DRM_MAJOR, render_node_minor);
   stat_buf->st_mode = S_IFCHR;

   return 0;
}

PUBLIC int fstat64(int fd, struct stat64* stat_buf)
{
   get_function_pointers();

   struct shim_fd *shim_fd = drm_shim_fd_lookup(fd);

   if (!shim_fd)
      return real_fstat64(fd, stat_buf);

   memset(stat_buf, 0, sizeof(*stat_buf));
   stat_buf->st_rdev = makedev(DRM_MAJOR, render_node_minor);
   stat_buf->st_mode = S_IFCHR;

   return 0;
}
#endif

/* Tracks if the opendir was on /dev/dri. */
PUBLIC DIR *
opendir(const char *name)
{
   init_shim();

   DIR *dir = real_opendir(name);
   if (strcmp(name, "/dev/dri") == 0) {
      if (!dir) {
         /* If /dev/dri didn't exist, we still want to be able to return our
          * fake /dev/dri/render* even though we probably can't
          * mkdir("/dev/dri").  Return a fake DIR pointer for that.
          */
         dir = fake_dev_dri;
      }

      simple_mtx_lock(&shim_lock);
      _mesa_set_add(opendir_set, dir);
      simple_mtx_unlock(&shim_lock);
   }

   return dir;
}

/* If we're looking at /dev/dri, add our render node to the list
 * before the real entries in the directory.
 */
PUBLIC struct dirent *
readdir(DIR *dir)
{
   init_shim();

   struct dirent *ent = NULL;

   static struct dirent render_node_dirent = { 0 };

   simple_mtx_lock(&shim_lock);
   if (_mesa_set_search(opendir_set, dir)) {
      strcpy(render_node_dirent.d_name,
             render_node_dirent_name);
      render_node_dirent.d_type = DT_CHR;
      ent = &render_node_dirent;
      _mesa_set_remove_key(opendir_set, dir);
   }
   simple_mtx_unlock(&shim_lock);

   if (!ent && dir != fake_dev_dri)
      ent = real_readdir(dir);

   return ent;
}

/* If we're looking at /dev/dri, add our render node to the list
 * before the real entries in the directory.
 */
PUBLIC struct dirent64 *
readdir64(DIR *dir)
{
   init_shim();

   struct dirent64 *ent = NULL;

   static struct dirent64 render_node_dirent = { 0 };

   simple_mtx_lock(&shim_lock);
   if (_mesa_set_search(opendir_set, dir)) {
      strcpy(render_node_dirent.d_name,
             render_node_dirent_name);
      render_node_dirent.d_type = DT_CHR;
      ent = &render_node_dirent;
      _mesa_set_remove_key(opendir_set, dir);
   }
   simple_mtx_unlock(&shim_lock);

   if (!ent && dir != fake_dev_dri)
      ent = real_readdir64(dir);

   return ent;
}

/* Cleans up tracking of opendir("/dev/dri") */
PUBLIC int
closedir(DIR *dir)
{
   init_shim();

   simple_mtx_lock(&shim_lock);
   _mesa_set_remove_key(opendir_set, dir);
   simple_mtx_unlock(&shim_lock);

   if (dir != fake_dev_dri)
      return real_closedir(dir);
   else
      return 0;
}

/* Handles libdrm's readlink to figure out what kind of device we have. */
PUBLIC ssize_t
readlink(const char *path, char *buf, size_t size)
{
   /* Shortcut to the real readlink so that jemalloc can use this. */
   if (!is_drm_device_path(path)) {
      get_function_pointers();
      return real_readlink(path, buf, size);
   }

   init_shim();

   if (hide_drm_device_path(path)) {
      errno = ENOENT;
      return -1;
   }

   for (int i = 0; i < file_overrides_count; i++) {
      if (strcmp(file_overrides[i].path, path) == 0) {
         if (file_overrides[i].is_link) {
            strncpy(buf, file_overrides[i].contents, size);
            return strlen(buf) + 1;
         }
      }
   }

   return real_readlink(path, buf, size);
}

#if __USE_FORTIFY_LEVEL > 0 && !defined _CLANG_FORTIFY_DISABLE
/* Identical to readlink, but with buffer overflow check */
PUBLIC ssize_t
__readlink_chk(const char *path, char *buf, size_t size, size_t buflen)
{
   if (size > buflen)
      abort();
   return readlink(path, buf, size);
}
#endif

/* Handles libdrm's realpath to figure out what kind of device we have. */
PUBLIC char *
realpath(const char *path, char *resolved_path)
{
   init_shim();

   if (strcmp(path, device_path) != 0)
      return real_realpath(path, resolved_path);

   strcpy(resolved_path, path);

   return resolved_path;
}

/* Main entrypoint to DRM drivers: the ioctl syscall.  We send all ioctls on
 * our DRM fd to drm_shim_ioctl().
 */
PUBLIC int
ioctl(int fd, unsigned long request, ...)
{
   get_function_pointers();

   va_list ap;
   va_start(ap, request);
   void *arg = va_arg(ap, void *);
   va_end(ap);

   struct shim_fd *shim_fd = drm_shim_fd_lookup(fd);
   if (!shim_fd)
      return real_ioctl(fd, request, arg);

   return drm_shim_ioctl(fd, request, arg);
}

/* Gallium uses this to dup the incoming fd on gbm screen creation */
PUBLIC int
fcntl(int fd, int cmd, ...)
{
   get_function_pointers();

   struct shim_fd *shim_fd = drm_shim_fd_lookup(fd);

   va_list ap;
   va_start(ap, cmd);
   void *arg = va_arg(ap, void *);
   va_end(ap);

   int ret = real_fcntl(fd, cmd, arg);

   if (cmd == F_DUPFD || cmd == F_DUPFD_CLOEXEC) {
      if (shim_fd) {
         drm_shim_fd_register(ret, shim_fd);
      } else {
         /* x11_dri3_open / xcb_dri3_open_reply_fds will end up here. */
         drmVersionPtr ver = drmGetVersion(fd);
         if (ver) {
            drm_shim_fd_register(fd, NULL);
            drm_shim_fd_register(ret, drm_shim_fd_lookup(fd));
            drmFree(ver);
         }
      }
   }

   return ret;
}
PUBLIC int fcntl64(int, int, ...)
   __attribute__((alias("fcntl")));

/* I wrote this when trying to fix gallium screen creation, leaving it around
 * since it's probably good to have.
 */
PUBLIC int
dup(int fd)
{
   get_function_pointers();

   int ret = real_dup(fd);

   struct shim_fd *shim_fd = drm_shim_fd_lookup(fd);
   if (shim_fd && ret >= 0)
      drm_shim_fd_register(ret, shim_fd);

   return ret;
}

PUBLIC void *
mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
   get_function_pointers();

   struct shim_fd *shim_fd = drm_shim_fd_lookup(fd);
   if (shim_fd)
      return drm_shim_mmap(shim_fd, length, prot, flags, fd, offset);

   return real_mmap(addr, length, prot, flags, fd, offset);
}

PUBLIC void *
mmap64(void* addr, size_t length, int prot, int flags, int fd, off64_t offset)
{
   get_function_pointers();

   struct shim_fd *shim_fd = drm_shim_fd_lookup(fd);
   if (shim_fd)
      return drm_shim_mmap(shim_fd, length, prot, flags, fd, offset);

   return real_mmap64(addr, length, prot, flags, fd, offset);
}

void
drm_shim_pci_device_setup(uint16_t vendor_id, uint16_t device_id,
                          const char *pci_slot, const char *driver_name)
{
   shim_device.bus_type = DRM_BUS_PCI;
   shim_device.driver_name = driver_name;
   drm_shim_override_link("../../../../../bus/pci", "/sys/dev/char/%d:%d/device/subsystem",
                          DRM_MAJOR, render_node_minor);

   char *uevent_content, *vendor_id_str, *device_id_str;

   nfasprintf(&uevent_content,
            "DRIVER=%s\n"
            "PCI_CLASS=30000\n"
            "PCI_ID=%04x:%04x\n"
            "PCI_SUBSYS_ID=uevent_content\n"
            "PCI_SLOT_NAME=%s\n"
            "MODALIAS=pci:v00008086d00005916sv00001028sd0000075Bbc03sc00i00\n",
            driver_name, vendor_id, device_id, pci_slot);
   nfasprintf(&vendor_id_str, "0x%04x", vendor_id);
   nfasprintf(&device_id_str, "0x%04x", device_id);

   drm_shim_override_file(uevent_content,
                          "/sys/dev/char/%d:%d/device/uevent",
                          DRM_MAJOR, render_node_minor);
   drm_shim_override_file("0x0\n",
                          "/sys/dev/char/%d:%d/device/revision",
                          DRM_MAJOR, render_node_minor);
   drm_shim_override_file(vendor_id_str,
                          "/sys/dev/char/%d:%d/device/vendor",
                          DRM_MAJOR, render_node_minor);
   drm_shim_override_file(vendor_id_str,
                          "/sys/devices/pci0000:00/%s/vendor", pci_slot);
   drm_shim_override_file(device_id_str,
                          "/sys/dev/char/%d:%d/device/device",
                          DRM_MAJOR, render_node_minor);
   drm_shim_override_file(device_id_str,
                          "/sys/devices/pci0000:00/%s/device", pci_slot);
   drm_shim_override_file("0x1234",
                          "/sys/dev/char/%d:%d/device/subsystem_vendor",
                          DRM_MAJOR, render_node_minor);
   drm_shim_override_file("0x1234",
                          "/sys/devices/pci0000:00/%s/subsystem_vendor", pci_slot);
   drm_shim_override_file("0x1234",
                          "/sys/dev/char/%d:%d/device/subsystem_device",
                          DRM_MAJOR, render_node_minor);
   drm_shim_override_file("0x1234",
                          "/sys/devices/pci0000:00/%s/subsystem_device", pci_slot);

   free(uevent_content);
   free(vendor_id_str);
   free(device_id_str);
}

void
drm_shim_platform_device_setup(const char *driver_name, const char *fullname, const char *compatible)
{
   shim_device.bus_type = DRM_BUS_PLATFORM;
   shim_device.driver_name = driver_name;
   drm_shim_override_link("../../../../../bus/platform", "/sys/dev/char/%d:%d/device/subsystem",
                          DRM_MAJOR, render_node_minor);

   char *uevent_content;
   nfasprintf(&uevent_content, "DRIVER=%s\n"
                          "OF_FULLNAME=%s\n"
                          "OF_COMPATIBLE_0=%s\n"
                          "OF_COMPATIBLE_N=1\n", driver_name, fullname, compatible);

   drm_shim_override_file(uevent_content,
                          "/sys/dev/char/%d:%d/device/uevent", DRM_MAJOR, render_node_minor);

   free(uevent_content);
}
