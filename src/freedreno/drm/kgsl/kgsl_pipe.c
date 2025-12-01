#include "kgsl_priv.h"
#include "freedreno_ringbuffer_sp.h"

/* TODO this function is borrowed from turnip, can it be shared in some way? */
int
kgsl_pipe_safe_ioctl(int fd, unsigned long request, void *arg)
{
   int ret;

   do {
      ret = ioctl(fd, request, arg);
   } while (ret == -1 && (errno == EINTR || errno == EAGAIN));

   return ret;
}

/* TODO this function is borrowed from turnip, can it be shared in some way?
 * safe_ioctl is not enough as restarted waits would not adjust the timeout
 * which could lead to waiting substantially longer than requested
 */
static int
wait_timestamp_safe(int fd,
                    unsigned int context_id,
                    unsigned int timestamp,
                    int64_t timeout_ms)
{
   int64_t start_time = os_time_get_nano();
   struct kgsl_device_waittimestamp_ctxtid wait = {
      .context_id = context_id,
      .timestamp = timestamp,
      .timeout = timeout_ms,
   };

   while (true) {
      int ret = kgsl_pipe_safe_ioctl(fd, IOCTL_KGSL_DEVICE_WAITTIMESTAMP_CTXTID, &wait);

      if (ret == -1 && (errno == EINTR || errno == EAGAIN)) {
         int64_t current_time = os_time_get_nano();

         /* update timeout to consider time that has passed since the start */
         timeout_ms -= (current_time - start_time) / 1000000;
         if (timeout_ms <= 0) {
            errno = ETIME;
            return -1;
         }

         wait.timeout = (unsigned int) timeout_ms;
         start_time = current_time;
      } else {
         return ret;
      }
   }
}

int
kgsl_get_prop(int fd, unsigned int type, void *value, size_t size)
{
   struct kgsl_device_getproperty getprop = {
      .type = type,
      .value = value,
      .sizebytes = size,
   };

   return kgsl_pipe_safe_ioctl(fd, IOCTL_KGSL_DEVICE_GETPROPERTY, &getprop);
}

static int
kgsl_pipe_get_param(struct fd_pipe *pipe, enum fd_param_id param,
                    uint64_t *value)
{
   struct kgsl_pipe *kgsl_pipe = to_kgsl_pipe(pipe);
   switch (param) {
   case FD_DEVICE_ID:
   case FD_GPU_ID:
      *value = kgsl_pipe->dev_id.gpu_id;
      return 0;
   case FD_GMEM_SIZE:
      *value = kgsl_pipe->gmem_size;
      return 0;
   case FD_GMEM_BASE:
      *value = kgsl_pipe->gmem_base;
      return 0;
   case FD_CHIP_ID:
      *value = kgsl_pipe->dev_id.chip_id;
      return 0;
   case FD_NR_PRIORITIES:
      /* Take from kgsl kmd source code, if device is a4xx or newer
       * it has KGSL_PRIORITY_MAX_RB_LEVELS=4 priorities otherwise it just has one.
       * https://android.googlesource.com/kernel/msm/+/refs/tags/android-13.0.0_r0.21/drivers/gpu/msm/kgsl.h#56
       */
      *value = kgsl_pipe->dev_id.gpu_id >= 400 ? 4 : 1;
      return 0;
   case FD_MAX_FREQ:
      /* Explicity fault on MAX_FREQ as we don't have a way to convert
       * timestamp values from KGSL into time values. If we use the default
       * path an error message would be generated when this is simply an
       * unsupported feature.
       */
      return -1;
   case FD_TIMESTAMP:
      return -1;
   default:
      ERROR_MSG("invalid param id: %d", param);
      return -1;
   }
}

static int
kgsl_pipe_set_param(struct fd_pipe *pipe, uint32_t param, uint64_t value)
{
    ERROR_MSG("kgsl_pipe_set_param not implemented");
    return -1;
}

static int
kgsl_pipe_wait(struct fd_pipe *pipe, const struct fd_fence *fence, uint64_t timeout)
{
    struct kgsl_pipe *kgsl_pipe = to_kgsl_pipe(pipe);
    return wait_timestamp_safe(pipe->dev->fd, kgsl_pipe->queue_id, fence->kfence, timeout);
}

