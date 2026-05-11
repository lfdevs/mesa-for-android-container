/* Copyright © 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*intel_imgui_draw_cb)(void);

void intel_imgui_ui(const char *window_name, intel_imgui_draw_cb callback);

#ifdef __cplusplus
}
#endif
