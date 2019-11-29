%{
/*
 * Copyright © 2018 Intel Corporation
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
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "i965_asm.h"

#define YYLTYPE YYLTYPE
typedef struct YYLTYPE
{
	int first_line;
	int first_column;
	int last_line;
	int last_column;
} YYLTYPE;

enum message_level {
	WARN,
	ERROR,
};

int yydebug = 1;

static void
message(enum message_level level, YYLTYPE *location,
	const char *fmt, ...)
{
	static const char *level_str[] = { "warning", "error" };
	va_list args;

	if (location)
		fprintf(stderr, "%s:%d:%d: %s: ", input_filename,
		        location->first_line,
		        location->first_column, level_str[level]);
	else
		fprintf(stderr, "%s:%s: ", input_filename, level_str[level]);

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
}

#define warn(flag, l, fmt, ...) 				 \
	do {							 \
		if (warning_flags & WARN_ ## flag)		 \
			message(WARN, l, fmt, ## __VA_ARGS__);	 \
	} while (0)

#define error(l, fmt, ...)				   	 \
	do {						   	 \
		message(ERROR, l, fmt, ## __VA_ARGS__);	   	 \
	} while (0)

static bool
isPowerofTwo(unsigned int x)
{
	return x && (!(x & (x - 1)));
}

static struct brw_reg
set_direct_src_operand(struct brw_reg *reg, int type)
{
	return brw_reg(reg->file,
		       reg->nr,
		       reg->subnr,
		       0,		// negate
		       0,		// abs
		       type,
		       0,		// vstride
		       0,		// width
		       0,		// hstride
		       BRW_SWIZZLE_NOOP,
		       WRITEMASK_XYZW);
}

static void
i965_asm_unary_instruction(int opcode, struct brw_codegen *p,
		           struct brw_reg dest, struct brw_reg src0)
{
	switch (opcode) {
	case BRW_OPCODE_BFREV:
		brw_BFREV(p, dest, src0);
		break;
	case BRW_OPCODE_CBIT:
		brw_CBIT(p, dest, src0);
		break;
	case BRW_OPCODE_F32TO16:
		brw_F32TO16(p, dest, src0);
		break;
	case BRW_OPCODE_F16TO32:
		brw_F16TO32(p, dest, src0);
		break;
	case BRW_OPCODE_MOV:
		brw_MOV(p, dest, src0);
		break;
	case BRW_OPCODE_FBL:
		brw_FBL(p, dest, src0);
		break;
	case BRW_OPCODE_FRC:
		brw_FRC(p, dest, src0);
		break;
	case BRW_OPCODE_FBH:
		brw_FBH(p, dest, src0);
		break;
	case BRW_OPCODE_NOT:
		brw_NOT(p, dest, src0);
		break;
	case BRW_OPCODE_RNDE:
		brw_RNDE(p, dest, src0);
		break;
	case BRW_OPCODE_RNDZ:
		brw_RNDZ(p, dest, src0);
		break;
	case BRW_OPCODE_RNDD:
		brw_RNDD(p, dest, src0);
		break;
	case BRW_OPCODE_LZD:
		brw_LZD(p, dest, src0);
		break;
	case BRW_OPCODE_DIM:
		brw_DIM(p, dest, src0);
		break;
	case BRW_OPCODE_RNDU:
		fprintf(stderr, "Opcode BRW_OPCODE_RNDU unhandled\n");
		break;
	default:
		fprintf(stderr, "Unsupported unary opcode\n");
	}
}

static void
i965_asm_binary_instruction(int opcode,
			    struct brw_codegen *p,
			    struct brw_reg dest,
			    struct brw_reg src0,
			    struct brw_reg src1)
{
	switch (opcode) {
	case BRW_OPCODE_ADDC:
		brw_ADDC(p, dest, src0, src1);
		break;
	case BRW_OPCODE_BFI1:
		brw_BFI1(p, dest, src0, src1);
		break;
	case BRW_OPCODE_DP2:
		brw_DP2(p, dest, src0, src1);
		break;
	case BRW_OPCODE_DP3:
		brw_DP3(p, dest, src0, src1);
		break;
	case BRW_OPCODE_DP4:
		brw_DP4(p, dest, src0, src1);
		break;
	case BRW_OPCODE_DPH:
		brw_DPH(p, dest, src0, src1);
		break;
	case BRW_OPCODE_LINE:
		brw_LINE(p, dest, src0, src1);
		break;
	case BRW_OPCODE_MAC:
		brw_MAC(p, dest, src0, src1);
		break;
	case BRW_OPCODE_MACH:
		brw_MACH(p, dest, src0, src1);
		break;
	case BRW_OPCODE_PLN:
		brw_PLN(p, dest, src0, src1);
		break;
	case BRW_OPCODE_ROL:
		brw_ROL(p, dest, src0, src1);
		break;
	case BRW_OPCODE_ROR:
		brw_ROR(p, dest, src0, src1);
		break;
	case BRW_OPCODE_SAD2:
		fprintf(stderr, "Opcode BRW_OPCODE_SAD2 unhandled\n");
		break;
	case BRW_OPCODE_SADA2:
		fprintf(stderr, "Opcode BRW_OPCODE_SADA2 unhandled\n");
		break;
	case BRW_OPCODE_SUBB:
		brw_SUBB(p, dest, src0, src1);
		break;
	case BRW_OPCODE_ADD:
		brw_ADD(p, dest, src0, src1);
		break;
	case BRW_OPCODE_CMP:
		/* Third parameter is conditional modifier
		 * which gets updated later
		 */
		brw_CMP(p, dest, 0, src0, src1);
		break;
	case BRW_OPCODE_AND:
		brw_AND(p, dest, src0, src1);
		break;
	case BRW_OPCODE_ASR:
		brw_ASR(p, dest, src0, src1);
		break;
	case BRW_OPCODE_AVG:
		brw_AVG(p, dest, src0, src1);
		break;
	case BRW_OPCODE_OR:
		brw_OR(p, dest, src0, src1);
		break;
	case BRW_OPCODE_SEL:
		brw_SEL(p, dest, src0, src1);
		break;
	case BRW_OPCODE_SHL:
		brw_SHL(p, dest, src0, src1);
		break;
	case BRW_OPCODE_SHR:
		brw_SHR(p, dest, src0, src1);
		break;
	case BRW_OPCODE_XOR:
		brw_XOR(p, dest, src0, src1);
		break;
	case BRW_OPCODE_MUL:
		brw_MUL(p, dest, src0, src1);
		break;
	default:
		fprintf(stderr, "Unsupported binary opcode\n");
	}
}

static void
i965_asm_ternary_instruction(int opcode,
			     struct brw_codegen *p,
			     struct brw_reg dest,
			     struct brw_reg src0,
			     struct brw_reg src1,
			     struct brw_reg src2)
{
	switch (opcode) {
	case BRW_OPCODE_MAD:
		brw_MAD(p, dest, src0, src1, src2);
		break;
	case BRW_OPCODE_CSEL:
		brw_CSEL(p, dest, src0, src1, src2);
		break;
	case BRW_OPCODE_LRP:
		brw_LRP(p, dest, src0, src1, src2);
		break;
	case BRW_OPCODE_BFE:
		brw_BFE(p, dest, src0, src1, src2);
		break;
	case BRW_OPCODE_BFI2:
		brw_BFI2(p, dest, src0, src1, src2);
		break;
	default:
		fprintf(stderr, "Unsupported ternary opcode\n");
	}
}

static void
i965_asm_set_instruction_options(struct brw_codegen *p,
				 struct options options)
{
	brw_inst_set_access_mode(p->devinfo, brw_last_inst,
			         options.access_mode);
	brw_inst_set_mask_control(p->devinfo, brw_last_inst,
				  options.mask_control);
	brw_inst_set_thread_control(p->devinfo, brw_last_inst,
				    options.thread_control);
	brw_inst_set_no_dd_check(p->devinfo, brw_last_inst,
			         options.no_dd_check);
	brw_inst_set_no_dd_clear(p->devinfo, brw_last_inst,
			         options.no_dd_clear);
	brw_inst_set_debug_control(p->devinfo, brw_last_inst,
			           options.debug_control);
	if (p->devinfo->gen >= 6)
		brw_inst_set_acc_wr_control(p->devinfo, brw_last_inst,
					    options.acc_wr_control);
	brw_inst_set_cmpt_control(p->devinfo, brw_last_inst,
				  options.compaction);
}

static void
i965_asm_set_dst_nr(struct brw_codegen *p,
	            struct brw_reg *reg,
	            struct options options)
{
	if (p->devinfo->gen <= 6) {
		if (reg->file == BRW_MESSAGE_REGISTER_FILE &&
		    options.qtr_ctrl == BRW_COMPRESSION_COMPRESSED &&
		    !options.is_compr)
			reg->nr |= BRW_MRF_COMPR4;
	}
}

%}

%locations

%start ROOT

%union {
	double number;
	int integer;
	unsigned long long int llint;
	struct brw_reg reg;
	struct brw_codegen *program;
	struct predicate predicate;
	struct condition condition;
	struct options options;
	brw_inst *instruction;
}

%token ABS
%token COLON
%token COMMA
%token DOT
%token LANGLE RANGLE
%token LCURLY RCURLY
%token LPAREN RPAREN
%token LSQUARE RSQUARE
%token PLUS MINUS
%token SEMICOLON

