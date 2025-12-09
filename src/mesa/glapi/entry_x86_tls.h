/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 2010 LunarG Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Chia-I Wu <olv@lunarg.com>
 */

#include <string.h>

#ifdef __CET__
#define ENDBR "endbr32\n\t"
#else
#define ENDBR
#endif

#ifdef HAVE_FUNC_ATTRIBUTE_VISIBILITY
#define HIDDEN __attribute__((visibility("hidden")))
#else
#define HIDDEN
#endif

#define X86_ENTRY_SIZE 32

__asm__(".text");

__asm__("x86_current_tls:\n\t"
	"call 1f\n"
        "1:\n\t"
        "popl %eax\n\t"
	"addl $_GLOBAL_OFFSET_TABLE_+[.-1b], %eax\n\t"
	"movl _mesa_glapi_tls_Dispatch@GOTNTPOFF(%eax), %eax\n\t"
	"ret");

#ifndef GLX_X86_READONLY_TEXT
__asm__(".section wtext, \"awx\", @progbits");
#endif /* GLX_X86_READONLY_TEXT */

__asm__(".balign 16\n"
        "x86_entry_start:");

#define STUB_ASM_ENTRY(func)     \
   ".globl " func "\n"           \
   ".type " func ", @function\n" \
   ".balign 16\n"                \
   func ":"

#define STUB_ASM_CODE(slot)                                 \
   ENDBR                                                    \
   "call 1f\n"                                              \
   "1:\n\t"                                                 \
   "popl %eax\n\t"                                          \
   "addl $_GLOBAL_OFFSET_TABLE_+[.-1b], %eax\n\t"           \
   "movl _mesa_glapi_tls_Dispatch@GOTNTPOFF(%eax), %eax\n\t" \
   "movl %gs:(%eax), %eax\n\t"                              \
   "jmp *(4 * " slot ")(%eax)"
