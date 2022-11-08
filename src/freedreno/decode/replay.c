/*
 * Copyright © 2022 Igalia S.L.
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xf86drm.h>
#include "drm-uapi/msm_drm.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "util/os_time.h"
#include "util/rb_tree.h"
#include "util/u_vector.h"
#include "buffers.h"
#include "cffdec.h"
#include "io.h"
#include "redump.h"

/**
 * Replay command stream obtained from:
 * - /sys/kernel/debug/dri/0/rd
 * - /sys/kernel/debug/dri/0/hangrd
 * !!! Command stream capture should be done with ALL buffers:
 * - echo 1 > /sys/module/msm/parameters/rd_full
 *
 * Requires kernel with MSM_INFO_SET_IOVA support.
 *
 * This tool is intended for reproduction of various GPU issues:
 * - GPU hangs, note that command stream obtained from hangrd
 *   may not reproduce (rarely) the hang, since the buffers are
 *   snapshotted at the moment of the hang and not at the start
 *   of the hanging command stream.
 * - TODO: Misrendering, would require marking framebuffer images
 *   at each renderpass in order to fetch and decode them.
 *
 * Code from Freedreno/Turnip is not re-used here since the relevant
 * pieces may introduce additional allocations which cannot be allowed
 * during the replay.
 */

static const char *exename = NULL;

static int handle_file(const char *filename);

static void
print_usage(const char *name)
{
   /* clang-format off */
   fprintf(stderr, "Usage:\n\n"
           "\t%s [OPTSIONS]... FILE...\n\n"
           "Options:\n"
           "\t-e, --exe=NAME   - only use cmdstream from named process\n"
           "\t-h, --help       - show this message\n"
           , name);
   /* clang-format on */
   exit(2);
}

/* clang-format off */
static const struct option opts[] = {
      /* Long opts that simply set a flag (no corresponding short alias: */

      /* Long opts with short alias: */
      { "exe",       required_argument, 0, 'e' },
      { "help",      no_argument,       0, 'h' },
};
/* clang-format on */

int
main(int argc, char **argv)
{
   int ret = -1;
   int c;

   while ((c = getopt_long(argc, argv, "e:h", opts, NULL)) != -1) {
      switch (c) {
      case 0:
         /* option that set a flag, nothing to do */
         break;
      case 'e':
         exename = optarg;
         break;
      case 'h':
      default:
         print_usage(argv[0]);
      }
   }

   while (optind < argc) {
      ret = handle_file(argv[optind]);
      if (ret) {
         fprintf(stderr, "error reading: %s\n", argv[optind]);
         fprintf(stderr, "continuing..\n");
      }
      optind++;
   }

   if (ret)
      print_usage(argv[0]);

   return ret;
}

static void
parse_addr(uint32_t *buf, int sz, unsigned int *len, uint64_t *gpuaddr)
{
   *gpuaddr = buf[0];
   *len = buf[1];
   if (sz > 8)
      *gpuaddr |= ((uint64_t)(buf[2])) << 32;
}

struct buffer {
   struct rb_node node;

   uint32_t gem_handle;
   uint64_t size;
   uint64_t iova;
   void *map;

   bool used;
   uint32_t flags;
};

struct cmdstream {
   uint64_t iova;
   uint64_t size;
};

struct device {
   int fd;

   struct rb_tree buffers;
   struct u_vector cmdstreams;
};

void
buffer_mem_free(struct device *dev, struct buffer *buf);

static int
rb_buffer_insert_cmp(const struct rb_node *n1, const struct rb_node *n2)
{
   const struct buffer *buf1 = (const struct buffer *)n1;
   const struct buffer *buf2 = (const struct buffer *)n2;
   /* Note that gpuaddr comparisions can overflow an int: */
   if (buf1->iova > buf2->iova)
      return 1;
   else if (buf1->iova < buf2->iova)
      return -1;
   return 0;
}

