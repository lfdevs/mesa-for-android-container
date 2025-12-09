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
#include <sstream>
#include <iomanip>

#include <llvm/Config/llvm-config.h>
#include <llvm-c/Core.h>
#include <llvm-c/Disassembler.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/Format.h>
#include <llvm/IR/Module.h>

#if LLVM_VERSION_MAJOR >= 17
#include <llvm/TargetParser/Host.h>
#else
#include <llvm/Support/Host.h>
#endif

#include "util/bitscan.h"
#include "util/u_debug.h"
#include "util/os_file.h"
#include "util/u_string.h" // asprintf on MSVC

#include "lp_bld_init.h"
#include "lp_bld_debug.h"
#include "lp_bld_intr.h"

#ifdef __linux__
#include <sys/stat.h>
#include <fcntl.h>
#endif

#include <llvm/BinaryFormat/Dwarf.h>


/**
 * Check alignment.
 *
 * It is important that this check is not implemented as a macro or inlined
 * function, as the compiler assumptions in respect to alignment of global
 * and stack variables would often make the check a no op, defeating the
 * whole purpose of the exercise.
 */
extern "C" bool
lp_check_alignment(const void *ptr, unsigned alignment)
{
   assert(util_is_power_of_two_or_zero(alignment));
   return ((uintptr_t)ptr & (alignment - 1)) == 0;
}


/**
 * Same as LLVMDumpValue, but through our debugging channels.
 */
extern "C" void
lp_debug_dump_value(LLVMValueRef value)
{
   char *str = LLVMPrintValueToString(value);
   if (str) {
      os_log_message(str);
      LLVMDisposeMessage(str);
   }
}


/*
 * Disassemble a function, using the LLVM MC disassembler.
 *
 * See also:
 * - http://blog.llvm.org/2010/01/x86-disassembler.html
 * - http://blog.llvm.org/2010/04/intro-to-llvm-mc-project.html
 */
static size_t
disassemble(const void* func, std::ostream &buffer)
{
   const uint8_t *bytes = (const uint8_t *)func;

   /*
    * Limit disassembly to this extent
    */
   const uint64_t extent = 96 * 1024;

   /*
    * Initialize all used objects.
    */

   const char *triple = LLVM_HOST_TRIPLE;
   LLVMDisasmContextRef D = LLVMCreateDisasm(triple, NULL, 0, NULL, NULL);
   char outline[1024];

   if (!D) {
      buffer << "error: could not create disassembler for triple "
             << triple << '\n';
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

      buffer << std::setw(6) << std::hex << (unsigned long)pc
             << std::setw(0) << std::dec << ":";

      Size = LLVMDisasmInstruction(D, (uint8_t *)bytes + pc, extent - pc, 0, outline,
                                   sizeof(outline));

      if (!Size) {
#if DETECT_ARCH_AARCH64
         uint32_t invalid = bytes[pc + 0] << 0 | bytes[pc + 1] << 8 |
                            bytes[pc + 2] << 16 | bytes[pc + 3] << 24;
         snprintf(outline, sizeof(outline), "\tinvalid %x", invalid);
         Size = 4;
#else
         buffer << "\tinvalid\n";
         break;
#endif
      }

      /*
       * Output the bytes in hexidecimal format.
       */

      if (0) {
         unsigned i;
         for (i = 0; i < Size; ++i) {
            buffer << std::hex << std::setfill('0') << std::setw(2)
                   << static_cast<int> (bytes[pc + i])
                   << std::setw(0) << std::dec;
         }
         for (; i < 16; ++i) {
            buffer << "   ";
         }
      }

      /*
       * Print the instruction.
       */

      buffer << outline << '\n';

      /*
       * Advance.
       */

      pc += Size;

      /*
       * Stop disassembling on return statements
       */

#if DETECT_ARCH_X86 || DETECT_ARCH_X86_64
      if (Size == 1 && bytes[pc - 1] == 0xc3) {
         break;
      }
#elif DETECT_ARCH_AARCH64
      if (Size == 4 && bytes[pc - 1] == 0xD6 && bytes[pc - 2] == 0x5F &&
            (bytes[pc - 3] & 0xFC) == 0 && (bytes[pc - 4] & 0x1F) == 0) {
         break;
      }
#endif

      if (pc >= extent) {
         buffer << "disassembly larger than " << extent << " bytes, aborting\n";
         break;
      }
   }

   buffer << '\n';

   LLVMDisasmDispose(D);

   /*
    * Print GDB command, useful to verify output.
    */
   if (0) {
      buffer << "disassemble " << std::hex << static_cast<const void*>(bytes) << ' '
             << static_cast<const void*>(bytes + pc) << std::dec << '\n';
   }

   return pc;
}


