/*
 * Mesa 3-D graphics library
 *
 * Copyright © 2021, Google Inc.
 * SPDX-License-Identifier: MIT
 */

#include <limits.h>
#include <stdlib.h>
#include <pthread.h>
#include <dirent.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif
#include "u_gralloc_internal.h"

#include <hardware/gralloc.h>

#include "drm-uapi/drm_fourcc.h"
#include "util/log.h"
#include "util/macros.h"
#include "util/u_memory.h"

#include <dlfcn.h>
#include <errno.h>
#include <string.h>

struct fallback_gralloc {
   struct u_gralloc base;
   gralloc_module_t *gralloc_module;
};

/* returns # of fds, and by reference the actual fds */
static unsigned
get_native_buffer_fds(const native_handle_t *handle, int fds[3])
{
   if (!handle)
      return 0;

   /*
    * Various gralloc implementations exist, but the dma-buf fd tends
    * to be first. Access it directly to avoid a dependency on specific
    * gralloc versions.
    */
   for (int i = 0; i < handle->numFds; i++)
      fds[i] = handle->data[i];

   return handle->numFds;
}


typedef const native_handle_t *panvk_v19_buffer_handle_t;
typedef int32_t (*panvk_v19_import_fn)(const native_handle_t *, panvk_v19_buffer_handle_t *);
typedef int32_t (*panvk_v19_free_fn)(panvk_v19_buffer_handle_t);
typedef int32_t (*panvk_v19_getstd_fn)(panvk_v19_buffer_handle_t, int64_t, void *, size_t);
struct panvk_v19_mapper_v5 {
   panvk_v19_import_fn importBuffer;
   panvk_v19_free_fn freeBuffer;
   void *getTransportSize, *lock, *unlock, *flushLockedBuffer, *rereadLockedBuffer, *getMetadata;
   panvk_v19_getstd_fn getStandardMetadata;
};
struct panvk_v19_mapper {
   __attribute__((aligned(16))) uint32_t version;
   struct panvk_v19_mapper_v5 v5;
};
typedef int32_t (*panvk_v19_load_mapper_fn)(struct panvk_v19_mapper **);
typedef void *(*panvk_v19_open_passthrough_fn)(const char *, const char *, int);
typedef void *(*panvk_v19_load_sphal_fn)(const char *, int);

#if defined(__aarch64__)
/* HIDL IMapper@4.0 passthrough for vendor API < 34 (no AIMapper stable-C).
 * libhidlbase is neither in the NDK nor visible to an app namespace, so the
 * frozen IMapper@4.0 C++ ABI (aarch64 Itanium, libc++ ABI v1) is called
 * directly. Layout checked against Arm gralloc4 arm::mapper::GrallocMapper
 * (MT8755, vendor API 33): vtable slots importBuffer=14 freeBuffer=15
 * get=23; Return<Error> stores Error at +0x24; std::function is a 32-byte
 * buffer + __base* at +0x20 and the impl copies it via __clone(__base*). */
typedef void (*panvk_v19_vfn)(void);
struct panvk_v19_hidl_vec { const void *data; uint32_t size; bool owns; }; /* hidl_vec, hidl_string */
struct panvk_v19_hidl_handle { const native_handle_t *handle; bool owns; };
struct panvk_v19_metadata_type { struct panvk_v19_hidl_vec name; int64_t value; };
struct panvk_v19_hidl_ret { int32_t exception; uint8_t status[0x1c]; bool checked; int32_t error; };
struct panvk_v19_fn_base { const panvk_v19_vfn *vtbl; void *ctx; };
struct panvk_v19_fn { /* aligned_storage<3 * sizeof(void *)> is 16-aligned: 32-byte buffer */
   _Alignas(16) struct panvk_v19_fn_base buf; void *pad[2]; struct panvk_v19_fn_base *f;
};
_Static_assert(sizeof(struct panvk_v19_hidl_vec) == 16, "hidl_vec");
_Static_assert(sizeof(struct panvk_v19_hidl_handle) == 16, "hidl_handle");
_Static_assert(sizeof(struct panvk_v19_metadata_type) == 24, "MetadataType");
_Static_assert(offsetof(struct panvk_v19_hidl_ret, error) == 0x24 &&
               sizeof(struct panvk_v19_hidl_ret) == 0x28, "Return<Error>");
