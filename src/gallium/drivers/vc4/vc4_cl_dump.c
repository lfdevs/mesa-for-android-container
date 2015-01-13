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

#include "util/u_math.h"
#include "util/macros.h"
#include "vc4_context.h"

#define dump_VC4_PACKET_LINE_WIDTH dump_float
#define dump_VC4_PACKET_POINT_SIZE dump_float

static void
dump_float(void *cl, uint32_t offset, uint32_t hw_offset)
{
        void *f = cl + offset;

        fprintf(stderr, "0x%08x 0x%08x:      %f (0x%08x)\n",
                offset, hw_offset, *(float *)f, *(uint32_t *)f);
}

static void
dump_VC4_PACKET_BRANCH_TO_SUB_LIST(void *cl, uint32_t offset, uint32_t hw_offset)
{
        uint32_t *addr = cl + offset;

        fprintf(stderr, "0x%08x 0x%08x:      addr 0x%08x\n",
                offset, hw_offset, *addr);
}

static void
dump_VC4_PACKET_FLAT_SHADE_FLAGS(void *cl, uint32_t offset, uint32_t hw_offset)
{
        uint32_t *bits = cl + offset;

        fprintf(stderr, "0x%08x 0x%08x:      bits 0x%08x\n",
                offset, hw_offset, *bits);
}

static void
dump_VC4_PACKET_VIEWPORT_OFFSET(void *cl, uint32_t offset, uint32_t hw_offset)
{
        uint16_t *o = cl + offset;

        fprintf(stderr, "0x%08x 0x%08x:      %f, %f (0x%04x, 0x%04x)\n",
                offset, hw_offset,
                o[0] / 16.0, o[1] / 16.0,
                o[0], o[1]);
}

static void
dump_VC4_PACKET_CLIPPER_XY_SCALING(void *cl, uint32_t offset, uint32_t hw_offset)
{
        uint32_t *scale = cl + offset;

        fprintf(stderr, "0x%08x 0x%08x:      %f, %f (%f, %f, 0x%08x, 0x%08x)\n",
                offset, hw_offset,
                uif(scale[0]) / 16.0, uif(scale[1]) / 16.0,
                uif(scale[0]), uif(scale[1]),
                scale[0], scale[1]);
}

static void
dump_VC4_PACKET_CLIPPER_Z_SCALING(void *cl, uint32_t offset, uint32_t hw_offset)
{
        uint32_t *translate = cl + offset;
        uint32_t *scale = cl + offset + 8;

        fprintf(stderr, "0x%08x 0x%08x:      %f, %f (0x%08x, 0x%08x)\n",
                offset, hw_offset,
                uif(translate[0]), uif(translate[1]),
                translate[0], translate[1]);

        fprintf(stderr, "0x%08x 0x%08x:      %f, %f (0x%08x, 0x%08x)\n",
                offset + 8, hw_offset + 8,
                uif(scale[0]), uif(scale[1]),
                scale[0], scale[1]);
}

static void
dump_VC4_PACKET_TILE_COORDINATES(void *cl, uint32_t offset, uint32_t hw_offset)
{
        uint8_t *tilecoords = cl + offset;

        fprintf(stderr, "0x%08x 0x%08x:      %d, %d\n",
                offset, hw_offset, tilecoords[0], tilecoords[1]);
}

static void
dump_VC4_PACKET_GEM_HANDLES(void *cl, uint32_t offset, uint32_t hw_offset)
{
        uint32_t *handles = cl + offset;

        fprintf(stderr, "0x%08x 0x%08x:      handle 0: %d, handle 1: %d\n",
                offset, hw_offset, handles[0], handles[1]);
}

#define PACKET_DUMP(name, size) [name] = { #name, size, dump_##name }
#define PACKET(name, size) [name] = { #name, size, NULL }

