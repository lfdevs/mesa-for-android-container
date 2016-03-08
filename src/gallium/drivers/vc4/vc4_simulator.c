/*
 * Copyright © 2014 Broadcom
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

#ifdef USE_VC4_SIMULATOR

#include "util/u_memory.h"
#include "util/ralloc.h"

#include "vc4_screen.h"
#include "vc4_context.h"
#include "kernel/vc4_drv.h"
#include "vc4_simulator_validate.h"
#include "simpenrose/simpenrose.h"

/* A marker placed just after each BO, then checked after rendering to make
 * sure it's still there.
 */
#define BO_SENTINEL		0xfedcba98

#define OVERFLOW_SIZE (32 * 1024 * 1024)

static struct drm_gem_cma_object *
vc4_wrap_bo_with_cma(struct drm_device *dev, struct vc4_bo *bo)
{
        struct vc4_context *vc4 = dev->vc4;
        struct vc4_screen *screen = vc4->screen;
        struct drm_vc4_bo *drm_bo = CALLOC_STRUCT(drm_vc4_bo);
        struct drm_gem_cma_object *obj = &drm_bo->base;
        uint32_t size = align(bo->size, 4096);

        drm_bo->bo = bo;
        obj->base.size = size;
        obj->base.dev = dev;
        obj->vaddr = screen->simulator_mem_base + dev->simulator_mem_next;
        obj->paddr = simpenrose_hw_addr(obj->vaddr);

        dev->simulator_mem_next += size + sizeof(uint32_t);
        dev->simulator_mem_next = align(dev->simulator_mem_next, 4096);
        assert(dev->simulator_mem_next <= screen->simulator_mem_size);

        *(uint32_t *)(obj->vaddr + bo->size) = BO_SENTINEL;

        return obj;
}

struct drm_gem_cma_object *
drm_gem_cma_create(struct drm_device *dev, size_t size)
{
        struct vc4_context *vc4 = dev->vc4;
        struct vc4_screen *screen = vc4->screen;

        struct vc4_bo *bo = vc4_bo_alloc(screen, size, "simulator validate");
        return vc4_wrap_bo_with_cma(dev, bo);
}

static int
vc4_simulator_pin_bos(struct drm_device *dev, struct vc4_exec_info *exec)
{
        struct drm_vc4_submit_cl *args = exec->args;
        struct vc4_context *vc4 = dev->vc4;
        struct vc4_bo **bos = vc4->bo_pointers.base;

        exec->bo_count = args->bo_handle_count;
        exec->bo = calloc(exec->bo_count, sizeof(void *));
        for (int i = 0; i < exec->bo_count; i++) {
                struct vc4_bo *bo = bos[i];
                struct drm_gem_cma_object *obj = vc4_wrap_bo_with_cma(dev, bo);

                struct drm_vc4_bo *drm_bo = to_vc4_bo(&obj->base);
#if 0
                fprintf(stderr, "bo hindex %d: %s\n", i, bo->name);
#endif

                vc4_bo_map(bo);
                memcpy(obj->vaddr, bo->map, bo->size);

                exec->bo[i] = obj;

                /* The kernel does this validation at shader create ioctl
                 * time.
                 */
                if (strcmp(bo->name, "code") == 0) {
                        drm_bo->validated_shader = vc4_validate_shader(obj);
                        if (!drm_bo->validated_shader)
                                abort();
                }
        }
        return 0;
}

static int
vc4_simulator_unpin_bos(struct vc4_exec_info *exec)
{
        for (int i = 0; i < exec->bo_count; i++) {
                struct drm_gem_cma_object *obj = exec->bo[i];
                struct drm_vc4_bo *drm_bo = to_vc4_bo(&obj->base);
                struct vc4_bo *bo = drm_bo->bo;

                assert(*(uint32_t *)(obj->vaddr + bo->size) == BO_SENTINEL);
                memcpy(bo->map, obj->vaddr, bo->size);

                if (drm_bo->validated_shader) {
                        free(drm_bo->validated_shader->texture_samples);
                        free(drm_bo->validated_shader);
                }
                free(obj);
        }

        free(exec->bo);

        return 0;
}

