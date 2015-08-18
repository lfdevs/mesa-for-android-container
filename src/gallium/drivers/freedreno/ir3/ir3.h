/*
 * Copyright (c) 2013 Rob Clark <robdclark@gmail.com>
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

#ifndef IR3_H_
#define IR3_H_

#include <stdint.h>
#include <stdbool.h>

#include "util/u_debug.h"
#include "util/list.h"

#include "instr-a3xx.h"
#include "disasm.h"  /* TODO move 'enum shader_t' somewhere else.. */

/* low level intermediate representation of an adreno shader program */

struct ir3_compiler;
struct ir3;
struct ir3_instruction;
struct ir3_block;

struct ir3_info {
	uint32_t gpu_id;
	uint16_t sizedwords;
	uint16_t instrs_count;   /* expanded to account for rpt's */
	/* NOTE: max_reg, etc, does not include registers not touched
	 * by the shader (ie. vertex fetched via VFD_DECODE but not
	 * touched by shader)
	 */
	int8_t   max_reg;   /* highest GPR # used by shader */
	int8_t   max_half_reg;
	int16_t  max_const;
};

struct ir3_register {
	enum {
		IR3_REG_CONST  = 0x001,
		IR3_REG_IMMED  = 0x002,
		IR3_REG_HALF   = 0x004,
		IR3_REG_RELATIV= 0x008,
		IR3_REG_R      = 0x010,
		/* Most instructions, it seems, can do float abs/neg but not
		 * integer.  The CP pass needs to know what is intended (int or
		 * float) in order to do the right thing.  For this reason the
		 * abs/neg flags are split out into float and int variants.  In
		 * addition, .b (bitwise) operations, the negate is actually a
		 * bitwise not, so split that out into a new flag to make it
		 * more clear.
		 */
		IR3_REG_FNEG   = 0x020,
		IR3_REG_FABS   = 0x040,
		IR3_REG_SNEG   = 0x080,
		IR3_REG_SABS   = 0x100,
		IR3_REG_BNOT   = 0x200,
		IR3_REG_EVEN   = 0x400,
		IR3_REG_POS_INF= 0x800,
		/* (ei) flag, end-input?  Set on last bary, presumably to signal
		 * that the shader needs no more input:
		 */
		IR3_REG_EI     = 0x1000,
		/* meta-flags, for intermediate stages of IR, ie.
		 * before register assignment is done:
		 */
		IR3_REG_SSA    = 0x2000,   /* 'instr' is ptr to assigning instr */
		IR3_REG_PHI_SRC= 0x4000,   /* phi src, regs[0]->instr points to phi */

	} flags;
	union {
		/* normal registers:
		 * the component is in the low two bits of the reg #, so
		 * rN.x becomes: (N << 2) | x
		 */
		int   num;
		/* immediate: */
		int32_t  iim_val;
		uint32_t uim_val;
		float    fim_val;
		/* relative: */
		int   offset;
	};

	/* for IR3_REG_SSA, src registers contain ptr back to
	 * assigning instruction.
	 */
	struct ir3_instruction *instr;

	union {
		/* used for cat5 instructions, but also for internal/IR level
		 * tracking of what registers are read/written by an instruction.
		 * wrmask may be a bad name since it is used to represent both
		 * src and dst that touch multiple adjacent registers.
		 */
		unsigned wrmask;
		/* for relative addressing, 32bits for array size is too small,
		 * but otoh we don't need to deal with disjoint sets, so instead
		 * use a simple size field (number of scalar components).
		 */
		unsigned size;
	};
};