_Static_assert(offsetof(struct panvk_v19_fn, f) == 32 && sizeof(struct panvk_v19_fn) == 48,
               "std::function");

struct panvk_v19_cb_ctx { int32_t error; void *buffer; uint8_t *data; size_t size; };

static void panvk_v19_fn_nop(struct panvk_v19_fn_base *self) { (void)self; }
static struct panvk_v19_fn_base *
panvk_v19_fn_clone(const struct panvk_v19_fn_base *self) { return (struct panvk_v19_fn_base *)self; }
static void
panvk_v19_fn_clone_into(const struct panvk_v19_fn_base *self, struct panvk_v19_fn_base *dst) { *dst = *self; }
static const void *
panvk_v19_fn_target(const struct panvk_v19_fn_base *self, const void *ti) { (void)self; (void)ti; return NULL; }

static void
panvk_v19_import_cb(struct panvk_v19_fn_base *self, const int32_t *error, void *const *buffer)
{
   struct panvk_v19_cb_ctx *ctx = self->ctx;
   ctx->error = *error;
   ctx->buffer = *error ? NULL : *buffer;
}

static void
panvk_v19_get_cb(struct panvk_v19_fn_base *self, const int32_t *error,
                 const struct panvk_v19_hidl_vec *vec)
{
   struct panvk_v19_cb_ctx *ctx = self->ctx;
   ctx->error = *error;
   if (*error || ctx->data || !vec->data || !vec->size || vec->size > 65536) return;
   ctx->data = malloc(vec->size);
   if (!ctx->data) return;
   memcpy(ctx->data, vec->data, vec->size);
   ctx->size = vec->size;
}

/* libc++ __function::__base: D1, D0, __clone(), __clone(p), destroy,
 * destroy_deallocate, operator(), target, target_type (after offset/RTTI). */
#define PANVK_V19_FN_VTBL(call) { NULL, NULL, \
   (panvk_v19_vfn)panvk_v19_fn_nop, (panvk_v19_vfn)panvk_v19_fn_nop, \
   (panvk_v19_vfn)panvk_v19_fn_clone, (panvk_v19_vfn)panvk_v19_fn_clone_into, \
   (panvk_v19_vfn)panvk_v19_fn_nop, (panvk_v19_vfn)panvk_v19_fn_nop, (panvk_v19_vfn)(call), \
   (panvk_v19_vfn)panvk_v19_fn_target, (panvk_v19_vfn)panvk_v19_fn_target }
static const panvk_v19_vfn panvk_v19_import_vtbl[] = PANVK_V19_FN_VTBL(panvk_v19_import_cb);
static const panvk_v19_vfn panvk_v19_get_vtbl[] = PANVK_V19_FN_VTBL(panvk_v19_get_cb);

enum { PANVK_V19_M4_IMPORT = 14, PANVK_V19_M4_FREE = 15, PANVK_V19_M4_GET = 23 };
typedef struct panvk_v19_hidl_ret (*panvk_v19_m4_import_fn)(void *, const struct panvk_v19_hidl_handle *,
                                                            struct panvk_v19_fn *);
typedef struct panvk_v19_hidl_ret (*panvk_v19_m4_free_fn)(void *, void *);
typedef struct panvk_v19_hidl_ret (*panvk_v19_m4_get_fn)(void *, void *,
                                                         const struct panvk_v19_metadata_type *,
                                                         struct panvk_v19_fn *);
typedef void *(*panvk_v19_m4_fetch_fn)(const char *);
static void *panvk_v19_m4; /* IMapper@4.0 * */
#define PANVK_V19_M4_CALL(type, slot) ((type)(*(const panvk_v19_vfn *const *)panvk_v19_m4)[slot])
#endif