static int
rb_buffer_search_cmp(const struct rb_node *node, const void *addrptr)
{
   const struct buffer *buf = (const struct buffer *)node;
   uint64_t iova = *(uint64_t *)addrptr;
   if (buf->iova + buf->size <= iova)
      return -1;
   else if (buf->iova > iova)
      return 1;
   return 0;
}

static struct device *
device_create()
{
   struct device *dev = calloc(sizeof(struct device), 1);

   dev->fd = drmOpenWithType("msm", NULL, DRM_NODE_RENDER);
   if (dev->fd < 0) {
      errx(1, "Cannot open MSM fd!");
   }

   struct drm_msm_param req = {
      .pipe = MSM_PIPE_3D0,
      .param = MSM_PARAM_VA_START,
   };

   int ret = drmCommandWriteRead(dev->fd, DRM_MSM_GET_PARAM, &req,
                                 sizeof(req));

   if (ret) {
      err(1, "MSM_INFO_SET_IOVA is unsupported");
   }

   rb_tree_init(&dev->buffers);
   u_vector_init(&dev->cmdstreams, 8, sizeof(struct cmdstream));

   return dev;
}

static struct buffer *
device_get_buffer(struct device *dev, uint64_t iova)
{
   if (iova == 0)
      return NULL;
   return (struct buffer *)rb_tree_search(&dev->buffers, &iova,
                                          rb_buffer_search_cmp);
}

static void
device_mark_buffers(struct device *dev)
{
   rb_tree_foreach_safe (struct buffer, buf, &dev->buffers, node) {
      buf->used = false;
   }
}

static void
device_free_unused_buffers(struct device *dev)
{
   rb_tree_foreach_safe (struct buffer, buf, &dev->buffers, node) {
      if (!buf->used) {
         buffer_mem_free(dev, buf);
         rb_tree_remove(&dev->buffers, &buf->node);
         free(buf);
      }
   }
}

static inline void
get_abs_timeout(struct drm_msm_timespec *tv, uint64_t ns)
{
   struct timespec t;
   clock_gettime(CLOCK_MONOTONIC, &t);
   tv->tv_sec = t.tv_sec + ns / 1000000000;
   tv->tv_nsec = t.tv_nsec + ns % 1000000000;
}

