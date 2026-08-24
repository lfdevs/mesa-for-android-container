/*
 * Copyright © 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <libgen.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "util/ralloc.h"
#include "util/u_math.h"

#include <xf86drm.h>
#include "drm-uapi/i915_drm.h"
#include "drm-uapi/xe_drm.h"

#include "intel/compiler/gen/gen.h"
#include "intel/compiler/gen/gen_names.h"
#include "intel/common/intel_common.h"
#include "intel/common/intel_compute_slm.h"
#include "intel/common/intel_gem.h"
#include "intel/common/xe/intel_engine.h"
#include "intel/decoder/intel_decoder.h"
#include "intel/dev/intel_debug.h"

#include "executor.h"
#include "executor_manual.h"

enum {
   /* Predictable base addresses here make it easier to spot errors. */
   EXECUTOR_BO_BATCH_ADDR = 0x10000000,
   EXECUTOR_BO_EXTRA_ADDR = 0x20000000,
   EXECUTOR_BO_DATA_ADDR  = 0x30000000,
   EXECUTOR_BO_PERF_ADDR  = 0x40000000,

   /* Apply to all BOs. */
   EXECUTOR_BO_SIZE = 10 * 1024 * 1024,
};

const char usage_line[] =
   "usage: executor [-d DEVICE] FILENAME [ARGS...]\n"
   "       executor [-d DEVICE] --oa OA [--oa-csv FILE] FILENAME [ARGS...]\n"
   "       executor [-d DEVICE] --oa list";

static void
open_manual()
{
   FILE *f = NULL;

   /* This fd will be set as stdin for executing man. */
   int fd = memfd_create("executor.1", 0);
   if (fd != -1)
      f = fdopen(fd, "w");

   if (!f) {
      /* Fallback to just printing the content out. */
      f = stderr;
   }

   fputs(executor_manual, f);


   fflush(f);

   if (f != stderr) {
      /* Inject the temporary as stdin for man. */
      lseek(fd, 0, SEEK_SET);
      dup2(fd, STDIN_FILENO);
      fclose(f);

      execlp("man", "man", "-l", "-", (char *)NULL);
   } else {
      exit(0);
   }
}

static void
print_help()
{
   printf(
      "%s\n"
      "\n"
      "SCRIPTING ENVIRONMENT:\n"
      "- alloc(SIZE_DWORDS|TABLE[, NAME|{name=STR, align=POWER_OF_TWO_BYTES, fill=VALUE}]) -> mem\n"
      "- surface_buffer(MEM, {format=STR}), surface_2d(MEM, opts), sampler(opts?), sampler_desc{...}\n"
      "- execute(SRC|{src=SRC, thread_groups=N})\n"
      "- mem:fill(VALUE), mem:set(TABLE, offset?), mem:read(COUNT, offset?), mem:to_table()\n"
      "- mem:dump(COUNT, offset?), mem:offset(), mem:addr(), mem:name(), mem[IDX], #mem\n"
      "- surface:bti(), sampler:index()\n"
      "- arg (table with command line arguments)\n"
      "- dump(ARRAY|MEM, COUNT)\n"
      "- devinfo = { ver, verx10, has_dpas, has_bfloat16, max_slm_size }\n"
      "\n"
      "ASSEMBLY MACROS:\n"
      "- @eot, @syncnop, @barrier\n"
      "- @mov REG IMM\n"
      "- @id REG, @tg REG, @globalid REG\n"
      "- @addr DST_REG MEM [DWORD_INDEX|REG]\n"
      "- @load DST_REG ADDR_REG\n"
      "- @store ADDR_REG SRC_REG\n"
      "- @param hw_threads N, @param simd 8|16|32, @param slm_size BYTES, @param autoswsb\n"
      "\n"
      "PERFORMANCE COUNTERS:\n"
      "- --oa PROFILE[:COUNTER1,COUNTER2]\n"
      "- --oa COUNTER1[,COUNTER2]\n"
      "- --oa PROFILE[:COUNTER1,COUNTER2] --oa-csv FILE\n"
      "- --oa list\n"
      "\n"
      "Use 'executor -d list' to list available devices.\n"
      "For more details, use 'executor --help' to open manual.\n",
      usage_line);
}

static struct {
   struct intel_device_info devinfo;
   struct isl_device isl_dev;
   int fd;

   const char *oa_csv_path;
   const char *oa_spec;
   const char *oa_metric_name;
   const char **oa_counter_names;
   int n_oa_counter_names;
   int *oa_counter_indices;
   bool oa_spec_has_colon;
   bool oa_list;
   struct intel_perf_config *perf_cfg;
   int perf_query_index;
   uint32_t perf_execute_count;
   FILE *oa_csv_file;
   char *oa_csv_mem;
   size_t oa_csv_mem_size;

   executor_context ec;
} E;

#define EXECUTOR_MEM_MT "executor.mem"
#define EXECUTOR_SURFACE_MT "executor.surface"
#define EXECUTOR_SAMPLER_MT "executor.sampler"

typedef uint32_t executor_mem_userdata;
typedef uint32_t executor_surface_userdata;
typedef uint32_t executor_sampler_userdata;

#define genX_call(func, ...)                                \
   switch (E.devinfo.verx10) {                              \
   case 90:  gfx9_  ##func(__VA_ARGS__); break;             \
   case 110: gfx11_ ##func(__VA_ARGS__); break;             \
   case 120: gfx12_ ##func(__VA_ARGS__); break;             \
   case 125: gfx125_##func(__VA_ARGS__); break;             \
   case 200: gfx20_ ##func(__VA_ARGS__); break;             \
   case 300: gfx30_ ##func(__VA_ARGS__); break;             \
   case 350: gfx35_ ##func(__VA_ARGS__); break;             \
   default: UNREACHABLE("Unsupported hardware generation"); \
   }

static void
executor_create_bo(executor_context *ec, executor_bo *bo, uint64_t addr, uint32_t size_in_bytes)
{
   if (ec->devinfo->kmd_type == INTEL_KMD_TYPE_I915) {
      struct drm_i915_gem_create gem_create = {
         .size = size_in_bytes,
      };

      int err = intel_ioctl(ec->fd, DRM_IOCTL_I915_GEM_CREATE, &gem_create);
      if (err)
         failf("i915_gem_create");

      struct drm_i915_gem_mmap_offset mm = {
         .handle = gem_create.handle,
         .flags  = ec->devinfo->has_local_mem ? I915_MMAP_OFFSET_FIXED
                                              : I915_MMAP_OFFSET_WC,
      };

      err = intel_ioctl(ec->fd, DRM_IOCTL_I915_GEM_MMAP_OFFSET, &mm);
      if (err)
         failf("i915_gem_mmap_offset");

      bo->handle = gem_create.handle;
      bo->map    = mmap(NULL, size_in_bytes, PROT_READ | PROT_WRITE,
                        MAP_SHARED, ec->fd, mm.offset);
      if (!bo->map)
         failf("mmap");
   } else {
      assert(ec->devinfo->kmd_type == INTEL_KMD_TYPE_XE);

      struct drm_xe_gem_create gem_create = {
         .size        = size_in_bytes,
         .cpu_caching = DRM_XE_GEM_CPU_CACHING_WB,
         .placement   = 1u << ec->devinfo->mem.sram.mem.instance,
      };

      int err = intel_ioctl(ec->fd, DRM_IOCTL_XE_GEM_CREATE, &gem_create);
      if (err)
         failf("xe_gem_create");

      struct drm_xe_gem_mmap_offset mm = {
         .handle = gem_create.handle,
      };

      err = intel_ioctl(ec->fd, DRM_IOCTL_XE_GEM_MMAP_OFFSET, &mm);
      if (err)
         failf("xe_gem_mmap_offset");

      bo->handle = gem_create.handle;
      bo->map    = mmap(NULL, size_in_bytes, PROT_READ | PROT_WRITE,
                        MAP_SHARED, ec->fd, mm.offset);
      if (!bo->map)
         failf("mmap");
   }

   bo->size   = size_in_bytes;
   bo->addr   = addr;
   bo->cursor = bo->map;

   assert(bo->addr % 4096 == 0);
   assert((uintptr_t)bo->map % 4096 == 0);
}

static void
executor_destroy_bo(executor_context *ec, executor_bo *bo)
{
   struct drm_gem_close gem_close = {
      .handle = bo->handle,
   };

   int err = munmap(bo->map, bo->size);
   if (err)
      failf("munmap");

   err = intel_ioctl(ec->fd, DRM_IOCTL_GEM_CLOSE, &gem_close);
   if (err)
      failf("gem_close");

   memset(bo, 0, sizeof(*bo));
}

static void
executor_print_bo(executor_bo *bo, const char *name)
{
   assert((bo->cursor - bo->map) % 4 == 0);
   uint32_t *dw = bo->map;
   uint32_t len = (uint32_t *)bo->cursor - dw;

   printf("=== %s (0x%08"PRIx64", %td bytes) ===\n", name, bo->addr, bo->cursor - bo->map);

   for (int i = 0; i < len; i++) {
      if ((i % 8) == 0) printf("[0x%08x] ", (i*4) + (uint32_t)bo->addr);
      printf("0x%08x ", dw[i]);
      if ((i % 8) == 7) printf("\n");
   }
   printf("\n");
}

void *
executor_alloc_bytes(executor_bo *bo, uint32_t size)
{
   return executor_alloc_bytes_aligned(bo, size, 0);
}

void *
executor_alloc_bytes_aligned(executor_bo *bo, uint32_t size, uint32_t alignment)
{
   uint64_t offset = (uintptr_t)bo->cursor - (uintptr_t)bo->map;
   if (alignment) {
      uint64_t gpu_addr = bo->addr + offset;
      uint64_t aligned_gpu_addr = align64(gpu_addr, alignment);
      offset = aligned_gpu_addr - bo->addr;
   }

   if (offset > bo->size || size > bo->size - offset)
      failf("executor BO overflow");

   void *ptr = (char *)bo->map + offset;
   bo->cursor = (char *)ptr + size;
   return ptr;
}

static bool
executor_mem_name_is_valid(const char *name)
{
   if (!name || !name[0])
      return false;

   if (!(isalpha((unsigned char)name[0]) || name[0] == '_'))
      return false;

   for (const char *c = name + 1; *c; c++) {
      if (!(isalnum((unsigned char)*c) || *c == '_'))
         return false;
   }

   return true;
}

executor_mem_region *
executor_find_mem_region(executor_context *ec, const char *key)
{
   util_dynarray_foreach(&ec->mem_regions, executor_mem_region, region) {
      if (region->name && !strcmp(region->name, key))
         return region;
   }

   return NULL;
}

static executor_mem_region *
executor_get_mem_region(executor_context *ec, uint32_t idx)
{
   if (idx >= util_dynarray_num_elements(&ec->mem_regions,
                                         executor_mem_region))
      failf("invalid memory object %u", idx);

   return util_dynarray_element(&ec->mem_regions, executor_mem_region, idx);
}

static uint32_t
register_surface_binding(executor_context *ec,
                         executor_mem_region region,
                         executor_surface_type type,
                         enum isl_format format,
                         uint32_t stride,
                         uint32_t width, uint32_t height)
{
   if (ec->num_surface_bindings >= 31)
      failf("too many surfaces; executor supports up to 31 BTI entries");

   if (ec->num_surface_bindings == ec->surface_binding_capacity) {
      uint32_t new_capacity = ec->surface_binding_capacity ?
         ec->surface_binding_capacity * 2 : 8;
      executor_surface_binding *new_bindings =
         realloc(ec->surface_bindings, new_capacity * sizeof(*new_bindings));
      if (!new_bindings)
         failf("failed to allocate surface binding table");
      ec->surface_bindings = new_bindings;
      ec->surface_binding_capacity = new_capacity;
   }

   const uint32_t bti = ec->num_surface_bindings;
   ec->surface_bindings[ec->num_surface_bindings++] =
      (executor_surface_binding) {
         .region = region,
         .type = type,
         .format = format,
         .stride = stride,
         .width = width,
         .height = height,
         .bti = bti,
      };

   return bti;
}

