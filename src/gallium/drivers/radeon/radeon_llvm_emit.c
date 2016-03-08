/*
 * Copyright 2011 Advanced Micro Devices, Inc.
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
 *
 * Authors: Tom Stellard <thomas.stellard@amd.com>
 *
 */

#include "radeon_llvm_emit.h"
#include "radeon_elf_util.h"
#include "c11/threads.h"
#include "gallivm/lp_bld_misc.h"
#include "util/u_debug.h"
#include "util/u_memory.h"
#include "pipe/p_shader_tokens.h"
#include "pipe/p_state.h"

#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Core.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define CPU_STRING_LEN 30
#define FS_STRING_LEN 30
#define TRIPLE_STRING_LEN 7

/**
 * Shader types for the LLVM backend.
 */
enum radeon_llvm_shader_type {
	RADEON_LLVM_SHADER_PS = 0,
	RADEON_LLVM_SHADER_VS = 1,
	RADEON_LLVM_SHADER_GS = 2,
	RADEON_LLVM_SHADER_CS = 3,
};

void radeon_llvm_add_attribute(LLVMValueRef F, const char *name, int value)
{
	char str[16];

	snprintf(str, sizeof(str), "%i", value);
	LLVMAddTargetDependentFunctionAttr(F, name, str);
}

/**
 * Set the shader type we want to compile
 *
 * @param type shader type to set
 */
void radeon_llvm_shader_type(LLVMValueRef F, unsigned type)
{
	enum radeon_llvm_shader_type llvm_type;

	switch (type) {
	case TGSI_PROCESSOR_VERTEX:
	case TGSI_PROCESSOR_TESS_CTRL:
	case TGSI_PROCESSOR_TESS_EVAL:
		llvm_type = RADEON_LLVM_SHADER_VS;
		break;
	case TGSI_PROCESSOR_GEOMETRY:
		llvm_type = RADEON_LLVM_SHADER_GS;
		break;
	case TGSI_PROCESSOR_FRAGMENT:
		llvm_type = RADEON_LLVM_SHADER_PS;
		break;
	case TGSI_PROCESSOR_COMPUTE:
		llvm_type = RADEON_LLVM_SHADER_CS;
		break;
	default:
		assert(0);
	}

	radeon_llvm_add_attribute(F, "ShaderType", llvm_type);
}

static void init_r600_target()
{
	gallivm_init_llvm_targets();
#if HAVE_LLVM < 0x0307
	LLVMInitializeR600TargetInfo();
	LLVMInitializeR600Target();
	LLVMInitializeR600TargetMC();
	LLVMInitializeR600AsmPrinter();
#else
	LLVMInitializeAMDGPUTargetInfo();
	LLVMInitializeAMDGPUTarget();
	LLVMInitializeAMDGPUTargetMC();
	LLVMInitializeAMDGPUAsmPrinter();

#endif
}

static once_flag init_r600_target_once_flag = ONCE_FLAG_INIT;

LLVMTargetRef radeon_llvm_get_r600_target(const char *triple)
{
	LLVMTargetRef target = NULL;
	char *err_message = NULL;

	call_once(&init_r600_target_once_flag, init_r600_target);

	if (LLVMGetTargetFromTriple(triple, &target, &err_message)) {
		fprintf(stderr, "Cannot find target for triple %s ", triple);
		if (err_message) {
			fprintf(stderr, "%s\n", err_message);
		}
		LLVMDisposeMessage(err_message);
		return NULL;
	}
	return target;
}

struct radeon_llvm_diagnostics {
	struct pipe_debug_callback *debug;
	unsigned retval;
};

static void radeonDiagnosticHandler(LLVMDiagnosticInfoRef di, void *context)
{
	struct radeon_llvm_diagnostics *diag = (struct radeon_llvm_diagnostics *)context;
	LLVMDiagnosticSeverity severity = LLVMGetDiagInfoSeverity(di);
	char *description = LLVMGetDiagInfoDescription(di);
	const char *severity_str = NULL;

	switch (severity) {
	case LLVMDSError:
		severity_str = "error";
		break;
	case LLVMDSWarning:
		severity_str = "warning";
		break;
	case LLVMDSRemark:
		severity_str = "remark";
		break;
	case LLVMDSNote:
		severity_str = "note";
		break;
	default:
		severity_str = "unknown";
	}

	pipe_debug_message(diag->debug, SHADER_INFO,
			   "LLVM diagnostic (%s): %s", severity_str, description);

	if (severity == LLVMDSError) {
		diag->retval = 1;
		fprintf(stderr,"LLVM triggered Diagnostic Handler: %s\n", description);
	}

	LLVMDisposeMessage(description);
}

/**
 * Compile an LLVM module to machine code.
 *
 * @returns 0 for success, 1 for failure
 */
unsigned radeon_llvm_compile(LLVMModuleRef M, struct radeon_shader_binary *binary,
			     const char *gpu_family,
			     LLVMTargetMachineRef tm,
			     struct pipe_debug_callback *debug)
{
	struct radeon_llvm_diagnostics diag;
	char cpu[CPU_STRING_LEN];
	char fs[FS_STRING_LEN];
	char *err;
	bool dispose_tm = false;
	LLVMContextRef llvm_ctx;
	LLVMMemoryBufferRef out_buffer;
	unsigned buffer_size;
	const char *buffer_data;
	char triple[TRIPLE_STRING_LEN];
	LLVMBool mem_err;

	diag.debug = debug;
	diag.retval = 0;

	if (!tm) {
		strncpy(triple, "r600--", TRIPLE_STRING_LEN);
		LLVMTargetRef target = radeon_llvm_get_r600_target(triple);
		if (!target) {
			return 1;
		}
		strncpy(cpu, gpu_family, CPU_STRING_LEN);
		memset(fs, 0, sizeof(fs));
		strncpy(fs, "+DumpCode", FS_STRING_LEN);
		tm = LLVMCreateTargetMachine(target, triple, cpu, fs,
				  LLVMCodeGenLevelDefault, LLVMRelocDefault,
						  LLVMCodeModelDefault);
		dispose_tm = true;
	}

	/* Setup Diagnostic Handler*/
	llvm_ctx = LLVMGetModuleContext(M);

	LLVMContextSetDiagnosticHandler(llvm_ctx, radeonDiagnosticHandler, &diag);

	/* Compile IR*/
	mem_err = LLVMTargetMachineEmitToMemoryBuffer(tm, M, LLVMObjectFile, &err,
								 &out_buffer);

	/* Process Errors/Warnings */
	if (mem_err) {
		fprintf(stderr, "%s: %s", __FUNCTION__, err);
		pipe_debug_message(debug, SHADER_INFO,
				   "LLVM emit error: %s", err);
		FREE(err);
		diag.retval = 1;
		goto out;
	}

	/* Extract Shader Code*/
	buffer_size = LLVMGetBufferSize(out_buffer);
	buffer_data = LLVMGetBufferStart(out_buffer);

	radeon_elf_read(buffer_data, buffer_size, binary);

	/* Clean up */
	LLVMDisposeMemoryBuffer(out_buffer);

out:
	if (dispose_tm) {
		LLVMDisposeTargetMachine(tm);
	}
	if (diag.retval != 0)
		pipe_debug_message(debug, SHADER_INFO, "LLVM compile failed");
	return diag.retval;
}