static const struct packet_info {
        const char *name;
        uint8_t size;
        void (*dump_func)(void *cl, uint32_t offset, uint32_t hw_offset);
} packet_info[] = {
        PACKET(VC4_PACKET_HALT, 1),
        PACKET(VC4_PACKET_NOP, 1),

        PACKET(VC4_PACKET_FLUSH, 1),
        PACKET(VC4_PACKET_FLUSH_ALL, 1),
        PACKET(VC4_PACKET_START_TILE_BINNING, 1),
        PACKET(VC4_PACKET_INCREMENT_SEMAPHORE, 1),
        PACKET(VC4_PACKET_WAIT_ON_SEMAPHORE, 1),

        PACKET(VC4_PACKET_BRANCH, 5),
        PACKET_DUMP(VC4_PACKET_BRANCH_TO_SUB_LIST, 5),

        PACKET(VC4_PACKET_STORE_MS_TILE_BUFFER, 1),
        PACKET(VC4_PACKET_STORE_MS_TILE_BUFFER_AND_EOF, 1),
        PACKET(VC4_PACKET_STORE_FULL_RES_TILE_BUFFER, 5),
        PACKET(VC4_PACKET_LOAD_FULL_RES_TILE_BUFFER, 5),
        PACKET(VC4_PACKET_STORE_TILE_BUFFER_GENERAL, 7),
        PACKET(VC4_PACKET_LOAD_TILE_BUFFER_GENERAL, 7),

        PACKET(VC4_PACKET_GL_INDEXED_PRIMITIVE, 14),
        PACKET(VC4_PACKET_GL_ARRAY_PRIMITIVE, 10),

        PACKET(VC4_PACKET_COMPRESSED_PRIMITIVE, 48),
        PACKET(VC4_PACKET_CLIPPED_COMPRESSED_PRIMITIVE, 49),

        PACKET(VC4_PACKET_PRIMITIVE_LIST_FORMAT, 2),

        PACKET(VC4_PACKET_GL_SHADER_STATE, 5),
        PACKET(VC4_PACKET_NV_SHADER_STATE, 5),
        PACKET(VC4_PACKET_VG_SHADER_STATE, 5),

        PACKET(VC4_PACKET_CONFIGURATION_BITS, 4),
        PACKET_DUMP(VC4_PACKET_FLAT_SHADE_FLAGS, 5),
        PACKET_DUMP(VC4_PACKET_POINT_SIZE, 5),
        PACKET_DUMP(VC4_PACKET_LINE_WIDTH, 5),
        PACKET(VC4_PACKET_RHT_X_BOUNDARY, 3),
        PACKET(VC4_PACKET_DEPTH_OFFSET, 5),
        PACKET(VC4_PACKET_CLIP_WINDOW, 9),
        PACKET_DUMP(VC4_PACKET_VIEWPORT_OFFSET, 5),
        PACKET(VC4_PACKET_Z_CLIPPING, 9),
        PACKET_DUMP(VC4_PACKET_CLIPPER_XY_SCALING, 9),
        PACKET_DUMP(VC4_PACKET_CLIPPER_Z_SCALING, 9),

        PACKET(VC4_PACKET_TILE_BINNING_MODE_CONFIG, 16),
        PACKET(VC4_PACKET_TILE_RENDERING_MODE_CONFIG, 11),
        PACKET(VC4_PACKET_CLEAR_COLORS, 14),
        PACKET_DUMP(VC4_PACKET_TILE_COORDINATES, 3),

        PACKET_DUMP(VC4_PACKET_GEM_HANDLES, 9),
};

void
vc4_dump_cl(void *cl, uint32_t size, bool is_render)
{
        uint32_t offset = 0, hw_offset = 0;
        uint8_t *cmds = cl;

        while (offset < size) {
                uint8_t header = cmds[offset];

                if (header > ARRAY_SIZE(packet_info) ||
                    !packet_info[header].name) {
                        fprintf(stderr, "0x%08x 0x%08x: Unknown packet 0x%02x (%d)!\n",
                                offset, hw_offset, header, header);
                        return;
                }

                const struct packet_info *p = packet_info + header;
                fprintf(stderr, "0x%08x 0x%08x: 0x%02x %s\n",
                        offset,
                        header != VC4_PACKET_GEM_HANDLES ? hw_offset : 0,
                        header, p->name);

                if (offset + p->size <= size &&
                    p->dump_func) {
                        p->dump_func(cmds, offset + 1, hw_offset + 1);
                } else {
                        for (uint32_t i = 1; i < p->size; i++) {
                                if (offset + i >= size) {
                                        fprintf(stderr, "0x%08x 0x%08x: CL overflow!\n",
                                                offset + i, hw_offset + i);
                                        return;
                                }
                                fprintf(stderr, "0x%08x 0x%08x: 0x%02x\n",
                                        offset + i,
                                        header != VC4_PACKET_GEM_HANDLES ? hw_offset + i : 0,
                                        cmds[offset + i]);
                        }
                }

                switch (header) {
                case VC4_PACKET_HALT:
                case VC4_PACKET_STORE_MS_TILE_BUFFER_AND_EOF:
                        return;
                default:
                        break;
                }

                offset += p->size;
                if (header != VC4_PACKET_GEM_HANDLES)
                        hw_offset += p->size;
        }
}