static void
device_submit_cmdstreams(struct device *dev)
{
   device_free_unused_buffers(dev);
   device_mark_buffers(dev);

   if (!u_vector_length(&dev->cmdstreams))
      return;

   struct drm_msm_gem_submit_cmd cmds[u_vector_length(&dev->cmdstreams)];

   uint32_t idx = 0;
   struct cmdstream *cmd;
   u_vector_foreach(cmd, &dev->cmdstreams) {
      struct buffer *cmdstream_buf = device_get_buffer(dev, cmd->iova);
      cmdstream_buf->flags = MSM_SUBMIT_BO_DUMP;

      uint32_t bo_idx = 0;
      rb_tree_foreach (struct buffer, buf, &dev->buffers, node) {
         if (buf == cmdstream_buf)
            break;

         bo_idx++;
      }

      struct drm_msm_gem_submit_cmd *submit_cmd = &cmds[idx];
      submit_cmd->type = MSM_SUBMIT_CMD_BUF;
      submit_cmd->submit_idx = bo_idx;
      submit_cmd->submit_offset = cmd->iova - cmdstream_buf->iova;
      submit_cmd->size = cmd->size;
      submit_cmd->pad = 0;
      submit_cmd->nr_relocs = 0;
      submit_cmd->relocs = 0;

      idx++;
   }

   uint32_t bo_count = 0;
   rb_tree_foreach (struct buffer, buf, &dev->buffers, node) {
      if (buf)
         bo_count++;
   }

   struct drm_msm_gem_submit_bo *bo_list =
      calloc(sizeof(struct drm_msm_gem_submit_bo), bo_count);

   uint32_t bo_idx = 0;
   rb_tree_foreach (struct buffer, buf, &dev->buffers, node) {
      struct drm_msm_gem_submit_bo *submit_bo = &bo_list[bo_idx++];
      submit_bo->handle = buf->gem_handle;
      submit_bo->flags = buf->flags | MSM_SUBMIT_BO_READ | MSM_SUBMIT_BO_WRITE;
      submit_bo->presumed = buf->iova;

      buf->flags = 0;
   }

   struct drm_msm_gem_submit submit_req = {
      .flags = MSM_PIPE_3D0,
      .queueid = 0,
      .bos = (uint64_t)(uintptr_t)bo_list,
      .nr_bos = bo_count,
      .cmds = (uint64_t)(uintptr_t)cmds,
      .nr_cmds = u_vector_length(&dev->cmdstreams),
      .in_syncobjs = 0,
      .out_syncobjs = 0,
      .nr_in_syncobjs = 0,
      .nr_out_syncobjs = 0,
      .syncobj_stride = sizeof(struct drm_msm_gem_submit_syncobj),
   };

   int ret = drmCommandWriteRead(dev->fd, DRM_MSM_GEM_SUBMIT, &submit_req,
                                 sizeof(submit_req));

   if (ret) {
      err(1, "DRM_MSM_GEM_SUBMIT failure %d", ret);
   }

   /* Wait for submission to complete in order to be sure that
    * freeing buffers would free their VMAs in the kernel.
    * Makes sure that new allocations won't clash with old ones.
    */
   struct drm_msm_wait_fence wait_req = {
      .fence = submit_req.fence,
      .queueid = 0,
   };
   get_abs_timeout(&wait_req.timeout, 1000000000);

   ret =
      drmCommandWrite(dev->fd, DRM_MSM_WAIT_FENCE, &wait_req, sizeof(wait_req));
   if (ret && (ret != -ETIMEDOUT)) {
      err(1, "DRM_MSM_WAIT_FENCE failure %d", ret);
   }

   u_vector_finish(&dev->cmdstreams);
   u_vector_init(&dev->cmdstreams, 8, sizeof(struct cmdstream));
}

static void
buffer_mem_alloc(struct device *dev, struct buffer *buf)
{
   {
      struct drm_msm_gem_new req = {.size = buf->size, .flags = MSM_BO_WC};

      int ret =
         drmCommandWriteRead(dev->fd, DRM_MSM_GEM_NEW, &req, sizeof(req));
      if (ret) {
         err(1, "DRM_MSM_GEM_NEW failure %d", ret);
      }

      buf->gem_handle = req.handle;
   }

   {
      struct drm_msm_gem_info req = {
         .handle = buf->gem_handle,
         .info = MSM_INFO_SET_IOVA,
         .value = buf->iova,
      };

      int ret =
         drmCommandWriteRead(dev->fd, DRM_MSM_GEM_INFO, &req, sizeof(req));

      if (ret) {
         err(1, "MSM_INFO_SET_IOVA failure %d", ret);
      }
   }

   {
      struct drm_msm_gem_info req = {
         .handle = buf->gem_handle,
         .info = MSM_INFO_GET_OFFSET,
      };

      int ret =
         drmCommandWriteRead(dev->fd, DRM_MSM_GEM_INFO, &req, sizeof(req));
      if (ret) {
         err(1, "MSM_INFO_GET_OFFSET failure %d", ret);
      }

      void *map = mmap(0, buf->size, PROT_READ | PROT_WRITE, MAP_SHARED,
                       dev->fd, req.value);
      if (map == MAP_FAILED) {
         err(1, "mmap failure");
      }

      buf->map = map;
   }
}

void
buffer_mem_free(struct device *dev, struct buffer *buf)
{
   munmap(buf->map, buf->size);

   struct drm_gem_close req = {
      .handle = buf->gem_handle,
   };
   drmIoctl(dev->fd, DRM_IOCTL_GEM_CLOSE, &req);
}

