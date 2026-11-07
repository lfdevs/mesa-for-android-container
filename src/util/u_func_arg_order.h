/*
 * Copyright © 2026 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#ifndef U_FUNC_ARG_ORDER_H
#define U_FUNC_ARG_ORDER_H

#include "util/macros.h"

/*
 * NIR and backend IR builder helpers often insert IR as their arguments are evaluated.
 * However, the order of argument evaluation is unspecified and can differ between compilers
 * and architectures. This header provides MESA_CALL_WITH_LTR_ARGS macro to enforce the
 * left-to-right evaluation order for the target function's arguments. Note, this doesn't
 * anything but GCC and Clang, because it requires the statement expression extension.
 *
 * Usage:
 *
 * #define ir_func(...) MESA_CALL_WITH_LTR_ARGS(_nir_func, __VA_ARGS__)
 * ir_def *_ir_func(ir_builder *b, ir_def *x, nir_def *y);
 *
 * #define ir_func(build, ...) \
 *    MESA_CALL_WITH_LTR_ARRAY_ARGS(_ir_func, ir_def *, build, __VA_ARGS__)
 * ir_def *_ir_func(ir_builder *b, ir_def *const *arr, unsigned arrsz);
 */

#if defined(__GNUC__) || defined(__clang__)
#ifdef __cplusplus
#define MESA_LTR_ARGS_AUTO auto &&
#else
#define MESA_LTR_ARGS_AUTO __auto_type
#endif

#define MESA_LTR_ARGS_SHADOW_PUSH                                                                 \
   PRAGMA_DIAGNOSTIC_PUSH                                                                         \
   PRAGMA_DIAGNOSTIC_IGNORED(-Wshadow)
#define MESA_LTR_ARGS_SHADOW_POP PRAGMA_DIAGNOSTIC_POP

#define MESA_LTR_ARGS_ARG(i, arg)                                                                 \
   MESA_LTR_ARGS_SHADOW_PUSH                                                                      \
   MESA_LTR_ARGS_AUTO _a##i = (arg);                                                              \
   MESA_LTR_ARGS_SHADOW_POP
#define MESA_LTR_ARGS_TYPED_ARG(type, i, arg)                                                     \
   MESA_LTR_ARGS_SHADOW_PUSH                                                                      \
   type _arr_arg##i = (arg);                                                                      \
   MESA_LTR_ARGS_SHADOW_POP
#define MESA_LTR_ARGS_ARRAY(type, ...)                                                            \
   MESA_LTR_ARGS_SHADOW_PUSH                                                                      \
   type _arr[] = {__VA_ARGS__};                                                                   \
   MESA_LTR_ARGS_SHADOW_POP

#define MESA_LTR_ARGS_CALL2(func, a0, a1)                                                          \
   ({                                                                                              \
      MESA_LTR_ARGS_ARG(0, a0);                                                                    \
      MESA_LTR_ARGS_ARG(1, a1);                                                                    \
      func(_a0, _a1);                                                                              \
   })
#define MESA_LTR_ARGS_CALL3(func, a0, a1, a2)                                                      \
   ({                                                                                              \
      MESA_LTR_ARGS_ARG(0, a0);                                                                    \
      MESA_LTR_ARGS_ARG(1, a1);                                                                    \
      MESA_LTR_ARGS_ARG(2, a2);                                                                    \
      func(_a0, _a1, _a2);                                                                         \
   })
#define MESA_LTR_ARGS_CALL4(func, a0, a1, a2, a3)                                                  \
   ({                                                                                              \
      MESA_LTR_ARGS_ARG(0, a0);                                                                    \
      MESA_LTR_ARGS_ARG(1, a1);                                                                    \
      MESA_LTR_ARGS_ARG(2, a2);                                                                    \
      MESA_LTR_ARGS_ARG(3, a3);                                                                    \
      func(_a0, _a1, _a2, _a3);                                                                    \
   })