/* datatypes */
%token <integer> TYPE_B TYPE_UB
%token <integer> TYPE_W TYPE_UW
%token <integer> TYPE_D TYPE_UD
%token <integer> TYPE_Q TYPE_UQ
%token <integer> TYPE_V TYPE_UV
%token <integer> TYPE_F TYPE_HF
%token <integer> TYPE_DF TYPE_NF
%token <integer> TYPE_VF

/* opcodes */
%token <integer> ADD ADD3 ADDC AND ASR AVG
%token <integer> BFE BFI1 BFI2 BFB BFREV BRC BRD BREAK
%token <integer> CALL CALLA CASE CBIT CMP CMPN CONT CSEL
%token <integer> DIM DO DPAS DPASW DP2 DP3 DP4 DP4A DPH
%token <integer> ELSE ENDIF F16TO32 F32TO16 FBH FBL FORK FRC
%token <integer> GOTO
%token <integer> HALT
%token <integer> IF IFF ILLEGAL
%token <integer> JMPI JOIN
%token <integer> LINE LRP LZD
%token <integer> MAC MACH MAD MADM MOV MOVI MUL MREST MSAVE
%token <integer> NENOP NOP NOT
%token <integer> OR
%token <integer> PLN POP PUSH
%token <integer> RET RNDD RNDE RNDU RNDZ ROL ROR
%token <integer> SAD2 SADA2 SEL SEND SENDC SENDS SENDSC SHL SHR SMOV SUBB SYNC
%token <integer> WAIT WHILE
%token <integer> XOR

/* extended math functions */
%token <integer> COS EXP FDIV INV INVM INTDIV INTDIVMOD INTMOD LOG POW RSQ
%token <integer> RSQRTM SIN SINCOS SQRT

/* shared functions for send */
%token CONST CRE DATA DP_DATA_1 GATEWAY MATH PIXEL_INTERP READ RENDER SAMPLER
%token THREAD_SPAWNER URB VME WRITE DP_SAMPLER

/* Conditional modifiers */
%token <integer> EQUAL GREATER GREATER_EQUAL LESS LESS_EQUAL NOT_EQUAL
%token <integer> NOT_ZERO OVERFLOW UNORDERED ZERO

/* register Access Modes */
%token ALIGN1 ALIGN16

/* accumulator write control */
%token ACCWREN

/* compaction control */
%token CMPTCTRL

/* compression control */
%token COMPR COMPR4 SECHALF

/* mask control (WeCtrl) */
%token WECTRL

/* debug control */
%token BREAKPOINT

/* dependency control */
%token NODDCLR NODDCHK

/* end of thread */
%token EOT

/* mask control */
%token MASK_DISABLE;

/* predicate control */
%token <integer> ANYV ALLV ANY2H ALL2H ANY4H ALL4H ANY8H ALL8H ANY16H ALL16H
%token <integer> ANY32H ALL32H

/* round instructions */
%token <integer> ROUND_INCREMENT

/* staturation */
%token SATURATE

/* thread control */
%token ATOMIC SWITCH

/* quater control */
%token QTR_2Q QTR_3Q QTR_4Q QTR_2H QTR_2N QTR_3N QTR_4N QTR_5N
%token QTR_6N QTR_7N QTR_8N

/* channels */
%token <integer> X Y Z W

/* reg files */
%token GENREGFILE MSGREGFILE

/* vertical stride in register region */
%token VxH

/* register type */
%token <integer> GENREG MSGREG ADDRREG ACCREG FLAGREG NOTIFYREG STATEREG
%token <integer> CONTROLREG IPREG PERFORMANCEREG THREADREG CHANNELENABLEREG
%token <integer> MASKREG

%token <integer> INTEGER
%token <llint> LONG
%token NULL_TOKEN

%precedence SUBREGNUM
%left PLUS MINUS
%precedence DOT
%precedence EMPTYEXECSIZE
%precedence LPAREN

%type <integer> execsize simple_int exp
%type <llint> exp2

/* predicate control */
%type <integer> predctrl predstate
%type <predicate> predicate

/* conditional modifier */
%type <condition> cond_mod
%type <integer> condModifiers

/* instruction options  */
%type <options> instoptions instoption_list
%type <integer> instoption

/* writemask */
%type <integer> writemask_x writemask_y writemask_z writemask_w
%type <reg> writemask

/* dst operand */
%type <reg> dst dstoperand dstoperandex dstoperandex_typed dstreg dsttype
%type <reg> dstoperandex_ud_typed
%type <integer> dstregion

%type <integer> saturate relativelocation rellocation
%type <reg> relativelocation2

/* src operand */
%type <reg> directsrcoperand directsrcaccoperand indirectsrcoperand srcacc
%type <reg> srcarcoperandex srcaccimm srcarcoperandex_typed srctype srcimm
%type <reg> srcarcoperandex_ud_typed srcimmtype indirectgenreg indirectregion
%type <reg> immreg src reg32 payload directgenreg_list addrparam region
%type <reg> region_wh swizzle directgenreg directmsgreg indirectmsgreg

/* registers */
%type <reg> accreg addrreg channelenablereg controlreg flagreg ipreg
%type <reg> notifyreg nullreg performancereg threadcontrolreg statereg maskreg
%type <integer> subregnum

/* immediate values */
%type <llint> immval

/* instruction opcodes */
%type <integer> unaryopcodes binaryopcodes binaryaccopcodes ternaryopcodes
%type <integer> sendop
%type <instruction> sendopcode

%type <integer> negate abs chansel math_function sharedfunction

%code {

static void
add_instruction_option(struct options *options, int option)
{
	switch (option) {
	case ALIGN1:
		options->access_mode = BRW_ALIGN_1;
		break;
	case ALIGN16:
		options->access_mode = BRW_ALIGN_16;
		break;
	case SECHALF:
		options->qtr_ctrl |= BRW_COMPRESSION_2NDHALF;
		break;
	case COMPR:
		options->qtr_ctrl |= BRW_COMPRESSION_COMPRESSED;
		options->is_compr = true;
		break;
	case COMPR4:
		options->qtr_ctrl |= BRW_COMPRESSION_COMPRESSED;
		break;
	case SWITCH:
		options->thread_control |= BRW_THREAD_SWITCH;
		break;
	case ATOMIC:
		options->thread_control |= BRW_THREAD_ATOMIC;
		break;
	case NODDCHK:
		options->no_dd_check = true;
		break;
	case NODDCLR:
		options->no_dd_clear = BRW_DEPENDENCY_NOTCLEARED;
		break;
	case MASK_DISABLE:
		options->mask_control |= BRW_MASK_DISABLE;
		break;
	case BREAKPOINT:
		options->debug_control = BRW_DEBUG_BREAKPOINT;
		break;
	case WECTRL:
		options->mask_control |= BRW_WE_ALL;
		break;
	case CMPTCTRL:
		options->compaction = true;
		break;
	case ACCWREN:
		options->acc_wr_control = true;
		break;
	case EOT:
		options->end_of_thread = true;
		break;
	/* TODO : Figure out how to set instruction group and get rid of
	 * code below
	 */
	case QTR_2Q:
		options->qtr_ctrl = BRW_COMPRESSION_2NDHALF;
		break;
	case QTR_3Q:
		options->qtr_ctrl = BRW_COMPRESSION_COMPRESSED;
		break;
	case QTR_4Q:
		options->qtr_ctrl = 3;
		break;
	case QTR_2H:
		options->qtr_ctrl = BRW_COMPRESSION_COMPRESSED;
		break;
	case QTR_2N:
		options->qtr_ctrl = BRW_COMPRESSION_NONE;
		options->nib_ctrl = true;
		break;
	case QTR_3N:
		options->qtr_ctrl = BRW_COMPRESSION_2NDHALF;
		break;
	case QTR_4N:
		options->qtr_ctrl = BRW_COMPRESSION_2NDHALF;
		options->nib_ctrl = true;
		break;
	case QTR_5N:
		options->qtr_ctrl = BRW_COMPRESSION_COMPRESSED;
		break;
	case QTR_6N:
		options->qtr_ctrl = BRW_COMPRESSION_COMPRESSED;
		options->nib_ctrl = true;
		break;
	case QTR_7N:
		options->qtr_ctrl = 3;
		break;
	case QTR_8N:
		options->qtr_ctrl = 3;
		options->nib_ctrl = true;
		break;
	}
}
}
%%

ROOT:
	instrseq
	;

instrseq:
	instrseq instruction SEMICOLON
	| instrseq relocatableinstruction SEMICOLON
	| instruction SEMICOLON
	| relocatableinstruction SEMICOLON
	;

/* Instruction Group */
instruction:
	unaryinstruction
	| binaryinstruction
	| binaryaccinstruction
	| mathinstruction
	| nopinstruction
	| syncinstruction
	| ternaryinstruction
	| sendinstruction
	;

relocatableinstruction:
	jumpinstruction
	| branchinstruction
	| breakinstruction
	| loopinstruction
	;

