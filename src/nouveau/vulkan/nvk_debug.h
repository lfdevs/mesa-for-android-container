/*
 * Copyright © 2024 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */
#ifndef NVK_DEBUG_H
#define NVK_DEBUG_H 1

enum nvk_debug {
   /* dumps all push buffers after submission */
   NVK_DEBUG_PUSH_DUMP = 1ull << 0,

   /* push buffer submissions wait on completion
    *
    * This is useful to find the submission killing the GPU context. For
    * easier debugging it also dumps the buffer leading to that.
    */
   NVK_DEBUG_PUSH_SYNC = 1ull << 1,

   /* Zero all client memory allocations
    */
   NVK_DEBUG_ZERO_MEMORY = 1ull << 2,

   /* Dump VM bind/unbinds
    */
   NVK_DEBUG_VM = 1ull << 3,

   /* Disable most cbufs
    *
    * Root descriptors still end up in a cbuf
    */
   NVK_DEBUG_NO_CBUF = 1ull << 5,
};

#endif /* NVK_DEBUG_H */
