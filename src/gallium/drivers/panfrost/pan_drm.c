/*
 * © Copyright 2019 Collabora, Ltd.
 * Copyright 2019 Alyssa Rosenzweig
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include <fcntl.h>
#include <xf86drm.h>

#include "drm-uapi/panfrost_drm.h"

#include "util/u_memory.h"
#include "util/os_time.h"
#include "os/os_mman.h"

#include "pan_screen.h"
#include "pan_resource.h"
#include "pan_context.h"
#include "pan_drm.h"
#include "pan_trace.h"

struct panfrost_drm {
	struct panfrost_driver base;
	int fd;
};

static void
panfrost_drm_allocate_slab(struct panfrost_screen *screen,
		           struct panfrost_memory *mem,
		           size_t pages,
		           bool same_va,
		           int extra_flags,
		           int commit_count,
		           int extent)
{
	struct panfrost_drm *drm = (struct panfrost_drm *)screen->driver;
	struct drm_panfrost_create_bo create_bo = {
		        .size = pages * 4096,
		        .flags = 0,  // TODO figure out proper flags..
	};
	struct drm_panfrost_mmap_bo mmap_bo = {0,};
	int ret;

	// TODO cache allocations
	// TODO properly handle errors
	// TODO take into account extra_flags

	ret = drmIoctl(drm->fd, DRM_IOCTL_PANFROST_CREATE_BO, &create_bo);
	if (ret) {
                fprintf(stderr, "DRM_IOCTL_PANFROST_CREATE_BO failed: %d\n", ret);
		assert(0);
	}

	mem->gpu = create_bo.offset;
	mem->gem_handle = create_bo.handle;
        mem->stack_bottom = 0;
        mem->size = create_bo.size;

	// TODO map and unmap on demand?
	mmap_bo.handle = create_bo.handle;
	ret = drmIoctl(drm->fd, DRM_IOCTL_PANFROST_MMAP_BO, &mmap_bo);
	if (ret) {
                fprintf(stderr, "DRM_IOCTL_PANFROST_MMAP_BO failed: %d\n", ret);
		assert(0);
	}

        mem->cpu = os_mmap(NULL, mem->size, PROT_READ | PROT_WRITE, MAP_SHARED,
                       drm->fd, mmap_bo.offset);
        if (mem->cpu == MAP_FAILED) {
                fprintf(stderr, "mmap failed: %p\n", mem->cpu);
		assert(0);
	}

        /* Record the mmap if we're tracing */
        if (!(extra_flags & PAN_ALLOCATE_GROWABLE))
                pantrace_mmap(mem->gpu, mem->cpu, mem->size, NULL);
}

static void
panfrost_drm_free_slab(struct panfrost_screen *screen, struct panfrost_memory *mem)
{
	struct panfrost_drm *drm = (struct panfrost_drm *)screen->driver;
	struct drm_gem_close gem_close = {
		.handle = mem->gem_handle,
	};
	int ret;

        if (os_munmap((void *) (uintptr_t) mem->cpu, mem->size)) {
                perror("munmap");
                abort();
        }

	mem->cpu = NULL;

	ret = drmIoctl(drm->fd, DRM_IOCTL_GEM_CLOSE, &gem_close);
	if (ret) {
                fprintf(stderr, "DRM_IOCTL_GEM_CLOSE failed: %d\n", ret);
		assert(0);
	}

	mem->gem_handle = -1;
}

static struct panfrost_bo *
panfrost_drm_import_bo(struct panfrost_screen *screen, struct winsys_handle *whandle)
{
	struct panfrost_bo *bo = CALLOC_STRUCT(panfrost_bo);
	struct panfrost_drm *drm = (struct panfrost_drm *)screen->driver;
        struct drm_panfrost_get_bo_offset get_bo_offset = {0,};
	struct drm_panfrost_mmap_bo mmap_bo = {0,};
        int ret;
        unsigned gem_handle;

	ret = drmPrimeFDToHandle(drm->fd, whandle->handle, &gem_handle);
	assert(!ret);

	get_bo_offset.handle = gem_handle;
        ret = drmIoctl(drm->fd, DRM_IOCTL_PANFROST_GET_BO_OFFSET, &get_bo_offset);
        assert(!ret);

	bo->gem_handle = gem_handle;
        bo->gpu = (mali_ptr) get_bo_offset.offset;
        pipe_reference_init(&bo->reference, 1);

	// TODO map and unmap on demand?
	mmap_bo.handle = gem_handle;
	ret = drmIoctl(drm->fd, DRM_IOCTL_PANFROST_MMAP_BO, &mmap_bo);
	if (ret) {
                fprintf(stderr, "DRM_IOCTL_PANFROST_MMAP_BO failed: %d\n", ret);
		assert(0);
	}

        bo->size = lseek(whandle->handle, 0, SEEK_END);
        assert(bo->size > 0);
        bo->cpu = os_mmap(NULL, bo->size, PROT_READ | PROT_WRITE, MAP_SHARED,
                       drm->fd, mmap_bo.offset);
        if (bo->cpu == MAP_FAILED) {
                fprintf(stderr, "mmap failed: %p\n", bo->cpu);
		assert(0);
	}

        /* Record the mmap if we're tracing */
        pantrace_mmap(bo->gpu, bo->cpu, bo->size, NULL);

        return bo;
}

