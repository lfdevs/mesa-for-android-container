/*
 * Copyright © 2025 Imagination Technologies Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PCO_PP_MACROS_H
#define PCO_PP_MACROS_H

#define _NUM_ARGS(_0,  \
                  _1,  \
                  _2,  \
                  _3,  \
                  _4,  \
                  _5,  \
                  _6,  \
                  _7,  \
                  _8,  \
                  _9,  \
                  _10, \
                  _11, \
                  _12, \
                  _13, \
                  _14, \
                  _15, \
                  _16, \
                  N,   \
                  ...) \
   N

/* Returns the numbers of arguments passed to the macro. */
#define NUM_ARGS(...)       \
   _NUM_ARGS(_,             \
             ##__VA_ARGS__, \
             16,            \
             15,            \
             14,            \
             13,            \
             12,            \
             11,            \
             10,            \
             9,             \
             8,             \
             7,             \
             6,             \
             5,             \
             4,             \
             3,             \
             2,             \
             1,             \
             0)

/* Returns the numbers of arguments passed to the macro + 2. */
#define NUM_ARGS_PLUS_2(...) \
   _NUM_ARGS(_,              \
             ##__VA_ARGS__,  \
             18,             \
             17,             \
             16,             \
             15,             \
             14,             \
             13,             \
             12,             \
             11,             \
             10,             \
             9,              \
             8,              \
             7,              \
             6,              \
             5,              \
             4,              \
             3,              \
             2)

#define _CAT2(a, b) a##b

/* Concatenates two tokens. */
#define CAT2(a, b) _CAT2(a, b)

#define _CAT3(a, b, c) a##b##c

/* Concatenates three tokens. */
#define CAT3(a, b, c) _CAT3(a, b, c)

/* Constructs a function name from its base name, separator, and the number of
 * args passed to it.
 */
#define SELECT_NAME(f, sep, ...) CAT3(f, sep, NUM_ARGS(__VA_ARGS__))

#endif /* ifndef PCO_PP_MACROS_H */
