/*
 * Copyright © 2012 Collabora, Ltd.
 * Copyright (C) 2025 Arm Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * Based on weston shared/os-compatibility.c
 */

#include "anon_file.h"
#include "detect_os.h"

#ifndef _WIN32

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>

#if defined(HAVE_MEMFD_CREATE) || defined(__FreeBSD__) || defined(__OpenBSD__)
#include <sys/mman.h>
#elif DETECT_OS_ANDROID
#include <sys/syscall.h>
#include <linux/memfd.h>
#endif

#if !(defined(__FreeBSD__) || DETECT_OS_ANDROID)
#include "log.h"
#include "os_misc.h"
#include <sys/stat.h>
#include <stdio.h>
#endif

#if !(defined(__FreeBSD__) || defined(HAVE_MKOSTEMP) || DETECT_OS_ANDROID)
static int
set_cloexec_or_close(int fd)
{
   long flags;

   if (fd == -1)
      return -1;

   flags = fcntl(fd, F_GETFD);
   if (flags == -1)
      goto err;

   if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1)
      goto err;

   return fd;

err:
   close(fd);
   return -1;
}
#endif

#if !(defined(__FreeBSD__) || DETECT_OS_ANDROID)
static int
create_tmpfile_cloexec(char *tmpname)
{
   int fd;

#ifdef HAVE_MKOSTEMP
   fd = mkostemp(tmpname, O_CLOEXEC);
#else
   fd = mkstemp(tmpname);
#endif

   if (fd < 0) {
      return fd;
   }

#ifndef HAVE_MKOSTEMP
   fd = set_cloexec_or_close(fd);
#endif

   unlink(tmpname);
   return fd;
}
#endif

#if !(defined(__FreeBSD__) || DETECT_OS_ANDROID)
/*
 * Gets the path to a suitable temporary directory for the current user.
 * Prefers using the environment variable `XDG_RUNTIME_DIR` if set,
 * otherwise falls back to creating or re-using a folder in `/tmp`.
 * Copies the path into the given `buf` of length `len` and also returns
 * a pointer to the same buffer for convenience.
 * Returns NULL if no suitable directory can found or created.
 */
static char*
get_or_create_user_temp_dir(char* buf, size_t len) {
    const char* env;
    struct stat st;
    int uid = getuid();

    env = os_get_option("XDG_RUNTIME_DIR");
    if (env && env[0] != '\0') {
        snprintf(buf, len, "%s", env);
        return buf;
    }

    snprintf(buf, len, "/tmp/xdg-runtime-mesa-%ld", (long)getuid());
    mesa_logd("%s: XDG_RUNTIME_DIR not set; falling back to temp dir %s",
        __func__, buf);
    if (stat(buf, &st) == 0) {
        /* If already exists, confirm the owner/permissions */
        if (!S_ISDIR(st.st_mode)) {
            mesa_loge(
                "%s: %s exists but is not a directory", __func__, buf);
            return NULL;
        }
        if (st.st_uid != uid) {
            mesa_loge(
                "%s: %s exists but has wrong owner", __func__, buf);
            return NULL;
        }

        return buf;
    } else if (errno == ENOENT) {
        /* Doesn't exist, try to create it */
        if (mkdir(buf, 0700) != 0) {
            mesa_loge("%s: mkdir %s failed: %s", __func__, buf,
                strerror(errno));
            return NULL;
        }

        return buf;
    } else {
        mesa_loge("%s: stat %s failed: %s", __func__, buf, 
            strerror(errno));
        return NULL;
    }
}
#endif

/*
 * Create a new, unique, anonymous file of the given size, and
 * return the file descriptor for it. The file descriptor is set
 * CLOEXEC. The file is immediately suitable for mmap()'ing
 * the given size at offset zero.
 *
 * An optional name for debugging can be provided as the second argument.
 *
 * The file should not have a permanent backing store like a disk,
 * but may have if XDG_RUNTIME_DIR is not properly implemented in OS.
 *
 * If memfd or SHM_ANON is supported, the filesystem is not touched at all.
 * Otherwise, the file name is deleted from the file system.
 *
 * The file is suitable for buffer sharing between processes by
 * transmitting the file descriptor over Unix sockets using the
 * SCM_RIGHTS methods.
 */
int
os_create_anonymous_file(int64_t size, const char *debug_name)
{
   int fd = -1, ret;
   /* First try using preferred APIs */
#if defined(HAVE_MEMFD_CREATE)
   if (!debug_name)
      debug_name = "mesa-shared";
   fd = memfd_create(debug_name, MFD_CLOEXEC | MFD_ALLOW_SEALING);
#elif DETECT_OS_ANDROID
   if (!debug_name)
      debug_name = "mesa-shared";
   fd = syscall(SYS_memfd_create, debug_name, MFD_CLOEXEC | MFD_ALLOW_SEALING);
#elif defined(__FreeBSD__)
   fd = shm_open(SHM_ANON, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
#elif defined(__OpenBSD__)
   char template[] = "/tmp/mesa-XXXXXXXXXX";
   fd = shm_mkstemp(template);
   if (fd != -1)
      shm_unlink(template);
#endif

    /*
     * If preferred API failed (or not included in this build),
     * fall back to using a file in a temporary dir
     */
#if !(defined(__FreeBSD__) || DETECT_OS_ANDROID)
    if (fd == -1) {
        char path[PATH_MAX];
        char *name;

        if (!get_or_create_user_temp_dir(path, sizeof(path))) {
            errno = ENOENT;
            return -1;
        }

        if (debug_name)
            asprintf(&name, "%s/mesa-shared-%s-XXXXXX", path, debug_name);
        else
            asprintf(&name, "%s/mesa-shared-XXXXXX", path);
        if (!name)
            return -1;

        fd = create_tmpfile_cloexec(name);

        free(name);
    }
#endif

   if (fd < 0)
      return -1;

   ret = ftruncate(fd, (off_t)size);
   if (ret < 0) {
      close(fd);
      return -1;
   }

   return fd;
}
#else

#include <windows.h>
#include <io.h>

int
os_create_anonymous_file(int64_t size, const char *debug_name)
{
   (void)debug_name;
   HANDLE h = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL,
      PAGE_READWRITE, (size >> 32), size & 0xFFFFFFFF, NULL);
   return _open_osfhandle((intptr_t)h, 0);
}
#endif