/* Unary instruction */
unaryinstruction:
	predicate unaryopcodes saturate cond_mod execsize dst srcaccimm	instoptions
	{
		i965_asm_set_dst_nr(p, &$6, $8);
		brw_set_default_access_mode(p, $8.access_mode);
		i965_asm_unary_instruction($2, p, $6, $7);
		brw_pop_insn_state(p);
		i965_asm_set_instruction_options(p, $8);
		brw_inst_set_cond_modifier(p->devinfo, brw_last_inst,
					   $4.cond_modifier);

		if (p->devinfo->gen >= 7) {
			if ($2 != BRW_OPCODE_DIM) {
				brw_inst_set_flag_reg_nr(p->devinfo,
							 brw_last_inst,
							 $4.flag_reg_nr);
				brw_inst_set_flag_subreg_nr(p->devinfo,
							    brw_last_inst,
							    $4.flag_subreg_nr);
			}
		}

		if ($7.file != BRW_IMMEDIATE_VALUE) {
			brw_inst_set_src0_vstride(p->devinfo, brw_last_inst,
						  $7.vstride);
		}
		brw_inst_set_saturate(p->devinfo, brw_last_inst, $3);
		brw_inst_set_exec_size(p->devinfo, brw_last_inst, $5);
		// TODO: set instruction group instead of qtr and nib ctrl
		brw_inst_set_qtr_control(p->devinfo, brw_last_inst,
				         $8.qtr_ctrl);

		if (p->devinfo->gen >= 7)
			brw_inst_set_nib_control(p->devinfo, brw_last_inst,
					         $8.nib_ctrl);
	}
	;

unaryopcodes:
	BFREV
	| CBIT
	| DIM
	| F16TO32
	| F32TO16
	| FBH
	| FBL
	| FRC
	| LZD
	| MOV
	| NOT
	| RNDD
	| RNDE
	| RNDU
	| RNDZ
	;

/* Binary instruction */
binaryinstruction:
	predicate binaryopcodes saturate cond_mod execsize dst srcimm srcimm instoptions
	{
		i965_asm_set_dst_nr(p, &$6, $9);
		brw_set_default_access_mode(p, $9.access_mode);
		i965_asm_binary_instruction($2, p, $6, $7, $8);
		i965_asm_set_instruction_options(p, $9);
		brw_inst_set_cond_modifier(p->devinfo, brw_last_inst,
					   $4.cond_modifier);

		if (p->devinfo->gen >= 7) {
			brw_inst_set_flag_reg_nr(p->devinfo, brw_last_inst,
					         $4.flag_reg_nr);
			brw_inst_set_flag_subreg_nr(p->devinfo, brw_last_inst,
						    $4.flag_subreg_nr);
		}

		brw_inst_set_saturate(p->devinfo, brw_last_inst, $3);
		brw_inst_set_exec_size(p->devinfo, brw_last_inst, $5);
		// TODO: set instruction group instead of qtr and nib ctrl
		brw_inst_set_qtr_control(p->devinfo, brw_last_inst,
				         $9.qtr_ctrl);

		if (p->devinfo->gen >= 7)
			brw_inst_set_nib_control(p->devinfo, brw_last_inst,
					         $9.nib_ctrl);

		brw_pop_insn_state(p);
	}
	;

binaryopcodes:
	ADDC
	| BFI1
	| DP2
	| DP3
	| DP4
	| DPH
	| LINE
	| MAC
	| MACH
	| MUL
	| PLN
	| ROL
	| ROR
	| SAD2
	| SADA2
	| SUBB
	;

/* Binary acc instruction */
binaryaccinstruction:
	predicate binaryaccopcodes saturate cond_mod execsize dst srcacc srcimm instoptions
	{
		i965_asm_set_dst_nr(p, &$6, $9);
		brw_set_default_access_mode(p, $9.access_mode);
		i965_asm_binary_instruction($2, p, $6, $7, $8);
		brw_pop_insn_state(p);
		i965_asm_set_instruction_options(p, $9);
		brw_inst_set_cond_modifier(p->devinfo, brw_last_inst,
					   $4.cond_modifier);

		if (p->devinfo->gen >= 7) {
			if (!brw_inst_flag_reg_nr(p->devinfo, brw_last_inst)) {
				brw_inst_set_flag_reg_nr(p->devinfo,
							 brw_last_inst,
						         $4.flag_reg_nr);
				brw_inst_set_flag_subreg_nr(p->devinfo,
							    brw_last_inst,
							    $4.flag_subreg_nr);
			}
		}

		brw_inst_set_saturate(p->devinfo, brw_last_inst, $3);
		brw_inst_set_exec_size(p->devinfo, brw_last_inst, $5);
		// TODO: set instruction group instead of qtr and nib ctrl
		brw_inst_set_qtr_control(p->devinfo, brw_last_inst,
				         $9.qtr_ctrl);

		if (p->devinfo->gen >= 7)
			brw_inst_set_nib_control(p->devinfo, brw_last_inst,
					         $9.nib_ctrl);

	}
	;

binaryaccopcodes:
	ADD
	| AND
	| ASR
	| AVG
	| CMP
	| CMPN
	| OR
	| SEL
	| SHL
	| SHR
	| XOR
	;

/* Math instruction */
mathinstruction:
	predicate MATH saturate math_function execsize dst src srcimm instoptions
	{
		brw_set_default_access_mode(p, $9.access_mode);
		gen6_math(p, $6, $4, $7, $8);
		i965_asm_set_instruction_options(p, $9);
		brw_inst_set_exec_size(p->devinfo, brw_last_inst, $5);
		brw_inst_set_saturate(p->devinfo, brw_last_inst, $3);
		// TODO: set instruction group instead
		brw_inst_set_qtr_control(p->devinfo, brw_last_inst,
				         $9.qtr_ctrl);

		if (p->devinfo->gen >= 7)
			brw_inst_set_nib_control(p->devinfo, brw_last_inst,
					         $9.nib_ctrl);

		brw_pop_insn_state(p);
	}
	;

math_function:
	COS
	| EXP
	| FDIV
	| INV
	| INVM
	| INTDIV
	| INTDIVMOD
	| INTMOD
	| LOG
	| POW
	| RSQ
	| RSQRTM
	| SIN
	| SQRT
	| SINCOS
	;

/* NOP instruction */
nopinstruction:
	NOP
	{
		brw_NOP(p);
	}
	;

/* Ternary operand instruction */
ternaryinstruction:
	predicate ternaryopcodes saturate cond_mod execsize dst src src src instoptions
	{
		brw_set_default_access_mode(p, $10.access_mode);
		i965_asm_ternary_instruction($2, p, $6, $7, $8, $9);
		brw_pop_insn_state(p);
		i965_asm_set_instruction_options(p, $10);
		brw_inst_set_cond_modifier(p->devinfo, brw_last_inst,
					   $4.cond_modifier);

		if (p->devinfo->gen >= 7) {
			brw_inst_set_3src_a16_flag_reg_nr(p->devinfo, brw_last_inst,
					         $4.flag_reg_nr);
			brw_inst_set_3src_a16_flag_subreg_nr(p->devinfo, brw_last_inst,
						    $4.flag_subreg_nr);
		}

		brw_inst_set_saturate(p->devinfo, brw_last_inst, $3);
		brw_inst_set_exec_size(p->devinfo, brw_last_inst, $5);
		// TODO: set instruction group instead of qtr and nib ctrl
		brw_inst_set_qtr_control(p->devinfo, brw_last_inst,
				         $10.qtr_ctrl);

		if (p->devinfo->gen >= 7)
			brw_inst_set_nib_control(p->devinfo, brw_last_inst,
					         $10.nib_ctrl);
	}
	;

ternaryopcodes:
	CSEL
	| BFE
	| BFI2
	| LRP
	| MAD
	;

/* Sync instruction */
syncinstruction:
	WAIT execsize src instoptions
	{
		brw_next_insn(p, $1);
		i965_asm_set_instruction_options(p, $4);
		brw_inst_set_exec_size(p->devinfo, brw_last_inst, $2);
		brw_set_default_access_mode(p, $4.access_mode);
		struct brw_reg src = brw_notification_reg();
		brw_set_dest(p, brw_last_inst, src);
		brw_set_src0(p, brw_last_inst, src);
		brw_set_src1(p, brw_last_inst, brw_null_reg());
		brw_inst_set_mask_control(p->devinfo, brw_last_inst, BRW_MASK_DISABLE);
	}
	;

