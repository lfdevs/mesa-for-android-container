/** C support code for ir3_parser.y, included in the generated parser. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util/half_float.h"
#include "util/u_math.h"

#include "ir3/instr-a3xx.h"
#include "ir3/ir3.h"
#include "ir3/ir3_shader.h"

#include "ir3_parser.h"

#define swap(a, b)                                                             \
   do {                                                                        \
      __typeof(a) __tmp = (a);                                                 \
      (a) = (b);                                                               \
      (b) = __tmp;                                                             \
   } while (0)

/* ir3 treats the abs/neg flags as separate flags for float vs integer,
 * but in the instruction encoding they are the same thing.  Tracking
 * them separately is only for the benefit of ir3 opt passes, and not
 * required here, so just use the float versions:
 */
#define IR3_REG_ABS    IR3_REG_FABS
#define IR3_REG_NEGATE IR3_REG_FNEG

static pthread_mutex_t ir3_parse_mtx = PTHREAD_MUTEX_INITIALIZER;

static struct ir3_kernel_info *info;
static struct ir3_shader_variant *variant;
/* NOTE the assembler doesn't really use the ir3_block construction
 * like the compiler does.  Everything is treated as one large block.
 * Which might happen to contain flow control.  But since we don't
 * use any of the ir3 backend passes (sched, RA, etc) this doesn't
 * really matter.
 */
static struct ir3_block *block;       /* current shader block */
static struct ir3_instruction *instr; /* current instruction */
static unsigned ip;                   /* current instruction pointer */
static struct hash_table *labels;

static bool is_in_fullnop_section;
static bool is_in_fullsync_section;

void *ir3_parser_dead_ctx;

char *current_line;

static struct {
   unsigned flags;
   unsigned repeat;
   unsigned nop;
} iflags;

static struct {
   unsigned flags;
   unsigned wrmask;
} rflags;

static struct {
   uint32_t reg_address_hi;
   uint32_t reg_address_lo;
   uint32_t reg_tmp;

   uint32_t regs_to_dump[128];
   uint32_t regs_count;
} meta_print_data;

int ir3_yyget_lineno(void);

static void
new_label(const char *name)
{
   ralloc_steal(labels, (void *)name);
   _mesa_hash_table_insert(labels, name, (void *)(uintptr_t)ip);
}

static struct ir3_instruction *
new_instr(opc_t opc)
{
   instr = ir3_instr_create_at_end(block, opc, 4, 6);
   instr->flags = iflags.flags;
   instr->repeat = iflags.repeat;
   instr->nop = iflags.nop;
   instr->line = ir3_yyget_lineno();
   iflags.flags = iflags.repeat = iflags.nop = 0;

   if (is_in_fullnop_section) {
      struct ir3_instruction *nop =
         ir3_instr_create_at(ir3_before_instr(instr), OPC_NOP, 0, 0);
      nop->repeat = 5;
      ip++;
   }

   if (is_in_fullsync_section) {
      struct ir3_instruction *nop =
         ir3_instr_create_at(ir3_before_instr(instr), OPC_NOP, 0, 0);
      nop->flags = IR3_INSTR_SS | IR3_INSTR_SY;
      ip++;
   }

   ip++;
   return instr;
}

static void
new_shader(void)
{
   variant->ir = ir3_create(variant->compiler, variant);
   block = ir3_block_create(variant->ir);
   list_addtail(&block->node, &variant->ir->block_list);
   ip = 0;
   labels = _mesa_hash_table_create(variant, _mesa_hash_string,
                                    _mesa_key_string_equal);
   ir3_parser_dead_ctx = ralloc_context(NULL);
}