static uint32_t
register_sampler_binding(executor_context *ec,
                         const executor_sampler_binding *state)
{
   if (ec->num_sampler_bindings >= 16)
      failf("too many samplers; executor supports up to 16 sampler entries");

   if (ec->num_sampler_bindings == ec->sampler_binding_capacity) {
      uint32_t new_capacity = ec->sampler_binding_capacity ?
         ec->sampler_binding_capacity * 2 : 8;
      executor_sampler_binding *new_bindings =
         realloc(ec->sampler_bindings, new_capacity * sizeof(*new_bindings));
      if (!new_bindings)
         failf("failed to allocate sampler binding table");
      ec->sampler_bindings = new_bindings;
      ec->sampler_binding_capacity = new_capacity;
   }

   const uint32_t index = ec->num_sampler_bindings;
   ec->sampler_bindings[ec->num_sampler_bindings] = *state;
   ec->sampler_bindings[ec->num_sampler_bindings].index = index;
   ec->num_sampler_bindings++;

   return index;
}

static uint32_t
executor_data_alloc(executor_context *ec, uint32_t size_dw, uint32_t alignment,
                    const char *name)
{
   if (size_dw > UINT32_MAX / 4)
      failf("data allocation too large");

   uint32_t size_bytes = size_dw * 4;

   if (alignment) {
      if (!util_is_power_of_two_nonzero(alignment))
         failf("allocation alignment must be a power of two");
      if (alignment < 4)
         alignment = 4;
   }

   void *ptr = executor_alloc_bytes_aligned(&ec->bo.data, size_bytes, alignment);
   executor_mem_region region = {
      .bo = &ec->bo.data,
      .offset = (uint32_t)((uintptr_t)ptr - (uintptr_t)ec->bo.data.map),
      .size = size_bytes,
      .map = ptr,
      .name = NULL,
   };

   uint64_t addr = ec->bo.data.addr + (uint64_t)region.offset;
   if (region.size > 0 && addr + region.size - 1 > UINT32_MAX)
      failf("data allocation exceeds 32-bit GPU address space required for a32 messages");

   if (name) {
      if (!executor_mem_name_is_valid(name))
         failf("invalid memory name '%s'", name);
      if (executor_find_mem_region(ec, name))
         failf("memory name '%s' already exists", name);

      region.name = ralloc_strdup(ec->mem_ctx, name);
      if (!region.name)
         failf("failed to allocate memory name");
   } else {
      while (true) {
         char generated[32];
         snprintf(generated, sizeof(generated), "buf%u", ec->next_mem_name_id++);
         if (executor_find_mem_region(ec, generated))
            continue;

         region.name = ralloc_strdup(ec->mem_ctx, generated);
         if (!region.name)
            failf("failed to allocate memory name");
         break;
      }
   }

   uint32_t idx = util_dynarray_num_elements(&ec->mem_regions,
                                             executor_mem_region);
   executor_mem_region *registered =
      util_dynarray_grow(&ec->mem_regions, executor_mem_region, 1);
   if (!registered)
      failf("failed to allocate memory region table");

   *registered = region;
   return idx;
}

executor_address
executor_address_of_ptr(executor_bo *bo, void *ptr)
{
   return (executor_address){ptr - bo->map + bo->addr};
}

static void *
executor_perf_bo_alloc(void *bufmgr, const char *name, uint64_t size)
{
   executor_context *ec = bufmgr;

   if (size > ec->bo.perf.size)
      failf("perf query BO allocation too large");

   return &ec->bo.perf;
}

/* intel_perf vtbl: executor has no live batch object to inspect and owns the
 * single real perf BO, so several callbacks are trivial adapters or no-ops.
 */
static void
executor_perf_bo_unreference(void *bo)
{
   /* Perf BO is destroyed with executor_context. */
}

static void *
executor_perf_bo_map(void *ctx, void *bo, unsigned flags)
{
   return ((executor_bo *)bo)->map;
}

static void
executor_perf_bo_unmap(void *bo)
{
   /* Perf BO slices are persistently mapped as part of ec->bo.perf. */
}

static bool
executor_perf_batch_references(void *batch, void *bo)
{
   /* Executor has no live batch object for intel_perf to inspect. */
   return false;
}

static void
executor_perf_bo_wait_rendering(void *bo)
{
   /* executor_context_dispatch() already waits for batch completion. */
}

static int
executor_perf_bo_busy(void *bo)
{
   /* Queries are only read after executor_context_dispatch() has waited. */
   return 0;
}

static void
executor_perf_emit_stall_at_pixel_scoreboard(void *ctx)
{
   executor_context *ec = ctx;
   genX_call(emit_perf_stall, ec);
}

static void
executor_perf_emit_mi_report_perf_count(void *ctx, void *bo,
                                        uint32_t offset_in_bytes,
                                        uint32_t report_id)
{
   executor_context *ec = ctx;
   genX_call(emit_mi_report_perf_count, ec, bo, offset_in_bytes, report_id);
}

static void
executor_perf_batchbuffer_flush(void *ctx, const char *file, int line)
{
   /* Unused because executor_perf_batch_references() always returns false. */
}

static void
executor_perf_store_register_mem(void *ctx, void *bo, uint32_t reg,
                                 uint32_t reg_size, uint32_t offset)
{
   executor_context *ec = ctx;
   genX_call(store_register_mem, ec, bo, reg, reg_size, offset);
}

static const __typeof__(((struct intel_perf_config *)0)->vtbl)
executor_perf_vtbl = {
   .bo_alloc = executor_perf_bo_alloc,
   .bo_unreference = executor_perf_bo_unreference,
   .bo_map = executor_perf_bo_map,
   .bo_unmap = executor_perf_bo_unmap,
   .batch_references = executor_perf_batch_references,
   .bo_wait_rendering = executor_perf_bo_wait_rendering,
   .bo_busy = executor_perf_bo_busy,
   .emit_stall_at_pixel_scoreboard =
      executor_perf_emit_stall_at_pixel_scoreboard,
   .emit_mi_report_perf_count = executor_perf_emit_mi_report_perf_count,
   .batchbuffer_flush = executor_perf_batchbuffer_flush,
   .store_register_mem = executor_perf_store_register_mem,
};

static bool
executor_perf_query_name_matches(const struct intel_perf_query_info *query,
                                 const char *name)
{
   return (query->symbol_name && !strcmp(query->symbol_name, name)) ||
          (query->name && !strcmp(query->name, name));
}

static bool
executor_perf_counter_name_matches(const struct intel_perf_query_counter *counter,
                                   const char *name)
{
   return (counter->symbol_name && !strcmp(counter->symbol_name, name)) ||
          (counter->name && !strcmp(counter->name, name));
}

static void
executor_perf_print_query_name(FILE *f, const char *prefix,
                               const struct intel_perf_query_info *query)
{
   fprintf(f, "%s%s%s%s\n", prefix,
           query->symbol_name ? query->symbol_name : query->name,
           query->symbol_name && query->name ? " - " : "",
           query->symbol_name && query->name ? query->name : "");
}

static void
executor_perf_print_available_queries(FILE *f)
{
   fprintf(f, "Available OA metric sets:\n");
   for (int i = 0; i < E.perf_cfg->n_queries; i++) {
      const struct intel_perf_query_info *query = &E.perf_cfg->queries[i];
      if (query->kind == INTEL_PERF_QUERY_TYPE_OA)
         executor_perf_print_query_name(f, "  ", query);
   }
}

static int
executor_perf_count_selected_counters(const struct intel_perf_query_info *query)
{
   return E.n_oa_counter_names > 0 ? E.n_oa_counter_names : query->n_counters;
}

static void
executor_perf_validate_counters(void *mem_ctx,
                                const struct intel_perf_query_info *query)
{
   E.oa_counter_indices =
      ralloc_array(mem_ctx, int, E.n_oa_counter_names);
   if (E.n_oa_counter_names > 0 && !E.oa_counter_indices)
      failf("failed to allocate OA counter index map");

   for (int i = 0; i < E.n_oa_counter_names; i++) {
      int idx = -1;
      for (int j = 0; j < query->n_counters; j++) {
         if (executor_perf_counter_name_matches(&query->counters[j],
                                                E.oa_counter_names[i])) {
            idx = j;
            break;
         }
      }

      if (idx < 0)
         failf("OA counter '%s' not found in metric set '%s'",
               E.oa_counter_names[i],
               query->symbol_name ? query->symbol_name : query->name);

      E.oa_counter_indices[i] = idx;
   }

   if (executor_perf_count_selected_counters(query) == 0)
      failf("OA metric set '%s' has no selected counters",
            query->symbol_name ? query->symbol_name : query->name);
}

static int
executor_find_named_perf_query(const char *metric_name)
{
   for (int i = 0; i < E.perf_cfg->n_queries; i++) {
      const struct intel_perf_query_info *query = &E.perf_cfg->queries[i];
      if (query->kind == INTEL_PERF_QUERY_TYPE_OA &&
          executor_perf_query_name_matches(query, metric_name))
         return i;
   }

   return -1;
}

static const char *
executor_default_perf_query_name(void)
{
   return "ComputeBasic";
}

static int
executor_find_perf_query(const char *metric_name)
{
   int query_index = executor_find_named_perf_query(metric_name);
   if (query_index >= 0)
      return query_index;

   executor_perf_print_available_queries(stderr);
   failf("OA metric set '%s' not found", metric_name);
   return -1;
}

static void
executor_add_oa_counter(void *mem_ctx, const char *counter)
{
   E.oa_counter_names = reralloc(mem_ctx, E.oa_counter_names,
                                 const char *, E.n_oa_counter_names + 1);
   if (!E.oa_counter_names)
      failf("failed to allocate OA counter filter");
   E.oa_counter_names[E.n_oa_counter_names++] = counter;
}

static void
executor_parse_oa_counter_list(void *mem_ctx, const char *counter_list,
                               const char *spec)
{
   char *counters = ralloc_strdup(mem_ctx, counter_list);
   while (counters && counters[0]) {
      char *counter = counters;
      char *comma = strchr(counters, ',');
      if (comma) {
         *comma = '\0';
         counters = comma + 1;
      } else {
         counters = NULL;
      }

      if (!counter[0])
         failf("empty OA counter name in '%s'", spec);

      executor_add_oa_counter(mem_ctx, counter);
   }
}

static void
executor_parse_oa_spec(void *mem_ctx, const char *spec)
{
   if (!spec)
      return;

   char *oa = ralloc_strdup(mem_ctx, spec);
   char *counter_list = strchr(oa, ':');
   if (counter_list) {
      E.oa_spec_has_colon = true;
      *counter_list++ = '\0';
      E.oa_metric_name = oa[0] ? oa : executor_default_perf_query_name();
      executor_parse_oa_counter_list(mem_ctx, counter_list, spec);
   } else if (oa[0]) {
      E.oa_metric_name = oa;
   } else {
      failf("missing OA metric set name in '%s'", spec);
   }
}

static void
executor_perf_list_query(const struct intel_perf_query_info *query)
{
   executor_perf_print_query_name(stdout, "", query);
   if (query->guid)
      printf("  guid: %s\n", query->guid);
   printf("  counters:\n");

   for (int i = 0; i < query->n_counters; i++) {
      const struct intel_perf_query_counter *counter = &query->counters[i];
      printf("    %s%s%s [%s, %s, %s]",
             counter->symbol_name ? counter->symbol_name : counter->name,
             counter->symbol_name && counter->name ? " - " : "",
             counter->symbol_name && counter->name ? counter->name : "",
             intel_perf_counter_type_name(counter->type),
             intel_perf_counter_data_type_name(counter->data_type),
             intel_perf_counter_units_name(counter->units));
      if (counter->category)
         printf(" category=%s", counter->category);
      printf("\n");
      if (counter->desc)
         printf("      %s\n", counter->desc);
   }
}

static void
executor_perf_list(void)
{
   if (!E.perf_cfg->n_queries)
      printf("no OA metrics available\n");

   for (int i = 0; i < E.perf_cfg->n_queries; i++) {
      const struct intel_perf_query_info *query = &E.perf_cfg->queries[i];
      if (query->kind == INTEL_PERF_QUERY_TYPE_OA)
         executor_perf_list_query(query);
   }
}

