#include "kgsl_priv.h"
#include "freedreno_ringbuffer_sp.h"

static int
timestamp_to_fd(struct fd_pipe *pipe, uint32_t timestamp)
{
    int fd;
    struct kgsl_pipe *kgsl_pipe = to_kgsl_pipe(pipe);
    struct kgsl_timestamp_event event = {
        .type = KGSL_TIMESTAMP_EVENT_FENCE,
        .context_id = kgsl_pipe->queue_id,
        .timestamp = timestamp,
        .priv = &fd,
        .len = sizeof(fd),
    };

    int ret = kgsl_pipe_safe_ioctl(pipe->dev->fd, IOCTL_KGSL_TIMESTAMP_EVENT, &event);
    if (ret)
        return -1;

    return fd;
}

static int
flush_submit_list(struct list_head *submit_list)
{
    struct fd_submit_sp *fd_submit = to_fd_submit_sp(last_submit(submit_list));
    struct fd_pipe *pipe = fd_submit->base.pipe;
    struct kgsl_pipe *kgsl_pipe = to_kgsl_pipe(pipe);
    const bool keep_deferred_submits =
        pipe->dev->features & FD_FEATURE_KGSL_SHARED_BO_SYNC;
    bool close_in_fence = keep_deferred_submits;
    unsigned nr_cmds = 0;
    int ret;

    MESA_TRACE_FUNC();

    foreach_submit (submit, submit_list) {
        assert(submit->pipe == &kgsl_pipe->base);
        nr_cmds += to_fd_ringbuffer_sp(submit->primary)->u.nr_cmds;
    }

    struct kgsl_command_object cmds[nr_cmds];
    unsigned cmd_idx = 0;
    foreach_submit_safe (submit, submit_list) {
        struct fd_ringbuffer_sp *deferred_primary =
            to_fd_ringbuffer_sp(submit->primary);

        for (unsigned i = 0; i < deferred_primary->u.nr_cmds; i++) {
            struct fd_bo *ring_bo = deferred_primary->u.cmds[i].ring_bo;

            cmds[cmd_idx++] = (struct kgsl_command_object) {
                .offset = 0,
                .gpuaddr = ring_bo->iova + submit_offset(ring_bo, deferred_primary->offset),
                .size = deferred_primary->u.cmds[i].size,
                .flags = KGSL_CMDLIST_IB,
                .id = ring_bo->handle,
            };
        }

        if (!keep_deferred_submits && submit != &fd_submit->base) {
            list_del(&submit->node);
            fd_submit_del(submit);
        }
    }
    DEBUG_MSG("merged %u submits", cmd_idx);

    if (fd_submit->wait_shared_bos) {
        for (unsigned i = 0; i < fd_submit->nr_bos; i++) {
            struct fd_bo *bo = fd_submit->bos[i];
            if (!(bo->alloc_flags & (FD_BO_SHARED | FD_BO_SCANOUT)))
                continue;

            /* The submit BO table does not track access per submit. Wait
             * for both readers and writers before accessing a shared BO.
             */
            ret = kgsl_bo_wait_dmabuf(bo, FD_BO_PREP_WRITE);
            if (ret) {
                ERROR_MSG("dma-buf wait failed (%s)", strerror(-ret));
                goto out;
            }
        }
    }

    struct kgsl_cmd_syncpoint_fence sync_fence = {
        .fd = fd_submit->in_fence_fd,
    };

    struct kgsl_command_syncpoint sync = {
        .type = KGSL_CMD_SYNCPOINT_TYPE_FENCE,
        .size = sizeof(sync_fence),
        .priv = (uintptr_t) &sync_fence,
    };


    struct kgsl_gpu_command req = {
        .flags = KGSL_CMDBATCH_SUBMIT_IB_LIST,
        .context_id = kgsl_pipe->queue_id,
        .cmdlist = (uintptr_t) cmds,
        .numcmds = cmd_idx,
        .cmdsize = sizeof(struct kgsl_command_object),
        .synclist = (uintptr_t) &sync,
        .syncsize = sizeof(struct kgsl_command_syncpoint),
        .numsyncs = sync_fence.fd != -1 ? 1 : 0,
    };

    ret = kgsl_pipe_safe_ioctl(pipe->dev->fd, IOCTL_KGSL_GPU_COMMAND, &req);

    if (ret) {
        ERROR_MSG("submit failed %d (%s)", ret, strerror(errno));
        goto out;
    }

    fd_submit->out_fence->kfence = req.timestamp;

    if (fd_submit->out_fence->use_fence_fd) {
        int fd = timestamp_to_fd(pipe, req.timestamp);
        if (fd < 0) {
            if (keep_deferred_submits)
                ret = -errno;
            ERROR_MSG("Failed to create sync file for timestamp (%s)", strerror(errno));
            goto out;
        }

        fd_submit->out_fence->fence_fd = fd;
    }
    close_in_fence = true;

out:
    if (close_in_fence && fd_submit->in_fence_fd != -1)
        close(fd_submit->in_fence_fd);

    if (keep_deferred_submits) {
        /* Keep A740 v2 BO references until KGSL has the command list and
         * their shared fence has a kernel timestamp to wait on.
         */
        foreach_submit_safe (submit, submit_list) {
            if (submit == last_submit(submit_list))
                break;

            list_del(&submit->node);
            fd_submit_del(submit);
        }
    }

    return ret;
}

struct fd_submit *
kgsl_submit_sp_new(struct fd_pipe *pipe)
{
    return fd_submit_sp_new(pipe, flush_submit_list);
}
