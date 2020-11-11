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
#include "drm-uapi/i915_drm.h"

#ifdef HAVE_VALGRIND
#include <valgrind.h>
#include <memcheck.h>
#define VG(x) x
#ifndef NDEBUG
#define __gen_validate_value(x) VALGRIND_CHECK_MEM_IS_DEFINED(&(x), sizeof(x))
#endif
#else
#define VG(x) ((void)0)
#endif

#include "common/gen_clflush.h"
#include "common/gen_decoder.h"
#include "common/gen_gem.h"
#include "common/gen_l3_config.h"
#include "dev/gen_device_info.h"
#include "blorp/blorp.h"
#include "compiler/brw_compiler.h"
#include "util/bitset.h"
#include "util/macros.h"
#include "util/hash_table.h"
#include "util/list.h"
#include "util/sparse_array.h"
#include "util/u_atomic.h"
#include "util/u_vector.h"
#include "util/u_math.h"
#include "util/vma.h"
#include "util/xmlconfig.h"
#include "vk_alloc.h"
#include "vk_debug_report.h"
#include "vk_object.h"

/* Pre-declarations needed for WSI entrypoints */
struct wl_surface;
struct wl_display;
typedef struct xcb_connection_t xcb_connection_t;
typedef uint32_t xcb_visualid_t;
typedef uint32_t xcb_window_t;

struct anv_batch;
struct anv_buffer;
struct anv_buffer_view;
struct anv_image_view;
struct anv_instance;

struct gen_aux_map_context;
struct gen_perf_config;
struct gen_perf_counter_pass;
struct gen_perf_query_result;

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_intel.h>
#include <vulkan/vk_icd.h>

#include "anv_android.h"
#include "anv_entrypoints.h"
#include "anv_extensions.h"
#include "isl/isl.h"

#include "dev/gen_debug.h"
#define MESA_LOG_TAG "MESA-INTEL"
#include "util/log.h"
#include "wsi_common.h"

#define NSEC_PER_SEC 1000000000ull

/* anv Virtual Memory Layout
 * =========================
 *
 * When the anv driver is determining the virtual graphics addresses of memory
 * objects itself using the softpin mechanism, the following memory ranges
 * will be used.
 *
 * Three special considerations to notice:
 *
 * (1) the dynamic state pool is located within the same 4 GiB as the low
 * heap. This is to work around a VF cache issue described in a comment in
 * anv_physical_device_init_heaps.
 *
 * (2) the binding table pool is located at lower addresses than the surface
 * state pool, within a 4 GiB range. This allows surface state base addresses
 * to cover both binding tables (16 bit offsets) and surface states (32 bit
 * offsets).
 *
 * (3) the last 4 GiB of the address space is withheld from the high
 * heap. Various hardware units will read past the end of an object for
 * various reasons. This healthy margin prevents reads from wrapping around
 * 48-bit addresses.
 */
#define LOW_HEAP_MIN_ADDRESS               0x000000001000ULL /* 4 KiB */
#define LOW_HEAP_MAX_ADDRESS               0x0000bfffffffULL
#define DYNAMIC_STATE_POOL_MIN_ADDRESS     0x0000c0000000ULL /* 3 GiB */
#define DYNAMIC_STATE_POOL_MAX_ADDRESS     0x0000ffffffffULL
#define BINDING_TABLE_POOL_MIN_ADDRESS     0x000100000000ULL /* 4 GiB */
#define BINDING_TABLE_POOL_MAX_ADDRESS     0x00013fffffffULL
#define SURFACE_STATE_POOL_MIN_ADDRESS     0x000140000000ULL /* 5 GiB */
#define SURFACE_STATE_POOL_MAX_ADDRESS     0x00017fffffffULL
#define INSTRUCTION_STATE_POOL_MIN_ADDRESS 0x000180000000ULL /* 6 GiB */
#define INSTRUCTION_STATE_POOL_MAX_ADDRESS 0x0001bfffffffULL
#define CLIENT_VISIBLE_HEAP_MIN_ADDRESS    0x0001c0000000ULL /* 7 GiB */
#define CLIENT_VISIBLE_HEAP_MAX_ADDRESS    0x0002bfffffffULL
#define HIGH_HEAP_MIN_ADDRESS              0x0002c0000000ULL /* 11 GiB */

#define LOW_HEAP_SIZE               \
   (LOW_HEAP_MAX_ADDRESS - LOW_HEAP_MIN_ADDRESS + 1)
#define DYNAMIC_STATE_POOL_SIZE     \
   (DYNAMIC_STATE_POOL_MAX_ADDRESS - DYNAMIC_STATE_POOL_MIN_ADDRESS + 1)
#define BINDING_TABLE_POOL_SIZE     \
   (BINDING_TABLE_POOL_MAX_ADDRESS - BINDING_TABLE_POOL_MIN_ADDRESS + 1)
#define SURFACE_STATE_POOL_SIZE     \
   (SURFACE_STATE_POOL_MAX_ADDRESS - SURFACE_STATE_POOL_MIN_ADDRESS + 1)
#define INSTRUCTION_STATE_POOL_SIZE \
   (INSTRUCTION_STATE_POOL_MAX_ADDRESS - INSTRUCTION_STATE_POOL_MIN_ADDRESS + 1)
#define CLIENT_VISIBLE_HEAP_SIZE               \
   (CLIENT_VISIBLE_HEAP_MAX_ADDRESS - CLIENT_VISIBLE_HEAP_MIN_ADDRESS + 1)

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

#define MAX_VBS         28
#define MAX_XFB_BUFFERS  4
#define MAX_XFB_STREAMS  4
#define MAX_SETS         8
#define MAX_RTS          8
#define MAX_VIEWPORTS   16
#define MAX_SCISSORS    16
#define MAX_PUSH_CONSTANTS_SIZE 128
#define MAX_DYNAMIC_BUFFERS 16
#define MAX_IMAGES 64
#define MAX_PUSH_DESCRIPTORS 32 /* Minimum requirement */
#define MAX_INLINE_UNIFORM_BLOCK_SIZE 4096
#define MAX_INLINE_UNIFORM_BLOCK_DESCRIPTORS 32
/* We need 16 for UBO block reads to work and 32 for push UBOs. However, we
 * use 64 here to avoid cache issues. This could most likely bring it back to
 * 32 if we had different virtual addresses for the different views on a given
 * GEM object.
 */
#define ANV_UBO_ALIGNMENT 64
#define ANV_SSBO_ALIGNMENT 4
#define ANV_SSBO_BOUNDS_CHECK_ALIGNMENT 4
#define MAX_VIEWS_FOR_PRIMITIVE_REPLICATION 16

/* From the Skylake PRM Vol. 7 "Binding Table Surface State Model":
 *
 *    "The surface state model is used when a Binding Table Index (specified
 *    in the message descriptor) of less than 240 is specified. In this model,
 *    the Binding Table Index is used to index into the binding table, and the
 *    binding table entry contains a pointer to the SURFACE_STATE."
 *
 * Binding table values above 240 are used for various things in the hardware
 * such as stateless, stateless with incoherent cache, SLM, and bindless.
 */
#define MAX_BINDING_TABLE_SIZE 240

/* The kernel relocation API has a limitation of a 32-bit delta value
 * applied to the address before it is written which, in spite of it being
 * unsigned, is treated as signed .  Because of the way that this maps to
 * the Vulkan API, we cannot handle an offset into a buffer that does not
 * fit into a signed 32 bits.  The only mechanism we have for dealing with
 * this at the moment is to limit all VkDeviceMemory objects to a maximum
 * of 2GB each.  The Vulkan spec allows us to do this:
 *
 *    "Some platforms may have a limit on the maximum size of a single
 *    allocation. For example, certain systems may fail to create
 *    allocations with a size greater than or equal to 4GB. Such a limit is
 *    implementation-dependent, and if such a failure occurs then the error
 *    VK_ERROR_OUT_OF_DEVICE_MEMORY should be returned."
 *
 * We don't use vk_error here because it's not an error so much as an
 * indication to the application that the allocation is too large.
 */
#define MAX_MEMORY_ALLOCATION_SIZE (1ull << 31)

#define ANV_SVGS_VB_INDEX    MAX_VBS
#define ANV_DRAWID_VB_INDEX (MAX_VBS + 1)

/* We reserve this MI ALU register for the purpose of handling predication.
 * Other code which uses the MI ALU should leave it alone.
 */
#define ANV_PREDICATE_RESULT_REG 0x2678 /* MI_ALU_REG15 */

/* We reserve this MI ALU register to pass around an offset computed from
 * VkPerformanceQuerySubmitInfoKHR::counterPassIndex VK_KHR_performance_query.
 * Other code which uses the MI ALU should leave it alone.
 */
#define ANV_PERF_QUERY_OFFSET_REG 0x2670 /* MI_ALU_REG14 */

/* For gen12 we set the streamout buffers using 4 separate commands
 * (3DSTATE_SO_BUFFER_INDEX_*) instead of 3DSTATE_SO_BUFFER. However the layout
 * of the 3DSTATE_SO_BUFFER_INDEX_* commands is identical to that of
 * 3DSTATE_SO_BUFFER apart from the SOBufferIndex field, so for now we use the
 * 3DSTATE_SO_BUFFER command, but change the 3DCommandSubOpcode.
 * SO_BUFFER_INDEX_0_CMD is actually the 3DCommandSubOpcode for
 * 3DSTATE_SO_BUFFER_INDEX_0.
 */
#define SO_BUFFER_INDEX_0_CMD 0x60
#define anv_printflike(a, b) __attribute__((__format__(__printf__, a, b)))

static inline uint32_t
align_down_npot_u32(uint32_t v, uint32_t a)
{
   return v - (v % a);
}

static inline uint32_t
align_down_u32(uint32_t v, uint32_t a)
{
   assert(a != 0 && a == (a & -a));
   return v & ~(a - 1);
}

static inline uint32_t
align_u32(uint32_t v, uint32_t a)
{
   assert(a != 0 && a == (a & -a));
   return align_down_u32(v + a - 1, a);
}

static inline uint64_t
align_down_u64(uint64_t v, uint64_t a)
{
   assert(a != 0 && a == (a & -a));
   return v & ~(a - 1);
}

static inline uint64_t
align_u64(uint64_t v, uint64_t a)
{
   return align_down_u64(v + a - 1, a);
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

static inline void *anv_unpack_ptr(uintptr_t ptr, int bits, int *flags)
{
   uintptr_t mask = (1ull << bits) - 1;
   *flags = ptr & mask;
   return (void *) (ptr & ~mask);
}

static inline uintptr_t anv_pack_ptr(void *ptr, int bits, int flags)
{
   uintptr_t value = (uintptr_t) ptr;
   uintptr_t mask = (1ull << bits) - 1;
   return value | (mask & flags);
}

#define for_each_bit(b, dword)                          \
   for (uint32_t __dword = (dword);                     \
        (b) = __builtin_ffs(__dword) - 1, __dword;      \
        __dword &= ~(1 << (b)))

#define typed_memcpy(dest, src, count) ({ \
   STATIC_ASSERT(sizeof(*src) == sizeof(*dest)); \
   memcpy((dest), (src), (count) * sizeof(*(src))); \
})

/* Mapping from anv object to VkDebugReportObjectTypeEXT. New types need
 * to be added here in order to utilize mapping in debug/error/perf macros.
 */
#define REPORT_OBJECT_TYPE(o)                                                      \
   __builtin_choose_expr (                                                         \
   __builtin_types_compatible_p (__typeof (o), struct anv_instance*),              \
   VK_DEBUG_REPORT_OBJECT_TYPE_INSTANCE_EXT,                                       \
   __builtin_choose_expr (                                                         \
   __builtin_types_compatible_p (__typeof (o), struct anv_physical_device*),       \
   VK_DEBUG_REPORT_OBJECT_TYPE_PHYSICAL_DEVICE_EXT,                                \
   __builtin_choose_expr (                                                         \
   __builtin_types_compatible_p (__typeof (o), struct anv_device*),                \
   VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_EXT,                                         \
   __builtin_choose_expr (                                                         \
   __builtin_types_compatible_p (__typeof (o), const struct anv_device*),          \
   VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_EXT,                                         \
   __builtin_choose_expr (                                                         \
   __builtin_types_compatible_p (__typeof (o), struct anv_queue*),                 \
   VK_DEBUG_REPORT_OBJECT_TYPE_QUEUE_EXT,                                          \
   __builtin_choose_expr (                                                         \
   __builtin_types_compatible_p (__typeof (o), struct anv_semaphore*),             \
   VK_DEBUG_REPORT_OBJECT_TYPE_SEMAPHORE_EXT,                                      \
   __builtin_choose_expr (                                                         \
   __builtin_types_compatible_p (__typeof (o), struct anv_cmd_buffer*),            \
   VK_DEBUG_REPORT_OBJECT_TYPE_COMMAND_BUFFER_EXT,                                 \
   __builtin_choose_expr (                                                         \
   __builtin_types_compatible_p (__typeof (o), struct anv_fence*),                 \
   VK_DEBUG_REPORT_OBJECT_TYPE_FENCE_EXT,                                          \
   __builtin_choose_expr (                                                         \
   __builtin_types_compatible_p (__typeof (o), struct anv_device_memory*),         \
   VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_MEMORY_EXT,                                  \
   __builtin_choose_expr (                                                         \
   __builtin_types_compatible_p (__typeof (o), struct anv_buffer*),                \
   VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT,                                         \
   __builtin_choose_expr (                                                         \
   __builtin_types_compatible_p (__typeof (o), struct anv_image*),                 \
   VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT,                                          \
   __builtin_choose_expr (                                                         \
   __builtin_types_compatible_p (__typeof (o), const struct anv_image*),           \
   VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT,                                          \
   __builtin_choose_expr (                                                         \
   __builtin_types_compatible_p (__typeof (o), struct anv_event*),                 \
   VK_DEBUG_REPORT_OBJECT_TYPE_EVENT_EXT,                                          \
   __builtin_choose_expr (                                                         \
   __builtin_types_compatible_p (__typeof (o), struct anv_query_pool*),            \
   VK_DEBUG_REPORT_OBJECT_TYPE_QUERY_POOL_EXT,                                     \
   __builtin_choose_expr (                                                         \
   __builtin_types_compatible_p (__typeof (o), struct anv_buffer_view*),           \
   VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_VIEW_EXT,                                    \
   __builtin_choose_expr (                                                         \
   __builtin_types_compatible_p (__typeof (o), struct anv_image_view*),            \
   VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_VIEW_EXT,                                     \
   __builtin_choose_expr (                                                         \
   __builtin_types_compatible_p (__typeof (o), struct anv_shader_module*),         \
   VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT,                                  \
   __builtin_choose_expr (                                                         \
   __builtin_types_compatible_p (__typeof (o), struct anv_pipeline_cache*),        \
   VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_CACHE_EXT,                                 \
   __builtin_choose_expr (                                                         \
   __builtin_types_compatible_p (__typeof (o), struct anv_pipeline_layout*),       \
   VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_LAYOUT_EXT,                                \
   __builtin_choose_expr (                                                         \
   __builtin_types_compatible_p (__typeof (o), struct anv_render_pass*),           \
   VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT,                                    \
   __builtin_choose_expr (                                                         \
   __builtin_types_compatible_p (__typeof (o), struct anv_pipeline*),              \
   VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT,                                       \
   __builtin_choose_expr (                                                         \
   __builtin_types_compatible_p (__typeof (o), struct anv_descriptor_set_layout*), \
   VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT_EXT,                          \
   __builtin_choose_expr (                                                         \
   __builtin_types_compatible_p (__typeof (o), struct anv_sampler*),               \
   VK_DEBUG_REPORT_OBJECT_TYPE_SAMPLER_EXT,                                        \
   __builtin_choose_expr (                                                         \
   __builtin_types_compatible_p (__typeof (o), struct anv_descriptor_pool*),       \
   VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_POOL_EXT,                                \
   __builtin_choose_expr (                                                         \
   __builtin_types_compatible_p (__typeof (o), struct anv_descriptor_set*),        \
   VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_EXT,                                 \
   __builtin_choose_expr (                                                         \
   __builtin_types_compatible_p (__typeof (o), struct anv_framebuffer*),           \
   VK_DEBUG_REPORT_OBJECT_TYPE_FRAMEBUFFER_EXT,                                    \
   __builtin_choose_expr (                                                         \
   __builtin_types_compatible_p (__typeof (o), struct anv_cmd_pool*),              \
   VK_DEBUG_REPORT_OBJECT_TYPE_COMMAND_POOL_EXT,                                   \
   __builtin_choose_expr (                                                         \
   __builtin_types_compatible_p (__typeof (o), struct anv_surface*),               \
   VK_DEBUG_REPORT_OBJECT_TYPE_SURFACE_KHR_EXT,                                    \
   __builtin_choose_expr (                                                         \
   __builtin_types_compatible_p (__typeof (o), struct wsi_swapchain*),             \
   VK_DEBUG_REPORT_OBJECT_TYPE_SWAPCHAIN_KHR_EXT,                                  \
   __builtin_choose_expr (                                                         \
   __builtin_types_compatible_p (__typeof (o), struct vk_debug_callback*),         \
   VK_DEBUG_REPORT_OBJECT_TYPE_DEBUG_REPORT_CALLBACK_EXT_EXT,                      \
   __builtin_choose_expr (                                                         \
   __builtin_types_compatible_p (__typeof (o), void*),                             \
   VK_DEBUG_REPORT_OBJECT_TYPE_UNKNOWN_EXT,                                        \
   /* The void expression results in a compile-time error                          \
      when assigning the result to something.  */                                  \
   (void)0)))))))))))))))))))))))))))))))

/* Whenever we generate an error, pass it through this function. Useful for
 * debugging, where we can break on it. Only call at error site, not when
 * propagating errors. Might be useful to plug in a stack trace here.
 */

VkResult __vk_errorv(struct anv_instance *instance, const void *object,
                     VkDebugReportObjectTypeEXT type, VkResult error,
                     const char *file, int line, const char *format,
                     va_list args);

VkResult __vk_errorf(struct anv_instance *instance, const void *object,
                     VkDebugReportObjectTypeEXT type, VkResult error,
                     const char *file, int line, const char *format, ...)
   anv_printflike(7, 8);

#ifdef DEBUG
#define vk_error(error) __vk_errorf(NULL, NULL,\
                                    VK_DEBUG_REPORT_OBJECT_TYPE_UNKNOWN_EXT,\
                                    error, __FILE__, __LINE__, NULL)
