/*
 * Copyright 2009 Marek Olšák <maraeo@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef R300_SHADER_SEMANTICS_H
#define R300_SHADER_SEMANTICS_H

#define ATTR_UNUSED             (-1)
#define ATTR_COLOR_COUNT        2
#define ATTR_GENERIC_COUNT      32

/* Information about what attributes are written by VS or read by FS.
 * The variables contain output/input register indices. */
struct r300_shader_semantics {
    int pos;
    int psize;
    int color[ATTR_COLOR_COUNT];
    int bcolor[ATTR_COLOR_COUNT];
    int face;
    int generic[ATTR_GENERIC_COUNT];
    int fog;
    int wpos;

    int num_generic;

    /* Total number of used inputs/outputs. */
    unsigned num_total;
};

static inline void r300_shader_semantics_reset(
    struct r300_shader_semantics* info)
{
    int i;

    info->pos = ATTR_UNUSED;
    info->psize = ATTR_UNUSED;
    info->face = ATTR_UNUSED;
    info->fog = ATTR_UNUSED;
    info->wpos = ATTR_UNUSED;

    for (i = 0; i < ATTR_COLOR_COUNT; i++) {
        info->color[i] = ATTR_UNUSED;
        info->bcolor[i] = ATTR_UNUSED;
    }

    for (i = 0; i < ATTR_GENERIC_COUNT; i++) {
        info->generic[i] = ATTR_UNUSED;
    }

    info->num_generic = 0;
    info->num_total = 0;
}

#endif
