/*
 * Copyright © 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "util/ralloc.h"

#include <xf86drm.h>
#include "drm-uapi/i915_drm.h"
#include "drm-uapi/xe_drm.h"

#include "intel/compiler/brw_asm.h"
#include "intel/compiler/brw_isa_info.h"
#include "intel/common/intel_gem.h"
#include "intel/common/xe/intel_engine.h"
#include "intel/decoder/intel_decoder.h"
#include "intel/dev/intel_debug.h"

#include "executor.h"

enum {
   /* Predictable base addresses here make it easier to spot errors. */
   EXECUTOR_BO_BATCH_ADDR = 0x10000000,
   EXECUTOR_BO_EXTRA_ADDR = 0x20000000,
   EXECUTOR_BO_DATA_ADDR  = 0x30000000,

   /* Apply to all BOs. */
   EXECUTOR_BO_SIZE = 10 * 1024 * 1024,
};

static void
print_help()
{
   printf(
      "Executes shaders written for Intel GPUs\n"
      "usage: executor FILENAME\n"
      "\n"
      "The input is a Lua script that can perform data manipulation\n"
      "and dispatch execution of compute shaders, written in Xe assembly,\n"
      "the same format used by the brw_asm assembler or when dumping\n"
      "shaders in debug mode.\n"
      "\n"
      "The goal is to have a tool to experiment directly with certain\n"
      "assembly instructions and the shared units without having to\n"
      "instrument the drivers.\n"
      "\n"
      "EXECUTION CONTEXT\n"
      "\n"
      "By default compute shaders are used with SIMD8 for Gfx9-125 and SIMD16\n"
      "for Xe2.  Only a single thread is dispatched.  A data buffer is used to\n"
      "pipe data into the shader and out of it, it is bound to the graphics\n"
      "address 0x%08x.\n"
      "\n"
      "The Gfx versions have differences in their assembly and shared units, so\n"
      "other than very simple examples, scripts for this program will be either\n"
      "specific to a version or provide shader variants for multiple versions.\n"
      "\n"
      "ASSEMBLY MACROS\n"
      "\n"
      "In addition to regular instructions, the follow macros will generate\n"
      "assembly code based on the Gfx version being executed.  Unlike in regular\n"
      "instructions, REGs don't use regions and can't be immediates.\n"
      "\n"
      "- @eot\n"
      "  Send an EOT message.\n"
      "\n"
      "- @mov REG IMM\n"
      "  Like a regular MOV but accepts numbers in both decimal and\n"
      "  floating-point.\n"
      "\n"
      "- @id REG\n"
      "  Write a local invocation index into REG.\n"
      "\n"
      "- @read DST_REG OFFSET_REG\n"
      "  Read 32-bit values from the memory buffer at OFFSET_REG into DST_REG.\n"
      "\n"
      "- @write OFFSET_REG SRC_REG\n"
      "  Write 32-bit values from SRC_REG to the memory buffer at OFFSET_REG.\n"
      "\n"
      "- @syncnop\n"
      "  Produce a coarse grained sync.nop (when applicable) to ensure data from\n"
      "  macros above are read/written.\n"
      "\n"
      "LUA ENVIRONMENT\n"
      "\n"
      "In addition to the regular Lua standard library the following variables and.\n"
      "functions are available.\n"
      "\n"
      "- execute({src=STR, data=ARRAY}) -> ARRAY\n"
      "  Takes a table as argument.  The 'src' in the table contains the shader to be\n"
      "  executed.  The 'data' argument will be used to fill the data buffer with 32-bit\n"
      "  values.  The function returns an ARRAY with the contents of the data buffer\n"
      "  after the shader completes.\n"
      "\n"
      "- dump(ARRAY, COUNT)\n"
      "  Pretty print the COUNT first elements of an array of 32-bit values.\n"
      "\n"
      "- check_ver(V, ...), check_verx10(V, ...)\n"
      "  Exit if the Gfx version being executed isn't in the arguments list.\n"
      "\n"
      "- ver, verx10\n"
      "  Variables containing the Gfx version being executed.\n"
      "\n"
      "This program was compiled with %s.\n"
      "\n"
      "ENVIRONMENT VARIABLES\n"
      "\n"
      "The following INTEL_DEBUG values (comma separated) are used:\n"
      "\n"
      " - bat             Dumps the batch buffer.\n"
      " - color           Uses colors for the batch buffer dump.\n"
      " - cs              Dumps the source after macro processing\n"
      "                   the final assembly.\n"
      "\n"
      "EXAMPLE\n"
      "\n"
      "The following script\n"
      "\n"
      "  local r = execute {\n"
      "    data={ [42] = 0x100 },\n"
      "    src=[[\n"
      "      @mov     g1      42\n"
      "      @read    g2      g1\n"
      "\n"
      "      @id      g3\n"
      "\n"
      "      add(8)   g4<1>UD  g2<8,8,1>UD  g3<8,8,1>UD  { align1 @1 1Q };\n"
      "\n"
      "      @write   g3       g4\n"
      "      @eot\n"
      "    ]]\n"
      "  }\n"
      "\n"
      "  dump(r, 4)\n"
      "\n"
      "Will produce the following output\n"
      "\n"
      "   [0x00000000] 0x00000100 0x00000101 0x00000102 0x00000103\n"
      "\n"
      "More examples can be found in the examples/ directory in the source code.\n"
      "\n", EXECUTOR_BO_DATA_ADDR, LUA_RELEASE);
}

