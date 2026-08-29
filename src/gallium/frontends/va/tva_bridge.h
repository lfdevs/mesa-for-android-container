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
 * Wrap the driver's pipe_context so that video codec creation is delegated
 * to the termux-va bridge, and repoint vscreen->pscreen at the wrapped
 * screen so capability queries describe the bridge's codec set.  Must be
 * called after the real multimedia context was created (context.c) and
 * before any VA entry point runs.  No-op when the bridge is inactive.
 */
void tva_bridge_wrap_driver(struct vl_screen *vscreen, struct pipe_context **pipe);

#ifdef __cplusplus
}
#endif

#endif /* TVA_BRIDGE_H */
