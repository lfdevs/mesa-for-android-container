/****************************************************************************
* Copyright (C) 2014-2015 Intel Corporation.   All Rights Reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice (including the next
* paragraph) shall be included in all copies or substantial portions of the
* Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
* IN THE SOFTWARE.
****************************************************************************/

#ifndef __SWR_ASSERT_H__
#define __SWR_ASSERT_H__

#if !defined(__SWR_OS_H__)
#error swr_assert.h should not be included directly, please include "common/os.h" instead.
#endif

//=============================================================================
//
// MACROS defined in this file:
//
// - SWR_ASSUME(expression, ...):   Tell compiler that the expression is true.
//                                  Helps with static code analysis as well.
//                                  DO NOT USE if code after this dynamically
//                                  checks for errors and handles them.  The
//                                  compiler may optimize out the error check.
//
// - SWR_ASSERT(expression, ...):   Inform the user is expression is false.
//                                  This check is only conditionally made,
//                                  usually only in debug mode.
//
// - SWR_REL_ASSERT(expression, ...): Unconditionally enabled version of SWR_ASSERT
//
// - SWR_ASSUME_ASSERT(expression, ...): Conditionally enabled SWR_ASSERT.  Uses
//                                       SWR_ASSUME if SWR_ASSERT is disabled.
//                                       DO NOT USE in combination with actual
//                                       error checking (see SWR_ASSUME)
//
// - SWR_REL_ASSUME_ASSERT(expression, ...): Same as SWR_REL_ASSERT.
//
//=============================================================================

#if defined(_WIN32)
#define SWR_ASSUME(e, ...) __assume(e)
#elif defined(__clang__)
#define SWR_ASSUME(e, ...) __builtin_assume(e)
#elif defined(__GNUC__)
#define SWR_ASSUME(e, ...) ((e) ? ((void)0) : __builtin_unreachable())
#else
#define SWR_ASSUME(e, ...) ASSUME(e)
#endif

#if !defined(SWR_ENABLE_ASSERTS)

#if !defined(NDEBUG)
#define SWR_ENABLE_ASSERTS 1
#else
#define SWR_ENABLE_ASSERTS 0
#endif // _DEBUG

#endif // SWR_ENABLE_ASSERTS

#if !defined(SWR_ENABLE_REL_ASSERTS)
#define SWR_ENABLE_REL_ASSERTS 1
#endif

#if SWR_ENABLE_ASSERTS || SWR_ENABLE_REL_ASSERTS
#include "assert.h"

#if !defined(__cplusplus)

#pragma message("C++ is required for SWR Asserts, falling back to assert.h")

#if SWR_ENABLE_ASSERTS
#define SWR_ASSERT(e, ...) assert(e)
#endif

#if SWR_ENABLE_REL_ASSERTS
#define SWR_REL_ASSERT(e, ...) assert(e)
#endif

#else

bool SwrAssert(
    bool        chkDebugger,
    bool&       enabled,
    const char* pExpression,
    const char* pFileName,
    uint32_t    lineNum,
    const char* function,
    const char* pFmtString = nullptr,
    ...);

#define _SWR_ASSERT(chkDebugger, e, ...) {\
    bool expFailed = !(e);\
    if (expFailed) {\
        static bool swrAssertEnabled = true;\
        expFailed = SwrAssert(chkDebugger, swrAssertEnabled, #e, __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__);\
        if (expFailed) { DEBUGBREAK; }\
    }\
}

#if SWR_ENABLE_ASSERTS
#define SWR_ASSERT(e, ...)              _SWR_ASSERT(true, e, ##__VA_ARGS__)
#define SWR_ASSUME_ASSERT(e, ...)       SWR_ASSERT(e, ##__VA_ARGS__)

#if defined(assert)
#undef assert
#endif
#define assert(exp) SWR_ASSERT(exp)

#endif // SWR_ENABLE_ASSERTS

#if SWR_ENABLE_REL_ASSERTS
#define SWR_REL_ASSERT(e, ...)          _SWR_ASSERT(false, e, ##__VA_ARGS__)
#define SWR_REL_ASSUME_ASSERT(e, ...)   SWR_REL_ASSERT(e, ##__VA_ARGS__)
#endif

#endif // C++

#endif // SWR_ENABLE_ASSERTS || SWR_ENABLE_REL_ASSERTS

#if !SWR_ENABLE_ASSERTS
#define SWR_ASSERT(e, ...)              (void)(0)
#define SWR_ASSUME_ASSERT(e, ...)       SWR_ASSUME(e, ##__VA_ARGS__)
#endif

#if !SWR_ENABLE_REL_ASSERTS
#define SWR_REL_ASSERT(e, ...)          (void)(0)
#define SWR_REL_ASSUME_ASSERT(e, ...)   SWR_ASSUME(e, ##__VA_ARGS__)
#endif

#define SWR_NOT_IMPL SWR_ASSERT(0, "%s not implemented", __FUNCTION__)

#endif//__SWR_ASSERT_H__