#define vk_errorfi(instance, obj, error, format, ...)\
    __vk_errorf(instance, obj, REPORT_OBJECT_TYPE(obj), error,\
                __FILE__, __LINE__, format, ## __VA_ARGS__)
#define vk_errorf(device, obj, error, format, ...)\
   vk_errorfi(anv_device_instance_or_null(device),\
              obj, error, format, ## __VA_ARGS__)
#else
#define vk_error(error) error
#define vk_errorfi(instance, obj, error, format, ...) error
#define vk_errorf(device, obj, error, format, ...) error
#endif

/**
 * Warn on ignored extension structs.
 *
 * The Vulkan spec requires us to ignore unsupported or unknown structs in
 * a pNext chain.  In debug mode, emitting warnings for ignored structs may
 * help us discover structs that we should not have ignored.
 *
 *
 * From the Vulkan 1.0.38 spec:
 *
 *    Any component of the implementation (the loader, any enabled layers,
 *    and drivers) must skip over, without processing (other than reading the
 *    sType and pNext members) any chained structures with sType values not
 *    defined by extensions supported by that component.
 */
#define anv_debug_ignored_stype(sType) \
   mesa_logd("%s: ignored VkStructureType %u\n", __func__, (sType))

void __anv_perf_warn(struct anv_device *device, const void *object,
                     VkDebugReportObjectTypeEXT type, const char *file,
                     int line, const char *format, ...)
   anv_printflike(6, 7);
void anv_loge(const char *format, ...) anv_printflike(1, 2);
void anv_loge_v(const char *format, va_list va);

/**
 * Print a FINISHME message, including its source location.
 */
#define anv_finishme(format, ...) \
   do { \
      static bool reported = false; \
      if (!reported) { \
         mesa_logw("%s:%d: FINISHME: " format, __FILE__, __LINE__, \
                    ##__VA_ARGS__); \
         reported = true; \
      } \
   } while (0)

/**
 * Print a perf warning message.  Set INTEL_DEBUG=perf to see these.
 */
#define anv_perf_warn(instance, obj, format, ...) \
   do { \
      static bool reported = false; \
      if (!reported && (INTEL_DEBUG & DEBUG_PERF)) { \
         __anv_perf_warn(instance, obj, REPORT_OBJECT_TYPE(obj), __FILE__, __LINE__,\
                         format, ##__VA_ARGS__); \
         reported = true; \
      } \
   } while (0)

/* A non-fatal assert.  Useful for debugging. */
#ifdef DEBUG
#define anv_assert(x) ({ \
   if (unlikely(!(x))) \
      mesa_loge("%s:%d ASSERT: %s", __FILE__, __LINE__, #x); \
})
#else
#define anv_assert(x)
#endif

/* A multi-pointer allocator
 *
 * When copying data structures from the user (such as a render pass), it's
 * common to need to allocate data for a bunch of different things.  Instead
 * of doing several allocations and having to handle all of the error checking
 * that entails, it can be easier to do a single allocation.  This struct
 * helps facilitate that.  The intended usage looks like this:
 *
 *    ANV_MULTIALLOC(ma)
 *    anv_multialloc_add(&ma, &main_ptr, 1);
 *    anv_multialloc_add(&ma, &substruct1, substruct1Count);
 *    anv_multialloc_add(&ma, &substruct2, substruct2Count);
 *
 *    if (!anv_multialloc_alloc(&ma, pAllocator, VK_ALLOCATION_SCOPE_FOO))
 *       return vk_error(VK_ERROR_OUT_OF_HOST_MEORY);
 */
struct anv_multialloc {
    size_t size;
    size_t align;

    uint32_t ptr_count;
    void **ptrs[8];
};

#define ANV_MULTIALLOC_INIT \
   ((struct anv_multialloc) { 0, })

#define ANV_MULTIALLOC(_name) \
   struct anv_multialloc _name = ANV_MULTIALLOC_INIT

__attribute__((always_inline))
static inline void
_anv_multialloc_add(struct anv_multialloc *ma,
                    void **ptr, size_t size, size_t align)
{
   size_t offset = align_u64(ma->size, align);
   ma->size = offset + size;
   ma->align = MAX2(ma->align, align);

   /* Store the offset in the pointer. */
   *ptr = (void *)(uintptr_t)offset;

   assert(ma->ptr_count < ARRAY_SIZE(ma->ptrs));
   ma->ptrs[ma->ptr_count++] = ptr;
}

#define anv_multialloc_add_size(_ma, _ptr, _size) \
   _anv_multialloc_add((_ma), (void **)(_ptr), (_size), __alignof__(**(_ptr)))

#define anv_multialloc_add(_ma, _ptr, _count) \
   anv_multialloc_add_size(_ma, _ptr, (_count) * sizeof(**(_ptr)));

__attribute__((always_inline))
static inline void *
anv_multialloc_alloc(struct anv_multialloc *ma,
                     const VkAllocationCallbacks *alloc,
                     VkSystemAllocationScope scope)
{
   void *ptr = vk_alloc(alloc, ma->size, ma->align, scope);
   if (!ptr)
      return NULL;

   /* Fill out each of the pointers with their final value.
    *
    *   for (uint32_t i = 0; i < ma->ptr_count; i++)
    *      *ma->ptrs[i] = ptr + (uintptr_t)*ma->ptrs[i];
    *
    * Unfortunately, even though ma->ptr_count is basically guaranteed to be a
    * constant, GCC is incapable of figuring this out and unrolling the loop
    * so we have to give it a little help.
    */
   STATIC_ASSERT(ARRAY_SIZE(ma->ptrs) == 8);
#define _ANV_MULTIALLOC_UPDATE_POINTER(_i) \
   if ((_i) < ma->ptr_count) \
      *ma->ptrs[_i] = ptr + (uintptr_t)*ma->ptrs[_i]
   _ANV_MULTIALLOC_UPDATE_POINTER(0);
   _ANV_MULTIALLOC_UPDATE_POINTER(1);
   _ANV_MULTIALLOC_UPDATE_POINTER(2);
   _ANV_MULTIALLOC_UPDATE_POINTER(3);
   _ANV_MULTIALLOC_UPDATE_POINTER(4);
   _ANV_MULTIALLOC_UPDATE_POINTER(5);
   _ANV_MULTIALLOC_UPDATE_POINTER(6);
   _ANV_MULTIALLOC_UPDATE_POINTER(7);
#undef _ANV_MULTIALLOC_UPDATE_POINTER

   return ptr;
}

__attribute__((always_inline))
static inline void *
anv_multialloc_alloc2(struct anv_multialloc *ma,
                      const VkAllocationCallbacks *parent_alloc,
                      const VkAllocationCallbacks *alloc,
                      VkSystemAllocationScope scope)
{
   return anv_multialloc_alloc(ma, alloc ? alloc : parent_alloc, scope);
}

struct anv_bo {
   uint32_t gem_handle;

   uint32_t refcount;

   /* Index into the current validation list.  This is used by the
    * validation list building alrogithm to track which buffers are already
    * in the validation list so that we can ensure uniqueness.
    */
   uint32_t index;

   /* Index for use with util_sparse_array_free_list */
   uint32_t free_index;

   /* Last known offset.  This value is provided by the kernel when we
    * execbuf and is used as the presumed offset for the next bunch of
    * relocations.
    */
   uint64_t offset;

   /** Size of the buffer not including implicit aux */
   uint64_t size;

   /* Map for internally mapped BOs.
    *
    * If ANV_BO_WRAPPER is set in flags, map points to the wrapped BO.
    */
   void *map;

   /** Size of the implicit CCS range at the end of the buffer
    *
    * On Gen12, CCS data is always a direct 1/256 scale-down.  A single 64K
    * page of main surface data maps to a 256B chunk of CCS data and that
    * mapping is provided on TGL-LP by the AUX table which maps virtual memory
    * addresses in the main surface to virtual memory addresses for CCS data.
    *
    * Because we can't change these maps around easily and because Vulkan
    * allows two VkImages to be bound to overlapping memory regions (as long
    * as the app is careful), it's not feasible to make this mapping part of
    * the image.  (On Gen11 and earlier, the mapping was provided via
    * RENDER_SURFACE_STATE so each image had its own main -> CCS mapping.)
    * Instead, we attach the CCS data directly to the buffer object and setup
    * the AUX table mapping at BO creation time.
    *
    * This field is for internal tracking use by the BO allocator only and
    * should not be touched by other parts of the code.  If something wants to
    * know if a BO has implicit CCS data, it should instead look at the
    * has_implicit_ccs boolean below.
    *
    * This data is not included in maps of this buffer.
    */
   uint32_t _ccs_size;

   /** Flags to pass to the kernel through drm_i915_exec_object2::flags */
   uint32_t flags;

   /** True if this BO may be shared with other processes */
   bool is_external:1;

   /** True if this BO is a wrapper
    *
    * When set to true, none of the fields in this BO are meaningful except
    * for anv_bo::is_wrapper and anv_bo::map which points to the actual BO.
    * See also anv_bo_unwrap().  Wrapper BOs are not allowed when use_softpin
    * is set in the physical device.
    */
   bool is_wrapper:1;

   /** See also ANV_BO_ALLOC_FIXED_ADDRESS */
   bool has_fixed_address:1;

   /** True if this BO wraps a host pointer */
   bool from_host_ptr:1;

   /** See also ANV_BO_ALLOC_CLIENT_VISIBLE_ADDRESS */
   bool has_client_visible_address:1;

   /** True if this BO has implicit CCS data attached to it */
   bool has_implicit_ccs:1;
};

static inline struct anv_bo *
anv_bo_ref(struct anv_bo *bo)
{
   p_atomic_inc(&bo->refcount);
   return bo;
}

static inline struct anv_bo *
anv_bo_unwrap(struct anv_bo *bo)
{
   while (bo->is_wrapper)
      bo = bo->map;
   return bo;
}

/* Represents a lock-free linked list of "free" things.  This is used by
 * both the block pool and the state pools.  Unfortunately, in order to
 * solve the ABA problem, we can't use a single uint32_t head.
 */
union anv_free_list {
   struct {
      uint32_t offset;

      /* A simple count that is incremented every time the head changes. */
      uint32_t count;
   };
   /* Make sure it's aligned to 64 bits. This will make atomic operations
    * faster on 32 bit platforms.
    */
   uint64_t u64 __attribute__ ((aligned (8)));
};

#define ANV_FREE_LIST_EMPTY ((union anv_free_list) { { UINT32_MAX, 0 } })

struct anv_block_state {
   union {
      struct {
         uint32_t next;
         uint32_t end;
      };
      /* Make sure it's aligned to 64 bits. This will make atomic operations
       * faster on 32 bit platforms.
       */
      uint64_t u64 __attribute__ ((aligned (8)));
   };
};

#define anv_block_pool_foreach_bo(bo, pool)  \
   for (struct anv_bo **_pp_bo = (pool)->bos, *bo; \
        _pp_bo != &(pool)->bos[(pool)->nbos] && (bo = *_pp_bo, true); \
        _pp_bo++)

#define ANV_MAX_BLOCK_POOL_BOS 20

struct anv_block_pool {
   struct anv_device *device;
   bool use_softpin;

   /* Wrapper BO for use in relocation lists.  This BO is simply a wrapper
    * around the actual BO so that we grow the pool after the wrapper BO has
    * been put in a relocation list.  This is only used in the non-softpin
    * case.
    */
   struct anv_bo wrapper_bo;

   struct anv_bo *bos[ANV_MAX_BLOCK_POOL_BOS];
   struct anv_bo *bo;
   uint32_t nbos;

   uint64_t size;

   /* The address where the start of the pool is pinned. The various bos that
    * are created as the pool grows will have addresses in the range
    * [start_address, start_address + BLOCK_POOL_MEMFD_SIZE).
    */
   uint64_t start_address;

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
    *
    * DO NOT access this pointer directly. Use anv_block_pool_map() instead,
    * since it will handle the softpin case as well, where this points to NULL.
    */
   void *map;
   int fd;

   /**
    * Array of mmaps and gem handles owned by the block pool, reclaimed when
    * the block pool is destroyed.
    */
   struct u_vector mmap_cleanups;

   struct anv_block_state state;

   struct anv_block_state back_state;
};

/* Block pools are backed by a fixed-size 1GB memfd */
#define BLOCK_POOL_MEMFD_SIZE (1ul << 30)

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
   uint32_t idx;
};

#define ANV_STATE_NULL ((struct anv_state) { .alloc_size = 0 })

struct anv_fixed_size_state_pool {
   union anv_free_list free_list;
   struct anv_block_state block;
};

#define ANV_MIN_STATE_SIZE_LOG2 6
#define ANV_MAX_STATE_SIZE_LOG2 21

#define ANV_STATE_BUCKETS (ANV_MAX_STATE_SIZE_LOG2 - ANV_MIN_STATE_SIZE_LOG2 + 1)

struct anv_free_entry {
   uint32_t next;
   struct anv_state state;
};

struct anv_state_table {
   struct anv_device *device;
   int fd;
   struct anv_free_entry *map;
   uint32_t size;
   struct anv_block_state state;
   struct u_vector cleanups;
};

struct anv_state_pool {
   struct anv_block_pool block_pool;

   /* Offset into the relevant state base address where the state pool starts
    * allocating memory.
    */
   int32_t start_offset;

   struct anv_state_table table;

   /* The size of blocks which will be allocated from the block pool */
   uint32_t block_size;

   /** Free list for "back" allocations */
   union anv_free_list back_alloc_free_list;

   struct anv_fixed_size_state_pool buckets[ANV_STATE_BUCKETS];
};

struct anv_state_reserved_pool {
   struct anv_state_pool *pool;
   union anv_free_list reserved_blocks;
   uint32_t count;
};

struct anv_state_stream {
   struct anv_state_pool *state_pool;

   /* The size of blocks to allocate from the state pool */
   uint32_t block_size;

   /* Current block we're allocating from */
   struct anv_state block;

   /* Offset into the current block at which to allocate the next state */
   uint32_t next;

   /* List of all blocks allocated from this pool */
   struct util_dynarray all_blocks;
};

/* The block_pool functions exported for testing only.  The block pool should
 * only be used via a state pool (see below).
 */
VkResult anv_block_pool_init(struct anv_block_pool *pool,
                             struct anv_device *device,
                             uint64_t start_address,
                             uint32_t initial_size);
void anv_block_pool_finish(struct anv_block_pool *pool);
int32_t anv_block_pool_alloc(struct anv_block_pool *pool,
                             uint32_t block_size, uint32_t *padding);
int32_t anv_block_pool_alloc_back(struct anv_block_pool *pool,
                                  uint32_t block_size);
void* anv_block_pool_map(struct anv_block_pool *pool, int32_t offset, uint32_t
size);

VkResult anv_state_pool_init(struct anv_state_pool *pool,
                             struct anv_device *device,
                             uint64_t base_address,
                             int32_t start_offset,
                             uint32_t block_size);
void anv_state_pool_finish(struct anv_state_pool *pool);
struct anv_state anv_state_pool_alloc(struct anv_state_pool *pool,
                                      uint32_t state_size, uint32_t alignment);
struct anv_state anv_state_pool_alloc_back(struct anv_state_pool *pool);
void anv_state_pool_free(struct anv_state_pool *pool, struct anv_state state);
void anv_state_stream_init(struct anv_state_stream *stream,
                           struct anv_state_pool *state_pool,
                           uint32_t block_size);
void anv_state_stream_finish(struct anv_state_stream *stream);
struct anv_state anv_state_stream_alloc(struct anv_state_stream *stream,
                                        uint32_t size, uint32_t alignment);

void anv_state_reserved_pool_init(struct anv_state_reserved_pool *pool,
                                      struct anv_state_pool *parent,
                                      uint32_t count, uint32_t size,
                                      uint32_t alignment);
void anv_state_reserved_pool_finish(struct anv_state_reserved_pool *pool);
struct anv_state anv_state_reserved_pool_alloc(struct anv_state_reserved_pool *pool);
void anv_state_reserved_pool_free(struct anv_state_reserved_pool *pool,
                                  struct anv_state state);

VkResult anv_state_table_init(struct anv_state_table *table,
                             struct anv_device *device,
                             uint32_t initial_entries);
void anv_state_table_finish(struct anv_state_table *table);
VkResult anv_state_table_add(struct anv_state_table *table, uint32_t *idx,
                             uint32_t count);
void anv_free_list_push(union anv_free_list *list,
                        struct anv_state_table *table,
                        uint32_t idx, uint32_t count);
struct anv_state* anv_free_list_pop(union anv_free_list *list,
                                    struct anv_state_table *table);


static inline struct anv_state *
anv_state_table_get(struct anv_state_table *table, uint32_t idx)
{
   return &table->map[idx].state;
}
/**
 * Implements a pool of re-usable BOs.  The interface is identical to that
 * of block_pool except that each block is its own BO.
 */
struct anv_bo_pool {
   struct anv_device *device;

   struct util_sparse_array_free_list free_list[16];
};

void anv_bo_pool_init(struct anv_bo_pool *pool, struct anv_device *device);
void anv_bo_pool_finish(struct anv_bo_pool *pool);
VkResult anv_bo_pool_alloc(struct anv_bo_pool *pool, uint32_t size,
                           struct anv_bo **bo_out);
void anv_bo_pool_free(struct anv_bo_pool *pool, struct anv_bo *bo);

struct anv_scratch_pool {
   /* Indexed by Per-Thread Scratch Space number (the hardware value) and stage */
   struct anv_bo *bos[16][MESA_SHADER_STAGES];
};

void anv_scratch_pool_init(struct anv_device *device,
                           struct anv_scratch_pool *pool);
void anv_scratch_pool_finish(struct anv_device *device,
                             struct anv_scratch_pool *pool);
struct anv_bo *anv_scratch_pool_alloc(struct anv_device *device,
                                      struct anv_scratch_pool *pool,
                                      gl_shader_stage stage,
                                      unsigned per_thread_scratch);

/** Implements a BO cache that ensures a 1-1 mapping of GEM BOs to anv_bos */
struct anv_bo_cache {
   struct util_sparse_array bo_map;
   pthread_mutex_t mutex;
};

VkResult anv_bo_cache_init(struct anv_bo_cache *cache);
void anv_bo_cache_finish(struct anv_bo_cache *cache);

struct anv_memory_type {
   /* Standard bits passed on to the client */
   VkMemoryPropertyFlags   propertyFlags;
   uint32_t                heapIndex;
};

struct anv_memory_heap {
   /* Standard bits passed on to the client */
   VkDeviceSize      size;
   VkMemoryHeapFlags flags;

   /** Driver-internal book-keeping.
    *
    * Align it to 64 bits to make atomic operations faster on 32 bit platforms.
    */
   VkDeviceSize      used __attribute__ ((aligned (8)));
};

struct anv_physical_device {
    struct vk_object_base                       base;

    /* Link in anv_instance::physical_devices */
    struct list_head                            link;

    struct anv_instance *                       instance;
    bool                                        no_hw;
    char                                        path[20];
    const char *                                name;
    struct {
       uint16_t                                 domain;
       uint8_t                                  bus;
       uint8_t                                  device;
       uint8_t                                  function;
    }                                           pci_info;
    struct gen_device_info                      info;
    /** Amount of "GPU memory" we want to advertise
     *
     * Clearly, this value is bogus since Intel is a UMA architecture.  On
     * gen7 platforms, we are limited by GTT size unless we want to implement
     * fine-grained tracking and GTT splitting.  On Broadwell and above we are
     * practically unlimited.  However, we will never report more than 3/4 of
     * the total system ram to try and avoid running out of RAM.
     */
    bool                                        supports_48bit_addresses;
    struct brw_compiler *                       compiler;
    struct isl_device                           isl_dev;
    struct gen_perf_config *                    perf;
    int                                         cmd_parser_version;
    bool                                        has_softpin;
    bool                                        has_exec_async;
    bool                                        has_exec_capture;
    bool                                        has_exec_fence;
    bool                                        has_syncobj;
    bool                                        has_syncobj_wait;
    bool                                        has_syncobj_wait_available;
    bool                                        has_context_priority;
    bool                                        has_context_isolation;
    bool                                        has_thread_submit;
    bool                                        has_mem_available;
    bool                                        has_mmap_offset;
    uint64_t                                    gtt_size;

    bool                                        use_softpin;
    bool                                        always_use_bindless;
    bool                                        use_call_secondary;

    /** True if we can access buffers using A64 messages */
    bool                                        has_a64_buffer_access;
    /** True if we can use bindless access for images */
    bool                                        has_bindless_images;
    /** True if we can use bindless access for samplers */
    bool                                        has_bindless_samplers;
    /** True if we can use timeline semaphores through execbuf */
    bool                                        has_exec_timeline;

    /** True if we can read the GPU timestamp register
     *
     * When running in a virtual context, the timestamp register is unreadable
     * on Gen12+.
     */
    bool                                        has_reg_timestamp;

    /** True if this device has implicit AUX
     *
     * If true, CCS is handled as an implicit attachment to the BO rather than
     * as an explicitly bound surface.
     */
    bool                                        has_implicit_ccs;

    bool                                        always_flush_cache;

    struct anv_device_extension_table           supported_extensions;

    uint32_t                                    eu_total;
    uint32_t                                    subslice_total;

    struct {
      uint32_t                                  type_count;
      struct anv_memory_type                    types[VK_MAX_MEMORY_TYPES];
      uint32_t                                  heap_count;
      struct anv_memory_heap                    heaps[VK_MAX_MEMORY_HEAPS];
    } memory;

    uint8_t                                     driver_build_sha1[20];
    uint8_t                                     pipeline_cache_uuid[VK_UUID_SIZE];
    uint8_t                                     driver_uuid[VK_UUID_SIZE];
    uint8_t                                     device_uuid[VK_UUID_SIZE];

    struct disk_cache *                         disk_cache;

    struct wsi_device                       wsi_device;
    int                                         local_fd;
    int                                         master_fd;
};

struct anv_app_info {
   const char*        app_name;
   uint32_t           app_version;
   const char*        engine_name;
   uint32_t           engine_version;
   uint32_t           api_version;
};

struct anv_instance {
    struct vk_object_base                       base;

    VkAllocationCallbacks                       alloc;

    struct anv_app_info                         app_info;

    struct anv_instance_extension_table         enabled_extensions;
    struct anv_instance_dispatch_table          dispatch;
    struct anv_physical_device_dispatch_table   physical_device_dispatch;
    struct anv_device_dispatch_table            device_dispatch;

    bool                                        physical_devices_enumerated;
    struct list_head                            physical_devices;

    bool                                        pipeline_cache_enabled;

    struct vk_debug_report_instance             debug_report_callbacks;

    struct driOptionCache                       dri_options;
    struct driOptionCache                       available_dri_options;
};

VkResult anv_init_wsi(struct anv_physical_device *physical_device);
void anv_finish_wsi(struct anv_physical_device *physical_device);

uint32_t anv_physical_device_api_version(struct anv_physical_device *dev);
bool anv_physical_device_extension_supported(struct anv_physical_device *dev,
                                             const char *name);

struct anv_queue_submit {
   struct anv_cmd_buffer *                   cmd_buffer;

   uint32_t                                  fence_count;
   uint32_t                                  fence_array_length;
   struct drm_i915_gem_exec_fence *          fences;
   uint64_t *                                fence_values;

   uint32_t                                  temporary_semaphore_count;
   uint32_t                                  temporary_semaphore_array_length;
   struct anv_semaphore_impl *               temporary_semaphores;

   /* Semaphores to be signaled with a SYNC_FD. */
   struct anv_semaphore **                   sync_fd_semaphores;
   uint32_t                                  sync_fd_semaphore_count;
   uint32_t                                  sync_fd_semaphore_array_length;

   /* Allocated only with non shareable timelines. */
   union {
      struct anv_timeline **                 wait_timelines;
      uint32_t *                             wait_timeline_syncobjs;
   };
   uint32_t                                  wait_timeline_count;
   uint32_t                                  wait_timeline_array_length;
   uint64_t *                                wait_timeline_values;

   struct anv_timeline **                    signal_timelines;
   uint32_t                                  signal_timeline_count;
   uint32_t                                  signal_timeline_array_length;
   uint64_t *                                signal_timeline_values;

   int                                       in_fence;
   bool                                      need_out_fence;
   int                                       out_fence;

   uint32_t                                  fence_bo_count;
   uint32_t                                  fence_bo_array_length;
   /* An array of struct anv_bo pointers with lower bit used as a flag to
    * signal we will wait on that BO (see anv_(un)pack_ptr).
    */
   uintptr_t *                               fence_bos;

   int                                       perf_query_pass;

   const VkAllocationCallbacks *             alloc;
   VkSystemAllocationScope                   alloc_scope;

   struct anv_bo *                           simple_bo;
   uint32_t                                  simple_bo_size;

   struct list_head                          link;
};

struct anv_queue {
    struct vk_object_base                       base;

   struct anv_device *                       device;

   VkDeviceQueueCreateFlags                  flags;

   /* Set once from the device api calls. */
   bool                                      lost_signaled;

   /* Only set once atomically by the queue */
   int                                       lost;
   int                                       error_line;
   const char *                              error_file;
   char                                      error_msg[80];

   /*
    * This mutext protects the variables below.
    */
   pthread_mutex_t                           mutex;

   pthread_t                                 thread;
   pthread_cond_t                            cond;

   /*
    * A list of struct anv_queue_submit to be submitted to i915.
    */
   struct list_head                          queued_submits;

   /* Set to true to stop the submission thread */
   bool                                      quit;
};

struct anv_pipeline_cache {
   struct vk_object_base                        base;
   struct anv_device *                          device;
   pthread_mutex_t                              mutex;

   struct hash_table *                          nir_cache;

   struct hash_table *                          cache;

   bool                                         external_sync;
};

struct nir_xfb_info;
struct anv_pipeline_bind_map;

void anv_pipeline_cache_init(struct anv_pipeline_cache *cache,
                             struct anv_device *device,
                             bool cache_enabled,
                             bool external_sync);
void anv_pipeline_cache_finish(struct anv_pipeline_cache *cache);

struct anv_shader_bin *
anv_pipeline_cache_search(struct anv_pipeline_cache *cache,
                          const void *key, uint32_t key_size);
struct anv_shader_bin *
anv_pipeline_cache_upload_kernel(struct anv_pipeline_cache *cache,
                                 gl_shader_stage stage,
                                 const void *key_data, uint32_t key_size,
                                 const void *kernel_data, uint32_t kernel_size,
                                 const struct brw_stage_prog_data *prog_data,
                                 uint32_t prog_data_size,
                                 const struct brw_compile_stats *stats,
                                 uint32_t num_stats,
                                 const struct nir_xfb_info *xfb_info,
                                 const struct anv_pipeline_bind_map *bind_map);

struct anv_shader_bin *
anv_device_search_for_kernel(struct anv_device *device,
                             struct anv_pipeline_cache *cache,
                             const void *key_data, uint32_t key_size,
                             bool *user_cache_bit);

struct anv_shader_bin *
anv_device_upload_kernel(struct anv_device *device,
                         struct anv_pipeline_cache *cache,
                         gl_shader_stage stage,
                         const void *key_data, uint32_t key_size,
                         const void *kernel_data, uint32_t kernel_size,
                         const struct brw_stage_prog_data *prog_data,
                         uint32_t prog_data_size,
                         const struct brw_compile_stats *stats,
                         uint32_t num_stats,
                         const struct nir_xfb_info *xfb_info,
                         const struct anv_pipeline_bind_map *bind_map);

struct nir_shader;
struct nir_shader_compiler_options;

struct nir_shader *
anv_device_search_for_nir(struct anv_device *device,
                          struct anv_pipeline_cache *cache,
                          const struct nir_shader_compiler_options *nir_options,
                          unsigned char sha1_key[20],
                          void *mem_ctx);

void
anv_device_upload_nir(struct anv_device *device,
                      struct anv_pipeline_cache *cache,
                      const struct nir_shader *nir,
                      unsigned char sha1_key[20]);

struct anv_address {
   struct anv_bo *bo;
   uint32_t offset;
};

struct anv_device {
    struct vk_device                            vk;

    struct anv_physical_device *                physical;
    bool                                        no_hw;
    struct gen_device_info                      info;
    struct isl_device                           isl_dev;
    int                                         context_id;
    int                                         fd;
    bool                                        can_chain_batches;
    bool                                        robust_buffer_access;
    bool                                        has_thread_submit;
    struct anv_device_extension_table           enabled_extensions;
    struct anv_device_dispatch_table            dispatch;

    pthread_mutex_t                             vma_mutex;
    struct util_vma_heap                        vma_lo;
    struct util_vma_heap                        vma_cva;
    struct util_vma_heap                        vma_hi;

    /** List of all anv_device_memory objects */
    struct list_head                            memory_objects;

    struct anv_bo_pool                          batch_bo_pool;

    struct anv_bo_cache                         bo_cache;

    struct anv_state_pool                       dynamic_state_pool;
    struct anv_state_pool                       instruction_state_pool;
    struct anv_state_pool                       binding_table_pool;
    struct anv_state_pool                       surface_state_pool;

    struct anv_state_reserved_pool              custom_border_colors;

    /** BO used for various workarounds
     *
     * There are a number of workarounds on our hardware which require writing
     * data somewhere and it doesn't really matter where.  For that, we use
     * this BO and just write to the first dword or so.
     *
     * We also need to be able to handle NULL buffers bound as pushed UBOs.
     * For that, we use the high bytes (>= 1024) of the workaround BO.
     */
    struct anv_bo *                             workaround_bo;
    struct anv_address                          workaround_address;

    struct anv_bo *                             trivial_batch_bo;
    struct anv_bo *                             hiz_clear_bo;
    struct anv_state                            null_surface_state;

    struct anv_pipeline_cache                   default_pipeline_cache;
    struct blorp_context                        blorp;

    struct anv_state                            border_colors;

    struct anv_state                            slice_hash;

    struct anv_queue                            queue;

    struct anv_scratch_pool                     scratch_pool;

    pthread_mutex_t                             mutex;
    pthread_cond_t                              queue_submit;
    int                                         _lost;
    int                                         lost_reported;

    struct gen_batch_decode_ctx                 decoder_ctx;
    /*
     * When decoding a anv_cmd_buffer, we might need to search for BOs through
     * the cmd_buffer's list.
     */
    struct anv_cmd_buffer                      *cmd_buffer_being_decoded;

    int                                         perf_fd; /* -1 if no opened */
    uint64_t                                    perf_metric; /* 0 if unset */

    struct gen_aux_map_context                  *aux_map_ctx;

    struct gen_debug_block_frame                *debug_frame_desc;
};

static inline struct anv_instance *
anv_device_instance_or_null(const struct anv_device *device)
{
   return device ? device->physical->instance : NULL;
}

static inline struct anv_state_pool *
anv_binding_table_pool(struct anv_device *device)
{
   if (device->physical->use_softpin)
      return &device->binding_table_pool;
   else
      return &device->surface_state_pool;
}

static inline struct anv_state
anv_binding_table_pool_alloc(struct anv_device *device) {
   if (device->physical->use_softpin)
      return anv_state_pool_alloc(&device->binding_table_pool,
                                  device->binding_table_pool.block_size, 0);
   else
      return anv_state_pool_alloc_back(&device->surface_state_pool);
}

static inline void
anv_binding_table_pool_free(struct anv_device *device, struct anv_state state) {
   anv_state_pool_free(anv_binding_table_pool(device), state);
}

static inline uint32_t
anv_mocs(const struct anv_device *device,
         const struct anv_bo *bo,
         isl_surf_usage_flags_t usage)
{
   if (bo->is_external)
      return device->isl_dev.mocs.external;

   return isl_mocs(&device->isl_dev, usage);
}

void anv_device_init_blorp(struct anv_device *device);
void anv_device_finish_blorp(struct anv_device *device);

void _anv_device_report_lost(struct anv_device *device);
VkResult _anv_device_set_lost(struct anv_device *device,
                              const char *file, int line,
                              const char *msg, ...)
   anv_printflike(4, 5);
VkResult _anv_queue_set_lost(struct anv_queue *queue,
                              const char *file, int line,
                              const char *msg, ...)
   anv_printflike(4, 5);
#define anv_device_set_lost(dev, ...) \
   _anv_device_set_lost(dev, __FILE__, __LINE__, __VA_ARGS__)
#define anv_queue_set_lost(queue, ...) \
   (queue)->device->has_thread_submit ? \
   _anv_queue_set_lost(queue, __FILE__, __LINE__, __VA_ARGS__) : \
   _anv_device_set_lost(queue->device, __FILE__, __LINE__, __VA_ARGS__)

static inline bool
anv_device_is_lost(struct anv_device *device)
{
   int lost = p_atomic_read(&device->_lost);
   if (unlikely(lost && !device->lost_reported))
      _anv_device_report_lost(device);
   return lost;
}

VkResult anv_device_query_status(struct anv_device *device);


enum anv_bo_alloc_flags {
   /** Specifies that the BO must have a 32-bit address
    *
    * This is the opposite of EXEC_OBJECT_SUPPORTS_48B_ADDRESS.
    */
   ANV_BO_ALLOC_32BIT_ADDRESS =  (1 << 0),

   /** Specifies that the BO may be shared externally */
   ANV_BO_ALLOC_EXTERNAL =       (1 << 1),

   /** Specifies that the BO should be mapped */
   ANV_BO_ALLOC_MAPPED =         (1 << 2),

   /** Specifies that the BO should be snooped so we get coherency */
   ANV_BO_ALLOC_SNOOPED =        (1 << 3),

   /** Specifies that the BO should be captured in error states */
   ANV_BO_ALLOC_CAPTURE =        (1 << 4),

   /** Specifies that the BO will have an address assigned by the caller
    *
    * Such BOs do not exist in any VMA heap.
    */
   ANV_BO_ALLOC_FIXED_ADDRESS = (1 << 5),

   /** Enables implicit synchronization on the BO
    *
    * This is the opposite of EXEC_OBJECT_ASYNC.
    */
   ANV_BO_ALLOC_IMPLICIT_SYNC =  (1 << 6),

   /** Enables implicit synchronization on the BO
    *
    * This is equivalent to EXEC_OBJECT_WRITE.
    */
   ANV_BO_ALLOC_IMPLICIT_WRITE = (1 << 7),

   /** Has an address which is visible to the client */
   ANV_BO_ALLOC_CLIENT_VISIBLE_ADDRESS = (1 << 8),

   /** This buffer has implicit CCS data attached to it */
   ANV_BO_ALLOC_IMPLICIT_CCS = (1 << 9),
};

VkResult anv_device_alloc_bo(struct anv_device *device, uint64_t size,
                             enum anv_bo_alloc_flags alloc_flags,
                             uint64_t explicit_address,
                             struct anv_bo **bo);
VkResult anv_device_import_bo_from_host_ptr(struct anv_device *device,
                                            void *host_ptr, uint32_t size,
                                            enum anv_bo_alloc_flags alloc_flags,
                                            uint64_t client_address,
                                            struct anv_bo **bo_out);
VkResult anv_device_import_bo(struct anv_device *device, int fd,
                              enum anv_bo_alloc_flags alloc_flags,
                              uint64_t client_address,
                              struct anv_bo **bo);
VkResult anv_device_export_bo(struct anv_device *device,
                              struct anv_bo *bo, int *fd_out);
void anv_device_release_bo(struct anv_device *device,
                           struct anv_bo *bo);

static inline struct anv_bo *
anv_device_lookup_bo(struct anv_device *device, uint32_t gem_handle)
{
   return util_sparse_array_get(&device->bo_cache.bo_map, gem_handle);
}

VkResult anv_device_bo_busy(struct anv_device *device, struct anv_bo *bo);
VkResult anv_device_wait(struct anv_device *device, struct anv_bo *bo,
                         int64_t timeout);

VkResult anv_queue_init(struct anv_device *device, struct anv_queue *queue);
void anv_queue_finish(struct anv_queue *queue);

VkResult anv_queue_execbuf_locked(struct anv_queue *queue, struct anv_queue_submit *submit);
VkResult anv_queue_submit_simple_batch(struct anv_queue *queue,
                                       struct anv_batch *batch);

uint64_t anv_gettime_ns(void);
uint64_t anv_get_absolute_timeout(uint64_t timeout);

void* anv_gem_mmap(struct anv_device *device,
                   uint32_t gem_handle, uint64_t offset, uint64_t size, uint32_t flags);
void anv_gem_munmap(struct anv_device *device, void *p, uint64_t size);
uint32_t anv_gem_create(struct anv_device *device, uint64_t size);
void anv_gem_close(struct anv_device *device, uint32_t gem_handle);
uint32_t anv_gem_userptr(struct anv_device *device, void *mem, size_t size);
int anv_gem_busy(struct anv_device *device, uint32_t gem_handle);
int anv_gem_wait(struct anv_device *device, uint32_t gem_handle, int64_t *timeout_ns);
int anv_gem_execbuffer(struct anv_device *device,
                       struct drm_i915_gem_execbuffer2 *execbuf);
int anv_gem_set_tiling(struct anv_device *device, uint32_t gem_handle,
                       uint32_t stride, uint32_t tiling);
int anv_gem_create_context(struct anv_device *device);
bool anv_gem_has_context_priority(int fd);
int anv_gem_destroy_context(struct anv_device *device, int context);
int anv_gem_set_context_param(int fd, int context, uint32_t param,
                              uint64_t value);
int anv_gem_get_context_param(int fd, int context, uint32_t param,
                              uint64_t *value);
int anv_gem_get_param(int fd, uint32_t param);
uint64_t anv_gem_get_drm_cap(int fd, uint32_t capability);
int anv_gem_get_tiling(struct anv_device *device, uint32_t gem_handle);
bool anv_gem_get_bit6_swizzle(int fd, uint32_t tiling);
int anv_gem_gpu_get_reset_stats(struct anv_device *device,
                                uint32_t *active, uint32_t *pending);
int anv_gem_handle_to_fd(struct anv_device *device, uint32_t gem_handle);
int anv_gem_reg_read(int fd, uint32_t offset, uint64_t *result);
uint32_t anv_gem_fd_to_handle(struct anv_device *device, int fd);
int anv_gem_set_caching(struct anv_device *device, uint32_t gem_handle, uint32_t caching);
int anv_gem_set_domain(struct anv_device *device, uint32_t gem_handle,
                       uint32_t read_domains, uint32_t write_domain);
int anv_gem_sync_file_merge(struct anv_device *device, int fd1, int fd2);
uint32_t anv_gem_syncobj_create(struct anv_device *device, uint32_t flags);
void anv_gem_syncobj_destroy(struct anv_device *device, uint32_t handle);
int anv_gem_syncobj_handle_to_fd(struct anv_device *device, uint32_t handle);
uint32_t anv_gem_syncobj_fd_to_handle(struct anv_device *device, int fd);
int anv_gem_syncobj_export_sync_file(struct anv_device *device,
                                     uint32_t handle);
int anv_gem_syncobj_import_sync_file(struct anv_device *device,
                                     uint32_t handle, int fd);
void anv_gem_syncobj_reset(struct anv_device *device, uint32_t handle);
bool anv_gem_supports_syncobj_wait(int fd);
int anv_gem_syncobj_wait(struct anv_device *device,
                         const uint32_t *handles, uint32_t num_handles,
                         int64_t abs_timeout_ns, bool wait_all);
int anv_gem_syncobj_timeline_wait(struct anv_device *device,
                                  const uint32_t *handles, const uint64_t *points,
                                  uint32_t num_items, int64_t abs_timeout_ns,
                                  bool wait_all, bool wait_materialize);
int anv_gem_syncobj_timeline_signal(struct anv_device *device,
                                    const uint32_t *handles, const uint64_t *points,
                                    uint32_t num_items);
int anv_gem_syncobj_timeline_query(struct anv_device *device,
                                   const uint32_t *handles, uint64_t *points,
                                   uint32_t num_items);

uint64_t anv_vma_alloc(struct anv_device *device,
                       uint64_t size, uint64_t align,
                       enum anv_bo_alloc_flags alloc_flags,
                       uint64_t client_address);
void anv_vma_free(struct anv_device *device,
                  uint64_t address, uint64_t size);

struct anv_reloc_list {
   uint32_t                                     num_relocs;
   uint32_t                                     array_length;
   struct drm_i915_gem_relocation_entry *       relocs;
   struct anv_bo **                             reloc_bos;
   uint32_t                                     dep_words;
   BITSET_WORD *                                deps;
};

VkResult anv_reloc_list_init(struct anv_reloc_list *list,
                             const VkAllocationCallbacks *alloc);
void anv_reloc_list_finish(struct anv_reloc_list *list,
                           const VkAllocationCallbacks *alloc);

VkResult anv_reloc_list_add(struct anv_reloc_list *list,
                            const VkAllocationCallbacks *alloc,
                            uint32_t offset, struct anv_bo *target_bo,
                            uint32_t delta, uint64_t *address_u64_out);

struct anv_batch_bo {
   /* Link in the anv_cmd_buffer.owned_batch_bos list */
   struct list_head                             link;

   struct anv_bo *                              bo;

   /* Bytes actually consumed in this batch BO */
   uint32_t                                     length;

   struct anv_reloc_list                        relocs;
};

struct anv_batch {
   const VkAllocationCallbacks *                alloc;

   struct anv_address                           start_addr;

   void *                                       start;
   void *                                       end;
   void *                                       next;

   struct anv_reloc_list *                      relocs;

   /* This callback is called (with the associated user data) in the event
    * that the batch runs out of space.
    */
   VkResult (*extend_cb)(struct anv_batch *, void *);
   void *                                       user_data;

   /**
    * Current error status of the command buffer. Used to track inconsistent
    * or incomplete command buffer states that are the consequence of run-time
    * errors such as out of memory scenarios. We want to track this in the
    * batch because the command buffer object is not visible to some parts
    * of the driver.
    */
   VkResult                                     status;
};

void *anv_batch_emit_dwords(struct anv_batch *batch, int num_dwords);
void anv_batch_emit_batch(struct anv_batch *batch, struct anv_batch *other);
uint64_t anv_batch_emit_reloc(struct anv_batch *batch,
                              void *location, struct anv_bo *bo, uint32_t offset);
struct anv_address anv_batch_address(struct anv_batch *batch, void *batch_location);

static inline void
anv_batch_set_storage(struct anv_batch *batch, struct anv_address addr,
                      void *map, size_t size)
{
   batch->start_addr = addr;
   batch->next = batch->start = map;
   batch->end = map + size;
}

static inline VkResult
anv_batch_set_error(struct anv_batch *batch, VkResult error)
{
   assert(error != VK_SUCCESS);
   if (batch->status == VK_SUCCESS)
      batch->status = error;
   return batch->status;
}

static inline bool
anv_batch_has_error(struct anv_batch *batch)
{
   return batch->status != VK_SUCCESS;
}

#define ANV_NULL_ADDRESS ((struct anv_address) { NULL, 0 })

static inline bool
anv_address_is_null(struct anv_address addr)
{
   return addr.bo == NULL && addr.offset == 0;
}

static inline uint64_t
anv_address_physical(struct anv_address addr)
{
   if (addr.bo && (addr.bo->flags & EXEC_OBJECT_PINNED))
      return gen_canonical_address(addr.bo->offset + addr.offset);
   else
      return gen_canonical_address(addr.offset);
}

static inline struct anv_address
anv_address_add(struct anv_address addr, uint64_t offset)
{
   addr.offset += offset;
   return addr;
}

static inline void
write_reloc(const struct anv_device *device, void *p, uint64_t v, bool flush)
{
   unsigned reloc_size = 0;
   if (device->info.gen >= 8) {
      reloc_size = sizeof(uint64_t);
      *(uint64_t *)p = gen_canonical_address(v);
   } else {
      reloc_size = sizeof(uint32_t);
      *(uint32_t *)p = v;
   }

   if (flush && !device->info.has_llc)
      gen_flush_range(p, reloc_size);
}

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

#define anv_batch_emitn(batch, n, cmd, ...) ({             \
      void *__dst = anv_batch_emit_dwords(batch, n);       \
      if (__dst) {                                         \
         struct cmd __template = {                         \
            __anv_cmd_header(cmd),                         \
           .DWordLength = n - __anv_cmd_length_bias(cmd),  \
            __VA_ARGS__                                    \
         };                                                \
         __anv_cmd_pack(cmd)(batch, __dst, &__template);   \
      }                                                    \
      __dst;                                               \
   })