static pthread_once_t panvk_v19_once = PTHREAD_ONCE_INIT;
static struct panvk_v19_mapper *panvk_v19_mapper_ptr;
static int panvk_v19_backend; /* 5: AIMapper stable-C, 4: HIDL IMapper@4.0, 0: none */

#define PANVK_V19_MAX_NAMES 16
static const char *const panvk_v19_hw_dirs[] = { "/vendor/lib64/hw", "/odm/lib64/hw" };
static const char panvk_v19_m4_prefix[] = "android.hardware.graphics.mapper@4.0-impl-";

static void
panvk_v19_add_name(char names[][64], int *n, const char *s, size_t len)
{
   if (!len || len >= 64 || *n >= PANVK_V19_MAX_NAMES || memchr(s, '/', len)) return;
   for (int i = 0; i < *n; i++)
      if (strlen(names[i]) == len && !memcmp(names[i], s, len)) return;
   memcpy(names[*n], s, len);
   names[*n][len] = '\0';
   (*n)++;
}

/* Mapper instance names, vendor-neutral: PANVK_MAPPER_NAMES (comma list),
 * then what the vendor hw dirs ship (mapper.<n>.so = AIMapper stable-C,
 * android.hardware.graphics.mapper@4.0-impl-<n>.so = HIDL mapper4), then
 * ro.hardware.gralloc, then "mediatek" in case the dirs cannot be listed. */
static int
panvk_v19_collect_names(char names[][64])
{
   int n = 0;
   const size_t plen = sizeof(panvk_v19_m4_prefix) - 1;
   for (const char *env = getenv("PANVK_MAPPER_NAMES"); env && *env;) {
      size_t len = strcspn(env, ",");
      panvk_v19_add_name(names, &n, env, len);
      env += len + (env[len] == ',');
   }
   for (unsigned d = 0; d < ARRAY_SIZE(panvk_v19_hw_dirs); d++) {
      DIR *dir = opendir(panvk_v19_hw_dirs[d]);
      if (!dir) continue;
      for (struct dirent *e; (e = readdir(dir));) {
         const char *s = e->d_name;
         size_t len = strlen(s);
         if (len < 4 || strcmp(s + len - 3, ".so")) continue;
         if (len > 10 && !strncmp(s, "mapper.", 7))
            panvk_v19_add_name(names, &n, s + 7, len - 10);
         else if (len > plen + 3 && !strncmp(s, panvk_v19_m4_prefix, plen))
            panvk_v19_add_name(names, &n, s + plen, len - plen - 3);
      }
      closedir(dir);
   }
#ifdef __ANDROID__
   char prop[PROP_VALUE_MAX] = "";
   if (__system_property_get("ro.hardware.gralloc", prop) > 0)
      panvk_v19_add_name(names, &n, prop, strlen(prop));
#endif
   panvk_v19_add_name(names, &n, "mediatek", 8);
   return n;
}

static void *
panvk_v19_dlopen(panvk_v19_load_sphal_fn load_sphal, const char *path, const char **how)
{
   if (access(path, F_OK) && errno == ENOENT) return NULL;
   void *so = load_sphal ? load_sphal(path, RTLD_NOW | RTLD_LOCAL) : NULL;
   *how = "SPHAL";
   if (!so) {
      so = dlopen(path, RTLD_NOW | RTLD_LOCAL);
      *how = "DIRECT";
   }
   if (!so) {
      const char *err = dlerror();
      mesa_logi("[P0A-V19-FULLPLANE] dlopen %s failed: %s", path, err ? err : "?");
   }
   return so;
}