/* Send instruction */
sendinstruction:
	predicate sendopcode execsize dst payload exp2 sharedfunction instoptions
	{
		i965_asm_set_instruction_options(p, $8);
		brw_inst_set_exec_size(p->devinfo, brw_last_inst, $3);
		brw_set_dest(p, brw_last_inst, $4);
		brw_set_src0(p, brw_last_inst, $5);
		brw_inst_set_bits(brw_last_inst, 127, 96, $6);
		brw_inst_set_src1_file_type(p->devinfo, brw_last_inst,
				            BRW_IMMEDIATE_VALUE,
					    BRW_REGISTER_TYPE_UD);
		brw_inst_set_sfid(p->devinfo, brw_last_inst, $7);
		brw_inst_set_eot(p->devinfo, brw_last_inst, $8.end_of_thread);
		// TODO: set instruction group instead of qtr and nib ctrl
		brw_inst_set_qtr_control(p->devinfo, brw_last_inst,
				         $8.qtr_ctrl);

		if (p->devinfo->gen >= 7)
			brw_inst_set_nib_control(p->devinfo, brw_last_inst,
					         $8.nib_ctrl);

		brw_pop_insn_state(p);
	}
	| predicate sendopcode execsize exp dst payload exp2 sharedfunction instoptions
	{
		i965_asm_set_instruction_options(p, $9);
		brw_inst_set_exec_size(p->devinfo, brw_last_inst, $3);
		brw_inst_set_base_mrf(p->devinfo, brw_last_inst, $4);
		brw_set_dest(p, brw_last_inst, $5);
		brw_set_src0(p, brw_last_inst, $6);
		brw_inst_set_bits(brw_last_inst, 127, 96, $7);
		brw_inst_set_src1_file_type(p->devinfo, brw_last_inst,
				            BRW_IMMEDIATE_VALUE,
					    BRW_REGISTER_TYPE_UD);
		brw_inst_set_sfid(p->devinfo, brw_last_inst, $8);
		brw_inst_set_eot(p->devinfo, brw_last_inst, $9.end_of_thread);
		// TODO: set instruction group instead of qtr and nib ctrl
		brw_inst_set_qtr_control(p->devinfo, brw_last_inst,
				         $9.qtr_ctrl);

		if (p->devinfo->gen >= 7)
			brw_inst_set_nib_control(p->devinfo, brw_last_inst,
					         $9.nib_ctrl);

		brw_pop_insn_state(p);
	}
	| predicate sendopcode execsize dst payload payload exp2 sharedfunction instoptions
	{
		i965_asm_set_instruction_options(p, $9);
		brw_inst_set_exec_size(p->devinfo, brw_last_inst, $3);
		brw_set_dest(p, brw_last_inst, $4);
		brw_set_src0(p, brw_last_inst, $5);
		brw_inst_set_bits(brw_last_inst, 127, 96, $7);
		brw_inst_set_sfid(p->devinfo, brw_last_inst, $8);
		brw_inst_set_eot(p->devinfo, brw_last_inst, $9.end_of_thread);
		// TODO: set instruction group instead of qtr and nib ctrl
		brw_inst_set_qtr_control(p->devinfo, brw_last_inst,
				         $9.qtr_ctrl);

		if (p->devinfo->gen >= 7)
			brw_inst_set_nib_control(p->devinfo, brw_last_inst,
					         $9.nib_ctrl);

		brw_pop_insn_state(p);
	}
	| predicate SENDS execsize dst payload payload exp2 exp2 sharedfunction instoptions
	{
		brw_next_insn(p, $2);
		i965_asm_set_instruction_options(p, $10);
		brw_inst_set_exec_size(p->devinfo, brw_last_inst, $3);
		brw_set_dest(p, brw_last_inst, $4);
		brw_set_src0(p, brw_last_inst, $5);
		brw_set_src1(p, brw_last_inst, $6);

		if (brw_inst_send_sel_reg32_ex_desc(p->devinfo, brw_last_inst)) {
			brw_inst_set_send_ex_desc_ia_subreg_nr(p->devinfo, brw_last_inst, $5.subnr);
		} else {
			brw_inst_set_sends_ex_desc(p->devinfo, brw_last_inst, $8);
		}

		brw_inst_set_bits(brw_last_inst, 127, 96, $7);
		brw_inst_set_sfid(p->devinfo, brw_last_inst, $9);
		brw_inst_set_eot(p->devinfo, brw_last_inst, $10.end_of_thread);
		// TODO: set instruction group instead of qtr and nib ctrl
		brw_inst_set_qtr_control(p->devinfo, brw_last_inst,
				         $10.qtr_ctrl);

		if (p->devinfo->gen >= 7)
			brw_inst_set_nib_control(p->devinfo, brw_last_inst,
					         $10.nib_ctrl);

		brw_pop_insn_state(p);
	}
	| predicate SENDS execsize dst payload payload src exp2 sharedfunction instoptions
	{
		brw_next_insn(p, $2);
		i965_asm_set_instruction_options(p, $10);
		brw_inst_set_exec_size(p->devinfo, brw_last_inst, $3);
		brw_set_dest(p, brw_last_inst, $4);
		brw_set_src0(p, brw_last_inst, $5);
		brw_set_src1(p, brw_last_inst, $6);

		brw_inst_set_send_sel_reg32_desc(p->devinfo, brw_last_inst, 1);
		brw_inst_set_sends_ex_desc(p->devinfo, brw_last_inst, $8);

		brw_inst_set_sfid(p->devinfo, brw_last_inst, $9);
		brw_inst_set_eot(p->devinfo, brw_last_inst, $10.end_of_thread);
		// TODO: set instruction group instead of qtr and nib ctrl
		brw_inst_set_qtr_control(p->devinfo, brw_last_inst,
				         $10.qtr_ctrl);

		if (p->devinfo->gen >= 7)
			brw_inst_set_nib_control(p->devinfo, brw_last_inst,
					         $10.nib_ctrl);

		brw_pop_insn_state(p);
	}
	;

sendop:
	SEND
	| SENDC
	;

sendopcode:
	sendop   { $$ = brw_next_insn(p, $1); }
	;

sharedfunction:
	NULL_TOKEN 	        { $$ = BRW_SFID_NULL; }
	| MATH 		        { $$ = BRW_SFID_MATH; }
	| GATEWAY 	        { $$ = BRW_SFID_MESSAGE_GATEWAY; }
	| READ 		        { $$ = BRW_SFID_DATAPORT_READ; }
	| WRITE 	        { $$ = BRW_SFID_DATAPORT_WRITE; }
	| URB 		        { $$ = BRW_SFID_URB; }
	| THREAD_SPAWNER 	{ $$ = BRW_SFID_THREAD_SPAWNER; }
	| VME 		        { $$ = BRW_SFID_VME; }
	| RENDER 	        { $$ = GEN6_SFID_DATAPORT_RENDER_CACHE; }
	| CONST 	        { $$ = GEN6_SFID_DATAPORT_CONSTANT_CACHE; }
	| DATA 		        { $$ = GEN7_SFID_DATAPORT_DATA_CACHE; }
	| PIXEL_INTERP 	        { $$ = GEN7_SFID_PIXEL_INTERPOLATOR; }
	| DP_DATA_1 	        { $$ = HSW_SFID_DATAPORT_DATA_CACHE_1; }
	| CRE 		        { $$ = HSW_SFID_CRE; }
	| SAMPLER	        { $$ = BRW_SFID_SAMPLER; }
	| DP_SAMPLER	        { $$ = GEN6_SFID_DATAPORT_SAMPLER_CACHE; }
	;

exp2:
	LONG 		{ $$ = $1; }
	| MINUS LONG 	{ $$ = -$2; }
	;

/* Jump instruction */
jumpinstruction:
	predicate JMPI execsize relativelocation2 instoptions
	{
		brw_next_insn(p, $2);
		i965_asm_set_instruction_options(p, $5);
		brw_inst_set_exec_size(p->devinfo, brw_last_inst, $3);
		brw_set_dest(p, brw_last_inst, brw_ip_reg());
		brw_set_src0(p, brw_last_inst, brw_ip_reg());
		brw_set_src1(p, brw_last_inst, $4);
		brw_inst_set_pred_control(p->devinfo, brw_last_inst,
					  brw_inst_pred_control(p->devinfo,
								brw_last_inst));
		brw_pop_insn_state(p);
	}
	;

