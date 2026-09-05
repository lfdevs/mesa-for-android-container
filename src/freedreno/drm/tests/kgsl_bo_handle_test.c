/* SPDX-License-Identifier: MIT */

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include "freedreno_drmif.h"
#include "kgsl/kgsl_priv.h"

/* Hardware test: allocate one native KGSL BO, without submitting GPU work.
 * The removed KMS-handle override tried to export this BO and returned zero.
 * A device-local handle must remain valid even when dma-buf export cannot work.
 */
int
main(void)
{
   int fd = open("/dev/kgsl-3d0", O_RDWR | O_CLOEXEC);
   if (fd < 0) {
      fprintf(stderr, "SKIP: /dev/kgsl-3d0 is not accessible\n");
      return 77;
   }

   struct fd_device *dev = fd_device_new(fd);
   if (!dev) {
      close(fd);
      fprintf(stderr, "Failed to create KGSL device\n");
      return 1;
   }

   struct fd_bo *bo = fd_bo_new(dev, 4096, 0, "handle regression test");
   int result = 1;
   if (!bo) {
      fprintf(stderr, "Failed to allocate native KGSL BO\n");
      goto out;
   }

   uint32_t handle = bo->handle;
   if (to_kgsl_bo(bo)->bo_type != KGSL_BO_NATIVE || !handle) {
      fprintf(stderr, "Expected a native, non-suballocated KGSL BO\n");
      goto out_bo;
   }

   /* The first lookup marks the BO shared; repeat to cover both states. */
   if (fd_bo_handle(bo) != handle || fd_bo_handle(bo) != handle) {
      fprintf(stderr, "Native handle was replaced during handle lookup\n");
      goto out_bo;
   }

   int dma_fd = fd_bo_dmabuf(bo);
   if (dma_fd >= 0)
      close(dma_fd);

   if (fd_bo_handle(bo) != handle) {
      fprintf(stderr, "dma-buf export attempt changed the native handle\n");
      goto out_bo;
   }

   printf("PASS: native KGSL handle survives sharing and %s export\n",
          dma_fd < 0 ? "failed" : "successful");
   result = 0;

out_bo:
   fd_bo_del(bo);
out:
   fd_device_del(dev);
   close(fd);
   return result;
}
