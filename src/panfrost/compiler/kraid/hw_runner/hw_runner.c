/*
 * Copyright © 2026 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "hw_runner_priv.h"
#include "pan_model.h"
#include "kmod/pan_kmod.h"
#include "util/macros.h"

void
hw_runner_new_cmd_stream(struct pan_kmod_dev *kdev,
                         struct hw_runner_invocation_info *info,
                         struct hw_runner_layout_info *out)
{
   unsigned arch = pan_arch(kdev->props.gpu_id);
   switch (arch) {
   case 10: hw_runner_new_cmd_stream_v10(kdev, info, out); break;
   case 12: hw_runner_new_cmd_stream_v12(kdev, info, out); break;
   case 13: hw_runner_new_cmd_stream_v13(kdev, info, out); break;
   default: UNREACHABLE("Unsupported architecture");
   }
}