static void
upload_buffer(struct device *dev, uint64_t iova, unsigned int size,
              void *hostptr)
{
   struct buffer *buf = device_get_buffer(dev, iova);

   if (!buf) {
      buf = calloc(sizeof(struct buffer), 1);
      buf->iova = iova;
      buf->size = size;
      rb_tree_insert(&dev->buffers, &buf->node, rb_buffer_insert_cmp);

      buffer_mem_alloc(dev, buf);
   } else if (buf->size != size) {
      buffer_mem_free(dev, buf);
      buf->size = size;
      buffer_mem_alloc(dev, buf);
   }

   memcpy(buf->map, hostptr, size);

   buf->used = true;
}

static int
handle_file(const char *filename)
{
   enum rd_sect_type type = RD_NONE;
   void *buf = NULL;
   struct io *io;
   int submit = 0;
   int sz, ret = 0;
   bool skip = false;
   bool need_submit = false;

   printf("Reading %s...\n", filename);

   if (!strcmp(filename, "-"))
      io = io_openfd(0);
   else
      io = io_open(filename);

   if (!io) {
      fprintf(stderr, "could not open: %s\n", filename);
      return -1;
   }

   struct device *dev = device_create();

   struct {
      unsigned int len;
      uint64_t gpuaddr;
   } gpuaddr = {0};

   while (true) {
      uint32_t arr[2];

      ret = io_readn(io, arr, 8);
      if (ret <= 0)
         goto end;

      while ((arr[0] == 0xffffffff) && (arr[1] == 0xffffffff)) {
         ret = io_readn(io, arr, 8);
         if (ret <= 0)
            goto end;
      }

      type = arr[0];
      sz = arr[1];

      if (sz < 0) {
         ret = -1;
         goto end;
      }

      free(buf);

      buf = malloc(sz + 1);
      ((char *)buf)[sz] = '\0';
      ret = io_readn(io, buf, sz);
      if (ret < 0)
         goto end;

      switch (type) {
      case RD_TEST:
      case RD_VERT_SHADER:
      case RD_FRAG_SHADER:
         /* no-op */
         break;
      case RD_CMD:
         skip = false;
         if (exename) {
            skip |= (strstr(buf, exename) != buf);
         } else {
            skip |= (strstr(buf, "fdperf") == buf);
            skip |= (strstr(buf, "chrome") == buf);
            skip |= (strstr(buf, "surfaceflinger") == buf);
            skip |= ((char *)buf)[0] == 'X';
         }
         break;

      case RD_GPUADDR:
         if (need_submit) {
            need_submit = false;
            device_submit_cmdstreams(dev);
         }

         parse_addr(buf, sz, &gpuaddr.len, &gpuaddr.gpuaddr);
         /* no-op */
         break;
      case RD_BUFFER_CONTENTS:
         upload_buffer(dev, gpuaddr.gpuaddr, gpuaddr.len, buf);
         buf = NULL;
         break;
      case RD_CMDSTREAM_ADDR: {
         unsigned int sizedwords;
         uint64_t gpuaddr;
         parse_addr(buf, sz, &sizedwords, &gpuaddr);
         printf("cmdstream %d: %d dwords\n", submit, sizedwords);

         if (!skip) {
            struct cmdstream *cmd = u_vector_add(&dev->cmdstreams);
            cmd->iova = gpuaddr;
            cmd->size = sizedwords * sizeof(uint32_t);
            need_submit = true;
         }

         submit++;
         break;
      }
      case RD_GPU_ID: {
         uint32_t gpu_id = *((unsigned int *)buf);
         if (!gpu_id)
            break;
         printf("gpuid: %d\n", gpu_id);
         break;
      }
      case RD_CHIP_ID: {
         uint64_t chip_id = *((uint64_t *)buf);
         uint32_t gpu_id = 100 * ((chip_id >> 24) & 0xff) +
                           10 * ((chip_id >> 16) & 0xff) +
                           ((chip_id >> 8) & 0xff);
         printf("gpuid: %d\n", gpu_id);
         break;
      }
      default:
         break;
      }
   }

end:
   if (need_submit)
      device_submit_cmdstreams(dev);

   close(dev->fd);

   io_close(io);
   fflush(stdout);

   if (ret < 0) {
      printf("corrupt file\n");
   }
   return 0;
}
