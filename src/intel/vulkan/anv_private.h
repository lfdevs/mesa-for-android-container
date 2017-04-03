/*
 * Copyright © 2015 Intel Corporation
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

#ifndef ANV_PRIVATE_H
#define ANV_PRIVATE_H

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <pthread.h>
#include <assert.h>
#include <stdint.h>
#include <i915_drm.h>

#ifdef HAVE_VALGRIND
#include <valgrind.h>
#include <memcheck.h>
#define VG(x) x
#define __gen_validate_value(x) VALGRIND_CHECK_MEM_IS_DEFINED(&(x), sizeof(x))
#else
#define VG(x)
#endif

#include "common/gen_device_info.h"
#include "blorp/blorp.h"
#include "brw_compiler.h"
#include "util/macros.h"
#include "util/list.h"
#include "util/u_vector.h"
#include "util/vk_alloc.h"

/* Pre-declarations needed for WSI entrypoints */
struct wl_surface;
struct wl_display;
typedef struct xcb_connection_t xcb_connection_t;
typedef uint32_t xcb_visualid_t;
typedef uint32_t xcb_window_t;

struct gen_l3_config;

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_intel.h>
#include <vulkan/vk_icd.h>

#include "anv_entrypoints.h"
#include "brw_context.h"
#include "isl/isl.h"

#include "wsi_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Allowing different clear colors requires us to perform a depth resolve at
 * the end of certain render passes. This is because while slow clears store
 * the clear color in the HiZ buffer, fast clears (without a resolve) don't.
 * See the PRMs for examples describing when additional resolves would be
 * necessary. To enable fast clears without requiring extra resolves, we set
 * the clear value to a globally-defined one. We could allow different values
 * if the user doesn't expect coherent data during or after a render passes
 * (VK_ATTACHMENT_STORE_OP_DONT_CARE), but such users (aside from the CTS)
 * don't seem to exist yet. In almost all Vulkan applications tested thus far,
 * 1.0f seems to be the only value used. The only application that doesn't set
 * this value does so through the usage of an seemingly uninitialized clear
 * value.
 */
#define ANV_HZ_FC_VAL 1.0f

#define MAX_VBS         32
#define MAX_SETS         8
#define MAX_RTS          8
#define MAX_VIEWPORTS   16
#define MAX_SCISSORS    16
#define MAX_PUSH_CONSTANTS_SIZE 128
#define MAX_DYNAMIC_BUFFERS 16
#define MAX_IMAGES 8

#define anv_printflike(a, b) __attribute__((__format__(__printf__, a, b)))

static inline uint32_t
align_down_npot_u32(uint32_t v, uint32_t a)
{
   return v - (v % a);
}

static inline uint32_t
align_u32(uint32_t v, uint32_t a)
{
   assert(a != 0 && a == (a & -a));
   return (v + a - 1) & ~(a - 1);
}

static inline uint64_t
align_u64(uint64_t v, uint64_t a)
{
   assert(a != 0 && a == (a & -a));
   return (v + a - 1) & ~(a - 1);
}

static inline int32_t
align_i32(int32_t v, int32_t a)
{
   assert(a != 0 && a == (a & -a));
   return (v + a - 1) & ~(a - 1);
}

/** Alignment must be a power of 2. */
static inline bool
anv_is_aligned(uintmax_t n, uintmax_t a)
{
   assert(a == (a & -a));
   return (n & (a - 1)) == 0;
}

static inline uint32_t
anv_minify(uint32_t n, uint32_t levels)
{
   if (unlikely(n == 0))
      return 0;
   else
      return MAX2(n >> levels, 1);
}

static inline float
anv_clamp_f(float f, float min, float max)
{
   assert(min < max);

   if (f > max)
      return max;
   else if (f < min)
      return min;
   else
      return f;
}

static inline bool
anv_clear_mask(uint32_t *inout_mask, uint32_t clear_mask)
{
   if (*inout_mask & clear_mask) {
      *inout_mask &= ~clear_mask;
      return true;
   } else {
      return false;
   }
}

static inline union isl_color_value
vk_to_isl_color(VkClearColorValue color)
{
   return (union isl_color_value) {
      .u32 = {
         color.uint32[0],
         color.uint32[1],
         color.uint32[2],
         color.uint32[3],
      },
   };
}

#define for_each_bit(b, dword)                          \
   for (uint32_t __dword = (dword);                     \
        (b) = __builtin_ffs(__dword) - 1, __dword;      \
        __dword &= ~(1 << (b)))

#define typed_memcpy(dest, src, count) ({ \
   STATIC_ASSERT(sizeof(*src) == sizeof(*dest)); \
   memcpy((dest), (src), (count) * sizeof(*(src))); \
})

/* Whenever we generate an error, pass it through this function. Useful for
 * debugging, where we can break on it. Only call at error site, not when
 * propagating errors. Might be useful to plug in a stack trace here.
 */

VkResult __vk_errorf(VkResult error, const char *file, int line, const char *format, ...);

#ifdef DEBUG
#define vk_error(error) __vk_errorf(error, __FILE__, __LINE__, NULL);
#define vk_errorf(error, format, ...) __vk_errorf(error, __FILE__, __LINE__, format, ## __VA_ARGS__);
#else
#define vk_error(error) error
#define vk_errorf(error, format, ...) error
#endif

void __anv_finishme(const char *file, int line, const char *format, ...)
   anv_printflike(3, 4);
void anv_loge(const char *format, ...) anv_printflike(1, 2);
void anv_loge_v(const char *format, va_list va);

/**
 * Print a FINISHME message, including its source location.
 */
#define anv_finishme(format, ...) \
   do { \
      static bool reported = false; \
      if (!reported) { \
         __anv_finishme(__FILE__, __LINE__, format, ##__VA_ARGS__); \
         reported = true; \
      } \
   } while (0)

/* A non-fatal assert.  Useful for debugging. */
#ifdef DEBUG
#define anv_assert(x) ({ \
   if (unlikely(!(x))) \
      fprintf(stderr, "%s:%d ASSERT: %s\n", __FILE__, __LINE__, #x); \
})
#else
#define anv_assert(x)
#endif

/**
 * If a block of code is annotated with anv_validate, then the block runs only
 * in debug builds.
 */
#ifdef DEBUG
#define anv_validate if (1)
#else
#define anv_validate if (0)
#endif

#define stub_return(v) \
   do { \
      anv_finishme("stub %s", __func__); \
      return (v); \
   } while (0)

#define stub() \
   do { \
      anv_finishme("stub %s", __func__); \
      return; \
   } while (0)

/**
 * A dynamically growable, circular buffer.  Elements are added at head and
 * removed from tail. head and tail are free-running uint32_t indices and we
 * only compute the modulo with size when accessing the array.  This way,
 * number of bytes in the queue is always head - tail, even in case of
 * wraparound.
 */

struct anv_bo {
   uint32_t gem_handle;

   /* Index into the current validation list.  This is used by the
    * validation list building alrogithm to track which buffers are already
    * in the validation list so that we can ensure uniqueness.
    */
   uint32_t index;

   /* Last known offset.  This value is provided by the kernel when we
    * execbuf and is used as the presumed offset for the next bunch of
    * relocations.
    */
   uint64_t offset;

   uint64_t size;
   void *map;

   /* We need to set the WRITE flag on winsys bos so GEM will know we're
    * writing to them and synchronize uses on other rings (eg if the display
    * server uses the blitter ring).
    */
   bool is_winsys_bo;
};

static inline void
anv_bo_init(struct anv_bo *bo, uint32_t gem_handle, uint64_t size)
{
   bo->gem_handle = gem_handle;
   bo->index = 0;
   bo->offset = -1;
   bo->size = size;
   bo->map = NULL;
   bo->is_winsys_bo = false;
}

/* Represents a lock-free linked list of "free" things.  This is used by
 * both the block pool and the state pools.  Unfortunately, in order to
 * solve the ABA problem, we can't use a single uint32_t head.
 */
union anv_free_list {
   struct {
      int32_t offset;

      /* A simple count that is incremented every time the head changes. */
      uint32_t count;
   };
   uint64_t u64;
};

#define ANV_FREE_LIST_EMPTY ((union anv_free_list) { { 1, 0 } })

struct anv_block_state {
   union {
      struct {
         uint32_t next;
         uint32_t end;
      };
      uint64_t u64;
   };
};

struct anv_block_pool {
   struct anv_device *device;

   struct anv_bo bo;