static void
vc4_dump_to_file(struct vc4_exec_info *exec)
{
        static int dumpno = 0;
        struct drm_vc4_get_hang_state *state;
        struct drm_vc4_get_hang_state_bo *bo_state;
        unsigned int dump_version = 0;

        if (!(vc4_debug & VC4_DEBUG_DUMP))
                return;

        state = calloc(1, sizeof(*state));

        int unref_count = 0;
        list_for_each_entry_safe(struct drm_vc4_bo, bo, &exec->unref_list,
                                 unref_head) {
                unref_count++;
        }

        /* Add one more for the overflow area that isn't wrapped in a BO. */
        state->bo_count = exec->bo_count + unref_count + 1;
        bo_state = calloc(state->bo_count, sizeof(*bo_state));

        char *filename = NULL;
        asprintf(&filename, "vc4-dri-%d.dump", dumpno++);
        FILE *f = fopen(filename, "w+");
        if (!f) {
                fprintf(stderr, "Couldn't open %s: %s", filename,
                        strerror(errno));
                return;
        }

        fwrite(&dump_version, sizeof(dump_version), 1, f);

        state->ct0ca = exec->ct0ca;
        state->ct0ea = exec->ct0ea;
        state->ct1ca = exec->ct1ca;
        state->ct1ea = exec->ct1ea;
        state->start_bin = exec->ct0ca;
        state->start_render = exec->ct1ca;
        fwrite(state, sizeof(*state), 1, f);

        int i;
        for (i = 0; i < exec->bo_count; i++) {
                struct drm_gem_cma_object *cma_bo = exec->bo[i];
                bo_state[i].handle = i; /* Not used by the parser. */
                bo_state[i].paddr = cma_bo->paddr;
                bo_state[i].size = cma_bo->base.size;
        }

        list_for_each_entry_safe(struct drm_vc4_bo, bo, &exec->unref_list,
                                 unref_head) {
                struct drm_gem_cma_object *cma_bo = &bo->base;
                bo_state[i].handle = 0;
                bo_state[i].paddr = cma_bo->paddr;
                bo_state[i].size = cma_bo->base.size;
                i++;
        }

        /* Add the static overflow memory area. */
        bo_state[i].handle = exec->bo_count;
        bo_state[i].paddr = 0;
        bo_state[i].size = OVERFLOW_SIZE;
        i++;

        fwrite(bo_state, sizeof(*bo_state), state->bo_count, f);

        for (int i = 0; i < exec->bo_count; i++) {
                struct drm_gem_cma_object *cma_bo = exec->bo[i];
                fwrite(cma_bo->vaddr, cma_bo->base.size, 1, f);
        }

        list_for_each_entry_safe(struct drm_vc4_bo, bo, &exec->unref_list,
                                 unref_head) {
                struct drm_gem_cma_object *cma_bo = &bo->base;
                fwrite(cma_bo->vaddr, cma_bo->base.size, 1, f);
        }

        void *overflow = calloc(1, OVERFLOW_SIZE);
        fwrite(overflow, 1, OVERFLOW_SIZE, f);
        free(overflow);

        free(state);
        free(bo_state);
        fclose(f);
}

