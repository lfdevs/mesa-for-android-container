/*
 * Copyright 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "intel/perf/intel_perf_private.h"
#include "intel_hang_replay_xe.h"
#include "drm-uapi/xe_drm.h"
#include "common/intel_gem.h"
#include "intel_hang_replay_lib.h"

static int syncobj_wait(int drm_fd, uint32_t *handles, uint32_t count, uint64_t abs_timeout_nsec,
                        uint32_t flags)
{
   struct drm_syncobj_wait wait = {};
   int err = 0;

   wait.handles = to_user_pointer(handles);
   wait.timeout_nsec = abs_timeout_nsec;
   wait.count_handles = count;
   wait.flags = flags;

   if (intel_ioctl(drm_fd, DRM_IOCTL_SYNCOBJ_WAIT, &wait))
      err = -errno;

   return err;
}

static int
syncobj_create(int drm_fd)
{
   struct drm_syncobj_create create = {};

   if (intel_ioctl(drm_fd, DRM_IOCTL_SYNCOBJ_CREATE, &create))
      return -errno;

   return create.handle;
}

static int
syncobj_destroy(int drm_fd, uint32_t handle)
{
   struct drm_syncobj_destroy destroy = {};
   int err = 0;

   destroy.handle = handle;
   if (intel_ioctl(drm_fd, DRM_IOCTL_SYNCOBJ_DESTROY, &destroy))
      err = -errno;

   return err;
}

static int
syncobj_reset(int drm_fd, uint32_t *handles, uint32_t count)
{
   struct drm_syncobj_array array = {};
   int err = 0;

   array.handles = to_user_pointer(handles);
   array.count_handles = count;
   if (intel_ioctl(drm_fd, DRM_IOCTL_SYNCOBJ_RESET, &array))
      err = -errno;

   return err;
}

static int
xe_bo_create(int drm_fd, uint32_t vm, struct gem_bo *bo)
{
   struct drm_xe_gem_create create = {
      .vm_id = vm,
      .size = bo->size,
      .placement = bo->props.mem_region,
      .flags = 0,
      .cpu_caching = bo->props.cpu_caching,
   };

   if (intel_ioctl(drm_fd, DRM_IOCTL_XE_GEM_CREATE, &create))
      return -errno;

   return create.handle;
}

static int
xe_bo_destroy(int drm_fd, uint32_t bo_handle)
{
   struct drm_gem_close close_bo = {
      .handle = bo_handle,
   };

   if (intel_ioctl(drm_fd, DRM_IOCTL_GEM_CLOSE, &close_bo))
      return -errno;

   return 0;
}

static int
xe_vm_bind(int drm_fd, uint32_t vm, struct gem_bo *bo, uint64_t obj_offset, uint32_t op, uint32_t flags,
           struct drm_xe_sync *sync, uint32_t num_syncs)
{
   struct drm_xe_vm_bind bind = {
      .vm_id = vm,
      .num_binds = 1,
      .bind.obj = bo->gem_handle,
      .bind.obj_offset = obj_offset,
      .bind.range = bo->size,
      .bind.addr = bo->offset,
      .bind.op = op,
      .bind.flags = flags,
      .num_syncs = num_syncs,
      .syncs = (uintptr_t)sync,
      .bind.pat_index = bo->props.pat_index,
   };

   if (intel_ioctl(drm_fd, DRM_IOCTL_XE_VM_BIND, &bind))
      return -errno;

   return 0;
}

static uint32_t
xe_vm_create(int drm_fd, uint32_t flags)
{
   struct drm_xe_vm_create create = {
      .flags = flags,
   };

   if (flags & DRM_XE_VM_CREATE_FLAG_LR_MODE) {
      fprintf(stderr, "Long running VM is not supported, aborting.\n");
      exit(EXIT_FAILURE);
   }

   if (intel_ioctl(drm_fd, DRM_IOCTL_XE_VM_CREATE, &create)) {
      fprintf(stderr, "vm creation failed, aborting\n");
      exit(EXIT_FAILURE);
   }

   return create.vm_id;
}

static int
xe_vm_destroy(int drm_fd, uint32_t vm)
{
   struct drm_xe_vm_destroy destroy = {
      .vm_id = vm,
   };

   if (intel_ioctl(drm_fd, DRM_IOCTL_XE_VM_DESTROY, &destroy))
      return -errno;

   return 0;
}

static uint32_t
xe_exec_queue_create(int drm_fd, uint32_t vm, uint16_t width, uint16_t num_placements,
                     struct drm_xe_engine_class_instance *instance, uint64_t ext)
{
   struct drm_xe_exec_queue_create create = {
      .extensions = ext,
      .vm_id = vm,
      .width = width,
      .num_placements = num_placements,
      .instances = to_user_pointer(instance),
   };

   if (intel_ioctl(drm_fd, DRM_IOCTL_XE_EXEC_QUEUE_CREATE, &create)) {
      fprintf(stderr, "exec_queue creation failed, aborting\n");
      exit(EXIT_FAILURE);
   }

   return create.exec_queue_id;
}

static int
xe_exec_queue_destroy(int drm_fd, uint32_t exec_queue)
{
   struct drm_xe_exec_queue_destroy destroy = {
      .exec_queue_id = exec_queue,
   };

   if (intel_ioctl(drm_fd, DRM_IOCTL_XE_EXEC_QUEUE_DESTROY, &destroy))
      return -errno;

   return 0;
}

static void*
gem_xe_mmap_offset(int drm_fd, uint32_t bo, size_t size, uint32_t pat_index,
                   const struct intel_device_info *devinfo)
{
   void *addr = MAP_FAILED;

   struct drm_xe_gem_mmap_offset mmo = {
      .handle = bo,
   };

   if (pat_index == devinfo->pat.compressed.index ||
       pat_index == devinfo->pat.compressed_scanout.index) {
      fprintf(stderr,
              "Warning: compressed BOs (PAT index %u) are not supported at the moment.\n"
              "Effort to support compressed BOs: https://patchwork.freedesktop.org/patch/663902\n",
              pat_index);
   }

   /* Get the fake offset back */
   if (intel_ioctl(drm_fd, DRM_IOCTL_XE_GEM_MMAP_OFFSET, &mmo) == 0)
      addr = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, drm_fd, mmo.offset);

   if (addr == MAP_FAILED)
      fprintf(stderr, "xe GEM mmap failed\n");

   return addr;
}