static void
executor_perf_create_query(executor_context *ec)
{
   if (!ec->perf_enabled)
      return;

   ec->perf_query.ctx = intel_perf_new_context(ec->mem_ctx);
   if (!ec->perf_query.ctx)
      failf("failed to allocate Intel perf context");

   const uint32_t hw_ctx = ec->devinfo->kmd_type == INTEL_KMD_TYPE_I915 ?
      ec->i915.ctx_id : ec->xe.queue_id;
   intel_perf_init_context(ec->perf_query.ctx, E.perf_cfg,
                           ec->perf_query.ctx,
                           ec, ec, ec->devinfo, hw_ctx, ec->fd);

   ec->perf_query.obj = intel_perf_new_query(ec->perf_query.ctx, E.perf_query_index);
   if (!ec->perf_query.obj)
      failf("failed to create OA performance query");
}

static void
executor_perf_print_counter_value(FILE *f,
                                  const struct intel_perf_query_counter *counter,
                                  const uint8_t *data)
{
   const uint8_t *p = data + counter->offset;

   switch (counter->data_type) {
   case INTEL_PERF_COUNTER_DATA_TYPE_BOOL32:
   case INTEL_PERF_COUNTER_DATA_TYPE_UINT32: {
      uint32_t value;
      assert((counter->offset & 3) == 0);
      memcpy(&value, p, sizeof(value));
      fprintf(f, "%"PRIu32, value);
      break;
   }
   case INTEL_PERF_COUNTER_DATA_TYPE_UINT64: {
      uint64_t value;
      assert((counter->offset & 7) == 0);
      memcpy(&value, p, sizeof(value));
      fprintf(f, "%"PRIu64, value);
      break;
   }
   case INTEL_PERF_COUNTER_DATA_TYPE_FLOAT: {
      float value;
      assert((counter->offset & 3) == 0);
      memcpy(&value, p, sizeof(value));
      fprintf(f, "%.9g", value);
      break;
   }
   case INTEL_PERF_COUNTER_DATA_TYPE_DOUBLE: {
      double value;
      assert((counter->offset & 7) == 0);
      memcpy(&value, p, sizeof(value));
      fprintf(f, "%.17g", value);
      break;
   }
   default:
      failf("unhandled OA counter data type %d", counter->data_type);
   }
}

static void
executor_perf_finish_query(executor_context *ec)
{
   if (!ec->perf_query.obj)
      return;

   const struct intel_perf_query_info *query =
      &E.perf_cfg->queries[E.perf_query_index];
   const bool write_header = E.perf_execute_count++ == 0;
   if (!E.oa_csv_file) {
      if (E.oa_csv_path) {
         E.oa_csv_file = fopen(E.oa_csv_path, "w");
         if (!E.oa_csv_file)
            failf("failed to open '%s' for writing", E.oa_csv_path);
      } else {
         E.oa_csv_file = open_memstream(&E.oa_csv_mem, &E.oa_csv_mem_size);
         if (!E.oa_csv_file)
            failf("failed to open memory stream for OA CSV output");
      }
   }

   /* When --oa specifies counters, columns follow the user-supplied order;
    * otherwise they follow the profile-defined order.
    */
   const int n_cols = executor_perf_count_selected_counters(query);

   if (write_header) {
      for (int i = 0; i < n_cols; i++) {
         const int idx = E.n_oa_counter_names > 0 ? E.oa_counter_indices[i] : i;
         const struct intel_perf_query_counter *counter = &query->counters[idx];
         if (i > 0)
            putc(',', E.oa_csv_file);
         fprintf(E.oa_csv_file, "%s",
                 counter->symbol_name ? counter->symbol_name : counter->name);
      }
      putc('\n', E.oa_csv_file);
   }

   uint8_t *data = calloc(1, query->data_size);
   if (!data)
      failf("failed to allocate OA query result buffer");

   intel_perf_wait_query(ec->perf_query.ctx, ec->perf_query.obj, NULL);
   intel_perf_get_query_data(ec->perf_query.ctx, ec->perf_query.obj, NULL,
                             query->data_size, (unsigned *)data, NULL);

   for (int i = 0; i < n_cols; i++) {
      const int idx = E.n_oa_counter_names > 0 ? E.oa_counter_indices[i] : i;
      const struct intel_perf_query_counter *counter = &query->counters[idx];
      if (i > 0)
         putc(',', E.oa_csv_file);
      executor_perf_print_counter_value(E.oa_csv_file, counter, data);
   }
   putc('\n', E.oa_csv_file);
   fflush(E.oa_csv_file);

   free(data);

   intel_perf_delete_query(ec->perf_query.ctx, ec->perf_query.obj);
   ec->perf_query.obj = NULL;
   intel_perf_free_context(ec->perf_query.ctx);
   ec->perf_query.ctx = NULL;
}

static bool
open_intel_render_device(drmDevicePtr dev,
                         struct intel_device_info *devinfo,
                         int *fd)
{
   if (!(dev->available_nodes & 1 << DRM_NODE_RENDER) ||
       dev->bustype != DRM_BUS_PCI ||
       dev->deviceinfo.pci->vendor_id != 0x8086)
      return false;

   *fd = open(dev->nodes[DRM_NODE_RENDER], O_RDWR | O_CLOEXEC);
   if (*fd < 0)
      return false;

   if (!intel_get_device_info_from_fd(*fd, devinfo, -1, -1) ||
       devinfo->ver < 8) {
      close(*fd);
      *fd = -1;
      return false;
   }

   return true;
}

static void
print_drm_devices()
{
   drmDevicePtr devices[8];
   int num_devices = drmGetDevices2(0, devices, ARRAY_SIZE(devices));

   if (num_devices < 1) {
      printf("No devices found.\n");
      return;
   }

   for (int i = 0; i < num_devices; i++) {
      struct intel_device_info devinfo = {};
      int fd = -1;

      if (open_intel_render_device(devices[i], &devinfo, &fd)) {
         printf("%d: %s\n", i, devinfo.name);
         close(fd);
      }
   }

   drmFreeDevices(devices, num_devices);
}

static int
get_drm_device(struct intel_device_info *devinfo, const char *device_pattern)
{
   drmDevicePtr devices[8];
   int num_devices = drmGetDevices2(0, devices, ARRAY_SIZE(devices));
   int fd = -1;
   int index = -1;

   if (!device_pattern)
      device_pattern = "";

   /* Interpret numbers as picking an index. */
   if (isdigit(device_pattern[0])) {
      index = atoi(device_pattern);
   }

   if (index != -1) {
      if (index >= num_devices)
         failf("No device with index %d", index);

      if (!open_intel_render_device(devices[index], devinfo, &fd))
         failf("Couldn't open device with index %d", index);

   } else {
      for (int i = 0; i < num_devices; i++) {
         if (open_intel_render_device(devices[i], devinfo, &fd)) {
            if (strcasestr(devinfo->name, device_pattern)) {
               /* Found a device! */
               break;
            }
            close(fd);
            fd = -1;
         }
      }
   }

   drmFreeDevices(devices, num_devices);
   return fd;
}

static struct intel_batch_decode_bo
decode_get_bo(void *_ec, bool ppgtt, uint64_t address)
{
   executor_context *ec = _ec;
   struct intel_batch_decode_bo bo = {0};

   if (address >= ec->bo.batch.addr && address < ec->bo.batch.addr + ec->bo.batch.size) {
      bo.addr = ec->bo.batch.addr;
      bo.size = ec->bo.batch.size;
      bo.map  = ec->bo.batch.map;
   } else if (address >= ec->bo.extra.addr && address < ec->bo.extra.addr + ec->bo.extra.size) {
      bo.addr = ec->bo.extra.addr;
      bo.size = ec->bo.extra.size;
      bo.map  = ec->bo.extra.map;
   } else if (address >= ec->bo.data.addr && address < ec->bo.data.addr + ec->bo.data.size) {
      bo.addr = ec->bo.data.addr;
      bo.size = ec->bo.data.size;
      bo.map  = ec->bo.data.map;
   } else if (address >= ec->bo.perf.addr &&
              address < ec->bo.perf.addr + ec->bo.perf.size) {
      bo.addr = ec->bo.perf.addr;
      bo.size = ec->bo.perf.size;
      bo.map  = ec->bo.perf.map;
   }

   return bo;
}

static unsigned
decode_get_state_size(void *_ec, uint64_t address, uint64_t base_address)
{
   return EXECUTOR_BO_SIZE;
}

static void
executor_check_bounds(uint32_t size_dw, uint32_t offset_dw, uint32_t count,
                      const char *what)
{
   uint64_t end = (uint64_t)offset_dw + count;
   if (offset_dw > size_dw || end > size_dw)
      failf("%s out of bounds", what);
}

static void
executor_fill_table(uint32_t *data, uint32_t size_dw, lua_State *L, int table_idx,
                    uint32_t base_offset, const char *what)
{
   lua_pushvalue(L, table_idx);

   lua_pushnil(L);
   while (lua_next(L, -2) != 0) {
      int val_idx = lua_gettop(L);
      int key_idx = val_idx - 1;

      if (lua_type(L, key_idx) != LUA_TNUMBER || !lua_isinteger(L, key_idx))
         failf("invalid key for %s", what);

      lua_Integer key = lua_tointeger(L, key_idx);
      if (key < 0)
         failf("invalid key for %s", what);

      uint64_t idx = (uint64_t)base_offset + (uint64_t)key;
      if (idx >= size_dw)
         failf("%s out of bounds", what);

      lua_Integer val = luaL_checkinteger(L, val_idx);
      data[idx] = (uint32_t)val;

      lua_pop(L, 1);
   }

   lua_pop(L, 1);
}

static void
executor_fill_value(uint32_t *data, uint32_t size_dw, uint32_t value)
{
   for (uint32_t i = 0; i < size_dw; i++)
      data[i] = value;
}

static uint32_t
executor_table_size(lua_State *L, int table_idx)
{
   uint32_t size = 0;
   bool found = false;

   lua_pushvalue(L, table_idx);

   lua_pushnil(L);
   while (lua_next(L, -2) != 0) {
      int key_idx = lua_gettop(L) - 1;

      if (lua_type(L, key_idx) != LUA_TNUMBER || !lua_isinteger(L, key_idx))
         failf("invalid allocation data key");

      lua_Integer key = lua_tointeger(L, key_idx);
      if (key < 0 || key >= UINT32_MAX)
         failf("invalid allocation data key");

      if ((uint32_t)key + 1 > size)
         size = (uint32_t)key + 1;
      found = true;

      lua_pop(L, 1);
   }

   lua_pop(L, 1);

   if (!found)
      failf("cannot infer allocation size from empty table");

   return size;
}

static void
executor_push_table(lua_State *L, const uint32_t *data, uint32_t count,
                    uint32_t offset_dw)
{
   lua_createtable(L, count, 0);
   for (uint32_t i = 0; i < count; i++) {
      lua_pushinteger(L, data[offset_dw + i]);
      lua_seti(L, -2, i);
   }
}

static uint32_t
executor_default_simd(const struct intel_device_info *devinfo)
{
   return devinfo->ver >= 20 ? 16 : 8;
}

static void
handle_param_hw_regs(executor_run *run, slice name, slice args)
{
   slice_cut_result cut = slice_cut_any(args, " \t");
   slice value = cut.before;
   slice extra = strip_spaces(cut.after);

   if (!slice_is_empty(extra))
      failf("@param %.*s has extra arguments", SLICE_FMT(name));
   if (slice_is_empty(value))
      failf("@param %.*s needs a value", SLICE_FMT(name));

   int64_t v;
   if (!parse_int64(value, &v))
      failf("@param %.*s must be an integer", SLICE_FMT(name));

   const struct intel_device_info *devinfo = run->ec->devinfo;
   bool valid;
   if (devinfo->verx10 >= 125 && devinfo->ver < 30) {
      valid = v == 128 || v == 256; /* Large GRF supports 128 or 256 regs */
   } else {
      valid = intel_register_blocks_supported(devinfo, v);
   }

   if (!valid)
      failf("@param %.*s %u is not supported", SLICE_FMT(name), (unsigned)v);

   run->hw_regs = (uint32_t)v;
}