static struct {
   struct intel_device_info devinfo;
   struct isl_device isl_dev;
   struct brw_isa_info isa;
   int fd;
} E;

#define genX_call(func, ...)                                \
   switch (E.devinfo.verx10) {                              \
   case 90:  gfx9_  ##func(__VA_ARGS__); break;             \
   case 110: gfx11_ ##func(__VA_ARGS__); break;             \
   case 120: gfx12_ ##func(__VA_ARGS__); break;             \
   case 125: gfx125_##func(__VA_ARGS__); break;             \
   case 200: gfx20_ ##func(__VA_ARGS__); break;             \
   default: unreachable("Unsupported hardware generation"); \
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
   void *r = bo->cursor;
   if (alignment) {
      r = (void *)(((uintptr_t)r + alignment-1) & ~((uintptr_t)alignment-1));
   }
   bo->cursor = r + size;
   return r;
}

executor_address
executor_address_of_ptr(executor_bo *bo, void *ptr)
{
   return (executor_address){ptr - bo->map + bo->addr};
}

static int
get_drm_device(struct intel_device_info *devinfo)
{
   drmDevicePtr devices[8];
   int max_devices = drmGetDevices2(0, devices, 8);

   int i, fd = -1;
   for (i = 0; i < max_devices; i++) {
      if (devices[i]->available_nodes & 1 << DRM_NODE_RENDER &&
          devices[i]->bustype == DRM_BUS_PCI &&
          devices[i]->deviceinfo.pci->vendor_id == 0x8086) {
         fd = open(devices[i]->nodes[DRM_NODE_RENDER], O_RDWR | O_CLOEXEC);
         if (fd < 0)
            continue;

         if (!intel_get_device_info_from_fd(fd, devinfo, -1, -1) ||
             devinfo->ver < 8) {
            close(fd);
            fd = -1;
            continue;
         }

         /* Found a device! */
         break;
      }
   }
   drmFreeDevices(devices, max_devices);

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
   }

   return bo;
}

static unsigned
decode_get_state_size(void *_ec, uint64_t address, uint64_t base_address)
{
   return EXECUTOR_BO_SIZE;
}

static void
parse_execute_data(executor_context *ec, lua_State *L, int table_idx)
{
   uint32_t *data = ec->bo.data.map;

   lua_pushvalue(L, table_idx);

   lua_pushnil(L);
   while (lua_next(L, -2) != 0) {
      int val_idx = lua_gettop(L);
      int key_idx = val_idx - 1;

      if (lua_type(L, key_idx) != LUA_TNUMBER || !lua_isinteger(L, key_idx))
         failf("invalid key for data in execute call");

      lua_Integer key = lua_tointeger(L, key_idx);
      assert(key <= 10 * 1024 * 1024 / 4);
      lua_Integer val = lua_tointeger(L, val_idx);
      data[key] = val;

      lua_pop(L, 1);
   }

   lua_pop(L, 1);
}

static void
parse_execute_args(executor_context *ec, lua_State *L, executor_params *params)
{
   int opts = lua_gettop(L);

   lua_pushnil(L);

   while (lua_next(L, opts) != 0) {
      int val_idx = lua_gettop(L);
      int key_idx = val_idx - 1;

      if (lua_type(L, key_idx) != LUA_TSTRING) {
         lua_pop(L, 1);
         continue;
      }

      const char *key = lua_tostring(L, key_idx);

      if (!strcmp(key, "src")) {
         params->original_src = ralloc_strdup(ec->mem_ctx, luaL_checkstring(L, val_idx));
      } else if (!strcmp(key, "data")) {
         parse_execute_data(ec, L, val_idx);
      } else {
         failf("unknown parameter '%s' for execute()", key);
      }

      lua_pop(L, 1);
   }
}