/* branch instruction */
branchinstruction:
	predicate ENDIF execsize relativelocation instoptions
	{
		brw_next_insn(p, $2);
		i965_asm_set_instruction_options(p, $5);
		brw_inst_set_exec_size(p->devinfo, brw_last_inst, $3);

		if (p->devinfo->gen < 6) {
			brw_set_dest(p, brw_last_inst, retype(brw_null_reg(),
				     BRW_REGISTER_TYPE_D));
			brw_set_src0(p, brw_last_inst, retype(brw_null_reg(),
				     BRW_REGISTER_TYPE_D));
			brw_set_src1(p, brw_last_inst, brw_imm_d(0x0));
			brw_inst_set_gen4_pop_count(p->devinfo, brw_last_inst,
						    $4);
		} else if (p->devinfo->gen == 6) {
			brw_set_dest(p, brw_last_inst, brw_imm_w(0x0));
			brw_inst_set_gen6_jump_count(p->devinfo, brw_last_inst,
						     $4);
			brw_set_src0(p, brw_last_inst, retype(brw_null_reg(),
				     BRW_REGISTER_TYPE_D));
			brw_set_src1(p, brw_last_inst, retype(brw_null_reg(),
				     BRW_REGISTER_TYPE_D));
		} else if (p->devinfo->gen == 7) {
			brw_set_dest(p, brw_last_inst, retype(brw_null_reg(),
				     BRW_REGISTER_TYPE_D));
			brw_set_src0(p, brw_last_inst, retype(brw_null_reg(),
				     BRW_REGISTER_TYPE_D));
			brw_set_src1(p, brw_last_inst, brw_imm_w(0x0));
			brw_inst_set_jip(p->devinfo, brw_last_inst, $4);
		} else {
			brw_set_src0(p, brw_last_inst, brw_imm_d($4));
		}

		if (p->devinfo->gen < 6)
			brw_inst_set_thread_control(p->devinfo, brw_last_inst,
						    BRW_THREAD_SWITCH);
		brw_pop_insn_state(p);
	}
	| ELSE execsize relativelocation rellocation instoptions
	{
		brw_next_insn(p, $1);
		i965_asm_set_instruction_options(p, $5);
		brw_inst_set_exec_size(p->devinfo, brw_last_inst, $2);

		if (p->devinfo->gen < 6) {
			brw_set_dest(p, brw_last_inst, brw_ip_reg());
			brw_set_src0(p, brw_last_inst, brw_ip_reg());
			brw_set_src1(p, brw_last_inst, brw_imm_d(0x0));
			brw_inst_set_gen4_jump_count(p->devinfo, brw_last_inst,
						     $3);
			brw_inst_set_gen4_pop_count(p->devinfo, brw_last_inst,
						    $4);
		} else if (p->devinfo->gen == 6) {
			brw_set_dest(p, brw_last_inst, brw_imm_w(0x0));
			brw_inst_set_gen6_jump_count(p->devinfo, brw_last_inst,
						     $3);
			brw_set_src0(p, brw_last_inst, retype(brw_null_reg(),
				     BRW_REGISTER_TYPE_D));
			brw_set_src1(p, brw_last_inst, retype(brw_null_reg(),
				     BRW_REGISTER_TYPE_D));
		} else if (p->devinfo->gen == 7) {
			brw_set_dest(p, brw_last_inst, retype(brw_null_reg(),
				     BRW_REGISTER_TYPE_D));
			brw_set_src0(p, brw_last_inst, retype(brw_null_reg(),
				     BRW_REGISTER_TYPE_D));
			brw_set_src1(p, brw_last_inst, brw_imm_w($3));
			brw_inst_set_jip(p->devinfo, brw_last_inst, $3);
			brw_inst_set_uip(p->devinfo, brw_last_inst, $4);
		} else {
			brw_set_dest(p, brw_last_inst, retype(brw_null_reg(),
				     BRW_REGISTER_TYPE_D));
			brw_set_src0(p, brw_last_inst, brw_imm_d($3));
			brw_inst_set_jip(p->devinfo, brw_last_inst, $3);
			brw_inst_set_uip(p->devinfo, brw_last_inst, $4);
		}

		if (!p->single_program_flow && p->devinfo->gen < 6)
			brw_inst_set_thread_control(p->devinfo, brw_last_inst,
						    BRW_THREAD_SWITCH);
	}
	| predicate IF execsize relativelocation rellocation instoptions
	{
		brw_next_insn(p, $2);
		i965_asm_set_instruction_options(p, $6);
		brw_inst_set_exec_size(p->devinfo, brw_last_inst, $3);

		if (p->devinfo->gen < 6) {
			brw_set_dest(p, brw_last_inst, brw_ip_reg());
			brw_set_src0(p, brw_last_inst, brw_ip_reg());
			brw_set_src1(p, brw_last_inst, brw_imm_d(0x0));
			brw_inst_set_gen4_jump_count(p->devinfo, brw_last_inst,
						     $4);
			brw_inst_set_gen4_pop_count(p->devinfo, brw_last_inst,
						    $5);
		} else if (p->devinfo->gen == 6) {
			brw_set_dest(p, brw_last_inst, brw_imm_w(0x0));
			brw_inst_set_gen6_jump_count(p->devinfo, brw_last_inst,
						     $4);
			brw_set_src0(p, brw_last_inst,
				     vec1(retype(brw_null_reg(),
				     BRW_REGISTER_TYPE_D)));
			brw_set_src1(p, brw_last_inst,
				     vec1(retype(brw_null_reg(),
				     BRW_REGISTER_TYPE_D)));
		} else if (p->devinfo->gen == 7) {
			brw_set_dest(p, brw_last_inst,
				     vec1(retype(brw_null_reg(),
				     BRW_REGISTER_TYPE_D)));
			brw_set_src0(p, brw_last_inst,
				     vec1(retype(brw_null_reg(),
				     BRW_REGISTER_TYPE_D)));
			brw_set_src1(p, brw_last_inst, brw_imm_w($4));
			brw_inst_set_jip(p->devinfo, brw_last_inst, $4);
			brw_inst_set_uip(p->devinfo, brw_last_inst, $5);
		} else {
			brw_set_dest(p, brw_last_inst,
				     vec1(retype(brw_null_reg(),
				     BRW_REGISTER_TYPE_D)));
			brw_set_src0(p, brw_last_inst, brw_imm_d($4));
			brw_inst_set_jip(p->devinfo, brw_last_inst, $4);
			brw_inst_set_uip(p->devinfo, brw_last_inst, $5);
		}

		if (!p->single_program_flow && p->devinfo->gen < 6)
			brw_inst_set_thread_control(p->devinfo, brw_last_inst,
						    BRW_THREAD_SWITCH);

		brw_pop_insn_state(p);
	}
	| predicate IFF execsize relativelocation instoptions
	{
		brw_next_insn(p, $2);
		i965_asm_set_instruction_options(p, $5);
		brw_inst_set_exec_size(p->devinfo, brw_last_inst, $3);

		if (p->devinfo->gen < 6) {
			brw_set_dest(p, brw_last_inst, brw_ip_reg());
			brw_set_src0(p, brw_last_inst, brw_ip_reg());
			brw_inst_set_gen4_jump_count(p->devinfo, brw_last_inst,
						     $4);
			brw_set_src1(p, brw_last_inst, brw_imm_d($4));
		} else if (p->devinfo->gen == 6) {
			brw_set_dest(p, brw_last_inst, brw_imm_w($4));
			brw_inst_set_gen6_jump_count(p->devinfo, brw_last_inst,
						     $4);
			brw_set_src0(p, brw_last_inst,
				     vec1(retype(brw_null_reg(),
				     BRW_REGISTER_TYPE_D)));
			brw_set_src1(p, brw_last_inst,
				     vec1(retype(brw_null_reg(),
				     BRW_REGISTER_TYPE_D)));
		} else if (p->devinfo->gen == 7) {
			brw_set_dest(p, brw_last_inst,
				     vec1(retype(brw_null_reg(),
				     BRW_REGISTER_TYPE_D)));
			brw_set_src0(p, brw_last_inst,
				     vec1(retype(brw_null_reg(),
				     BRW_REGISTER_TYPE_D)));
			brw_set_src1(p, brw_last_inst, brw_imm_w($4));
			brw_inst_set_jip(p->devinfo, brw_last_inst, $4);
		} else {
			brw_set_dest(p, brw_last_inst,
				     vec1(retype(brw_null_reg(),
				     BRW_REGISTER_TYPE_D)));
			brw_set_src0(p, brw_last_inst, brw_imm_d($4));
			brw_inst_set_jip(p->devinfo, brw_last_inst, $4);
		}

		if (!p->single_program_flow && p->devinfo->gen < 6)
			brw_inst_set_thread_control(p->devinfo, brw_last_inst,
						    BRW_THREAD_SWITCH);

		brw_pop_insn_state(p);
	}
	;

/* break instruction */
breakinstruction:
	predicate BREAK execsize relativelocation relativelocation instoptions
	{
		brw_next_insn(p, $2);
		i965_asm_set_instruction_options(p, $6);
		brw_inst_set_exec_size(p->devinfo, brw_last_inst, $3);

		if (p->devinfo->gen >= 8) {
			brw_set_dest(p, brw_last_inst, retype(brw_null_reg(),
				     BRW_REGISTER_TYPE_D));
			brw_set_src0(p, brw_last_inst, brw_imm_d($4));
			brw_inst_set_uip(p->devinfo, brw_last_inst, $5);
		} else if (p->devinfo->gen >= 6) {
			brw_set_dest(p, brw_last_inst, retype(brw_null_reg(),
				     BRW_REGISTER_TYPE_D));
			brw_set_src0(p, brw_last_inst, retype(brw_null_reg(),
				     BRW_REGISTER_TYPE_D));
			brw_set_src1(p, brw_last_inst, brw_imm_d(0x0));
			brw_inst_set_jip(p->devinfo, brw_last_inst, $4);
			brw_inst_set_uip(p->devinfo, brw_last_inst, $5);
		} else {
			brw_set_dest(p, brw_last_inst, brw_ip_reg());
			brw_set_src0(p, brw_last_inst, brw_ip_reg());
			brw_set_src1(p, brw_last_inst, brw_imm_d(0x0));
			brw_inst_set_gen4_jump_count(p->devinfo, brw_last_inst,
						     $4);
			brw_inst_set_gen4_pop_count(p->devinfo, brw_last_inst,
						    $5);
		}

		brw_pop_insn_state(p);
	}
	| predicate HALT execsize relativelocation relativelocation instoptions
	{
		brw_next_insn(p, $2);
		i965_asm_set_instruction_options(p, $6);
		brw_inst_set_exec_size(p->devinfo, brw_last_inst, $3);
		brw_set_dest(p, brw_last_inst, retype(brw_null_reg(),
			     BRW_REGISTER_TYPE_D));

		if (p->devinfo->gen >= 8) {
			brw_set_src0(p, brw_last_inst, brw_imm_d($4));
			brw_inst_set_uip(p->devinfo, brw_last_inst, $5);
		} else {
			brw_set_src0(p, brw_last_inst, retype(brw_null_reg(),
				     BRW_REGISTER_TYPE_D));
			brw_set_src1(p, brw_last_inst, brw_imm_d($5));
		}

		brw_inst_set_jip(p->devinfo, brw_last_inst, $4);
		brw_inst_set_uip(p->devinfo, brw_last_inst, $5);
		brw_pop_insn_state(p);
	}
	| predicate CONT execsize relativelocation relativelocation instoptions
	{
		brw_next_insn(p, $2);
		i965_asm_set_instruction_options(p, $6);
		brw_inst_set_exec_size(p->devinfo, brw_last_inst, $3);
		brw_set_dest(p, brw_last_inst, brw_ip_reg());

		if (p->devinfo->gen >= 8) {
			brw_set_src0(p, brw_last_inst, brw_imm_d(0x0));
			brw_inst_set_jip(p->devinfo, brw_last_inst, $4);
			brw_inst_set_uip(p->devinfo, brw_last_inst, $5);
		} else {
			brw_set_src0(p, brw_last_inst, brw_ip_reg());
			brw_set_src1(p, brw_last_inst, brw_imm_d(0x0));
			if (p->devinfo->gen >= 6) {
				brw_inst_set_jip(p->devinfo, brw_last_inst, $4);
				brw_inst_set_uip(p->devinfo, brw_last_inst, $5);
			} else {
				brw_inst_set_gen4_jump_count(p->devinfo, brw_last_inst,
							     $4);
				brw_inst_set_gen4_pop_count(p->devinfo, brw_last_inst,
							    $5);
			}
		}

		brw_pop_insn_state(p);
	}
	;