#define anv_batch_emit_merge(batch, dwords0, dwords1)                   \
   do {                                                                 \
      uint32_t *dw;                                                     \
                                                                        \
      STATIC_ASSERT(ARRAY_SIZE(dwords0) == ARRAY_SIZE(dwords1));        \
      dw = anv_batch_emit_dwords((batch), ARRAY_SIZE(dwords0));         \
      if (!dw)                                                          \
         break;                                                         \
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

/* #define __gen_get_batch_dwords anv_batch_emit_dwords */
/* #define __gen_get_batch_address anv_batch_address */
/* #define __gen_address_value anv_address_physical */
/* #define __gen_address_offset anv_address_add */

struct anv_device_memory {
   struct vk_object_base                        base;

   struct list_head                             link;

   struct anv_bo *                              bo;
   struct anv_memory_type *                     type;
   VkDeviceSize                                 map_size;
   void *                                       map;

   /* If set, we are holding reference to AHardwareBuffer
    * which we must release when memory is freed.
    */
   struct AHardwareBuffer *                     ahw;

   /* If set, this memory comes from a host pointer. */
   void *                                       host_ptr;
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

/** Struct representing a sampled image descriptor
 *
 * This descriptor layout is used for sampled images, bare sampler, and
 * combined image/sampler descriptors.
 */
struct anv_sampled_image_descriptor {
   /** Bindless image handle
    *
    * This is expected to already be shifted such that the 20-bit
    * SURFACE_STATE table index is in the top 20 bits.
    */
   uint32_t image;

   /** Bindless sampler handle
    *
    * This is assumed to be a 32B-aligned SAMPLER_STATE pointer relative
    * to the dynamic state base address.
    */
   uint32_t sampler;
};

struct anv_texture_swizzle_descriptor {
   /** Texture swizzle
    *
    * See also nir_intrinsic_channel_select_intel
    */
   uint8_t swizzle[4];

   /** Unused padding to ensure the struct is a multiple of 64 bits */
   uint32_t _pad;
};

/** Struct representing a storage image descriptor */
struct anv_storage_image_descriptor {
   /** Bindless image handles
    *
    * These are expected to already be shifted such that the 20-bit
    * SURFACE_STATE table index is in the top 20 bits.
    */
   uint32_t read_write;
   uint32_t write_only;
};

/** Struct representing a address/range descriptor
 *
 * The fields of this struct correspond directly to the data layout of
 * nir_address_format_64bit_bounded_global addresses.  The last field is the
 * offset in the NIR address so it must be zero so that when you load the
 * descriptor you get a pointer to the start of the range.
 */
struct anv_address_range_descriptor {
   uint64_t address;
   uint32_t range;
   uint32_t zero;
};

enum anv_descriptor_data {
   /** The descriptor contains a BTI reference to a surface state */
   ANV_DESCRIPTOR_SURFACE_STATE  = (1 << 0),
   /** The descriptor contains a BTI reference to a sampler state */
   ANV_DESCRIPTOR_SAMPLER_STATE  = (1 << 1),
   /** The descriptor contains an actual buffer view */
   ANV_DESCRIPTOR_BUFFER_VIEW    = (1 << 2),
   /** The descriptor contains auxiliary image layout data */
   ANV_DESCRIPTOR_IMAGE_PARAM    = (1 << 3),
   /** The descriptor contains auxiliary image layout data */
   ANV_DESCRIPTOR_INLINE_UNIFORM = (1 << 4),
   /** anv_address_range_descriptor with a buffer address and range */
   ANV_DESCRIPTOR_ADDRESS_RANGE  = (1 << 5),
   /** Bindless surface handle */
   ANV_DESCRIPTOR_SAMPLED_IMAGE  = (1 << 6),
   /** Storage image handles */
   ANV_DESCRIPTOR_STORAGE_IMAGE  = (1 << 7),
   /** Storage image handles */
   ANV_DESCRIPTOR_TEXTURE_SWIZZLE  = (1 << 8),
};

struct anv_descriptor_set_binding_layout {
   /* The type of the descriptors in this binding */
   VkDescriptorType type;

   /* Flags provided when this binding was created */
   VkDescriptorBindingFlagsEXT flags;

   /* Bitfield representing the type of data this descriptor contains */
   enum anv_descriptor_data data;

   /* Maximum number of YCbCr texture/sampler planes */
   uint8_t max_plane_count;

   /* Number of array elements in this binding (or size in bytes for inline
    * uniform data)
    */
   uint16_t array_size;

   /* Index into the flattend descriptor set */
   uint16_t descriptor_index;

   /* Index into the dynamic state array for a dynamic buffer */
   int16_t dynamic_offset_index;

   /* Index into the descriptor set buffer views */
   int16_t buffer_view_index;

   /* Offset into the descriptor buffer where this descriptor lives */
   uint32_t descriptor_offset;

   /* Immutable samplers (or NULL if no immutable samplers) */
   struct anv_sampler **immutable_samplers;
};

unsigned anv_descriptor_size(const struct anv_descriptor_set_binding_layout *layout);

unsigned anv_descriptor_type_size(const struct anv_physical_device *pdevice,
                                  VkDescriptorType type);

bool anv_descriptor_supports_bindless(const struct anv_physical_device *pdevice,
                                      const struct anv_descriptor_set_binding_layout *binding,
                                      bool sampler);

bool anv_descriptor_requires_bindless(const struct anv_physical_device *pdevice,
                                      const struct anv_descriptor_set_binding_layout *binding,
                                      bool sampler);

struct anv_descriptor_set_layout {
   struct vk_object_base base;

   /* Descriptor set layouts can be destroyed at almost any time */
   uint32_t ref_cnt;

   /* Number of bindings in this descriptor set */
   uint16_t binding_count;

   /* Total number of descriptors */
   uint16_t descriptor_count;

   /* Shader stages affected by this descriptor set */
   uint16_t shader_stages;

   /* Number of buffer views in this descriptor set */
   uint16_t buffer_view_count;

   /* Number of dynamic offsets used by this descriptor set */
   uint16_t dynamic_offset_count;

   /* For each dynamic buffer, which VkShaderStageFlagBits stages are using
    * this buffer
    */
   VkShaderStageFlags dynamic_offset_stages[MAX_DYNAMIC_BUFFERS];

   /* Size of the descriptor buffer for this descriptor set */
   uint32_t descriptor_buffer_size;

   /* Bindings in this descriptor set */
   struct anv_descriptor_set_binding_layout binding[0];
};

void anv_descriptor_set_layout_destroy(struct anv_device *device,
                                       struct anv_descriptor_set_layout *layout);

static inline void
anv_descriptor_set_layout_ref(struct anv_descriptor_set_layout *layout)
{
   assert(layout && layout->ref_cnt >= 1);
   p_atomic_inc(&layout->ref_cnt);
}

static inline void
anv_descriptor_set_layout_unref(struct anv_device *device,
                                struct anv_descriptor_set_layout *layout)
{
   assert(layout && layout->ref_cnt >= 1);
   if (p_atomic_dec_zero(&layout->ref_cnt))
      anv_descriptor_set_layout_destroy(device, layout);
}

struct anv_descriptor {
   VkDescriptorType type;

   union {
      struct {
         VkImageLayout layout;
         struct anv_image_view *image_view;
         struct anv_sampler *sampler;
      };

      struct {
         struct anv_buffer *buffer;
         uint64_t offset;
         uint64_t range;
      };

      struct anv_buffer_view *buffer_view;
   };
};

struct anv_descriptor_set {
   struct vk_object_base base;

   struct anv_descriptor_pool *pool;
   struct anv_descriptor_set_layout *layout;

   /* Amount of space occupied in the the pool by this descriptor set. It can
    * be larger than the size of the descriptor set.
    */
   uint32_t size;

   /* State relative to anv_descriptor_pool::bo */
   struct anv_state desc_mem;
   /* Surface state for the descriptor buffer */
   struct anv_state desc_surface_state;

   uint32_t buffer_view_count;
   struct anv_buffer_view *buffer_views;

   /* Link to descriptor pool's desc_sets list . */
   struct list_head pool_link;

   uint32_t descriptor_count;
   struct anv_descriptor descriptors[0];
};

struct anv_buffer_view {
   struct vk_object_base base;

   enum isl_format format; /**< VkBufferViewCreateInfo::format */
   uint64_t range; /**< VkBufferViewCreateInfo::range */

   struct anv_address address;

   struct anv_state surface_state;
   struct anv_state storage_surface_state;
   struct anv_state writeonly_storage_surface_state;

   struct brw_image_param storage_image_param;
};

struct anv_push_descriptor_set {
   struct anv_descriptor_set set;

   /* Put this field right behind anv_descriptor_set so it fills up the
    * descriptors[0] field. */
   struct anv_descriptor descriptors[MAX_PUSH_DESCRIPTORS];

   /** True if the descriptor set buffer has been referenced by a draw or
    * dispatch command.
    */
   bool set_used_on_gpu;

   struct anv_buffer_view buffer_views[MAX_PUSH_DESCRIPTORS];
};

struct anv_descriptor_pool {
   struct vk_object_base base;

   uint32_t size;
   uint32_t next;
   uint32_t free_list;

   struct anv_bo *bo;
   struct util_vma_heap bo_heap;

   struct anv_state_stream surface_state_stream;
   void *surface_state_free_list;

   struct list_head desc_sets;

   char data[0];
};

enum anv_descriptor_template_entry_type {
   ANV_DESCRIPTOR_TEMPLATE_ENTRY_TYPE_IMAGE,
   ANV_DESCRIPTOR_TEMPLATE_ENTRY_TYPE_BUFFER,
   ANV_DESCRIPTOR_TEMPLATE_ENTRY_TYPE_BUFFER_VIEW
};

struct anv_descriptor_template_entry {
   /* The type of descriptor in this entry */
   VkDescriptorType type;

   /* Binding in the descriptor set */
   uint32_t binding;

   /* Offset at which to write into the descriptor set binding */
   uint32_t array_element;

   /* Number of elements to write into the descriptor set binding */
   uint32_t array_count;

   /* Offset into the user provided data */
   size_t offset;

   /* Stride between elements into the user provided data */
   size_t stride;
};

struct anv_descriptor_update_template {
    struct vk_object_base base;

    VkPipelineBindPoint bind_point;

   /* The descriptor set this template corresponds to. This value is only
    * valid if the template was created with the templateType
    * VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET.
    */
   uint8_t set;

   /* Number of entries in this template */
   uint32_t entry_count;

   /* Entries of the template */
   struct anv_descriptor_template_entry entries[0];
};

size_t
anv_descriptor_set_layout_size(const struct anv_descriptor_set_layout *layout,
                               uint32_t var_desc_count);

void
anv_descriptor_set_write_image_view(struct anv_device *device,
                                    struct anv_descriptor_set *set,
                                    const VkDescriptorImageInfo * const info,
                                    VkDescriptorType type,
                                    uint32_t binding,
                                    uint32_t element);

void
anv_descriptor_set_write_buffer_view(struct anv_device *device,
                                     struct anv_descriptor_set *set,
                                     VkDescriptorType type,
                                     struct anv_buffer_view *buffer_view,
                                     uint32_t binding,
                                     uint32_t element);

void
anv_descriptor_set_write_buffer(struct anv_device *device,
                                struct anv_descriptor_set *set,
                                struct anv_state_stream *alloc_stream,
                                VkDescriptorType type,
                                struct anv_buffer *buffer,
                                uint32_t binding,
                                uint32_t element,
                                VkDeviceSize offset,
                                VkDeviceSize range);
void
anv_descriptor_set_write_inline_uniform_data(struct anv_device *device,
                                             struct anv_descriptor_set *set,
                                             uint32_t binding,
                                             const void *data,
                                             size_t offset,
                                             size_t size);

void
anv_descriptor_set_write_template(struct anv_device *device,
                                  struct anv_descriptor_set *set,
                                  struct anv_state_stream *alloc_stream,
                                  const struct anv_descriptor_update_template *template,
                                  const void *data);

VkResult
anv_descriptor_set_create(struct anv_device *device,
                          struct anv_descriptor_pool *pool,
                          struct anv_descriptor_set_layout *layout,
                          uint32_t var_desc_count,
                          struct anv_descriptor_set **out_set);

void
anv_descriptor_set_destroy(struct anv_device *device,
                           struct anv_descriptor_pool *pool,
                           struct anv_descriptor_set *set);

#define ANV_DESCRIPTOR_SET_NULL             (UINT8_MAX - 5)
#define ANV_DESCRIPTOR_SET_PUSH_CONSTANTS   (UINT8_MAX - 4)
#define ANV_DESCRIPTOR_SET_DESCRIPTORS      (UINT8_MAX - 3)
#define ANV_DESCRIPTOR_SET_NUM_WORK_GROUPS  (UINT8_MAX - 2)
#define ANV_DESCRIPTOR_SET_SHADER_CONSTANTS (UINT8_MAX - 1)
#define ANV_DESCRIPTOR_SET_COLOR_ATTACHMENTS UINT8_MAX

struct anv_pipeline_binding {
   /** Index in the descriptor set
    *
    * This is a flattened index; the descriptor set layout is already taken
    * into account.
    */
   uint32_t index;

   /** The descriptor set this surface corresponds to.
    *
    * The special ANV_DESCRIPTOR_SET_* values above indicates that this
    * binding is not a normal descriptor set but something else.
    */
   uint8_t set;

   union {
      /** Plane in the binding index for images */
      uint8_t plane;

      /** Input attachment index (relative to the subpass) */
      uint8_t input_attachment_index;

      /** Dynamic offset index (for dynamic UBOs and SSBOs) */
      uint8_t dynamic_offset_index;
   };

   /** For a storage image, whether it is write-only */
   uint8_t write_only;

   /** Pad to 64 bits so that there are no holes and we can safely memcmp
    * assuming POD zero-initialization.
    */
   uint8_t pad;
};

struct anv_push_range {
   /** Index in the descriptor set */
   uint32_t index;

   /** Descriptor set index */
   uint8_t set;

   /** Dynamic offset index (for dynamic UBOs) */
   uint8_t dynamic_offset_index;

   /** Start offset in units of 32B */
   uint8_t start;

   /** Range in units of 32B */
   uint8_t length;
};

struct anv_pipeline_layout {
   struct vk_object_base base;

   struct {
      struct anv_descriptor_set_layout *layout;
      uint32_t dynamic_offset_start;
   } set[MAX_SETS];

   uint32_t num_sets;

   unsigned char sha1[20];
};

struct anv_buffer {
   struct vk_object_base                        base;

   struct anv_device *                          device;
   VkDeviceSize                                 size;

   VkBufferUsageFlags                           usage;

   /* Set when bound */
   struct anv_address                           address;
};

static inline uint64_t
anv_buffer_get_range(struct anv_buffer *buffer, uint64_t offset, uint64_t range)
{
   assert(offset <= buffer->size);
   if (range == VK_WHOLE_SIZE) {
      return buffer->size - offset;
   } else {
      assert(range + offset >= range);
      assert(range + offset <= buffer->size);
      return range;
   }
}

enum anv_cmd_dirty_bits {
   ANV_CMD_DIRTY_DYNAMIC_VIEWPORT                    = 1 << 0, /* VK_DYNAMIC_STATE_VIEWPORT */
   ANV_CMD_DIRTY_DYNAMIC_SCISSOR                     = 1 << 1, /* VK_DYNAMIC_STATE_SCISSOR */
   ANV_CMD_DIRTY_DYNAMIC_LINE_WIDTH                  = 1 << 2, /* VK_DYNAMIC_STATE_LINE_WIDTH */
   ANV_CMD_DIRTY_DYNAMIC_DEPTH_BIAS                  = 1 << 3, /* VK_DYNAMIC_STATE_DEPTH_BIAS */
   ANV_CMD_DIRTY_DYNAMIC_BLEND_CONSTANTS             = 1 << 4, /* VK_DYNAMIC_STATE_BLEND_CONSTANTS */
   ANV_CMD_DIRTY_DYNAMIC_DEPTH_BOUNDS                = 1 << 5, /* VK_DYNAMIC_STATE_DEPTH_BOUNDS */
   ANV_CMD_DIRTY_DYNAMIC_STENCIL_COMPARE_MASK        = 1 << 6, /* VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK */
   ANV_CMD_DIRTY_DYNAMIC_STENCIL_WRITE_MASK          = 1 << 7, /* VK_DYNAMIC_STATE_STENCIL_WRITE_MASK */
   ANV_CMD_DIRTY_DYNAMIC_STENCIL_REFERENCE           = 1 << 8, /* VK_DYNAMIC_STATE_STENCIL_REFERENCE */
   ANV_CMD_DIRTY_PIPELINE                            = 1 << 9,
   ANV_CMD_DIRTY_INDEX_BUFFER                        = 1 << 10,
   ANV_CMD_DIRTY_RENDER_TARGETS                      = 1 << 11,
   ANV_CMD_DIRTY_XFB_ENABLE                          = 1 << 12,
   ANV_CMD_DIRTY_DYNAMIC_LINE_STIPPLE                = 1 << 13, /* VK_DYNAMIC_STATE_LINE_STIPPLE_EXT */
   ANV_CMD_DIRTY_DYNAMIC_CULL_MODE                   = 1 << 14, /* VK_DYNAMIC_STATE_CULL_MODE_EXT */
   ANV_CMD_DIRTY_DYNAMIC_FRONT_FACE                  = 1 << 15, /* VK_DYNAMIC_STATE_FRONT_FACE_EXT */
   ANV_CMD_DIRTY_DYNAMIC_PRIMITIVE_TOPOLOGY          = 1 << 16, /* VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY_EXT */
   ANV_CMD_DIRTY_DYNAMIC_VERTEX_INPUT_BINDING_STRIDE = 1 << 17, /* VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE_EXT */
   ANV_CMD_DIRTY_DYNAMIC_DEPTH_TEST_ENABLE           = 1 << 18, /* VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE_EXT */
   ANV_CMD_DIRTY_DYNAMIC_DEPTH_WRITE_ENABLE          = 1 << 19, /* VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE_EXT */
   ANV_CMD_DIRTY_DYNAMIC_DEPTH_COMPARE_OP            = 1 << 20, /* VK_DYNAMIC_STATE_DEPTH_COMPARE_OP_EXT */
   ANV_CMD_DIRTY_DYNAMIC_DEPTH_BOUNDS_TEST_ENABLE    = 1 << 21, /* VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE_EXT */
   ANV_CMD_DIRTY_DYNAMIC_STENCIL_TEST_ENABLE         = 1 << 22, /* VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE_EXT */
   ANV_CMD_DIRTY_DYNAMIC_STENCIL_OP                  = 1 << 23, /* VK_DYNAMIC_STATE_STENCIL_OP_EXT */
};
typedef uint32_t anv_cmd_dirty_mask_t;

#define ANV_CMD_DIRTY_DYNAMIC_ALL                       \
   (ANV_CMD_DIRTY_DYNAMIC_VIEWPORT |                    \
    ANV_CMD_DIRTY_DYNAMIC_SCISSOR |                     \
    ANV_CMD_DIRTY_DYNAMIC_LINE_WIDTH |                  \
    ANV_CMD_DIRTY_DYNAMIC_DEPTH_BIAS |                  \
    ANV_CMD_DIRTY_DYNAMIC_BLEND_CONSTANTS |             \
    ANV_CMD_DIRTY_DYNAMIC_DEPTH_BOUNDS |                \
    ANV_CMD_DIRTY_DYNAMIC_STENCIL_COMPARE_MASK |        \
    ANV_CMD_DIRTY_DYNAMIC_STENCIL_WRITE_MASK |          \
    ANV_CMD_DIRTY_DYNAMIC_STENCIL_REFERENCE |           \
    ANV_CMD_DIRTY_DYNAMIC_LINE_STIPPLE |                \
    ANV_CMD_DIRTY_DYNAMIC_CULL_MODE |                   \
    ANV_CMD_DIRTY_DYNAMIC_FRONT_FACE |                  \
    ANV_CMD_DIRTY_DYNAMIC_PRIMITIVE_TOPOLOGY |          \
    ANV_CMD_DIRTY_DYNAMIC_VERTEX_INPUT_BINDING_STRIDE | \
    ANV_CMD_DIRTY_DYNAMIC_DEPTH_TEST_ENABLE |           \
    ANV_CMD_DIRTY_DYNAMIC_DEPTH_WRITE_ENABLE |          \
    ANV_CMD_DIRTY_DYNAMIC_DEPTH_COMPARE_OP |            \
    ANV_CMD_DIRTY_DYNAMIC_DEPTH_BOUNDS_TEST_ENABLE |    \
    ANV_CMD_DIRTY_DYNAMIC_STENCIL_TEST_ENABLE |         \
    ANV_CMD_DIRTY_DYNAMIC_STENCIL_OP)

static inline enum anv_cmd_dirty_bits
anv_cmd_dirty_bit_for_vk_dynamic_state(VkDynamicState vk_state)
{
   switch (vk_state) {
   case VK_DYNAMIC_STATE_VIEWPORT:
   case VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT_EXT:
      return ANV_CMD_DIRTY_DYNAMIC_VIEWPORT;
   case VK_DYNAMIC_STATE_SCISSOR:
   case VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT_EXT:
      return ANV_CMD_DIRTY_DYNAMIC_SCISSOR;
   case VK_DYNAMIC_STATE_LINE_WIDTH:
      return ANV_CMD_DIRTY_DYNAMIC_LINE_WIDTH;
   case VK_DYNAMIC_STATE_DEPTH_BIAS:
      return ANV_CMD_DIRTY_DYNAMIC_DEPTH_BIAS;
   case VK_DYNAMIC_STATE_BLEND_CONSTANTS:
      return ANV_CMD_DIRTY_DYNAMIC_BLEND_CONSTANTS;
   case VK_DYNAMIC_STATE_DEPTH_BOUNDS:
      return ANV_CMD_DIRTY_DYNAMIC_DEPTH_BOUNDS;
   case VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK:
      return ANV_CMD_DIRTY_DYNAMIC_STENCIL_COMPARE_MASK;
   case VK_DYNAMIC_STATE_STENCIL_WRITE_MASK:
      return ANV_CMD_DIRTY_DYNAMIC_STENCIL_WRITE_MASK;
   case VK_DYNAMIC_STATE_STENCIL_REFERENCE:
      return ANV_CMD_DIRTY_DYNAMIC_STENCIL_REFERENCE;
   case VK_DYNAMIC_STATE_LINE_STIPPLE_EXT:
      return ANV_CMD_DIRTY_DYNAMIC_LINE_STIPPLE;
   case VK_DYNAMIC_STATE_CULL_MODE_EXT:
      return ANV_CMD_DIRTY_DYNAMIC_CULL_MODE;
   case VK_DYNAMIC_STATE_FRONT_FACE_EXT:
      return ANV_CMD_DIRTY_DYNAMIC_FRONT_FACE;
   case VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY_EXT:
      return ANV_CMD_DIRTY_DYNAMIC_PRIMITIVE_TOPOLOGY;
   case VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE_EXT:
      return ANV_CMD_DIRTY_DYNAMIC_VERTEX_INPUT_BINDING_STRIDE;
   case VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE_EXT:
      return ANV_CMD_DIRTY_DYNAMIC_DEPTH_TEST_ENABLE;
   case VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE_EXT:
      return ANV_CMD_DIRTY_DYNAMIC_DEPTH_WRITE_ENABLE;
   case VK_DYNAMIC_STATE_DEPTH_COMPARE_OP_EXT:
      return ANV_CMD_DIRTY_DYNAMIC_DEPTH_COMPARE_OP;
   case VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE_EXT:
      return ANV_CMD_DIRTY_DYNAMIC_DEPTH_BOUNDS_TEST_ENABLE;
   case VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE_EXT:
      return ANV_CMD_DIRTY_DYNAMIC_STENCIL_TEST_ENABLE;
   case VK_DYNAMIC_STATE_STENCIL_OP_EXT:
      return ANV_CMD_DIRTY_DYNAMIC_STENCIL_OP;
   default:
      assert(!"Unsupported dynamic state");
      return 0;
   }
}


enum anv_pipe_bits {
   ANV_PIPE_DEPTH_CACHE_FLUSH_BIT            = (1 << 0),
   ANV_PIPE_STALL_AT_SCOREBOARD_BIT          = (1 << 1),
   ANV_PIPE_STATE_CACHE_INVALIDATE_BIT       = (1 << 2),
   ANV_PIPE_CONSTANT_CACHE_INVALIDATE_BIT    = (1 << 3),
   ANV_PIPE_VF_CACHE_INVALIDATE_BIT          = (1 << 4),
   ANV_PIPE_DATA_CACHE_FLUSH_BIT             = (1 << 5),
   ANV_PIPE_TILE_CACHE_FLUSH_BIT             = (1 << 6),
   ANV_PIPE_TEXTURE_CACHE_INVALIDATE_BIT     = (1 << 10),
   ANV_PIPE_INSTRUCTION_CACHE_INVALIDATE_BIT = (1 << 11),
   ANV_PIPE_RENDER_TARGET_CACHE_FLUSH_BIT    = (1 << 12),
   ANV_PIPE_DEPTH_STALL_BIT                  = (1 << 13),
   ANV_PIPE_CS_STALL_BIT                     = (1 << 20),
   ANV_PIPE_END_OF_PIPE_SYNC_BIT             = (1 << 21),

   /* This bit does not exist directly in PIPE_CONTROL.  Instead it means that
    * a flush has happened but not a CS stall.  The next time we do any sort
    * of invalidation we need to insert a CS stall at that time.  Otherwise,
    * we would have to CS stall on every flush which could be bad.
    */
   ANV_PIPE_NEEDS_END_OF_PIPE_SYNC_BIT       = (1 << 22),

   /* This bit does not exist directly in PIPE_CONTROL. It means that render
    * target operations related to transfer commands with VkBuffer as
    * destination are ongoing. Some operations like copies on the command
    * streamer might need to be aware of this to trigger the appropriate stall
    * before they can proceed with the copy.
    */
   ANV_PIPE_RENDER_TARGET_BUFFER_WRITES      = (1 << 23),

   /* This bit does not exist directly in PIPE_CONTROL. It means that Gen12
    * AUX-TT data has changed and we need to invalidate AUX-TT data.  This is
    * done by writing the AUX-TT register.
    */
   ANV_PIPE_AUX_TABLE_INVALIDATE_BIT         = (1 << 24),

   /* This bit does not exist directly in PIPE_CONTROL. It means that a
    * PIPE_CONTROL with a post-sync operation will follow. This is used to
    * implement a workaround for Gen9.
    */
   ANV_PIPE_POST_SYNC_BIT                    = (1 << 25),
};

#define ANV_PIPE_FLUSH_BITS ( \
   ANV_PIPE_DEPTH_CACHE_FLUSH_BIT | \
   ANV_PIPE_DATA_CACHE_FLUSH_BIT | \
   ANV_PIPE_RENDER_TARGET_CACHE_FLUSH_BIT | \
   ANV_PIPE_TILE_CACHE_FLUSH_BIT)

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
   ANV_PIPE_INSTRUCTION_CACHE_INVALIDATE_BIT | \
   ANV_PIPE_AUX_TABLE_INVALIDATE_BIT)