   /* The offset from the start of the bo to the "center" of the block
    * pool.  Pointers to allocated blocks are given by
    * bo.map + center_bo_offset + offsets.
    */
   uint32_t center_bo_offset;

   /* Current memory map of the block pool.  This pointer may or may not
    * point to the actual beginning of the block pool memory.  If
    * anv_block_pool_alloc_back has ever been called, then this pointer
    * will point to the "center" position of the buffer and all offsets
    * (negative or positive) given out by the block pool alloc functions
    * will be valid relative to this pointer.
    *
    * In particular, map == bo.map + center_offset
    */
   void *map;
   int fd;

   /**
    * Array of mmaps and gem handles owned by the block pool, reclaimed when
    * the block pool is destroyed.
    */
   struct u_vector mmap_cleanups;

   uint32_t block_size;

   union anv_free_list free_list;
   struct anv_block_state state;

   union anv_free_list back_free_list;
   struct anv_block_state back_state;
};

/* Block pools are backed by a fixed-size 2GB memfd */
#define BLOCK_POOL_MEMFD_SIZE (1ull << 32)

/* The center of the block pool is also the middle of the memfd.  This may
 * change in the future if we decide differently for some reason.
 */
#define BLOCK_POOL_MEMFD_CENTER (BLOCK_POOL_MEMFD_SIZE / 2)

static inline uint32_t
anv_block_pool_size(struct anv_block_pool *pool)
{
   return pool->state.end + pool->back_state.end;
}

struct anv_state {
   int32_t offset;
   uint32_t alloc_size;
   void *map;
};

struct anv_fixed_size_state_pool {
   size_t state_size;
   union anv_free_list free_list;
   struct anv_block_state block;
};

#define ANV_MIN_STATE_SIZE_LOG2 6
#define ANV_MAX_STATE_SIZE_LOG2 20

#define ANV_STATE_BUCKETS (ANV_MAX_STATE_SIZE_LOG2 - ANV_MIN_STATE_SIZE_LOG2 + 1)

struct anv_state_pool {
   struct anv_block_pool *block_pool;
   struct anv_fixed_size_state_pool buckets[ANV_STATE_BUCKETS];
};

struct anv_state_stream_block;

struct anv_state_stream {
   struct anv_block_pool *block_pool;

   /* The current working block */
   struct anv_state_stream_block *block;

   /* Offset at which the current block starts */
   uint32_t start;
   /* Offset at which to allocate the next state */
   uint32_t next;
   /* Offset at which the current block ends */
   uint32_t end;
};

#define CACHELINE_SIZE 64
#define CACHELINE_MASK 63

static inline void
anv_clflush_range(void *start, size_t size)
{
   void *p = (void *) (((uintptr_t) start) & ~CACHELINE_MASK);
   void *end = start + size;

   __builtin_ia32_mfence();
   while (p < end) {
      __builtin_ia32_clflush(p);
      p += CACHELINE_SIZE;
   }
}

static inline void
anv_invalidate_range(void *start, size_t size)
{
   void *p = (void *) (((uintptr_t) start) & ~CACHELINE_MASK);
   void *end = start + size;

   while (p < end) {
      __builtin_ia32_clflush(p);
      p += CACHELINE_SIZE;
   }
   __builtin_ia32_mfence();
}

static void inline
anv_state_clflush(struct anv_state state)
{
   anv_clflush_range(state.map, state.alloc_size);
}

VkResult anv_block_pool_init(struct anv_block_pool *pool,
                             struct anv_device *device, uint32_t block_size);
void anv_block_pool_finish(struct anv_block_pool *pool);
int32_t anv_block_pool_alloc(struct anv_block_pool *pool);
int32_t anv_block_pool_alloc_back(struct anv_block_pool *pool);
void anv_block_pool_free(struct anv_block_pool *pool, int32_t offset);
void anv_state_pool_init(struct anv_state_pool *pool,
                         struct anv_block_pool *block_pool);
void anv_state_pool_finish(struct anv_state_pool *pool);
struct anv_state anv_state_pool_alloc(struct anv_state_pool *pool,
                                      size_t state_size, size_t alignment);
void anv_state_pool_free(struct anv_state_pool *pool, struct anv_state state);
void anv_state_stream_init(struct anv_state_stream *stream,
                           struct anv_block_pool *block_pool);
void anv_state_stream_finish(struct anv_state_stream *stream);
struct anv_state anv_state_stream_alloc(struct anv_state_stream *stream,
                                        uint32_t size, uint32_t alignment);

/**
 * Implements a pool of re-usable BOs.  The interface is identical to that
 * of block_pool except that each block is its own BO.
 */
struct anv_bo_pool {
   struct anv_device *device;

   void *free_list[16];
};

void anv_bo_pool_init(struct anv_bo_pool *pool, struct anv_device *device);
void anv_bo_pool_finish(struct anv_bo_pool *pool);
VkResult anv_bo_pool_alloc(struct anv_bo_pool *pool, struct anv_bo *bo,
                           uint32_t size);
void anv_bo_pool_free(struct anv_bo_pool *pool, const struct anv_bo *bo);

struct anv_scratch_bo {
   bool exists;
   struct anv_bo bo;
};

struct anv_scratch_pool {
   /* Indexed by Per-Thread Scratch Space number (the hardware value) and stage */
   struct anv_scratch_bo bos[16][MESA_SHADER_STAGES];
};

void anv_scratch_pool_init(struct anv_device *device,
                           struct anv_scratch_pool *pool);
void anv_scratch_pool_finish(struct anv_device *device,
                             struct anv_scratch_pool *pool);
struct anv_bo *anv_scratch_pool_alloc(struct anv_device *device,
                                      struct anv_scratch_pool *pool,
                                      gl_shader_stage stage,
                                      unsigned per_thread_scratch);

extern struct anv_dispatch_table dtable;

struct anv_physical_device {
    VK_LOADER_DATA                              _loader_data;

    struct anv_instance *                       instance;
    uint32_t                                    chipset_id;
    char                                        path[20];
    const char *                                name;
    struct gen_device_info                      info;
    uint64_t                                    aperture_size;
    struct brw_compiler *                       compiler;
    struct isl_device                           isl_dev;
    int                                         cmd_parser_version;

    uint32_t                                    eu_total;
    uint32_t                                    subslice_total;

    uint8_t                                     uuid[VK_UUID_SIZE];

    struct wsi_device                       wsi_device;
};

struct anv_instance {
    VK_LOADER_DATA                              _loader_data;

    VkAllocationCallbacks                       alloc;

    uint32_t                                    apiVersion;
    int                                         physicalDeviceCount;
    struct anv_physical_device                  physicalDevice;
};

VkResult anv_init_wsi(struct anv_physical_device *physical_device);
void anv_finish_wsi(struct anv_physical_device *physical_device);

struct anv_queue {
    VK_LOADER_DATA                              _loader_data;

    struct anv_device *                         device;

    struct anv_state_pool *                     pool;
};

struct anv_pipeline_cache {
   struct anv_device *                          device;
   pthread_mutex_t                              mutex;

   struct hash_table *                          cache;
};

struct anv_pipeline_bind_map;

void anv_pipeline_cache_init(struct anv_pipeline_cache *cache,
                             struct anv_device *device,
                             bool cache_enabled);
void anv_pipeline_cache_finish(struct anv_pipeline_cache *cache);

struct anv_shader_bin *
anv_pipeline_cache_search(struct anv_pipeline_cache *cache,
                          const void *key, uint32_t key_size);
struct anv_shader_bin *
anv_pipeline_cache_upload_kernel(struct anv_pipeline_cache *cache,
                                 const void *key_data, uint32_t key_size,
                                 const void *kernel_data, uint32_t kernel_size,
                                 const struct brw_stage_prog_data *prog_data,
                                 uint32_t prog_data_size,
                                 const struct anv_pipeline_bind_map *bind_map);

struct anv_device {
    VK_LOADER_DATA                              _loader_data;

    VkAllocationCallbacks                       alloc;

    struct anv_instance *                       instance;
    uint32_t                                    chipset_id;
    struct gen_device_info                      info;
    struct isl_device                           isl_dev;
    int                                         context_id;
    int                                         fd;
    bool                                        can_chain_batches;
    bool                                        robust_buffer_access;

    struct anv_bo_pool                          batch_bo_pool;

    struct anv_block_pool                       dynamic_state_block_pool;
    struct anv_state_pool                       dynamic_state_pool;

    struct anv_block_pool                       instruction_block_pool;
    struct anv_state_pool                       instruction_state_pool;

    struct anv_block_pool                       surface_state_block_pool;
    struct anv_state_pool                       surface_state_pool;

