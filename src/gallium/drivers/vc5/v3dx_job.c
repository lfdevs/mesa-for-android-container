/*
 * Copyright © 2014-2017 Broadcom
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/** @file v3dx_job.c
 *
 * V3D version-specific functions for submitting VC5 render jobs to the
 * kernel.
 */

#include "vc5_context.h"
#include "broadcom/cle/v3dx_pack.h"

void v3dX(bcl_epilogue)(struct vc5_context *vc5, struct vc5_job *job)
{
                vc5_cl_ensure_space_with_branch(&job->bcl,
                                                cl_packet_length(OCCLUSION_QUERY_COUNTER) +
#if V3D_VERSION >= 41
                                                cl_packet_length(TRANSFORM_FEEDBACK_SPECS) +
#endif
                                                cl_packet_length(INCREMENT_SEMAPHORE) +
                                                cl_packet_length(FLUSH_ALL_STATE));

                if (job->oq_enabled) {
                        /* Disable the OQ at the end of the CL, so that the
                         * draw calls at the start of the CL don't inherit the
                         * OQ counter.
                         */
                        cl_emit(&job->bcl, OCCLUSION_QUERY_COUNTER, counter);
                }

                /* Disable TF at the end of the CL, so that the next job to be
                 * run doesn't start out trying to write TF primitives.  On
                 * V3D 3.x, it's only the TF primitive mode that triggers TF
                 * writes.
                 */
#if V3D_VERSION >= 41
                if (job->tf_enabled) {
                        cl_emit(&job->bcl, TRANSFORM_FEEDBACK_SPECS, tfe) {
                                tfe.enable = false;
                        };
                }
#endif /* V3D_VERSION >= 41 */

                /* Increment the semaphore indicating that binning is done and
                 * unblocking the render thread.  Note that this doesn't act
                 * until the FLUSH completes.
                 */
                cl_emit(&job->bcl, INCREMENT_SEMAPHORE, incr);

                /* The FLUSH_ALL emits any unwritten state changes in each
                 * tile.  We can use this to reset any state that needs to be
                 * present at the start of the next tile, as we do with
                 * OCCLUSION_QUERY_COUNTER above.
                 */
                cl_emit(&job->bcl, FLUSH_ALL_STATE, flush);
}