#define MESA_LTR_ARGS_CALL5(func, a0, a1, a2, a3, a4)                                              \
   ({                                                                                              \
      MESA_LTR_ARGS_ARG(0, a0);                                                                    \
      MESA_LTR_ARGS_ARG(1, a1);                                                                    \
      MESA_LTR_ARGS_ARG(2, a2);                                                                    \
      MESA_LTR_ARGS_ARG(3, a3);                                                                    \
      MESA_LTR_ARGS_ARG(4, a4);                                                                    \
      func(_a0, _a1, _a2, _a3, _a4);                                                               \
   })
#define MESA_LTR_ARGS_CALL6(func, a0, a1, a2, a3, a4, a5)                                          \
   ({                                                                                              \
      MESA_LTR_ARGS_ARG(0, a0);                                                                    \
      MESA_LTR_ARGS_ARG(1, a1);                                                                    \
      MESA_LTR_ARGS_ARG(2, a2);                                                                    \
      MESA_LTR_ARGS_ARG(3, a3);                                                                    \
      MESA_LTR_ARGS_ARG(4, a4);                                                                    \
      MESA_LTR_ARGS_ARG(5, a5);                                                                    \
      func(_a0, _a1, _a2, _a3, _a4, _a5);                                                          \
   })
#define MESA_LTR_ARGS_CALL7(func, a0, a1, a2, a3, a4, a5, a6)                                      \
   ({                                                                                              \
      MESA_LTR_ARGS_ARG(0, a0);                                                                    \
      MESA_LTR_ARGS_ARG(1, a1);                                                                    \
      MESA_LTR_ARGS_ARG(2, a2);                                                                    \
      MESA_LTR_ARGS_ARG(3, a3);                                                                    \
      MESA_LTR_ARGS_ARG(4, a4);                                                                    \
      MESA_LTR_ARGS_ARG(5, a5);                                                                    \
      MESA_LTR_ARGS_ARG(6, a6);                                                                    \
      func(_a0, _a1, _a2, _a3, _a4, _a5, _a6);                                                     \
   })
#define MESA_LTR_ARGS_CALL8(func, a0, a1, a2, a3, a4, a5, a6, a7)                                  \
   ({                                                                                              \
      MESA_LTR_ARGS_ARG(0, a0);                                                                    \
      MESA_LTR_ARGS_ARG(1, a1);                                                                    \
      MESA_LTR_ARGS_ARG(2, a2);                                                                    \
      MESA_LTR_ARGS_ARG(3, a3);                                                                    \
      MESA_LTR_ARGS_ARG(4, a4);                                                                    \
      MESA_LTR_ARGS_ARG(5, a5);                                                                    \
      MESA_LTR_ARGS_ARG(6, a6);                                                                    \
      MESA_LTR_ARGS_ARG(7, a7);                                                                    \
      func(_a0, _a1, _a2, _a3, _a4, _a5, _a6, _a7);                                                \
   })
#define MESA_LTR_ARGS_CALL9(func, a0, a1, a2, a3, a4, a5, a6, a7, a8)                              \
   ({                                                                                              \
      MESA_LTR_ARGS_ARG(0, a0);                                                                    \
      MESA_LTR_ARGS_ARG(1, a1);                                                                    \
      MESA_LTR_ARGS_ARG(2, a2);                                                                    \
      MESA_LTR_ARGS_ARG(3, a3);                                                                    \
      MESA_LTR_ARGS_ARG(4, a4);                                                                    \
      MESA_LTR_ARGS_ARG(5, a5);                                                                    \
      MESA_LTR_ARGS_ARG(6, a6);                                                                    \
      MESA_LTR_ARGS_ARG(7, a7);                                                                    \
      MESA_LTR_ARGS_ARG(8, a8);                                                                    \
      func(_a0, _a1, _a2, _a3, _a4, _a5, _a6, _a7, _a8);                                           \
   })