static void
executor_context_setup(executor_context *ec)
{
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

   executor_create_bo(ec, &ec->bo.batch, EXECUTOR_BO_BATCH_ADDR, EXECUTOR_BO_SIZE);
   executor_create_bo(ec, &ec->bo.extra, EXECUTOR_BO_EXTRA_ADDR, EXECUTOR_BO_SIZE);
   executor_create_bo(ec, &ec->bo.data,  EXECUTOR_BO_DATA_ADDR, EXECUTOR_BO_SIZE);

   uint32_t *data = ec->bo.data.map;
   for (int i = 0; i < EXECUTOR_BO_SIZE / 4; i++)
      data[i] = 0xABABABAB;
}

static void
executor_context_dispatch(executor_context *ec)
{
   if (ec->devinfo->kmd_type == INTEL_KMD_TYPE_I915) {
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
      };

      struct drm_i915_gem_execbuffer2 exec = {0};
      exec.buffers_ptr = (uintptr_t)objs;
      exec.buffer_count = ARRAY_SIZE(objs);
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
      };

      struct drm_xe_sync bind_syncs[] = {
         {
            .type   = DRM_XE_SYNC_TYPE_SYNCOBJ,
            .handle = sync_handles[0],
            .flags  = DRM_XE_SYNC_FLAG_SIGNAL,
         },
      };

      struct drm_xe_vm_bind bind = {
         .vm_id           = ec->xe.vm_id,
         .num_binds       = ARRAY_SIZE(bind_ops),
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
            .handle = sync_handles[0],
         },
         {
            .type   = DRM_XE_SYNC_TYPE_SYNCOBJ,
            .handle = sync_handles[1],
            .flags  = DRM_XE_SYNC_FLAG_SIGNAL,
         }
      };

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
   }
}

static void
executor_context_teardown(executor_context *ec)
{
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
}

static int
l_execute(lua_State *L)
{
   executor_context ec = {
      .mem_ctx = ralloc_context(NULL),
      .devinfo = &E.devinfo,
      .isl_dev = &E.isl_dev,
      .fd      = E.fd,
   };

   executor_context_setup(&ec);

   executor_params params = {0};

   {
      if (lua_gettop(L) != 1)
         failf("execute() must have a single table argument");

      parse_execute_args(&ec, L, &params);

      const char *src = executor_apply_macros(&ec, params.original_src);

      FILE *f = fmemopen((void *)src, strlen(src), "r");

      brw_assemble_flags flags = 0;

      if (INTEL_DEBUG(DEBUG_CS)) {
         printf("=== Processed assembly source ===\n"
                "%s"
                "=================================\n\n", src);
         flags = BRW_ASSEMBLE_DUMP;
      }

      brw_assemble_result asm = brw_assemble(ec.mem_ctx, ec.devinfo, f, "", flags);
      fclose(f);

      if (!asm.bin)
         failf("assembler failure");

      params.kernel_bin = asm.bin;
      params.kernel_size = asm.bin_size;
   }

   genX_call(emit_execute, &ec, &params);

   if (INTEL_DEBUG(DEBUG_BATCH)) {
      struct intel_batch_decode_ctx decoder;
      enum intel_batch_decode_flags flags = INTEL_BATCH_DECODE_DEFAULT_FLAGS;
      if (INTEL_DEBUG(DEBUG_COLOR))
         flags |= INTEL_BATCH_DECODE_IN_COLOR;

      intel_batch_decode_ctx_init_brw(&decoder, &E.isa, &E.devinfo, stdout,
                                      flags, NULL, decode_get_bo, decode_get_state_size, &ec);

      assert(ec.bo.batch.cursor > ec.bo.batch.map);
      const int batch_offset = ec.batch_start - ec.bo.batch.addr;
      const int batch_size = (ec.bo.batch.cursor - ec.bo.batch.map) - batch_offset;
      assert(batch_offset < batch_size);

      intel_print_batch(&decoder, ec.bo.batch.map, batch_size, ec.batch_start, false);

      intel_batch_decode_ctx_finish(&decoder);
   }

   executor_context_dispatch(&ec);

   {
      /* TODO: Use userdata to return a wrapped C array instead of building
       * values.  Could make integration with array operations better.
       */
      uint32_t *data = ec.bo.data.map;
      const int n = ec.bo.data.size / 4;
      lua_createtable(L, n, 0);
      for (int i = 0; i < 8; i++) {
         lua_pushinteger(L, data[i]);
         lua_seti(L, -2, i);
      }
   }

   executor_context_teardown(&ec);
   ralloc_free(ec.mem_ctx);

   return 1;
}

