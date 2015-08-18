/**************************************************************************
 *
 * Copyright 2009-2011 VMware, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#include <stddef.h>

#include <llvm-c/Core.h>
#include <llvm-c/Disassembler.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/Format.h>
#include <llvm/Support/Host.h>
#include <llvm/IR/Module.h>

#include "util/u_math.h"
#include "util/u_debug.h"

#include "lp_bld_debug.h"

#ifdef __linux__
#include <sys/stat.h>
#include <fcntl.h>
#endif



/**
 * Check alignment.
 *
 * It is important that this check is not implemented as a macro or inlined
 * function, as the compiler assumptions in respect to alignment of global
 * and stack variables would often make the check a no op, defeating the
 * whole purpose of the exercise.
 */
extern "C" boolean
lp_check_alignment(const void *ptr, unsigned alignment)
{
   assert(util_is_power_of_two(alignment));
   return ((uintptr_t)ptr & (alignment - 1)) == 0;
}

#if (defined(PIPE_OS_WINDOWS) && !defined(PIPE_CC_MSVC)) || defined(PIPE_OS_EMBEDDED)

class raw_debug_ostream :
   public llvm::raw_ostream
{
private:
   uint64_t pos;

public:
   raw_debug_ostream() : pos(0) { }

   void write_impl(const char *Ptr, size_t Size);

   uint64_t current_pos() const { return pos; }
   size_t preferred_buffer_size() const { return 512; }
};


void
raw_debug_ostream::write_impl(const char *Ptr, size_t Size)
{
   if (Size > 0) {
      char *lastPtr = (char *)&Ptr[Size];
      char last = *lastPtr;
      *lastPtr = 0;
      _debug_printf("%*s", Size, Ptr);
      *lastPtr = last;
      pos += Size;
   }
}

#endif

extern "C" const char *
lp_get_module_id(LLVMModuleRef module)
{
   return llvm::unwrap(module)->getModuleIdentifier().c_str();
}


/**
 * Same as LLVMDumpValue, but through our debugging channels.
 */
extern "C" void
lp_debug_dump_value(LLVMValueRef value)
{
#if (defined(PIPE_OS_WINDOWS) && !defined(PIPE_CC_MSVC)) || defined(PIPE_OS_EMBEDDED)
   raw_debug_ostream os;
   llvm::unwrap(value)->print(os);
   os.flush();
#else
   LLVMDumpValue(value);
#endif
}


/*
 * Disassemble a function, using the LLVM MC disassembler.
 *
 * See also:
 * - http://blog.llvm.org/2010/01/x86-disassembler.html
 * - http://blog.llvm.org/2010/04/intro-to-llvm-mc-project.html
 */
static size_t
disassemble(const void* func)
{
   const uint8_t *bytes = (const uint8_t *)func;

   /*
    * Limit disassembly to this extent
    */
   const uint64_t extent = 96 * 1024;

   /*
    * Initialize all used objects.
    */

   std::string Triple = llvm::sys::getProcessTriple();
   LLVMDisasmContextRef D = LLVMCreateDisasm(Triple.c_str(), NULL, 0, NULL, NULL);
   char outline[1024];

   if (!D) {
      _debug_printf("error: couldn't create disassembler for triple %s\n",
                    Triple.c_str());
      return 0;
   }

   uint64_t pc;
   pc = 0;
   while (pc < extent) {
      size_t Size;

      /*
       * Print address.  We use addresses relative to the start of the function,
       * so that between runs.
       */

      _debug_printf("%6lu:\t", (unsigned long)pc);

      Size = LLVMDisasmInstruction(D, (uint8_t *)bytes + pc, extent - pc, 0, outline,
                                   sizeof outline);

      if (!Size) {
         _debug_printf("invalid\n");
         pc += 1;
         break;
      }

      /*
       * Output the bytes in hexidecimal format.
       */

      if (0) {
         unsigned i;
         for (i = 0; i < Size; ++i) {
            _debug_printf("%02x ", bytes[pc + i]);
         }
         for (; i < 16; ++i) {
            _debug_printf("   ");
         }
      }

      /*
       * Print the instruction.
       */

      _debug_printf("%*s", Size, outline);

      _debug_printf("\n");

      /*
       * Stop disassembling on return statements, if there is no record of a
       * jump to a successive address.
       *
       * XXX: This currently assumes x86
       */

      if (Size == 1 && bytes[pc] == 0xc3) {
         break;
      }

      /*
       * Advance.
       */

      pc += Size;

      if (pc >= extent) {
         _debug_printf("disassembly larger than %ull bytes, aborting\n", extent);
         break;
      }
   }

   _debug_printf("\n");

   LLVMDisasmDispose(D);

   /*
    * Print GDB command, useful to verify output.
    */
   if (0) {
      _debug_printf("disassemble %p %p\n", bytes, bytes + pc);
   }

   return pc;
}


extern "C" void
lp_disassemble(LLVMValueRef func, const void *code) {
   _debug_printf("%s:\n", LLVMGetValueName(func));
   disassemble(code);
}


/*
 * Linux perf profiler integration.
 *
 * See also:
 * - http://penberg.blogspot.co.uk/2009/06/jato-has-profiler.html
 * - https://github.com/penberg/jato/commit/73ad86847329d99d51b386f5aba692580d1f8fdc
 * - http://git.kernel.org/?p=linux/kernel/git/torvalds/linux.git;a=commitdiff;h=80d496be89ed7dede5abee5c057634e80a31c82d
 */
extern "C" void
lp_profile(LLVMValueRef func, const void *code)
{
#if defined(__linux__) && defined(PROFILE)
   static boolean first_time = TRUE;
   static FILE *perf_map_file = NULL;
   static int perf_asm_fd = -1;
   if (first_time) {
      /*
       * We rely on the disassembler for determining a function's size, but
       * the disassembly is a leaky and slow operation, so avoid running
       * this except when running inside linux perf, which can be inferred
       * by the PERF_BUILDID_DIR environment variable.
       */
      if (getenv("PERF_BUILDID_DIR")) {
         pid_t pid = getpid();
         char filename[256];
         util_snprintf(filename, sizeof filename, "/tmp/perf-%llu.map", (unsigned long long)pid);
         perf_map_file = fopen(filename, "wt");
         util_snprintf(filename, sizeof filename, "/tmp/perf-%llu.map.asm", (unsigned long long)pid);
         mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
         perf_asm_fd = open(filename, O_WRONLY | O_CREAT, mode);
      }
      first_time = FALSE;
   }
   if (perf_map_file) {
      const char *symbol = LLVMGetValueName(func);
      unsigned long addr = (uintptr_t)code;
      llvm::raw_fd_ostream Out(perf_asm_fd, false);
      Out << symbol << ":\n";
      unsigned long size = disassemble(code);
      fprintf(perf_map_file, "%lx %lx %s\n", addr, size, symbol);
      fflush(perf_map_file);
   }
#else
   (void)func;
   (void)code;
#endif
}