    struct anv_bo                               workaround_bo;

    struct anv_pipeline_cache                   blorp_shader_cache;
    struct blorp_context                        blorp;

    struct anv_state                            border_colors;

    struct anv_queue                            queue;

    struct anv_scratch_pool                     scratch_pool;

    uint32_t                                    default_mocs;

    pthread_mutex_t                             mutex;
    pthread_cond_t                              queue_submit;
};

void anv_device_init_blorp(struct anv_device *device);
void anv_device_finish_blorp(struct anv_device *device);

VkResult anv_device_execbuf(struct anv_device *device,
                            struct drm_i915_gem_execbuffer2 *execbuf,
                            struct anv_bo **execbuf_bos);

void* anv_gem_mmap(struct anv_device *device,
                   uint32_t gem_handle, uint64_t offset, uint64_t size, uint32_t flags);
void anv_gem_munmap(void *p, uint64_t size);
uint32_t anv_gem_create(struct anv_device *device, size_t size);
void anv_gem_close(struct anv_device *device, uint32_t gem_handle);
uint32_t anv_gem_userptr(struct anv_device *device, void *mem, size_t size);
int anv_gem_wait(struct anv_device *device, uint32_t gem_handle, int64_t *timeout_ns);
int anv_gem_execbuffer(struct anv_device *device,
                       struct drm_i915_gem_execbuffer2 *execbuf);
int anv_gem_set_tiling(struct anv_device *device, uint32_t gem_handle,
                       uint32_t stride, uint32_t tiling);
int anv_gem_create_context(struct anv_device *device);
int anv_gem_destroy_context(struct anv_device *device, int context);
int anv_gem_get_param(int fd, uint32_t param);
bool anv_gem_get_bit6_swizzle(int fd, uint32_t tiling);
int anv_gem_get_aperture(int fd, uint64_t *size);
int anv_gem_handle_to_fd(struct anv_device *device, uint32_t gem_handle);
uint32_t anv_gem_fd_to_handle(struct anv_device *device, int fd);
int anv_gem_set_caching(struct anv_device *device, uint32_t gem_handle, uint32_t caching);
int anv_gem_set_domain(struct anv_device *device, uint32_t gem_handle,
                       uint32_t read_domains, uint32_t write_domain);

VkResult anv_bo_init_new(struct anv_bo *bo, struct anv_device *device, uint64_t size);

struct anv_reloc_list {
   size_t                                       num_relocs;
   size_t                                       array_length;
   struct drm_i915_gem_relocation_entry *       relocs;
   struct anv_bo **                             reloc_bos;
};

VkResult anv_reloc_list_init(struct anv_reloc_list *list,
                             const VkAllocationCallbacks *alloc);
void anv_reloc_list_finish(struct anv_reloc_list *list,
                           const VkAllocationCallbacks *alloc);

uint64_t anv_reloc_list_add(struct anv_reloc_list *list,
                            const VkAllocationCallbacks *alloc,
                            uint32_t offset, struct anv_bo *target_bo,
                            uint32_t delta);

struct anv_batch_bo {
   /* Link in the anv_cmd_buffer.owned_batch_bos list */
   struct list_head                             link;

   struct anv_bo                                bo;

   /* Bytes actually consumed in this batch BO */
   size_t                                       length;

   struct anv_reloc_list                        relocs;
};

struct anv_batch {
   const VkAllocationCallbacks *                alloc;

   void *                                       start;
   void *                                       end;
   void *                                       next;

   struct anv_reloc_list *                      relocs;

   /* This callback is called (with the associated user data) in the event
    * that the batch runs out of space.
    */
   VkResult (*extend_cb)(struct anv_batch *, void *);
   void *                                       user_data;
};

void *anv_batch_emit_dwords(struct anv_batch *batch, int num_dwords);
void anv_batch_emit_batch(struct anv_batch *batch, struct anv_batch *other);
uint64_t anv_batch_emit_reloc(struct anv_batch *batch,
                              void *location, struct anv_bo *bo, uint32_t offset);
VkResult anv_device_submit_simple_batch(struct anv_device *device,
                                        struct anv_batch *batch);

struct anv_address {
   struct anv_bo *bo;
   uint32_t offset;
};

static inline uint64_t
_anv_combine_address(struct anv_batch *batch, void *location,
                     const struct anv_address address, uint32_t delta)
{
   if (address.bo == NULL) {
      return address.offset + delta;
   } else {
      assert(batch->start <= location && location < batch->end);

      return anv_batch_emit_reloc(batch, location, address.bo, address.offset + delta);
   }
}

#define __gen_address_type struct anv_address
#define __gen_user_data struct anv_batch
#define __gen_combine_address _anv_combine_address

/* Wrapper macros needed to work around preprocessor argument issues.  In
 * particular, arguments don't get pre-evaluated if they are concatenated.
 * This means that, if you pass GENX(3DSTATE_PS) into the emit macro, the
 * GENX macro won't get evaluated if the emit macro contains "cmd ## foo".
 * We can work around this easily enough with these helpers.
 */
#define __anv_cmd_length(cmd) cmd ## _length
#define __anv_cmd_length_bias(cmd) cmd ## _length_bias
#define __anv_cmd_header(cmd) cmd ## _header
#define __anv_cmd_pack(cmd) cmd ## _pack
#define __anv_reg_num(reg) reg ## _num

#define anv_pack_struct(dst, struc, ...) do {                              \
      struct struc __template = {                                          \
         __VA_ARGS__                                                       \
      };                                                                   \
      __anv_cmd_pack(struc)(NULL, dst, &__template);                       \
      VG(VALGRIND_CHECK_MEM_IS_DEFINED(dst, __anv_cmd_length(struc) * 4)); \
   } while (0)

#define anv_batch_emitn(batch, n, cmd, ...) ({          \
      void *__dst = anv_batch_emit_dwords(batch, n);    \
      struct cmd __template = {                         \
         __anv_cmd_header(cmd),                         \
        .DWordLength = n - __anv_cmd_length_bias(cmd),  \
         __VA_ARGS__                                    \
      };                                                \
      __anv_cmd_pack(cmd)(batch, __dst, &__template);   \
      __dst;                                            \
   })

#define anv_batch_emit_merge(batch, dwords0, dwords1)                   \
   do {                                                                 \
      uint32_t *dw;                                                     \
                                                                        \
      STATIC_ASSERT(ARRAY_SIZE(dwords0) == ARRAY_SIZE(dwords1));        \
      dw = anv_batch_emit_dwords((batch), ARRAY_SIZE(dwords0));         \
      for (uint32_t i = 0; i < ARRAY_SIZE(dwords0); i++)                \
         dw[i] = (dwords0)[i] | (dwords1)[i];                           \
      VG(VALGRIND_CHECK_MEM_IS_DEFINED(dw, ARRAY_SIZE(dwords0) * 4));\
   } while (0)

#define anv_batch_emit(batch, cmd, name)                            \
   for (struct cmd name = { __anv_cmd_header(cmd) },                    \
        *_dst = anv_batch_emit_dwords(batch, __anv_cmd_length(cmd));    \
        __builtin_expect(_dst != NULL, 1);                              \
        ({ __anv_cmd_pack(cmd)(batch, _dst, &name);                     \
           VG(VALGRIND_CHECK_MEM_IS_DEFINED(_dst, __anv_cmd_length(cmd) * 4)); \
           _dst = NULL;                                                 \
         }))

#define anv_state_pool_emit(pool, cmd, align, ...) ({                   \
      const uint32_t __size = __anv_cmd_length(cmd) * 4;                \
      struct anv_state __state =                                        \
         anv_state_pool_alloc((pool), __size, align);                   \
      struct cmd __template = {                                         \
         __VA_ARGS__                                                    \
      };                                                                \
      __anv_cmd_pack(cmd)(NULL, __state.map, &__template);              \
      VG(VALGRIND_CHECK_MEM_IS_DEFINED(__state.map, __anv_cmd_length(cmd) * 4)); \
      if (!(pool)->block_pool->device->info.has_llc)                    \
         anv_state_clflush(__state);                                    \
      __state;                                                          \
   })

#define GEN7_MOCS (struct GEN7_MEMORY_OBJECT_CONTROL_STATE) {  \
   .GraphicsDataTypeGFDT                        = 0,           \
   .LLCCacheabilityControlLLCCC                 = 0,           \
   .L3CacheabilityControlL3CC                   = 1,           \
}

