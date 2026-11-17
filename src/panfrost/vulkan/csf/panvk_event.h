/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_EVENT_H
#define PANVK_EVENT_H

#ifndef PAN_ARCH
#error "PAN_ARCH must be defined"
#endif

#include "vk_object.h"

#include "panvk_mempool.h"

struct panvk_event {
   struct vk_object_base base;

   /* v10 is lacking IAND/IOR instructions, which forces us to have one syncobj
    * per-subqueue instead of one syncobj on which subqueues would only
    * set/clear their bit. On kbase this is its own CSF event memory BO: the
    * rw_nc pool is ordinary memory, and kbase never re-checks a SYNC_WAIT
    * whose word is not event memory once the waiting CSG has been evicted.
    * Elsewhere it is a suballocation of the rw_nc pool. */
   struct panvk_priv_mem syncobjs;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(panvk_event, base, VkEvent, VK_OBJECT_TYPE_EVENT)

#endif
