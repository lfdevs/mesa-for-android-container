/*
 * Copyright © 2026 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

/*
 * Bindgen input header: exposes C APIs to Rust.
 */

#include "kmod/pan_kmod.h"
#include "hw_runner.h"
#include "pan_props.h"
#include "genxml/decode.h"
#include <xf86drm.h>
#include "drm-uapi/panthor_drm.h"
#include <sys/mman.h>