#define MESA_LTR_ARGS_CALL10(func, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9)                         \
   ({                                                                                              \
      MESA_LTR_ARGS_ARG(0, a0);                                                                    \
      MESA_LTR_ARGS_ARG(1, a1);                                                                    \
      MESA_LTR_ARGS_ARG(2, a2);                                                                    \
      MESA_LTR_ARGS_ARG(3, a3);                                                                    \
      MESA_LTR_ARGS_ARG(4, a4);                                                                    \
      MESA_LTR_ARGS_ARG(5, a5);                                                                    \
      MESA_LTR_ARGS_ARG(6, a6);                                                                    \
      MESA_LTR_ARGS_ARG(7, a7);                                                                    \
      MESA_LTR_ARGS_ARG(8, a8);                                                                    \
      MESA_LTR_ARGS_ARG(9, a9);                                                                    \
      func(_a0, _a1, _a2, _a3, _a4, _a5, _a6, _a7, _a8, _a9);                                      \
   })
#define MESA_LTR_ARGS_CALL11(func, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10)                    \
   ({                                                                                              \
      MESA_LTR_ARGS_ARG(0, a0);                                                                    \
      MESA_LTR_ARGS_ARG(1, a1);                                                                    \
      MESA_LTR_ARGS_ARG(2, a2);                                                                    \
      MESA_LTR_ARGS_ARG(3, a3);                                                                    \
      MESA_LTR_ARGS_ARG(4, a4);                                                                    \
      MESA_LTR_ARGS_ARG(5, a5);                                                                    \
      MESA_LTR_ARGS_ARG(6, a6);                                                                    \
      MESA_LTR_ARGS_ARG(7, a7);                                                                    \
      MESA_LTR_ARGS_ARG(8, a8);                                                                    \
      MESA_LTR_ARGS_ARG(9, a9);                                                                    \
      MESA_LTR_ARGS_ARG(10, a10);                                                                  \
      func(_a0, _a1, _a2, _a3, _a4, _a5, _a6, _a7, _a8, _a9, _a10);                                \
   })
#define MESA_LTR_ARGS_CALL12(func, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11)               \
   ({                                                                                              \
      MESA_LTR_ARGS_ARG(0, a0);                                                                    \
      MESA_LTR_ARGS_ARG(1, a1);                                                                    \
      MESA_LTR_ARGS_ARG(2, a2);                                                                    \
      MESA_LTR_ARGS_ARG(3, a3);                                                                    \
      MESA_LTR_ARGS_ARG(4, a4);                                                                    \
      MESA_LTR_ARGS_ARG(5, a5);                                                                    \
      MESA_LTR_ARGS_ARG(6, a6);                                                                    \
      MESA_LTR_ARGS_ARG(7, a7);                                                                    \
      MESA_LTR_ARGS_ARG(8, a8);                                                                    \
      MESA_LTR_ARGS_ARG(9, a9);                                                                    \
      MESA_LTR_ARGS_ARG(10, a10);                                                                  \
      MESA_LTR_ARGS_ARG(11, a11);                                                                  \
      func(_a0, _a1, _a2, _a3, _a4, _a5, _a6, _a7, _a8, _a9, _a10, _a11);                          \
   })
#define MESA_LTR_ARGS_CALL13(func, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12)          \
   ({                                                                                              \
      MESA_LTR_ARGS_ARG(0, a0);                                                                    \
      MESA_LTR_ARGS_ARG(1, a1);                                                                    \
      MESA_LTR_ARGS_ARG(2, a2);                                                                    \
      MESA_LTR_ARGS_ARG(3, a3);                                                                    \
      MESA_LTR_ARGS_ARG(4, a4);                                                                    \
      MESA_LTR_ARGS_ARG(5, a5);                                                                    \
      MESA_LTR_ARGS_ARG(6, a6);                                                                    \
      MESA_LTR_ARGS_ARG(7, a7);                                                                    \
      MESA_LTR_ARGS_ARG(8, a8);                                                                    \
      MESA_LTR_ARGS_ARG(9, a9);                                                                    \
      MESA_LTR_ARGS_ARG(10, a10);                                                                  \
      MESA_LTR_ARGS_ARG(11, a11);                                                                  \
      MESA_LTR_ARGS_ARG(12, a12);                                                                  \
      func(_a0, _a1, _a2, _a3, _a4, _a5, _a6, _a7, _a8, _a9, _a10, _a11, _a12);                    \
   })