#define GEN75_MOCS (struct GEN75_MEMORY_OBJECT_CONTROL_STATE) {  \
   .LLCeLLCCacheabilityControlLLCCC             = 0,           \
   .L3CacheabilityControlL3CC                   = 1,           \
}

#define GEN8_MOCS (struct GEN8_MEMORY_OBJECT_CONTROL_STATE) {  \
      .MemoryTypeLLCeLLCCacheabilityControl = WB,              \
      .TargetCache = L3DefertoPATforLLCeLLCselection,          \
      .AgeforQUADLRU = 0                                       \
   }

/* Skylake: MOCS is now an index into an array of 62 different caching
 * configurations programmed by the kernel.
 */

#define GEN9_MOCS (struct GEN9_MEMORY_OBJECT_CONTROL_STATE) {  \
      /* TC=LLC/eLLC, LeCC=WB, LRUM=3, L3CC=WB */              \
      .IndextoMOCSTables                           = 2         \
   }

#define GEN9_MOCS_PTE {                                 \
      /* TC=LLC/eLLC, LeCC=WB, LRUM=3, L3CC=WB */       \
      .IndextoMOCSTables                           = 1  \
   }

struct anv_device_memory {
   struct anv_bo                                bo;
   uint32_t                                     type_index;
   VkDeviceSize                                 map_size;
   void *                                       map;
};

/**
 * Header for Vertex URB Entry (VUE)
 */
struct anv_vue_header {
   uint32_t Reserved;
   uint32_t RTAIndex; /* RenderTargetArrayIndex */
   uint32_t ViewportIndex;
   float PointWidth;
};

struct anv_descriptor_set_binding_layout {
#ifndef NDEBUG
   /* The type of the descriptors in this binding */
   VkDescriptorType type;
#endif

   /* Number of array elements in this binding */
   uint16_t array_size;

   /* Index into the flattend descriptor set */
   uint16_t descriptor_index;

   /* Index into the dynamic state array for a dynamic buffer */
   int16_t dynamic_offset_index;

   /* Index into the descriptor set buffer views */
   int16_t buffer_index;

   struct {
      /* Index into the binding table for the associated surface */
      int16_t surface_index;

      /* Index into the sampler table for the associated sampler */
      int16_t sampler_index;

      /* Index into the image table for the associated image */
      int16_t image_index;
   } stage[MESA_SHADER_STAGES];

   /* Immutable samplers (or NULL if no immutable samplers) */
   struct anv_sampler **immutable_samplers;
};

struct anv_descriptor_set_layout {
   /* Number of bindings in this descriptor set */
   uint16_t binding_count;

   /* Total size of the descriptor set with room for all array entries */
   uint16_t size;

   /* Shader stages affected by this descriptor set */
   uint16_t shader_stages;

   /* Number of buffers in this descriptor set */
   uint16_t buffer_count;

   /* Number of dynamic offsets used by this descriptor set */
   uint16_t dynamic_offset_count;

   /* Bindings in this descriptor set */
   struct anv_descriptor_set_binding_layout binding[0];
};

struct anv_descriptor {
   VkDescriptorType type;

   union {
      struct {
         struct anv_image_view *image_view;
         struct anv_sampler *sampler;
      };

      struct anv_buffer_view *buffer_view;
   };
};

struct anv_descriptor_set {
   const struct anv_descriptor_set_layout *layout;
   uint32_t size;
   uint32_t buffer_count;
   struct anv_buffer_view *buffer_views;
   struct anv_descriptor descriptors[0];
};

struct anv_descriptor_pool {
   uint32_t size;
   uint32_t next;
   uint32_t free_list;

   struct anv_state_stream surface_state_stream;
   void *surface_state_free_list;

   char data[0];
};

VkResult
anv_descriptor_set_create(struct anv_device *device,
                          struct anv_descriptor_pool *pool,
                          const struct anv_descriptor_set_layout *layout,
                          struct anv_descriptor_set **out_set);

void
anv_descriptor_set_destroy(struct anv_device *device,
                           struct anv_descriptor_pool *pool,
                           struct anv_descriptor_set *set);

#define ANV_DESCRIPTOR_SET_COLOR_ATTACHMENTS UINT8_MAX

struct anv_pipeline_binding {
   /* The descriptor set this surface corresponds to.  The special value of
    * ANV_DESCRIPTOR_SET_COLOR_ATTACHMENTS indicates that the offset refers
    * to a color attachment and not a regular descriptor.
    */
   uint8_t set;

   /* Binding in the descriptor set */
   uint8_t binding;

   /* Index in the binding */
   uint8_t index;

   /* Input attachment index (relative to the subpass) */
   uint8_t input_attachment_index;
};

struct anv_pipeline_layout {
   struct {
      struct anv_descriptor_set_layout *layout;
      uint32_t dynamic_offset_start;
   } set[MAX_SETS];

   uint32_t num_sets;

   struct {
      bool has_dynamic_offsets;
   } stage[MESA_SHADER_STAGES];

   unsigned char sha1[20];
};

struct anv_buffer {
   struct anv_device *                          device;
   VkDeviceSize                                 size;

   VkBufferUsageFlags                           usage;

   /* Set when bound */
   struct anv_bo *                              bo;
   VkDeviceSize                                 offset;
};

enum anv_cmd_dirty_bits {
   ANV_CMD_DIRTY_DYNAMIC_VIEWPORT                  = 1 << 0, /* VK_DYNAMIC_STATE_VIEWPORT */
   ANV_CMD_DIRTY_DYNAMIC_SCISSOR                   = 1 << 1, /* VK_DYNAMIC_STATE_SCISSOR */
   ANV_CMD_DIRTY_DYNAMIC_LINE_WIDTH                = 1 << 2, /* VK_DYNAMIC_STATE_LINE_WIDTH */
   ANV_CMD_DIRTY_DYNAMIC_DEPTH_BIAS                = 1 << 3, /* VK_DYNAMIC_STATE_DEPTH_BIAS */
   ANV_CMD_DIRTY_DYNAMIC_BLEND_CONSTANTS           = 1 << 4, /* VK_DYNAMIC_STATE_BLEND_CONSTANTS */
   ANV_CMD_DIRTY_DYNAMIC_DEPTH_BOUNDS              = 1 << 5, /* VK_DYNAMIC_STATE_DEPTH_BOUNDS */
   ANV_CMD_DIRTY_DYNAMIC_STENCIL_COMPARE_MASK      = 1 << 6, /* VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK */
   ANV_CMD_DIRTY_DYNAMIC_STENCIL_WRITE_MASK        = 1 << 7, /* VK_DYNAMIC_STATE_STENCIL_WRITE_MASK */
   ANV_CMD_DIRTY_DYNAMIC_STENCIL_REFERENCE         = 1 << 8, /* VK_DYNAMIC_STATE_STENCIL_REFERENCE */
   ANV_CMD_DIRTY_DYNAMIC_ALL                       = (1 << 9) - 1,
   ANV_CMD_DIRTY_PIPELINE                          = 1 << 9,
   ANV_CMD_DIRTY_INDEX_BUFFER                      = 1 << 10,
   ANV_CMD_DIRTY_RENDER_TARGETS                    = 1 << 11,
};
typedef uint32_t anv_cmd_dirty_mask_t;

enum anv_pipe_bits {
   ANV_PIPE_DEPTH_CACHE_FLUSH_BIT            = (1 << 0),
   ANV_PIPE_STALL_AT_SCOREBOARD_BIT          = (1 << 1),
   ANV_PIPE_STATE_CACHE_INVALIDATE_BIT       = (1 << 2),
   ANV_PIPE_CONSTANT_CACHE_INVALIDATE_BIT    = (1 << 3),
   ANV_PIPE_VF_CACHE_INVALIDATE_BIT          = (1 << 4),
   ANV_PIPE_DATA_CACHE_FLUSH_BIT             = (1 << 5),
   ANV_PIPE_TEXTURE_CACHE_INVALIDATE_BIT     = (1 << 10),
   ANV_PIPE_INSTRUCTION_CACHE_INVALIDATE_BIT = (1 << 11),
   ANV_PIPE_RENDER_TARGET_CACHE_FLUSH_BIT    = (1 << 12),
   ANV_PIPE_DEPTH_STALL_BIT                  = (1 << 13),
   ANV_PIPE_CS_STALL_BIT                     = (1 << 20),

   /* This bit does not exist directly in PIPE_CONTROL.  Instead it means that
    * a flush has happened but not a CS stall.  The next time we do any sort
    * of invalidation we need to insert a CS stall at that time.  Otherwise,
    * we would have to CS stall on every flush which could be bad.
    */
   ANV_PIPE_NEEDS_CS_STALL_BIT               = (1 << 21),
};

