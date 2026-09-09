/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * tva_bridge.h - glue between the Mesa VA frontend and the termux-va
 *                daemon (Termux MediaCodec hardware decode).
 *
 * Copyright (C) 2026 lfdevs
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * This is new code written for the termux-va project (GPL-3.0); the wire
 * protocol client it drives is a port of droidspaces-media-decode's
 * vaapi-driver/src/dmd_client.c (Apache-2.0, see tva_client.c).
 */
#ifndef TVA_BRIDGE_H
#define TVA_BRIDGE_H

#include <stdbool.h>

struct vl_screen;
struct pipe_context;
struct pipe_screen;

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Whether the bridge should take over VA decoding in this process.
 *
 * Activation order:
 *   - TERMUX_VA_BRIDGE=0            -> never
 *   - TERMUX_VA_BRIDGE=1 (or true)  -> always
 *   - otherwise: active when TERMUX_VA_SOCKET / TERMUX_VA_SOCKET_DIR is set,
 *     or when the default endpoint exists as a socket
 *     (/tmp/termux-va/termux-va.sock)
 */
bool tva_bridge_active(void);

/*
 * Fill the underlying screen's NULL video capability hooks
 * (get_video_param / is_video_format_supported) with the bridge's codec
 * table.  Drivers without a video path (freedreno, llvmpipe) leave them
 * NULL, which would fail the VA frontend's init check.  Hooks that are
 * already present are left untouched.
 */
void tva_bridge_screen_set_video_hooks(struct pipe_screen *screen);

/*
 * Fill the multimedia context's create_video_codec /
 * create_video_buffer(_with_modifiers) hooks with the bridge
 * implementations.  `pipe` must be the REAL context created from the raw
 * screen (the implementations receive it as-is; no wrapper object is
 * involved).
 */
void tva_bridge_pipe_set_codec_hooks(struct pipe_context *pipe);

/*
 * Create the underlying screen for the VA frontend's vscreen, with backend
 * selection (see TERMUX_VA_GPU_BACKEND in docs/envvars.rst):
 *
 *   auto (default) stock loader selection, then llvmpipe
 *   kgsl           force the "kgsl" freedreno alias (GPU submission via
 *                  /dev/kgsl-3d0, handed fd as control/identity fd)
 *   drm            stock loader selection only
 *   sw             llvmpipe only
 *
 * Does not take ownership of `fd` (pipe_loader dups it internally).
 * Returns NULL when every selected backend failed.
 */
struct vl_screen *tva_bridge_vscreen_create(int fd, bool honor_dri_prime);

#ifdef __cplusplus
}
#endif

#endif /* TVA_BRIDGE_H */