/* loop instruction */
loopinstruction:
	predicate WHILE execsize relativelocation instoptions
	{
		brw_next_insn(p, $2);
		i965_asm_set_instruction_options(p, $5);
		brw_inst_set_exec_size(p->devinfo, brw_last_inst, $3);

		if (p->devinfo->gen >= 6) {
			if (p->devinfo->gen >= 8) {
				brw_set_dest(p, brw_last_inst,
					     retype(brw_null_reg(),
					     BRW_REGISTER_TYPE_D));
				brw_set_src0(p, brw_last_inst, brw_imm_d($4));
			} else if (p->devinfo->gen == 7) {
				brw_set_dest(p, brw_last_inst,
					     retype(brw_null_reg(),
					     BRW_REGISTER_TYPE_D));
				brw_set_src0(p, brw_last_inst,
					     retype(brw_null_reg(),
					     BRW_REGISTER_TYPE_D));
				brw_set_src1(p, brw_last_inst,
					     brw_imm_w(0x0));
				brw_inst_set_jip(p->devinfo, brw_last_inst,
						 $4);
			} else {
				brw_set_dest(p, brw_last_inst, brw_imm_w(0x0));
				brw_inst_set_gen6_jump_count(p->devinfo,
							     brw_last_inst,
							     $4);
				brw_set_src0(p, brw_last_inst,
					     retype(brw_null_reg(),
					     BRW_REGISTER_TYPE_D));
				brw_set_src1(p, brw_last_inst,
					     retype(brw_null_reg(),
					     BRW_REGISTER_TYPE_D));
			}
		} else {
			brw_set_dest(p, brw_last_inst, brw_ip_reg());
			brw_set_src0(p, brw_last_inst, brw_ip_reg());
			brw_set_src1(p, brw_last_inst, brw_imm_d(0x0));
			brw_inst_set_gen4_jump_count(p->devinfo, brw_last_inst,
						     $4);
			brw_inst_set_gen4_pop_count(p->devinfo, brw_last_inst,
						    0);
		}
		brw_pop_insn_state(p);
	}
	| DO execsize instoptions
	{
		brw_next_insn(p, $1);
		if (p->devinfo->gen < 6) {
			brw_inst_set_exec_size(p->devinfo, brw_last_inst, $2);
			i965_asm_set_instruction_options(p, $3);
			brw_set_dest(p, brw_last_inst, brw_null_reg());
			brw_set_src0(p, brw_last_inst, brw_null_reg());
			brw_set_src1(p, brw_last_inst, brw_null_reg());

			brw_inst_set_qtr_control(p->devinfo, brw_last_inst, BRW_COMPRESSION_NONE);
		}
	}
	;

/* Relative location */
relativelocation2:
	immreg
	| reg32
	;

simple_int:
	INTEGER 	        { $$ = $1; }
	| MINUS INTEGER 	{ $$ = -$2; }
	| LONG 		        { $$ = $1; }
	| MINUS LONG 	        { $$ = -$2; }
	;

rellocation:
	relativelocation
	| %empty { $$ = 0; }
	;

relativelocation:
	simple_int
	{
		$$ = $1;
	}
	;

/* Destination register */
dst:
	dstoperand
	| dstoperandex
	;

dstoperand:
	dstreg dstregion writemask dsttype
	{
		$$ = $1;

		if ($2 == -1) {
			$$.hstride = BRW_HORIZONTAL_STRIDE_1;
			$$.vstride = BRW_VERTICAL_STRIDE_1;
			$$.width = BRW_WIDTH_1;
		} else {
			$$.hstride = $2;
		}
		$$.type = $4.type;
		$$.writemask = $3.writemask;
		$$.swizzle = BRW_SWIZZLE_NOOP;
		$$.subnr = $$.subnr * brw_reg_type_to_size($4.type);
	}
	;

dstoperandex:
	dstoperandex_typed dstregion writemask dsttype
	{
		$$ = $1;
		$$.hstride = $2;
		$$.type = $4.type;
		$$.writemask = $3.writemask;
		$$.subnr = $$.subnr * brw_reg_type_to_size($4.type);
	}
	| dstoperandex_ud_typed
	{
		$$ = $1;
		$$.hstride = 1;
		$$.type = BRW_REGISTER_TYPE_UD;
	}
	/* BSpec says "When the conditional modifier is present, updates
	 * to the selected flag register also occur. In this case, the
	 * register region fields of the ‘null’ operand are valid."
	 */
	| nullreg dstregion writemask dsttype
	{
		$$ = $1;
		if ($2 == -1) {
			$$.hstride = BRW_HORIZONTAL_STRIDE_1;
			$$.vstride = BRW_VERTICAL_STRIDE_1;
			$$.width = BRW_WIDTH_1;
		} else {
			$$.hstride = $2;
		}
		$$.writemask = $3.writemask;
		$$.type = $4.type;
	}
	| threadcontrolreg
	{
		$$ = $1;
		$$.hstride = 1;
		$$.type = BRW_REGISTER_TYPE_UW;
	}
	;

dstoperandex_ud_typed:
	controlreg
	| ipreg
	| channelenablereg
	| performancereg
	;

dstoperandex_typed:
	accreg
	| flagreg
	| addrreg
	| maskreg
	| statereg
	;

dstreg:
	directgenreg
	{
		$$ = $1;
		$$.address_mode = BRW_ADDRESS_DIRECT;
	}
	| indirectgenreg
	{
		$$ = $1;
		$$.address_mode = BRW_ADDRESS_REGISTER_INDIRECT_REGISTER;
	}
	| directmsgreg
	{
		$$ = $1;
		$$.address_mode = BRW_ADDRESS_DIRECT;
	}
	| indirectmsgreg
	{
		$$ = $1;
		$$.address_mode = BRW_ADDRESS_REGISTER_INDIRECT_REGISTER;
	}
	;

/* Source register */
srcaccimm:
	srcacc
	| immreg
	;

immreg:
	immval srcimmtype
	{
		uint32_t u32;
		uint64_t u64;
		switch ($2.type) {
		case BRW_REGISTER_TYPE_UD:
			u32 = $1;
			$$ = brw_imm_ud(u32);
			break;
		case BRW_REGISTER_TYPE_D:
			$$ = brw_imm_d($1);
			break;
		case BRW_REGISTER_TYPE_UW:
			u32 = $1 | ($1 << 16);
			$$ = brw_imm_uw(u32);
			break;
		case BRW_REGISTER_TYPE_W:
			u32 = $1;
			$$ = brw_imm_w(u32);
			break;
		case BRW_REGISTER_TYPE_F:
			$$ = brw_imm_reg(BRW_REGISTER_TYPE_F);
			$$.u64 = $1;
			$$.ud = $1;
			break;
		case BRW_REGISTER_TYPE_V:
			$$ = brw_imm_v($1);
			break;
		case BRW_REGISTER_TYPE_UV:
			$$ = brw_imm_uv($1);
			break;
		case BRW_REGISTER_TYPE_VF:
			$$ = brw_imm_reg(BRW_REGISTER_TYPE_VF);
			$$.d = $1;
			break;
		case BRW_REGISTER_TYPE_Q:
			u64 = $1;
			$$ = brw_imm_q(u64);
			break;
		case BRW_REGISTER_TYPE_UQ:
			u64 = $1;
			$$ = brw_imm_uq(u64);
			break;
		case BRW_REGISTER_TYPE_DF:
			$$ = brw_imm_reg(BRW_REGISTER_TYPE_DF);
			$$.d64 = $1;
			break;
		default:
			error(&@2, "Unkown immdediate type %s\n",
			      brw_reg_type_to_letters($2.type));
		}
	}
	;

reg32:
	directgenreg region srctype
	{
		$$ = set_direct_src_operand(&$1, $3.type);
		$$ = stride($$, $2.vstride, $2.width, $2.hstride);
	}
	;

payload:
	directsrcoperand
	;

src:
	directsrcoperand
	| indirectsrcoperand
	;

srcacc:
	directsrcaccoperand
	| indirectsrcoperand
	;

srcimm:
	directsrcoperand
	| indirectsrcoperand
	| immreg
	;

directsrcaccoperand:
	directsrcoperand
	| accreg region srctype
	{
		$$ = set_direct_src_operand(&$1, $3.type);
		$$.vstride = $2.vstride;
		$$.width = $2.width;
		$$.hstride = $2.hstride;
	}
	;

srcarcoperandex:
	srcarcoperandex_typed region srctype
	{
		$$ = brw_reg($1.file,
			     $1.nr,
			     $1.subnr,
			     0,
			     0,
			     $3.type,
			     $2.vstride,
			     $2.width,
			     $2.hstride,
			     BRW_SWIZZLE_NOOP,
			     WRITEMASK_XYZW);
	}
	| srcarcoperandex_ud_typed
	{
		$$ = set_direct_src_operand(&$1, BRW_REGISTER_TYPE_UD);
	}
	| nullreg region srctype
	{
		$$ = set_direct_src_operand(&$1, $3.type);
		$$.vstride = $2.vstride;
		$$.width = $2.width;
		$$.hstride = $2.hstride;
	}
	| threadcontrolreg
	{
		$$ = set_direct_src_operand(&$1, BRW_REGISTER_TYPE_UW);
	}
	;

