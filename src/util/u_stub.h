/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 * 
 * Helpers macro to stub out some functions by replacing the declaration
 * with an empty stub.
 * The typical usage is:
 *
 *    #ifndef HAVE_XXXX
 *    #define __U_STUB__
 *    #endif
 *    #include "u_stub.h"
 * 
 * To override TAIL, define __U_STUB__TAIL before including u_stub.h, for example:
 *
 *    #define __U_STUB__TAIL { return VA_STATUS_ERROR_UNIMPLEMENTED; }
 *    #ifndef HAVE_XXXX
 *    #define __U_STUB__
 *    #endif
 *    #include "u_stub.h"
 *
 */

#undef MESAPROC
#undef TAIL
#undef TAILZ
#undef TAILV
#undef TAILB
#undef TAILBT
#undef TAILPTR

#ifdef __U_STUB__
#define MESAPROC static inline

#ifdef __U_STUB__TAIL
#define TAIL __U_STUB__TAIL
#else
#define TAIL                                                                                       \
   {                                                                                               \
      return -1;                                                                                   \
   }
#endif
#define TAILZ                                                                                      \
   {                                                                                               \
      return 0;                                                                                     \
   }
#define TAILV                                                                                      \
   {                                                                                               \
   }
#define TAILPTR                                                                                    \
   {                                                                                               \
      return NULL;                                                                                 \
   }
#define TAILB                                                                                      \
   {                                                                                               \
      return false;                                                                                \
   }
#define TAILBT                                                                                     \
   {                                                                                               \
      return true;                                                                                 \
   }
#else
#define MESAPROC
#define TAIL
#define TAILZ
#define TAILV
#define TAILB
#define TAILBT
#define TAILPTR
#endif

#undef __U_STUB__