#define MESA_LTR_ARGS_CALL14(func, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13)     \
   ({                                                                                              \
      MESA_LTR_ARGS_ARG(0, a0);                                                                    \
      MESA_LTR_ARGS_ARG(1, a1);                                                                    \
      MESA_LTR_ARGS_ARG(2, a2);                                                                    \
      MESA_LTR_ARGS_ARG(3, a3);                                                                    \
      MESA_LTR_ARGS_ARG(4, a4);                                                                    \
      MESA_LTR_ARGS_ARG(5, a5);                                                                    \
      MESA_LTR_ARGS_ARG(6, a6);                                                                    \
      MESA_LTR_ARGS_ARG(7, a7);                                                                    \
      MESA_LTR_ARGS_ARG(8, a8);                                                                    \
      MESA_LTR_ARGS_ARG(9, a9);                                                                    \
      MESA_LTR_ARGS_ARG(10, a10);                                                                  \
      MESA_LTR_ARGS_ARG(11, a11);                                                                  \
      MESA_LTR_ARGS_ARG(12, a12);                                                                  \
      MESA_LTR_ARGS_ARG(13, a13);                                                                  \
      func(_a0, _a1, _a2, _a3, _a4, _a5, _a6, _a7, _a8, _a9, _a10, _a11, _a12, _a13);              \
   })
#define MESA_LTR_ARGS_CALL15(func, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,     \
                             a14)                                                                  \
   ({                                                                                              \
      MESA_LTR_ARGS_ARG(0, a0);                                                                    \
      MESA_LTR_ARGS_ARG(1, a1);                                                                    \
      MESA_LTR_ARGS_ARG(2, a2);                                                                    \
      MESA_LTR_ARGS_ARG(3, a3);                                                                    \
      MESA_LTR_ARGS_ARG(4, a4);                                                                    \
      MESA_LTR_ARGS_ARG(5, a5);                                                                    \
      MESA_LTR_ARGS_ARG(6, a6);                                                                    \
      MESA_LTR_ARGS_ARG(7, a7);                                                                    \
      MESA_LTR_ARGS_ARG(8, a8);                                                                    \
      MESA_LTR_ARGS_ARG(9, a9);                                                                    \
      MESA_LTR_ARGS_ARG(10, a10);                                                                  \
      MESA_LTR_ARGS_ARG(11, a11);                                                                  \
      MESA_LTR_ARGS_ARG(12, a12);                                                                  \
      MESA_LTR_ARGS_ARG(13, a13);                                                                  \
      MESA_LTR_ARGS_ARG(14, a14);                                                                  \
      func(_a0, _a1, _a2, _a3, _a4, _a5, _a6, _a7, _a8, _a9, _a10, _a11, _a12, _a13, _a14);        \
   })
#define MESA_LTR_ARGS_CALL16(func, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,     \
                             a14, a15)                                                             \
   ({                                                                                              \
      MESA_LTR_ARGS_ARG(0, a0);                                                                    \
      MESA_LTR_ARGS_ARG(1, a1);                                                                    \
      MESA_LTR_ARGS_ARG(2, a2);                                                                    \
      MESA_LTR_ARGS_ARG(3, a3);                                                                    \
      MESA_LTR_ARGS_ARG(4, a4);                                                                    \
      MESA_LTR_ARGS_ARG(5, a5);                                                                    \
      MESA_LTR_ARGS_ARG(6, a6);                                                                    \
      MESA_LTR_ARGS_ARG(7, a7);                                                                    \
      MESA_LTR_ARGS_ARG(8, a8);                                                                    \
      MESA_LTR_ARGS_ARG(9, a9);                                                                    \
      MESA_LTR_ARGS_ARG(10, a10);                                                                  \
      MESA_LTR_ARGS_ARG(11, a11);                                                                  \
      MESA_LTR_ARGS_ARG(12, a12);                                                                  \
      MESA_LTR_ARGS_ARG(13, a13);                                                                  \
      MESA_LTR_ARGS_ARG(14, a14);                                                                  \
      MESA_LTR_ARGS_ARG(15, a15);                                                                  \
      func(_a0, _a1, _a2, _a3, _a4, _a5, _a6, _a7, _a8, _a9, _a10, _a11, _a12, _a13, _a14, _a15);  \
   })