static void
panvk_v19_mapper_init_once(void)
{
   char names[PANVK_V19_MAX_NAMES][64], path[PATH_MAX];
   int n = panvk_v19_collect_names(names);
   panvk_v19_open_passthrough_fn open_hal = NULL;
   panvk_v19_load_sphal_fn load_sphal = NULL;
   void *bn = dlopen("libbinder_ndk.so", RTLD_NOW | RTLD_LOCAL);
   if (!bn)
      bn = dlopen("/system/lib64/libbinder_ndk.so", RTLD_NOW | RTLD_LOCAL);
   if (bn)
      open_hal = (panvk_v19_open_passthrough_fn)dlsym(bn, "AServiceManager_openDeclaredPassthroughHal");
   void *vs = dlopen("libvndksupport.so", RTLD_NOW | RTLD_LOCAL);
   if (!vs)
      vs = dlopen("/system/lib64/libvndksupport.so", RTLD_NOW | RTLD_LOCAL);
   if (vs)
      load_sphal = (panvk_v19_load_sphal_fn)dlsym(vs, "android_load_sphal_library");

   /* AIMapper stable-C (mapper5), as libui Gralloc5 loads it. */
   for (int i = 0; i < n; i++) {
      const char *how = "BINDER_PASSTHROUGH";
      void *so = open_hal ? open_hal("mapper", names[i], RTLD_NOW | RTLD_LOCAL) : NULL;
      for (unsigned d = 0; !so && d < ARRAY_SIZE(panvk_v19_hw_dirs); d++) {
         snprintf(path, sizeof(path), "%s/mapper.%s.so", panvk_v19_hw_dirs[d], names[i]);
         so = panvk_v19_dlopen(load_sphal, path, &how);
      }
      if (!so) continue;
      panvk_v19_load_mapper_fn load =
         (panvk_v19_load_mapper_fn)dlsym(so, "AIMapper_loadIMapper");
      struct panvk_v19_mapper *m = NULL;
      int32_t rc = load ? load(&m) : -1;
      mesa_logi("[P0A-V19-FULLPLANE] init backend=aimapper name=%s how=%s rc=%d mapper=%p version=%u",
                names[i], how, rc, m, m ? m->version : 0);
      if (!rc && m && m->version >= 5 && m->v5.importBuffer && m->v5.freeBuffer &&
          m->v5.getStandardMetadata) {
         panvk_v19_mapper_ptr = m;
         panvk_v19_backend = 5;
         return;
      }
   }
#if defined(__aarch64__)
   /* HIDL IMapper@4.0 passthrough (vendor API < 34). */
   for (int i = 0; i < n; i++) {
      for (unsigned d = 0; d < ARRAY_SIZE(panvk_v19_hw_dirs); d++) {
         const char *how = "NONE";
         snprintf(path, sizeof(path), "%s/%s%s.so", panvk_v19_hw_dirs[d], panvk_v19_m4_prefix, names[i]);
         void *so = panvk_v19_dlopen(load_sphal, path, &how);
         if (!so) continue;
         panvk_v19_m4_fetch_fn fetch = (panvk_v19_m4_fetch_fn)dlsym(so, "HIDL_FETCH_IMapper");
         void *m = fetch ? fetch("default") : NULL;
         mesa_logi("[P0A-V19-FULLPLANE] init backend=hidl-mapper4 name=%s how=%s mapper=%p",
                   names[i], how, m);
         if (m) {
            panvk_v19_m4 = m;
            panvk_v19_backend = 4;
            return;
         }
      }
   }
#endif
   mesa_logw("[P0A-V19-FULLPLANE] mapper load failed (%d candidate names, first=%s); "
             "PANVK_MAPPER_NAMES=<suffix> overrides", n, n ? names[0] : "-");
}

static int
panvk_v19_init_mapper(void)
{
   if (pthread_once(&panvk_v19_once, panvk_v19_mapper_init_once)) return -ENOTSUP;
   return panvk_v19_backend ? 0 : -ENOTSUP;
}

static int32_t
panvk_v19_import(const native_handle_t *raw, panvk_v19_buffer_handle_t *out)
{
#if defined(__aarch64__)
   if (panvk_v19_backend == 4) {
      struct panvk_v19_cb_ctx ctx = { .error = -1 };
      struct panvk_v19_hidl_handle hh = { raw, false };
      struct panvk_v19_fn cb = { { panvk_v19_import_vtbl + 2, &ctx } };
      cb.f = &cb.buf;
      struct panvk_v19_hidl_ret r =
         PANVK_V19_M4_CALL(panvk_v19_m4_import_fn, PANVK_V19_M4_IMPORT)(panvk_v19_m4, &hh, &cb);
      *out = ctx.buffer;
      return r.exception ? r.exception : ctx.error;
   }
#endif
   return panvk_v19_mapper_ptr->v5.importBuffer(raw, out);
}

