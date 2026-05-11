/* SPDX-License-Identifier: MIT */

/* Copyright 2024 Advanced Micro Devices, Inc. */

#ifndef SPL_DEBUG_H
#define SPL_DEBUG_H

#if defined(LINUX_DM)
#if defined(CONFIG_HAVE_KGDB) || defined(CONFIG_KGDB)
#define SPL_ASSERT_CRITICAL(expr) do {	\
	if (WARN_ON(!(expr))) { \
		kgdb_breakpoint(); \
	} \
} while (0)
#else
#define SPL_ASSERT_CRITICAL(expr) do {	\
	if (WARN_ON(!(expr))) { \
		; \
	} \
} while (0)
#endif /* CONFIG_HAVE_KGDB || CONFIG_KGDB */

#if defined(CONFIG_DEBUG_KERNEL_DC)
#define SPL_ASSERT(expr) SPL_ASSERT_CRITICAL(expr)
#else
#define SPL_ASSERT(expr) WARN_ON(!(expr))
#endif /* CONFIG_DEBUG_KERNEL_DC */

#define SPL_BREAK_TO_DEBUGGER() SPL_ASSERT(0)

#else /* other DMs */

#ifdef DBG

/*
 * Definition of a break to the debugger that should cover all platforms that
 * we expect to run under.  This break goes directly to the platform provided
 * method for breaking.  It does not use the MCIL interface.
 */
#if defined(__GNUC__)
#if defined(__i386__) || defined(__x86_64__)
#define SPL_BREAK_TO_DEBUGGER()         __asm("int3;")
#elif defined(__powerpc__)
#define SPL_BREAK_TO_DEBUGGER()         __asm(".long 0x7d821008 ")
#else
/*
 * Unsupported GCC architecture. Define macro to
 * generate error during compilation.
 */
#define SPL_BREAK_TO_DEBUGGER()   0
#endif
#elif defined(_WIN32)
/*
 * Assume that we are using Microsoft compiler.
 * Let's use compiler intrinsic
 */
#define SPL_BREAK_TO_DEBUGGER()         __debugbreak()
#else
/*
 * Unsupported Architecture. Define macro to
 * generate error during compilation.
 */
#define SPL_BREAK_TO_DEBUGGER()   0
#endif

/*
 * Hard assert with no message.  Goes stright to debugger.
 * Not supported through MCIL.
 */
#ifdef SPL_ASSERT
#undef SPL_ASSERT
#endif
#define SPL_ASSERT(b) {if (!(b)) {SPL_BREAK_TO_DEBUGGER(); }}

#define SPL_ASSERT_CRITICAL(expr)  do {if (!(expr)) SPL_BREAK_TO_DEBUGGER(); } while (0)

 /*
  * DebugPrint() is a method of the DalBaseClass.
  * This macro makes the assumption that it will be called from within a C++ object derived
  * from the DalBaseClass.
  *
  * DALMSG uses the MCIL interface to display a runtime debug message.
  */
#define SPL_DALMSG(b) {DebugPrint b; }
#define SPL_DAL_ASSERT_MSG(b, m) {bool __bCondition_ = (b); if (!__bCondition_) {SPL_DALMSG(m) {SPL_BREAK_TO_DEBUGGER(); }; }; }

#else   // DBG

#ifdef SPL_ASSERT
#undef SPL_ASSERT
#endif
//#define SPL_ASSERT(_bool)  (do {} while (0))
#define SPL_ASSERT(_bool)

#define SPL_ASSERT_CRITICAL(expr)  do {if (expr)/* Do nothing */; } while (0)

#ifdef SPL_BREAK_TO_DEBUGGER
#undef SPL_BREAK_TO_DEBUGGER
#endif
#define SPL_BREAK_TO_DEBUGGER()

#ifdef SPL_DALMSG
#undef SPL_DALMSG
#endif
#define SPL_DALMSG(b)

#ifdef SPL_DAL_ASSERT_MSG
#undef SPL_DAL_ASSERT_MSG
#endif
#define SPL_DAL_ASSERT_MSG(b, m)

#endif  // DBG

#endif /* LINUX_DM */

#endif  // SPL_DEBUG_H
