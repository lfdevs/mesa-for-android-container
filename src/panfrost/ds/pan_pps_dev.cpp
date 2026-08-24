/*
 * Copyright © 2021 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "pan_pps_perf.h"

#include <lib/kmod/pan_kmod.h>
#include <perf/pan_perf.h>

#include <pps/pps.h>
#include <util/ralloc.h>

namespace pps {
PanfrostDevice::PanfrostDevice(int fd): fd(fd)
{
   assert(fd >= 0);
}

PanfrostDevice::~PanfrostDevice()
{
}

PanfrostDevice::PanfrostDevice(PanfrostDevice &&o): fd{o.fd}
{
   o.fd = -1;
}

PanfrostDevice &
PanfrostDevice::operator=(PanfrostDevice &&o)
{
   std::swap(fd, o.fd);
   return *this;
}

} // namespace pps