static int32_t
panvk_v19_free(panvk_v19_buffer_handle_t h)
{
#if defined(__aarch64__)
   if (panvk_v19_backend == 4) {
      struct panvk_v19_hidl_ret r =
         PANVK_V19_M4_CALL(panvk_v19_m4_free_fn, PANVK_V19_M4_FREE)(panvk_v19_m4, (void *)h);
      return r.exception ? r.exception : r.error;
   }
#endif
   return panvk_v19_mapper_ptr->v5.freeBuffer(h);
}

struct panvk_v19_blob { uint8_t *data; size_t size; };

static int
panvk_v19_get_blob(panvk_v19_buffer_handle_t h, int64_t type, struct panvk_v19_blob *out)
{
   memset(out, 0, sizeof(*out));
#if defined(__aarch64__)
   if (panvk_v19_backend == 4) {
      static const char name[] = "android.hardware.graphics.common.StandardMetadataType";
      struct panvk_v19_metadata_type mt = { { name, sizeof(name) - 1, false }, type };
      struct panvk_v19_cb_ctx ctx = { .error = -1 };
      struct panvk_v19_fn cb = { { panvk_v19_get_vtbl + 2, &ctx } };
      cb.f = &cb.buf;
      struct panvk_v19_hidl_ret r =
         PANVK_V19_M4_CALL(panvk_v19_m4_get_fn, PANVK_V19_M4_GET)(panvk_v19_m4, (void *)h, &mt, &cb);
      if (r.exception || ctx.error || !ctx.data) {
         free(ctx.data);
         return -EINVAL;
      }
      out->data = ctx.data;
      out->size = ctx.size;
      return 0;
   }
#endif
   int32_t need = panvk_v19_mapper_ptr->v5.getStandardMetadata(h, type, NULL, 0);
   if (need <= 0 || need > 65536) return -EINVAL;
   uint8_t *data = malloc((size_t)need);
   if (!data) return -ENOMEM;
   int32_t got = panvk_v19_mapper_ptr->v5.getStandardMetadata(h, type, data, (size_t)need);
   if (got <= 0 || got > need) { free(data); return -EINVAL; }
   out->data = data;
   out->size = (size_t)got;
   return 0;
}

static int
panvk_v19_read_u64(const struct panvk_v19_blob *b, size_t *pos, uint64_t *v)
{
   if (*pos > b->size || b->size - *pos < sizeof(*v)) return -EINVAL;
   memcpy(v, b->data + *pos, sizeof(*v));
   *pos += sizeof(*v);
   return 0;
}

static int
panvk_v19_read_i64(const struct panvk_v19_blob *b, size_t *pos, int64_t *v)
{
   if (*pos > b->size || b->size - *pos < sizeof(*v)) return -EINVAL;
   memcpy(v, b->data + *pos, sizeof(*v));
   *pos += sizeof(*v);
   return 0;
}

static int
panvk_v19_skip_string(const struct panvk_v19_blob *b, size_t *pos, const char *expected)
{
   uint64_t n = 0;
   if (panvk_v19_read_u64(b, pos, &n) || n > 512 || *pos > b->size || n > b->size - *pos)
      return -EINVAL;
   if (expected && (n != strlen(expected) || memcmp(b->data + *pos, expected, (size_t)n)))
      return -EINVAL;
   *pos += (size_t)n;
   return 0;
}

static int
panvk_v19_metadata_header(const struct panvk_v19_blob *b, int64_t expected_type, size_t *pos)
{
   static const char name[] = "android.hardware.graphics.common.StandardMetadataType";
   int64_t type = -1;
   *pos = 0;
   if (panvk_v19_skip_string(b, pos, name) || panvk_v19_read_i64(b, pos, &type) ||
       type != expected_type)
      return -EINVAL;
   return 0;
}

