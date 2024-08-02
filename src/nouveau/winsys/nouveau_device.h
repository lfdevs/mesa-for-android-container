#ifndef NOUVEAU_DEVICE
#define NOUVEAU_DEVICE 1

#include "nouveau_private.h"
#include "nv_device_info.h"
#include "util/simple_mtx.h"

#include <stddef.h>

struct _drmDevice;
struct hash_table;

#ifdef __cplusplus
extern "C" {
#endif

#define NOUVEAU_WS_DEVICE_KERNEL_RESERVATION_START (1ull << 39)

struct nouveau_ws_device {
   int fd;

   struct nv_device_info info;

   uint32_t max_push;

   simple_mtx_t bos_lock;
   struct hash_table *bos;

   bool has_vm_bind;
};

struct nouveau_ws_device *nouveau_ws_device_new(struct _drmDevice *drm_device);
void nouveau_ws_device_destroy(struct nouveau_ws_device *);

uint64_t nouveau_ws_device_vram_used(struct nouveau_ws_device *);
uint64_t nouveau_ws_device_timestamp(struct nouveau_ws_device *device);
bool nouveau_ws_device_has_tiled_bo(struct nouveau_ws_device *device);

#ifdef __cplusplus
}
#endif

#endif
