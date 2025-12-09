/*
 * Copyright 2014 Broadcom
 * Copyright 2018 Alyssa Rosenzweig
 * Copyright 2025 Tomeu Vizoso
 * SPDX-License-Identifier: MIT
 */

#include "util/os_file.h"
#include "util/u_screen.h"

#include "rocket/rkt_device.h"
#include "rkt_drm_public.h"

struct pipe_screen *
rkt_drm_screen_create(int fd, const struct pipe_screen_config *config)
{
   return u_pipe_screen_lookup_or_create(os_dupfd_cloexec(fd), config, NULL,
                                         rkt_screen_create);
}