srcarcoperandex_ud_typed:
	controlreg
	| statereg
	| ipreg
	| channelenablereg
	;

srcarcoperandex_typed:
	flagreg
	| maskreg
	;

indirectsrcoperand:
	negate abs indirectgenreg indirectregion swizzle srctype
	{
		$$ = brw_reg($3.file,
			     0,
			     $3.subnr,
			     $1,  // negate
			     $2,  // abs
			     $6.type,
			     $4.vstride,
			     $4.width,
			     $4.hstride,
			     $5.swizzle,
			     WRITEMASK_X);

		$$.address_mode = BRW_ADDRESS_REGISTER_INDIRECT_REGISTER;
		// brw_reg set indirect_offset to 0 so set it to valid value
		$$.indirect_offset = $3.indirect_offset;
	}
	;

directgenreg_list:
	directgenreg
	| directmsgreg
	| notifyreg
	| addrreg
	| performancereg
	;

directsrcoperand:
	negate abs directgenreg_list region swizzle srctype
	{
		$$ = brw_reg($3.file,
			     $3.nr,
			     $3.subnr,
			     $1,
			     $2,
			     $6.type,
			     $4.vstride,
			     $4.width,
			     $4.hstride,
			     $5.swizzle,
			     WRITEMASK_X);
	}
	| srcarcoperandex
	;

/* Address register */
addrparam:
	addrreg exp
	{
		memset(&$$, '\0', sizeof($$));
		$$.subnr = $1.subnr;
		$$.indirect_offset = $2;
	}
	| addrreg
	;

/* Register files and register numbers */
exp:
	INTEGER 	{ $$ = $1; }
	| LONG 	        { $$ = $1; }
	;

subregnum:
	DOT exp 		        { $$ = $2; }
	| %empty %prec SUBREGNUM 	{ $$ = 0; }
	;

directgenreg:
	GENREG subregnum
	{
		memset(&$$, '\0', sizeof($$));
		$$.file = BRW_GENERAL_REGISTER_FILE;
		$$.nr = $1;
		$$.subnr = $2;
	}
	;

indirectgenreg:
	GENREGFILE LSQUARE addrparam RSQUARE
	{
		memset(&$$, '\0', sizeof($$));
		$$.file = BRW_GENERAL_REGISTER_FILE;
		$$.subnr = $3.subnr;
		$$.indirect_offset = $3.indirect_offset;
	}
	;

directmsgreg:
	MSGREG subregnum
	{
		$$ = brw_message_reg($1);
		$$.subnr = $2;
	}
	;

indirectmsgreg:
	MSGREGFILE LSQUARE addrparam RSQUARE
	{
		memset(&$$, '\0', sizeof($$));
		$$.file = BRW_MESSAGE_REGISTER_FILE;
		$$.subnr = $3.subnr;
		$$.indirect_offset = $3.indirect_offset;
	}
	;

addrreg:
	ADDRREG subregnum
	{
		if ($1 != 0)
			error(&@1, "Address register number %d"
				   "out of range\n", $1);

		int subnr = (p->devinfo->gen >= 8) ? 16 : 8;

		if ($2 > subnr)
			error(&@2, "Address sub resgister number %d"
				   "out of range\n", $2);

		$$ = brw_address_reg($2);
	}
	;

accreg:
	ACCREG subregnum
	{
		int nr_reg;
		if (p->devinfo->gen < 8)
			nr_reg = 2;
		else
			nr_reg = 10;

		if ($1 > nr_reg)
			error(&@1, "Accumulator register number %d"
				   " out of range\n", $1);

		memset(&$$, '\0', sizeof($$));
		$$.file = BRW_ARCHITECTURE_REGISTER_FILE;
		$$.nr = BRW_ARF_ACCUMULATOR;
		$$.subnr = $2;
	}
	;

flagreg:
	FLAGREG subregnum
	{
		// SNB = 1 flag reg and IVB+ = 2 flag reg
		int nr_reg = (p->devinfo->gen >= 7) ? 2 : 1;
		int subnr = nr_reg;

		if ($1 > nr_reg)
			error(&@1, "Flag register number %d"
				   " out of range \n", $1);
		if ($2 > subnr)
			error(&@2, "Flag subregister number %d"
				   " out of range\n", $2);

		$$.file = BRW_ARCHITECTURE_REGISTER_FILE;
		$$.nr = BRW_ARF_FLAG | $1;
		$$.subnr = $2;
	}
	;

maskreg:
	MASKREG subregnum
	{
		if ($1 > 0)
			error(&@1, "Mask register number %d"
				   " out of range\n", $1);

		$$ = brw_mask_reg($2);
	}
	;

notifyreg:
	NOTIFYREG subregnum
	{
		if ($1 > 0)
			error(&@1, "Notification register number %d"
				   " out of range\n", $1);

		int subnr = (p->devinfo->gen >= 11) ? 2 : 3;
		if ($2 > subnr)
			error(&@2, "Notification sub register number %d"
				   " out of range\n", $2);

		$$ = brw_notification_reg();
		$$.subnr = $2;
	}
	;

statereg:
	STATEREG subregnum
	{
		if ($1 > 2)
			error(&@1, "State register number %d"
				   " out of range\n", $1);

		if ($2 > 4)
			error(&@2, "State sub register number %d"
				   " out of range\n", $2);

		$$ = brw_sr0_reg($2);
		$$.nr = $1;
	}
	;

controlreg:
	CONTROLREG subregnum
	{
		if ($1 > 0)
			error(&@1, "Control register number %d"
				   " out of range\n", $1);

		if ($2 > 4)
			error(&@2, "control sub register number %d"
				   " out of range\n", $2);

		$$ = brw_cr0_reg($2);
		$$.nr = $1;
	}
	;

ipreg:
	IPREG srctype 	{ $$ = brw_ip_reg(); }
	;

nullreg:
	NULL_TOKEN 	{ $$ = brw_null_reg(); }
	;

threadcontrolreg:
	THREADREG subregnum
	{
		if ($1 > 0)
			error(&@1, "Thread control register number %d"
				   " out of range\n", $1);

		if ($2 > 7)
			error(&@2, "Thread control sub register number %d"
				   " out of range\n", $2);

		$$ = brw_tdr_reg();
		$$.subnr = $2;
	}
	;

performancereg:
	PERFORMANCEREG subregnum
	{
		int subnr;
		if (p->devinfo->gen >= 10)
			subnr = 5;
		else if (p->devinfo->gen <= 8)
			subnr = 3;
		else
			subnr = 4;

		if ($2 > subnr)
			error(&@2, "Performance sub register number %d"
				   " out of range\n", $2);

		$$.file = BRW_ARCHITECTURE_REGISTER_FILE;
		$$.nr = BRW_ARF_TIMESTAMP;
	}
	;

channelenablereg:
	CHANNELENABLEREG subregnum
	{
		if ($1 > 0)
			error(&@1, "Channel enable register number %d"
				   " out of range\n", $1);

		$$ = brw_mask_reg($2);
	}
	;

/* Immediate values */
immval:
	exp2
	{
		$$ = $1;
	}
	| LSQUARE exp2 COMMA exp2 COMMA exp2 COMMA exp2 RSQUARE
	{
		$$ = ($2 << 0) | ($4 << 8) | ($6 << 16) | ($8 << 24);
	}
	;

/* Regions */
dstregion:
	%empty 	{ $$ = -1; }
	| LANGLE exp RANGLE
	{
		if ($2 != 0 && ($2 > 4 || !isPowerofTwo($2)))
			error(&@2, "Invalid Horizontal stride %d\n", $2);

		$$ = ffs($2);
	}
	;

indirectregion:
	region
	| region_wh
	;

region:
	%empty
	{
		$$ = stride($$, BRW_VERTICAL_STRIDE_1, BRW_WIDTH_2, BRW_HORIZONTAL_STRIDE_1);
	}
	| LANGLE exp RANGLE
	{
		if ($2 != 0 && ($2 > 32 || !isPowerofTwo($2)))
			error(&@2, "Invalid VertStride %d\n", $2);

		$$ = stride($$, $2, BRW_WIDTH_1, 0);
	}
	| LANGLE exp COMMA exp COMMA exp RANGLE
	{

		if ($2 != 0 && ($2 > 32 || !isPowerofTwo($2)))
			error(&@2, "Invalid VertStride %d\n", $2);

		if ($4 > 16 || !isPowerofTwo($4))
			error(&@4, "Invalid width %d\n", $4);

		if ($6 != 0 && ($6 > 4 || !isPowerofTwo($6)))
			error(&@6, "Invalid Horizontal stride in"
				   "  region_wh %d\n", $6);

		$$ = stride($$, $2, $4, $6);
	}
	| LANGLE exp SEMICOLON exp COMMA exp RANGLE
	{
		if ($2 != 0 && ($2 > 32 || !isPowerofTwo($2)))
			error(&@2, "Invalid VertStride %d\n", $2);

		if ($4 > 16 || !isPowerofTwo($4))
			error(&@4, "Invalid width %d\n", $4);

		if ($6 != 0 && ($6 > 4 || !isPowerofTwo($6)))
			error(&@6, "Invalid Horizontal stride in"
				   " region_wh %d\n", $6);

		$$ = stride($$, $2, $4, $6);
	}
	| LANGLE VxH COMMA exp COMMA exp RANGLE
	{
		if ($4 > 16 || !isPowerofTwo($4))
			error(&@4, "Invalid width %d\n", $4);

		if ($6 != 0 && ($6 > 4 || !isPowerofTwo($6)))
			error(&@6, "Invalid Horizontal stride in"
				   " region_wh %d\n", $6);

		$$ = brw_VxH_indirect(0, 0);
	}
	;

