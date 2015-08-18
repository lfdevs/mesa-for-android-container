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

#ifndef VC4_CL_H
#define VC4_CL_H

#include <stdint.h>

#include "util/u_math.h"
#include "util/macros.h"

#include "kernel/vc4_packet.h"

struct vc4_bo;

/**
 * Undefined structure, used for typechecking that you're passing the pointers
 * to these functions correctly.
 */
struct vc4_cl_out;

struct vc4_cl {
        void *base;
        struct vc4_cl_out *next;
        struct vc4_cl_out *reloc_next;
        uint32_t size;
#ifdef DEBUG
        uint32_t reloc_count;
#endif
};

void vc4_init_cl(struct vc4_context *vc4, struct vc4_cl *cl);
void vc4_reset_cl(struct vc4_cl *cl);
void vc4_dump_cl(void *cl, uint32_t size, bool is_render);
uint32_t vc4_gem_hindex(struct vc4_context *vc4, struct vc4_bo *bo);

struct PACKED unaligned_16 { uint16_t x; };
struct PACKED unaligned_32 { uint32_t x; };

static inline uint32_t cl_offset(struct vc4_cl *cl)
{
        return (char *)cl->next - (char *)cl->base;
}

static inline void
cl_advance(struct vc4_cl_out **cl, uint32_t n)
{
        (*cl) = (struct vc4_cl_out *)((char *)(*cl) + n);
}

static inline struct vc4_cl_out *
cl_start(struct vc4_cl *cl)
{
        return cl->next;
}

static inline void
cl_end(struct vc4_cl *cl, struct vc4_cl_out *next)
{
        cl->next = next;
        assert(cl_offset(cl) <= cl->size);
}


static inline void
put_unaligned_32(struct vc4_cl_out *ptr, uint32_t val)
{
        struct unaligned_32 *p = (void *)ptr;
        p->x = val;
}

static inline void
put_unaligned_16(struct vc4_cl_out *ptr, uint16_t val)
{
        struct unaligned_16 *p = (void *)ptr;
        p->x = val;
}

static inline void
cl_u8(struct vc4_cl_out **cl, uint8_t n)
{
        *(uint8_t *)(*cl) = n;
        cl_advance(cl, 1);
}

static inline void
cl_u16(struct vc4_cl_out **cl, uint16_t n)
{
        put_unaligned_16(*cl, n);
        cl_advance(cl, 2);
}

static inline void
cl_u32(struct vc4_cl_out **cl, uint32_t n)
{
        put_unaligned_32(*cl, n);
        cl_advance(cl, 4);
}

static inline void
cl_aligned_u32(struct vc4_cl_out **cl, uint32_t n)
{
        *(uint32_t *)(*cl) = n;
        cl_advance(cl, 4);
}

static inline void
cl_ptr(struct vc4_cl_out **cl, void *ptr)
{
        *(struct vc4_cl_out **)(*cl) = ptr;
        cl_advance(cl, sizeof(void *));
}

static inline void
cl_f(struct vc4_cl_out **cl, float f)
{
        cl_u32(cl, fui(f));
}

static inline void
cl_aligned_f(struct vc4_cl_out **cl, float f)
{
        cl_aligned_u32(cl, fui(f));
}

static inline void
cl_start_reloc(struct vc4_cl *cl, struct vc4_cl_out **out, uint32_t n)
{
        assert(n == 1 || n == 2);
#ifdef DEBUG
        assert(cl->reloc_count == 0);
        cl->reloc_count = n;
#endif

        cl_u8(out, VC4_PACKET_GEM_HANDLES);
        cl->reloc_next = *out;
        cl_u32(out, 0); /* Space where hindex will be written. */
        cl_u32(out, 0); /* Space where hindex will be written. */
}

static inline struct vc4_cl_out *
cl_start_shader_reloc(struct vc4_cl *cl, uint32_t n)
{
#ifdef DEBUG
        assert(cl->reloc_count == 0);
        cl->reloc_count = n;
#endif
        cl->reloc_next = cl->next;

        /* Reserve the space where hindex will be written. */
        cl_advance(&cl->next, n * 4);

        return cl->next;
}

static inline void
cl_reloc(struct vc4_context *vc4, struct vc4_cl *cl, struct vc4_cl_out **cl_out,
         struct vc4_bo *bo, uint32_t offset)
{
        *(uint32_t *)cl->reloc_next = vc4_gem_hindex(vc4, bo);
        cl_advance(&cl->reloc_next, 4);

#ifdef DEBUG
        cl->reloc_count--;
#endif

        cl_u32(cl_out, offset);
}

static inline void
cl_aligned_reloc(struct vc4_context *vc4, struct vc4_cl *cl,
                 struct vc4_cl_out **cl_out,
                 struct vc4_bo *bo, uint32_t offset)
{
        *(uint32_t *)cl->reloc_next = vc4_gem_hindex(vc4, bo);
        cl_advance(&cl->reloc_next, 4);

#ifdef DEBUG
        cl->reloc_count--;
#endif

        cl_aligned_u32(cl_out, offset);
}

void cl_ensure_space(struct vc4_cl *cl, uint32_t size);

#endif /* VC4_CL_H */
