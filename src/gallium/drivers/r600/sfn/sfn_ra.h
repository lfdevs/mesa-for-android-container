/* -*- mesa-c++  -*-
 * Copyright 2022 Collabora LTD
 * Author: Gert Wollny <gert.wollny@collabora.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef INTERFERENCE_H
#define INTERFERENCE_H

#include "sfn_valuefactory.h"

namespace r600 {

bool
register_allocation(LiveRangeMap& lrm);

} // namespace r600

#endif // INTERFERENCE_H