static inline enum anv_pipe_bits
anv_pipe_flush_bits_for_access_flags(struct anv_device *device,
                                     VkAccessFlags flags)
{
   enum anv_pipe_bits pipe_bits = 0;

   unsigned b;
   for_each_bit(b, flags) {
      switch ((VkAccessFlagBits)(1 << b)) {
      case VK_ACCESS_SHADER_WRITE_BIT:
         /* We're transitioning a buffer that was previously used as write
          * destination through the data port. To make its content available
          * to future operations, flush the data cache.
          */
         pipe_bits |= ANV_PIPE_DATA_CACHE_FLUSH_BIT;
         break;
      case VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT:
         /* We're transitioning a buffer that was previously used as render
          * target. To make its content available to future operations, flush
          * the render target cache.
          */
         pipe_bits |= ANV_PIPE_RENDER_TARGET_CACHE_FLUSH_BIT;
         break;
      case VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT:
         /* We're transitioning a buffer that was previously used as depth
          * buffer. To make its content available to future operations, flush
          * the depth cache.
          */
         pipe_bits |= ANV_PIPE_DEPTH_CACHE_FLUSH_BIT;
         break;
      case VK_ACCESS_TRANSFER_WRITE_BIT:
         /* We're transitioning a buffer that was previously used as a
          * transfer write destination. Generic write operations include color
          * & depth operations as well as buffer operations like :
          *     - vkCmdClearColorImage()
          *     - vkCmdClearDepthStencilImage()
          *     - vkCmdBlitImage()
          *     - vkCmdCopy*(), vkCmdUpdate*(), vkCmdFill*()
          *
          * Most of these operations are implemented using Blorp which writes
          * through the render target, so flush that cache to make it visible
          * to future operations. And for depth related operations we also
          * need to flush the depth cache.
          */
         pipe_bits |= ANV_PIPE_RENDER_TARGET_CACHE_FLUSH_BIT;
         pipe_bits |= ANV_PIPE_DEPTH_CACHE_FLUSH_BIT;
         break;
      case VK_ACCESS_MEMORY_WRITE_BIT:
         /* We're transitioning a buffer for generic write operations. Flush
          * all the caches.
          */
         pipe_bits |= ANV_PIPE_FLUSH_BITS;
         break;
      default:
         break; /* Nothing to do */
      }
   }

   return pipe_bits;
}