#define MESA_LTR_ARGS_CALL17(func, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,     \
                             a14, a15, a16)                                                        \
   ({                                                                                              \
      MESA_LTR_ARGS_ARG(0, a0);                                                                    \
      MESA_LTR_ARGS_ARG(1, a1);                                                                    \
      MESA_LTR_ARGS_ARG(2, a2);                                                                    \
      MESA_LTR_ARGS_ARG(3, a3);                                                                    \
      MESA_LTR_ARGS_ARG(4, a4);                                                                    \
      MESA_LTR_ARGS_ARG(5, a5);                                                                    \
      MESA_LTR_ARGS_ARG(6, a6);                                                                    \
      MESA_LTR_ARGS_ARG(7, a7);                                                                    \
      MESA_LTR_ARGS_ARG(8, a8);                                                                    \
      MESA_LTR_ARGS_ARG(9, a9);                                                                    \
      MESA_LTR_ARGS_ARG(10, a10);                                                                  \
      MESA_LTR_ARGS_ARG(11, a11);                                                                  \
      MESA_LTR_ARGS_ARG(12, a12);                                                                  \
      MESA_LTR_ARGS_ARG(13, a13);                                                                  \
      MESA_LTR_ARGS_ARG(14, a14);                                                                  \
      MESA_LTR_ARGS_ARG(15, a15);                                                                  \
      MESA_LTR_ARGS_ARG(16, a16);                                                                  \
      func(_a0, _a1, _a2, _a3, _a4, _a5, _a6, _a7, _a8, _a9, _a10, _a11, _a12, _a13, _a14, _a15, _a16); \
   })

#define MESA_LTR_ARGS_ARRAY_CALL1(func, type, fixed, a0)                                           \
   ({                                                                                              \
      MESA_LTR_ARGS_ARG(0, fixed);                                                                 \
      MESA_LTR_ARGS_TYPED_ARG(type, 0, a0);                                                        \
      MESA_LTR_ARGS_ARRAY(type, _arr_arg0);                                                        \
      func(_a0, _arr, 1);                                                                          \
   })
#define MESA_LTR_ARGS_ARRAY_CALL2(func, type, fixed, a0, a1)                                       \
   ({                                                                                              \
      MESA_LTR_ARGS_ARG(0, fixed);                                                                 \
      MESA_LTR_ARGS_TYPED_ARG(type, 0, a0);                                                        \
      MESA_LTR_ARGS_TYPED_ARG(type, 1, a1);                                                        \
      MESA_LTR_ARGS_ARRAY(type, _arr_arg0, _arr_arg1);                                             \
      func(_a0, _arr, 2);                                                                          \
   })
#define MESA_LTR_ARGS_ARRAY_CALL3(func, type, fixed, a0, a1, a2)                                   \
   ({                                                                                              \
      MESA_LTR_ARGS_ARG(0, fixed);                                                                 \
      MESA_LTR_ARGS_TYPED_ARG(type, 0, a0);                                                        \
      MESA_LTR_ARGS_TYPED_ARG(type, 1, a1);                                                        \
      MESA_LTR_ARGS_TYPED_ARG(type, 2, a2);                                                        \
      MESA_LTR_ARGS_ARRAY(type, _arr_arg0, _arr_arg1, _arr_arg2);                                  \
      func(_a0, _arr, 3);                                                                          \
   })