static int
panfrost_drm_export_bo(struct panfrost_screen *screen, int gem_handle, unsigned int stride, struct winsys_handle *whandle)
{
	struct panfrost_drm *drm = (struct panfrost_drm *)screen->driver;
        struct drm_prime_handle args = {
                .handle = gem_handle,
                .flags = DRM_CLOEXEC,
        };

        int ret = drmIoctl(drm->fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &args);
        if (ret == -1)
                return FALSE;

        whandle->handle = args.fd;
        whandle->stride = stride;

        return TRUE;
}

static void
panfrost_drm_free_imported_bo(struct panfrost_screen *screen, struct panfrost_bo *bo) 
{
	struct panfrost_drm *drm = (struct panfrost_drm *)screen->driver;
	struct drm_gem_close gem_close = {
		.handle = bo->gem_handle,
	};
	int ret;

	ret = drmIoctl(drm->fd, DRM_IOCTL_GEM_CLOSE, &gem_close);
	if (ret) {
                fprintf(stderr, "DRM_IOCTL_GEM_CLOSE failed: %d\n", ret);
		assert(0);
	}

	bo->gem_handle = -1;
	bo->gpu = (mali_ptr)NULL;
}

static int
panfrost_drm_submit_job(struct panfrost_context *ctx, u64 job_desc, int reqs, struct pipe_surface *surf)
{
        struct pipe_context *gallium = (struct pipe_context *) ctx;
        struct panfrost_screen *screen = pan_screen(gallium->screen);
	struct panfrost_drm *drm = (struct panfrost_drm *)screen->driver;
        struct drm_panfrost_submit submit = {0,};
        int bo_handles[7];

        submit.in_syncs = (u64) (uintptr_t) &ctx->out_sync;
        submit.in_sync_count = 1;

        submit.out_sync = ctx->out_sync;

	submit.jc = job_desc;
	submit.requirements = reqs;

	if (surf) {
		struct panfrost_resource *res = pan_resource(surf->texture);
		assert(res->bo->gem_handle > 0);
		bo_handles[submit.bo_handle_count++] = res->bo->gem_handle;

		if (res->bo->checksum_slab.gem_handle)
			bo_handles[submit.bo_handle_count++] = res->bo->checksum_slab.gem_handle;
	}

	/* TODO: Add here the transient pools */
        /* TODO: Add here the BOs listed in the panfrost_job */
	bo_handles[submit.bo_handle_count++] = ctx->shaders.gem_handle;
	bo_handles[submit.bo_handle_count++] = ctx->scratchpad.gem_handle;
	bo_handles[submit.bo_handle_count++] = ctx->tiler_heap.gem_handle;
	bo_handles[submit.bo_handle_count++] = ctx->varying_mem.gem_handle;
	bo_handles[submit.bo_handle_count++] = ctx->misc_0.gem_handle;
	submit.bo_handles = (u64) (uintptr_t) bo_handles;

        /* Dump memory _before_ submitting so we're not corrupted with actual GPU results */
        pantrace_dump_memory();

	if (drmIoctl(drm->fd, DRM_IOCTL_PANFROST_SUBMIT, &submit)) {
	        fprintf(stderr, "Error submitting: %m\n");
	        return errno;
	}

        /* Trace the job if we're doing that and do a memory dump. We may
         * want to adjust this logic once we're ready to trace FBOs */
        pantrace_submit_job(submit.jc, submit.requirements, FALSE);

	return 0;
}

static int
panfrost_drm_submit_vs_fs_job(struct panfrost_context *ctx, bool has_draws, bool is_scanout)
{
        struct pipe_surface *surf = ctx->pipe_framebuffer.cbufs[0];
	int ret;

        if (has_draws) {
		ret = panfrost_drm_submit_job(ctx, ctx->set_value_job, 0, NULL);
		assert(!ret);
	}

	ret = panfrost_drm_submit_job(ctx, panfrost_fragment_job(ctx), PANFROST_JD_REQ_FS, surf);

        return ret;
}

static struct panfrost_fence *
panfrost_fence_create(struct panfrost_context *ctx)
{
        struct pipe_context *gallium = (struct pipe_context *) ctx;
        struct panfrost_screen *screen = pan_screen(gallium->screen);
	struct panfrost_drm *drm = (struct panfrost_drm *)screen->driver;
        struct panfrost_fence *f = calloc(1, sizeof(*f));
        if (!f)
                return NULL;

        /* Snapshot the last Panfrost's rendering's out fence.  We'd rather have
         * another syncobj instead of a sync file, but this is all we get.
         * (HandleToFD/FDToHandle just gives you another syncobj ID for the
         * same syncobj).
         */
        drmSyncobjExportSyncFile(drm->fd, ctx->out_sync, &f->fd);
        if (f->fd == -1) {
                fprintf(stderr, "export failed\n");
                free(f);
                return NULL;
        }

        pipe_reference_init(&f->reference, 1);

        return f;
}

