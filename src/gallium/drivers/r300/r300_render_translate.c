/*
 * Copyright 2010 Marek Olšák <maraeo@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "r300_context.h"
#include "util/u_endian.h"
#include "util/u_index_modify.h"
#include "util/u_upload_mgr.h"

#if UTIL_ARCH_BIG_ENDIAN
/* The VAP endian-swap mode is global, so keep BE index streams 32-bit like
 * translated vertex attributes.
 */
void r300_rebuild_elts_to_uint_userptr(struct pipe_context *context,
                                       const struct pipe_draw_info *info,
                                       unsigned add_transfer_flags,
                                       int index_bias,
                                       unsigned start,
                                       unsigned count,
                                       void *out)
{
    struct pipe_transfer *in_transfer = NULL;
    const uint8_t *in_map;
    uint32_t *out_map = out;

    if (info->has_user_indices) {
       in_map = info->index.user;
    } else {
       in_map = pipe_buffer_map(context, info->index.resource,
                                PIPE_MAP_READ |
                                add_transfer_flags,
                                &in_transfer);
    }

    in_map += start * info->index_size;

    switch (info->index_size) {
    case 1:
        for (unsigned i = 0; i < count; i++) {
            out_map[i] = in_map[i] + index_bias;
        }
        break;

    case 2: {
        const uint16_t *in_map16 = (const uint16_t *)in_map;

        for (unsigned i = 0; i < count; i++) {
            out_map[i] = in_map16[i] + index_bias;
        }
        break;
    }

    case 4: {
        const uint32_t *in_map32 = (const uint32_t *)in_map;

        for (unsigned i = 0; i < count; i++) {
            out_map[i] = in_map32[i] + index_bias;
        }
        break;
    }
    }

    if (in_transfer)
       pipe_buffer_unmap(context, in_transfer);
}
#endif

void r300_translate_index_buffer(struct r300_context *r300,
                                 const struct pipe_draw_info *info,
                                 struct pipe_resource **out_buffer,
                                 unsigned *index_size, int index_offset,
                                 unsigned *start, unsigned count,
                                 const uint8_t **export_ptr)
{
    unsigned out_offset;
    void **ptr = (void **)export_ptr;

#if UTIL_ARCH_BIG_ENDIAN
    if (*index_size < 4 || index_offset) {
        *out_buffer = NULL;
        u_upload_alloc_ref(r300->uploader, 0, count * sizeof(uint32_t), 4,
                           &out_offset, out_buffer, ptr);

        r300_rebuild_elts_to_uint_userptr(&r300->context, info,
                                          PIPE_MAP_UNSYNCHRONIZED,
                                          index_offset, *start, count, *ptr);

        *index_size = 4;
        *start = out_offset / sizeof(uint32_t);
    }
#else
    switch (*index_size) {
    case 1:
        *out_buffer = NULL;
        u_upload_alloc_ref(r300->uploader, 0, count * 2, 4,
                       &out_offset, out_buffer, ptr);

        util_shorten_ubyte_elts_to_userptr(
                &r300->context, info, PIPE_MAP_UNSYNCHRONIZED, index_offset,
                *start, count, *ptr);

        *index_size = 2;
        *start = out_offset / 2;
        break;

    case 2:
        if (index_offset) {
            *out_buffer = NULL;
            u_upload_alloc_ref(r300->uploader, 0, count * 2, 4,
                           &out_offset, out_buffer, ptr);

            util_rebuild_ushort_elts_to_userptr(&r300->context, info,
                                                PIPE_MAP_UNSYNCHRONIZED,
                                                index_offset, *start,
                                                count, *ptr);

            *start = out_offset / 2;
        }
        break;

    case 4:
        if (index_offset) {
            *out_buffer = NULL;
            u_upload_alloc_ref(r300->uploader, 0, count * 4, 4,
                           &out_offset, out_buffer, ptr);

            util_rebuild_uint_elts_to_userptr(&r300->context, info,
                                              PIPE_MAP_UNSYNCHRONIZED,
                                              index_offset, *start,
                                              count, *ptr);

            *start = out_offset / 4;
        }
        break;
    }
#endif
}
