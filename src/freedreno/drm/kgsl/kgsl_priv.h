#ifndef KGSL_PRIV_H
#define KGSL_PRIV_H
#include "freedreno_priv.h"

/* TODO the KGSL kernel interface should probably be moved */
/* into someplace common that both turnip and freedreno can use */
#include "../../vulkan/msm_kgsl.h"

int kgsl_get_prop(int fd, unsigned int type, void *value, size_t size);

struct kgsl_device {
    struct fd_device base;
};
FD_DEFINE_CAST(fd_device, kgsl_device);

struct fd_device *kgsl_device_new(int fd);
static void kgsl_device_destroy(struct fd_device *dev);

struct kgsl_pipe {
    struct fd_pipe base;

    struct fd_dev_id dev_id;

    uint32_t gmem_size;
    uint64_t gmem_base;
    uint32_t queue_id;
};
FD_DEFINE_CAST(fd_pipe, kgsl_pipe);

struct fd_pipe *kgsl_pipe_new(struct fd_device *dev, enum fd_pipe_id id,
                              uint32_t prio);
int kgsl_pipe_safe_ioctl(int fd, unsigned long request, void *arg);
struct fd_submit *kgsl_submit_sp_new(struct fd_pipe *pipe);

struct kgsl_bo {
    struct fd_bo base;
    const char *name;
    uint64_t iova;
    uint32_t queue_id;
    int import_fd; // fd for imported buffers

    enum {
        KGSL_BO_NATIVE,
        KGSL_BO_IMPORT,
    } bo_type;
};
FD_DEFINE_CAST(fd_bo, kgsl_bo);

struct fd_bo *kgsl_bo_new(struct fd_device *dev, uint32_t size, uint32_t flags);
struct fd_bo *kgsl_bo_from_dmabuf(struct fd_device *dev, int fd);
struct fd_bo *kgsl_bo_from_handle(struct fd_device *dev, uint32_t size, uint32_t handle);
void kgsl_bo_close_handle(struct fd_bo *bo);

#endif
