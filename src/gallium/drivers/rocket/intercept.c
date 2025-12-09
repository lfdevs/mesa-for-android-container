/*
 * Copyright (c) 2025 Tomeu Vizoso <tomeu@tomeuvizoso.net>
 * SPDX-License-Identifier: MIT
 */

#include <dlfcn.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "drm-uapi/rknpu_ioctl.h"
#include "rkt_registers.h"

// #define GETENV 1

struct bo {
   int handle;
   unsigned size;
   uint64_t obj_addr;
   uint64_t dma_addr;
};

#define MAX_BOS 3000

struct context {
   int dump_file;
   int device_fd;
   struct bo bos[MAX_BOS];
   unsigned next_handle_id;
};

struct context context = {0};

static void
dump_log(const char *format, ...)
{
   va_list args;
   va_start(args, format);

   int dump_fd = open("rknpu.log", O_CREAT | O_RDWR | O_APPEND,
                      S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
   vdprintf(dump_fd, format, args);
   close(dump_fd);

   va_end(args);
}

#define L(...) dump_log(__VA_ARGS__);

static void *
map_bo(struct bo *bo)
{
   struct rknpu_mem_map req = {0};

   req.handle = bo->handle;
   ioctl(context.device_fd, DRM_IOCTL_RKNPU_MEM_MAP, &req);
   return mmap(NULL, bo->size, PROT_READ | PROT_WRITE, MAP_SHARED,
               context.device_fd, req.offset);
}

static struct bo *
find_bo(uint64_t dma_address, unsigned *offset)
{
   for (int j = 0; j < context.next_handle_id; j++) {
      fprintf(stderr, "needle %lx hay %lx i %d\n", dma_address,
              context.bos[j].dma_addr, j);
      if (dma_address >= context.bos[j].dma_addr &&
          dma_address < context.bos[j].dma_addr + context.bos[j].size) {
         *offset = dma_address - context.bos[j].dma_addr;
         return &context.bos[j];
      }
   }

   return NULL;
}

static void
dump_buffer(const char *name, uint64_t dma_address, unsigned size)
{
   unsigned offset = 0;
   struct bo *bo = find_bo(dma_address, &offset);

   fprintf(stderr, "dump_buffer name %s dma 0x%lx size %u bo %p\n", name,
           dma_address, size, bo);

   if (size == 0 || size + offset > bo->size)
      size = bo->size - offset;

   int fd = open(name, O_CREAT | O_RDWR | O_TRUNC,
                 S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
   write(fd, map_bo(bo) + offset, size);
   close(fd);
}

static unsigned task_id = 0;

static int
handle_submit(struct rknpu_submit *args, uint32_t *output_address)
{
   int ret = 0;

   L("struct rknpu_submit submit = {\n");
   L("   .flags = %x,\n", args->flags);
   L("   .timeout = %d,\n", args->timeout);
   L("   .task_start = %d,\n", args->task_start);
   L("   .task_number = %d,\n", args->task_number);
   L("   .task_counter = %d,\n", args->task_counter);
   L("   .priority = %d,\n", args->priority);
   L("   .task_obj_addr = 0x%llx,\n", args->task_obj_addr);
   L("   .regcfg_obj_addr = 0x%llx,\n", args->regcfg_obj_addr);
   L("   .task_base_addr = 0x%llx,\n", args->task_base_addr);
   L("   .user_data = 0x%llx,\n", args->user_data);
   L("   .core_mask = %x,\n", args->core_mask);
   L("   .fence_fd = %d,\n", args->fence_fd);
   L("   .subcore_task = {\n");
   L("      {\n");
   L("         .task_start = %d,\n", args->subcore_task[0].task_start);
   L("         .task_number = %d,\n", args->subcore_task[0].task_number);
   L("      },\n");
   L("      {\n");
   L("         .task_start = %d,\n", args->subcore_task[1].task_start);
   L("         .task_number = %d,\n", args->subcore_task[1].task_number);
   L("      },\n");
   L("      {\n");
   L("         .task_start = %d,\n", args->subcore_task[2].task_start);
   L("         .task_number = %d,\n", args->subcore_task[2].task_number);
   L("      },\n");
   L("   },\n");
   L("};\n");

   struct bo *task_bo = NULL;
   for (int i = 0; i < context.next_handle_id; i++) {
      if (context.bos[i].obj_addr == args->task_obj_addr) {
         task_bo = &context.bos[i];
         break;
      }
   }

   struct rknpu_task *tasks = map_bo(task_bo);
   for (int i = args->task_start; i < args->task_start + args->task_number / 3;
        i++) {
      L("tasks[%d].flags = 0x%x;\n", i, tasks[i].flags);
      L("tasks[%d].op_idx = %d;\n", i, tasks[i].op_idx);
      L("tasks[%d].enable_mask = 0x%x;\n", i, tasks[i].enable_mask);
      L("tasks[%d].int_mask = 0x%x;\n", i, tasks[i].int_mask);
      L("tasks[%d].int_clear = 0x%x;\n", i, tasks[i].int_clear);
      L("tasks[%d].regcfg_amount = %d;\n", i, tasks[i].regcfg_amount);
      L("tasks[%d].regcfg_offset = 0x%x;\n", i, tasks[i].regcfg_offset);
      L("tasks[%d].regcmd_addr = 0x%llx;\n", i, tasks[i].regcmd_addr);

      if (tasks[i].regcmd_addr == 0x0)
         continue;

      char name[PATH_MAX];
      unsigned size = (tasks[i].regcfg_amount + RKNPU_PC_DATA_EXTRA_AMOUNT) *
                      sizeof(uint64_t);
      sprintf(name, "regcmd%d.bin", task_id);
      dump_buffer(name, tasks[i].regcmd_addr + tasks[i].regcfg_offset, size);

      uint32_t input_address = 0x0;
      *output_address = 0x0;
      uint32_t weights_address = 0x0;
      uint32_t biases_address = 0x0;
      uint32_t eltwise_address = 0x0;

      unsigned offset = 0;
      struct bo *bo =
         find_bo(tasks[i].regcmd_addr + tasks[i].regcfg_offset, &offset);
      uint64_t *regcmd = map_bo(bo) + offset;
      for (int j = 0; j < tasks[i].regcfg_amount + RKNPU_PC_DATA_EXTRA_AMOUNT;
           j++) {
         switch (regcmd[j] & 0xffff) {
         case REG_CNA_FEATURE_DATA_ADDR:
            input_address = (regcmd[j] & 0xffffffff0000) >> 16;
            break;
         case REG_CNA_DCOMP_ADDR0:
            weights_address = (regcmd[j] & 0xffffffff0000) >> 16;
            break;
         case REG_DPU_DST_BASE_ADDR:
            if (*output_address == 0x0)
               *output_address = (regcmd[j] & 0xffffffff0000) >> 16;
            break;
         case REG_DPU_RDMA_RDMA_BS_BASE_ADDR:
            biases_address = (regcmd[j] & 0xffffffff0000) >> 16;
            break;
         case REG_DPU_RDMA_RDMA_EW_BASE_ADDR:
            eltwise_address = (regcmd[j] & 0xffffffff0000) >> 16;
            break;
         }
      }

      fprintf(stderr, "weights_address %x\n", weights_address);
      fprintf(stderr, "input_address %x\n", input_address);
      fprintf(stderr, "output_address %x\n", *output_address);
      fprintf(stderr, "biases_address %x\n", biases_address);
      fprintf(stderr, "eltwise_address %x\n", eltwise_address);

      if (weights_address != 0x0) {
         sprintf(name, "weights%d.bin", task_id);
         dump_buffer(name, weights_address, 0);
      }

      if (biases_address != 0x0) {
         sprintf(name, "biases%d.bin", task_id);
         dump_buffer(name, biases_address, 0);
      }

      if (eltwise_address != 0x0) {
         sprintf(name, "eltwise%d.bin", task_id);
         dump_buffer(name, eltwise_address, 0);
      }

      if (input_address != 0x0) {
         sprintf(name, "input%d.bin", task_id);
         dump_buffer(name, input_address, 0);
      }

      task_id++;
   }

   return ret;
}

static void
handle_mem_sync(struct rknpu_mem_sync *args)
{
   L("struct rknpu_mem_sync sync = {\n");
   L("   .flags = 0x%x,\n", args->flags);
   L("   .reserved = 0x%x,\n", args->reserved);
   L("   .obj_addr = 0x%llx,\n", args->obj_addr);
   L("   .offset = 0x%llx,\n", args->offset);
   L("   .size = %llu,\n", args->size);
   L("};\n");
}

static int
handle_mem_create(struct rknpu_mem_create *args)
{
   int ret = 0;

#if 0
   L("struct rknpu_mem_create create = {\n");
   L("   .dma_addr = 0x%llx,\n", args->dma_addr);
   L("   .flags = 0x%x,\n", args->flags);
   L("   .handle = %u,\n", args->handle);
   L("   .obj_addr = 0x%llx,\n", args->obj_addr);
   L("   .size = %llu,\n", args->size);
   L("};\n");
#endif

   assert(context.next_handle_id < MAX_BOS);

   context.bos[context.next_handle_id].handle = args->handle;
   context.bos[context.next_handle_id].size = args->size;
   context.bos[context.next_handle_id].obj_addr = args->obj_addr;
   context.bos[context.next_handle_id].dma_addr = args->dma_addr;

   fprintf(stderr, "%s: dma_addr %llx\n", __func__, args->dma_addr);
   context.next_handle_id++;

   return ret;
}

static void
handle_action(struct rknpu_action *args)
{
   switch (args->flags) {
   case RKNPU_GET_HW_VERSION:
      L("%s: RKNPU_GET_HW_VERSION %x\n", __func__, args->value);
      break;
   case RKNPU_GET_DRV_VERSION:
      L("%s: RKNPU_GET_DRV_VERSION %x\n", __func__, args->value);
      break;
   case RKNPU_POWER_ON:
      L("%s: RKNPU_POWER_ON %x\n", __func__, args->value);
      break;
   case RKNPU_GET_IOMMU_EN:
      L("%s: RKNPU_GET_IOMMU_EN %x\n", __func__, args->value);
      break;
   case RKNPU_SET_PROC_NICE:
      L("%s: RKNPU_SET_PROC_NICE %x\n", __func__, args->value);
      break;
   case RKNPU_GET_FREQ:
      L("%s: RKNPU_GET_FREQ %x\n", __func__, args->value);
      break;
   default:
      L("%s: unhandled action %d %x\n", __func__, args->flags, args->value);
      break;
   }
}

typedef int (*real_ioctl_t)(int fd, unsigned long request, ...);
int
ioctl(int fd, unsigned long request, ...)
{
   int ret;
   uint32_t output_address = 0;

   va_list ap;
   va_start(ap, request);
   void *ptr_ = va_arg(ap, void *);
   va_end(ap);

   real_ioctl_t real_ioctl;
   real_ioctl = (real_ioctl_t)dlsym(RTLD_NEXT, "ioctl");

   switch (request) {
   case DRM_IOCTL_RKNPU_SUBMIT:
      handle_submit(ptr_, &output_address);
      break;
   case DRM_IOCTL_RKNPU_MEM_SYNC:
      // handle_mem_sync(ptr_);
      break;
   case DRM_IOCTL_RKNPU_ACTION:
      // handle_action(ptr_);
      break;
   }

   ret = real_ioctl(fd, request, ptr_);

   switch (request) {
   case DRM_IOCTL_RKNPU_SUBMIT: {
      char name[PATH_MAX];
      sprintf(name, "output%d.bin", task_id);
      dump_buffer(name, output_address, 0);

      break;
   }
   case DRM_IOCTL_RKNPU_MEM_CREATE:
   case IOCTL_RKNPU_MEM_CREATE:
   case 0xc0286442:
      handle_mem_create(ptr_);
      context.device_fd = fd;
      break;
   }

   return ret;
}

/* Intended to be called from GDB when the underlying memory is not directly
 * accessible to it. */
void dump_mem(uint32_t *ptr, unsigned bytes);

void
dump_mem(uint32_t *ptr, unsigned bytes)
{
   for (int i = 0; i < bytes / 4; i++) {
      fprintf(stderr, "%08x %08x %08x %08x\n", ptr[0], ptr[1], ptr[2], ptr[3]);
      ptr += 4;
   }
}

#ifdef GETENV
typedef char *(*real_getenv_t)(const char *name);
char *
getenv(const char *name)
{
   real_getenv_t real_getenv;
   real_getenv = (real_getenv_t)dlsym(RTLD_NEXT, "getenv");

   fprintf(stderr, "getenv %s\n", name);

   return real_getenv(name);
}

#endif