struct ir3_instruction {
	struct ir3_block *block;
	int category;
	opc_t opc;
	enum {
		/* (sy) flag is set on first instruction, and after sample
		 * instructions (probably just on RAW hazard).
		 */
		IR3_INSTR_SY    = 0x001,
		/* (ss) flag is set on first instruction, and first instruction
		 * to depend on the result of "long" instructions (RAW hazard):
		 *
		 *   rcp, rsq, log2, exp2, sin, cos, sqrt
		 *
		 * It seems to synchronize until all in-flight instructions are
		 * completed, for example:
		 *
		 *   rsq hr1.w, hr1.w
		 *   add.f hr2.z, (neg)hr2.z, hc0.y
		 *   mul.f hr2.w, (neg)hr2.y, (neg)hr2.y
		 *   rsq hr2.x, hr2.x
		 *   (rpt1)nop
		 *   mad.f16 hr2.w, hr2.z, hr2.z, hr2.w
		 *   nop
		 *   mad.f16 hr2.w, (neg)hr0.w, (neg)hr0.w, hr2.w
		 *   (ss)(rpt2)mul.f hr1.x, (r)hr1.x, hr1.w
		 *   (rpt2)mul.f hr0.x, (neg)(r)hr0.x, hr2.x
		 *
		 * The last mul.f does not have (ss) set, presumably because the
		 * (ss) on the previous instruction does the job.
		 *
		 * The blob driver also seems to set it on WAR hazards, although
		 * not really clear if this is needed or just blob compiler being
		 * sloppy.  So far I haven't found a case where removing the (ss)
		 * causes problems for WAR hazard, but I could just be getting
		 * lucky:
		 *
		 *   rcp r1.y, r3.y
		 *   (ss)(rpt2)mad.f32 r3.y, (r)c9.x, r1.x, (r)r3.z
		 *
		 */
		IR3_INSTR_SS    = 0x002,
		/* (jp) flag is set on jump targets:
		 */
		IR3_INSTR_JP    = 0x004,
		IR3_INSTR_UL    = 0x008,
		IR3_INSTR_3D    = 0x010,
		IR3_INSTR_A     = 0x020,
		IR3_INSTR_O     = 0x040,
		IR3_INSTR_P     = 0x080,
		IR3_INSTR_S     = 0x100,
		IR3_INSTR_S2EN  = 0x200,
		IR3_INSTR_G     = 0x400,
		/* meta-flags, for intermediate stages of IR, ie.
		 * before register assignment is done:
		 */
		IR3_INSTR_MARK  = 0x1000,
	} flags;
	int repeat;
#ifdef DEBUG
	unsigned regs_max;
#endif
	unsigned regs_count;
	struct ir3_register **regs;
	union {
		struct {
			char inv;
			char comp;
			int  immed;
			struct ir3_block *target;
		} cat0;
		struct {
			type_t src_type, dst_type;
		} cat1;
		struct {
			enum {
				IR3_COND_LT = 0,
				IR3_COND_LE = 1,
				IR3_COND_GT = 2,
				IR3_COND_GE = 3,
				IR3_COND_EQ = 4,
				IR3_COND_NE = 5,
			} condition;
		} cat2;
		struct {
			unsigned samp, tex;
			type_t type;
		} cat5;
		struct {
			type_t type;
			int src_offset;
			int dst_offset;
			int iim_val;
		} cat6;
		/* for meta-instructions, just used to hold extra data
		 * before instruction scheduling, etc
		 */
		struct {
			int off;              /* component/offset */
		} fo;
		struct {
			int aid;
		} fi;
		struct {
			/* used to temporarily hold reference to nir_phi_instr
			 * until we resolve the phi srcs
			 */
			void *nphi;
		} phi;
		struct {
			struct ir3_block *block;
		} inout;
	};

	/* transient values used during various algorithms: */
	union {
		/* The instruction depth is the max dependency distance to output.
		 *
		 * You can also think of it as the "cost", if we did any sort of
		 * optimization for register footprint.  Ie. a value that is  just
		 * result of moving a const to a reg would have a low cost,  so to
		 * it could make sense to duplicate the instruction at various
		 * points where the result is needed to reduce register footprint.
		 *
		 * DEPTH_UNUSED used to mark unused instructions after depth
		 * calculation pass.
		 */
#define DEPTH_UNUSED  ~0
		unsigned depth;
		/* When we get to the RA stage, we no longer need depth, but
		 * we do need instruction's position/name:
		 */
		struct {
			uint16_t ip;
			uint16_t name;
		};
	};

	/* Used during CP and RA stages.  For fanin and shader inputs/
	 * outputs where we need a sequence of consecutive registers,
	 * keep track of each src instructions left (ie 'n-1') and right
	 * (ie 'n+1') neighbor.  The front-end must insert enough mov's
	 * to ensure that each instruction has at most one left and at
	 * most one right neighbor.  During the copy-propagation pass,
	 * we only remove mov's when we can preserve this constraint.
	 * And during the RA stage, we use the neighbor information to
	 * allocate a block of registers in one shot.
	 *
	 * TODO: maybe just add something like:
	 *   struct ir3_instruction_ref {
	 *       struct ir3_instruction *instr;
	 *       unsigned cnt;
	 *   }
	 *
	 * Or can we get away without the refcnt stuff?  It seems like
	 * it should be overkill..  the problem is if, potentially after
	 * already eliminating some mov's, if you have a single mov that
	 * needs to be grouped with it's neighbors in two different
	 * places (ex. shader output and a fanin).
	 */
	struct {
		struct ir3_instruction *left, *right;
		uint16_t left_cnt, right_cnt;
	} cp;

