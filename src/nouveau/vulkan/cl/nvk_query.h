/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "compiler/libcl/libcl.h"

struct nvk_query_report {
   uint64_t value;
   uint64_t timestamp;
};
