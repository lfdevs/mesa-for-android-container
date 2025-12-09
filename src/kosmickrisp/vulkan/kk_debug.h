/*
 * Copyright 2025 LunarG, Inc.
 * SPDX-License-Identifier: MIT
 */
#ifndef KK_DEBUG_H
#define KK_DEBUG_H 1

enum kk_debug {
   /* Print out the NIR from the compiler */
   KK_DEBUG_NIR = 1ull << 0,
   /* Print out the generated MSL source code from the compiler */
   KK_DEBUG_MSL = 1ull << 1,
};

extern enum kk_debug kk_mesa_debug_flags;

#define KK_DEBUG(flag) unlikely(kk_mesa_debug_flags &KK_DEBUG_##flag)

extern void kk_process_debug_variable(void);

#endif /* KK_DEBUG_H */