static type_t
parse_type(const char **type)
{
   if (!strncmp("f16", *type, 3)) {
      *type += 3;
      return TYPE_F16;
   } else if (!strncmp("f32", *type, 3)) {
      *type += 3;
      return TYPE_F32;
   } else if (!strncmp("u16", *type, 3)) {
      *type += 3;
      return TYPE_U16;
   } else if (!strncmp("u32", *type, 3)) {
      *type += 3;
      return TYPE_U32;
   } else if (!strncmp("s16", *type, 3)) {
      *type += 3;
      return TYPE_S16;
   } else if (!strncmp("s32", *type, 3)) {
      *type += 3;
      return TYPE_S32;
   } else if (!strncmp("u8", *type, 2)) {
      *type += 2;
      return TYPE_U8;
   } else if (!strncmp("u8_32", *type, 5)) {
      *type += 5;
      return TYPE_U8_32;
   } else if (!strncmp("u64", *type, 3)) {
      *type += 3;
      return TYPE_ATOMIC_U64;
   } else {
      assert(0); /* shouldn't get here */
      return ~0;
   }
}

static struct ir3_instruction *
parse_type_type(struct ir3_instruction *instr, const char *type_type)
{
   instr->cat1.src_type = parse_type(&type_type);
   instr->cat1.dst_type = parse_type(&type_type);
   return instr;
}

static struct ir3_register *
new_src(int num, unsigned flags)
{
   struct ir3_register *reg;
   flags |= rflags.flags;
   if (num & 0x1)
      flags |= IR3_REG_HALF;
   reg = ir3_src_create(instr, num >> 1, flags);
   reg->wrmask = MAX2(1, rflags.wrmask);
   /* Make sure that relative reads have a size as required, so that
    * aliasing-related regmask checks in collect_info() aren't surprised by
    * 0-length arrays.  We don't need a proper size, because aliases will
    * never be indirectly referenced, anyway.
    */
   if (flags & IR3_REG_RELATIV)
      reg->size = 1;
   rflags.flags = rflags.wrmask = 0;
   return reg;
}

static struct ir3_register *
new_dst(int num, unsigned flags)
{
   struct ir3_register *reg;
   flags |= rflags.flags;
   if (num & 0x1)
      flags |= IR3_REG_HALF;
   reg = ir3_dst_create(instr, num >> 1, flags);
   reg->wrmask = MAX2(1, rflags.wrmask);
   /* Make sure that relative writes have a size as required, so that
    * aliasing-related regmask checks in collect_info() aren't surprised by
    * 0-length arrays.  We don't need a proper size, because aliases will
    * never be indirectly referenced, anyway.
    */
   if (flags & IR3_REG_RELATIV)
      reg->size = 1;
   rflags.flags = rflags.wrmask = 0;
   return reg;
}

static struct ir3_register *
dummy_dst(void)
{
   return new_dst(0, 0);
}

static void
fixup_cat5_s2en(void)
{
   assert(opc_cat(instr->opc) == 5);
   if (!(instr->flags & IR3_INSTR_S2EN))
      return;
   /* For various reasons (ie. mainly to make the .s2en src easier to
    * find, given that various different cat5 tex instructions can have
    * different # of src registers), in ir3 the samp/tex src register
    * is first, rather than last.  So we have to detect this case and
    * fix things up.
    */

   uint32_t s2en_off = instr->srcs_count - 1;
   if (instr->flags & IR3_INSTR_A1EN)
      s2en_off = instr->srcs_count - 2;

   struct ir3_register *s2en_src = instr->srcs[s2en_off];

   if (instr->flags & IR3_INSTR_B)
      assert(!(s2en_src->flags & IR3_REG_HALF));
   else
      assert(s2en_src->flags & IR3_REG_HALF);

   memmove(instr->srcs + 1, instr->srcs, s2en_off * sizeof(instr->srcs[0]));
   instr->srcs[0] = s2en_src;
}