#define MESA_LTR_ARGS_ARRAY_CALL4(func, type, fixed, a0, a1, a2, a3)                               \
   ({                                                                                              \
      MESA_LTR_ARGS_ARG(0, fixed);                                                                 \
      MESA_LTR_ARGS_TYPED_ARG(type, 0, a0);                                                        \
      MESA_LTR_ARGS_TYPED_ARG(type, 1, a1);                                                        \
      MESA_LTR_ARGS_TYPED_ARG(type, 2, a2);                                                        \
      MESA_LTR_ARGS_TYPED_ARG(type, 3, a3);                                                        \
      MESA_LTR_ARGS_ARRAY(type, _arr_arg0, _arr_arg1, _arr_arg2, _arr_arg3);                       \
      func(_a0, _arr, 4);                                                                          \
   })
#define MESA_LTR_ARGS_ARRAY_CALL5(func, type, fixed, a0, a1, a2, a3, a4)                           \
   ({                                                                                              \
      MESA_LTR_ARGS_ARG(0, fixed);                                                                 \
      MESA_LTR_ARGS_TYPED_ARG(type, 0, a0);                                                        \
      MESA_LTR_ARGS_TYPED_ARG(type, 1, a1);                                                        \
      MESA_LTR_ARGS_TYPED_ARG(type, 2, a2);                                                        \
      MESA_LTR_ARGS_TYPED_ARG(type, 3, a3);                                                        \
      MESA_LTR_ARGS_TYPED_ARG(type, 4, a4);                                                        \
      MESA_LTR_ARGS_ARRAY(type, _arr_arg0, _arr_arg1, _arr_arg2, _arr_arg3, _arr_arg4);            \
      func(_a0, _arr, 5);                                                                          \
   })
#define MESA_LTR_ARGS_ARRAY_CALL6(func, type, fixed, a0, a1, a2, a3, a4, a5)                       \
   ({                                                                                              \
      MESA_LTR_ARGS_ARG(0, fixed);                                                                 \
      MESA_LTR_ARGS_TYPED_ARG(type, 0, a0);                                                        \
      MESA_LTR_ARGS_TYPED_ARG(type, 1, a1);                                                        \
      MESA_LTR_ARGS_TYPED_ARG(type, 2, a2);                                                        \
      MESA_LTR_ARGS_TYPED_ARG(type, 3, a3);                                                        \
      MESA_LTR_ARGS_TYPED_ARG(type, 4, a4);                                                        \
      MESA_LTR_ARGS_TYPED_ARG(type, 5, a5);                                                        \
      MESA_LTR_ARGS_ARRAY(type, _arr_arg0, _arr_arg1, _arr_arg2, _arr_arg3, _arr_arg4, _arr_arg5); \
      func(_a0, _arr, 6);                                                                          \
   })
#define MESA_LTR_ARGS_ARRAY_CALL7(func, type, fixed, a0, a1, a2, a3, a4, a5, a6)                   \
   ({                                                                                              \
      MESA_LTR_ARGS_ARG(0, fixed);                                                                 \
      MESA_LTR_ARGS_TYPED_ARG(type, 0, a0);                                                        \
      MESA_LTR_ARGS_TYPED_ARG(type, 1, a1);                                                        \
      MESA_LTR_ARGS_TYPED_ARG(type, 2, a2);                                                        \
      MESA_LTR_ARGS_TYPED_ARG(type, 3, a3);                                                        \
      MESA_LTR_ARGS_TYPED_ARG(type, 4, a4);                                                        \
      MESA_LTR_ARGS_TYPED_ARG(type, 5, a5);                                                        \
      MESA_LTR_ARGS_TYPED_ARG(type, 6, a6);                                                        \
      MESA_LTR_ARGS_ARRAY(type, _arr_arg0, _arr_arg1, _arr_arg2, _arr_arg3, _arr_arg4, _arr_arg5,  \
                          _arr_arg6);                                                             \
      func(_a0, _arr, 7);                                                                          \
   })
