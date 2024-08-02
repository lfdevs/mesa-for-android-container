/*
 * Copyright 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "dev/intel_device_info.h"

void intel_common_update_device_info(int fd, struct intel_device_info *devinfo);
