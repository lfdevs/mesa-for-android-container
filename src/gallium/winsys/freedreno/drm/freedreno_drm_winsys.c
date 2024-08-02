/*
 * Copyright (C) 2012 Rob Clark <robclark@freedesktop.org>
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
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#include "util/u_inlines.h"
#include "util/u_screen.h"

#include "freedreno_drm_public.h"

#include "freedreno/freedreno_screen.h"

#include "virtio/virtio-gpu/drm_hw.h"

struct pipe_screen *
fd_drm_screen_create_renderonly(int fd, struct renderonly *ro,
		const struct pipe_screen_config *config)
{
	return u_pipe_screen_lookup_or_create(fd, config, ro, fd_screen_create);
}

/**
 * Check if the native-context type exposed by virtgpu is one we
 * support, and that we support the underlying device.
 */
bool
fd_drm_probe_nctx(int fd, const struct virgl_renderer_capset_drm *caps)
{
	if (caps->context_type != VIRTGPU_DRM_CONTEXT_MSM)
		return false;

	struct fd_dev_id dev_id = {
		.gpu_id = caps->u.msm.gpu_id,
		.chip_id = caps->u.msm.chip_id,
	};
	const struct fd_dev_info info = fd_dev_info(&dev_id);

	return info.chip != 0;
}