static inline enum anv_pipe_bits
anv_pipe_invalidate_bits_for_access_flags(struct anv_device *device,
                                          VkAccessFlags flags)
{
   enum anv_pipe_bits pipe_bits = 0;

   unsigned b;
   for_each_bit(b, flags) {
      switch ((VkAccessFlagBits)(1 << b)) {
      case VK_ACCESS_INDIRECT_COMMAND_READ_BIT:
         /* Indirect draw commands take a buffer as input that we're going to
          * read from the command streamer to load some of the HW registers
          * (see genX_cmd_buffer.c:load_indirect_parameters). This requires a
          * command streamer stall so that all the cache flushes have
          * completed before the command streamer loads from memory.
          */
         pipe_bits |=  ANV_PIPE_CS_STALL_BIT;
         /* Indirect draw commands also set gl_BaseVertex & gl_BaseIndex
          * through a vertex buffer, so invalidate that cache.
          */
         pipe_bits |= ANV_PIPE_VF_CACHE_INVALIDATE_BIT;
         /* For CmdDipatchIndirect, we also load gl_NumWorkGroups through a
          * UBO from the buffer, so we need to invalidate constant cache.
          */
         pipe_bits |= ANV_PIPE_CONSTANT_CACHE_INVALIDATE_BIT;
         break;
      case VK_ACCESS_INDEX_READ_BIT:
      case VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT:
         /* We transitioning a buffer to be used for as input for vkCmdDraw*
          * commands, so we invalidate the VF cache to make sure there is no
          * stale data when we start rendering.
          */
         pipe_bits |= ANV_PIPE_VF_CACHE_INVALIDATE_BIT;
         break;
      case VK_ACCESS_UNIFORM_READ_BIT:
         /* We transitioning a buffer to be used as uniform data. Because
          * uniform is accessed through the data port & sampler, we need to
          * invalidate the texture cache (sampler) & constant cache (data
          * port) to avoid stale data.
          */
         pipe_bits |= ANV_PIPE_CONSTANT_CACHE_INVALIDATE_BIT;
         if (device->physical->compiler->indirect_ubos_use_sampler)
            pipe_bits |= ANV_PIPE_TEXTURE_CACHE_INVALIDATE_BIT;
         else
            pipe_bits |= ANV_PIPE_DATA_CACHE_FLUSH_BIT;
         break;
      case VK_ACCESS_SHADER_READ_BIT:
      case VK_ACCESS_INPUT_ATTACHMENT_READ_BIT:
      case VK_ACCESS_TRANSFER_READ_BIT:
         /* Transitioning a buffer to be read through the sampler, so
          * invalidate the texture cache, we don't want any stale data.
          */
         pipe_bits |= ANV_PIPE_TEXTURE_CACHE_INVALIDATE_BIT;
         break;
      case VK_ACCESS_MEMORY_READ_BIT:
         /* Transitioning a buffer for generic read, invalidate all the
          * caches.
          */
         pipe_bits |= ANV_PIPE_INVALIDATE_BITS;
         break;
      case VK_ACCESS_MEMORY_WRITE_BIT:
         /* Generic write, make sure all previously written things land in
          * memory.
          */
         pipe_bits |= ANV_PIPE_FLUSH_BITS;
         break;
      case VK_ACCESS_CONDITIONAL_RENDERING_READ_BIT_EXT:
         /* Transitioning a buffer for conditional rendering. We'll load the
          * content of this buffer into HW registers using the command
          * streamer, so we need to stall the command streamer to make sure
          * any in-flight flush operations have completed.
          */
         pipe_bits |= ANV_PIPE_CS_STALL_BIT;
         break;
      default:
         break; /* Nothing to do */
      }
   }

   return pipe_bits;
}

#define VK_IMAGE_ASPECT_ANY_COLOR_BIT_ANV (         \
   VK_IMAGE_ASPECT_COLOR_BIT | \
   VK_IMAGE_ASPECT_PLANE_0_BIT | \
   VK_IMAGE_ASPECT_PLANE_1_BIT | \
   VK_IMAGE_ASPECT_PLANE_2_BIT)
#define VK_IMAGE_ASPECT_PLANES_BITS_ANV ( \
   VK_IMAGE_ASPECT_PLANE_0_BIT | \
   VK_IMAGE_ASPECT_PLANE_1_BIT | \
   VK_IMAGE_ASPECT_PLANE_2_BIT)

struct anv_vertex_binding {
   struct anv_buffer *                          buffer;
   VkDeviceSize                                 offset;
   VkDeviceSize                                 stride;
   VkDeviceSize                                 size;
};

struct anv_xfb_binding {
   struct anv_buffer *                          buffer;
   VkDeviceSize                                 offset;
   VkDeviceSize                                 size;
};

struct anv_push_constants {
   /** Push constant data provided by the client through vkPushConstants */
   uint8_t client_data[MAX_PUSH_CONSTANTS_SIZE];

   /** Dynamic offsets for dynamic UBOs and SSBOs */
   uint32_t dynamic_offsets[MAX_DYNAMIC_BUFFERS];

   /* Robust access pushed registers. */
   uint64_t push_reg_mask[MESA_SHADER_STAGES];

   /** Pad out to a multiple of 32 bytes */
   uint32_t pad[2];

   struct {
      /** Base workgroup ID
       *
       * Used for vkCmdDispatchBase.
       */
      uint32_t base_work_group_id[3];

      /** Subgroup ID
       *
       * This is never set by software but is implicitly filled out when
       * uploading the push constants for compute shaders.
       */
      uint32_t subgroup_id;
   } cs;
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

   struct {
      struct {
         VkStencilOp fail_op;
         VkStencilOp pass_op;
         VkStencilOp depth_fail_op;
         VkCompareOp compare_op;
      } front;
      struct {
         VkStencilOp fail_op;
         VkStencilOp pass_op;
         VkStencilOp depth_fail_op;
         VkCompareOp compare_op;
      } back;
   } stencil_op;

   struct {
      uint32_t                                  factor;
      uint16_t                                  pattern;
   } line_stipple;

   VkCullModeFlags                              cull_mode;
   VkFrontFace                                  front_face;
   VkPrimitiveTopology                          primitive_topology;
   bool                                         depth_test_enable;
   bool                                         depth_write_enable;
   VkCompareOp                                  depth_compare_op;
   bool                                         depth_bounds_test_enable;
   bool                                         stencil_test_enable;
   bool                                         dyn_vbo_stride;
   bool                                         dyn_vbo_size;
};

extern const struct anv_dynamic_state default_dynamic_state;

