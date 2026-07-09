/*
 * Copyright (c) 2012 Apple Inc.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE ABOVE LISTED COPYRIGHT
 * HOLDER(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name(s) of the above
 * copyright holders shall not be used in advertising or otherwise to
 * promote the sale, use or other dealings in this Software without
 * prior written authorization.
 */

#ifndef APPLE_GLX_LOG_H
#define APPLE_GLX_LOG_H

#include <AvailabilityMacros.h>
#include <sys/cdefs.h>

#if defined(MAC_OS_X_VERSION_MIN_REQUIRED) && MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_12
#include <os/log.h>

extern os_log_t apple_glx_oslog;

#define apple_glx_log(fmt, args...)       os_log(apple_glx_oslog, fmt, ##args)
#define apple_glx_log_info(fmt, args...)  os_log_info(apple_glx_oslog, fmt, ##args)
#define apple_glx_log_debug(fmt, args...) os_log_debug(apple_glx_oslog, fmt, ##args)
#define apple_glx_log_error(fmt, args...) os_log_error(apple_glx_oslog, fmt, ##args)

#else
#include <asl.h>

#define apple_glx_log(fmt, args...)       asl_log(NULL, NULL, ASL_LEVEL_NOTICE, fmt, ##args)
#define apple_glx_log_info(fmt, args...)  asl_log(NULL, NULL, ASL_LEVEL_INFO, fmt, ##args)
#define apple_glx_log_debug(fmt, args...) asl_log(NULL, NULL, ASL_LEVEL_DEBUG, fmt, ##args)
#define apple_glx_log_error(fmt, args...) asl_log(NULL, NULL, ASL_LEVEL_ERR, fmt, ##args)

#endif

void apple_glx_log_init(void);

#endif