static void
write_xe_bo_data(int drm_fd, uint32_t bo, int file_fd, size_t size, uint32_t pat_index,
                 const struct intel_device_info *devinfo)
{
   void *map = gem_xe_mmap_offset(drm_fd, bo, size, pat_index, devinfo);
   assert(map != MAP_FAILED);

   write_malloc_data(map, file_fd, size);

   munmap(map, size);
}

static void *
load_userptr_data(int file_fd, uint64_t bo_size)
{
   void *map = malloc(bo_size);
   if (!map) {
      fprintf(stderr, "Failed to allocate memory for USERPTR BO\n");
      return NULL;
   }
   write_malloc_data(map, file_fd, bo_size);

   return map;
}


static uint32_t
xe_create_exec_queue_and_set_hw_image(int drm_fd, uint32_t vm, const void *hw_img_data,
                                      uint32_t img_size)
{
   /* TODO: add additional information in the intel_hang_dump_block_exec &
    * intel_hang_dump_block_hw_image structures to specify the engine and use
    * the correct engine here. For now let's use the Render engine.
    */
   struct drm_xe_engine_class_instance instance = {
      .engine_class = DRM_XE_ENGINE_CLASS_RENDER,
   };

   struct drm_xe_ext_set_property ext = {
      .base.next_extension = 0,
      .base.name = DRM_XE_EXEC_QUEUE_EXTENSION_SET_PROPERTY,
      .property = DRM_XE_EXEC_QUEUE_SET_HANG_REPLAY_STATE,
      .value = img_size,
      .ptr = (uint64_t)(uintptr_t)hw_img_data,
   };

   return xe_exec_queue_create(drm_fd, vm, 1, 1, &instance, to_user_pointer(&ext));
}

static int
xe_exec(int drm_fd, struct drm_xe_exec *exec)
{
   if (intel_ioctl(drm_fd, DRM_IOCTL_XE_EXEC, exec)) {
      fprintf(stderr, "xe_exec failed, aborting\n");
      exit(EXIT_FAILURE);
   }

   return 0;
}