static void
handle_param_hw_threads(executor_run *run, slice name, slice args)
{
   executor_context *ec = run->ec;
   slice_cut_result cut = slice_cut_any(args, " \t");
   slice value = cut.before;
   slice extra = strip_spaces(cut.after);

   if (!slice_is_empty(extra))
      failf("@param %.*s has extra arguments", SLICE_FMT(name));
   if (slice_is_empty(value))
      failf("@param %.*s needs a value", SLICE_FMT(name));

   int64_t v;
   if (!parse_int64(value, &v))
      failf("@param %.*s must be an integer", SLICE_FMT(name));
   if (v < 1 || v > ec->devinfo->max_cs_workgroup_threads)
      failf("@param %.*s out of range [1, %u]", SLICE_FMT(name),
            ec->devinfo->max_cs_workgroup_threads);

   const uint32_t hw_threads = (uint32_t)v;
   /* TODO: Use ThreadGroupDispatchSize to support more. */
   if (ec->devinfo->verx10 >= 125 && hw_threads > 16)
      failf("hw_threads > 16 not supported");

   run->hw_threads = hw_threads;
}

static void
handle_param_simd(executor_run *run, slice name, slice args)
{
   executor_context *ec = run->ec;
   slice_cut_result cut = slice_cut_any(args, " \t");
   slice value = cut.before;
   slice extra = strip_spaces(cut.after);

   if (!slice_is_empty(extra))
      failf("@param %.*s has extra arguments", SLICE_FMT(name));
   if (slice_is_empty(value))
      failf("@param %.*s needs a value", SLICE_FMT(name));

   int64_t simd;
   if (!parse_int64(value, &simd))
      failf("@param %.*s must be an integer", SLICE_FMT(name));

   char *name_str = slice_to_cstr(run->tmp_ctx, name);
   if (simd != 8 && simd != 16 && simd != 32)
      failf("%s must be 8, 16, or 32", name_str);

   if (ec->devinfo->ver >= 20 && simd != 16 && simd != 32)
      failf("%s must be 16 or 32 on Xe2+", name_str);

   run->simd = simd;
}

static void
handle_param_slm_size(executor_run *run, slice name, slice args)
{
   executor_context *ec = run->ec;
   slice_cut_result cut = slice_cut_any(args, " \t");
   slice value = cut.before;
   slice extra = strip_spaces(cut.after);

   if (!slice_is_empty(extra))
      failf("@param %.*s has extra arguments", SLICE_FMT(name));
   if (slice_is_empty(value))
      failf("@param %.*s needs a value", SLICE_FMT(name));

   int64_t slm_size;
   if (!parse_int64(value, &slm_size))
      failf("@param %.*s must be an integer", SLICE_FMT(name));

   if (slm_size < 0)
      failf("@param %.*s must be a non-negative byte size", SLICE_FMT(name));

   const uint32_t max_slm_size = intel_device_info_get_max_slm_size(ec->devinfo);
   if (slm_size > max_slm_size)
      failf("@param %.*s exceeds the device SLM limit (%u bytes)",
            SLICE_FMT(name), max_slm_size);

   const uint32_t requested = (uint32_t)slm_size;
   const uint32_t allocated =
      intel_compute_slm_calculate_size(ec->devinfo->ver, requested);
   if (allocated > max_slm_size)
      failf("@param %.*s rounds up to %u bytes, exceeding the device SLM limit (%u bytes)",
            SLICE_FMT(name), allocated, max_slm_size);

   run->slm_size = requested;
}

static void
handle_param_autoswsb(executor_run *run, slice name, slice args)
{
   if (!slice_is_empty(args))
      failf("@param %.*s has extra arguments", SLICE_FMT(name));

   run->autoswsb = true;
}

static void
executor_parse_source_params(executor_run *run, slice src)
{
   static const struct {
      const char *name;
      void (*handle)(executor_run *run, slice name, slice args);
   } param_handlers[] = {
      { "hw_regs",    handle_param_hw_regs },
      { "hw_threads", handle_param_hw_threads },
      { "simd",       handle_param_simd },
      { "slm_size",   handle_param_slm_size },
      { "autoswsb",   handle_param_autoswsb },
   };

   slice rest = src;
   const slice param = slice_from_cstr("@param");

   while (!slice_is_empty(rest)) {
      slice_cut_result line_cut = slice_cut_any(rest, "\n\r");
      slice line = strip_spaces(line_cut.before);
      rest = line_cut.after;

      if (!slice_starts_with(line, param) ||
          (line.len > param.len &&
           !isspace((unsigned char)line.data[param.len])))
         continue;

      line = strip_spaces(
         trim_comments(slice_strip_prefix(line, param)));
      slice_cut_result cut = slice_cut_any(line, " \t");
      slice name = cut.before;

      slice args = strip_spaces(cut.after);

      if (slice_is_empty(name))
         failf("@param needs a name");

      bool found = false;
      for (int i = 0; i < ARRAY_SIZE(param_handlers); i++) {
         if (slice_equal_cstr(name, param_handlers[i].name)) {
            param_handlers[i].handle(run, name, args);
            found = true;
            break;
         }
      }

      if (!found)
         failf("unknown @param '%.*s'", SLICE_FMT(name));
   }
}

static void
parse_execute_thread_groups(executor_run *run, lua_State *L, int idx)
{
   lua_Integer val = luaL_checkinteger(L, idx);
   if (val < 1 || val > UINT32_MAX)
      failf("execute() thread_groups must be in range 1..%u", UINT32_MAX);

   run->thread_groups = (uint32_t)val;
}

static void
parse_execute_args(executor_run *run, lua_State *L)
{
   if (lua_gettop(L) != 1)
      failf("execute() expects a shader string or one table argument");

   if (lua_type(L, 1) == LUA_TSTRING) {
      size_t len;
      const char *src = lua_tolstring(L, 1, &len);
      run->original_src = (slice) { src, len };
      return;
   }

   if (lua_type(L, 1) != LUA_TTABLE)
      failf("execute() expects a shader string or table");

   lua_pushnil(L);

   while (lua_next(L, 1) != 0) {
      int val_idx = lua_gettop(L);
      int key_idx = val_idx - 1;

      if (lua_type(L, key_idx) != LUA_TSTRING) {
         lua_pop(L, 1);
         continue;
      }

      const char *key = lua_tostring(L, key_idx);

      if (!strcmp(key, "src")) {
         size_t len;
         const char *src = luaL_checklstring(L, val_idx, &len);
         run->original_src = (slice) { src, len };
      } else if (!strcmp(key, "thread_groups")) {
         parse_execute_thread_groups(run, L, val_idx);
      } else {
         failf("unknown parameter '%s' for execute()", key);
      }

      lua_pop(L, 1);
   }

   if (!run->original_src.data)
      failf("execute() missing 'src'");
}

static void
executor_context_setup(executor_context *ec)
{
   *ec = (executor_context) {
      .mem_ctx = ralloc_context(NULL),
      .devinfo = &E.devinfo,
      .isl_dev = &E.isl_dev,
      .fd = E.fd,
      .perf_enabled = E.oa_csv_path != NULL || E.oa_spec != NULL,
   };
   if (!ec->mem_ctx)
      failf("failed to allocate executor context");

   if (ec->devinfo->kmd_type == INTEL_KMD_TYPE_I915) {
      struct drm_i915_gem_context_create create = {0};
      int err = intel_ioctl(ec->fd, DRM_IOCTL_I915_GEM_CONTEXT_CREATE, &create);
      if (err)
         failf("i915_gem_context_create");
      ec->i915.ctx_id = create.ctx_id;
   } else {
      assert(ec->devinfo->kmd_type == INTEL_KMD_TYPE_XE);

      struct drm_xe_vm_create create = {
         .flags = DRM_XE_VM_CREATE_FLAG_SCRATCH_PAGE,
      };
      int err = intel_ioctl(ec->fd, DRM_IOCTL_XE_VM_CREATE, &create);
      if (err)
         failf("xe_vm_create");
      ec->xe.vm_id = create.vm_id;

      struct drm_xe_engine_class_instance instance = {0};

      struct intel_query_engine_info *engines_info = xe_engine_get_info(ec->fd);
      assert(engines_info);

      bool found_engine = false;
      for (int i = 0; i < engines_info->num_engines; i++) {
         struct intel_engine_class_instance *e = &engines_info->engines[i];
         if (e->engine_class == INTEL_ENGINE_CLASS_RENDER) {
            instance.engine_class = DRM_XE_ENGINE_CLASS_RENDER;
            instance.engine_instance = e->engine_instance;
            instance.gt_id = e->gt_id;
            found_engine = true;
            break;
         }
      }
      assert(found_engine);
      free(engines_info);

      struct drm_xe_exec_queue_create queue_create = {
         .vm_id          = ec->xe.vm_id,
         .width          = 1,
         .num_placements = 1,
         .instances      = (uintptr_t)&instance,
      };
      err = intel_ioctl(ec->fd, DRM_IOCTL_XE_EXEC_QUEUE_CREATE, &queue_create);
      if (err)
         failf("xe_exec_queue_create");
      ec->xe.queue_id = queue_create.exec_queue_id;
   }

   util_dynarray_init(&ec->mem_regions, ec->mem_ctx);

   executor_create_bo(ec, &ec->bo.batch, EXECUTOR_BO_BATCH_ADDR, EXECUTOR_BO_SIZE);
   executor_create_bo(ec, &ec->bo.extra, EXECUTOR_BO_EXTRA_ADDR, EXECUTOR_BO_SIZE);
   executor_create_bo(ec, &ec->bo.data,  EXECUTOR_BO_DATA_ADDR, EXECUTOR_BO_SIZE);
   if (ec->perf_enabled)
      executor_create_bo(ec, &ec->bo.perf, EXECUTOR_BO_PERF_ADDR,
                         EXECUTOR_BO_SIZE);

   uint32_t *data = ec->bo.data.map;
   for (int i = 0; i < EXECUTOR_BO_SIZE / 4; i++)
      data[i] = 0xABABABAB;
}