static void
panfrost_drm_force_flush_fragment(struct panfrost_context *ctx,
				  struct pipe_fence_handle **fence)
{
        struct pipe_context *gallium = (struct pipe_context *) ctx;
        struct panfrost_screen *screen = pan_screen(gallium->screen);
        struct panfrost_drm *drm = (struct panfrost_drm *)screen->driver;

        if (!screen->last_fragment_flushed) {
		drmSyncobjWait(drm->fd, &ctx->out_sync, 1, INT64_MAX, 0, NULL);
                screen->last_fragment_flushed = true;

                /* The job finished up, so we're safe to clean it up now */
                panfrost_free_job(ctx, screen->last_job);
	}

        if (fence) {
                struct panfrost_fence *f = panfrost_fence_create(ctx);
                gallium->screen->fence_reference(gallium->screen, fence, NULL);
                *fence = (struct pipe_fence_handle *)f;
        }
}

static void
panfrost_drm_enable_counters(struct panfrost_screen *screen)
{
	fprintf(stderr, "unimplemented: %s\n", __func__);
}

static void
panfrost_drm_dump_counters(struct panfrost_screen *screen)
{
	fprintf(stderr, "unimplemented: %s\n", __func__);
}

static unsigned
panfrost_drm_query_gpu_version(struct panfrost_screen *screen)
{
	struct panfrost_drm *drm = (struct panfrost_drm *)screen->driver;
        struct drm_panfrost_get_param get_param = {0,};
        int ret;

	get_param.param = DRM_PANFROST_PARAM_GPU_ID;
        ret = drmIoctl(drm->fd, DRM_IOCTL_PANFROST_GET_PARAM, &get_param);
        assert(!ret);

	return get_param.value;
}

static int
panfrost_drm_init_context(struct panfrost_context *ctx)
{
        struct pipe_context *gallium = (struct pipe_context *) ctx;
        struct panfrost_screen *screen = pan_screen(gallium->screen);
	struct panfrost_drm *drm = (struct panfrost_drm *)screen->driver;

        return drmSyncobjCreate(drm->fd, DRM_SYNCOBJ_CREATE_SIGNALED,
                                &ctx->out_sync);
}

static void
panfrost_drm_fence_reference(struct pipe_screen *screen,
                         struct pipe_fence_handle **ptr,
                         struct pipe_fence_handle *fence)
{
        struct panfrost_fence **p = (struct panfrost_fence **)ptr;
        struct panfrost_fence *f = (struct panfrost_fence *)fence;
        struct panfrost_fence *old = *p;

        if (pipe_reference(&(*p)->reference, &f->reference)) {
                close(old->fd);
                free(old);
        }
        *p = f;
}

static boolean
panfrost_drm_fence_finish(struct pipe_screen *pscreen,
                      struct pipe_context *ctx,
                      struct pipe_fence_handle *fence,
                      uint64_t timeout)
{
        struct panfrost_screen *screen = pan_screen(pscreen);
	struct panfrost_drm *drm = (struct panfrost_drm *)screen->driver;
        struct panfrost_fence *f = (struct panfrost_fence *)fence;
        int ret;

        unsigned syncobj;
        ret = drmSyncobjCreate(drm->fd, 0, &syncobj);
        if (ret) {
                fprintf(stderr, "Failed to create syncobj to wait on: %m\n");
                return false;
        }

        drmSyncobjImportSyncFile(drm->fd, syncobj, f->fd);
        if (ret) {
                fprintf(stderr, "Failed to import fence to syncobj: %m\n");
                return false;
        }

        uint64_t abs_timeout = os_time_get_absolute_timeout(timeout);
        if (abs_timeout == OS_TIMEOUT_INFINITE)
                abs_timeout = INT64_MAX;

        ret = drmSyncobjWait(drm->fd, &syncobj, 1, abs_timeout, 0, NULL);

        drmSyncobjDestroy(drm->fd, syncobj);

        return ret >= 0;
}

struct panfrost_driver *
panfrost_create_drm_driver(int fd)
{
	struct panfrost_drm *driver = CALLOC_STRUCT(panfrost_drm);

	driver->fd = fd;

	driver->base.import_bo = panfrost_drm_import_bo;
	driver->base.export_bo = panfrost_drm_export_bo;
	driver->base.free_imported_bo = panfrost_drm_free_imported_bo;
	driver->base.submit_vs_fs_job = panfrost_drm_submit_vs_fs_job;
	driver->base.force_flush_fragment = panfrost_drm_force_flush_fragment;
	driver->base.allocate_slab = panfrost_drm_allocate_slab;
	driver->base.free_slab = panfrost_drm_free_slab;
	driver->base.enable_counters = panfrost_drm_enable_counters;
	driver->base.query_gpu_version = panfrost_drm_query_gpu_version;
	driver->base.init_context = panfrost_drm_init_context;
	driver->base.fence_reference = panfrost_drm_fence_reference;
	driver->base.fence_finish = panfrost_drm_fence_finish;
	driver->base.dump_counters = panfrost_drm_dump_counters;

        return &driver->base;
}
