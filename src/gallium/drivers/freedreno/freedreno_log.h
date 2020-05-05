/*
 * Copyright © 2020 Google, Inc.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef FREEDRENO_LOG_H_
#define FREEDRENO_LOG_H_

#include "freedreno_util.h"

struct fd_batch;
struct fd_context;

void fd_log_process(struct fd_context *ctx, bool wait);
void fd_log_flush(struct fd_batch *batch);
void _fd_log(struct fd_batch *batch, const char *fmt, ...)
	_util_printf_format(2, 3);

/* macro wrapper so that arguments are not evaluated when logging is not
 * enabled:
 */
#define fd_log(__batch, __fmt, ...) 							\
	do { 														\
		if (unlikely(fd_mesa_debug & FD_DBG_LOG)) { 			\
			_fd_log(__batch, __fmt, ##__VA_ARGS__); 			\
		} else { 												\
			DBG(__fmt, ##__VA_ARGS__); 							\
		} 														\
	} while (0)

/* A variant of fd_log() which provides a FILE* stream to write the
 * log msg into, mostly to make u_dump_state stuff useful
 */
#define fd_log_stream(__batch, __stream, __exp)					\
	do {														\
		if (unlikely(fd_mesa_debug & FD_DBG_LOG)) { 			\
			char buf[1024];										\
			FILE *__stream = fmemopen(buf, sizeof(buf), "w");	\
			__exp;												\
			fclose(__stream);									\
			_fd_log(__batch, "%s", buf);						\
		} 														\
	} while (0)

void fd_log_eof(struct fd_context *ctx);

#endif /* FREEDRENO_LOG_H_ */