static int
panvk_v19_get_scalar(panvk_v19_buffer_handle_t h, int64_t type, void *dst, size_t width)
{
   struct panvk_v19_blob b;
   int rc = panvk_v19_get_blob(h, type, &b);
   if (rc) return rc;
   size_t pos = 0;
   rc = panvk_v19_metadata_header(&b, type, &pos);
   if (!rc && pos <= b.size && width == b.size - pos)
      memcpy(dst, b.data + pos, width);
   else if (!rc)
      rc = -EINVAL;
   free(b.data);
   return rc;
}

static int
panvk_v19_decode_planes(panvk_v19_buffer_handle_t h, const native_handle_t *raw,
                        uint64_t allocation, struct u_gralloc_buffer_basic_info *out)
{
   static const char component_name[] =
      "android.hardware.graphics.common.PlaneLayoutComponentType";
   struct panvk_v19_blob b;
   int rc = panvk_v19_get_blob(h, 15, &b);
   if (rc) return rc;
   size_t pos = 0;
   uint64_t planes = 0;
   rc = panvk_v19_metadata_header(&b, 15, &pos);
   if (rc || panvk_v19_read_u64(&b, &pos, &planes) || planes == 0 || planes > 4) {
      free(b.data); return -EINVAL;
   }
   int strides[4] = {0}, offsets[4] = {0};
   int fds[4] = {-1, -1, -1, -1};
   int fd_index = 0;
   for (uint64_t i = 0; i < planes; i++) {
      uint64_t components = 0;
      if (panvk_v19_read_u64(&b, &pos, &components) || components > 16) { rc = -EINVAL; break; }
      for (uint64_t c = 0; c < components; c++) {
         int64_t component_type, bit_offset, bit_size;
         if (panvk_v19_skip_string(&b, &pos, component_name) ||
             panvk_v19_read_i64(&b, &pos, &component_type) ||
             panvk_v19_read_i64(&b, &pos, &bit_offset) ||
             panvk_v19_read_i64(&b, &pos, &bit_size) ||
             bit_offset < 0 || bit_size <= 0) { rc = -EINVAL; break; }
      }
      if (rc) break;
      int64_t offset, sample_inc, stride, width, height, total, hsub, vsub;
      if (panvk_v19_read_i64(&b, &pos, &offset) ||
          panvk_v19_read_i64(&b, &pos, &sample_inc) ||
          panvk_v19_read_i64(&b, &pos, &stride) ||
          panvk_v19_read_i64(&b, &pos, &width) ||
          panvk_v19_read_i64(&b, &pos, &height) ||
          panvk_v19_read_i64(&b, &pos, &total) ||
          panvk_v19_read_i64(&b, &pos, &hsub) ||
          panvk_v19_read_i64(&b, &pos, &vsub) ||
          offset < 0 || offset > INT_MAX ||
          sample_inc <= 0 || stride <= 0 || stride > INT_MAX ||
          width <= 0 || height <= 0 || total <= 0 ||
          (uint64_t)offset > allocation || (uint64_t)total > allocation - (uint64_t)offset ||
          hsub <= 0 || vsub <= 0) { rc = -EINVAL; break; }
      offsets[i] = (int)offset;
      strides[i] = (int)stride;
      if (offsets[i] == 0 && i > 0) fd_index++;
      if (!raw || fd_index >= raw->numFds) { rc = -EINVAL; break; }
      fds[i] = raw->data[fd_index];
      mesa_logi("[P0A-V19-FULLPLANE] plane=%u fd_index=%d offset=%d stride=%d total=%lld sample_bits=%lld samples=%lldx%lld sub=%lldx%lld",
                (unsigned)i, fd_index, offsets[i], strides[i], (long long)total,
                (long long)sample_inc, (long long)width, (long long)height,
                (long long)hsub, (long long)vsub);
   }
   if (!rc && pos != b.size) rc = -EINVAL;
   if (!rc) {
      out->num_planes = (int)planes;
      for (int i = 0; i < out->num_planes; i++) {
         out->fds[i] = fds[i];
         out->strides[i] = strides[i];
         out->offsets[i] = offsets[i];
      }
   }
   free(b.data);
   return rc;
}