uint32_t anv_dynamic_state_copy(struct anv_dynamic_state *dest,
                                const struct anv_dynamic_state *src,
                                uint32_t copy_mask);

struct anv_surface_state {
   struct anv_state state;
   /** Address of the surface referred to by this state
    *
    * This address is relative to the start of the BO.
    */
   struct anv_address address;
   /* Address of the aux surface, if any
    *
    * This field is ANV_NULL_ADDRESS if and only if no aux surface exists.
    *
    * With the exception of gen8, the bottom 12 bits of this address' offset
    * include extra aux information.
    */
   struct anv_address aux_address;
   /* Address of the clear color, if any
    *
    * This address is relative to the start of the BO.
    */
   struct anv_address clear_address;
};

/**
 * Attachment state when recording a renderpass instance.
 *
 * The clear value is valid only if there exists a pending clear.
 */
struct anv_attachment_state {
   enum isl_aux_usage                           aux_usage;
   struct anv_surface_state                     color;
   struct anv_surface_state                     input;

   VkImageLayout                                current_layout;
   VkImageLayout                                current_stencil_layout;
   VkImageAspectFlags                           pending_clear_aspects;
   VkImageAspectFlags                           pending_load_aspects;
   bool                                         fast_clear;
   VkClearValue                                 clear_value;

   /* When multiview is active, attachments with a renderpass clear
    * operation have their respective layers cleared on the first
    * subpass that uses them, and only in that subpass. We keep track
    * of this using a bitfield to indicate which layers of an attachment
    * have not been cleared yet when multiview is active.
    */
   uint32_t                                     pending_clear_views;
   struct anv_image_view *                      image_view;
};

/** State tracking for vertex buffer flushes
 *
 * On Gen8-9, the VF cache only considers the bottom 32 bits of memory
 * addresses.  If you happen to have two vertex buffers which get placed
 * exactly 4 GiB apart and use them in back-to-back draw calls, you can get
 * collisions.  In order to solve this problem, we track vertex address ranges
 * which are live in the cache and invalidate the cache if one ever exceeds 32
 * bits.
 */
struct anv_vb_cache_range {
   /* Virtual address at which the live vertex buffer cache range starts for
    * this vertex buffer index.
    */
   uint64_t start;

   /* Virtual address of the byte after where vertex buffer cache range ends.
    * This is exclusive such that end - start is the size of the range.
    */
   uint64_t end;
};

/** State tracking for particular pipeline bind point
 *
 * This struct is the base struct for anv_cmd_graphics_state and
 * anv_cmd_compute_state.  These are used to track state which is bound to a
 * particular type of pipeline.  Generic state that applies per-stage such as
 * binding table offsets and push constants is tracked generically with a
 * per-stage array in anv_cmd_state.
 */
struct anv_cmd_pipeline_state {
   struct anv_descriptor_set *descriptors[MAX_SETS];
   struct anv_push_descriptor_set *push_descriptors[MAX_SETS];

   struct anv_push_constants push_constants;

   /* Push constant state allocated when flushing push constants. */
   struct anv_state          push_constants_state;
};

/** State tracking for graphics pipeline
 *
 * This has anv_cmd_pipeline_state as a base struct to track things which get
 * bound to a graphics pipeline.  Along with general pipeline bind point state
 * which is in the anv_cmd_pipeline_state base struct, it also contains other
 * state which is graphics-specific.
 */
struct anv_cmd_graphics_state {
   struct anv_cmd_pipeline_state base;

   struct anv_graphics_pipeline *pipeline;

   anv_cmd_dirty_mask_t dirty;
   uint32_t vb_dirty;

   struct anv_vb_cache_range ib_bound_range;
   struct anv_vb_cache_range ib_dirty_range;
   struct anv_vb_cache_range vb_bound_ranges[33];
   struct anv_vb_cache_range vb_dirty_ranges[33];

   VkShaderStageFlags push_constant_stages;

   struct anv_dynamic_state dynamic;

   uint32_t primitive_topology;

   struct {
      struct anv_buffer *index_buffer;
      uint32_t index_type; /**< 3DSTATE_INDEX_BUFFER.IndexFormat */
      uint32_t index_offset;
   } gen7;
};

/** State tracking for compute pipeline
 *
 * This has anv_cmd_pipeline_state as a base struct to track things which get
 * bound to a compute pipeline.  Along with general pipeline bind point state
 * which is in the anv_cmd_pipeline_state base struct, it also contains other
 * state which is compute-specific.
 */
struct anv_cmd_compute_state {
   struct anv_cmd_pipeline_state base;

   struct anv_compute_pipeline *pipeline;

   bool pipeline_dirty;

   struct anv_address num_workgroups;
};

/** State required while building cmd buffer */
struct anv_cmd_state {
   /* PIPELINE_SELECT.PipelineSelection */
   uint32_t                                     current_pipeline;
   const struct gen_l3_config *                 current_l3_config;
   uint32_t                                     last_aux_map_state;

   struct anv_cmd_graphics_state                gfx;
   struct anv_cmd_compute_state                 compute;

   enum anv_pipe_bits                           pending_pipe_bits;
   VkShaderStageFlags                           descriptors_dirty;
   VkShaderStageFlags                           push_constants_dirty;

   struct anv_framebuffer *                     framebuffer;
   struct anv_render_pass *                     pass;
   struct anv_subpass *                         subpass;
   VkRect2D                                     render_area;
   uint32_t                                     restart_index;
   struct anv_vertex_binding                    vertex_bindings[MAX_VBS];
   bool                                         xfb_enabled;
   struct anv_xfb_binding                       xfb_bindings[MAX_XFB_BUFFERS];
   struct anv_state                             binding_tables[MESA_SHADER_STAGES];
   struct anv_state                             samplers[MESA_SHADER_STAGES];

   unsigned char                                sampler_sha1s[MESA_SHADER_STAGES][20];
   unsigned char                                surface_sha1s[MESA_SHADER_STAGES][20];
   unsigned char                                push_sha1s[MESA_SHADER_STAGES][20];

   /**
    * Whether or not the gen8 PMA fix is enabled.  We ensure that, at the top
    * of any command buffer it is disabled by disabling it in EndCommandBuffer
    * and before invoking the secondary in ExecuteCommands.
    */
   bool                                         pma_fix_enabled;

   /**
    * Whether or not we know for certain that HiZ is enabled for the current
    * subpass.  If, for whatever reason, we are unsure as to whether HiZ is
    * enabled or not, this will be false.
    */
   bool                                         hiz_enabled;

   bool                                         conditional_render_enabled;

   /**
    * Last rendering scale argument provided to
    * genX(cmd_buffer_emit_hashing_mode)().
    */
   unsigned                                     current_hash_scale;

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
   struct anv_state                             attachment_states;

   /**
    * A null surface state of the right size to match the framebuffer.  This
    * is one of the states in attachment_states.
    */
   struct anv_state                             null_surface_state;
};

struct anv_cmd_pool {
   struct vk_object_base                        base;
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
   ANV_CMD_BUFFER_EXEC_MODE_CALL_AND_RETURN,
};

struct anv_cmd_buffer {
   struct vk_object_base                        base;

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
   struct u_vector                              bt_block_states;
   struct anv_state                             bt_next;

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

   struct anv_query_pool                       *perf_query_pool;

   struct anv_cmd_state                         state;

   struct anv_address                           return_addr;

   /* Set by SetPerformanceMarkerINTEL, written into queries by CmdBeginQuery */
   uint64_t                                     intel_perf_marker;
};

VkResult anv_cmd_buffer_init_batch_bo_chain(struct anv_cmd_buffer *cmd_buffer);
void anv_cmd_buffer_fini_batch_bo_chain(struct anv_cmd_buffer *cmd_buffer);
void anv_cmd_buffer_reset_batch_bo_chain(struct anv_cmd_buffer *cmd_buffer);
void anv_cmd_buffer_end_batch_buffer(struct anv_cmd_buffer *cmd_buffer);
void anv_cmd_buffer_add_secondary(struct anv_cmd_buffer *primary,
                                  struct anv_cmd_buffer *secondary);
void anv_cmd_buffer_prepare_execbuf(struct anv_cmd_buffer *cmd_buffer);
VkResult anv_cmd_buffer_execbuf(struct anv_queue *queue,
                                struct anv_cmd_buffer *cmd_buffer,
                                const VkSemaphore *in_semaphores,
                                const uint64_t *in_wait_values,
                                uint32_t num_in_semaphores,
                                const VkSemaphore *out_semaphores,
                                const uint64_t *out_signal_values,
                                uint32_t num_out_semaphores,
                                VkFence fence,
                                int perf_query_pass);

VkResult anv_cmd_buffer_reset(struct anv_cmd_buffer *cmd_buffer);

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
anv_cmd_buffer_gfx_push_constants(struct anv_cmd_buffer *cmd_buffer);
struct anv_state
anv_cmd_buffer_cs_push_constants(struct anv_cmd_buffer *cmd_buffer);

const struct anv_image_view *
anv_cmd_buffer_get_depth_stencil_view(const struct anv_cmd_buffer *cmd_buffer);

VkResult
anv_cmd_buffer_alloc_blorp_binding_table(struct anv_cmd_buffer *cmd_buffer,
                                         uint32_t num_entries,
                                         uint32_t *state_offset,
                                         struct anv_state *bt_state);

void anv_cmd_buffer_dump(struct anv_cmd_buffer *cmd_buffer);

void anv_cmd_emit_conditional_render_predicate(struct anv_cmd_buffer *cmd_buffer);

enum anv_fence_type {
   ANV_FENCE_TYPE_NONE = 0,
   ANV_FENCE_TYPE_BO,
   ANV_FENCE_TYPE_WSI_BO,
   ANV_FENCE_TYPE_SYNCOBJ,
   ANV_FENCE_TYPE_WSI,
};

enum anv_bo_fence_state {
   /** Indicates that this is a new (or newly reset fence) */
   ANV_BO_FENCE_STATE_RESET,

   /** Indicates that this fence has been submitted to the GPU but is still
    * (as far as we know) in use by the GPU.
    */
   ANV_BO_FENCE_STATE_SUBMITTED,

   ANV_BO_FENCE_STATE_SIGNALED,
};

struct anv_fence_impl {
   enum anv_fence_type type;

   union {
      /** Fence implementation for BO fences
       *
       * These fences use a BO and a set of CPU-tracked state flags.  The BO
       * is added to the object list of the last execbuf call in a QueueSubmit
       * and is marked EXEC_WRITE.  The state flags track when the BO has been
       * submitted to the kernel.  We need to do this because Vulkan lets you
       * wait on a fence that has not yet been submitted and I915_GEM_BUSY
       * will say it's idle in this case.
       */
      struct {
         struct anv_bo *bo;
         enum anv_bo_fence_state state;
      } bo;

      /** DRM syncobj handle for syncobj-based fences */
      uint32_t syncobj;

      /** WSI fence */
      struct wsi_fence *fence_wsi;
   };
};

struct anv_fence {
   struct vk_object_base base;

   /* Permanent fence state.  Every fence has some form of permanent state
    * (type != ANV_SEMAPHORE_TYPE_NONE).  This may be a BO to fence on (for
    * cross-process fences) or it could just be a dummy for use internally.
    */
   struct anv_fence_impl permanent;

   /* Temporary fence state.  A fence *may* have temporary state.  That state
    * is added to the fence by an import operation and is reset back to
    * ANV_SEMAPHORE_TYPE_NONE when the fence is reset.  A fence with temporary
    * state cannot be signaled because the fence must already be signaled
    * before the temporary state can be exported from the fence in the other
    * process and imported here.
    */
   struct anv_fence_impl temporary;
};

void anv_fence_reset_temporary(struct anv_device *device,
                               struct anv_fence *fence);

struct anv_event {
   struct vk_object_base                        base;
   uint64_t                                     semaphore;
   struct anv_state                             state;
};

enum anv_semaphore_type {
   ANV_SEMAPHORE_TYPE_NONE = 0,
   ANV_SEMAPHORE_TYPE_DUMMY,
   ANV_SEMAPHORE_TYPE_BO,
   ANV_SEMAPHORE_TYPE_WSI_BO,
   ANV_SEMAPHORE_TYPE_SYNC_FILE,
   ANV_SEMAPHORE_TYPE_DRM_SYNCOBJ,
   ANV_SEMAPHORE_TYPE_TIMELINE,
   ANV_SEMAPHORE_TYPE_DRM_SYNCOBJ_TIMELINE,
};

struct anv_timeline_point {
   struct list_head link;

   uint64_t serial;

   /* Number of waiter on this point, when > 0 the point should not be garbage
    * collected.
    */
   int waiting;

   /* BO used for synchronization. */
   struct anv_bo *bo;
};

struct anv_timeline {
   pthread_mutex_t mutex;
   pthread_cond_t  cond;

   uint64_t highest_past;
   uint64_t highest_pending;

   struct list_head points;
   struct list_head free_points;
};

struct anv_semaphore_impl {
   enum anv_semaphore_type type;

   union {
      /* A BO representing this semaphore when type == ANV_SEMAPHORE_TYPE_BO
       * or type == ANV_SEMAPHORE_TYPE_WSI_BO.  This BO will be added to the
       * object list on any execbuf2 calls for which this semaphore is used as
       * a wait or signal fence.  When used as a signal fence or when type ==
       * ANV_SEMAPHORE_TYPE_WSI_BO, the EXEC_OBJECT_WRITE flag will be set.
       */
      struct anv_bo *bo;

      /* The sync file descriptor when type == ANV_SEMAPHORE_TYPE_SYNC_FILE.
       * If the semaphore is in the unsignaled state due to either just being
       * created or because it has been used for a wait, fd will be -1.
       */
      int fd;

      /* Sync object handle when type == ANV_SEMAPHORE_TYPE_DRM_SYNCOBJ.
       * Unlike GEM BOs, DRM sync objects aren't deduplicated by the kernel on
       * import so we don't need to bother with a userspace cache.
       */
      uint32_t syncobj;

      /* Non shareable timeline semaphore
       *
       * Used when kernel don't have support for timeline semaphores.
       */
      struct anv_timeline timeline;
   };
};

struct anv_semaphore {
   struct vk_object_base base;

   uint32_t refcount;

   /* Permanent semaphore state.  Every semaphore has some form of permanent
    * state (type != ANV_SEMAPHORE_TYPE_NONE).  This may be a BO to fence on
    * (for cross-process semaphores0 or it could just be a dummy for use
    * internally.
    */
   struct anv_semaphore_impl permanent;

   /* Temporary semaphore state.  A semaphore *may* have temporary state.
    * That state is added to the semaphore by an import operation and is reset
    * back to ANV_SEMAPHORE_TYPE_NONE when the semaphore is waited on.  A
    * semaphore with temporary state cannot be signaled because the semaphore
    * must already be signaled before the temporary state can be exported from
    * the semaphore in the other process and imported here.
    */
   struct anv_semaphore_impl temporary;
};

void anv_semaphore_reset_temporary(struct anv_device *device,
                                   struct anv_semaphore *semaphore);

struct anv_shader_module {
   struct vk_object_base                        base;

   unsigned char                                sha1[20];
   uint32_t                                     size;
   char                                         data[0];
};

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

enum anv_shader_reloc {
   ANV_SHADER_RELOC_CONST_DATA_ADDR_LOW,
   ANV_SHADER_RELOC_CONST_DATA_ADDR_HIGH,
};

struct anv_pipeline_bind_map {
   unsigned char                                surface_sha1[20];
   unsigned char                                sampler_sha1[20];
   unsigned char                                push_sha1[20];

   uint32_t surface_count;
   uint32_t sampler_count;

   struct anv_pipeline_binding *                surface_to_descriptor;
   struct anv_pipeline_binding *                sampler_to_descriptor;

   struct anv_push_range                        push_ranges[4];
};

struct anv_shader_bin_key {
   uint32_t size;
   uint8_t data[0];
};

struct anv_shader_bin {
   uint32_t ref_cnt;

   gl_shader_stage stage;

   const struct anv_shader_bin_key *key;

   struct anv_state kernel;
   uint32_t kernel_size;

   const struct brw_stage_prog_data *prog_data;
   uint32_t prog_data_size;

   struct brw_compile_stats stats[3];
   uint32_t num_stats;

   struct nir_xfb_info *xfb_info;

   struct anv_pipeline_bind_map bind_map;
};

struct anv_shader_bin *
anv_shader_bin_create(struct anv_device *device,
                      gl_shader_stage stage,
                      const void *key, uint32_t key_size,
                      const void *kernel, uint32_t kernel_size,
                      const struct brw_stage_prog_data *prog_data,
                      uint32_t prog_data_size,
                      const struct brw_compile_stats *stats, uint32_t num_stats,
                      const struct nir_xfb_info *xfb_info,
                      const struct anv_pipeline_bind_map *bind_map);

void
anv_shader_bin_destroy(struct anv_device *device, struct anv_shader_bin *shader);

static inline void
anv_shader_bin_ref(struct anv_shader_bin *shader)
{
   assert(shader && shader->ref_cnt >= 1);
   p_atomic_inc(&shader->ref_cnt);
}

static inline void
anv_shader_bin_unref(struct anv_device *device, struct anv_shader_bin *shader)
{
   assert(shader && shader->ref_cnt >= 1);
   if (p_atomic_dec_zero(&shader->ref_cnt))
      anv_shader_bin_destroy(device, shader);
}

struct anv_pipeline_executable {
   gl_shader_stage stage;

   struct brw_compile_stats stats;

   char *nir;
   char *disasm;
};

enum anv_pipeline_type {
   ANV_PIPELINE_GRAPHICS,
   ANV_PIPELINE_COMPUTE,
};

struct anv_pipeline {
   struct vk_object_base                        base;

   struct anv_device *                          device;

   struct anv_batch                             batch;
   struct anv_reloc_list                        batch_relocs;

   void *                                       mem_ctx;

   enum anv_pipeline_type                       type;
   VkPipelineCreateFlags                        flags;

   struct util_dynarray                         executables;

   const struct gen_l3_config *                 l3_config;
};

struct anv_graphics_pipeline {
   struct anv_pipeline                          base;

   uint32_t                                     batch_data[512];

   anv_cmd_dirty_mask_t                         dynamic_state_mask;
   struct anv_dynamic_state                     dynamic_state;

   uint32_t                                     topology;

   struct anv_subpass *                         subpass;

   struct anv_shader_bin *                      shaders[MESA_SHADER_STAGES];

   VkShaderStageFlags                           active_stages;

   bool                                         primitive_restart;
   bool                                         writes_depth;
   bool                                         depth_test_enable;
   bool                                         writes_stencil;
   bool                                         stencil_test_enable;
   bool                                         depth_clamp_enable;
   bool                                         depth_clip_enable;
   bool                                         sample_shading_enable;
   bool                                         kill_pixel;
   bool                                         depth_bounds_test_enable;

   /* When primitive replication is used, subpass->view_mask will describe what
    * views to replicate.
    */
   bool                                         use_primitive_replication;

   struct anv_state                             blend_state;

   uint32_t                                     vb_used;
   struct anv_pipeline_vertex_binding {
      uint32_t                                  stride;
      bool                                      instanced;
      uint32_t                                  instance_divisor;
   } vb[MAX_VBS];

   struct {
      uint32_t                                  sf[7];
      uint32_t                                  depth_stencil_state[3];
      uint32_t                                  clip[4];
      uint32_t                                  xfb_bo_pitch[4];
   } gen7;

   struct {
      uint32_t                                  sf[4];
      uint32_t                                  raster[5];
      uint32_t                                  wm_depth_stencil[3];
   } gen8;

   struct {
      uint32_t                                  wm_depth_stencil[4];
   } gen9;
};

struct anv_compute_pipeline {
   struct anv_pipeline                          base;

   struct anv_shader_bin *                      cs;
   uint32_t                                     cs_right_mask;
   uint32_t                                     batch_data[9];
   uint32_t                                     interface_descriptor_data[8];
};