static void
executor_context_dispatch(executor_context *ec)
{
   if (ec->devinfo->kmd_type == INTEL_KMD_TYPE_I915) {
      const uint32_t buffer_count = 3 + ec->perf_enabled;
      struct drm_i915_gem_exec_object2 objs[] = {
         {
            .handle = ec->bo.batch.handle,
            .offset = ec->bo.batch.addr,
            .flags  = EXEC_OBJECT_PINNED,
         },
         {
            .handle = ec->bo.extra.handle,
            .offset = ec->bo.extra.addr,
            .flags  = EXEC_OBJECT_PINNED,
         },
         {
            .handle = ec->bo.data.handle,
            .offset = ec->bo.data.addr,
            .flags  = EXEC_OBJECT_PINNED | EXEC_OBJECT_WRITE,
         },
         {},
      };
      if (ec->perf_enabled) {
         objs[3] = (struct drm_i915_gem_exec_object2) {
            .handle = ec->bo.perf.handle,
            .offset = ec->bo.perf.addr,
            .flags  = EXEC_OBJECT_PINNED | EXEC_OBJECT_WRITE,
         };
      }

      struct drm_i915_gem_execbuffer2 exec = {0};
      exec.buffers_ptr = (uintptr_t)objs;
      exec.buffer_count = buffer_count;
      exec.batch_start_offset = ec->batch_start - ec->bo.batch.addr;
      exec.flags = I915_EXEC_BATCH_FIRST;
      exec.rsvd1 = ec->i915.ctx_id;

      int err = intel_ioctl(ec->fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &exec);
      if (err)
          failf("i915_gem_execbuffer2");

      struct drm_i915_gem_wait wait = {0};
      wait.bo_handle = ec->bo.batch.handle;
      wait.timeout_ns = INT64_MAX;

      err = intel_ioctl(ec->fd, DRM_IOCTL_I915_GEM_WAIT, &wait);
      if (err)
         failf("i915_gem_wait");
   } else {
      assert(ec->devinfo->kmd_type == INTEL_KMD_TYPE_XE);

      /* First syncobj is signalled by the binding operation and waited by the
       * execution of the batch buffer.
       *
       * Second syncobj is singalled by the execution of batch buffer and
       * waited at the end.
       */
      uint32_t sync_handles[2] = {0};
      for (int i = 0; i < 2; i++) {
         struct drm_syncobj_create sync_create = {0};
         int err = intel_ioctl(ec->fd, DRM_IOCTL_SYNCOBJ_CREATE, &sync_create);
         if (err)
            failf("syncobj_create");
         sync_handles[i] = sync_create.handle;
      }

      const uint32_t num_binds = 3 + ec->perf_enabled;
      struct drm_xe_vm_bind_op bind_ops[] = {
         {
            .op        = DRM_XE_VM_BIND_OP_MAP,
            .obj       = ec->bo.batch.handle,
            .addr      = ec->bo.batch.addr,
            .range     = EXECUTOR_BO_SIZE,
            .pat_index = ec->devinfo->pat.cached_coherent.index,
         },
         {
            .op        = DRM_XE_VM_BIND_OP_MAP,
            .obj       = ec->bo.extra.handle,
            .addr      = ec->bo.extra.addr,
            .range     = EXECUTOR_BO_SIZE,
            .pat_index = ec->devinfo->pat.cached_coherent.index,
         },
         {
            .op        = DRM_XE_VM_BIND_OP_MAP,
            .obj       = ec->bo.data.handle,
            .addr      = ec->bo.data.addr,
            .range     = EXECUTOR_BO_SIZE,
            .pat_index = ec->devinfo->pat.cached_coherent.index,
         },
         {},
      };
      if (ec->perf_enabled) {
         bind_ops[3] = (struct drm_xe_vm_bind_op) {
            .op        = DRM_XE_VM_BIND_OP_MAP,
            .obj       = ec->bo.perf.handle,
            .addr      = ec->bo.perf.addr,
            .range     = ec->bo.perf.size,
            .pat_index = ec->devinfo->pat.cached_coherent.index,
         };
      }

      struct drm_xe_sync bind_syncs[] = {
         {
            .type   = DRM_XE_SYNC_TYPE_SYNCOBJ,
            .addr   = 0,
            .flags  = DRM_XE_SYNC_FLAG_SIGNAL,
         },
      };
      bind_syncs[0].handle = sync_handles[0];

      struct drm_xe_vm_bind bind = {
         .vm_id           = ec->xe.vm_id,
         .num_binds       = num_binds,
         .vector_of_binds = (uintptr_t)bind_ops,
         .num_syncs       = 1,
         .syncs           = (uintptr_t)bind_syncs,
      };

      int err = intel_ioctl(ec->fd, DRM_IOCTL_XE_VM_BIND, &bind);
      if (err)
         failf("xe_vm_bind");

      struct drm_xe_sync exec_syncs[] = {
         {
            .type   = DRM_XE_SYNC_TYPE_SYNCOBJ,
            .addr   = 0,
         },
         {
            .type   = DRM_XE_SYNC_TYPE_SYNCOBJ,
            .addr   = 0,
            .flags  = DRM_XE_SYNC_FLAG_SIGNAL,
         }
      };
      exec_syncs[0].handle = sync_handles[0];
      exec_syncs[1].handle = sync_handles[1];

      struct drm_xe_exec exec = {
         .exec_queue_id    = ec->xe.queue_id,
         .num_batch_buffer = 1,
         .address          = ec->batch_start,
         .num_syncs        = 2,
         .syncs            = (uintptr_t)exec_syncs,
      };
      err = intel_ioctl(ec->fd, DRM_IOCTL_XE_EXEC, &exec);
      if (err)
         failf("xe_exec");

      struct drm_syncobj_wait wait = {
         .count_handles = 1,
         .handles       = (uintptr_t)&sync_handles[1],
         .timeout_nsec  = INT64_MAX,
      };
      err = intel_ioctl(ec->fd, DRM_IOCTL_SYNCOBJ_WAIT, &wait);
      if (err)
         failf("syncobj_wait");

      for (int i = 0; i < ARRAY_SIZE(sync_handles); i++) {
         struct drm_syncobj_destroy sync_destroy = {
            .handle = sync_handles[i],
         };
         err = intel_ioctl(ec->fd, DRM_IOCTL_SYNCOBJ_DESTROY, &sync_destroy);
         if (err)
            failf("syncobj_destroy");
      }
   }
}

static void
executor_context_teardown(executor_context *ec)
{
   free(ec->surface_bindings);
   ec->surface_bindings = NULL;
   ec->num_surface_bindings = 0;
   ec->surface_binding_capacity = 0;

   free(ec->sampler_bindings);
   ec->sampler_bindings = NULL;
   ec->num_sampler_bindings = 0;
   ec->sampler_binding_capacity = 0;

   if (ec->perf_enabled)
      executor_destroy_bo(ec, &ec->bo.perf);

   executor_destroy_bo(ec, &ec->bo.batch);
   executor_destroy_bo(ec, &ec->bo.extra);
   executor_destroy_bo(ec, &ec->bo.data);

   if (ec->devinfo->kmd_type == INTEL_KMD_TYPE_I915) {
      struct drm_i915_gem_context_destroy destroy = {
         .ctx_id = ec->i915.ctx_id,
      };
      int err = intel_ioctl(ec->fd, DRM_IOCTL_I915_GEM_CONTEXT_DESTROY, &destroy);
      if (err)
         failf("i915_gem_context_destroy");
   } else {
      assert(ec->devinfo->kmd_type == INTEL_KMD_TYPE_XE);

      struct drm_xe_exec_queue_destroy queue_destroy = {
         .exec_queue_id = ec->xe.queue_id,
      };
      int err = intel_ioctl(ec->fd, DRM_IOCTL_XE_EXEC_QUEUE_DESTROY, &queue_destroy);
      if (err)
         failf("xe_exec_queue_destroy");

      struct drm_xe_vm_destroy destroy = {
         .vm_id =  ec->xe.vm_id,
      };
      err = intel_ioctl(ec->fd, DRM_IOCTL_XE_VM_DESTROY, &destroy);
      if (err)
         failf("xe_vm_destroy");
   }

   ralloc_free(ec->mem_ctx);
   ec->mem_ctx = NULL;
}

static void
executor_print_gen_program(executor_context *ec,
                           gen_inst *insts,
                           int num_insts,
                           const gen_error *errors,
                           int num_errors)
{
   gen_print_params print = {
      .devinfo = ec->devinfo,
      .fp = stderr,
      .insts = insts,
      .num_insts = num_insts,
      .errors = errors,
      .num_errors = num_errors,
   };

   gen_print(&print);
}

static bool
inst_has_type(const struct intel_device_info *devinfo,
              const gen_inst *inst, gen_reg_type type)
{
   if (inst->dst.file != GEN_BAD_FILE && inst->dst.type == type)
      return true;

   const unsigned num_sources = gen_inst_num_sources(devinfo, inst);
   for (unsigned i = 0; i < num_sources; i++) {
      if (inst->src[i].type == type)
         return true;
   }

   return false;
}

static bool
inst_is_unordered(const struct intel_device_info *devinfo,
                           const gen_inst *inst)
{
   return gen_inst_is_send(inst) ||
          (devinfo->ver < 20 && inst->opcode == GEN_OP_MATH) ||
          inst->opcode == GEN_OP_DPAS ||
          (devinfo->has_64bit_float_via_math_pipe &&
           inst_has_type(devinfo, inst, GEN_TYPE_DF));
}

static void
adjust_autoswsb_branch(gen_inst *inst, int old_ip, int new_ip,
                                const int *old_to_new, int num_old_insts,
                                int src_idx)
{
   if (src_idx < 0 || inst->src[src_idx].file != GEN_IMM)
      return;

   const int32_t old_rel = (int32_t)inst->src[src_idx].imm;
   if (old_rel % 16 != 0)
      return;

   const int old_target = old_ip + old_rel / 16;
   if (old_target < 0 || old_target > num_old_insts)
      return;

   const int32_t new_rel = (old_to_new[old_target] - new_ip) * 16;
   inst->src[src_idx].imm = (uint32_t)new_rel;
}

static void
apply_autoswsb(executor_context *ec, gen_parse_params *parse)
{
   if (ec->devinfo->ver < 12)
      return;

   int extra_syncs = 0;
   for (int i = 0; i < parse->num_insts; i++) {
      const gen_inst *inst = &parse->insts[i];
      if (inst_is_unordered(ec->devinfo, inst) &&
          !(gen_inst_is_send(inst) && inst->send.eot) &&
          !(inst->swsb.regdist || inst->swsb.mode))
         extra_syncs++;
   }

   gen_inst *insts = ralloc_array(parse->mem_ctx, gen_inst,
                                  parse->num_insts + extra_syncs);
   int *old_to_new = ralloc_array(parse->mem_ctx, int, parse->num_insts + 1);

   int new_count = 0;
   for (int i = 0; i < parse->num_insts; i++) {
      gen_inst inst = parse->insts[i];
      const bool has_swsb = inst.swsb.regdist || inst.swsb.mode;
      const bool is_eot_send = gen_inst_is_send(&inst) && inst.send.eot;
      const bool is_unordered = inst_is_unordered(ec->devinfo, &inst) &&
                                !is_eot_send;

      old_to_new[i] = new_count;

      if (!has_swsb) {
         if (is_unordered) {
            inst.swsb = gen_swsb_dst_dep(gen_swsb_sbid(GEN_SBID_SET, 0), 1);
         } else {
            inst.swsb = gen_swsb_regdist(1);
         }
      }

      insts[new_count++] = inst;

      if (is_unordered && !has_swsb) {
         gen_inst sync = {
            .opcode = GEN_OP_SYNC,
            .exec_size = 1,
            .no_mask = true,
            .swsb = gen_swsb_sbid(GEN_SBID_DST, 0),
         };

         sync.sync.func = GEN_SYNC_NOP;
         sync.dst = gen_null();
         sync.src[0] = gen_null();

         insts[new_count++] = sync;
      }
   }
   old_to_new[parse->num_insts] = new_count;

   for (int i = 0; i < parse->num_insts; i++) {
      const int new_ip = old_to_new[i];
      gen_inst *inst = &insts[new_ip];

      adjust_autoswsb_branch(inst, i, new_ip, old_to_new,
                                      parse->num_insts,
                                      gen_inst_jip_src_index(inst->opcode));
      adjust_autoswsb_branch(inst, i, new_ip, old_to_new,
                                      parse->num_insts,
                                      gen_inst_uip_src_index(inst->opcode));
   }

   parse->insts = insts;
   parse->num_insts = new_count;
}

static bool
executor_assemble(executor_run *run, const char *src)
{
   executor_context *ec = run->ec;
   const bool dump = INTEL_DEBUG(DEBUG_CS);

   gen_parse_params parse = {
      .devinfo = ec->devinfo,
      .text = src,
      .text_size = (int)strlen(src),
      .mem_ctx = run->tmp_ctx,
   };

   if (!gen_parse(&parse)) {
      for (int i = 0; i < parse.num_errors; i++) {
         fprintf(stderr, "<executor>:%u: %s\n",
                 parse.errors[i].index, parse.errors[i].msg);
      }
      return false;
   }

   if (parse.num_insts == 0) {
      fprintf(stderr, "no instructions to assemble\n");
      return false;
   }

   if (run->autoswsb)
      apply_autoswsb(ec, &parse);

   if (!gen_finish_structured_cf(parse.insts, parse.num_insts, -1)) {
      executor_print_gen_program(ec, parse.insts, parse.num_insts, NULL, 0);
      fprintf(stderr, "Failed to finalize structured control flow.\n");
      return false;
   }

   if (dump)
      executor_print_gen_program(ec, parse.insts, parse.num_insts, NULL, 0);

   const int raw_bytes_size = parse.num_insts * (int)sizeof(gen_raw_inst);

   gen_encode_params encode = {
      .devinfo = ec->devinfo,
      .mem_ctx = run->tmp_ctx,
      .insts = parse.insts,
      .num_insts = parse.num_insts,
      .raw_bytes = rzalloc_size(run->tmp_ctx, raw_bytes_size),
      .raw_bytes_size = raw_bytes_size,
   };

   if (!gen_encode(&encode)) {
      executor_print_gen_program(ec, parse.insts, parse.num_insts,
                                 encode.errors, encode.num_errors);
      fprintf(stderr, "Invalid instructions.\n");
      return false;
   }

   run->kernel_bin = encode.raw_bytes;
   run->kernel_size = encode.raw_bytes_size;
   return true;
}

static executor_mem_region *
executor_mem_get_from_lua(lua_State *L, int idx)
{
   executor_mem_userdata *region_idx = luaL_checkudata(L, idx, EXECUTOR_MEM_MT);
   return executor_get_mem_region(&E.ec, *region_idx);
}