static int
panvk_v19_query_mapper(const native_handle_t *raw, struct u_gralloc_buffer_basic_info *out)
{
   if (!raw || !out || panvk_v19_init_mapper()) return -ENOTSUP;
   panvk_v19_buffer_handle_t imported = NULL;
   int32_t ir = panvk_v19_import(raw, &imported);
   if (ir || !imported) {
      mesa_logw("[P0A-V19-FULLPLANE] import rc=%d", ir);
      return -EINVAL;
   }
   uint32_t fourcc = 0;
   uint64_t modifier = DRM_FORMAT_MOD_INVALID, allocation = 0, layer_count = 0;
   int rl = panvk_v19_get_scalar(imported, 5, &layer_count, sizeof(layer_count));
   int rf = panvk_v19_get_scalar(imported, 7, &fourcc, sizeof(fourcc));
   int rm = panvk_v19_get_scalar(imported, 8, &modifier, sizeof(modifier));
   int ra = panvk_v19_get_scalar(imported, 10, &allocation, sizeof(allocation));
   int rp = 0;
   if (!rl && !rf && !rm && !ra && layer_count && fourcc &&
       modifier != DRM_FORMAT_MOD_INVALID && allocation)
      rp = panvk_v19_decode_planes(imported, raw, allocation, out);
   else
      rp = -EINVAL;
   int32_t fr = panvk_v19_free(imported);
   mesa_logi("[P0A-V19-FULLPLANE] metadata layer_rc=%d layers=%llu fourcc_rc=%d fourcc=0x%08x modifier_rc=%d modifier=0x%016llx alloc_rc=%d alloc=%llu planes_rc=%d free_rc=%d",
             rl, (unsigned long long)layer_count, rf, fourcc, rm,
             (unsigned long long)modifier, ra, (unsigned long long)allocation, rp, fr);
   if (rl || rf || rm || ra || rp || fr) return -EINVAL;
   out->drm_fourcc = fourcc;
   out->modifier = modifier;
   mesa_logi("[P0A-V19-FULLPLANE] accepted fourcc=0x%08x modifier=0x%016llx planes=%d",
             out->drm_fourcc, (unsigned long long)out->modifier, out->num_planes);
   return 0;
}

static int
fallback_gralloc_get_yuv_info(struct u_gralloc *gralloc,
                              struct u_gralloc_buffer_handle *hnd,
                              struct u_gralloc_buffer_basic_info *out)
{
   struct fallback_gralloc *gr = (struct fallback_gralloc *)gralloc;
   gralloc_module_t *gr_mod = gr->gralloc_module;
   struct android_ycbcr ycbcr;
   int num_fds = 0;
   int fds[3];
   int ret;

   num_fds = get_native_buffer_fds(hnd->handle, fds);
   if (num_fds == 0)
      return -EINVAL;

   if (!gr_mod || !gr_mod->lock_ycbcr) {
      return -EINVAL;
   }

   memset(&ycbcr, 0, sizeof(ycbcr));
   ret = gr_mod->lock_ycbcr(gr_mod, hnd->handle, 0, 0, 0, 0, 0, &ycbcr);
   if (ret) {
      /* HACK: See native_window_buffer_get_buffer_info() and
       * https://issuetracker.google.com/32077885.*/
      if (hnd->hal_format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED)
         return -EAGAIN;

      mesa_logw("gralloc->lock_ycbcr failed: %d", ret);
      return -EINVAL;
   }
   gr_mod->unlock(gr_mod, hnd->handle);

   ret = bufferinfo_from_ycbcr(&ycbcr, hnd, out);
   if (ret)
      return ret;

   /*
    * Since this is EGL_NATIVE_BUFFER_ANDROID don't assume that
    * the single-fd case cannot happen.  So handle eithe single
    * fd or fd-per-plane case:
    */
   if (num_fds == 1) {
      out->fds[1] = out->fds[0] = fds[0];
      if (out->num_planes == 3)
         out->fds[2] = fds[0];
   } else {
      assert(num_fds == out->num_planes);
      out->fds[0] = fds[0];
      out->fds[1] = fds[1];
      out->fds[2] = fds[2];
   }

