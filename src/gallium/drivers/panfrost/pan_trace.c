/*
 * Copyright (C) 2019 Alyssa Rosenzweig
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

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <panfrost-job.h>
#include "pan_trace.h"
#include "util/list.h"

/* The pandecode utility is capable of parsing a command stream trace and
 * disassembling any referenced shaders. Traces themselves are glorified memory
 * dumps, a directory consisting of .bin's for each memory segment, and a
 * simple plain-text description of the interesting kernel activity.
 * Historically, these dumps have been produced via panwrap, an LD_PRELOAD shim
 * sitting between the driver and the kernel. However, for modern Panfrost, we
 * can just produce the dumps ourselves, which is rather less fragile. This
 * file (pantrace) implements this functionality. */

static FILE *pan_control_log;
static const char *pan_control_base;

/* Represent the abstraction for a single mmap chunk */

static unsigned pantrace_memory_count = 0;

struct pantrace_memory {
        struct list_head node;

        mali_ptr gpu;
        void *cpu;
        size_t sz;
        char *full_filename;
};

static struct pantrace_memory mmaps;

void
pantrace_initialize(const char *base)
{
        /* Open the control.log */
        char fn[128];
        snprintf(fn, 128, "%s/control.log", base);
        pan_control_log = fopen(fn, "w+");
        assert(pan_control_log);

        /* Save the base for later */
        pan_control_base = base;

        /* Initialize the mmap list */
        list_inithead(&mmaps.node);
}

static bool
pantrace_is_initialized(void)
{
        return pan_control_log && pan_control_base;
}

/* Traces a submitted job with a given job chain, core requirements, and
 * platform */

void
pantrace_submit_job(mali_ptr jc, unsigned core_req, unsigned is_bifrost)
{
        if (!pantrace_is_initialized())
                return;

        fprintf(pan_control_log, "JS %" PRIx64 " %x %x\n",
                        jc, core_req, is_bifrost);
        fflush(pan_control_log);
}

/* Dumps a given mapped memory buffer with the given label. If no label
 * is given (label == NULL), one is created */

void
pantrace_mmap(mali_ptr gpu, void *cpu, size_t sz, char *label)
{
        if (!pantrace_is_initialized())
                return;

        char *filename = NULL;
        char *full_filename = NULL;

        /* Create a filename based on the label or count */

        if (label) {
                asprintf(&filename, "%s.bin", label);
        } else {
                asprintf(&filename, "memory_%d.bin", pantrace_memory_count++);
        }

        /* Emit an mmap for it */
        fprintf(pan_control_log, "MMAP %" PRIx64 " %s\n", gpu, filename);
        fflush(pan_control_log);

        /* Dump the memory itself */
        asprintf(&full_filename, "%s/%s", pan_control_base, filename);
        free(filename);

        struct pantrace_memory *mem = malloc(sizeof(*mem));
        list_inithead(&mem->node);
        mem->gpu = gpu;
        mem->cpu = cpu;
        mem->sz = sz;
        mem->full_filename = full_filename;
        list_add(&mem->node, &mmaps.node);
}

/* Dump all memory at once, once everything has been written */

void
pantrace_dump_memory(void)
{
        if (!pantrace_is_initialized())
                return;

        list_for_each_entry(struct pantrace_memory, pos, &mmaps.node, node) {
                /* Save the mapping */
                FILE *fp = fopen(pos->full_filename, "wb");
                fwrite(pos->cpu, 1, pos->sz, fp);
                fclose(fp);
        }
}
