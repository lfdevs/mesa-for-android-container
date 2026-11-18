/*
 * Copyright © 2026 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

/* Find the writable cache directory of the Android app that owns this
 * process: /data/user/<uid / 100000>/<package>/cache or
 * /data/data/<package>/cache.
 *
 * The package cannot always be read from /proc/self/cmdline: a helper that
 * the app fork()s and execve()s (a test runner, a game launcher's native
 * process) has the path of its binary there, under
 * /data/app/[~~<hash>/]<package>-<hash>/, and its parent is the app process
 * whose cmdline is the package (up to a ':' service suffix). The candidates,
 * in order:
 *
 *   1. /proc/self/cmdline when it is a package name,
 *   2. /proc/<ppid>/cmdline when it is a package name,
 *   3. the package in the /data/app path of argv[0],
 *   4. the package in $TMPDIR, the current directory or $HOME when they are
 *      under /data/user/<n>/<package> or /data/data/<package>.
 *
 * The first candidate whose cache directory is writable wins. The parsing
 * helpers are pure so that tools/android-cache-dir-test can test them on a
 * host.
 */

#ifndef U_ANDROID_APP_DIR_H
#define U_ANDROID_APP_DIR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A Java package name: letters, digits, '_' and at least one '.', no
 * leading digit or dot. */
static inline bool
u_android_is_package(const char *s, size_t len)
{
   bool dot = false;

   if (len == 0 || len >= 224 || s[0] == '.' || (s[0] >= '0' && s[0] <= '9'))
      return false;
   for (size_t i = 0; i < len; i++) {
      const char c = s[i];
      if (c == '.') {
         if (s[i - 1] == '.' || i + 1 == len)
            return false;
         dot = true;
      } else if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || c == '_')) {
         return false;
      }
   }
   return dot;
}

/* Copy the package out of a process name ("pkg" or "pkg:service"). */
static inline bool
u_android_package_from_procname(const char *name, char *pkg, size_t size)
{
   const size_t len = strcspn(name, ":");

   if (len >= size || !u_android_is_package(name, len))
      return false;
   memcpy(pkg, name, len);
   pkg[len] = 0;
   return true;
}

/* Copy the package out of a path under /data/app/[~~<x>/]<pkg>-<x>/,
 * /data/user/<n>/<pkg>, /data/user_de/<n>/<pkg> or /data/data/<pkg>.
 * Package names have no '-', so the package ends at the first '-' in
 * /data/app (the hash after it is base64url and may contain one). */
static inline bool
u_android_package_from_path(const char *path, char *pkg, size_t size)
{
   const char *p = NULL;
   char end = '/';

   if (!path)
      return false;
   if (!strncmp(path, "/data/app/", 10)) {
      p = path + 10;
      if (p[0] == '~' && p[1] == '~') {
         p = strchr(p, '/');
         if (!p)
            return false;
         p++;
      }
      end = '-';
   } else if (!strncmp(path, "/data/data/", 11)) {
      p = path + 11;
   } else if (!strncmp(path, "/data/user/", 11) ||
              !strncmp(path, "/data/user_de/", 14)) {
      p = path + (path[10] == '/' ? 11 : 14);
      if (*p < '0' || *p > '9')
         return false;
      while (*p >= '0' && *p <= '9')
         p++;
      if (*p != '/')
         return false;
      p++;
   } else {
      return false;
   }

   size_t len = 0;
   while (p[len] && p[len] != '/' && p[len] != end)
      len++;
   /* /data/app/<pkg>-<x>: the '-' is required. */
   if (end == '-' && p[len] != '-')
      return false;
   if (len >= size || !u_android_is_package(p, len))
      return false;
   memcpy(pkg, p, len);
   pkg[len] = 0;
   return true;
}

#if defined(__ANDROID__) || defined(U_ANDROID_APP_DIR_TEST)

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#ifndef U_ANDROID_APP_DIR_ROOT
#define U_ANDROID_APP_DIR_ROOT ""
#endif

static inline ssize_t
u_android_read_file(const char *file, char *buf, size_t size)
{
   int fd = open(file, O_RDONLY | O_CLOEXEC);

   if (fd < 0)
      return -1;
   const ssize_t n = read(fd, buf, size - 1);
   close(fd);
   buf[n > 0 ? n : 0] = 0;
   return n;
}

/* Writable cache directory of the package, or false. */
static inline bool
u_android_package_cache_dir(const char *pkg, unsigned uid, char *buf,
                            size_t size)
{
   snprintf(buf, size, U_ANDROID_APP_DIR_ROOT "/data/user/%u/%s/cache",
            uid / 100000, pkg);
   if (access(buf, W_OK) == 0)
      return true;
   snprintf(buf, size, U_ANDROID_APP_DIR_ROOT "/data/data/%s/cache", pkg);
   return access(buf, W_OK) == 0;
}

/* The app's writable cache directory, in buf, or NULL. */
static inline const char *
u_android_app_cache_dir(char *buf, size_t size)
{
   char name[512], pkg[256], file[64];
   const unsigned uid = (unsigned)getuid();

   /* 1, 3: our own cmdline (a package or an execve()d helper's path). */
   if (u_android_read_file("/proc/self/cmdline", name, sizeof(name)) > 0) {
      if (u_android_package_from_procname(name, pkg, sizeof(pkg)) &&
          u_android_package_cache_dir(pkg, uid, buf, size))
         return buf;
      if (u_android_package_from_path(name, pkg, sizeof(pkg)) &&
          u_android_package_cache_dir(pkg, uid, buf, size))
         return buf;
   }

   /* 2: the parent, for a helper forked by the app process. */
   snprintf(file, sizeof(file), "/proc/%d/cmdline", (int)getppid());
   if (u_android_read_file(file, name, sizeof(name)) > 0 &&
       u_android_package_from_procname(name, pkg, sizeof(pkg)) &&
       u_android_package_cache_dir(pkg, uid, buf, size))
      return buf;

   /* 4: directories that the app handed down. */
   const char *dirs[] = { getenv("TMPDIR"), NULL, getenv("HOME") };
   if (getcwd(name, sizeof(name)))
      dirs[1] = name;
   for (unsigned i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
      if (u_android_package_from_path(dirs[i], pkg, sizeof(pkg)) &&
          u_android_package_cache_dir(pkg, uid, buf, size))
         return buf;
   }
   return NULL;
}

#endif

#ifdef __cplusplus
}
#endif

#endif