	/* an instruction can reference at most one address register amongst
	 * it's src/dst registers.  Beyond that, you need to insert mov's.
	 *
	 * NOTE: do not write this directly, use ir3_instr_set_address()
	 */
	struct ir3_instruction *address;

	/* in case of a instruction with relative dst instruction, we need to
	 * capture the dependency on the fanin for the previous values of
	 * the array elements.  Since we don't know at compile time actually
	 * which array elements are written, this serves to preserve the
	 * unconditional write to array elements prior to the conditional
	 * write.
	 *
	 * TODO only cat1 can do indirect write.. we could maybe move this
	 * into instr->cat1.fanin (but would require the frontend to insert
	 * the extra mov)
	 */
	struct ir3_instruction *fanin;

	/* Entry in ir3_block's instruction list: */
	struct list_head node;

#ifdef DEBUG
	uint32_t serialno;
#endif
};

static inline struct ir3_instruction *
ir3_neighbor_first(struct ir3_instruction *instr)
{
	while (instr->cp.left)
		instr = instr->cp.left;
	return instr;
}

static inline int ir3_neighbor_count(struct ir3_instruction *instr)
{
	int num = 1;

	debug_assert(!instr->cp.left);

	while (instr->cp.right) {
		num++;
		instr = instr->cp.right;
	}

	return num;
}

struct ir3_heap_chunk;

struct ir3 {
	struct ir3_compiler *compiler;

	unsigned ninputs, noutputs;
	struct ir3_instruction **inputs;
	struct ir3_instruction **outputs;

	/* Track bary.f (and ldlv) instructions.. this is needed in
	 * scheduling to ensure that all varying fetches happen before
	 * any potential kill instructions.  The hw gets grumpy if all
	 * threads in a group are killed before the last bary.f gets
	 * a chance to signal end of input (ei).
	 */
	unsigned baryfs_count, baryfs_sz;
	struct ir3_instruction **baryfs;

	/* Track all indirect instructions (read and write).  To avoid
	 * deadlock scenario where an address register gets scheduled,
	 * but other dependent src instructions cannot be scheduled due
	 * to dependency on a *different* address register value, the
	 * scheduler needs to ensure that all dependencies other than
	 * the instruction other than the address register are scheduled
	 * before the one that writes the address register.  Having a
	 * convenient list of instructions that reference some address
	 * register simplifies this.
	 */
	unsigned indirects_count, indirects_sz;
	struct ir3_instruction **indirects;
	/* and same for instructions that consume predicate register: */
	unsigned predicates_count, predicates_sz;
	struct ir3_instruction **predicates;

	/* Track instructions which do not write a register but other-
	 * wise must not be discarded (such as kill, stg, etc)
	 */
	unsigned keeps_count, keeps_sz;
	struct ir3_instruction **keeps;

	/* List of blocks: */
	struct list_head block_list;

	unsigned heap_idx;
	struct ir3_heap_chunk *chunk;
};

typedef struct nir_block nir_block;

struct ir3_block {
	struct list_head node;
	struct ir3 *shader;

	nir_block *nblock;

	struct list_head instr_list;  /* list of ir3_instruction */

	/* each block has either one or two successors.. in case of
	 * two successors, 'condition' decides which one to follow.
	 * A block preceding an if/else has two successors.
	 */
	struct ir3_instruction *condition;
	struct ir3_block *successors[2];

	uint16_t start_ip, end_ip;

	/* used for per-pass extra block data.  Mainly used right
	 * now in RA step to track livein/liveout.
	 */
	void *bd;

#ifdef DEBUG
	uint32_t serialno;
#endif
};

struct ir3 * ir3_create(struct ir3_compiler *compiler,
		unsigned nin, unsigned nout);
void ir3_destroy(struct ir3 *shader);
void * ir3_assemble(struct ir3 *shader,
		struct ir3_info *info, uint32_t gpu_id);
void * ir3_alloc(struct ir3 *shader, int sz);

struct ir3_block * ir3_block_create(struct ir3 *shader);

struct ir3_instruction * ir3_instr_create(struct ir3_block *block,
		int category, opc_t opc);
struct ir3_instruction * ir3_instr_create2(struct ir3_block *block,
		int category, opc_t opc, int nreg);
struct ir3_instruction * ir3_instr_clone(struct ir3_instruction *instr);
const char *ir3_instr_name(struct ir3_instruction *instr);

struct ir3_register * ir3_reg_create(struct ir3_instruction *instr,
		int num, int flags);

void ir3_instr_set_address(struct ir3_instruction *instr,
		struct ir3_instruction *addr);

static inline bool ir3_instr_check_mark(struct ir3_instruction *instr)
{
	if (instr->flags & IR3_INSTR_MARK)
		return true;  /* already visited */
	instr->flags |= IR3_INSTR_MARK;
	return false;
}