static int
l_dump(lua_State *L)
{
   /* TODO: Use a table to add options for the dump, e.g.
    * starting offset, format, etc.
    */

   assert(lua_type(L, 1) == LUA_TTABLE);
   assert(lua_type(L, 2) == LUA_TNUMBER);
   assert(lua_isinteger(L, 2));

   lua_Integer len_ = lua_tointeger(L, 2);
   assert(len_ >= 0 && len_ <= INT_MAX);
   int len = len_;

   int i;
   for (i = 0; i < len; i++) {
      if (i%8 == 0) printf("[0x%08x]", i * 4);
      lua_rawgeti(L, 1, i);
      lua_Integer val = lua_tointeger(L, -1);
      printf(" 0x%08x", (uint32_t)val);
      lua_pop(L, 1);
      if (i%8 == 7) printf("\n");
   }
   if (i%8 != 0) printf("\n");
   return 0;
}

static int
l_check_ver(lua_State *L)
{
   int top = lua_gettop(L);
   for (int i = 1; i <= top; i++) {
      lua_Integer v = luaL_checknumber(L, i);
      if (E.devinfo.ver == v) {
         return 0;
      }
   }
   failf("script doesn't support version=%d verx10=%d\n",
         E.devinfo.ver, E.devinfo.verx10);
   return 0;
}

static int
l_check_verx10(lua_State *L)
{
   int top = lua_gettop(L);
   for (int i = 1; i <= top; i++) {
      lua_Integer v = luaL_checknumber(L, i);
      if (E.devinfo.verx10 == v) {
         return 0;
      }
   }
   failf("script doesn't support version=%d verx10=%d\n",
         E.devinfo.ver, E.devinfo.verx10);
   return 0;
}

/* TODO: Review numeric limits in the code, specially around Lua integer
 * conversion.
 */

int
main(int argc, char *argv[])
{
   if (argc < 2 ||
       !strcmp(argv[1], "--help") ||
       !strcmp(argv[1], "-help") ||
       !strcmp(argv[1], "-h") ||
       !strcmp(argv[1], "help")) {
      print_help();
      return 0;
   }

   if (argc > 2) {
      /* TODO: Expose extra arguments to the script as a variable. */
      failf("invalid extra arguments\nusage: executor FILENAME");
      return 1;
   }

   process_intel_debug_variable();

   E.fd = get_drm_device(&E.devinfo);
   isl_device_init(&E.isl_dev, &E.devinfo);
   brw_init_isa_info(&E.isa, &E.devinfo);
   assert(E.devinfo.kmd_type == INTEL_KMD_TYPE_I915 ||
          E.devinfo.kmd_type == INTEL_KMD_TYPE_XE);

   lua_State *L = luaL_newstate();

   /* TODO: Could be nice to export some kind of builder interface,
    * maybe even let the script construct a shader at the BRW IR
    * level and let the later passes kick in.
    */

   luaL_openlibs(L);

   lua_pushinteger(L, E.devinfo.ver);
   lua_setglobal(L, "ver");

   lua_pushinteger(L, E.devinfo.verx10);
   lua_setglobal(L, "verx10");

   lua_pushcfunction(L, l_execute);
   lua_setglobal(L, "execute");

   lua_pushcfunction(L, l_dump);
   lua_setglobal(L, "dump");

   lua_pushcfunction(L, l_check_ver);
   lua_setglobal(L, "check_ver");

   lua_pushcfunction(L, l_check_verx10);
   lua_setglobal(L, "check_verx10");

   const char *filename = argv[1];
   int err = luaL_loadfile(L, filename);
   if (err)
      failf("failed to load script: %s", lua_tostring(L, -1));

   err = lua_pcall(L, 0, 0, 0);
   if (err)
      failf("failed to run script: %s", lua_tostring(L, -1));

   lua_close(L);
   close(E.fd);

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
