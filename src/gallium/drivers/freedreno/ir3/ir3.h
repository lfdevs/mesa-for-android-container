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

#include "instr-a3xx.h"
#include "disasm.h"  /* TODO move 'enum shader_t' somewhere else.. */

/* low level intermediate representation of an adreno shader program */

struct ir3;
struct ir3_instruction;
struct ir3_block;

struct ir3 * fd_asm_parse(const char *src);

struct ir3_info {
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
		IR3_REG_NEGATE = 0x020,
		IR3_REG_ABS    = 0x040,
		IR3_REG_EVEN   = 0x080,
		IR3_REG_POS_INF= 0x100,
		/* (ei) flag, end-input?  Set on last bary, presumably to signal
		 * that the shader needs no more input:
		 */
		IR3_REG_EI     = 0x200,
		/* meta-flags, for intermediate stages of IR, ie.
		 * before register assignment is done:
		 */
		IR3_REG_SSA    = 0x1000,   /* 'instr' is ptr to assigning instr */
		IR3_REG_IA     = 0x2000,   /* meta-input dst is "assigned" */
		IR3_REG_ADDR   = 0x4000,   /* register is a0.x */
	} flags;
	union {
		/* normal registers:
		 * the component is in the low two bits of the reg #, so
		 * rN.x becomes: (N << 2) | x
		 */
		int num;
		/* immediate: */
		int     iim_val;
		float   fim_val;
		/* relative: */
		int offset;
		/* for IR3_REG_SSA, src registers contain ptr back to
		 * assigning instruction.
		 */
		struct ir3_instruction *instr;
	};

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
			int offset;
			int iim_val;
		} cat6;
		/* for meta-instructions, just used to hold extra data
		 * before instruction scheduling, etc
		 */
		struct {
			int off;              /* component/offset */
		} fo;
		struct {
			struct ir3_block *if_block, *else_block;
		} flow;
		struct {
			struct ir3_block *block;
		} inout;
		struct {
			int off;              /* offset relative to addr reg */
		} deref;

		/* XXX keep this as big as all other union members! */
		uint32_t info[3];
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
	struct ir3_instruction *next;
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
	unsigned instrs_count, instrs_sz;
	struct ir3_instruction **instrs;
	unsigned baryfs_count, baryfs_sz;
	struct ir3_instruction **baryfs;
	struct ir3_block *block;
	unsigned heap_idx;
	struct ir3_heap_chunk *chunk;
};

struct ir3_block {
	struct ir3 *shader;
	unsigned ntemporaries, ninputs, noutputs;
	/* maps TGSI_FILE_TEMPORARY index back to the assigning instruction: */
	struct ir3_instruction **temporaries;
	struct ir3_instruction **inputs;
	struct ir3_instruction **outputs;
	/* only a single address register: */
	struct ir3_instruction *address;
	struct ir3_block *parent;
	struct ir3_instruction *head;
};

struct ir3 * ir3_create(void);
void ir3_destroy(struct ir3 *shader);
void * ir3_assemble(struct ir3 *shader,
		struct ir3_info *info, uint32_t gpu_id);
void * ir3_alloc(struct ir3 *shader, int sz);

struct ir3_block * ir3_block_create(struct ir3 *shader,
		unsigned ntmp, unsigned nin, unsigned nout);

struct ir3_instruction * ir3_instr_create(struct ir3_block *block,
		int category, opc_t opc);
struct ir3_instruction * ir3_instr_create2(struct ir3_block *block,
		int category, opc_t opc, int nreg);
struct ir3_instruction * ir3_instr_clone(struct ir3_instruction *instr);
const char *ir3_instr_name(struct ir3_instruction *instr);

struct ir3_register * ir3_reg_create(struct ir3_instruction *instr,
		int num, int flags);


static inline bool ir3_instr_check_mark(struct ir3_instruction *instr)
{
	if (instr->flags & IR3_INSTR_MARK)
		return true;  /* already visited */
	instr->flags |= IR3_INSTR_MARK;
	return false;
}

static inline void ir3_clear_mark(struct ir3 *shader)
{
	/* TODO would be nice to drop the instruction array.. for
	 * new compiler, _clear_mark() is all we use it for, and
	 * we could probably manage a linked list instead..
	 *
	 * Also, we'll probably want to mark instructions within
	 * a block, so tracking the list of instrs globally is
	 * unlikely to be what we want.
	 */
	unsigned i;
	for (i = 0; i < shader->instrs_count; i++) {
		struct ir3_instruction *instr = shader->instrs[i];
		instr->flags &= ~IR3_INSTR_MARK;
	}
}

static inline int ir3_instr_regno(struct ir3_instruction *instr,
		struct ir3_register *reg)
{
	unsigned i;
	for (i = 0; i < instr->regs_count; i++)
		if (reg == instr->regs[i])
			return i;
	return -1;
}


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

static inline bool is_addr(struct ir3_instruction *instr)
{
	return is_meta(instr) && (instr->opc == OPC_META_DEREF);
}

static inline bool writes_addr(struct ir3_instruction *instr)
{
	if (instr->regs_count > 0) {
		struct ir3_register *dst = instr->regs[0];
		return !!(dst->flags & IR3_REG_ADDR);
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

static inline bool reg_gpr(struct ir3_register *r)
{
	if (r->flags & (IR3_REG_CONST | IR3_REG_IMMED | IR3_REG_ADDR))
		return false;
	if ((reg_num(r) == REG_A0) || (reg_num(r) == REG_P0))
		return false;
	return true;
}

/* dump: */
#include <stdio.h>
void ir3_dump(struct ir3 *shader, const char *name,
		struct ir3_block *block /* XXX maybe 'block' ptr should move to ir3? */,
		FILE *f);
void ir3_dump_instr_single(struct ir3_instruction *instr);
void ir3_dump_instr_list(struct ir3_instruction *instr);

/* flatten if/else: */
int ir3_block_flatten(struct ir3_block *block);

/* depth calculation: */
int ir3_delayslots(struct ir3_instruction *assigner,
		struct ir3_instruction *consumer, unsigned n);
void ir3_block_depth(struct ir3_block *block);

/* copy-propagate: */
void ir3_block_cp(struct ir3_block *block);

/* group neightbors and insert mov's to resolve conflicts: */
void ir3_block_group(struct ir3_block *block);

/* scheduling: */
int ir3_block_sched(struct ir3_block *block);

/* register assignment: */
int ir3_block_ra(struct ir3_block *block, enum shader_t type,
		bool frag_coord, bool frag_face);

/* legalize: */
void ir3_block_legalize(struct ir3_block *block,
		bool *has_samp, int *max_bary);

#ifndef ARRAY_SIZE
#  define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

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
