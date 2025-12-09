/*
 * Copyright 2014 Broadcom
 * Copyright 2018 Alyssa Rosenzweig
 * Copyright 2025 Tomeu Vizoso
 * SPDX-License-Identifier: MIT
 */

#ifndef __RKT_DRM_PUBLIC_H__
#define __RKT_DRM_PUBLIC_H__

struct pipe_screen;
struct pipe_screen_config;

struct pipe_screen *
rkt_drm_screen_create(int drmFD, const struct pipe_screen_config *config);

#endif /* __RKT_DRM_PUBLIC_H__ */