static void
executor_mem_push(lua_State *L, uint32_t region_idx)
{
   executor_mem_region *region = executor_get_mem_region(&E.ec, region_idx);
   assert(region->bo);
   assert(region->offset % 4 == 0);
   assert(region->size % 4 == 0);

   executor_mem_userdata *mem = lua_newuserdata(L, sizeof(*mem));
   *mem = region_idx;

   luaL_getmetatable(L, EXECUTOR_MEM_MT);
   lua_setmetatable(L, -2);
}

static void
surface_push(lua_State *L, uint32_t bti)
{
   executor_surface_userdata *surface = lua_newuserdata(L, sizeof(*surface));
   *surface = bti;

   luaL_getmetatable(L, EXECUTOR_SURFACE_MT);
   lua_setmetatable(L, -2);
}

static void
sampler_push(lua_State *L, uint32_t index)
{
   executor_sampler_userdata *sampler = lua_newuserdata(L, sizeof(*sampler));
   *sampler = index;

   luaL_getmetatable(L, EXECUTOR_SAMPLER_MT);
   lua_setmetatable(L, -2);
}

static void
executor_dump_values(const uint32_t *data, uint32_t count, uint32_t base_index)
{
   for (uint32_t i = 0; i < count; i++) {
      if (i % 8 == 0)
         printf("[0x%08x]", (base_index + i) * 4);
      printf(" 0x%08x", data[i]);
      if (i % 8 == 7)
         printf("\n");
   }
   if (count % 8 != 0)
      printf("\n");
}

static void
parse_alloc_options(lua_State *L, int idx, uint32_t *alignment,
                    const char **name, bool *has_fill, uint32_t *fill_value)
{
   if (idx > lua_gettop(L) || lua_isnil(L, idx))
      return;

   if (lua_type(L, idx) == LUA_TSTRING) {
      *name = lua_tostring(L, idx);
      return;
   }

   luaL_checktype(L, idx, LUA_TTABLE);

   lua_getfield(L, idx, "align");
   if (!lua_isnil(L, -1)) {
      lua_Integer align_val = luaL_checkinteger(L, -1);
      if (align_val < 0 || align_val > UINT32_MAX)
         failf("invalid alignment");
      *alignment = (uint32_t)align_val;
   }
   lua_pop(L, 1);

   lua_getfield(L, idx, "name");
   if (!lua_isnil(L, -1))
      *name = luaL_checkstring(L, -1);
   lua_pop(L, 1);

   lua_getfield(L, idx, "fill");
   if (!lua_isnil(L, -1)) {
      lua_Integer fill_val = luaL_checkinteger(L, -1);
      *fill_value = (uint32_t)fill_val;
      *has_fill = true;
   }
   lua_pop(L, 1);
}

static int
l_executor_alloc(lua_State *L)
{
   executor_context *ec = &E.ec;
   if (lua_gettop(L) < 1 || lua_gettop(L) > 2)
      failf("alloc() expects data/size and optional name/options");

   uint32_t alignment = 0;
   const char *name = NULL;
   bool has_fill = false;
   uint32_t fill_value = 0;
   parse_alloc_options(L, 2, &alignment, &name, &has_fill, &fill_value);

   uint32_t size_dw;
   bool fill_from_table = false;

   if (lua_type(L, 1) == LUA_TTABLE) {
      size_dw = executor_table_size(L, 1);
      fill_from_table = true;
   } else {
      lua_Integer size_val = luaL_checkinteger(L, 1);
      if (size_val < 0 || size_val > UINT32_MAX)
         failf("invalid allocation size");
      size_dw = (uint32_t)size_val;
   }

   uint32_t region_idx = executor_data_alloc(ec, size_dw, alignment, name);
   executor_mem_region *region = executor_get_mem_region(ec, region_idx);
   if (fill_from_table || has_fill) {
      executor_fill_value(region->map, size_dw, has_fill ? fill_value : 0);
      if (fill_from_table)
         executor_fill_table(region->map, size_dw, L, 1, 0, "allocation data");
   }

   executor_mem_push(L, region_idx);
   return 1;
}

static enum isl_format
parse_surface_format(const char *format, uint32_t *stride)
{
   if (!format || !strcmp(format, "r32uint") || !strcmp(format, "r32u")) {
      *stride = 4;
      return ISL_FORMAT_R32_UINT;
   } else if (!strcmp(format, "r32float") || !strcmp(format, "r32f")) {
      *stride = 4;
      return ISL_FORMAT_R32_FLOAT;
   } else if (!strcmp(format, "rgba32float") || !strcmp(format, "rgba32f")) {
      *stride = 16;
      return ISL_FORMAT_R32G32B32A32_FLOAT;
   } else if (!strcmp(format, "rgba8unorm") || !strcmp(format, "rgba8")) {
      *stride = 4;
      return ISL_FORMAT_R8G8B8A8_UNORM;
   }

   failf("unknown surface format '%s'", format);
   return ISL_FORMAT_UNSUPPORTED;
}

static const char *
surface_opt_format(lua_State *L, int idx)
{
   if (lua_isnoneornil(L, idx))
      return NULL;

   if (lua_type(L, idx) == LUA_TSTRING)
      return lua_tostring(L, idx);

   luaL_checktype(L, idx, LUA_TTABLE);
   lua_getfield(L, idx, "format");
   const char *format = lua_isnil(L, -1) ? NULL : luaL_checkstring(L, -1);
   lua_pop(L, 1);
   return format;
}

static uint32_t
lua_check_u32(lua_State *L, int idx, const char *what)
{
   lua_Integer val = luaL_checkinteger(L, idx);
   if (val < 0 || val > UINT32_MAX)
      failf("invalid %s", what);
   return (uint32_t)val;
}

static uint32_t
lua_check_u32_field(lua_State *L, int opts, const char *field,
                    const char *what)
{
   lua_getfield(L, opts, field);
   if (lua_isnil(L, -1))
      failf("%s missing '%s'", what, field);
   uint32_t val = lua_check_u32(L, -1, field);
   lua_pop(L, 1);
   return val;
}

static int
l_executor_surface_buffer(lua_State *L)
{
   executor_context *ctx = &E.ec;
   executor_mem_region *mem = executor_mem_get_from_lua(L, 1);

   uint32_t stride = 0;
   enum isl_format isl_format =
      parse_surface_format(surface_opt_format(L, 2), &stride);
   if (mem->size < stride)
      failf("surface_buffer() memory object too small for format");

   uint32_t bti = register_surface_binding(ctx, *mem,
                                           EXECUTOR_SURFACE_BUFFER,
                                           isl_format, stride, 0, 0);
   surface_push(L, bti);
   return 1;
}

static int
l_executor_surface_2d(lua_State *L)
{
   executor_context *ctx = &E.ec;
   executor_mem_region *mem = executor_mem_get_from_lua(L, 1);

   luaL_checktype(L, 2, LUA_TTABLE);
   int opts = lua_absindex(L, 2);

   uint32_t element_size = 0;
   enum isl_format isl_format =
      parse_surface_format(surface_opt_format(L, opts), &element_size);
   uint32_t width = lua_check_u32_field(L, opts, "width", "surface_2d()");
   uint32_t height = lua_check_u32_field(L, opts, "height", "surface_2d()");
   if (width == 0 || height == 0)
      failf("surface_2d() width and height must be non-zero");

   const uint64_t min_row_pitch = (uint64_t)element_size * width;
   if (min_row_pitch > UINT32_MAX)
      failf("surface_2d() width * element size exceeds maximum stride");

   uint32_t row_pitch = (uint32_t)min_row_pitch;
   lua_getfield(L, opts, "stride");
   if (!lua_isnil(L, -1))
      row_pitch = lua_check_u32(L, -1, "surface_2d() stride");
   lua_pop(L, 1);

   if ((uint64_t)row_pitch < min_row_pitch)
      failf("surface_2d() stride smaller than width * element size");
   if ((uint64_t)row_pitch * height > mem->size)
      failf("surface_2d() memory object too small");

   uint32_t bti = register_surface_binding(ctx, *mem,
                                           EXECUTOR_SURFACE_2D,
                                           isl_format, row_pitch,
                                           width, height);
   surface_push(L, bti);
   return 1;
}

static executor_sampler_filter
parse_sampler_filter(const char *filter, const char *what)
{
   if (!filter || !strcmp(filter, "nearest"))
      return EXECUTOR_SAMPLER_FILTER_NEAREST;
   if (!strcmp(filter, "linear"))
      return EXECUTOR_SAMPLER_FILTER_LINEAR;
   failf("unknown %s '%s'", what, filter);
   return EXECUTOR_SAMPLER_FILTER_NEAREST;
}

static executor_sampler_address
parse_sampler_address(const char *address)
{
   if (!address || !strcmp(address, "clamp"))
      return EXECUTOR_SAMPLER_ADDRESS_CLAMP;
   if (!strcmp(address, "repeat"))
      return EXECUTOR_SAMPLER_ADDRESS_REPEAT;
   if (!strcmp(address, "mirror"))
      return EXECUTOR_SAMPLER_ADDRESS_MIRROR;
   failf("unknown sampler address mode '%s'", address);
   return EXECUTOR_SAMPLER_ADDRESS_CLAMP;
}

static float
lua_check_sampler_lod(lua_State *L, int idx, const char *what)
{
   const lua_Number lod = luaL_checknumber(L, idx);
   if (!isfinite(lod) || lod < 0 || lod > 4095.0 / 256.0)
      failf("invalid %s", what);

   return (float)lod;
}

static int
l_executor_sampler(lua_State *L)
{
   executor_context *ctx = &E.ec;
   executor_sampler_binding state = {
      .min_filter = EXECUTOR_SAMPLER_FILTER_NEAREST,
      .mag_filter = EXECUTOR_SAMPLER_FILTER_NEAREST,
      .address_mode = EXECUTOR_SAMPLER_ADDRESS_CLAMP,
      .nonnormalized_coordinates = false,
      .min_lod = 0,
      .max_lod = 14,
   };

   if (!lua_isnoneornil(L, 1)) {
      luaL_checktype(L, 1, LUA_TTABLE);
      int opts = lua_absindex(L, 1);

      lua_getfield(L, opts, "filter");
      if (!lua_isnil(L, -1)) {
         state.min_filter = parse_sampler_filter(luaL_checkstring(L, -1),
                                                 "sampler filter");
         state.mag_filter = state.min_filter;
      }
      lua_pop(L, 1);

      lua_getfield(L, opts, "min_filter");
      if (!lua_isnil(L, -1))
         state.min_filter = parse_sampler_filter(luaL_checkstring(L, -1),
                                                 "sampler min_filter");
      lua_pop(L, 1);

      lua_getfield(L, opts, "mag_filter");
      if (!lua_isnil(L, -1))
         state.mag_filter = parse_sampler_filter(luaL_checkstring(L, -1),
                                                 "sampler mag_filter");
      lua_pop(L, 1);

      lua_getfield(L, opts, "address");
      if (!lua_isnil(L, -1))
         state.address_mode = parse_sampler_address(luaL_checkstring(L, -1));
      lua_pop(L, 1);

      lua_getfield(L, opts, "normalized");
      if (!lua_isnil(L, -1))
         state.nonnormalized_coordinates = !lua_toboolean(L, -1);
      lua_pop(L, 1);

      lua_getfield(L, opts, "nonnormalized");
      if (!lua_isnil(L, -1))
         state.nonnormalized_coordinates = lua_toboolean(L, -1);
      lua_pop(L, 1);

      lua_getfield(L, opts, "min_lod");
      if (!lua_isnil(L, -1))
         state.min_lod = lua_check_sampler_lod(L, -1, "sampler min_lod");
      lua_pop(L, 1);

      lua_getfield(L, opts, "max_lod");
      if (!lua_isnil(L, -1))
         state.max_lod = lua_check_sampler_lod(L, -1, "sampler max_lod");
      lua_pop(L, 1);
   }

   uint32_t index = register_sampler_binding(ctx, &state);
   sampler_push(L, index);
   return 1;
}