extern "C" void
lp_disassemble(LLVMValueRef func, const void *code)
{
   std::ostringstream buffer;
   std::string s;

   buffer << LLVMGetValueName(func) << ":\n";
   disassemble(code, buffer);
   s = buffer.str();
   os_log_message(s.c_str());
   os_log_message("\n");
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
#if defined(PROFILE)
   static std::ofstream perf_asm_file;
   static bool first_time = true;
   static FILE *perf_map_file = NULL;
   if (first_time) {
      unsigned long long pid = (unsigned long long)getpid();
      char filename[1024];
#if defined(__linux__)
      /*
       * We rely on the disassembler for determining a function's size, but
       * the disassembly is a leaky and slow operation, so avoid running
       * this except when running inside linux perf, which can be inferred
       * by the PERF_BUILDID_DIR environment variable.
       */
      if (os_get_option("PERF_BUILDID_DIR")) {
         snprintf(filename, sizeof(filename), "/tmp/perf-%llu.map", pid);
         perf_map_file = fopen(filename, "wt");
         snprintf(filename, sizeof(filename), "/tmp/perf-%llu.map.asm", pid);
         perf_asm_file.open(filename);
      }
#else
      if (const char* output_dir = os_get_option("JIT_SYMBOL_MAP_DIR")) {
         snprintf(filename, sizeof(filename), "%s/jit-symbols-%llu.map", output_dir, pid);
         perf_map_file = fopen(filename, "wt");
         snprintf(filename, sizeof(filename), "%s/jit-symbols-%llu.map.asm", output_dir, pid);
         perf_asm_file.open(filename);
      }
#endif
      first_time = false;
   }
   if (perf_map_file) {
      const char *symbol = LLVMGetValueName(func);
      unsigned long addr = (uintptr_t)code;
      perf_asm_file << symbol << " " << std::hex
                    << (uintptr_t)code << std::dec << ":\n";
      unsigned long size = disassemble(code, perf_asm_file);
      perf_asm_file.flush();
      fprintf(perf_map_file, "%lx %lx %s\n", addr, size, symbol);
      fflush(perf_map_file);
   }
#else
   (void)func;
   (void)code;
#endif
}