#define ANV_PIPE_FLUSH_BITS ( \
   ANV_PIPE_DEPTH_CACHE_FLUSH_BIT | \
   ANV_PIPE_DATA_CACHE_FLUSH_BIT | \
   ANV_PIPE_RENDER_TARGET_CACHE_FLUSH_BIT)

#define ANV_PIPE_STALL_BITS ( \
   ANV_PIPE_STALL_AT_SCOREBOARD_BIT | \
   ANV_PIPE_DEPTH_STALL_BIT | \
   ANV_PIPE_CS_STALL_BIT)

#define ANV_PIPE_INVALIDATE_BITS ( \
   ANV_PIPE_STATE_CACHE_INVALIDATE_BIT | \
   ANV_PIPE_CONSTANT_CACHE_INVALIDATE_BIT | \
   ANV_PIPE_VF_CACHE_INVALIDATE_BIT | \
   ANV_PIPE_DATA_CACHE_FLUSH_BIT | \
   ANV_PIPE_TEXTURE_CACHE_INVALIDATE_BIT | \
   ANV_PIPE_INSTRUCTION_CACHE_INVALIDATE_BIT)

struct anv_vertex_binding {
   struct anv_buffer *                          buffer;
   VkDeviceSize                                 offset;
};

struct anv_push_constants {
   /* Current allocated size of this push constants data structure.
    * Because a decent chunk of it may not be used (images on SKL, for
    * instance), we won't actually allocate the entire structure up-front.
    */
   uint32_t size;

   /* Push constant data provided by the client through vkPushConstants */
   uint8_t client_data[MAX_PUSH_CONSTANTS_SIZE];

   /* Our hardware only provides zero-based vertex and instance id so, in
    * order to satisfy the vulkan requirements, we may have to push one or
    * both of these into the shader.
    */
   uint32_t base_vertex;
   uint32_t base_instance;

   /* Offsets and ranges for dynamically bound buffers */
   struct {
      uint32_t offset;
      uint32_t range;
   } dynamic[MAX_DYNAMIC_BUFFERS];

   /* Image data for image_load_store on pre-SKL */
   struct brw_image_param images[MAX_IMAGES];
};

struct anv_dynamic_state {
   struct {
      uint32_t                                  count;
      VkViewport                                viewports[MAX_VIEWPORTS];
   } viewport;

   struct {
      uint32_t                                  count;
      VkRect2D                                  scissors[MAX_SCISSORS];
   } scissor;

   float                                        line_width;

   struct {
      float                                     bias;
      float                                     clamp;
      float                                     slope;
   } depth_bias;

   float                                        blend_constants[4];

   struct {
      float                                     min;
      float                                     max;
   } depth_bounds;

   struct {
      uint32_t                                  front;
      uint32_t                                  back;
   } stencil_compare_mask;

   struct {
      uint32_t                                  front;
      uint32_t                                  back;
   } stencil_write_mask;

   struct {
      uint32_t                                  front;
      uint32_t                                  back;
   } stencil_reference;
};

extern const struct anv_dynamic_state default_dynamic_state;

void anv_dynamic_state_copy(struct anv_dynamic_state *dest,
                            const struct anv_dynamic_state *src,
                            uint32_t copy_mask);

/**
 * Attachment state when recording a renderpass instance.
 *
 * The clear value is valid only if there exists a pending clear.
 */
struct anv_attachment_state {
   enum isl_aux_usage                           aux_usage;
   enum isl_aux_usage                           input_aux_usage;
   struct anv_state                             color_rt_state;
   struct anv_state                             input_att_state;

   VkImageLayout                                current_layout;
   VkImageAspectFlags                           pending_clear_aspects;
   bool                                         fast_clear;
   VkClearValue                                 clear_value;
   bool                                         clear_color_is_zero_one;
};

/** State required while building cmd buffer */
struct anv_cmd_state {
   /* PIPELINE_SELECT.PipelineSelection */
   uint32_t                                     current_pipeline;
   const struct gen_l3_config *                 current_l3_config;
   uint32_t                                     vb_dirty;
   anv_cmd_dirty_mask_t                         dirty;
   anv_cmd_dirty_mask_t                         compute_dirty;
   enum anv_pipe_bits                           pending_pipe_bits;
   uint32_t                                     num_workgroups_offset;
   struct anv_bo                                *num_workgroups_bo;
   VkShaderStageFlags                           descriptors_dirty;
   VkShaderStageFlags                           push_constants_dirty;
   uint32_t                                     scratch_size;
   struct anv_pipeline *                        pipeline;
   struct anv_pipeline *                        compute_pipeline;
   struct anv_framebuffer *                     framebuffer;
   struct anv_render_pass *                     pass;
   struct anv_subpass *                         subpass;
   VkRect2D                                     render_area;
   uint32_t                                     restart_index;
   struct anv_vertex_binding                    vertex_bindings[MAX_VBS];
   struct anv_descriptor_set *                  descriptors[MAX_SETS];
   VkShaderStageFlags                           push_constant_stages;
   struct anv_push_constants *                  push_constants[MESA_SHADER_STAGES];
   struct anv_state                             binding_tables[MESA_SHADER_STAGES];
   struct anv_state                             samplers[MESA_SHADER_STAGES];
   struct anv_dynamic_state                     dynamic;
   bool                                         need_query_wa;

   /**
    * Array length is anv_cmd_state::pass::attachment_count. Array content is
    * valid only when recording a render pass instance.
    */
   struct anv_attachment_state *                attachments;

   /**
    * Surface states for color render targets.  These are stored in a single
    * flat array.  For depth-stencil attachments, the surface state is simply
    * left blank.
    */
   struct anv_state                             render_pass_states;

   /**
    * A null surface state of the right size to match the framebuffer.  This
    * is one of the states in render_pass_states.
    */
   struct anv_state                             null_surface_state;

   struct {
      struct anv_buffer *                       index_buffer;
      uint32_t                                  index_type; /**< 3DSTATE_INDEX_BUFFER.IndexFormat */
      uint32_t                                  index_offset;
   } gen7;
};

struct anv_cmd_pool {
   VkAllocationCallbacks                        alloc;
   struct list_head                             cmd_buffers;
};

#define ANV_CMD_BUFFER_BATCH_SIZE 8192

enum anv_cmd_buffer_exec_mode {
   ANV_CMD_BUFFER_EXEC_MODE_PRIMARY,
   ANV_CMD_BUFFER_EXEC_MODE_EMIT,
   ANV_CMD_BUFFER_EXEC_MODE_GROW_AND_EMIT,
   ANV_CMD_BUFFER_EXEC_MODE_CHAIN,
   ANV_CMD_BUFFER_EXEC_MODE_COPY_AND_CHAIN,
};

struct anv_cmd_buffer {
   VK_LOADER_DATA                               _loader_data;

   struct anv_device *                          device;

   struct anv_cmd_pool *                        pool;
   struct list_head                             pool_link;

   struct anv_batch                             batch;

   /* Fields required for the actual chain of anv_batch_bo's.
    *
    * These fields are initialized by anv_cmd_buffer_init_batch_bo_chain().
    */
   struct list_head                             batch_bos;
   enum anv_cmd_buffer_exec_mode                exec_mode;

   /* A vector of anv_batch_bo pointers for every batch or surface buffer
    * referenced by this command buffer
    *
    * initialized by anv_cmd_buffer_init_batch_bo_chain()
    */
   struct u_vector                            seen_bbos;

   /* A vector of int32_t's for every block of binding tables.
    *
    * initialized by anv_cmd_buffer_init_batch_bo_chain()
    */
   struct u_vector                            bt_blocks;
   uint32_t                                     bt_next;

   struct anv_reloc_list                        surface_relocs;
   /** Last seen surface state block pool center bo offset */
   uint32_t                                     last_ss_pool_center;

   /* Serial for tracking buffer completion */
   uint32_t                                     serial;

   /* Stream objects for storing temporary data */
   struct anv_state_stream                      surface_state_stream;
   struct anv_state_stream                      dynamic_state_stream;

   VkCommandBufferUsageFlags                    usage_flags;
   VkCommandBufferLevel                         level;

   struct anv_cmd_state                         state;
};