static void
kgsl_pipe_destroy(struct fd_pipe *pipe)
{
    struct kgsl_pipe *kgsl_pipe = to_kgsl_pipe(pipe);
    struct kgsl_drawctxt_destroy req = {
        .drawctxt_id = kgsl_pipe->queue_id,
    };

    fd_pipe_sp_ringpool_fini(pipe);
    kgsl_pipe_safe_ioctl(pipe->dev->fd, IOCTL_KGSL_DRAWCTXT_DESTROY, &req);
    free(kgsl_pipe);
}

static int
kgsl_reset_status(struct fd_pipe *pipe, enum fd_reset_status *status)
{
    struct kgsl_pipe *kgsl_pipe = to_kgsl_pipe(pipe);
    uint32_t value = kgsl_pipe->queue_id;
    int ret = kgsl_get_prop(pipe->dev->fd, KGSL_PROP_GPU_RESET_STAT, &value, sizeof(value));

    if (!ret) {
        switch (value) {
        case KGSL_CTX_STAT_NO_ERROR:
            *status = FD_RESET_NO_ERROR;
            break;
        case KGSL_CTX_STAT_GUILTY_CONTEXT_RESET_EXT:
            *status = FD_RESET_GUILTY;
            break;
        case KGSL_CTX_STAT_INNOCENT_CONTEXT_RESET_EXT:
            *status = FD_RESET_INNOCENT;
            break;
        case KGSL_CTX_STAT_UNKNOWN_CONTEXT_RESET_EXT:
        default:
            *status = FD_RESET_UNKNOWN;
            break;
        }
    }

    return ret;
}

static const struct fd_pipe_funcs pipe_funcs = {
    .ringbuffer_new_object = fd_ringbuffer_sp_new_object,
    .submit_new = kgsl_submit_sp_new,
    .reset_status = kgsl_reset_status,
    .flush = fd_pipe_sp_flush,
    .wait = kgsl_pipe_wait,
    .get_param = kgsl_pipe_get_param,
    .set_param = kgsl_pipe_set_param,
    .destroy = kgsl_pipe_destroy,
};

struct fd_pipe *kgsl_pipe_new(struct fd_device *dev, enum fd_pipe_id id,
                              uint32_t prio)
{
    struct kgsl_pipe *kgsl_pipe = NULL;
    struct fd_pipe *pipe = NULL;
    kgsl_pipe = calloc(1, sizeof(*kgsl_pipe));
    if (!kgsl_pipe) {
        ERROR_MSG("allocation failed");
        goto fail;
    }

    pipe = &kgsl_pipe->base;
    pipe->dev = dev;
    pipe->funcs = &pipe_funcs;

    struct kgsl_devinfo info;
    if(kgsl_get_prop(dev->fd, KGSL_PROP_DEVICE_INFO, &info, sizeof(info)))
        goto fail;

    uint64_t gmem_iova;
    if(kgsl_get_prop(dev->fd, KGSL_PROP_UCHE_GMEM_VADDR, &gmem_iova, sizeof(gmem_iova)))
        goto fail;

    kgsl_pipe->dev_id.gpu_id =
        ((info.chip_id >> 24) & 0xff) * 100 +
        ((info.chip_id >> 16) & 0xff) * 10 +
        ((info.chip_id >>  8) & 0xff);

    kgsl_pipe->dev_id.chip_id = info.chip_id;
    kgsl_pipe->gmem_size = info.gmem_sizebytes;
    kgsl_pipe->gmem_base = gmem_iova;

    struct kgsl_drawctxt_create req = {
        .flags = KGSL_CONTEXT_SAVE_GMEM |
                 KGSL_CONTEXT_NO_GMEM_ALLOC |
                 KGSL_CONTEXT_PREAMBLE,
    };

    int ret = kgsl_pipe_safe_ioctl(dev->fd, IOCTL_KGSL_DRAWCTXT_CREATE, &req);
    if(ret)
        goto fail;

    kgsl_pipe->queue_id = req.drawctxt_id;

    fd_pipe_sp_ringpool_init(pipe);

    return pipe;
fail:
    if (pipe)
        fd_pipe_del(pipe);
    return NULL;
}