LLVMMetadataRef
lp_bld_debug_info_type(gallivm_state *gallivm, LLVMTypeRef type)
{
   LLVMTypeKind kind = LLVMGetTypeKind(type);

   if (kind == LLVMHalfTypeKind)
      return LLVMDIBuilderCreateBasicType(
         gallivm->di_builder, "float16_t", strlen("float16_t"), 16, llvm::dwarf::DW_ATE_float, LLVMDIFlagZero);
   if (kind == LLVMFloatTypeKind)
      return LLVMDIBuilderCreateBasicType(
         gallivm->di_builder, "float", strlen("float"), 32, llvm::dwarf::DW_ATE_float, LLVMDIFlagZero);
   if (kind == LLVMDoubleTypeKind)
      return LLVMDIBuilderCreateBasicType(
         gallivm->di_builder, "double", strlen("double"), 64, llvm::dwarf::DW_ATE_float, LLVMDIFlagZero);

   if (kind == LLVMIntegerTypeKind) {
      uint32_t bit_size = LLVMGetIntTypeWidth(type);
      if (bit_size == 1)
         return LLVMDIBuilderCreateBasicType(
            gallivm->di_builder, "bool", strlen("bool"), 1, llvm::dwarf::DW_ATE_unsigned, LLVMDIFlagZero);
      if (bit_size == 8)
         return LLVMDIBuilderCreateBasicType(
            gallivm->di_builder, "int8_t", strlen("int8_t"), 8, llvm::dwarf::DW_ATE_unsigned, LLVMDIFlagZero);
      if (bit_size == 16)
         return LLVMDIBuilderCreateBasicType(
            gallivm->di_builder, "int16_t", strlen("int16_t"), 16, llvm::dwarf::DW_ATE_unsigned, LLVMDIFlagZero);
      if (bit_size == 32)
         return LLVMDIBuilderCreateBasicType(
            gallivm->di_builder, "int32_t", strlen("int32_t"), 32, llvm::dwarf::DW_ATE_unsigned, LLVMDIFlagZero);
      if (bit_size == 64)
         return LLVMDIBuilderCreateBasicType(
            gallivm->di_builder, "int64_t", strlen("int64_t"), 64, llvm::dwarf::DW_ATE_unsigned, LLVMDIFlagZero);
   }

   if (kind == LLVMFunctionTypeKind) {
      uint32_t num_params = LLVMCountParamTypes(type);

      LLVMTypeRef *param_types = (LLVMTypeRef *)calloc(num_params, sizeof(LLVMTypeRef));
      LLVMMetadataRef *di_param_types = (LLVMMetadataRef *)calloc(num_params + 1, sizeof(LLVMMetadataRef));

      LLVMGetParamTypes(type, param_types);

      di_param_types[0] = lp_bld_debug_info_type(gallivm, LLVMGetReturnType(type));
      for (uint32_t i = 0;  i < num_params; i++)
         di_param_types[i + 1] = lp_bld_debug_info_type(gallivm, param_types[i]);

      LLVMMetadataRef function = LLVMDIBuilderCreateSubroutineType(
         gallivm->di_builder, gallivm->file, di_param_types, num_params + 1, LLVMDIFlagZero);

      free(param_types);
      free(di_param_types);

      return function;
   }

   if (kind == LLVMArrayTypeKind) {
      uint32_t count = LLVMGetArrayLength(type);
      LLVMMetadataRef subrange = LLVMDIBuilderGetOrCreateSubrange(gallivm->di_builder, 0, count);
      LLVMMetadataRef element_type = lp_bld_debug_info_type(gallivm, LLVMGetElementType(type));
      return LLVMDIBuilderCreateArrayType(
         gallivm->di_builder, count, 0, element_type, &subrange, 1);
   }

   if (kind == LLVMPointerTypeKind) {
      return LLVMDIBuilderCreatePointerType(
         gallivm->di_builder, NULL, sizeof(void *) * 8, 0, 0, "", 0);
   }

   if (kind == LLVMVectorTypeKind) {
      uint32_t count = LLVMGetVectorSize(type);
      LLVMMetadataRef subrange = LLVMDIBuilderGetOrCreateSubrange(gallivm->di_builder, 0, count);
      LLVMMetadataRef element_type = lp_bld_debug_info_type(gallivm, LLVMGetElementType(type));
      return LLVMDIBuilderCreateVectorType(
         gallivm->di_builder, count, 0, element_type, &subrange, 1);
   }

   return NULL;
}


static uint32_t global_shader_index = 0;


void
lp_function_add_debug_info(gallivm_state *gallivm, LLVMValueRef func, LLVMTypeRef func_type)
{
   if (!gallivm->di_builder)
      return;

   if (!gallivm->file) {
      uint32_t shader_index = p_atomic_add_return(&global_shader_index, 1);

      os_mkdir(LP_NIR_SHADER_DUMP_DIR, 0755);

      asprintf(&gallivm->file_name, "%s/%u.nir", LP_NIR_SHADER_DUMP_DIR, shader_index);

      gallivm->file = LLVMDIBuilderCreateFile(gallivm->di_builder, gallivm->file_name, strlen(gallivm->file_name), ".", 1);

      LLVMDIBuilderCreateCompileUnit(
         gallivm->di_builder, LLVMDWARFSourceLanguageC11, gallivm->file, gallivm->file_name, strlen(gallivm->file_name),
         0, NULL, 0, 0, NULL, 0, LLVMDWARFEmissionFull, 0, 0, 0, "/", 1, "", 0);
   }

   LLVMMetadataRef di_function_type = lp_bld_debug_info_type(gallivm, func_type);
   const char *func_name = LLVMGetValueName(func);
   LLVMMetadataRef di_function = LLVMDIBuilderCreateFunction(
      gallivm->di_builder, NULL, func_name, strlen(func_name), func_name, strlen(func_name),
      gallivm->file, 1, di_function_type, true, true, 1, LLVMDIFlagZero, false);

   LLVMSetSubprogram(func, di_function);

   lp_add_function_attr(func, -1, LP_FUNC_ATTR_NOINLINE);
   lp_add_function_attr(func, -1, LP_FUNC_ATTR_OPTNONE);

   gallivm->di_function = di_function;
}