#define MESA_LTR_ARGS_ARRAY_CALL8(func, type, fixed, a0, a1, a2, a3, a4, a5, a6, a7)               \
   ({                                                                                              \
      MESA_LTR_ARGS_ARG(0, fixed);                                                                 \
      MESA_LTR_ARGS_TYPED_ARG(type, 0, a0);                                                        \
      MESA_LTR_ARGS_TYPED_ARG(type, 1, a1);                                                        \
      MESA_LTR_ARGS_TYPED_ARG(type, 2, a2);                                                        \
      MESA_LTR_ARGS_TYPED_ARG(type, 3, a3);                                                        \
      MESA_LTR_ARGS_TYPED_ARG(type, 4, a4);                                                        \
      MESA_LTR_ARGS_TYPED_ARG(type, 5, a5);                                                        \
      MESA_LTR_ARGS_TYPED_ARG(type, 6, a6);                                                        \
      MESA_LTR_ARGS_TYPED_ARG(type, 7, a7);                                                        \
      MESA_LTR_ARGS_ARRAY(type, _arr_arg0, _arr_arg1, _arr_arg2, _arr_arg3, _arr_arg4, _arr_arg5,  \
                          _arr_arg6, _arr_arg7);                                                   \
      func(_a0, _arr, 8);                                                                          \
   })
#else
#define MESA_LTR_ARGS_CALL2(func, a0, a1)                 func((a0), (a1))
#define MESA_LTR_ARGS_CALL3(func, a0, a1, a2)             func((a0), (a1), (a2))
#define MESA_LTR_ARGS_CALL4(func, a0, a1, a2, a3)         func((a0), (a1), (a2), (a3))
#define MESA_LTR_ARGS_CALL5(func, a0, a1, a2, a3, a4)     func((a0), (a1), (a2), (a3), (a4))
#define MESA_LTR_ARGS_CALL6(func, a0, a1, a2, a3, a4, a5) func((a0), (a1), (a2), (a3), (a4), (a5))
#define MESA_LTR_ARGS_CALL7(func, a0, a1, a2, a3, a4, a5, a6)                                      \
   func((a0), (a1), (a2), (a3), (a4), (a5), (a6))
#define MESA_LTR_ARGS_CALL8(func, a0, a1, a2, a3, a4, a5, a6, a7)                                  \
   func((a0), (a1), (a2), (a3), (a4), (a5), (a6), (a7))
#define MESA_LTR_ARGS_CALL9(func, a0, a1, a2, a3, a4, a5, a6, a7, a8)                              \
   func((a0), (a1), (a2), (a3), (a4), (a5), (a6), (a7), (a8))
#define MESA_LTR_ARGS_CALL10(func, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9)                         \
   func((a0), (a1), (a2), (a3), (a4), (a5), (a6), (a7), (a8), (a9))
#define MESA_LTR_ARGS_CALL11(func, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10)                    \
   func((a0), (a1), (a2), (a3), (a4), (a5), (a6), (a7), (a8), (a9), (a10))
#define MESA_LTR_ARGS_CALL12(func, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11)               \
   func((a0), (a1), (a2), (a3), (a4), (a5), (a6), (a7), (a8), (a9), (a10), (a11))
#define MESA_LTR_ARGS_CALL13(func, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12)          \
   func((a0), (a1), (a2), (a3), (a4), (a5), (a6), (a7), (a8), (a9), (a10), (a11), (a12))
#define MESA_LTR_ARGS_CALL14(func, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13)     \
   func((a0), (a1), (a2), (a3), (a4), (a5), (a6), (a7), (a8), (a9), (a10), (a11), (a12), (a13))
#define MESA_LTR_ARGS_CALL15(func, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,     \
                             a14)                                                                  \
   func((a0), (a1), (a2), (a3), (a4), (a5), (a6), (a7), (a8), (a9), (a10), (a11), (a12), (a13),    \
        (a14))