VkResult anv_cmd_buffer_init_batch_bo_chain(struct anv_cmd_buffer *cmd_buffer);
void anv_cmd_buffer_fini_batch_bo_chain(struct anv_cmd_buffer *cmd_buffer);
void anv_cmd_buffer_reset_batch_bo_chain(struct anv_cmd_buffer *cmd_buffer);
void anv_cmd_buffer_end_batch_buffer(struct anv_cmd_buffer *cmd_buffer);
void anv_cmd_buffer_add_secondary(struct anv_cmd_buffer *primary,
                                  struct anv_cmd_buffer *secondary);
void anv_cmd_buffer_prepare_execbuf(struct anv_cmd_buffer *cmd_buffer);
VkResult anv_cmd_buffer_execbuf(struct anv_device *device,
                                struct anv_cmd_buffer *cmd_buffer);

VkResult anv_cmd_buffer_reset(struct anv_cmd_buffer *cmd_buffer);

VkResult
anv_cmd_buffer_ensure_push_constants_size(struct anv_cmd_buffer *cmd_buffer,
                                          gl_shader_stage stage, uint32_t size);
#define anv_cmd_buffer_ensure_push_constant_field(cmd_buffer, stage, field) \
   anv_cmd_buffer_ensure_push_constants_size(cmd_buffer, stage, \
      (offsetof(struct anv_push_constants, field) + \
       sizeof(cmd_buffer->state.push_constants[0]->field)))

struct anv_state anv_cmd_buffer_emit_dynamic(struct anv_cmd_buffer *cmd_buffer,
                                             const void *data, uint32_t size, uint32_t alignment);
struct anv_state anv_cmd_buffer_merge_dynamic(struct anv_cmd_buffer *cmd_buffer,
                                              uint32_t *a, uint32_t *b,
                                              uint32_t dwords, uint32_t alignment);

struct anv_address
anv_cmd_buffer_surface_base_address(struct anv_cmd_buffer *cmd_buffer);
struct anv_state
anv_cmd_buffer_alloc_binding_table(struct anv_cmd_buffer *cmd_buffer,
                                   uint32_t entries, uint32_t *state_offset);
struct anv_state
anv_cmd_buffer_alloc_surface_state(struct anv_cmd_buffer *cmd_buffer);
struct anv_state
anv_cmd_buffer_alloc_dynamic_state(struct anv_cmd_buffer *cmd_buffer,
                                   uint32_t size, uint32_t alignment);

VkResult
anv_cmd_buffer_new_binding_table_block(struct anv_cmd_buffer *cmd_buffer);

void gen8_cmd_buffer_emit_viewport(struct anv_cmd_buffer *cmd_buffer);
void gen8_cmd_buffer_emit_depth_viewport(struct anv_cmd_buffer *cmd_buffer,
                                         bool depth_clamp_enable);
void gen7_cmd_buffer_emit_scissor(struct anv_cmd_buffer *cmd_buffer);

void anv_cmd_buffer_setup_attachments(struct anv_cmd_buffer *cmd_buffer,
                                      struct anv_render_pass *pass,
                                      struct anv_framebuffer *framebuffer,
                                      const VkClearValue *clear_values);

void anv_cmd_buffer_emit_state_base_address(struct anv_cmd_buffer *cmd_buffer);

struct anv_state
anv_cmd_buffer_push_constants(struct anv_cmd_buffer *cmd_buffer,
                              gl_shader_stage stage);
struct anv_state
anv_cmd_buffer_cs_push_constants(struct anv_cmd_buffer *cmd_buffer);

void anv_cmd_buffer_clear_subpass(struct anv_cmd_buffer *cmd_buffer);
void anv_cmd_buffer_resolve_subpass(struct anv_cmd_buffer *cmd_buffer);

const struct anv_image_view *
anv_cmd_buffer_get_depth_stencil_view(const struct anv_cmd_buffer *cmd_buffer);

struct anv_state
anv_cmd_buffer_alloc_blorp_binding_table(struct anv_cmd_buffer *cmd_buffer,
                                         uint32_t num_entries,
                                         uint32_t *state_offset);

void anv_cmd_buffer_dump(struct anv_cmd_buffer *cmd_buffer);

enum anv_fence_state {
   /** Indicates that this is a new (or newly reset fence) */
   ANV_FENCE_STATE_RESET,

   /** Indicates that this fence has been submitted to the GPU but is still
    * (as far as we know) in use by the GPU.
    */
   ANV_FENCE_STATE_SUBMITTED,

   ANV_FENCE_STATE_SIGNALED,
};

struct anv_fence {
   struct anv_bo bo;
   struct drm_i915_gem_execbuffer2 execbuf;
   struct drm_i915_gem_exec_object2 exec2_objects[1];
   enum anv_fence_state state;
};

struct anv_event {
   uint64_t                                     semaphore;
   struct anv_state                             state;
};

struct anv_shader_module {
   unsigned char                                sha1[20];
   uint32_t                                     size;
   char                                         data[0];
};

void anv_hash_shader(unsigned char *hash, const void *key, size_t key_size,
                     struct anv_shader_module *module,
                     const char *entrypoint,
                     const struct anv_pipeline_layout *pipeline_layout,
                     const VkSpecializationInfo *spec_info);

static inline gl_shader_stage
vk_to_mesa_shader_stage(VkShaderStageFlagBits vk_stage)
{
   assert(__builtin_popcount(vk_stage) == 1);
   return ffs(vk_stage) - 1;
}

static inline VkShaderStageFlagBits
mesa_to_vk_shader_stage(gl_shader_stage mesa_stage)
{
   return (1 << mesa_stage);
}

#define ANV_STAGE_MASK ((1 << MESA_SHADER_STAGES) - 1)

#define anv_foreach_stage(stage, stage_bits)                         \
   for (gl_shader_stage stage,                                       \
        __tmp = (gl_shader_stage)((stage_bits) & ANV_STAGE_MASK);    \
        stage = __builtin_ffs(__tmp) - 1, __tmp;                     \
        __tmp &= ~(1 << (stage)))

struct anv_pipeline_bind_map {
   uint32_t surface_count;
   uint32_t sampler_count;
   uint32_t image_count;

   struct anv_pipeline_binding *                surface_to_descriptor;
   struct anv_pipeline_binding *                sampler_to_descriptor;
};

struct anv_shader_bin_key {
   uint32_t size;
   uint8_t data[0];
};

struct anv_shader_bin {
   uint32_t ref_cnt;

   const struct anv_shader_bin_key *key;

   struct anv_state kernel;
   uint32_t kernel_size;

   const struct brw_stage_prog_data *prog_data;
   uint32_t prog_data_size;

   struct anv_pipeline_bind_map bind_map;

   /* Prog data follows, then params, then the key, all aligned to 8-bytes */
};

struct anv_shader_bin *
anv_shader_bin_create(struct anv_device *device,
                      const void *key, uint32_t key_size,
                      const void *kernel, uint32_t kernel_size,
                      const struct brw_stage_prog_data *prog_data,
                      uint32_t prog_data_size, const void *prog_data_param,
                      const struct anv_pipeline_bind_map *bind_map);

void
anv_shader_bin_destroy(struct anv_device *device, struct anv_shader_bin *shader);

static inline void
anv_shader_bin_ref(struct anv_shader_bin *shader)
{
   assert(shader->ref_cnt >= 1);
   __sync_fetch_and_add(&shader->ref_cnt, 1);
}

static inline void
anv_shader_bin_unref(struct anv_device *device, struct anv_shader_bin *shader)
{
   assert(shader->ref_cnt >= 1);
   if (__sync_fetch_and_add(&shader->ref_cnt, -1) == 1)
      anv_shader_bin_destroy(device, shader);
}

struct anv_pipeline {
   struct anv_device *                          device;
   struct anv_batch                             batch;
   uint32_t                                     batch_data[512];
   struct anv_reloc_list                        batch_relocs;
   uint32_t                                     dynamic_state_mask;
   struct anv_dynamic_state                     dynamic_state;

   struct anv_pipeline_layout *                 layout;

   bool                                         needs_data_cache;

   struct anv_shader_bin *                      shaders[MESA_SHADER_STAGES];

   struct {
      const struct gen_l3_config *              l3_config;
      uint32_t                                  total_size;
   } urb;

   VkShaderStageFlags                           active_stages;
   struct anv_state                             blend_state;

   uint32_t                                     vb_used;
   uint32_t                                     binding_stride[MAX_VBS];
   bool                                         instancing_enable[MAX_VBS];
   bool                                         primitive_restart;
   uint32_t                                     topology;

   uint32_t                                     cs_right_mask;

   bool                                         depth_clamp_enable;

   struct {
      uint32_t                                  sf[7];
      uint32_t                                  depth_stencil_state[3];
   } gen7;