   return 0;
}

static int
fallback_gralloc_get_buffer_info(struct u_gralloc *gralloc,
                                 struct u_gralloc_buffer_handle *hnd,
                                 struct u_gralloc_buffer_basic_info *out)
{
   int num_planes = 0;
   int drm_fourcc = 0;
   int stride = 0;

   if (hnd->handle->numFds == 0)
      return -EINVAL;

   if (is_hal_format_yuv(hnd->hal_format)) {
      int ret = fallback_gralloc_get_yuv_info(gralloc, hnd, out);
      /*
       * HACK: https://issuetracker.google.com/32077885
       * There is no API available to properly query the
       * IMPLEMENTATION_DEFINED format. As a workaround we rely here on
       * gralloc allocating either an arbitrary YCbCr 4:2:0 or RGBX_8888, with
       * the latter being recognized by lock_ycbcr failing.
       */
      if (ret != -EAGAIN)
         return ret;
   }

   /*
    * Non-YUV formats could *also* have multiple planes, such as ancillary
    * color compression state buffer, but the rest of the code isn't ready
    * yet to deal with modifiers:
    */
   num_planes = 1;

   drm_fourcc = get_fourcc_from_hal_format(hnd->hal_format);
   if (drm_fourcc == -1) {
      mesa_loge("Failed to get drm_fourcc");
      return -EINVAL;
   }

   stride = hnd->pixel_stride * get_hal_format_bpp(hnd->hal_format);
   if (stride == 0) {
      mesa_loge("Failed to calcuulate stride");
      return -EINVAL;
   }

   out->drm_fourcc = drm_fourcc;
   out->modifier = DRM_FORMAT_MOD_INVALID;
   out->num_planes = num_planes;
   out->fds[0] = hnd->handle->data[0];
   out->strides[0] = stride;

#ifdef HAS_FREEDRENO
   uint32_t gmsm = ('g' << 24) | ('m' << 16) | ('s' << 8) | 'm';
   if (hnd->handle->numInts >= 2 && hnd->handle->data[hnd->handle->numFds] == gmsm) {
      /* This UBWC flag was introduced in a5xx. */
      bool ubwc = hnd->handle->data[hnd->handle->numFds + 1] & 0x08000000;
      out->modifier = ubwc ? DRM_FORMAT_MOD_QCOM_COMPRESSED : DRM_FORMAT_MOD_LINEAR;
   }
#endif



   int stable_ret = panvk_v19_query_mapper(hnd->handle, out);
   if (stable_ret != 0) {
      mesa_logw("[P0A-V19-FULLPLANE] complete metadata unavailable rc=%d; refusing guessed layout",
                stable_ret);
      return stable_ret;
   }

   return 0;
}

static int
destroy(struct u_gralloc *gralloc)
{
   struct fallback_gralloc *gr = (struct fallback_gralloc *)gralloc;
   if (gr->gralloc_module) {
      dlclose(gr->gralloc_module->common.dso);
   }

   FREE(gr);

   return 0;
}

struct u_gralloc *
u_gralloc_fallback_create()
{
   struct fallback_gralloc *gr = CALLOC_STRUCT(fallback_gralloc);
   int err = 0;

   err = hw_get_module(GRALLOC_HARDWARE_MODULE_ID,
                       (const hw_module_t **)&gr->gralloc_module);

   if (err) {
      mesa_logw(
         "No gralloc hwmodule detected (video buffers won't be supported)");
   } else if (!gr->gralloc_module->lock_ycbcr) {
      mesa_logw("Gralloc doesn't support lock_ycbcr (video buffers won't be "
                "supported)");
   }

   gr->base.ops.get_buffer_basic_info = fallback_gralloc_get_buffer_info;
   gr->base.ops.destroy = destroy;

   mesa_logi("Using fallback gralloc implementation");

   return &gr->base;
}