region_wh:
	LANGLE exp COMMA exp RANGLE
	{
		if ($2 > 16 || !isPowerofTwo($2))
			error(&@2, "Invalid width %d\n", $2);

		if ($4 != 0 && ($4 > 4 || !isPowerofTwo($4)))
			error(&@4, "Invalid Horizontal stride in"
				   " region_wh %d\n", $4);

		$$ = stride($$, BRW_VERTICAL_STRIDE_ONE_DIMENSIONAL, $2, $4);
	}
	;

srctype:
	%empty 	        { $$ = retype($$, BRW_REGISTER_TYPE_F); }
	| TYPE_F 	{ $$ = retype($$, BRW_REGISTER_TYPE_F); }
	| TYPE_UD 	{ $$ = retype($$, BRW_REGISTER_TYPE_UD); }
	| TYPE_D 	{ $$ = retype($$, BRW_REGISTER_TYPE_D); }
	| TYPE_UW 	{ $$ = retype($$, BRW_REGISTER_TYPE_UW); }
	| TYPE_W 	{ $$ = retype($$, BRW_REGISTER_TYPE_W); }
	| TYPE_UB 	{ $$ = retype($$, BRW_REGISTER_TYPE_UB); }
	| TYPE_B 	{ $$ = retype($$, BRW_REGISTER_TYPE_B); }
	| TYPE_DF 	{ $$ = retype($$, BRW_REGISTER_TYPE_DF); }
	| TYPE_UQ 	{ $$ = retype($$, BRW_REGISTER_TYPE_UQ); }
	| TYPE_Q 	{ $$ = retype($$, BRW_REGISTER_TYPE_Q); }
	| TYPE_HF 	{ $$ = retype($$, BRW_REGISTER_TYPE_HF); }
	| TYPE_NF 	{ $$ = retype($$, BRW_REGISTER_TYPE_NF); }
	;

srcimmtype:
	srctype 	{ $$ = $1; }
	| TYPE_V 	{ $$ = retype($$, BRW_REGISTER_TYPE_V); }
	| TYPE_VF 	{ $$ = retype($$, BRW_REGISTER_TYPE_VF); }
	| TYPE_UV 	{ $$ = retype($$, BRW_REGISTER_TYPE_UV); }
	;

dsttype:
	srctype 	{ $$ = $1; }
	;

writemask:
	%empty
	{
		$$= brw_set_writemask($$, WRITEMASK_XYZW);
	}
	| DOT writemask_x writemask_y writemask_z writemask_w
	{
		$$ = brw_set_writemask($$, $2 | $3 | $4 | $5);
	}
	;

writemask_x:
	%empty 	{ $$ = 0; }
	| X 	{ $$ = 1 << BRW_CHANNEL_X; }
	;

writemask_y:
	%empty 	{ $$ = 0; }
	| Y 	{ $$ = 1 << BRW_CHANNEL_Y; }
	;

writemask_z:
	%empty 	{ $$ = 0; }
	| Z 	{ $$ = 1 << BRW_CHANNEL_Z; }
	;

writemask_w:
	%empty 	{ $$ = 0; }
	| W 	{ $$ = 1 << BRW_CHANNEL_W; }
	;

swizzle:
	%empty
	{
		$$.swizzle = BRW_SWIZZLE_NOOP;
	}
	| DOT chansel
	{
		$$.swizzle = BRW_SWIZZLE4($2, $2, $2, $2);
	}
	| DOT chansel chansel chansel chansel
	{
		$$.swizzle = BRW_SWIZZLE4($2, $3, $4, $5);
	}
	;

chansel:
	X
	| Y
	| Z
	| W
	;

/* Instruction prediction and modifiers */
predicate:
	%empty
	{
		brw_push_insn_state(p);
		brw_set_default_predicate_control(p, BRW_PREDICATE_NONE);
		brw_set_default_flag_reg(p, 0, 0);
		brw_set_default_predicate_inverse(p, false);
	}
	| LPAREN predstate flagreg predctrl RPAREN
	{
		brw_push_insn_state(p);
		brw_set_default_predicate_inverse(p, $2);
		brw_set_default_flag_reg(p, $3.nr, $3.subnr);
		brw_set_default_predicate_control(p, $4);
	}
	;

predstate:
	%empty 	        { $$ = 0; }
	| PLUS 	        { $$ = 0; }
	| MINUS 	{ $$ = 1; }
	;

predctrl:
	%empty 	        { $$ = BRW_PREDICATE_NORMAL; }
	| DOT X 	{ $$ = BRW_PREDICATE_ALIGN16_REPLICATE_X; }
	| DOT Y 	{ $$ = BRW_PREDICATE_ALIGN16_REPLICATE_Y; }
	| DOT Z 	{ $$ = BRW_PREDICATE_ALIGN16_REPLICATE_Z; }
	| DOT W 	{ $$ = BRW_PREDICATE_ALIGN16_REPLICATE_W; }
	| ANYV
	| ALLV
	| ANY2H
	| ALL2H
	| ANY4H
	| ALL4H
	| ANY8H
	| ALL8H
	| ANY16H
	| ALL16H
	| ANY32H
	| ALL32H
	;

/* Source Modification */
negate:
	%empty 	        { $$ = 0; }
	| MINUS 	{ $$ = 1; }
	;

abs:
	%empty 	{ $$ = 0; }
	| ABS 	{ $$ = 1; }
	;

/* Flag (Conditional) Modifier */
cond_mod:
	condModifiers
	{
		$$.cond_modifier = $1;
		$$.flag_reg_nr = 0;
		$$.flag_subreg_nr = 0;
	}
	| condModifiers DOT flagreg
	{
		$$.cond_modifier = $1;
		$$.flag_reg_nr = $3.nr;
		$$.flag_subreg_nr = $3.subnr;
	}
	;

condModifiers:
	%empty 	{ $$ = BRW_CONDITIONAL_NONE; }
	| ZERO
	| EQUAL
	| NOT_ZERO
	| NOT_EQUAL
	| GREATER
	| GREATER_EQUAL
	| LESS
	| LESS_EQUAL
	| OVERFLOW
	| ROUND_INCREMENT
	| UNORDERED
	;

saturate:
	%empty 		{ $$ = BRW_INSTRUCTION_NORMAL; }
	| SATURATE 	{ $$ = BRW_INSTRUCTION_SATURATE; }
	;

/* Execution size */
execsize:
	%empty %prec EMPTYEXECSIZE
	{
		$$ = 0;
	}
	| LPAREN exp2 RPAREN
	{
		if ($2 > 32 || !isPowerofTwo($2))
			error(&@2, "Invalid execution size %d\n", $2);

		$$ = cvt($2) - 1;
	}
	;

/* Instruction options */
instoptions:
	%empty
	{
		memset(&$$, 0, sizeof($$));
	}
	| LCURLY instoption_list RCURLY
	{
		memset(&$$, 0, sizeof($$));
		$$ = $2;
	}
	;

instoption_list:
	instoption_list COMMA instoption
	{
		memset(&$$, 0, sizeof($$));
		$$ = $1;
		add_instruction_option(&$$, $3);
	}
	| instoption_list instoption
	{
		memset(&$$, 0, sizeof($$));
		$$ = $1;
		add_instruction_option(&$$, $2);
	}
	| %empty
	{
		memset(&$$, 0, sizeof($$));
	}
	;

instoption:
	ALIGN1 	        { $$ = ALIGN1;}
	| ALIGN16 	{ $$ = ALIGN16; }
	| ACCWREN 	{ $$ = ACCWREN; }
	| SECHALF 	{ $$ = SECHALF; }
	| COMPR 	{ $$ = COMPR; }
	| COMPR4 	{ $$ = COMPR4; }
	| BREAKPOINT 	{ $$ = BREAKPOINT; }
	| NODDCLR 	{ $$ = NODDCLR; }
	| NODDCHK 	{ $$ = NODDCHK; }
	| MASK_DISABLE 	{ $$ = MASK_DISABLE; }
	| EOT 	        { $$ = EOT; }
	| SWITCH 	{ $$ = SWITCH; }
	| ATOMIC 	{ $$ = ATOMIC; }
	| CMPTCTRL 	{ $$ = CMPTCTRL; }
	| WECTRL 	{ $$ = WECTRL; }
	| QTR_2Q 	{ $$ = QTR_2Q; }
	| QTR_3Q 	{ $$ = QTR_3Q; }
	| QTR_4Q 	{ $$ = QTR_4Q; }
	| QTR_2H 	{ $$ = QTR_2H; }
	| QTR_2N 	{ $$ = QTR_2N; }
	| QTR_3N 	{ $$ = QTR_3N; }
	| QTR_4N 	{ $$ = QTR_4N; }
	| QTR_5N 	{ $$ = QTR_5N; }
	| QTR_6N 	{ $$ = QTR_6N; }
	| QTR_7N 	{ $$ = QTR_7N; }
	| QTR_8N 	{ $$ = QTR_8N; }
	;

%%

extern int yylineno;

void
yyerror(char *msg)
{
	fprintf(stderr, "%s: %d: %s at \"%s\"\n",
	        input_filename, yylineno, msg, lex_text());
	++errors;
}
