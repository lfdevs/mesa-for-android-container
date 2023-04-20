/*
 * Copyright 2010 Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#ifndef AGX_PUBLIC_H
#define AGX_PUBLIC_H

#ifdef __cplusplus
extern "C" {
#endif

struct pipe_screen;
struct sw_winsys;
struct renderonly;

struct pipe_screen *agx_screen_create(int fd, struct renderonly *ro,
                                      struct sw_winsys *winsys);

#ifdef __cplusplus
}
#endif

#endif