#define ANV_DECL_PIPELINE_DOWNCAST(pipe_type, pipe_enum)             \
   static inline struct anv_##pipe_type##_pipeline *                 \
   anv_pipeline_to_##pipe_type(struct anv_pipeline *pipeline)      \
   {                                                                 \
      assert(pipeline->type == pipe_enum);                           \
      return (struct anv_##pipe_type##_pipeline *) pipeline;         \
   }

ANV_DECL_PIPELINE_DOWNCAST(graphics, ANV_PIPELINE_GRAPHICS)
ANV_DECL_PIPELINE_DOWNCAST(compute, ANV_PIPELINE_COMPUTE)

static inline bool
anv_pipeline_has_stage(const struct anv_graphics_pipeline *pipeline,
                       gl_shader_stage stage)
{
   return (pipeline->active_stages & mesa_to_vk_shader_stage(stage)) != 0;
}

#define ANV_DECL_GET_GRAPHICS_PROG_DATA_FUNC(prefix, stage)             \
static inline const struct brw_##prefix##_prog_data *                   \
get_##prefix##_prog_data(const struct anv_graphics_pipeline *pipeline)  \
{                                                                       \
   if (anv_pipeline_has_stage(pipeline, stage)) {                       \
      return (const struct brw_##prefix##_prog_data *)                  \
             pipeline->shaders[stage]->prog_data;                       \
   } else {                                                             \
      return NULL;                                                      \
   }                                                                    \
}

ANV_DECL_GET_GRAPHICS_PROG_DATA_FUNC(vs, MESA_SHADER_VERTEX)
ANV_DECL_GET_GRAPHICS_PROG_DATA_FUNC(tcs, MESA_SHADER_TESS_CTRL)
ANV_DECL_GET_GRAPHICS_PROG_DATA_FUNC(tes, MESA_SHADER_TESS_EVAL)
ANV_DECL_GET_GRAPHICS_PROG_DATA_FUNC(gs, MESA_SHADER_GEOMETRY)
ANV_DECL_GET_GRAPHICS_PROG_DATA_FUNC(wm, MESA_SHADER_FRAGMENT)

static inline const struct brw_cs_prog_data *
get_cs_prog_data(const struct anv_compute_pipeline *pipeline)
{
   assert(pipeline->cs);
   return (const struct brw_cs_prog_data *) pipeline->cs->prog_data;
}

static inline const struct brw_vue_prog_data *
anv_pipeline_get_last_vue_prog_data(const struct anv_graphics_pipeline *pipeline)
{
   if (anv_pipeline_has_stage(pipeline, MESA_SHADER_GEOMETRY))
      return &get_gs_prog_data(pipeline)->base;
   else if (anv_pipeline_has_stage(pipeline, MESA_SHADER_TESS_EVAL))
      return &get_tes_prog_data(pipeline)->base;
   else
      return &get_vs_prog_data(pipeline)->base;
}

VkResult
anv_pipeline_init(struct anv_pipeline *pipeline,
                  struct anv_device *device,
                  enum anv_pipeline_type type,
                  VkPipelineCreateFlags flags,
                  const VkAllocationCallbacks *pAllocator);

void
anv_pipeline_finish(struct anv_pipeline *pipeline,
                    struct anv_device *device,
                    const VkAllocationCallbacks *pAllocator);

VkResult
anv_graphics_pipeline_init(struct anv_graphics_pipeline *pipeline, struct anv_device *device,
                           struct anv_pipeline_cache *cache,
                           const VkGraphicsPipelineCreateInfo *pCreateInfo,
                           const VkAllocationCallbacks *alloc);

VkResult
anv_pipeline_compile_cs(struct anv_compute_pipeline *pipeline,
                        struct anv_pipeline_cache *cache,
                        const VkComputePipelineCreateInfo *info,
                        const struct anv_shader_module *module,
                        const char *entrypoint,
                        const VkSpecializationInfo *spec_info);

struct anv_cs_parameters {
   uint32_t group_size;
   uint32_t simd_size;
   uint32_t threads;
};

struct anv_cs_parameters
anv_cs_parameters(const struct anv_compute_pipeline *pipeline);

struct anv_format_plane {
   enum isl_format isl_format:16;
   struct isl_swizzle swizzle;

   /* Whether this plane contains chroma channels */
   bool has_chroma;

   /* For downscaling of YUV planes */
   uint8_t denominator_scales[2];

   /* How to map sampled ycbcr planes to a single 4 component element. */
   struct isl_swizzle ycbcr_swizzle;

   /* What aspect is associated to this plane */
   VkImageAspectFlags aspect;
};


struct anv_format {
   struct anv_format_plane planes[3];
   VkFormat vk_format;
   uint8_t n_planes;
   bool can_ycbcr;
};

/**
 * Return the aspect's _format_ plane, not its _memory_ plane (using the
 * vocabulary of VK_EXT_image_drm_format_modifier). As a consequence, \a
 * aspect_mask may contain VK_IMAGE_ASPECT_PLANE_*, but must not contain
 * VK_IMAGE_ASPECT_MEMORY_PLANE_* .
 */
static inline uint32_t
anv_image_aspect_to_plane(VkImageAspectFlags image_aspects,
                          VkImageAspectFlags aspect_mask)
{
   switch (aspect_mask) {
   case VK_IMAGE_ASPECT_COLOR_BIT:
   case VK_IMAGE_ASPECT_DEPTH_BIT:
   case VK_IMAGE_ASPECT_PLANE_0_BIT:
      return 0;
   case VK_IMAGE_ASPECT_STENCIL_BIT:
      if ((image_aspects & VK_IMAGE_ASPECT_DEPTH_BIT) == 0)
         return 0;
      /* Fall-through */
   case VK_IMAGE_ASPECT_PLANE_1_BIT:
      return 1;
   case VK_IMAGE_ASPECT_PLANE_2_BIT:
      return 2;
   default:
      /* Purposefully assert with depth/stencil aspects. */
      unreachable("invalid image aspect");
   }
}

static inline VkImageAspectFlags
anv_plane_to_aspect(VkImageAspectFlags image_aspects,
                    uint32_t plane)
{
   if (image_aspects & VK_IMAGE_ASPECT_ANY_COLOR_BIT_ANV) {
      if (util_bitcount(image_aspects) > 1)
         return VK_IMAGE_ASPECT_PLANE_0_BIT << plane;
      return VK_IMAGE_ASPECT_COLOR_BIT;
   }
   if (image_aspects & VK_IMAGE_ASPECT_DEPTH_BIT)
      return VK_IMAGE_ASPECT_DEPTH_BIT << plane;
   assert(image_aspects == VK_IMAGE_ASPECT_STENCIL_BIT);
   return VK_IMAGE_ASPECT_STENCIL_BIT;
}

#define anv_foreach_image_aspect_bit(b, image, aspects) \
   for_each_bit(b, anv_image_expand_aspects(image, aspects))

const struct anv_format *
anv_get_format(VkFormat format);

static inline uint32_t
anv_get_format_planes(VkFormat vk_format)
{
   const struct anv_format *format = anv_get_format(vk_format);

   return format != NULL ? format->n_planes : 0;
}

struct anv_format_plane
anv_get_format_plane(const struct gen_device_info *devinfo, VkFormat vk_format,
                     VkImageAspectFlagBits aspect, VkImageTiling tiling);

static inline enum isl_format
anv_get_isl_format(const struct gen_device_info *devinfo, VkFormat vk_format,
                   VkImageAspectFlags aspect, VkImageTiling tiling)
{
   return anv_get_format_plane(devinfo, vk_format, aspect, tiling).isl_format;
}

bool anv_formats_ccs_e_compatible(const struct gen_device_info *devinfo,
                                  VkImageCreateFlags create_flags,
                                  VkFormat vk_format,
                                  VkImageTiling vk_tiling,
                                  const VkImageFormatListCreateInfoKHR *fmt_list);

static inline struct isl_swizzle
anv_swizzle_for_render(struct isl_swizzle swizzle)
{
   /* Sometimes the swizzle will have alpha map to one.  We do this to fake
    * RGB as RGBA for texturing
    */
   assert(swizzle.a == ISL_CHANNEL_SELECT_ONE ||
          swizzle.a == ISL_CHANNEL_SELECT_ALPHA);

   /* But it doesn't matter what we render to that channel */
   swizzle.a = ISL_CHANNEL_SELECT_ALPHA;

   return swizzle;
}

void
anv_pipeline_setup_l3_config(struct anv_pipeline *pipeline, bool needs_slm);

/**
 * Subsurface of an anv_image.
 */
struct anv_surface {
   /** Valid only if isl_surf::size_B > 0. */
   struct isl_surf isl;

   /**
    * Offset from VkImage's base address, as bound by vkBindImageMemory().
    */
   uint32_t offset;
};

struct anv_image {
   struct vk_object_base base;

   VkImageType type; /**< VkImageCreateInfo::imageType */
   /* The original VkFormat provided by the client.  This may not match any
    * of the actual surface formats.
    */
   VkFormat vk_format;
   const struct anv_format *format;

   VkImageAspectFlags aspects;
   VkExtent3D extent;
   uint32_t levels;
   uint32_t array_size;
   uint32_t samples; /**< VkImageCreateInfo::samples */
   uint32_t n_planes;
   VkImageUsageFlags usage; /**< VkImageCreateInfo::usage. */
   VkImageUsageFlags stencil_usage;
   VkImageCreateFlags create_flags; /* Flags used when creating image. */
   VkImageTiling tiling; /** VkImageCreateInfo::tiling */

   /** True if this is needs to be bound to an appropriately tiled BO.
    *
    * When not using modifiers, consumers such as X11, Wayland, and KMS need
    * the tiling passed via I915_GEM_SET_TILING.  When exporting these buffers
    * we require a dedicated allocation so that we can know to allocate a
    * tiled buffer.
    */
   bool needs_set_tiling;

   /**
    * Must be DRM_FORMAT_MOD_INVALID unless tiling is
    * VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT.
    */
   uint64_t drm_format_mod;

   VkDeviceSize size;
   uint32_t alignment;

   /* Whether the image is made of several underlying buffer objects rather a
    * single one with different offsets.
    */
   bool disjoint;

   /* Image was created with external format. */
   bool external_format;

   /**
    * Image subsurfaces
    *
    * For each foo, anv_image::planes[x].surface is valid if and only if
    * anv_image::aspects has a x aspect. Refer to anv_image_aspect_to_plane()
    * to figure the number associated with a given aspect.
    *
    * The hardware requires that the depth buffer and stencil buffer be
    * separate surfaces.  From Vulkan's perspective, though, depth and stencil
    * reside in the same VkImage.  To satisfy both the hardware and Vulkan, we
    * allocate the depth and stencil buffers as separate surfaces in the same
    * bo.
    *
    * Memory layout :
    *
    * -----------------------
    * |     surface0        |   /|\
    * -----------------------    |
    * |   shadow surface0   |    |
    * -----------------------    | Plane 0
    * |    aux surface0     |    |
    * -----------------------    |
    * | fast clear colors0  |   \|/
    * -----------------------
    * |     surface1        |   /|\
    * -----------------------    |
    * |   shadow surface1   |    |
    * -----------------------    | Plane 1
    * |    aux surface1     |    |
    * -----------------------    |
    * | fast clear colors1  |   \|/
    * -----------------------
    * |        ...          |
    * |                     |
    * -----------------------
    */
   struct {
      /**
       * Offset of the entire plane (whenever the image is disjoint this is
       * set to 0).
       */
      uint32_t offset;

      VkDeviceSize size;
      uint32_t alignment;

      struct anv_surface surface;

      /**
       * A surface which shadows the main surface and may have different
       * tiling. This is used for sampling using a tiling that isn't supported
       * for other operations.
       */
      struct anv_surface shadow_surface;

      /**
       * The base aux usage for this image.  For color images, this can be
       * either CCS_E or CCS_D depending on whether or not we can reliably
       * leave CCS on all the time.
       */
      enum isl_aux_usage aux_usage;

      struct anv_surface aux_surface;

      /**
       * Offset of the fast clear state (used to compute the
       * fast_clear_state_offset of the following planes).
       */
      uint32_t fast_clear_state_offset;

      /**
       * BO associated with this plane, set when bound.
       */
      struct anv_address address;

      /**
       * When destroying the image, also free the bo.
       * */
      bool bo_is_owned;
   } planes[3];
};

/* The ordering of this enum is important */
enum anv_fast_clear_type {
   /** Image does not have/support any fast-clear blocks */
   ANV_FAST_CLEAR_NONE = 0,
   /** Image has/supports fast-clear but only to the default value */
   ANV_FAST_CLEAR_DEFAULT_VALUE = 1,
   /** Image has/supports fast-clear with an arbitrary fast-clear value */
   ANV_FAST_CLEAR_ANY = 2,
};

/* Returns the number of auxiliary buffer levels attached to an image. */
static inline uint8_t
anv_image_aux_levels(const struct anv_image * const image,
                     VkImageAspectFlagBits aspect)
{
   uint32_t plane = anv_image_aspect_to_plane(image->aspects, aspect);
   if (image->planes[plane].aux_usage == ISL_AUX_USAGE_NONE)
      return 0;

   return image->levels;
}

/* Returns the number of auxiliary buffer layers attached to an image. */
static inline uint32_t
anv_image_aux_layers(const struct anv_image * const image,
                     VkImageAspectFlagBits aspect,
                     const uint8_t miplevel)
{
   assert(image);

   /* The miplevel must exist in the main buffer. */
   assert(miplevel < image->levels);

   if (miplevel >= anv_image_aux_levels(image, aspect)) {
      /* There are no layers with auxiliary data because the miplevel has no
       * auxiliary data.
       */
      return 0;
   }

   return MAX2(image->array_size, image->extent.depth >> miplevel);
}

static inline struct anv_address
anv_image_get_clear_color_addr(UNUSED const struct anv_device *device,
                               const struct anv_image *image,
                               VkImageAspectFlagBits aspect)
{
   assert(image->aspects & VK_IMAGE_ASPECT_ANY_COLOR_BIT_ANV);

   uint32_t plane = anv_image_aspect_to_plane(image->aspects, aspect);
   return anv_address_add(image->planes[plane].address,
                          image->planes[plane].fast_clear_state_offset);
}

static inline struct anv_address
anv_image_get_fast_clear_type_addr(const struct anv_device *device,
                                   const struct anv_image *image,
                                   VkImageAspectFlagBits aspect)
{
   struct anv_address addr =
      anv_image_get_clear_color_addr(device, image, aspect);

   const unsigned clear_color_state_size = device->info.gen >= 10 ?
      device->isl_dev.ss.clear_color_state_size :
      device->isl_dev.ss.clear_value_size;
   return anv_address_add(addr, clear_color_state_size);
}

static inline struct anv_address
anv_image_get_compression_state_addr(const struct anv_device *device,
                                     const struct anv_image *image,
                                     VkImageAspectFlagBits aspect,
                                     uint32_t level, uint32_t array_layer)
{
   assert(level < anv_image_aux_levels(image, aspect));
   assert(array_layer < anv_image_aux_layers(image, aspect, level));
   UNUSED uint32_t plane = anv_image_aspect_to_plane(image->aspects, aspect);
   assert(image->planes[plane].aux_usage == ISL_AUX_USAGE_CCS_E);

   struct anv_address addr =
      anv_image_get_fast_clear_type_addr(device, image, aspect);
   addr.offset += 4; /* Go past the fast clear type */

   if (image->type == VK_IMAGE_TYPE_3D) {
      for (uint32_t l = 0; l < level; l++)
         addr.offset += anv_minify(image->extent.depth, l) * 4;
   } else {
      addr.offset += level * image->array_size * 4;
   }
   addr.offset += array_layer * 4;

   assert(addr.offset <
          image->planes[plane].address.offset + image->planes[plane].size);
   return addr;
}

/* Returns true if a HiZ-enabled depth buffer can be sampled from. */
static inline bool
anv_can_sample_with_hiz(const struct gen_device_info * const devinfo,
                        const struct anv_image *image)
{
   if (!(image->aspects & VK_IMAGE_ASPECT_DEPTH_BIT))
      return false;

   /* For Gen8-11, there are some restrictions around sampling from HiZ.
    * The Skylake PRM docs for RENDER_SURFACE_STATE::AuxiliarySurfaceMode
    * say:
    *
    *    "If this field is set to AUX_HIZ, Number of Multisamples must
    *    be MULTISAMPLECOUNT_1, and Surface Type cannot be SURFTYPE_3D."
    */
   if (image->type == VK_IMAGE_TYPE_3D)
      return false;

   /* Allow this feature on BDW even though it is disabled in the BDW devinfo
    * struct. There's documentation which suggests that this feature actually
    * reduces performance on BDW, but it has only been observed to help so
    * far. Sampling fast-cleared blocks on BDW must also be handled with care
    * (see depth_stencil_attachment_compute_aux_usage() for more info).
    */
   if (devinfo->gen != 8 && !devinfo->has_sample_with_hiz)
      return false;

   return image->samples == 1;
}

static inline bool
anv_image_plane_uses_aux_map(const struct anv_device *device,
                             const struct anv_image *image,
                             uint32_t plane)
{
   return device->info.has_aux_map &&
      isl_aux_usage_has_ccs(image->planes[plane].aux_usage);
}

void
anv_cmd_buffer_mark_image_written(struct anv_cmd_buffer *cmd_buffer,
                                  const struct anv_image *image,
                                  VkImageAspectFlagBits aspect,
                                  enum isl_aux_usage aux_usage,
                                  uint32_t level,
                                  uint32_t base_layer,
                                  uint32_t layer_count);

void
anv_image_clear_color(struct anv_cmd_buffer *cmd_buffer,
                      const struct anv_image *image,
                      VkImageAspectFlagBits aspect,
                      enum isl_aux_usage aux_usage,
                      enum isl_format format, struct isl_swizzle swizzle,
                      uint32_t level, uint32_t base_layer, uint32_t layer_count,
                      VkRect2D area, union isl_color_value clear_color);
void
anv_image_clear_depth_stencil(struct anv_cmd_buffer *cmd_buffer,
                              const struct anv_image *image,
                              VkImageAspectFlags aspects,
                              enum isl_aux_usage depth_aux_usage,
                              uint32_t level,
                              uint32_t base_layer, uint32_t layer_count,
                              VkRect2D area,
                              float depth_value, uint8_t stencil_value);
void
anv_image_msaa_resolve(struct anv_cmd_buffer *cmd_buffer,
                       const struct anv_image *src_image,
                       enum isl_aux_usage src_aux_usage,
                       uint32_t src_level, uint32_t src_base_layer,
                       const struct anv_image *dst_image,
                       enum isl_aux_usage dst_aux_usage,
                       uint32_t dst_level, uint32_t dst_base_layer,
                       VkImageAspectFlagBits aspect,
                       uint32_t src_x, uint32_t src_y,
                       uint32_t dst_x, uint32_t dst_y,
                       uint32_t width, uint32_t height,
                       uint32_t layer_count,
                       enum blorp_filter filter);
void
anv_image_hiz_op(struct anv_cmd_buffer *cmd_buffer,
                 const struct anv_image *image,
                 VkImageAspectFlagBits aspect, uint32_t level,
                 uint32_t base_layer, uint32_t layer_count,
                 enum isl_aux_op hiz_op);
void
anv_image_hiz_clear(struct anv_cmd_buffer *cmd_buffer,
                    const struct anv_image *image,
                    VkImageAspectFlags aspects,
                    uint32_t level,
                    uint32_t base_layer, uint32_t layer_count,
                    VkRect2D area, uint8_t stencil_value);
void
anv_image_mcs_op(struct anv_cmd_buffer *cmd_buffer,
                 const struct anv_image *image,
                 enum isl_format format, struct isl_swizzle swizzle,
                 VkImageAspectFlagBits aspect,
                 uint32_t base_layer, uint32_t layer_count,
                 enum isl_aux_op mcs_op, union isl_color_value *clear_value,
                 bool predicate);
void
anv_image_ccs_op(struct anv_cmd_buffer *cmd_buffer,
                 const struct anv_image *image,
                 enum isl_format format, struct isl_swizzle swizzle,
                 VkImageAspectFlagBits aspect, uint32_t level,
                 uint32_t base_layer, uint32_t layer_count,
                 enum isl_aux_op ccs_op, union isl_color_value *clear_value,
                 bool predicate);

void
anv_image_copy_to_shadow(struct anv_cmd_buffer *cmd_buffer,
                         const struct anv_image *image,
                         VkImageAspectFlagBits aspect,
                         uint32_t base_level, uint32_t level_count,
                         uint32_t base_layer, uint32_t layer_count);

enum isl_aux_state
anv_layout_to_aux_state(const struct gen_device_info * const devinfo,
                        const struct anv_image *image,
                        const VkImageAspectFlagBits aspect,
                        const VkImageLayout layout);

enum isl_aux_usage
anv_layout_to_aux_usage(const struct gen_device_info * const devinfo,
                        const struct anv_image *image,
                        const VkImageAspectFlagBits aspect,
                        const VkImageUsageFlagBits usage,
                        const VkImageLayout layout);

enum anv_fast_clear_type
anv_layout_to_fast_clear_type(const struct gen_device_info * const devinfo,
                              const struct anv_image * const image,
                              const VkImageAspectFlagBits aspect,
                              const VkImageLayout layout);

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

static inline VkImageAspectFlags
anv_image_expand_aspects(const struct anv_image *image,
                         VkImageAspectFlags aspects)
{
   /* If the underlying image has color plane aspects and
    * VK_IMAGE_ASPECT_COLOR_BIT has been requested, then return the aspects of
    * the underlying image. */
   if ((image->aspects & VK_IMAGE_ASPECT_PLANES_BITS_ANV) != 0 &&
       aspects == VK_IMAGE_ASPECT_COLOR_BIT)
      return image->aspects;

   return aspects;
}

static inline bool
anv_image_aspects_compatible(VkImageAspectFlags aspects1,
                             VkImageAspectFlags aspects2)
{
   if (aspects1 == aspects2)
      return true;

   /* Only 1 color aspects are compatibles. */
   if ((aspects1 & VK_IMAGE_ASPECT_ANY_COLOR_BIT_ANV) != 0 &&
       (aspects2 & VK_IMAGE_ASPECT_ANY_COLOR_BIT_ANV) != 0 &&
       util_bitcount(aspects1) == util_bitcount(aspects2))
      return true;

   return false;
}

struct anv_image_view {
   struct vk_object_base base;

   const struct anv_image *image; /**< VkImageViewCreateInfo::image */

   VkImageAspectFlags aspect_mask;
   VkFormat vk_format;
   VkExtent3D extent; /**< Extent of VkImageViewCreateInfo::baseMipLevel. */

   unsigned n_planes;
   struct {
      uint32_t image_plane;

      struct isl_view isl;

      /**
       * RENDER_SURFACE_STATE when using image as a sampler surface with an
       * image layout of SHADER_READ_ONLY_OPTIMAL or
       * DEPTH_STENCIL_READ_ONLY_OPTIMAL.
       */
      struct anv_surface_state optimal_sampler_surface_state;

      /**
       * RENDER_SURFACE_STATE when using image as a sampler surface with an
       * image layout of GENERAL.
       */
      struct anv_surface_state general_sampler_surface_state;

      /**
       * RENDER_SURFACE_STATE when using image as a storage image. Separate
       * states for write-only and readable, using the real format for
       * write-only and the lowered format for readable.
       */
      struct anv_surface_state storage_surface_state;
      struct anv_surface_state writeonly_storage_surface_state;

      struct brw_image_param storage_image_param;
   } planes[3];
};

enum anv_image_view_state_flags {
   ANV_IMAGE_VIEW_STATE_STORAGE_WRITE_ONLY   = (1 << 0),
   ANV_IMAGE_VIEW_STATE_TEXTURE_OPTIMAL      = (1 << 1),
};

void anv_image_fill_surface_state(struct anv_device *device,
                                  const struct anv_image *image,
                                  VkImageAspectFlagBits aspect,
                                  const struct isl_view *view,
                                  isl_surf_usage_flags_t view_usage,
                                  enum isl_aux_usage aux_usage,
                                  const union isl_color_value *clear_color,
                                  enum anv_image_view_state_flags flags,
                                  struct anv_surface_state *state_inout,
                                  struct brw_image_param *image_param_out);

struct anv_image_create_info {
   const VkImageCreateInfo *vk_info;

   /** An opt-in bitmask which filters an ISL-mapping of the Vulkan tiling. */
   isl_tiling_flags_t isl_tiling_flags;

   /** These flags will be added to any derived from VkImageCreateInfo. */
   isl_surf_usage_flags_t isl_extra_usage_flags;

   uint32_t stride;
   bool external_format;
};

VkResult anv_image_create(VkDevice _device,
                          const struct anv_image_create_info *info,
                          const VkAllocationCallbacks* alloc,
                          VkImage *pImage);

enum isl_format
anv_isl_format_for_descriptor_type(const struct anv_device *device,
                                   VkDescriptorType type);

static inline VkExtent3D
anv_sanitize_image_extent(const VkImageType imageType,
                          const VkExtent3D imageExtent)
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

static inline VkOffset3D
anv_sanitize_image_offset(const VkImageType imageType,
                          const VkOffset3D imageOffset)
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

VkFormatFeatureFlags
anv_get_image_format_features(const struct gen_device_info *devinfo,
                              VkFormat vk_format,
                              const struct anv_format *anv_format,
                              VkImageTiling vk_tiling);

void anv_fill_buffer_surface_state(struct anv_device *device,
                                   struct anv_state state,
                                   enum isl_format format,
                                   isl_surf_usage_flags_t usage,
                                   struct anv_address address,
                                   uint32_t range, uint32_t stride);

static inline void
anv_clear_color_from_att_state(union isl_color_value *clear_color,
                               const struct anv_attachment_state *att_state,
                               const struct anv_image_view *iview)
{
   const struct isl_format_layout *view_fmtl =
      isl_format_get_layout(iview->planes[0].isl.format);

#define COPY_CLEAR_COLOR_CHANNEL(c, i) \
   if (view_fmtl->channels.c.bits) \
      clear_color->u32[i] = att_state->clear_value.color.uint32[i]

   COPY_CLEAR_COLOR_CHANNEL(r, 0);
   COPY_CLEAR_COLOR_CHANNEL(g, 1);
   COPY_CLEAR_COLOR_CHANNEL(b, 2);
   COPY_CLEAR_COLOR_CHANNEL(a, 3);

#undef COPY_CLEAR_COLOR_CHANNEL
}


/* Haswell border color is a bit of a disaster.  Float and unorm formats use a
 * straightforward 32-bit float color in the first 64 bytes.  Instead of using
 * a nice float/integer union like Gen8+, Haswell specifies the integer border
 * color as a separate entry /after/ the float color.  The layout of this entry
 * also depends on the format's bpp (with extra hacks for RG32), and overlaps.
 *
 * Since we don't know the format/bpp, we can't make any of the border colors
 * containing '1' work for all formats, as it would be in the wrong place for
 * some of them.  We opt to make 32-bit integers work as this seems like the
 * most common option.  Fortunately, transparent black works regardless, as
 * all zeroes is the same in every bit-size.
 */
struct hsw_border_color {
   float float32[4];
   uint32_t _pad0[12];
   uint32_t uint32[4];
   uint32_t _pad1[108];
};

struct gen8_border_color {
   union {
      float float32[4];
      uint32_t uint32[4];
   };
   /* Pad out to 64 bytes */
   uint32_t _pad[12];
};

struct anv_ycbcr_conversion {
   struct vk_object_base base;

   const struct anv_format *        format;
   VkSamplerYcbcrModelConversion    ycbcr_model;
   VkSamplerYcbcrRange              ycbcr_range;
   VkComponentSwizzle               mapping[4];
   VkChromaLocation                 chroma_offsets[2];
   VkFilter                         chroma_filter;
   bool                             chroma_reconstruction;
};

struct anv_sampler {
   struct vk_object_base        base;

   uint32_t                     state[3][4];
   uint32_t                     n_planes;
   struct anv_ycbcr_conversion *conversion;

   /* Blob of sampler state data which is guaranteed to be 32-byte aligned
    * and with a 32-byte stride for use as bindless samplers.
    */
   struct anv_state             bindless_state;

   struct anv_state             custom_border_color;
};

struct anv_framebuffer {
   struct vk_object_base                        base;

   uint32_t                                     width;
   uint32_t                                     height;
   uint32_t                                     layers;

   uint32_t                                     attachment_count;
   struct anv_image_view *                      attachments[0];
};

struct anv_subpass_attachment {
   VkImageUsageFlagBits usage;
   uint32_t attachment;
   VkImageLayout layout;

   /* Used only with attachment containing stencil data. */
   VkImageLayout stencil_layout;
};

struct anv_subpass {
   uint32_t                                     attachment_count;

   /**
    * A pointer to all attachment references used in this subpass.
    * Only valid if ::attachment_count > 0.
    */
   struct anv_subpass_attachment *              attachments;
   uint32_t                                     input_count;
   struct anv_subpass_attachment *              input_attachments;
   uint32_t                                     color_count;
   struct anv_subpass_attachment *              color_attachments;
   struct anv_subpass_attachment *              resolve_attachments;

   struct anv_subpass_attachment *              depth_stencil_attachment;
   struct anv_subpass_attachment *              ds_resolve_attachment;
   VkResolveModeFlagBitsKHR                     depth_resolve_mode;
   VkResolveModeFlagBitsKHR                     stencil_resolve_mode;

   uint32_t                                     view_mask;

   /** Subpass has a depth/stencil self-dependency */
   bool                                         has_ds_self_dep;

   /** Subpass has at least one color resolve attachment */
   bool                                         has_color_resolve;
};

static inline unsigned
anv_subpass_view_count(const struct anv_subpass *subpass)
{
   return MAX2(1, util_bitcount(subpass->view_mask));
}

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
   VkImageLayout                                first_subpass_layout;

   VkImageLayout                                stencil_initial_layout;
   VkImageLayout                                stencil_final_layout;

   /* The subpass id in which the attachment will be used last. */
   uint32_t                                     last_subpass_idx;
};

struct anv_render_pass {
   struct vk_object_base                        base;

   uint32_t                                     attachment_count;
   uint32_t                                     subpass_count;
   /* An array of subpass_count+1 flushes, one per subpass boundary */
   enum anv_pipe_bits *                         subpass_flushes;
   struct anv_render_pass_attachment *          attachments;
   struct anv_subpass                           subpasses[0];
};

#define ANV_PIPELINE_STATISTICS_MASK 0x000007ff

#define OA_SNAPSHOT_SIZE (256)
#define ANV_KHR_PERF_QUERY_SIZE (ALIGN(sizeof(uint64_t), 64) + 2 * OA_SNAPSHOT_SIZE)

struct anv_query_pool {
   struct vk_object_base                        base;

   VkQueryType                                  type;
   VkQueryPipelineStatisticFlags                pipeline_statistics;
   /** Stride between slots, in bytes */
   uint32_t                                     stride;
   /** Number of slots in this query pool */
   uint32_t                                     slots;
   struct anv_bo *                              bo;

   /* Perf queries : */
   struct anv_bo                                reset_bo;
   uint32_t                                     n_counters;
   struct gen_perf_counter_pass                *counter_pass;
   uint32_t                                     n_passes;
   struct gen_perf_query_info                 **pass_query;
};

static inline uint32_t khr_perf_query_preamble_offset(struct anv_query_pool *pool,
                                                      uint32_t pass)
{
   return pass * ANV_KHR_PERF_QUERY_SIZE + 8;
}

int anv_get_instance_entrypoint_index(const char *name);
int anv_get_device_entrypoint_index(const char *name);
int anv_get_physical_device_entrypoint_index(const char *name);

const char *anv_get_instance_entry_name(int index);
const char *anv_get_physical_device_entry_name(int index);
const char *anv_get_device_entry_name(int index);

bool
anv_instance_entrypoint_is_enabled(int index, uint32_t core_version,
                                   const struct anv_instance_extension_table *instance);
bool
anv_physical_device_entrypoint_is_enabled(int index, uint32_t core_version,
                                          const struct anv_instance_extension_table *instance);
bool
anv_device_entrypoint_is_enabled(int index, uint32_t core_version,
                                 const struct anv_instance_extension_table *instance,
                                 const struct anv_device_extension_table *device);

void *anv_resolve_device_entrypoint(const struct gen_device_info *devinfo,
                                    uint32_t index);
void *anv_lookup_entrypoint(const struct gen_device_info *devinfo,
                            const char *name);

void anv_dump_image_to_ppm(struct anv_device *device,
                           struct anv_image *image, unsigned miplevel,
                           unsigned array_layer, VkImageAspectFlagBits aspect,
                           const char *filename);

enum anv_dump_action {
   ANV_DUMP_FRAMEBUFFERS_BIT = 0x1,
};

#ifdef DEBUG
PUBLIC
#endif
void anv_dump_start(struct anv_device *device, enum anv_dump_action actions);
#ifdef DEBUG
PUBLIC
#endif
void anv_dump_finish(void);

void anv_dump_add_attachments(struct anv_cmd_buffer *cmd_buffer);

static inline uint32_t
anv_get_subpass_id(const struct anv_cmd_state * const cmd_state)
{
   /* This function must be called from within a subpass. */
   assert(cmd_state->pass && cmd_state->subpass);

   const uint32_t subpass_id = cmd_state->subpass - cmd_state->pass->subpasses;

   /* The id of this subpass shouldn't exceed the number of subpasses in this
    * render pass minus 1.
    */
   assert(subpass_id < cmd_state->pass->subpass_count);
   return subpass_id;
}

struct anv_performance_configuration_intel {
   struct vk_object_base      base;

   struct gen_perf_registers *register_config;

   uint64_t                   config_id;
};

struct gen_perf_config *anv_get_perf(const struct gen_device_info *devinfo, int fd);
void anv_device_perf_init(struct anv_device *device);
void anv_perf_write_pass_results(struct gen_perf_config *perf,
                                 struct anv_query_pool *pool, uint32_t pass,
                                 const struct gen_perf_query_result *accumulated_results,
                                 union VkPerformanceCounterResultKHR *results);

#define ANV_FROM_HANDLE(__anv_type, __name, __handle) \
   VK_FROM_HANDLE(__anv_type, __name, __handle)

VK_DEFINE_HANDLE_CASTS(anv_cmd_buffer, base, VkCommandBuffer,
                       VK_OBJECT_TYPE_COMMAND_BUFFER)
VK_DEFINE_HANDLE_CASTS(anv_device, vk.base, VkDevice, VK_OBJECT_TYPE_DEVICE)
VK_DEFINE_HANDLE_CASTS(anv_instance, base, VkInstance, VK_OBJECT_TYPE_INSTANCE)
VK_DEFINE_HANDLE_CASTS(anv_physical_device, base, VkPhysicalDevice,
                       VK_OBJECT_TYPE_PHYSICAL_DEVICE)
VK_DEFINE_HANDLE_CASTS(anv_queue, base, VkQueue, VK_OBJECT_TYPE_QUEUE)

VK_DEFINE_NONDISP_HANDLE_CASTS(anv_cmd_pool, base, VkCommandPool,
                               VK_OBJECT_TYPE_COMMAND_POOL)
VK_DEFINE_NONDISP_HANDLE_CASTS(anv_buffer, base, VkBuffer,
                               VK_OBJECT_TYPE_BUFFER)
VK_DEFINE_NONDISP_HANDLE_CASTS(anv_buffer_view, base, VkBufferView,
                               VK_OBJECT_TYPE_BUFFER_VIEW)
VK_DEFINE_NONDISP_HANDLE_CASTS(anv_descriptor_pool, base, VkDescriptorPool,
                               VK_OBJECT_TYPE_DESCRIPTOR_POOL)
VK_DEFINE_NONDISP_HANDLE_CASTS(anv_descriptor_set, base, VkDescriptorSet,
                               VK_OBJECT_TYPE_DESCRIPTOR_SET)
VK_DEFINE_NONDISP_HANDLE_CASTS(anv_descriptor_set_layout, base,
                               VkDescriptorSetLayout,
                               VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT)
VK_DEFINE_NONDISP_HANDLE_CASTS(anv_descriptor_update_template, base,
                               VkDescriptorUpdateTemplate,
                               VK_OBJECT_TYPE_DESCRIPTOR_UPDATE_TEMPLATE)
VK_DEFINE_NONDISP_HANDLE_CASTS(anv_device_memory, base, VkDeviceMemory,
                               VK_OBJECT_TYPE_DEVICE_MEMORY)
VK_DEFINE_NONDISP_HANDLE_CASTS(anv_fence, base, VkFence, VK_OBJECT_TYPE_FENCE)
VK_DEFINE_NONDISP_HANDLE_CASTS(anv_event, base, VkEvent, VK_OBJECT_TYPE_EVENT)
VK_DEFINE_NONDISP_HANDLE_CASTS(anv_framebuffer, base, VkFramebuffer,
                               VK_OBJECT_TYPE_FRAMEBUFFER)
VK_DEFINE_NONDISP_HANDLE_CASTS(anv_image, base, VkImage, VK_OBJECT_TYPE_IMAGE)
VK_DEFINE_NONDISP_HANDLE_CASTS(anv_image_view, base, VkImageView,
                               VK_OBJECT_TYPE_IMAGE_VIEW);
VK_DEFINE_NONDISP_HANDLE_CASTS(anv_pipeline_cache, base, VkPipelineCache,
                               VK_OBJECT_TYPE_PIPELINE_CACHE)
VK_DEFINE_NONDISP_HANDLE_CASTS(anv_pipeline, base, VkPipeline,
                               VK_OBJECT_TYPE_PIPELINE)
VK_DEFINE_NONDISP_HANDLE_CASTS(anv_pipeline_layout, base, VkPipelineLayout,
                               VK_OBJECT_TYPE_PIPELINE_LAYOUT)
VK_DEFINE_NONDISP_HANDLE_CASTS(anv_query_pool, base, VkQueryPool,
                               VK_OBJECT_TYPE_QUERY_POOL)
VK_DEFINE_NONDISP_HANDLE_CASTS(anv_render_pass, base, VkRenderPass,
                               VK_OBJECT_TYPE_RENDER_PASS)
VK_DEFINE_NONDISP_HANDLE_CASTS(anv_sampler, base, VkSampler,
                               VK_OBJECT_TYPE_SAMPLER)
VK_DEFINE_NONDISP_HANDLE_CASTS(anv_semaphore, base, VkSemaphore,
                               VK_OBJECT_TYPE_SEMAPHORE)
VK_DEFINE_NONDISP_HANDLE_CASTS(anv_shader_module, base, VkShaderModule,
                               VK_OBJECT_TYPE_SHADER_MODULE)
VK_DEFINE_NONDISP_HANDLE_CASTS(anv_ycbcr_conversion, base,
                               VkSamplerYcbcrConversion,
                               VK_OBJECT_TYPE_SAMPLER_YCBCR_CONVERSION)
VK_DEFINE_NONDISP_HANDLE_CASTS(anv_performance_configuration_intel, base,
                               VkPerformanceConfigurationINTEL,
                               VK_OBJECT_TYPE_PERFORMANCE_CONFIGURATION_INTEL)

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
#  define genX(x) gen11_##x
#  include "anv_genX.h"
#  undef genX
#  define genX(x) gen12_##x
#  include "anv_genX.h"
#  undef genX
#endif

#endif /* ANV_PRIVATE_H */
