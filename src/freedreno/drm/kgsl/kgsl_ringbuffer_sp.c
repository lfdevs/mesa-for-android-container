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
    unsigned nr_cmds = 0;


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

        if (submit == last_submit(submit_list)) {
            DEBUG_MSG("merged %u submits", cmd_idx);
            break;
        }

        list_del(&submit->node);
        fd_submit_del(submit);
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

    int ret = kgsl_pipe_safe_ioctl(pipe->dev->fd, IOCTL_KGSL_GPU_COMMAND, &req);

    if (ret) {
        ERROR_MSG("submit failed %d (%s)", ret, strerror(errno));
        goto fail;
    }

    fd_submit->out_fence->kfence = req.timestamp;

    if  (fd_submit->out_fence->use_fence_fd) {
        int fd = timestamp_to_fd(pipe, req.timestamp);
        if (fd < 0) {
            ERROR_MSG("Failed to create sync file for timestamp (%s)", strerror(errno));
            goto fail;
        }

        fd_submit->out_fence->fence_fd = fd;
    }

    if (fd_submit->in_fence_fd != -1)
        close(fd_submit->in_fence_fd);

fail:
    return ret;
}

struct fd_submit *
kgsl_submit_sp_new(struct fd_pipe *pipe)
{
    return fd_submit_sp_new(pipe, flush_submit_list);
}