#define MESA_LTR_ARGS_CALL16(func, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,     \
                             a14, a15)                                                             \
   func((a0), (a1), (a2), (a3), (a4), (a5), (a6), (a7), (a8), (a9), (a10), (a11), (a12), (a13),    \
        (a14), (a15))
#define MESA_LTR_ARGS_CALL17(func, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,     \
                             a14, a15, a16)                                                        \
   func((a0), (a1), (a2), (a3), (a4), (a5), (a6), (a7), (a8), (a9), (a10), (a11), (a12), (a13),    \
        (a14), (a15), (a16))

#define MESA_LTR_ARGS_ARRAY_CALL1(func, type, fixed, a0)                                           \
   func((fixed), (type[]){(a0)}, 1)
#define MESA_LTR_ARGS_ARRAY_CALL2(func, type, fixed, a0, a1)                                       \
   func((fixed), (type[]){(a0), (a1)}, 2)
#define MESA_LTR_ARGS_ARRAY_CALL3(func, type, fixed, a0, a1, a2)                                   \
   func((fixed), (type[]){(a0), (a1), (a2)}, 3)
#define MESA_LTR_ARGS_ARRAY_CALL4(func, type, fixed, a0, a1, a2, a3)                               \
   func((fixed), (type[]){(a0), (a1), (a2), (a3)}, 4)
#define MESA_LTR_ARGS_ARRAY_CALL5(func, type, fixed, a0, a1, a2, a3, a4)                           \
   func((fixed), (type[]){(a0), (a1), (a2), (a3), (a4)}, 5)
#define MESA_LTR_ARGS_ARRAY_CALL6(func, type, fixed, a0, a1, a2, a3, a4, a5)                       \
   func((fixed), (type[]){(a0), (a1), (a2), (a3), (a4), (a5)}, 6)
#define MESA_LTR_ARGS_ARRAY_CALL7(func, type, fixed, a0, a1, a2, a3, a4, a5, a6)                   \
   func((fixed), (type[]){(a0), (a1), (a2), (a3), (a4), (a5), (a6)}, 7)
#define MESA_LTR_ARGS_ARRAY_CALL8(func, type, fixed, a0, a1, a2, a3, a4, a5, a6, a7)               \
   func((fixed), (type[]){(a0), (a1), (a2), (a3), (a4), (a5), (a6), (a7)}, 8)
#endif

#define MESA_LTR_ARGS_EXPAND(x)  x
#define MESA_LTR_ARGS_CAT2(a, b) a##b
#define MESA_LTR_ARGS_CAT(a, b)  MESA_LTR_ARGS_CAT2(a, b)
#define MESA_LTR_ARGS_ARG_N(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, \
                            _17, N, ...)                                                           \
   N
#define MESA_LTR_ARGS_NARG_(...) MESA_LTR_ARGS_ARG_N(__VA_ARGS__)
#define MESA_LTR_ARGS_NARG(...)                                                                    \
   MESA_LTR_ARGS_NARG_(__VA_ARGS__, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1)
#define MESA_CALL_WITH_LTR_ARGS(func, ...)                                                         \
   MESA_LTR_ARGS_EXPAND(                                                                           \
      MESA_LTR_ARGS_CAT(MESA_LTR_ARGS_CALL, MESA_LTR_ARGS_NARG(__VA_ARGS__))(func, __VA_ARGS__))
#define MESA_CALL_WITH_LTR_ARRAY_ARGS(func, type, fixed, ...)                                      \
   MESA_LTR_ARGS_EXPAND(MESA_LTR_ARGS_CAT(MESA_LTR_ARGS_ARRAY_CALL,                                \
                                          MESA_LTR_ARGS_NARG(__VA_ARGS__))(func, type, fixed,       \
                                                                           __VA_ARGS__))

#endif /* U_FUNC_ARG_ORDER_H */
