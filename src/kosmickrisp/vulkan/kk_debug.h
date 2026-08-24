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
   /* Forces robustness for all shaders */
   KK_DEBUG_FORCE_ROBUSTNESS = 1ull << 2,
};

extern enum kk_debug kk_mesa_debug_flags;

#define KK_DEBUG(flag) unlikely(kk_mesa_debug_flags &KK_DEBUG_##flag)

enum kk_experimental {
   /* Advertise custom border features */
   KK_EXPERIMENTAL_CUSTOM_BORDER = 1ull << 0,
   /* Advertise image view min LOD feature */
   KK_EXPERIMENTAL_IMAGE_VIEW_MIN_LOD = 1ull << 1,
   /* Using allowGPUOptimizedContents on M5 for this format causes
    * vkCmdCopyImage() tests in CTS to fail, but it is otherwise usable */
   KK_EXPERIMENTAL_M5_OPTIMIZE_E5B9G9R9 = 1ull << 2,
};

extern enum kk_experimental kk_mesa_experimental_flags;

#define KK_EXPERIMENTAL(flag)                                                  \
   unlikely(kk_mesa_experimental_flags &KK_EXPERIMENTAL_##flag)

extern void kk_process_debug_variable(void);

#endif /* KK_DEBUG_H */