void ir3_block_clear_mark(struct ir3_block *block);
void ir3_clear_mark(struct ir3 *shader);

unsigned ir3_count_instructions(struct ir3 *ir);

static inline int ir3_instr_regno(struct ir3_instruction *instr,
		struct ir3_register *reg)
{
	unsigned i;
	for (i = 0; i < instr->regs_count; i++)
		if (reg == instr->regs[i])
			return i;
	return -1;
}


#define MAX_ARRAYS 16

/* comp:
 *   0 - x
 *   1 - y
 *   2 - z
 *   3 - w
 */
static inline uint32_t regid(int num, int comp)
{
	return (num << 2) | (comp & 0x3);
}

static inline uint32_t reg_num(struct ir3_register *reg)
{
	return reg->num >> 2;
}

static inline uint32_t reg_comp(struct ir3_register *reg)
{
	return reg->num & 0x3;
}

static inline bool is_flow(struct ir3_instruction *instr)
{
	return (instr->category == 0);
}

static inline bool is_kill(struct ir3_instruction *instr)
{
	return is_flow(instr) && (instr->opc == OPC_KILL);
}

static inline bool is_nop(struct ir3_instruction *instr)
{
	return is_flow(instr) && (instr->opc == OPC_NOP);
}

/* Is it a non-transformative (ie. not type changing) mov?  This can
 * also include absneg.s/absneg.f, which for the most part can be
 * treated as a mov (single src argument).
 */
static inline bool is_same_type_mov(struct ir3_instruction *instr)
{
	struct ir3_register *dst = instr->regs[0];

	/* mov's that write to a0.x or p0.x are special: */
	if (dst->num == regid(REG_P0, 0))
		return false;
	if (dst->num == regid(REG_A0, 0))
		return false;

	if ((instr->category == 1) &&
			(instr->cat1.src_type == instr->cat1.dst_type))
		return true;
	if ((instr->category == 2) && ((instr->opc == OPC_ABSNEG_F) ||
			(instr->opc == OPC_ABSNEG_S)))
		return true;
	return false;
}

static inline bool is_alu(struct ir3_instruction *instr)
{
	return (1 <= instr->category) && (instr->category <= 3);
}

static inline bool is_sfu(struct ir3_instruction *instr)
{
	return (instr->category == 4);
}

static inline bool is_tex(struct ir3_instruction *instr)
{
	return (instr->category == 5);
}

static inline bool is_mem(struct ir3_instruction *instr)
{
	return (instr->category == 6);
}

static inline bool
is_store(struct ir3_instruction *instr)
{
	if (is_mem(instr)) {
		/* these instructions, the "destination" register is
		 * actually a source, the address to store to.
		 */
		switch (instr->opc) {
		case OPC_STG:
		case OPC_STP:
		case OPC_STL:
		case OPC_STLW:
		case OPC_L2G:
		case OPC_G2L:
			return true;
		default:
			break;
		}
	}
	return false;
}

static inline bool is_load(struct ir3_instruction *instr)
{
	if (is_mem(instr)) {
		switch (instr->opc) {
		case OPC_LDG:
		case OPC_LDL:
		case OPC_LDP:
		case OPC_L2G:
		case OPC_LDLW:
		case OPC_LDC_4:
		case OPC_LDLV:
		/* probably some others too.. */
			return true;
		default:
			break;
		}
	}
	return false;
}

static inline bool is_input(struct ir3_instruction *instr)
{
	/* in some cases, ldlv is used to fetch varying without
	 * interpolation.. fortunately inloc is the first src
	 * register in either case
	 */
	if (is_mem(instr) && (instr->opc == OPC_LDLV))
		return true;
	return (instr->category == 2) && (instr->opc == OPC_BARY_F);
}

static inline bool is_meta(struct ir3_instruction *instr)
{
	/* TODO how should we count PHI (and maybe fan-in/out) which
	 * might actually contribute some instructions to the final
	 * result?
	 */
	return (instr->category == -1);
}

static inline bool writes_addr(struct ir3_instruction *instr)
{
	if (instr->regs_count > 0) {
		struct ir3_register *dst = instr->regs[0];
		return reg_num(dst) == REG_A0;
	}
	return false;
}

static inline bool writes_pred(struct ir3_instruction *instr)
{
	if (instr->regs_count > 0) {
		struct ir3_register *dst = instr->regs[0];
		return reg_num(dst) == REG_P0;
	}
	return false;
}

/* returns defining instruction for reg */
/* TODO better name */
static inline struct ir3_instruction *ssa(struct ir3_register *reg)
{
	if (reg->flags & IR3_REG_SSA)
		return reg->instr;
	return NULL;
}