bool
process_xe_dmp_file(int file_fd, int drm_fd, const struct intel_device_info *devinfo,
                    struct util_dynarray *buffers, void *mem_ctx,
                    struct intel_hang_dump_block_exec *init,
                    struct intel_hang_dump_block_exec *block_exec,
                    uint32_t vm_flags, uint32_t bo_dumpable, bool scratch)
{
   void *hw_img = NULL;
   uint32_t hw_img_size = 0;
   uint32_t exec_queue = 0;
   struct drm_xe_sync sync = {
      .type = DRM_XE_SYNC_TYPE_SYNCOBJ,
      .flags = DRM_XE_SYNC_FLAG_SIGNAL,
      .handle = syncobj_create(drm_fd),
   };
   struct drm_xe_exec exec = {
       .num_syncs = 1,
       .syncs = to_user_pointer(&sync),
       .num_batch_buffer = 1,
   };
   const uint32_t dumpable_bit = bo_dumpable ? DRM_XE_VM_BIND_FLAG_DUMPABLE : 0;

   if (scratch && vm_flags != (vm_flags | DRM_XE_VM_CREATE_FLAG_SCRATCH_PAGE)) {
      vm_flags |= DRM_XE_VM_CREATE_FLAG_SCRATCH_PAGE;
      fprintf(stderr, "DRM_XE_VM_CREATE_FLAG_SCRATCH_PAGE flag is now being set.\n");
   }

   uint32_t vm = xe_vm_create(drm_fd, vm_flags);

   /* Allocate BOs populate them */
   int i = 0;
   int bo_counter = 0;
   util_dynarray_foreach(buffers, struct gem_bo, bo) {
      if (!bo->hw_img)
         bo_counter++;
   }
   util_dynarray_foreach(buffers, struct gem_bo, bo) {
      uint32_t ops = 0;
      uint32_t flags = dumpable_bit;
      uint64_t obj_offset = 0;
      int ret;

      lseek(file_fd, bo->file_offset, SEEK_SET);

      if (bo->hw_img) {
         hw_img = malloc(bo->size);
         write_malloc_data(hw_img, file_fd, bo->size);
         hw_img_size = bo->size;
         continue;
      }

      if (bo->props.mem_type == INTEL_HANG_DUMP_BLOCK_MEM_TYPE_NULL_SPARSE) {
         ops = DRM_XE_VM_BIND_OP_MAP;
         flags = DRM_XE_VM_BIND_FLAG_NULL;
         bo->gem_handle = 0;
      } else if (bo->props.mem_permission == INTEL_HANG_DUMP_BLOCK_MEM_TYPE_READ_ONLY) {
         ret = bo->gem_handle = xe_bo_create(drm_fd, vm, bo);
         if (ret < 0) {
            fprintf(stderr, "Failed to create BO for read-only block (addr: 0x%llx, size: 0x%llx). Exiting. Error: %d\n",
                   (unsigned long long)bo->offset, (unsigned long long)bo->size, ret);
            syncobj_destroy(drm_fd, sync.handle);
            xe_vm_destroy(drm_fd, vm);
            return EXIT_FAILURE;
         }
         write_xe_bo_data(drm_fd, bo->gem_handle, file_fd, bo->size, bo->props.pat_index, devinfo);
         ops = DRM_XE_VM_BIND_OP_MAP;
         flags |= DRM_XE_VM_BIND_FLAG_READONLY;
      } else if (bo->props.mem_type == INTEL_HANG_DUMP_BLOCK_MEM_TYPE_USERPTR) {
         ops = DRM_XE_VM_BIND_OP_MAP_USERPTR;

         /* Allocate host memory and load BO content into it */
         void *map = load_userptr_data(file_fd, bo->size);
         if (!map) {
             fprintf(stderr, "Failed to allocate/load USERPTR BO data, skipping bind.\n");
             continue;
         }
         bo->offset = (uint64_t)(uintptr_t)map;
         bo->gem_handle = 0;
      } else {
         ret = bo->gem_handle = xe_bo_create(drm_fd, vm, bo);
         if (ret < 0) {
            fprintf(stderr, "Failed to create BO (addr: 0x%llx, size: 0x%llx). Exiting. Error: %d\n",
                   (unsigned long long)bo->offset, (unsigned long long)bo->size, ret);
            syncobj_destroy(drm_fd, sync.handle);
            xe_vm_destroy(drm_fd, vm);
            return EXIT_FAILURE;
         }
         write_xe_bo_data(drm_fd, bo->gem_handle, file_fd, bo->size, bo->props.pat_index, devinfo);
         ops = DRM_XE_VM_BIND_OP_MAP;
      }
      i++;
      ret = xe_vm_bind(drm_fd, vm, bo, obj_offset, ops, flags, &sync, i == bo_counter ? 1 : 0);
      if (ret < 0) {
         fprintf(stderr, "Failed to bind BO (addr: 0x%llx) to VM. Exiting. Error: %d\n",
                 (unsigned long long)bo->offset, ret);
         syncobj_destroy(drm_fd, sync.handle);
         xe_vm_destroy(drm_fd, vm);
         return EXIT_FAILURE;
      }
   }

   if (hw_img) {
      exec_queue = xe_create_exec_queue_and_set_hw_image(drm_fd, vm, hw_img, hw_img_size);
      if (exec_queue == 0) {
         fprintf(stderr, "error: dump file didn't include a hw image context, exiting... %s\n", strerror(errno));
            return EXIT_FAILURE;
      }
   }

   exec.exec_queue_id = exec_queue;
   exec.address = block_exec->offset;

   /* wait for last bind */
   syncobj_wait(drm_fd, &sync.handle, 1, INT64_MAX, 0);
   syncobj_reset(drm_fd, &sync.handle, 1);

   xe_exec(drm_fd, &exec);
   syncobj_wait(drm_fd, &sync.handle, 1, INT64_MAX, 0);

   syncobj_destroy(drm_fd, sync.handle);
   xe_exec_queue_destroy(drm_fd, exec.exec_queue_id);
   xe_vm_destroy(drm_fd, vm);

   if (hw_img)
      free(hw_img);

   /* Clean up GEM BO handles created during replay. */
   util_dynarray_foreach(buffers, struct gem_bo, bo) {
      if (bo->gem_handle != 0) {
         xe_bo_destroy(drm_fd, bo->gem_handle);
      }
   }

   /* Clean up host memory allocated for USERPTR binds. */
   util_dynarray_foreach(buffers, struct gem_bo, bo) {
      if (bo->props.mem_type == INTEL_HANG_DUMP_BLOCK_MEM_TYPE_USERPTR) {
         /* bo->offset holds the host memory address (map) */
         void *map = (void *)(uintptr_t)bo->offset;
         free(map);
      }
   }

   return EXIT_SUCCESS;
}