   struct {
      uint32_t                                  sf[4];
      uint32_t                                  raster[5];
      uint32_t                                  wm_depth_stencil[3];
   } gen8;

   struct {
      uint32_t                                  wm_depth_stencil[4];
   } gen9;

   uint32_t                                     interface_descriptor_data[8];
};

static inline bool
anv_pipeline_has_stage(const struct anv_pipeline *pipeline,
                       gl_shader_stage stage)
{
   return (pipeline->active_stages & mesa_to_vk_shader_stage(stage)) != 0;
}

#define ANV_DECL_GET_PROG_DATA_FUNC(prefix, stage)                   \
static inline const struct brw_##prefix##_prog_data *                \
get_##prefix##_prog_data(const struct anv_pipeline *pipeline)        \
{                                                                    \
   if (anv_pipeline_has_stage(pipeline, stage)) {                    \
      return (const struct brw_##prefix##_prog_data *)               \
             pipeline->shaders[stage]->prog_data;                    \
   } else {                                                          \
      return NULL;                                                   \
   }                                                                 \
}

ANV_DECL_GET_PROG_DATA_FUNC(vs, MESA_SHADER_VERTEX)
ANV_DECL_GET_PROG_DATA_FUNC(tcs, MESA_SHADER_TESS_CTRL)
ANV_DECL_GET_PROG_DATA_FUNC(tes, MESA_SHADER_TESS_EVAL)
ANV_DECL_GET_PROG_DATA_FUNC(gs, MESA_SHADER_GEOMETRY)
ANV_DECL_GET_PROG_DATA_FUNC(wm, MESA_SHADER_FRAGMENT)
ANV_DECL_GET_PROG_DATA_FUNC(cs, MESA_SHADER_COMPUTE)

static inline const struct brw_vue_prog_data *
anv_pipeline_get_last_vue_prog_data(const struct anv_pipeline *pipeline)
{
   if (anv_pipeline_has_stage(pipeline, MESA_SHADER_GEOMETRY))
      return &get_gs_prog_data(pipeline)->base;
   else if (anv_pipeline_has_stage(pipeline, MESA_SHADER_TESS_EVAL))
      return &get_tes_prog_data(pipeline)->base;
   else
      return &get_vs_prog_data(pipeline)->base;
}

VkResult
anv_pipeline_init(struct anv_pipeline *pipeline, struct anv_device *device,
                  struct anv_pipeline_cache *cache,
                  const VkGraphicsPipelineCreateInfo *pCreateInfo,
                  const VkAllocationCallbacks *alloc);

VkResult
anv_pipeline_compile_cs(struct anv_pipeline *pipeline,
                        struct anv_pipeline_cache *cache,
                        const VkComputePipelineCreateInfo *info,
                        struct anv_shader_module *module,
                        const char *entrypoint,
                        const VkSpecializationInfo *spec_info);

struct anv_format {
   enum isl_format isl_format:16;
   struct isl_swizzle swizzle;
};

struct anv_format
anv_get_format(const struct gen_device_info *devinfo, VkFormat format,
               VkImageAspectFlags aspect, VkImageTiling tiling);

static inline enum isl_format
anv_get_isl_format(const struct gen_device_info *devinfo, VkFormat vk_format,
                   VkImageAspectFlags aspect, VkImageTiling tiling)
{
   return anv_get_format(devinfo, vk_format, aspect, tiling).isl_format;
}

void
anv_pipeline_setup_l3_config(struct anv_pipeline *pipeline, bool needs_slm);

/**
 * Subsurface of an anv_image.
 */
struct anv_surface {
   /** Valid only if isl_surf::size > 0. */
   struct isl_surf isl;

   /**
    * Offset from VkImage's base address, as bound by vkBindImageMemory().
    */
   uint32_t offset;
};

struct anv_image {
   VkImageType type;
   /* The original VkFormat provided by the client.  This may not match any
    * of the actual surface formats.
    */
   VkFormat vk_format;
   VkImageAspectFlags aspects;
   VkExtent3D extent;
   uint32_t levels;
   uint32_t array_size;
   uint32_t samples; /**< VkImageCreateInfo::samples */
   VkImageUsageFlags usage; /**< Superset of VkImageCreateInfo::usage. */
   VkImageTiling tiling; /** VkImageCreateInfo::tiling */

   VkDeviceSize size;
   uint32_t alignment;

   /* Set when bound */
   struct anv_bo *bo;
   VkDeviceSize offset;

   /**
    * Image subsurfaces
    *
    * For each foo, anv_image::foo_surface is valid if and only if
    * anv_image::aspects has a foo aspect.
    *
    * The hardware requires that the depth buffer and stencil buffer be
    * separate surfaces.  From Vulkan's perspective, though, depth and stencil
    * reside in the same VkImage.  To satisfy both the hardware and Vulkan, we
    * allocate the depth and stencil buffers as separate surfaces in the same
    * bo.
    */
   union {
      struct anv_surface color_surface;

      struct {
         struct anv_surface depth_surface;
         struct anv_surface stencil_surface;
      };
   };

   /**
    * For color images, this is the aux usage for this image when not used as a
    * color attachment.
    * 
    * For depth/stencil images, this is set to ISL_AUX_USAGE_HIZ if the image
    * has a HiZ buffer.
    */
   enum isl_aux_usage aux_usage;

   struct anv_surface aux_surface;
};

/* Returns true if a HiZ-enabled depth buffer can be sampled from. */
static inline bool
anv_can_sample_with_hiz(uint8_t gen, uint32_t samples)
{
   return gen >= 8 && samples == 1;
}

void
anv_gen8_hiz_op_resolve(struct anv_cmd_buffer *cmd_buffer,
                        const struct anv_image *image,
                        enum blorp_hiz_op op);

/* This is defined as a macro so that it works for both
 * VkImageSubresourceRange and VkImageSubresourceLayers
 */
#define anv_get_layerCount(_image, _range) \
   ((_range)->layerCount == VK_REMAINING_ARRAY_LAYERS ? \
    (_image)->array_size - (_range)->baseArrayLayer : (_range)->layerCount)

static inline uint32_t
anv_get_levelCount(const struct anv_image *image,
                   const VkImageSubresourceRange *range)
{
   return range->levelCount == VK_REMAINING_MIP_LEVELS ?
          image->levels - range->baseMipLevel : range->levelCount;
}


struct anv_image_view {
   const struct anv_image *image; /**< VkImageViewCreateInfo::image */
   struct anv_bo *bo;
   uint32_t offset; /**< Offset into bo. */

   struct isl_view isl;

   VkImageAspectFlags aspect_mask;
   VkFormat vk_format;
   VkExtent3D extent; /**< Extent of VkImageViewCreateInfo::baseMipLevel. */

   /** RENDER_SURFACE_STATE when using image as a sampler surface. */
   struct anv_state sampler_surface_state;

   /** RENDER_SURFACE_STATE when using image as a storage image. */
   struct anv_state storage_surface_state;

   struct brw_image_param storage_image_param;
};

struct anv_image_create_info {
   const VkImageCreateInfo *vk_info;

   /** An opt-in bitmask which filters an ISL-mapping of the Vulkan tiling. */
   isl_tiling_flags_t isl_tiling_flags;

   uint32_t stride;
};

VkResult anv_image_create(VkDevice _device,
                          const struct anv_image_create_info *info,
                          const VkAllocationCallbacks* alloc,
                          VkImage *pImage);

const struct anv_surface *
anv_image_get_surface_for_aspect_mask(const struct anv_image *image,
                                      VkImageAspectFlags aspect_mask);

struct anv_buffer_view {
   enum isl_format format; /**< VkBufferViewCreateInfo::format */
   struct anv_bo *bo;
   uint32_t offset; /**< Offset into bo. */
   uint64_t range; /**< VkBufferViewCreateInfo::range */

   struct anv_state surface_state;
   struct anv_state storage_surface_state;

   struct brw_image_param storage_image_param;
};

enum isl_format
anv_isl_format_for_descriptor_type(VkDescriptorType type);

static inline struct VkExtent3D
anv_sanitize_image_extent(const VkImageType imageType,
                          const struct VkExtent3D imageExtent)
{
   switch (imageType) {
   case VK_IMAGE_TYPE_1D:
      return (VkExtent3D) { imageExtent.width, 1, 1 };
   case VK_IMAGE_TYPE_2D:
      return (VkExtent3D) { imageExtent.width, imageExtent.height, 1 };
   case VK_IMAGE_TYPE_3D:
      return imageExtent;
   default:
      unreachable("invalid image type");
   }
}

static inline struct VkOffset3D
anv_sanitize_image_offset(const VkImageType imageType,
                          const struct VkOffset3D imageOffset)
{
   switch (imageType) {
   case VK_IMAGE_TYPE_1D:
      return (VkOffset3D) { imageOffset.x, 0, 0 };
   case VK_IMAGE_TYPE_2D:
      return (VkOffset3D) { imageOffset.x, imageOffset.y, 0 };
   case VK_IMAGE_TYPE_3D:
      return imageOffset;
   default:
      unreachable("invalid image type");
   }
}


void anv_fill_buffer_surface_state(struct anv_device *device,
                                   struct anv_state state,
                                   enum isl_format format,
                                   uint32_t offset, uint32_t range,
                                   uint32_t stride);

void anv_image_view_fill_image_param(struct anv_device *device,
                                     struct anv_image_view *view,
                                     struct brw_image_param *param);
void anv_buffer_view_fill_image_param(struct anv_device *device,
                                      struct anv_buffer_view *view,
                                      struct brw_image_param *param);

struct anv_sampler {
   uint32_t state[4];
};

struct anv_framebuffer {
   uint32_t                                     width;
   uint32_t                                     height;
   uint32_t                                     layers;

   uint32_t                                     attachment_count;
   struct anv_image_view *                      attachments[0];
};

struct anv_subpass {
   uint32_t                                     input_count;
   uint32_t *                                   input_attachments;
   uint32_t                                     color_count;
   uint32_t *                                   color_attachments;
   uint32_t *                                   resolve_attachments;

   /* TODO: Consider storing the depth/stencil VkAttachmentReference
    * instead of its two structure members (below) individually.
    */
   uint32_t                                     depth_stencil_attachment;
   VkImageLayout                                depth_stencil_layout;

   /** Subpass has a depth/stencil self-dependency */
   bool                                         has_ds_self_dep;

   /** Subpass has at least one resolve attachment */
   bool                                         has_resolve;
};

enum anv_subpass_usage {
   ANV_SUBPASS_USAGE_DRAW =         (1 << 0),
   ANV_SUBPASS_USAGE_INPUT =        (1 << 1),
   ANV_SUBPASS_USAGE_RESOLVE_SRC =  (1 << 2),
   ANV_SUBPASS_USAGE_RESOLVE_DST =  (1 << 3),
};

struct anv_render_pass_attachment {
   /* TODO: Consider using VkAttachmentDescription instead of storing each of
    * its members individually.
    */
   VkFormat                                     format;
   uint32_t                                     samples;
   VkImageUsageFlags                            usage;
   VkAttachmentLoadOp                           load_op;
   VkAttachmentStoreOp                          store_op;
   VkAttachmentLoadOp                           stencil_load_op;
   VkImageLayout                                initial_layout;
   VkImageLayout                                final_layout;

   /* An array, indexed by subpass id, of how the attachment will be used. */
   enum anv_subpass_usage *                     subpass_usage;

   /* The subpass id in which the attachment will be used last. */
   uint32_t                                     last_subpass_idx;
};

struct anv_render_pass {
   uint32_t                                     attachment_count;
   uint32_t                                     subpass_count;
   uint32_t *                                   subpass_attachments;
   enum anv_subpass_usage *                     subpass_usages;
   struct anv_render_pass_attachment *          attachments;
   struct anv_subpass                           subpasses[0];
};

struct anv_query_pool_slot {
   uint64_t begin;
   uint64_t end;
   uint64_t available;
};

struct anv_query_pool {
   VkQueryType                                  type;
   uint32_t                                     slots;
   struct anv_bo                                bo;
};

void *anv_lookup_entrypoint(const struct gen_device_info *devinfo,
                            const char *name);

void anv_dump_image_to_ppm(struct anv_device *device,
                           struct anv_image *image, unsigned miplevel,
                           unsigned array_layer, VkImageAspectFlagBits aspect,
                           const char *filename);

enum anv_dump_action {
   ANV_DUMP_FRAMEBUFFERS_BIT = 0x1,
};

void anv_dump_start(struct anv_device *device, enum anv_dump_action actions);
void anv_dump_finish(void);

void anv_dump_add_framebuffer(struct anv_cmd_buffer *cmd_buffer,
                              struct anv_framebuffer *fb);

#define ANV_DEFINE_HANDLE_CASTS(__anv_type, __VkType)                      \
                                                                           \
   static inline struct __anv_type *                                       \
   __anv_type ## _from_handle(__VkType _handle)                            \
   {                                                                       \
      return (struct __anv_type *) _handle;                                \
   }                                                                       \
                                                                           \
   static inline __VkType                                                  \
   __anv_type ## _to_handle(struct __anv_type *_obj)                       \
   {                                                                       \
      return (__VkType) _obj;                                              \
   }

#define ANV_DEFINE_NONDISP_HANDLE_CASTS(__anv_type, __VkType)              \
                                                                           \
   static inline struct __anv_type *                                       \
   __anv_type ## _from_handle(__VkType _handle)                            \
   {                                                                       \
      return (struct __anv_type *)(uintptr_t) _handle;                     \
   }                                                                       \
                                                                           \
   static inline __VkType                                                  \
   __anv_type ## _to_handle(struct __anv_type *_obj)                       \
   {                                                                       \
      return (__VkType)(uintptr_t) _obj;                                   \
   }

#define ANV_FROM_HANDLE(__anv_type, __name, __handle) \
   struct __anv_type *__name = __anv_type ## _from_handle(__handle)

ANV_DEFINE_HANDLE_CASTS(anv_cmd_buffer, VkCommandBuffer)
ANV_DEFINE_HANDLE_CASTS(anv_device, VkDevice)
ANV_DEFINE_HANDLE_CASTS(anv_instance, VkInstance)
ANV_DEFINE_HANDLE_CASTS(anv_physical_device, VkPhysicalDevice)
ANV_DEFINE_HANDLE_CASTS(anv_queue, VkQueue)

ANV_DEFINE_NONDISP_HANDLE_CASTS(anv_cmd_pool, VkCommandPool)
ANV_DEFINE_NONDISP_HANDLE_CASTS(anv_buffer, VkBuffer)
ANV_DEFINE_NONDISP_HANDLE_CASTS(anv_buffer_view, VkBufferView)
ANV_DEFINE_NONDISP_HANDLE_CASTS(anv_descriptor_pool, VkDescriptorPool)
ANV_DEFINE_NONDISP_HANDLE_CASTS(anv_descriptor_set, VkDescriptorSet)
ANV_DEFINE_NONDISP_HANDLE_CASTS(anv_descriptor_set_layout, VkDescriptorSetLayout)
ANV_DEFINE_NONDISP_HANDLE_CASTS(anv_device_memory, VkDeviceMemory)
ANV_DEFINE_NONDISP_HANDLE_CASTS(anv_fence, VkFence)
ANV_DEFINE_NONDISP_HANDLE_CASTS(anv_event, VkEvent)
ANV_DEFINE_NONDISP_HANDLE_CASTS(anv_framebuffer, VkFramebuffer)
ANV_DEFINE_NONDISP_HANDLE_CASTS(anv_image, VkImage)
ANV_DEFINE_NONDISP_HANDLE_CASTS(anv_image_view, VkImageView);
ANV_DEFINE_NONDISP_HANDLE_CASTS(anv_pipeline_cache, VkPipelineCache)
ANV_DEFINE_NONDISP_HANDLE_CASTS(anv_pipeline, VkPipeline)
ANV_DEFINE_NONDISP_HANDLE_CASTS(anv_pipeline_layout, VkPipelineLayout)
ANV_DEFINE_NONDISP_HANDLE_CASTS(anv_query_pool, VkQueryPool)
ANV_DEFINE_NONDISP_HANDLE_CASTS(anv_render_pass, VkRenderPass)
ANV_DEFINE_NONDISP_HANDLE_CASTS(anv_sampler, VkSampler)
ANV_DEFINE_NONDISP_HANDLE_CASTS(anv_shader_module, VkShaderModule)

/* Gen-specific function declarations */
#ifdef genX
#  include "anv_genX.h"
#else
#  define genX(x) gen7_##x
#  include "anv_genX.h"
#  undef genX
#  define genX(x) gen75_##x
#  include "anv_genX.h"
#  undef genX
#  define genX(x) gen8_##x
#  include "anv_genX.h"
#  undef genX
#  define genX(x) gen9_##x
#  include "anv_genX.h"
#  undef genX
#endif

#ifdef __cplusplus
}
#endif

#endif /* ANV_PRIVATE_H */