static inline bool conflicts(struct ir3_instruction *a,
		struct ir3_instruction *b)
{
	return (a && b) && (a != b);
}

static inline bool reg_gpr(struct ir3_register *r)
{
	if (r->flags & (IR3_REG_CONST | IR3_REG_IMMED))
		return false;
	if ((reg_num(r) == REG_A0) || (reg_num(r) == REG_P0))
		return false;
	return true;
}

static inline type_t half_type(type_t type)
{
	switch (type) {
	case TYPE_F32: return TYPE_F16;
	case TYPE_U32: return TYPE_U16;
	case TYPE_S32: return TYPE_S16;
	case TYPE_F16:
	case TYPE_U16:
	case TYPE_S16:
		return type;
	default:
		assert(0);
		return ~0;
	}
}

/* some cat2 instructions (ie. those which are not float) can embed an
 * immediate:
 */
static inline bool ir3_cat2_int(opc_t opc)
{
	switch (opc) {
	case OPC_ADD_U:
	case OPC_ADD_S:
	case OPC_SUB_U:
	case OPC_SUB_S:
	case OPC_CMPS_U:
	case OPC_CMPS_S:
	case OPC_MIN_U:
	case OPC_MIN_S:
	case OPC_MAX_U:
	case OPC_MAX_S:
	case OPC_CMPV_U:
	case OPC_CMPV_S:
	case OPC_MUL_U:
	case OPC_MUL_S:
	case OPC_MULL_U:
	case OPC_CLZ_S:
	case OPC_ABSNEG_S:
	case OPC_AND_B:
	case OPC_OR_B:
	case OPC_NOT_B:
	case OPC_XOR_B:
	case OPC_BFREV_B:
	case OPC_CLZ_B:
	case OPC_SHL_B:
	case OPC_SHR_B:
	case OPC_ASHR_B:
	case OPC_MGEN_B:
	case OPC_GETBIT_B:
	case OPC_CBITS_B:
	case OPC_BARY_F:
		return true;

	default:
		return false;
	}
}


/* map cat2 instruction to valid abs/neg flags: */
static inline unsigned ir3_cat2_absneg(opc_t opc)
{
	switch (opc) {
	case OPC_ADD_F:
	case OPC_MIN_F:
	case OPC_MAX_F:
	case OPC_MUL_F:
	case OPC_SIGN_F:
	case OPC_CMPS_F:
	case OPC_ABSNEG_F:
	case OPC_CMPV_F:
	case OPC_FLOOR_F:
	case OPC_CEIL_F:
	case OPC_RNDNE_F:
	case OPC_RNDAZ_F:
	case OPC_TRUNC_F:
	case OPC_BARY_F:
		return IR3_REG_FABS | IR3_REG_FNEG;

	case OPC_ADD_U:
	case OPC_ADD_S:
	case OPC_SUB_U:
	case OPC_SUB_S:
	case OPC_CMPS_U:
	case OPC_CMPS_S:
	case OPC_MIN_U:
	case OPC_MIN_S:
	case OPC_MAX_U:
	case OPC_MAX_S:
	case OPC_CMPV_U:
	case OPC_CMPV_S:
	case OPC_MUL_U:
	case OPC_MUL_S:
	case OPC_MULL_U:
	case OPC_CLZ_S:
		return 0;

	case OPC_ABSNEG_S:
		return IR3_REG_SABS | IR3_REG_SNEG;

	case OPC_AND_B:
	case OPC_OR_B:
	case OPC_NOT_B:
	case OPC_XOR_B:
	case OPC_BFREV_B:
	case OPC_CLZ_B:
	case OPC_SHL_B:
	case OPC_SHR_B:
	case OPC_ASHR_B:
	case OPC_MGEN_B:
	case OPC_GETBIT_B:
	case OPC_CBITS_B:
		return IR3_REG_BNOT;

	default:
		return 0;
	}
}

/* map cat3 instructions to valid abs/neg flags: */
static inline unsigned ir3_cat3_absneg(opc_t opc)
{
	switch (opc) {
	case OPC_MAD_F16:
	case OPC_MAD_F32:
	case OPC_SEL_F16:
	case OPC_SEL_F32:
		return IR3_REG_FNEG;

	case OPC_MAD_U16:
	case OPC_MADSH_U16:
	case OPC_MAD_S16:
	case OPC_MADSH_M16:
	case OPC_MAD_U24:
	case OPC_MAD_S24:
	case OPC_SEL_S16:
	case OPC_SEL_S32:
	case OPC_SAD_S16:
	case OPC_SAD_S32:
		/* neg *may* work on 3rd src.. */

	case OPC_SEL_B16:
	case OPC_SEL_B32:

	default:
		return 0;
	}
}