static int
l_execute(lua_State *L)
{
   executor_context *ec = &E.ec;
   executor_run run = {
      .ec = ec,
      .tmp_ctx = ralloc_context(ec->mem_ctx),
      .hw_regs = 128,
      .hw_threads = 1,
      .thread_groups = 1,
      .simd = executor_default_simd(ec->devinfo),
   };
   if (!run.tmp_ctx)
      failf("failed to allocate execute scratch context");

   {
      parse_execute_args(&run, L);

      /* Reset the cursors for a new dispatch. */
      ec->bo.batch.cursor = ec->bo.batch.map;
      ec->bo.extra.cursor = ec->bo.extra.map;

      executor_parse_source_params(&run, run.original_src);

      if (!run.simd)
         run.simd = executor_default_simd(ec->devinfo);

      const char *src = executor_apply_macros(&run);

      if (INTEL_DEBUG(DEBUG_CS)) {
         printf("=== Processed assembly source ===\n"
                "%s"
                "=================================\n\n", src);
      }

      if (!executor_assemble(&run, src)) {
         ralloc_free(run.tmp_ctx);
         failf("assembler failure");
      }
   }

   executor_perf_create_query(ec);
   genX_call(emit_execute, &run);

   if (INTEL_DEBUG(DEBUG_BATCH)) {
      struct intel_batch_decode_ctx decoder;
      enum intel_batch_decode_flags flags = INTEL_BATCH_DECODE_DEFAULT_FLAGS;
      if (INTEL_DEBUG(DEBUG_COLOR))
         flags |= INTEL_BATCH_DECODE_IN_COLOR;

      intel_batch_decode_ctx_init_gen(&decoder, ec->devinfo, stdout,
                                      flags, NULL, decode_get_bo,
                                      decode_get_state_size, ec);

      assert(ec->bo.batch.cursor > ec->bo.batch.map);
      const int batch_offset = ec->batch_start - ec->bo.batch.addr;
      const int batch_size = (ec->bo.batch.cursor - ec->bo.batch.map) - batch_offset;
      assert(batch_offset < batch_size);

      intel_print_batch(&decoder, ec->bo.batch.map, batch_size, ec->batch_start, false);

      intel_batch_decode_ctx_finish(&decoder);
   }

   executor_context_dispatch(ec);
   executor_perf_finish_query(ec);

   ralloc_free(run.tmp_ctx);
   return 0;
}

static int
l_mem_fill(lua_State *L)
{
   executor_mem_region *mem = executor_mem_get_from_lua(L, 1);

   if (lua_gettop(L) != 2)
      failf("mem:fill() expects one value");

   lua_Integer val = luaL_checkinteger(L, 2);
   uint32_t size_dw = mem->size / 4;
   executor_fill_value(mem->map, size_dw, (uint32_t)val);

   return 0;
}

static int
l_mem_set(lua_State *L)
{
   executor_mem_region *mem = executor_mem_get_from_lua(L, 1);
   luaL_checktype(L, 2, LUA_TTABLE);

   lua_Integer offset_val = luaL_optinteger(L, 3, 0);
   if (offset_val < 0 || offset_val > UINT32_MAX)
      failf("invalid set offset");

   uint32_t offset_dw = (uint32_t)offset_val;
   uint32_t size_dw = mem->size / 4;
   executor_check_bounds(size_dw, offset_dw, 0, "memory set");
   executor_fill_table(mem->map, size_dw, L, 2, offset_dw, "memory set");
   return 0;
}

static int
l_mem_read(lua_State *L)
{
   executor_mem_region *mem = executor_mem_get_from_lua(L, 1);
   lua_Integer count_val = luaL_checkinteger(L, 2);
   lua_Integer offset_val = luaL_optinteger(L, 3, 0);

   if (count_val < 0 || count_val > UINT32_MAX)
      failf("invalid read count");
   if (offset_val < 0 || offset_val > UINT32_MAX)
      failf("invalid read offset");

   uint32_t count = (uint32_t)count_val;
   uint32_t offset_dw = (uint32_t)offset_val;
   uint32_t size_dw = mem->size / 4;

   executor_check_bounds(size_dw, offset_dw, count, "memory read");
   executor_push_table(L, mem->map, count, offset_dw);
   return 1;
}

static int
l_mem_to_table(lua_State *L)
{
   executor_mem_region *mem = executor_mem_get_from_lua(L, 1);
   uint32_t size_dw = mem->size / 4;
   executor_push_table(L, mem->map, size_dw, 0);
   return 1;
}

static int
l_mem_offset(lua_State *L)
{
   executor_mem_region *mem = executor_mem_get_from_lua(L, 1);
   uint32_t offset_dw = mem->offset / 4;
   lua_pushinteger(L, offset_dw);
   return 1;
}

static int
l_mem_addr(lua_State *L)
{
   executor_mem_region *mem = executor_mem_get_from_lua(L, 1);
   uint64_t addr = mem->bo->addr + mem->offset;
   if (addr > UINT32_MAX)
      failf("memory address 0x%llx exceeds 32-bit limit for a32 messages",
            (unsigned long long)addr);
   lua_pushinteger(L, addr);
   return 1;
}

static int
l_mem_name(lua_State *L)
{
   executor_mem_region *mem = executor_mem_get_from_lua(L, 1);
   if (mem->name)
      lua_pushstring(L, mem->name);
   else
      lua_pushnil(L);
   return 1;
}

static int
l_mem_dump(lua_State *L)
{
   executor_mem_region *mem = executor_mem_get_from_lua(L, 1);
   lua_Integer count_val = luaL_checkinteger(L, 2);
   lua_Integer offset_val = luaL_optinteger(L, 3, 0);

   if (count_val < 0 || count_val > UINT32_MAX)
      failf("invalid dump count");
   if (offset_val < 0 || offset_val > UINT32_MAX)
      failf("invalid dump offset");

   uint32_t count = (uint32_t)count_val;
   uint32_t offset_dw = (uint32_t)offset_val;
   uint32_t size_dw = mem->size / 4;

   executor_check_bounds(size_dw, offset_dw, count, "memory dump");

   uint32_t *data = mem->map;
   executor_dump_values(data + offset_dw, count, offset_dw);
   return 0;
}

static int
l_mem_index(lua_State *L)
{
   executor_mem_region *mem = executor_mem_get_from_lua(L, 1);

   if (lua_type(L, 2) == LUA_TNUMBER && lua_isinteger(L, 2)) {
      lua_Integer idx = lua_tointeger(L, 2);
      uint32_t size_dw = mem->size / 4;
      if (idx < 0 || idx >= (lua_Integer)size_dw)
         failf("memory index out of bounds");
      uint32_t *data = mem->map;
      lua_pushinteger(L, data[idx]);
      return 1;
   }

   luaL_getmetatable(L, EXECUTOR_MEM_MT);
   lua_pushvalue(L, 2);
   lua_rawget(L, -2);
   lua_remove(L, -2);
   return 1;
}

static int
l_mem_newindex(lua_State *L)
{
   executor_mem_region *mem = executor_mem_get_from_lua(L, 1);

   if (lua_type(L, 2) != LUA_TNUMBER || !lua_isinteger(L, 2))
      failf("invalid memory index");

   lua_Integer idx = lua_tointeger(L, 2);
   uint32_t size_dw = mem->size / 4;
   if (idx < 0 || idx >= (lua_Integer)size_dw)
      failf("memory index out of bounds");

   lua_Integer val = luaL_checkinteger(L, 3);
   uint32_t *data = mem->map;
   data[idx] = (uint32_t)val;
   return 0;
}

static int
l_mem_len(lua_State *L)
{
   executor_mem_region *mem = executor_mem_get_from_lua(L, 1);
   uint32_t size_dw = mem->size / 4;
   lua_pushinteger(L, size_dw);
   return 1;
}

static int
l_surface_bti(lua_State *L)
{
   executor_surface_userdata *surface =
      luaL_checkudata(L, 1, EXECUTOR_SURFACE_MT);
   lua_pushinteger(L, *surface);
   return 1;
}

static int
l_sampler_index(lua_State *L)
{
   executor_sampler_userdata *sampler =
      luaL_checkudata(L, 1, EXECUTOR_SAMPLER_MT);
   lua_pushinteger(L, *sampler);
   return 1;
}

static uint32_t
sampler_simd_mode(uint32_t simd, bool high_precision)
{
   if (E.devinfo.ver == 9 && high_precision)
      failf("sampler_desc() hp is not supported on Gfx9");

   if (E.devinfo.ver >= 20) {
      if (high_precision) {
         if (simd == 16) return GEN_XE2_SAMPLER_SIMD_MODE_SIMD16H;
         if (simd == 32) return GEN_XE2_SAMPLER_SIMD_MODE_SIMD32H;
      } else {
         if (simd == 16) return GEN_XE2_SAMPLER_SIMD_MODE_SIMD16;
         if (simd == 32) return GEN_XE2_SAMPLER_SIMD_MODE_SIMD32;
      }
   } else {
      if (high_precision) {
         if (simd == 8) return GEN_GFX11_SAMPLER_SIMD_MODE_SIMD8H;
         if (simd == 16) return GEN_GFX11_SAMPLER_SIMD_MODE_SIMD16H;
      } else {
         if (simd == 8) return GEN_SAMPLER_SIMD_MODE_SIMD8;
         if (simd == 16) return GEN_SAMPLER_SIMD_MODE_SIMD16;
         if (simd == 32) return GEN_SAMPLER_SIMD_MODE_SIMD32_64;
      }
   }

   failf("unsupported sampler SIMD mode");
   return 0;
}

static bool
sampler_desc_read_index(lua_State *L, int idx, const char *what,
                        bool surface, uint32_t *out)
{
   if (lua_isnil(L, idx))
      return false;

   if (surface && luaL_testudata(L, idx, EXECUTOR_SURFACE_MT)) {
      executor_surface_userdata *bti =
         luaL_checkudata(L, idx, EXECUTOR_SURFACE_MT);
      *out = *bti;
      return true;
   }

   if (!surface && luaL_testudata(L, idx, EXECUTOR_SAMPLER_MT)) {
      executor_sampler_userdata *sampler =
         luaL_checkudata(L, idx, EXECUTOR_SAMPLER_MT);
      *out = *sampler;
      return true;
   }

   lua_Integer val = luaL_checkinteger(L, idx);
   if (val < 0 || val > UINT32_MAX)
      failf("invalid %s", what);
   *out = (uint32_t)val;
   return true;
}

static uint32_t
sampler_desc_get_index(lua_State *L, int opts, const char *primary_field,
                       const char *fallback_field, const char *what,
                       bool surface)
{
   uint32_t index = 0;
   lua_getfield(L, opts, primary_field);
   bool found = sampler_desc_read_index(L, -1, what, surface, &index);
   lua_pop(L, 1);
   if (found)
      return index;

   lua_getfield(L, opts, fallback_field);
   found = sampler_desc_read_index(L, -1, what, surface, &index);
   lua_pop(L, 1);
   if (!found)
      failf("sampler_desc() missing '%s'", fallback_field);
   return index;
}

static int
l_sampler_desc(lua_State *L)
{
   luaL_checktype(L, 1, LUA_TTABLE);
   int opts = lua_absindex(L, 1);

   lua_getfield(L, opts, "op");
   const char *op = luaL_optstring(L, -1, "ld");
   lua_pop(L, 1);

   uint32_t bti = sampler_desc_get_index(L, opts, "surface", "bti",
                                         "BTI", true);
   if (bti > 255)
      failf("invalid BTI");

   uint32_t sampler = 0;
   lua_getfield(L, opts, "sampler");
   if (!sampler_desc_read_index(L, -1, "sampler index", false, &sampler))
      sampler = 0;
   lua_pop(L, 1);
   if (sampler > 15)
      failf("invalid sampler index");

   lua_Integer simd_val = executor_default_simd(&E.devinfo);
   lua_getfield(L, opts, "simd");
   if (!lua_isnil(L, -1))
      simd_val = luaL_checkinteger(L, -1);
   lua_pop(L, 1);
   if (simd_val != 8 && simd_val != 16 && simd_val != 32)
      failf("sampler_desc() simd must be 8, 16, or 32");

   lua_getfield(L, opts, "mlen");
   if (lua_isnil(L, -1))
      failf("sampler_desc() missing 'mlen'");
   lua_Integer mlen_val = luaL_checkinteger(L, -1);
   lua_pop(L, 1);

   lua_getfield(L, opts, "rlen");
   if (lua_isnil(L, -1))
      failf("sampler_desc() missing 'rlen'");
   lua_Integer rlen_val = luaL_checkinteger(L, -1);
   lua_pop(L, 1);

   if (mlen_val < 0 || mlen_val > 15 || rlen_val < 0 || rlen_val > 31)
      failf("invalid sampler_desc() mlen/rlen");

   lua_getfield(L, opts, "header");
   bool header = !lua_isnil(L, -1) && lua_toboolean(L, -1);
   lua_pop(L, 1);

   lua_getfield(L, opts, "hp");
   bool hp = !lua_isnil(L, -1) && lua_toboolean(L, -1);
   lua_pop(L, 1);

   gen_message_desc msg = {
      .msg_length = (uint32_t)mlen_val,
      .response_length = (uint32_t)rlen_val,
      .header_present = header,
   };
   bool valid = false;
   unsigned msg_type =
      gen_sampler_msg_type_from_string(&E.devinfo, op, strlen(op), &valid);
   if (!valid)
      failf("unknown sampler message op '%s'", op);

   gen_sampler_desc smpl = {
      .msg_type = msg_type,
      .simd_mode = sampler_simd_mode((uint32_t)simd_val, hp),
      .bti = bti,
      .sampler_index = sampler,
      .return_hp = hp,
   };

   uint32_t desc = gen_message_desc_encode(&E.devinfo, &msg) |
                   gen_sampler_desc_encode(&E.devinfo, &smpl);
   lua_pushinteger(L, desc);
   return 1;
}

