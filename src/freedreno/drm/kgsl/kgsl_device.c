#include "kgsl_priv.h"

static const struct fd_device_funcs funcs = {
    .bo_new = kgsl_bo_new,
    .pipe_new = kgsl_pipe_new,
    .bo_from_handle = kgsl_bo_from_handle,
    .bo_from_dmabuf = kgsl_bo_from_dmabuf,
    .bo_close_handle = kgsl_bo_close_handle,
    .destroy = kgsl_device_destroy,
};

struct fd_device *
kgsl_device_new(int fd)
{
    struct kgsl_device *kgsl_dev;
    struct fd_device *dev;
    struct kgsl_devinfo info;

    /* Try to read the device info to detect if the FD is really KGSL */
    if(kgsl_get_prop(fd, KGSL_PROP_DEVICE_INFO, &info, sizeof(info)))
        return NULL;

    kgsl_dev = calloc(1, sizeof(*kgsl_dev));
    if (!kgsl_dev)
      return NULL;

    dev = &kgsl_dev->base;
    dev->funcs = &funcs;
    dev->fd = fd;
    dev->version = FD_VERSION_ROBUSTNESS;
    dev->features = FD_FEATURE_DIRECT_RESET | FD_FEATURE_IMPORT_DMABUF;

    /* async submit_queue used for softpin deffered submits */
    util_queue_init(&dev->submit_queue, "sq", 8, 1, 0, NULL);

    dev->bo_size = sizeof(struct kgsl_bo);

    return dev;
}

static void
kgsl_device_destroy(struct fd_device *dev)
{
}