int
vc4_simulator_flush(struct vc4_context *vc4, struct drm_vc4_submit_cl *args)
{
        struct vc4_screen *screen = vc4->screen;
        struct vc4_surface *csurf = vc4_surface(vc4->framebuffer.cbufs[0]);
        struct vc4_resource *ctex = csurf ? vc4_resource(csurf->base.texture) : NULL;
        uint32_t winsys_stride = ctex ? ctex->bo->simulator_winsys_stride : 0;
        uint32_t sim_stride = ctex ? ctex->slices[0].stride : 0;
        uint32_t row_len = MIN2(sim_stride, winsys_stride);
        struct vc4_exec_info exec;
        struct drm_device local_dev = {
                .vc4 = vc4,
                .simulator_mem_next = OVERFLOW_SIZE,
        };
        struct drm_device *dev = &local_dev;
        int ret;

        memset(&exec, 0, sizeof(exec));
        list_inithead(&exec.unref_list);

        if (ctex && ctex->bo->simulator_winsys_map) {
#if 0
                fprintf(stderr, "%dx%d %d %d %d\n",
                        ctex->base.b.width0, ctex->base.b.height0,
                        winsys_stride,
                        sim_stride,
                        ctex->bo->size);
#endif

                for (int y = 0; y < ctex->base.b.height0; y++) {
                        memcpy(ctex->bo->map + y * sim_stride,
                               ctex->bo->simulator_winsys_map + y * winsys_stride,
                               row_len);
                }
        }

        exec.args = args;

        ret = vc4_simulator_pin_bos(dev, &exec);
        if (ret)
                return ret;

        ret = vc4_cl_validate(dev, &exec);
        if (ret)
                return ret;

        if (vc4_debug & VC4_DEBUG_CL) {
                fprintf(stderr, "RCL:\n");
                vc4_dump_cl(screen->simulator_mem_base + exec.ct1ca,
                            exec.ct1ea - exec.ct1ca, true);
        }

        vc4_dump_to_file(&exec);

        if (exec.ct0ca != exec.ct0ea) {
                int bfc = simpenrose_do_binning(exec.ct0ca, exec.ct0ea);
                if (bfc != 1) {
                        fprintf(stderr, "Binning returned %d flushes, should be 1.\n",
                                bfc);
                        fprintf(stderr, "Relocated binning command list:\n");
                        vc4_dump_cl(screen->simulator_mem_base + exec.ct0ca,
                                    exec.ct0ea - exec.ct0ca, false);
                        abort();
                }
        }
        int rfc = simpenrose_do_rendering(exec.ct1ca, exec.ct1ea);
        if (rfc != 1) {
                fprintf(stderr, "Rendering returned %d frames, should be 1.\n",
                        rfc);
                fprintf(stderr, "Relocated render command list:\n");
                vc4_dump_cl(screen->simulator_mem_base + exec.ct1ca,
                            exec.ct1ea - exec.ct1ca, true);
                abort();
        }

        ret = vc4_simulator_unpin_bos(&exec);
        if (ret)
                return ret;

        list_for_each_entry_safe(struct drm_vc4_bo, bo, &exec.unref_list,
                                 unref_head) {
		list_del(&bo->unref_head);
                assert(*(uint32_t *)(bo->base.vaddr + bo->bo->size) ==
                       BO_SENTINEL);
                vc4_bo_unreference(&bo->bo);
                free(bo);
        }

        if (ctex && ctex->bo->simulator_winsys_map) {
                for (int y = 0; y < ctex->base.b.height0; y++) {
                        memcpy(ctex->bo->simulator_winsys_map + y * winsys_stride,
                               ctex->bo->map + y * sim_stride,
                               row_len);
                }
        }

        return 0;
}

void
vc4_simulator_init(struct vc4_screen *screen)
{
        screen->simulator_mem_size = 256 * 1024 * 1024;
        screen->simulator_mem_base = ralloc_size(screen,
                                                 screen->simulator_mem_size);

        /* We supply our own memory so that we can have more aperture
         * available (256MB instead of simpenrose's default 64MB).
         */
        simpenrose_init_hardware_supply_mem(screen->simulator_mem_base,
                                            screen->simulator_mem_size);

        /* Carve out low memory for tile allocation overflow.  The kernel
         * should be automatically handling overflow memory setup on real
         * hardware, but for simulation we just get one shot to set up enough
         * overflow memory before execution.  This overflow mem will be used
         * up over the whole lifetime of simpenrose (not reused on each
         * flush), so it had better be big.
         */
        simpenrose_supply_overflow_mem(0, OVERFLOW_SIZE);
}

#endif /* USE_VC4_SIMULATOR */