static int
l_dump(lua_State *L)
{
   /* TODO: Use a table to add options for the dump, e.g.
    * starting offset, format, etc.
    */

   if (lua_type(L, 2) != LUA_TNUMBER || !lua_isinteger(L, 2))
      failf("dump() expects a count");

   lua_Integer len_ = lua_tointeger(L, 2);
   if (len_ < 0 || len_ > UINT32_MAX)
      failf("invalid dump count");

   uint32_t len = (uint32_t)len_;

   if (lua_type(L, 1) == LUA_TTABLE) {
      for (uint32_t i = 0; i < len; i++) {
         if (i % 8 == 0)
            printf("[0x%08x]", i * 4);
         lua_rawgeti(L, 1, i);
         lua_Integer val = lua_tointeger(L, -1);
         printf(" 0x%08x", (uint32_t)val);
         lua_pop(L, 1);
         if (i % 8 == 7)
            printf("\n");
      }
      if (len % 8 != 0)
         printf("\n");
      return 0;
   }

   if (!luaL_testudata(L, 1, EXECUTOR_MEM_MT))
      failf("dump() expects a table or memory object");

   executor_mem_region *mem = executor_mem_get_from_lua(L, 1);
   uint32_t size_dw = mem->size / 4;
   executor_check_bounds(size_dw, 0, len, "dump");
   executor_dump_values(mem->map, len, 0);
   return 0;
}

/* TODO: Review numeric limits in the code, specially around Lua integer
 * conversion.
 */

int
main(int argc, char *argv[])
{
   int opt;
   const char *device_pattern = NULL;

   enum {
      OPT_OA_CSV = 1000,
      OPT_OA,
   };

   static const struct option long_options[] = {
       {"help",    no_argument,       0, 'H'},
       {"device",  required_argument, 0, 'd'},
       {"oa-csv", required_argument, 0, OPT_OA_CSV},
       {"oa",     required_argument, 0, OPT_OA},
       {},
   };

   /* The `+` ensures that arguments are not reordered (the default), since
    * the arguments after the script name are made available to the script.
    */
   while ((opt = getopt_long(argc, argv, "+d:h", long_options, NULL)) != -1) {
      switch (opt) {
      case 'd':
         if (!strcmp(optarg, "list")) {
            print_drm_devices();
            return 0;
         }
         device_pattern = optarg;
         break;
      case 'h':
         print_help();
         return 0;
      case 'H':
         open_manual();
         return 0;
      case OPT_OA_CSV:
         E.oa_csv_path = optarg;
         break;
      case OPT_OA:
         E.oa_spec = optarg;
         break;
      default:
         fprintf(stderr, "%s\n", usage_line);
         return 1;
      }
   }

   if (E.oa_spec && !strcmp(E.oa_spec, "list")) {
      E.oa_list = true;
      E.oa_spec = NULL;
   }

   if (E.oa_list && E.oa_csv_path) {
      fprintf(stderr, "%s\n", usage_line);
      fprintf(stderr, "--oa list cannot be combined with --oa-csv\n");
      return 1;
   }

   if (E.oa_csv_path && !E.oa_spec) {
      fprintf(stderr, "%s\n", usage_line);
      fprintf(stderr, "--oa-csv requires --oa\n");
      return 1;
   }

   if (!E.oa_list && optind >= argc) {
      fprintf(stderr, "%s\n", usage_line);
      fprintf(stderr, "expected FILENAME after options\n");
      return 1;
   }

   void *mem_ctx = ralloc_context(NULL);

   const char *filename = optind < argc ? argv[optind] : NULL;

   executor_parse_oa_spec(mem_ctx, E.oa_spec);

   process_intel_debug_variable();

   E.fd = get_drm_device(&E.devinfo, device_pattern);
   if (E.fd < 0)
      failf("Failed to open DRM device");

   fprintf(stderr, "Using device: %s\n", E.devinfo.name);

   isl_device_init(&E.isl_dev, &E.devinfo);
   assert(E.devinfo.kmd_type == INTEL_KMD_TYPE_I915 ||
          E.devinfo.kmd_type == INTEL_KMD_TYPE_XE);

   if (E.oa_csv_path || E.oa_spec || E.oa_list) {
      E.perf_cfg = intel_perf_new(NULL);
      if (!E.perf_cfg)
         failf("failed to allocate Intel perf config");

      E.perf_cfg->vtbl = executor_perf_vtbl;
      intel_perf_init_metrics(E.perf_cfg, &E.devinfo, E.fd,
                              false /* include_pipeline_statistics */,
                              true /* use_register_snapshots */);

      if (!E.oa_list) {
         if (!E.oa_spec_has_colon &&
             executor_find_named_perf_query(E.oa_metric_name) < 0) {
            executor_parse_oa_counter_list(mem_ctx, E.oa_metric_name, E.oa_spec);
            E.oa_metric_name = executor_default_perf_query_name();
         }

         E.perf_query_index = executor_find_perf_query(E.oa_metric_name);
      }

      if (E.perf_query_index < 0) {
         if (E.perf_cfg->features_supported & INTEL_PERF_FEATURE_OA_BLOCKED_BY_POLICY) {
            const char *sysctl = E.devinfo.kmd_type == INTEL_KMD_TYPE_XE ?
               "/proc/sys/dev/xe/observation_paranoid" :
               "/proc/sys/dev/i915/perf_stream_paranoid";
            failf("no OA metric sets available for %s; access is blocked by %s",
                  E.devinfo.name, sysctl);
         }
         failf("no OA metric sets available for %s", E.devinfo.name);
      }

      if (E.oa_list) {
         executor_perf_list();
         close(E.fd);
         intel_perf_free(E.perf_cfg);
         ralloc_free(mem_ctx);
         return 0;
      }

      const struct intel_perf_query_info *query =
         &E.perf_cfg->queries[E.perf_query_index];
      executor_perf_validate_counters(mem_ctx, query);

      fprintf(stderr, "Using OA profile: %s%s%s (%d/%d counters)\n",
              query->symbol_name ? query->symbol_name : query->name,
              query->symbol_name && query->name ? " - " : "",
              query->symbol_name && query->name ? query->name : "",
              executor_perf_count_selected_counters(query), query->n_counters);
   }

   executor_context_setup(&E.ec);

   lua_State *L = luaL_newstate();

   /* TODO: Could be nice to export some kind of builder interface,
    * maybe even let the script construct a shader at the BRW IR
    * level and let the later passes kick in.
    */

   luaL_openlibs(L);

   /* Command line arguments starting from the script name are
    * available to the script to use.
    */
   lua_newtable(L);
   for (int i = optind; i < argc; i++) {
      lua_pushstring(L, argv[i]);
      lua_seti(L, -2, i - optind);
   }
   lua_setglobal(L, "arg");

   /* Add the script's directory to package.path so scripts can require()
    * files from the same directory.
    */
   {
      char *script = ralloc_strdup(mem_ctx, filename);
      const char *script_dir = dirname(script);

      lua_getglobal(L, "package");
      lua_getfield(L, -1, "path");
      const char *original_path = lua_tostring(L, -1);

      const char *new_path =
         ralloc_asprintf(mem_ctx, "%s/?.lua;%s", script_dir, original_path);

      lua_pop(L, 1);
      lua_pushstring(L, new_path);
      lua_setfield(L, -2, "path");
      lua_pop(L, 1);
   }

   lua_newtable(L);
   {
      lua_pushinteger(L, E.devinfo.ver);
      lua_setfield(L, -2, "ver");

      lua_pushinteger(L, E.devinfo.verx10);
      lua_setfield(L, -2, "verx10");

      lua_pushboolean(L, E.devinfo.has_systolic);
      lua_setfield(L, -2, "has_dpas");

      lua_pushboolean(L, E.devinfo.has_bfloat16);
      lua_setfield(L, -2, "has_bfloat16");

      lua_pushinteger(L, intel_device_info_get_max_slm_size(&E.devinfo));
      lua_setfield(L, -2, "max_slm_size");
   }
   lua_setglobal(L, "devinfo");

   static const luaL_Reg mem_methods[] = {
      {"fill",     l_mem_fill},
      {"set",      l_mem_set},
      {"read",     l_mem_read},
      {"to_table", l_mem_to_table},
      {"offset",   l_mem_offset},
      {"addr",     l_mem_addr},
      {"name",     l_mem_name},
      {"dump",     l_mem_dump},
      {"__index",    l_mem_index},
      {"__newindex", l_mem_newindex},
      {"__len",      l_mem_len},
      {NULL, NULL},
   };

   luaL_newmetatable(L, EXECUTOR_MEM_MT);
   luaL_setfuncs(L, mem_methods, 0);
   lua_pop(L, 1);

   static const luaL_Reg surface_methods[] = {
      {"bti", l_surface_bti},
      {NULL, NULL},
   };

   luaL_newmetatable(L, EXECUTOR_SURFACE_MT);
   luaL_setfuncs(L, surface_methods, 0);
   lua_pushvalue(L, -1);
   lua_setfield(L, -2, "__index");
   lua_pop(L, 1);

   static const luaL_Reg sampler_methods[] = {
      {"index", l_sampler_index},
      {NULL, NULL},
   };

   luaL_newmetatable(L, EXECUTOR_SAMPLER_MT);
   luaL_setfuncs(L, sampler_methods, 0);
   lua_pushvalue(L, -1);
   lua_setfield(L, -2, "__index");
   lua_pop(L, 1);

   static const luaL_Reg executor_globals[] = {
      {"alloc",          l_executor_alloc},
      {"surface_buffer", l_executor_surface_buffer},
      {"surface_2d",     l_executor_surface_2d},
      {"sampler",        l_executor_sampler},
      {"execute",        l_execute},
      {"sampler_desc",   l_sampler_desc},
      {"dump",           l_dump},
      {NULL, NULL},
   };

   lua_pushglobaltable(L);
   luaL_setfuncs(L, executor_globals, 0);
   lua_pop(L, 1);

   int err = luaL_loadfile(L, filename);
   if (err)
      failf("failed to load script: %s", lua_tostring(L, -1));

   err = lua_pcall(L, 0, 0, 0);
   if (err)
      failf("failed to run script: %s", lua_tostring(L, -1));

   lua_close(L);

   executor_context_teardown(&E.ec);

   if (E.oa_csv_file) {
      fclose(E.oa_csv_file);
      if (!E.oa_csv_path && E.oa_csv_mem_size > 0)
         fwrite(E.oa_csv_mem, 1, E.oa_csv_mem_size, stdout);
      free(E.oa_csv_mem);
   }

   close(E.fd);

   if (E.perf_cfg)
      intel_perf_free(E.perf_cfg);

   ralloc_free(mem_ctx);

   return 0;
}

void
failf(const char *fmt, ...)
{
   va_list args;
   va_start(args, fmt);
   fprintf(stderr, "ERROR: ");
   vfprintf(stderr, fmt, args);
   fprintf(stderr, "\n");
   va_end(args);
   exit(1);
}
