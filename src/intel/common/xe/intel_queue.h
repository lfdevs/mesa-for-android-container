/*
 * Copyright 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

int xe_queue_get_syncobj_for_idle(int fd, uint32_t exec_queue_id, uint32_t *syncobj);