#define array_insert(arr, val) do { \
		if (arr ## _count == arr ## _sz) { \
			arr ## _sz = MAX2(2 * arr ## _sz, 16); \
			arr = realloc(arr, arr ## _sz * sizeof(arr[0])); \
		} \
		arr[arr ##_count++] = val; \
	} while (0)

/* iterator for an instructions's sources (reg), also returns src #: */
#define foreach_src_n(__srcreg, __n, __instr) \
	if ((__instr)->regs_count) \
		for (unsigned __cnt = (__instr)->regs_count - 1, __n = 0; __n < __cnt; __n++) \
			if ((__srcreg = (__instr)->regs[__n + 1]))

/* iterator for an instructions's sources (reg): */
#define foreach_src(__srcreg, __instr) \
	foreach_src_n(__srcreg, __i, __instr)

static inline unsigned __ssa_src_cnt(struct ir3_instruction *instr)
{
	if (instr->fanin)
		return instr->regs_count + 2;
	if (instr->address)
		return instr->regs_count + 1;
	return instr->regs_count;
}

static inline struct ir3_instruction * __ssa_src_n(struct ir3_instruction *instr, unsigned n)
{
	if (n == (instr->regs_count + 1))
		return instr->fanin;
	if (n == (instr->regs_count + 0))
		return instr->address;
	return ssa(instr->regs[n]);
}

#define __src_cnt(__instr) ((__instr)->address ? (__instr)->regs_count : (__instr)->regs_count - 1)

/* iterator for an instruction's SSA sources (instr), also returns src #: */
#define foreach_ssa_src_n(__srcinst, __n, __instr) \
	if ((__instr)->regs_count) \
		for (unsigned __cnt = __ssa_src_cnt(__instr) - 1, __n = 0; __n < __cnt; __n++) \
			if ((__srcinst = __ssa_src_n(__instr, __n + 1)))

/* iterator for an instruction's SSA sources (instr): */
#define foreach_ssa_src(__srcinst, __instr) \
	foreach_ssa_src_n(__srcinst, __i, __instr)


/* dump: */
void ir3_print(struct ir3 *ir);
void ir3_print_instr(struct ir3_instruction *instr);

/* depth calculation: */
int ir3_delayslots(struct ir3_instruction *assigner,
		struct ir3_instruction *consumer, unsigned n);
void ir3_insert_by_depth(struct ir3_instruction *instr, struct list_head *list);
void ir3_depth(struct ir3 *ir);

/* copy-propagate: */
void ir3_cp(struct ir3 *ir);

/* group neighbors and insert mov's to resolve conflicts: */
void ir3_group(struct ir3 *ir);

/* scheduling: */
int ir3_sched(struct ir3 *ir);

/* register assignment: */
struct ir3_ra_reg_set * ir3_ra_alloc_reg_set(void *memctx);
int ir3_ra(struct ir3 *ir3, enum shader_t type,
		bool frag_coord, bool frag_face);

/* legalize: */
void ir3_legalize(struct ir3 *ir, bool *has_samp, int *max_bary);

/* ************************************************************************* */
/* instruction helpers */

static inline struct ir3_instruction *
ir3_MOV(struct ir3_block *block, struct ir3_instruction *src, type_t type)
{
	struct ir3_instruction *instr =
		ir3_instr_create(block, 1, 0);
	ir3_reg_create(instr, 0, 0);   /* dst */
	ir3_reg_create(instr, 0, IR3_REG_SSA)->instr = src;
	instr->cat1.src_type = type;
	instr->cat1.dst_type = type;
	return instr;
}

static inline struct ir3_instruction *
ir3_COV(struct ir3_block *block, struct ir3_instruction *src,
		type_t src_type, type_t dst_type)
{
	struct ir3_instruction *instr =
		ir3_instr_create(block, 1, 0);
	ir3_reg_create(instr, 0, 0);   /* dst */
	ir3_reg_create(instr, 0, IR3_REG_SSA)->instr = src;
	instr->cat1.src_type = src_type;
	instr->cat1.dst_type = dst_type;
	return instr;
}

static inline struct ir3_instruction *
ir3_NOP(struct ir3_block *block)
{
	return ir3_instr_create(block, 0, OPC_NOP);
}

#define INSTR0(CAT, name)                                                \
static inline struct ir3_instruction *                                   \
ir3_##name(struct ir3_block *block)                                      \
{                                                                        \
	struct ir3_instruction *instr =                                      \
		ir3_instr_create(block, CAT, OPC_##name);                        \
	return instr;                                                        \
}

#define INSTR1(CAT, name)                                                \
static inline struct ir3_instruction *                                   \
ir3_##name(struct ir3_block *block,                                      \
		struct ir3_instruction *a, unsigned aflags)                      \
{                                                                        \
	struct ir3_instruction *instr =                                      \
		ir3_instr_create(block, CAT, OPC_##name);                        \
	ir3_reg_create(instr, 0, 0);   /* dst */                             \
	ir3_reg_create(instr, 0, IR3_REG_SSA | aflags)->instr = a;           \
	return instr;                                                        \
}

#define INSTR2(CAT, name)                                                \
static inline struct ir3_instruction *                                   \
ir3_##name(struct ir3_block *block,                                      \
		struct ir3_instruction *a, unsigned aflags,                      \
		struct ir3_instruction *b, unsigned bflags)                      \
{                                                                        \
	struct ir3_instruction *instr =                                      \
		ir3_instr_create(block, CAT, OPC_##name);                        \
	ir3_reg_create(instr, 0, 0);   /* dst */                             \
	ir3_reg_create(instr, 0, IR3_REG_SSA | aflags)->instr = a;           \
	ir3_reg_create(instr, 0, IR3_REG_SSA | bflags)->instr = b;           \
	return instr;                                                        \
}

#define INSTR3(CAT, name)                                                \
static inline struct ir3_instruction *                                   \
ir3_##name(struct ir3_block *block,                                      \
		struct ir3_instruction *a, unsigned aflags,                      \
		struct ir3_instruction *b, unsigned bflags,                      \
		struct ir3_instruction *c, unsigned cflags)                      \
{                                                                        \
	struct ir3_instruction *instr =                                      \
		ir3_instr_create(block, CAT, OPC_##name);                        \
	ir3_reg_create(instr, 0, 0);   /* dst */                             \
	ir3_reg_create(instr, 0, IR3_REG_SSA | aflags)->instr = a;           \
	ir3_reg_create(instr, 0, IR3_REG_SSA | bflags)->instr = b;           \
	ir3_reg_create(instr, 0, IR3_REG_SSA | cflags)->instr = c;           \
	return instr;                                                        \
}

/* cat0 instructions: */
INSTR0(0, BR);
INSTR0(0, JUMP);
INSTR1(0, KILL);
INSTR0(0, END);

/* cat2 instructions, most 2 src but some 1 src: */
INSTR2(2, ADD_F)
INSTR2(2, MIN_F)
INSTR2(2, MAX_F)
INSTR2(2, MUL_F)
INSTR1(2, SIGN_F)
INSTR2(2, CMPS_F)
INSTR1(2, ABSNEG_F)
INSTR2(2, CMPV_F)
INSTR1(2, FLOOR_F)
INSTR1(2, CEIL_F)
INSTR1(2, RNDNE_F)
INSTR1(2, RNDAZ_F)
INSTR1(2, TRUNC_F)
INSTR2(2, ADD_U)
INSTR2(2, ADD_S)
INSTR2(2, SUB_U)
INSTR2(2, SUB_S)
INSTR2(2, CMPS_U)
INSTR2(2, CMPS_S)
INSTR2(2, MIN_U)
INSTR2(2, MIN_S)
INSTR2(2, MAX_U)
INSTR2(2, MAX_S)
INSTR1(2, ABSNEG_S)
INSTR2(2, AND_B)
INSTR2(2, OR_B)
INSTR1(2, NOT_B)
INSTR2(2, XOR_B)
INSTR2(2, CMPV_U)
INSTR2(2, CMPV_S)
INSTR2(2, MUL_U)
INSTR2(2, MUL_S)
INSTR2(2, MULL_U)
INSTR1(2, BFREV_B)
INSTR1(2, CLZ_S)
INSTR1(2, CLZ_B)
INSTR2(2, SHL_B)
INSTR2(2, SHR_B)
INSTR2(2, ASHR_B)
INSTR2(2, BARY_F)
INSTR2(2, MGEN_B)
INSTR2(2, GETBIT_B)
INSTR1(2, SETRM)
INSTR1(2, CBITS_B)
INSTR2(2, SHB)
INSTR2(2, MSAD)

/* cat3 instructions: */
INSTR3(3, MAD_U16)
INSTR3(3, MADSH_U16)
INSTR3(3, MAD_S16)
INSTR3(3, MADSH_M16)
INSTR3(3, MAD_U24)
INSTR3(3, MAD_S24)
INSTR3(3, MAD_F16)
INSTR3(3, MAD_F32)
INSTR3(3, SEL_B16)
INSTR3(3, SEL_B32)
INSTR3(3, SEL_S16)
INSTR3(3, SEL_S32)
INSTR3(3, SEL_F16)
INSTR3(3, SEL_F32)
INSTR3(3, SAD_S16)
INSTR3(3, SAD_S32)

/* cat4 instructions: */
INSTR1(4, RCP)
INSTR1(4, RSQ)
INSTR1(4, LOG2)
INSTR1(4, EXP2)
INSTR1(4, SIN)
INSTR1(4, COS)
INSTR1(4, SQRT)

/* cat5 instructions: */
INSTR1(5, DSX)
INSTR1(5, DSY)

static inline struct ir3_instruction *
ir3_SAM(struct ir3_block *block, opc_t opc, type_t type,
		unsigned wrmask, unsigned flags, unsigned samp, unsigned tex,
		struct ir3_instruction *src0, struct ir3_instruction *src1)
{
	struct ir3_instruction *sam;
	struct ir3_register *reg;

	sam = ir3_instr_create(block, 5, opc);
	sam->flags |= flags;
	ir3_reg_create(sam, 0, 0)->wrmask = wrmask;
	if (src0) {
		reg = ir3_reg_create(sam, 0, IR3_REG_SSA);
		reg->wrmask = (1 << (src0->regs_count - 1)) - 1;
		reg->instr = src0;
	}
	if (src1) {
		reg = ir3_reg_create(sam, 0, IR3_REG_SSA);
		reg->instr = src1;
		reg->wrmask = (1 << (src1->regs_count - 1)) - 1;
	}
	sam->cat5.samp = samp;
	sam->cat5.tex  = tex;
	sam->cat5.type  = type;

	return sam;
}

/* cat6 instructions: */
INSTR2(6, LDLV)
INSTR2(6, LDG)
INSTR3(6, STG)

/* ************************************************************************* */
/* split this out or find some helper to use.. like main/bitset.h.. */

#include <string.h>

#define MAX_REG 256

typedef uint8_t regmask_t[2 * MAX_REG / 8];

static inline unsigned regmask_idx(struct ir3_register *reg)
{
	unsigned num = reg->num;
	debug_assert(num < MAX_REG);
	if (reg->flags & IR3_REG_HALF)
		num += MAX_REG;
	return num;
}

static inline void regmask_init(regmask_t *regmask)
{
	memset(regmask, 0, sizeof(*regmask));
}

static inline void regmask_set(regmask_t *regmask, struct ir3_register *reg)
{
	unsigned idx = regmask_idx(reg);
	if (reg->flags & IR3_REG_RELATIV) {
		unsigned i;
		for (i = 0; i < reg->size; i++, idx++)
			(*regmask)[idx / 8] |= 1 << (idx % 8);
	} else {
		unsigned mask;
		for (mask = reg->wrmask; mask; mask >>= 1, idx++)
			if (mask & 1)
				(*regmask)[idx / 8] |= 1 << (idx % 8);
	}
}

static inline void regmask_or(regmask_t *dst, regmask_t *a, regmask_t *b)
{
	unsigned i;
	for (i = 0; i < ARRAY_SIZE(*dst); i++)
		(*dst)[i] = (*a)[i] | (*b)[i];
}

/* set bits in a if not set in b, conceptually:
 *   a |= (reg & ~b)
 */
static inline void regmask_set_if_not(regmask_t *a,
		struct ir3_register *reg, regmask_t *b)
{
	unsigned idx = regmask_idx(reg);
	if (reg->flags & IR3_REG_RELATIV) {
		unsigned i;
		for (i = 0; i < reg->size; i++, idx++)
			if (!((*b)[idx / 8] & (1 << (idx % 8))))
				(*a)[idx / 8] |= 1 << (idx % 8);
	} else {
		unsigned mask;
		for (mask = reg->wrmask; mask; mask >>= 1, idx++)
			if (mask & 1)
				if (!((*b)[idx / 8] & (1 << (idx % 8))))
					(*a)[idx / 8] |= 1 << (idx % 8);
	}
}

static inline bool regmask_get(regmask_t *regmask,
		struct ir3_register *reg)
{
	unsigned idx = regmask_idx(reg);
	if (reg->flags & IR3_REG_RELATIV) {
		unsigned i;
		for (i = 0; i < reg->size; i++, idx++)
			if ((*regmask)[idx / 8] & (1 << (idx % 8)))
				return true;
	} else {
		unsigned mask;
		for (mask = reg->wrmask; mask; mask >>= 1, idx++)
			if (mask & 1)
				if ((*regmask)[idx / 8] & (1 << (idx % 8)))
					return true;
	}
	return false;
}

/* ************************************************************************* */

#endif /* IR3_H_ */
