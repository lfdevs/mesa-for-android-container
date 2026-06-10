/*
 * Copyright ©2021 Collabora Ltd.
 * Copyright © 2015 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#ifndef GEN_MACROS_H
#define GEN_MACROS_H

/* Macros for handling per-gen compilation.
 *
 * The macro GENX() automatically suffixes whatever you give it with _vX
 *
 * You can do pseudo-runtime checks in your function such as
 *
 * if (PAN_ARCH == 4) {
 *    // Do something
 * }
 *
 * The contents of the if statement must be valid regardless of gen, but
 * the if will get compiled away on everything except first-generation Midgard.
 *
 * For places where you really do have a compile-time conflict, you can
 * use preprocessor logic:
 *
 * #if (PAN_ARCH == 75)
 *    // Do something
 * #endif
 *
 * However, it is strongly recommended that the former be used whenever
 * possible.
 */

/* Base macro defined on the command line. */
#ifndef PAN_ARCH
#include "genxml/common_pack.h"
#else

/* Suffixing macros */
#if (PAN_ARCH == 4)
#define GENX(X) X##_v4
#include "genxml/v4_pack.h"
#elif (PAN_ARCH == 5)
#define GENX(X) X##_v5
#include "genxml/v5_pack.h"
#elif (PAN_ARCH == 6)
#define GENX(X) X##_v6
#include "genxml/v6_pack.h"
#elif (PAN_ARCH == 7)
#define GENX(X) X##_v7
#include "genxml/v7_pack.h"
#elif (PAN_ARCH == 9)
#define GENX(X) X##_v9
#include "genxml/v9_pack.h"
#elif (PAN_ARCH == 10)
#define GENX(X) X##_v10
#include "genxml/v10_pack.h"
#elif (PAN_ARCH == 12)
#define GENX(X) X##_v12
#include "genxml/v12_pack.h"
#elif (PAN_ARCH == 13)
#define GENX(X) X##_v13
#include "genxml/v13_pack.h"
#elif (PAN_ARCH == 14)
#define GENX(X) X##_v14
#include "genxml/v14_pack.h"
#else
#error "Need to add suffixing macro for this architecture"
#endif

#endif /* PAN_ARCH */
#endif /* GEN_MACROS_H */