static void
add_const(unsigned reg, unsigned c0, unsigned c1, unsigned c2, unsigned c3)
{
   struct ir3_imm_const_state *imm_state = &variant->imm_state;
   assert((reg & 0x7) == 0);
   int idx =
      reg >> (1 + 2); /* low bit is half vs full, next two bits are swiz */
   if (idx * 4 + 4 > imm_state->size) {
      imm_state->values =
         rerzalloc(variant, imm_state->values, __typeof__(imm_state->values[0]),
                   imm_state->size, idx * 4 + 4);
      for (unsigned i = imm_state->size; i < idx * 4; i++)
         imm_state->values[i] = 0xd0d0d0d0;
      imm_state->size = imm_state->count = idx * 4 + 4;
   }
   imm_state->values[idx * 4 + 0] = c0;
   imm_state->values[idx * 4 + 1] = c1;
   imm_state->values[idx * 4 + 2] = c2;
   imm_state->values[idx * 4 + 3] = c3;
}

static void
add_buf_init_val(uint32_t val)
{
   assert(info->num_bufs > 0);
   unsigned idx = info->num_bufs - 1;

   if (!info->buf_init_data[idx]) {
      unsigned sz = info->buf_sizes[idx] * 4;
      info->buf_init_data[idx] = malloc(sz);
      memset(info->buf_init_data[idx], 0, sz);
   }

   assert(info->buf_init_data_sizes[idx] < info->buf_sizes[idx]);
   info->buf_init_data[idx][info->buf_init_data_sizes[idx]++] = val;
}

static void
add_sysval(unsigned reg, unsigned compmask, gl_system_value sysval)
{
   unsigned n = variant->inputs_count++;
   variant->inputs[n].regid = reg;
   variant->inputs[n].sysval = true;
   variant->inputs[n].slot = sysval;
   variant->inputs[n].compmask = compmask;
   variant->total_in++;
}

static bool
resolve_labels(void)
{
   int instr_ip = 0;
   foreach_instr (instr, &block->instr_list) {
      if (opc_cat(instr->opc) == 0 && instr->cat0.target_label) {
         struct hash_entry *entry =
            _mesa_hash_table_search(labels, instr->cat0.target_label);
         if (!entry) {
            fprintf(stderr, "unknown label %s\n", instr->cat0.target_label);
            return false;
         }
         int target_ip = (uintptr_t)entry->data;
         instr->cat0.immed = target_ip - instr_ip;
      }
      instr_ip++;
   }
   return true;
}

static unsigned
cat6_type_shift()
{
   return util_logbase2(type_size(instr->cat6.type) / 8);
}

#ifdef YYDEBUG
int yydebug;
#endif

extern int yylex(void);
void ir3_yyset_lineno(int _line_number);
void ir3_yyset_input(FILE *f);

int yyparse(void);

static void
yyerror(const char *error)
{
   fprintf(stderr, "error at line %d: %s\n%s\n", ir3_yyget_lineno(), error,
           current_line);
}

/* Needs to be a macro to use YYERROR */
#define illegal_syntax_from(gen_from, error)                                   \
   do {                                                                        \
      if (variant->compiler->gen >= gen_from) {                                \
         yyerror(error);                                                       \
         YYERROR;                                                              \
      }                                                                        \
   } while (0)

struct ir3 *
ir3_parse(struct ir3_shader_variant *v, struct ir3_kernel_info *k, FILE *f)
{
   pthread_mutex_lock(&ir3_parse_mtx);

   ir3_yyset_lineno(1);
   ir3_yyset_input(f);
#ifdef YYDEBUG
   yydebug = 1;
#endif
   info = k;
   variant = v;

   is_in_fullnop_section = false;
   is_in_fullsync_section = false;

   if (yyparse() || !resolve_labels()) {
      ir3_destroy(variant->ir);
      variant->ir = NULL;
   }
   ralloc_free(labels);
   ralloc_free(ir3_parser_dead_ctx);

   struct ir3 *ir = variant->ir;
   pthread_mutex_unlock(&ir3_parse_mtx);
   return ir;
}
