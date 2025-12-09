/*
 * Copyright © 2018 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "aco_ir.h"

#include "util/bitset.h"
#include "util/enum_operators.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <map>
#include <optional>
#include <vector>

namespace aco {
namespace {

struct ra_ctx;
struct DefInfo;

unsigned get_subdword_operand_stride(amd_gfx_level gfx_level, const aco_ptr<Instruction>& instr,
                                     unsigned idx, RegClass rc);
void add_subdword_operand(ra_ctx& ctx, aco_ptr<Instruction>& instr, unsigned idx, unsigned byte,
                          RegClass rc);
void add_subdword_definition(Program* program, aco_ptr<Instruction>& instr, PhysReg reg,
                             bool allow_16bit_write);

struct parallelcopy {
   constexpr parallelcopy(Operand op_, Definition def_, int copy_kill_ = -1)
       : op(op_), def(def_), copy_kill(copy_kill_)
   {}

   Operand op;
   Definition def;

   /* If not negative, this copy is only used by a single operand of the instruction and not after
    * the instruction. There might also be more copy-kill copies or a normal copy with the same
    * operand.
    *
    * update_renames() assumes that the copy's source temporary is still live after the parallelcopy
    * instruction unless there's a normal copy of the temporary.
    *
    * update_renames() also assumes that when a copy-kill copy is added, the register file is before
    * the current instruction but after the parallelcopy instruction.
    */
   int copy_kill;
};

struct assignment {
   PhysReg reg;
   RegClass rc;
   union {
      struct {
         bool assigned : 1;
         bool precolor_affinity : 1;
         bool renamed : 1;
      };
      uint8_t _ = 0;
   };
   uint32_t affinity = 0;
   assignment() = default;
   assignment(PhysReg reg_, RegClass rc_) : reg(reg_), rc(rc_) { assigned = true; }
   void set(const Definition& def)
   {
      assigned = true;
      reg = def.physReg();
      rc = def.regClass();
   }
   void set_precolor_affinity(PhysReg affinity_reg)
   {
      if (assigned || precolor_affinity)
         return;

      precolor_affinity = true;
      reg = affinity_reg;
   }
};

/* Iterator type for making PhysRegInterval compatible with range-based for */
struct PhysRegIterator {
   using difference_type = int;
   using value_type = unsigned;
   using reference = const unsigned&;
   using pointer = const unsigned*;
   using iterator_category = std::bidirectional_iterator_tag;

   PhysReg reg;

   PhysReg operator*() const { return reg; }

   PhysRegIterator& operator++()
   {
      reg.reg_b += 4;
      return *this;
   }

   PhysRegIterator& operator--()
   {
      reg.reg_b -= 4;
      return *this;
   }

   bool operator==(PhysRegIterator oth) const { return reg == oth.reg; }

   bool operator!=(PhysRegIterator oth) const { return reg != oth.reg; }

   bool operator<(PhysRegIterator oth) const { return reg < oth.reg; }
};

struct vector_info {
   vector_info() : is_weak(false), index(0), num_parts(0), parts(NULL) {}
   vector_info(Instruction* instr, unsigned idx, unsigned start = 0, bool weak = false)
       : is_weak(weak), index(idx),
         num_parts(instr->operands.size() - start - (instr_disables_wqm(instr) * 2)),
         parts(instr->operands.begin() + start)
   {
      if (parts[0].isVectorAligned()) {
         num_parts = 1;
         while (parts[num_parts - 1].isVectorAligned())
            num_parts++;
      }
   }

   /* If true, then we should stop trying to form a vector if anything goes wrong. Useful for when
    * the cost of failing does not introduce copies. */
   bool is_weak;
   uint16_t index;
   uint32_t num_parts;
   Operand* parts;
};

struct vector_operand {
   Definition def;
   uint32_t start;
   uint32_t num_part;
};

struct ra_ctx {

   Program* program;
   Block* block = NULL;
   aco::monotonic_buffer_resource memory;
   std::vector<assignment> assignments;
   std::vector<aco::unordered_map<uint32_t, Temp>> renames;
   std::vector<std::pair<uint32_t, PhysReg>> loop_header;
   std::vector<vector_operand> vector_operands;
   aco::unordered_map<uint32_t, Temp> orig_names;
   aco::unordered_map<uint32_t, vector_info> vectors;
   aco::unordered_map<uint32_t, Instruction*> split_vectors;
   aco_ptr<Instruction> pseudo_dummy;
   aco_ptr<Instruction> phi_dummy;
   uint16_t max_used_sgpr = 0;
   uint16_t max_used_vgpr = 0;
   RegisterDemand limit;
   std::bitset<512> war_hint;
   PhysRegIterator rr_sgpr_it;
   PhysRegIterator rr_vgpr_it;

   uint16_t sgpr_bounds;
   uint16_t vgpr_bounds;
   uint16_t num_linear_vgprs;

   ra_test_policy policy;

   ra_ctx(Program* program_, ra_test_policy policy_)
       : program(program_), assignments(program->peekAllocationId()),
         renames(program->blocks.size(), aco::unordered_map<uint32_t, Temp>(memory)),
         orig_names(memory), vectors(memory), split_vectors(memory), policy(policy_)
   {
      pseudo_dummy.reset(create_instruction(aco_opcode::p_parallelcopy, Format::PSEUDO, 0, 0));
      phi_dummy.reset(create_instruction(aco_opcode::p_linear_phi, Format::PSEUDO, 0, 0));
      limit = get_addr_regs_from_waves(program, program->min_waves);

      sgpr_bounds = program->max_reg_demand.sgpr;
      vgpr_bounds = program->max_reg_demand.vgpr;
      num_linear_vgprs = 0;
   }
};

/* Half-open register interval used in "sliding window"-style for-loops */
struct PhysRegInterval {
   PhysReg lo_;
   unsigned size;

   /* Inclusive lower bound */
   PhysReg lo() const { return lo_; }

   /* Exclusive upper bound */
   PhysReg hi() const { return PhysReg{lo() + size}; }

   PhysRegInterval& operator+=(uint32_t stride)
   {
      lo_ = PhysReg{lo_.reg() + stride};
      return *this;
   }

   bool operator!=(const PhysRegInterval& oth) const { return lo_ != oth.lo_ || size != oth.size; }

   /* Construct a half-open interval, excluding the end register */
   static PhysRegInterval from_until(PhysReg first, PhysReg end) { return {first, end - first}; }

   bool contains(PhysReg reg) const { return lo() <= reg && reg < hi(); }

   bool contains(const PhysRegInterval& needle) const
   {
      return needle.lo() >= lo() && needle.hi() <= hi();
   }

   PhysRegIterator begin() const { return {lo_}; }

   PhysRegIterator end() const { return {PhysReg{lo_ + size}}; }
};

bool
intersects(const PhysRegInterval& a, const PhysRegInterval& b)
{
   return a.hi() > b.lo() && b.hi() > a.lo();
}

/* Gets the stride for full (non-subdword) registers */
uint32_t
get_stride(RegClass rc)
{
   if (rc.type() == RegType::vgpr) {
      return 1;
   } else {
      uint32_t size = rc.size();
      if (size == 2) {
         return 2;
      } else if (size >= 4 && util_is_power_of_two_or_zero(size)) {
         return 4;
      } else {
         return 1;
      }
   }
}

PhysRegInterval
get_reg_bounds(ra_ctx& ctx, RegType type, bool linear_vgpr)
{
   uint16_t linear_vgpr_start = ctx.vgpr_bounds - ctx.num_linear_vgprs;
   if (type == RegType::vgpr && linear_vgpr) {
      return PhysRegInterval{PhysReg(256 + linear_vgpr_start), ctx.num_linear_vgprs};
   } else if (type == RegType::vgpr) {
      return PhysRegInterval{PhysReg(256), linear_vgpr_start};
   } else {
      return PhysRegInterval{PhysReg(0), ctx.sgpr_bounds};
   }
}

PhysRegInterval
get_reg_bounds(ra_ctx& ctx, RegClass rc)
{
   return get_reg_bounds(ctx, rc.type(), rc.is_linear_vgpr());
}

struct DefInfo {
   PhysRegInterval bounds;
   uint8_t size;
   uint8_t stride;
   /* Even if stride=4, we might be able to write to the high half instead without preserving the
    * low half. In that case, data_stride=2. */
   uint8_t data_stride;
   RegClass rc;

   DefInfo(ra_ctx& ctx, aco_ptr<Instruction>& instr, RegClass rc_, int operand) : rc(rc_)
   {
      size = rc.size();
      stride = get_stride(rc) * 4;
      data_stride = 0;

      bounds = get_reg_bounds(ctx, rc);

      if (rc.is_subdword() && operand >= 0) {
         /* stride in bytes */
         stride = get_subdword_operand_stride(ctx.program->gfx_level, instr, operand, rc);
      } else if (rc.is_subdword()) {
         get_subdword_definition_info(ctx.program, instr);
      } else if (instr->isMIMG() && instr->mimg().d16 && ctx.program->gfx_level <= GFX9) {
         /* Workaround GFX9 hardware bug for D16 image instructions: FeatureImageGather4D16Bug
          *
          * The register use is not calculated correctly, and the hardware assumes a
          * full dword per component. Don't use the last registers of the register file.
          * Otherwise, the instruction will be skipped.
          *
          * https://reviews.llvm.org/D81172
          */
         bool imageGather4D16Bug = operand == -1 && rc == v2 && instr->mimg().dmask != 0xF;
         assert(ctx.program->gfx_level == GFX9 && "Image D16 on GFX8 not supported.");

         if (imageGather4D16Bug)
            bounds.size -= MAX2(rc.bytes() / 4 - ctx.num_linear_vgprs, 0);
      } else if (instr_info.classes[(int)instr->opcode] == instr_class::valu_pseudo_scalar_trans) {
         /* RDNA4 ISA doc, 7.10. Pseudo-scalar Transcendental ALU ops:
          * - VCC may not be used as a destination
          */
         if (bounds.contains(vcc))
            bounds.size = vcc - bounds.lo();
      }

      if (!data_stride)
         data_stride = stride;
   }

private:
   void get_subdword_definition_info(Program* program, const aco_ptr<Instruction>& instr);
};

class RegisterFile {
public:
   RegisterFile() { regs.fill(0); }

   std::array<uint32_t, 512> regs;
   std::map<uint32_t, std::array<uint32_t, 4>> subdword_regs;

   const uint32_t& operator[](PhysReg index) const { return regs[index]; }

   uint32_t& operator[](PhysReg index) { return regs[index]; }

   unsigned count_zero(PhysRegInterval reg_interval) const
   {
      unsigned res = 0;
      for (PhysReg reg : reg_interval)
         res += !regs[reg];
      return res;
   }

   unsigned count_zero_or_blocked(PhysRegInterval reg_interval) const
   {
      unsigned res = 0;
      for (PhysReg reg : reg_interval)
         res += !regs[reg] || regs[reg] == 0xFFFFFFFF;
      return res;
   }

   /* Returns true if any of the bytes in the given range are allocated or blocked */
   bool test(PhysReg start, unsigned num_bytes) const
   {
      for (PhysReg i = start; i.reg_b < start.reg_b + num_bytes; i = PhysReg(i + 1)) {
         assert(i <= 511);
         if (regs[i] & 0x0FFFFFFF)
            return true;
         if (regs[i] == 0xF0000000) {
            auto it = subdword_regs.find(i);
            assert(it != subdword_regs.end());
            for (unsigned j = i.byte(); i * 4 + j < start.reg_b + num_bytes && j < 4; j++) {
               if (it->second[j])
                  return true;
            }
         }
      }
      return false;
   }

   void block(PhysReg start, RegClass rc)
   {
      if (rc.is_subdword())
         fill_subdword(start, rc.bytes(), 0xFFFFFFFF);
      else
         fill(start, rc.size(), 0xFFFFFFFF);
   }

   void block(PhysRegInterval interval) { fill(interval.lo(), interval.size, 0xFFFFFFFF); }

   void clear(PhysRegInterval interval) { fill(interval.lo(), interval.size, 0); }

   bool is_blocked(PhysReg start) const
   {
      if (regs[start] == 0xFFFFFFFF)
         return true;
      if (regs[start] == 0xF0000000) {
         auto it = subdword_regs.find(start);
         assert(it != subdword_regs.end());
         for (unsigned i = start.byte(); i < 4; i++)
            if (it->second[i] == 0xFFFFFFFF)
               return true;
      }
      return false;
   }

   bool is_empty_or_blocked(PhysReg start) const
   {
      /* Empty is 0, blocked is 0xFFFFFFFF, so to check both we compare the
       * incremented value to 1 */
      if (regs[start] == 0xF0000000) {
         auto it = subdword_regs.find(start);
         assert(it != subdword_regs.end());
         return it->second[start.byte()] + 1 <= 1;
      }
      return regs[start] + 1 <= 1;
   }

   void clear(PhysReg start, RegClass rc)
   {
      if (rc.is_subdword())
         fill_subdword(start, rc.bytes(), 0);
      else
         fill(start, rc.size(), 0);
   }

   void fill_killed_operands(Instruction* instr)
   {
      for (Operand& op : instr->operands) {
         if (op.isPrecolored()) {
            block(op.physReg(), op.regClass());
         } else if (op.isFixed() && op.isFirstKillBeforeDef()) {
            if (op.regClass().is_subdword())
               fill_subdword(op.physReg(), op.bytes(), op.tempId());
            else
               fill(op.physReg(), op.size(), op.tempId());
         }
      }
   }

   void clear(Operand op)
   {
      if (op.isTemp() && !is_empty_or_blocked(op.physReg()))
         assert(get_id(op.physReg()) == op.tempId());
      clear(op.physReg(), op.regClass());
   }

   void fill(Definition def)
   {
      if (def.regClass().is_subdword())
         fill_subdword(def.physReg(), def.bytes(), def.tempId());
      else
         fill(def.physReg(), def.size(), def.tempId());
   }

   void clear(Definition def) { clear(def.physReg(), def.regClass()); }

   unsigned get_id(PhysReg reg) const
   {
      return regs[reg] == 0xF0000000 ? subdword_regs.at(reg)[reg.byte()] : regs[reg];
   }

private:
   void fill(PhysReg start, unsigned size, uint32_t val)
   {
      for (unsigned i = 0; i < size; i++)
         regs[start + i] = val;
   }

   void fill_subdword(PhysReg start, unsigned num_bytes, uint32_t val)
   {
      fill(start, DIV_ROUND_UP(num_bytes, 4), 0xF0000000);
      for (PhysReg i = start; i.reg_b < start.reg_b + num_bytes; i = PhysReg(i + 1)) {
         /* emplace or get */
         std::array<uint32_t, 4>& sub =
            subdword_regs.emplace(i, std::array<uint32_t, 4>{0, 0, 0, 0}).first->second;
         for (unsigned j = i.byte(); i * 4 + j < start.reg_b + num_bytes && j < 4; j++)
            sub[j] = val;

         if (sub == std::array<uint32_t, 4>{0, 0, 0, 0}) {
            subdword_regs.erase(i);
            regs[i] = 0;
         }
      }
   }
};

std::vector<unsigned> find_vars(ra_ctx& ctx, const RegisterFile& reg_file,
                                const PhysRegInterval reg_interval);

/* helper function for debugging */
UNUSED void
print_reg(const RegisterFile& reg_file, PhysReg reg, bool has_adjacent_variable)
{
   if (reg_file[reg] == 0xFFFFFFFF) {
      printf((const char*)u8"☐");
   } else if (reg_file[reg]) {
      const bool show_subdword_alloc = (reg_file[reg] == 0xF0000000);
      if (show_subdword_alloc) {
         auto block_chars = {
            // clang-format off
            u8"?", u8"▘", u8"▝", u8"▀",
            u8"▖", u8"▌", u8"▞", u8"▛",
            u8"▗", u8"▚", u8"▐", u8"▜",
            u8"▄", u8"▙", u8"▟", u8"▉"
            // clang-format on
         };
         unsigned index = 0;
         for (int i = 0; i < 4; ++i) {
            if (reg_file.subdword_regs.at(reg)[i]) {
               index |= 1 << i;
            }
         }
         printf("%s", (const char*)(block_chars.begin()[index]));
      } else {
         /* Indicate filled register slot */
         if (!has_adjacent_variable) {
            printf((const char*)u8"█");
         } else {
            /* Use a slightly shorter box to leave a small gap between adjacent variables */
            printf((const char*)u8"▉");
         }
      }
   } else {
      printf((const char*)u8"·");
   }
}

/* helper function for debugging */
UNUSED void
print_regs(ra_ctx& ctx, PhysRegInterval regs, const RegisterFile& reg_file)
{
   char reg_char = regs.lo().reg() >= 256 ? 'v' : 's';
   const int max_regs_per_line = 64;

   /* print markers */
   printf("       ");
   for (int i = 0; i < std::min<int>(max_regs_per_line, ROUND_DOWN_TO(regs.size, 4)); i += 4) {
      printf("%-3.2u ", i);
   }
   printf("\n");

   /* print usage */
   auto line_begin_it = regs.begin();
   while (line_begin_it != regs.end()) {
      const int regs_in_line =
         std::min<int>(max_regs_per_line, std::distance(line_begin_it, regs.end()));

      if (line_begin_it == regs.begin()) {
         printf("%cgprs: ", reg_char);
      } else {
         printf("  %+4d ", std::distance(regs.begin(), line_begin_it));
      }
      const auto line_end_it = std::next(line_begin_it, regs_in_line);

      for (auto reg_it = line_begin_it; reg_it != line_end_it; ++reg_it) {
         bool has_adjacent_variable =
            (std::next(reg_it) != line_end_it &&
             reg_file[*reg_it] != reg_file[*std::next(reg_it)] && reg_file[*std::next(reg_it)]);
         print_reg(reg_file, *reg_it, has_adjacent_variable);
      }

      line_begin_it = line_end_it;
      printf("\n");
   }

   const unsigned free_regs =
      std::count_if(regs.begin(), regs.end(), [&](auto reg) { return !reg_file[reg]; });
   printf("%u/%u used, %u/%u free\n", regs.size - free_regs, regs.size, free_regs, regs.size);

   /* print assignments ordered by registers */
   std::map<PhysReg, std::pair<unsigned, unsigned>> regs_to_vars; /* maps to byte size and temp id */
   for (unsigned id : find_vars(ctx, reg_file, regs)) {
      const assignment& var = ctx.assignments[id];
      PhysReg reg = var.reg;
      ASSERTED auto inserted = regs_to_vars.emplace(reg, std::make_pair(var.rc.bytes(), id));
      assert(inserted.second);
   }

   for (const auto& reg_and_var : regs_to_vars) {
      const auto& first_reg = reg_and_var.first;
      const auto& size_id = reg_and_var.second;

      printf("%%%u ", size_id.second);
      if (ctx.orig_names.count(size_id.second) &&
          ctx.orig_names[size_id.second].id() != size_id.second) {
         printf("(was %%%d) ", ctx.orig_names[size_id.second].id());
      }
      printf("= %c[%d", reg_char, first_reg.reg() % 256);
      PhysReg last_reg = first_reg.advance(size_id.first - 1);
      if (first_reg.reg() != last_reg.reg()) {
         assert(first_reg.byte() == 0 && last_reg.byte() == 3);
         printf("-%d", last_reg.reg() % 256);
      }
      printf("]");
      if (first_reg.byte() != 0 || last_reg.byte() != 3) {
         printf("[%d:%d]", first_reg.byte() * 8, (last_reg.byte() + 1) * 8);
      }
      printf("\n");
   }
}

bool
is_sgpr_writable_without_side_effects(amd_gfx_level gfx_level, PhysReg reg)
{
   assert(reg < 256);
   bool has_flat_scr_lo_gfx89 = gfx_level >= GFX8 && gfx_level <= GFX9;
   bool has_flat_scr_lo_gfx7_or_xnack_mask = gfx_level <= GFX9;
   return (reg <= vcc_hi || reg == m0) &&
          (!has_flat_scr_lo_gfx89 || (reg != flat_scr_lo && reg != flat_scr_hi)) &&
          (!has_flat_scr_lo_gfx7_or_xnack_mask || (reg != 104 || reg != 105));
}

static bool
convert_bitwise_to_16bit(Instruction* instr)
{
   if (instr->opcode == aco_opcode::v_cndmask_b32) {
      instr->opcode = aco_opcode::v_cndmask_b16;
      instr->format = withoutVOP2(asVOP3(instr->format));
      instr->valu().abs = 0;
      instr->valu().neg = 0;
   } else if (instr->opcode == aco_opcode::v_mov_b32) {
      instr->opcode = aco_opcode::v_mov_b16;
      instr->valu().abs = 0;
      instr->valu().neg = 0;
   } else if (instr->opcode == aco_opcode::v_not_b32) {
      instr->opcode = aco_opcode::v_not_b16;
   } else if (instr->opcode == aco_opcode::v_and_b32) {
      instr->opcode = aco_opcode::v_and_b16;
      instr->format = withoutVOP2(asVOP3(instr->format));
   } else if (instr->opcode == aco_opcode::v_or_b32) {
      instr->opcode = aco_opcode::v_or_b16;
      instr->format = withoutVOP2(asVOP3(instr->format));
   } else if (instr->opcode == aco_opcode::v_xor_b32) {
      instr->opcode = aco_opcode::v_xor_b16;
      instr->format = withoutVOP2(asVOP3(instr->format));
   } else {
      return false;
   }

   return true;
}

unsigned
get_subdword_operand_stride(amd_gfx_level gfx_level, const aco_ptr<Instruction>& instr,
                            unsigned idx, RegClass rc)
{
   assert(gfx_level >= GFX8);
   if (instr->isPseudo()) {
      /* v_readfirstlane_b32 cannot use SDWA */
      if (instr->opcode == aco_opcode::p_as_uniform ||
          instr->opcode == aco_opcode::p_permlane64_shared_vgpr)
         return 4;
      else
         return rc.bytes() % 2 == 0 ? 2 : 1;
   }

   assert(rc.bytes() <= 2);
   if (instr->isVALU()) {
      if (can_use_SDWA(gfx_level, instr, false))
         return rc.bytes();
      if (can_use_opsel(gfx_level, instr->opcode, idx))
         return 2;
      if (instr->isVOP3P())
         return 2;
   }

   switch (instr->opcode) {
   case aco_opcode::v_mov_b32:
   case aco_opcode::v_not_b32:
   case aco_opcode::v_and_b32:
   case aco_opcode::v_or_b32:
   case aco_opcode::v_xor_b32:
   case aco_opcode::v_cndmask_b32:
      return gfx_level >= GFX11 && instr->definitions[0].bytes() <= 2 ? 2 : 4;
   case aco_opcode::v_cvt_f32_ubyte0: return 1;
   case aco_opcode::ds_write_b8:
   case aco_opcode::ds_write_b16: return gfx_level >= GFX9 ? 2 : 4;
   case aco_opcode::buffer_store_byte:
   case aco_opcode::buffer_store_short:
   case aco_opcode::buffer_store_format_d16_x:
   case aco_opcode::flat_store_byte:
   case aco_opcode::flat_store_short:
   case aco_opcode::scratch_store_byte:
   case aco_opcode::scratch_store_short:
   case aco_opcode::global_store_byte:
   case aco_opcode::global_store_short: return gfx_level >= GFX9 ? 2 : 4;
   default: return 4;
   }
}

void
add_subdword_operand(ra_ctx& ctx, aco_ptr<Instruction>& instr, unsigned idx, unsigned byte,
                     RegClass rc)
{
   amd_gfx_level gfx_level = ctx.program->gfx_level;
   if (instr->isPseudo() || byte == 0)
      return;

   assert(rc.bytes() <= 2);
   if (instr->isVALU()) {
      if (instr->opcode == aco_opcode::v_cvt_f32_ubyte0) {
         switch (byte) {
         case 0: instr->opcode = aco_opcode::v_cvt_f32_ubyte0; break;
         case 1: instr->opcode = aco_opcode::v_cvt_f32_ubyte1; break;
         case 2: instr->opcode = aco_opcode::v_cvt_f32_ubyte2; break;
         case 3: instr->opcode = aco_opcode::v_cvt_f32_ubyte3; break;
         }
         return;
      }

      /* use SDWA */
      if (can_use_SDWA(gfx_level, instr, false)) {
         convert_to_SDWA(gfx_level, instr);
         return;
      }

      /* use opsel */
      if (instr->isVOP3P()) {
         assert(byte == 2 && !instr->valu().opsel_lo[idx]);
         instr->valu().opsel_lo[idx] = true;
         instr->valu().opsel_hi[idx] = true;
         return;
      }

      convert_bitwise_to_16bit(instr.get());

      assert(can_use_opsel(gfx_level, instr->opcode, idx));
      instr->valu().opsel[idx] = true;
      return;
   }

   assert(byte == 2);
   if (instr->opcode == aco_opcode::ds_write_b8)
      instr->opcode = aco_opcode::ds_write_b8_d16_hi;
   else if (instr->opcode == aco_opcode::ds_write_b16)
      instr->opcode = aco_opcode::ds_write_b16_d16_hi;
   else if (instr->opcode == aco_opcode::buffer_store_byte)
      instr->opcode = aco_opcode::buffer_store_byte_d16_hi;
   else if (instr->opcode == aco_opcode::buffer_store_short)
      instr->opcode = aco_opcode::buffer_store_short_d16_hi;
   else if (instr->opcode == aco_opcode::buffer_store_format_d16_x)
      instr->opcode = aco_opcode::buffer_store_format_d16_hi_x;
   else if (instr->opcode == aco_opcode::flat_store_byte)
      instr->opcode = aco_opcode::flat_store_byte_d16_hi;
   else if (instr->opcode == aco_opcode::flat_store_short)
      instr->opcode = aco_opcode::flat_store_short_d16_hi;
   else if (instr->opcode == aco_opcode::scratch_store_byte)
      instr->opcode = aco_opcode::scratch_store_byte_d16_hi;
   else if (instr->opcode == aco_opcode::scratch_store_short)
      instr->opcode = aco_opcode::scratch_store_short_d16_hi;
   else if (instr->opcode == aco_opcode::global_store_byte)
      instr->opcode = aco_opcode::global_store_byte_d16_hi;
   else if (instr->opcode == aco_opcode::global_store_short)
      instr->opcode = aco_opcode::global_store_short_d16_hi;
   else
      UNREACHABLE("Something went wrong: Impossible register assignment.");
   return;
}

void
DefInfo::get_subdword_definition_info(Program* program, const aco_ptr<Instruction>& instr)
{
   amd_gfx_level gfx_level = program->gfx_level;
   assert(gfx_level >= GFX8);

   stride = rc.bytes() % 2 == 0 ? 2 : 1;

   if (instr->isPseudo()) {
      if (instr->opcode == aco_opcode::p_interp_gfx11 ||
          instr->opcode == aco_opcode::p_permlane64_shared_vgpr) {
         rc = RegClass(RegType::vgpr, rc.size());
         stride = 4;
      }
      return;
   }

   if (instr->isVALU()) {
      if (rc.bytes() == 3)
         rc = v1;

      if (can_use_SDWA(gfx_level, instr, false))
         return;

      rc = instr_is_16bit(gfx_level, instr->opcode) ? v2b : v1;
      stride = 4;
      if (instr->opcode == aco_opcode::v_fma_mixlo_f16 ||
          can_use_opsel(gfx_level, instr->opcode, -1)) {
         data_stride = 2;
         stride = rc == v2b ? 2 : stride;
      } else if ((instr->opcode == aco_opcode::v_cndmask_b32 ||
                  instr->opcode == aco_opcode::v_mov_b32 ||
                  instr->opcode == aco_opcode::v_not_b32 ||
                  instr->opcode == aco_opcode::v_and_b32 || instr->opcode == aco_opcode::v_or_b32 ||
                  instr->opcode == aco_opcode::v_xor_b32) &&
                 program->gfx_level >= GFX11) {
         /* Convert to 16bit opcode on demand. */
         rc = v2b;
         data_stride = 2;
         stride = 2;
      }
      return;
   }

   switch (instr->opcode) {
   case aco_opcode::v_interp_p2_f16: return;
   /* D16 loads with _hi version */
   case aco_opcode::ds_read_u8_d16:
   case aco_opcode::ds_read_i8_d16:
   case aco_opcode::ds_read_u16_d16:
   case aco_opcode::flat_load_ubyte_d16:
   case aco_opcode::flat_load_sbyte_d16:
   case aco_opcode::flat_load_short_d16:
   case aco_opcode::global_load_ubyte_d16:
   case aco_opcode::global_load_sbyte_d16:
   case aco_opcode::global_load_short_d16:
   case aco_opcode::scratch_load_ubyte_d16:
   case aco_opcode::scratch_load_sbyte_d16:
   case aco_opcode::scratch_load_short_d16:
   case aco_opcode::buffer_load_ubyte_d16:
   case aco_opcode::buffer_load_sbyte_d16:
   case aco_opcode::buffer_load_short_d16:
   case aco_opcode::buffer_load_format_d16_x: {
      assert(gfx_level >= GFX9);
      if (program->dev.sram_ecc_enabled) {
         rc = v1;
         stride = 4;
         data_stride = 2;
      } else {
         stride = 2;
      }
      return;
   }
   /* 3-component D16 loads */
   case aco_opcode::buffer_load_format_d16_xyz:
   case aco_opcode::tbuffer_load_format_d16_xyz: {
      assert(gfx_level >= GFX9);
      stride = 4;
      if (program->dev.sram_ecc_enabled)
         rc = v2;
      return;
   }
   default: break;
   }

   stride = 4;
   if (instr->isMIMG() && instr->mimg().d16 && !program->dev.sram_ecc_enabled)
      assert(gfx_level >= GFX9);
   else
      rc = RegClass(RegType::vgpr, rc.size());
}

void
add_subdword_definition(Program* program, aco_ptr<Instruction>& instr, PhysReg reg,
                        bool allow_16bit_write)
{
   if (instr->isPseudo())
      return;

   if (instr->isVALU()) {
      amd_gfx_level gfx_level = program->gfx_level;
      assert(instr->definitions[0].bytes() <= 2);

      if (reg.byte() == 0 && allow_16bit_write && instr_is_16bit(gfx_level, instr->opcode))
         return;

      /* use SDWA */
      if (can_use_SDWA(gfx_level, instr, false)) {
         convert_to_SDWA(gfx_level, instr);
         return;
      }

      assert(allow_16bit_write);

      if (instr->opcode == aco_opcode::v_fma_mixlo_f16) {
         instr->opcode = aco_opcode::v_fma_mixhi_f16;
         return;
      }

      if (convert_bitwise_to_16bit(instr.get())) {
         if (reg.byte() == 0)
            return;
      }

      /* use opsel */
      assert(reg.byte() == 2);
      assert(can_use_opsel(gfx_level, instr->opcode, -1));
      instr->valu().opsel[3] = true; /* dst in high half */
      return;
   }

   if (reg.byte() == 0)
      return;
   else if (instr->opcode == aco_opcode::v_interp_p2_f16)
      instr->opcode = aco_opcode::v_interp_p2_hi_f16;
   else if (instr->opcode == aco_opcode::buffer_load_ubyte_d16)
      instr->opcode = aco_opcode::buffer_load_ubyte_d16_hi;
   else if (instr->opcode == aco_opcode::buffer_load_sbyte_d16)
      instr->opcode = aco_opcode::buffer_load_sbyte_d16_hi;
   else if (instr->opcode == aco_opcode::buffer_load_short_d16)
      instr->opcode = aco_opcode::buffer_load_short_d16_hi;
   else if (instr->opcode == aco_opcode::buffer_load_format_d16_x)
      instr->opcode = aco_opcode::buffer_load_format_d16_hi_x;
   else if (instr->opcode == aco_opcode::flat_load_ubyte_d16)
      instr->opcode = aco_opcode::flat_load_ubyte_d16_hi;
   else if (instr->opcode == aco_opcode::flat_load_sbyte_d16)
      instr->opcode = aco_opcode::flat_load_sbyte_d16_hi;
   else if (instr->opcode == aco_opcode::flat_load_short_d16)
      instr->opcode = aco_opcode::flat_load_short_d16_hi;
   else if (instr->opcode == aco_opcode::scratch_load_ubyte_d16)
      instr->opcode = aco_opcode::scratch_load_ubyte_d16_hi;
   else if (instr->opcode == aco_opcode::scratch_load_sbyte_d16)
      instr->opcode = aco_opcode::scratch_load_sbyte_d16_hi;
   else if (instr->opcode == aco_opcode::scratch_load_short_d16)
      instr->opcode = aco_opcode::scratch_load_short_d16_hi;
   else if (instr->opcode == aco_opcode::global_load_ubyte_d16)
      instr->opcode = aco_opcode::global_load_ubyte_d16_hi;
   else if (instr->opcode == aco_opcode::global_load_sbyte_d16)
      instr->opcode = aco_opcode::global_load_sbyte_d16_hi;
   else if (instr->opcode == aco_opcode::global_load_short_d16)
      instr->opcode = aco_opcode::global_load_short_d16_hi;
   else if (instr->opcode == aco_opcode::ds_read_u8_d16)
      instr->opcode = aco_opcode::ds_read_u8_d16_hi;
   else if (instr->opcode == aco_opcode::ds_read_i8_d16)
      instr->opcode = aco_opcode::ds_read_i8_d16_hi;
   else if (instr->opcode == aco_opcode::ds_read_u16_d16)
      instr->opcode = aco_opcode::ds_read_u16_d16_hi;
   else
      UNREACHABLE("Something went wrong: Impossible register assignment.");
}

void
adjust_max_used_regs(ra_ctx& ctx, RegClass rc, unsigned reg)
{
   uint16_t max_addressible_sgpr = ctx.limit.sgpr;
   unsigned size = rc.size();
   if (rc.type() == RegType::vgpr) {
      assert(reg >= 256);
      uint16_t hi = reg - 256 + size - 1;
      assert(hi <= 255);
      ctx.max_used_vgpr = std::max(ctx.max_used_vgpr, hi);
   } else if (reg + rc.size() <= max_addressible_sgpr) {
      uint16_t hi = reg + size - 1;
      ctx.max_used_sgpr = std::max(ctx.max_used_sgpr, std::min(hi, max_addressible_sgpr));
   }
}

void
update_renames(ra_ctx& ctx, RegisterFile& reg_file, std::vector<parallelcopy>& parallelcopies,
               aco_ptr<Instruction>& instr, bool fill_operands = false, bool clear_operands = true,
               bool never_rename = false)
{
   /* clear operands */
   if (clear_operands) {
      for (parallelcopy& copy : parallelcopies) {
         /* the definitions with id are not from this function and already handled */
         if (copy.def.isTemp() || copy.copy_kill >= 0)
            continue;
         reg_file.clear(copy.op);
      }
   }

   /* allocate id's and rename operands: this is done transparently here */
   auto it = parallelcopies.begin();
   while (it != parallelcopies.end()) {
      if (it->def.isTemp()) {
         ++it;
         continue;
      }

      bool is_copy_kill = it->copy_kill >= 0;

      /* check if we moved a definition: change the register and remove copy */
      bool is_def = false;
      for (Definition& def : instr->definitions) {
         if (def.isTemp() && def.getTemp() == it->op.getTemp()) {
            assert(!is_copy_kill);
            // FIXME: ensure that the definition can use this reg
            def.setFixed(it->def.physReg());
            reg_file.fill(def);
            ctx.assignments[def.tempId()].reg = def.physReg();
            it = parallelcopies.erase(it);
            is_def = true;
            break;
         }
      }
      if (is_def)
         continue;

      /* check if we moved a vector-operand */
      for (vector_operand& vec : ctx.vector_operands) {
         if (vec.def.getTemp() == it->op.getTemp()) {
            vec.def.setFixed(it->def.physReg());
            reg_file.fill(vec.def);
            ctx.assignments[vec.def.tempId()].reg = vec.def.physReg();
            it = parallelcopies.erase(it);
            is_def = true;
         }
      }
      if (is_def)
         continue;

      /* Check if we moved another parallelcopy definition. We use a different path for copy-kill
       * copies, since they are able to exist alongside a normal copy with the same operand.
       */
      for (parallelcopy& other : parallelcopies) {
         if (!other.def.isTemp())
            continue;
         if (is_copy_kill && it->op.getTemp() == other.def.getTemp()) {
            it->op = other.op;
            break;
         } else if (it->op.getTemp() == other.def.getTemp()) {
            other.def.setFixed(it->def.physReg());
            ctx.assignments[other.def.tempId()].reg = other.def.physReg();
            it = parallelcopies.erase(it);
            is_def = true;
            /* check if we moved an operand, again */
            bool fill = true;
            for (Operand& op : instr->operands) {
               if (op.isTemp() && op.tempId() == other.def.tempId()) {
                  // FIXME: ensure that the operand can use this reg
                  if (op.isFixed())
                     op.setFixed(other.def.physReg());
                  fill = !op.isKillBeforeDef() || fill_operands;
               }
            }
            if (fill)
               reg_file.fill(other.def);
            break;
         }
      }
      if (is_def)
         continue;

      parallelcopy& copy = *it;
      copy.def.setTemp(ctx.program->allocateTmp(copy.def.regClass()));
      ctx.assignments.emplace_back(copy.def.physReg(), copy.def.regClass());
      assert(ctx.assignments.size() == ctx.program->peekAllocationId());

      /* Check if we moved an operand:
       * For copy-kill operands, use the current Operand name so that kill flags stay correct.
       */
      Operand copy_op = is_copy_kill ? instr->operands[copy.copy_kill] : it->op;
      bool first[2] = {true, true};
      bool fill = !is_copy_kill;
      for (unsigned i = 0; i < instr->operands.size(); i++) {
         Operand& op = instr->operands[i];
         if (!op.isTemp())
            continue;
         if (op.tempId() == copy_op.tempId()) {
            /* only rename precolored operands if the copy-location matches */
            bool omit_renaming = op.isPrecolored() && op.physReg() != copy.def.physReg();
            omit_renaming |= is_copy_kill && i != (unsigned)copy.copy_kill;
            omit_renaming |= never_rename;

            /* If this is a copy-kill, then the renamed operand is killed since we don't rename any
             * uses in other instructions. If it's a normal copy, then this operand is killed if we
             * don't rename it since any future uses will be renamed to use the copy definition. */
            bool kill =
               op.isKill() || (omit_renaming && !is_copy_kill) || (!omit_renaming && is_copy_kill);

            /* Fix the kill flags */
            if (first[omit_renaming])
               op.setFirstKill(kill);
            else
               op.setKill(kill);
            first[omit_renaming] = false;

            if (omit_renaming)
               continue;

            op.setTemp(copy.def.getTemp());
            if (op.isFixed())
               op.setFixed(copy.def.physReg());

            /* Copy-kill or precolored operand parallelcopies are only added when setting up
             * operands.
             */
            assert(!op.isPrecolored() || fill_operands);
            assert(!is_copy_kill || fill_operands);
            fill = !op.isKillBeforeDef() || fill_operands;
         }
      }

      /* Apply changes to register file. */
      if (fill)
         reg_file.fill(copy.def);

      ++it;
   }
}

std::optional<PhysReg>
get_reg_simple(ra_ctx& ctx, const RegisterFile& reg_file, DefInfo info)
{
   PhysRegInterval bounds = info.bounds;
   uint32_t size = info.size;
   uint32_t stride = DIV_ROUND_UP(info.stride, 4);
   RegClass rc = info.rc;

   if (stride < size && !rc.is_subdword()) {
      DefInfo new_info = info;
      new_info.stride = info.stride * 2;
      if (size % (stride * 2) == 0) {
         std::optional<PhysReg> res = get_reg_simple(ctx, reg_file, new_info);
         if (res)
            return res;
      }
   }

   PhysRegIterator& rr_it = rc.type() == RegType::vgpr ? ctx.rr_vgpr_it : ctx.rr_sgpr_it;
   if (stride == 1) {
      if (rr_it != bounds.begin() && bounds.contains(rr_it.reg)) {
         assert(bounds.begin() < rr_it);
         assert(rr_it < bounds.end());
         info.bounds = PhysRegInterval::from_until(rr_it.reg, bounds.hi());
         std::optional<PhysReg> res = get_reg_simple(ctx, reg_file, info);
         if (res)
            return res;
         bounds = PhysRegInterval::from_until(bounds.lo(), rr_it.reg);
      }
   }

   auto is_free = [&](PhysReg reg_index)
   { return reg_file[reg_index] == 0 && !ctx.war_hint[reg_index]; };

   for (PhysRegInterval reg_win = {bounds.lo(), size}; reg_win.hi() <= bounds.hi();
        reg_win += stride) {
      if (std::all_of(reg_win.begin(), reg_win.end(), is_free)) {
         if (stride == 1) {
            PhysRegIterator new_rr_it{PhysReg{reg_win.lo() + size}};
            if (new_rr_it < bounds.end())
               rr_it = new_rr_it;
         }
         adjust_max_used_regs(ctx, rc, reg_win.lo());
         return reg_win.lo();
      }
   }

   /* do this late because using the upper bytes of a register can require
    * larger instruction encodings or copies
    * TODO: don't do this in situations where it doesn't benefit */
   if (rc.is_subdword()) {
      for (const std::pair<const uint32_t, std::array<uint32_t, 4>>& entry :
           reg_file.subdword_regs) {
         assert(reg_file[PhysReg{entry.first}] == 0xF0000000);
         if (!bounds.contains({PhysReg{entry.first}, rc.size()}))
            continue;

         auto it = entry.second.begin();
         for (unsigned i = 0; i < 4; i += info.stride) {
            /* check if there's a block of free bytes large enough to hold the register */
            bool reg_found =
               std::all_of(std::next(it, i), std::next(it, std::min(4u, i + rc.bytes())),
                           [](unsigned v) { return v == 0; });

            /* check if also the neighboring reg is free if needed */
            if (reg_found && i + rc.bytes() > 4)
               reg_found = (reg_file[PhysReg{entry.first + 1}] == 0);

            if (reg_found) {
               PhysReg res{entry.first};
               res.reg_b += i;
               adjust_max_used_regs(ctx, rc, entry.first);
               return res;
            }
         }
      }
   }

   return {};
}

/* collect variables from a register area */
std::vector<unsigned>
find_vars(ra_ctx& ctx, const RegisterFile& reg_file, const PhysRegInterval reg_interval)
{
   std::vector<unsigned> vars;
   for (PhysReg j : reg_interval) {
      if (reg_file.is_blocked(j))
         continue;
      if (reg_file[j] == 0xF0000000) {
         for (unsigned k = 0; k < 4; k++) {
            unsigned id = reg_file.subdword_regs.at(j)[k];
            if (id && (vars.empty() || id != vars.back()))
               vars.emplace_back(id);
         }
      } else {
         unsigned id = reg_file[j];
         if (id && (vars.empty() || id != vars.back()))
            vars.emplace_back(id);
      }
   }
   return vars;
}

/* collect variables from a register area and clear reg_file
 * variables are sorted in decreasing size and
 * increasing assigned register
 */
std::vector<unsigned>
collect_vars(ra_ctx& ctx, RegisterFile& reg_file, const PhysRegInterval reg_interval)
{
   std::vector<unsigned> ids = find_vars(ctx, reg_file, reg_interval);
   std::sort(ids.begin(), ids.end(),
             [&](unsigned a, unsigned b)
             {
                assignment& var_a = ctx.assignments[a];
                assignment& var_b = ctx.assignments[b];
                return var_a.rc.bytes() > var_b.rc.bytes() ||
                       (var_a.rc.bytes() == var_b.rc.bytes() && var_a.reg < var_b.reg);
             });

   for (unsigned id : ids) {
      assignment& var = ctx.assignments[id];
      reg_file.clear(var.reg, var.rc);
   }
   return ids;
}

std::vector<unsigned>
collect_vars_from_bitset(ra_ctx& ctx, RegisterFile& reg_file, const BITSET_DECLARE(set, 512))
{
   std::vector<unsigned> vars, vars2;
   unsigned start, end;
   BITSET_FOREACH_RANGE(start, end, set, 512) {
      PhysRegInterval interval = PhysRegInterval{PhysReg{start}, end - start};
      vars2 = collect_vars(ctx, reg_file, interval);
      vars.insert(vars.end(), vars2.begin(), vars2.end());
   }
   assert(std::unique(vars.begin(), vars.end()) == vars.end());
   return vars;
}

std::optional<PhysReg>
get_reg_for_create_vector_copy(ra_ctx& ctx, RegisterFile& reg_file,
                               std::vector<parallelcopy>& parallelcopies,
                               aco_ptr<Instruction>& instr, const PhysRegInterval def_reg,
                               DefInfo info, unsigned id)
{
   PhysReg reg = def_reg.lo();
   /* dead operand: return position in vector */
   for (unsigned i = 0; i < instr->operands.size(); i++) {
      if (instr->operands[i].isTemp() && instr->operands[i].tempId() == id &&
          instr->operands[i].isKillBeforeDef()) {
         assert(!reg_file.test(reg, instr->operands[i].bytes()));
         if (info.rc.is_subdword() || reg.byte() == 0)
            return reg;
         else
            return {};
      }
      reg.reg_b += instr->operands[i].bytes();
   }

   /* GFX9+ has a VGPR swap instruction. */
   if (ctx.program->gfx_level <= GFX8 || info.rc.type() == RegType::sgpr)
      return {};

   /* check if the previous position was in vector */
   assignment& var = ctx.assignments[id];
   if (def_reg.contains(PhysRegInterval{var.reg, info.size})) {
      reg = def_reg.lo();
      /* try to use the previous register of the operand */
      for (unsigned i = 0; i < instr->operands.size(); i++) {
         if (reg != var.reg) {
            reg.reg_b += instr->operands[i].bytes();
            continue;
         }

         /* check if we can swap positions */
         if (instr->operands[i].isTemp() && instr->operands[i].isFirstKill() &&
             instr->operands[i].regClass() == info.rc) {
            assignment& op = ctx.assignments[instr->operands[i].tempId()];
            /* if everything matches, create parallelcopy for the killed operand */
            if (!intersects(def_reg, PhysRegInterval{op.reg, op.rc.size()}) && op.reg != scc &&
                reg_file.get_id(op.reg) == instr->operands[i].tempId()) {
               Definition pc_def = Definition(reg, info.rc);
               parallelcopies.emplace_back(instr->operands[i], pc_def);
               return op.reg;
            }
         }
         return {};
      }
   }
   return {};
}

bool
get_regs_for_copies(ra_ctx& ctx, RegisterFile& reg_file, std::vector<parallelcopy>& parallelcopies,
                    const std::vector<unsigned>& vars, aco_ptr<Instruction>& instr,
                    const PhysRegInterval def_reg)
{
   /* Variables are sorted from large to small and with increasing assigned register */
   for (unsigned id : vars) {
      assignment& var = ctx.assignments[id];
      PhysRegInterval bounds = get_reg_bounds(ctx, var.rc);
      DefInfo info = DefInfo(ctx, ctx.pseudo_dummy, var.rc, -1);
      uint32_t size = info.size;

      /* check if this is a dead operand, then we can re-use the space from the definition
       * also use the correct stride for sub-dword operands */
      bool is_dead_operand = false;
      std::optional<PhysReg> res;
      if (instr->opcode == aco_opcode::p_create_vector) {
         res =
            get_reg_for_create_vector_copy(ctx, reg_file, parallelcopies, instr, def_reg, info, id);
      } else {
         for (unsigned i = 0; !is_phi(instr) && i < instr->operands.size(); i++) {
            if (instr->operands[i].isTemp() && instr->operands[i].tempId() == id) {
               info = DefInfo(ctx, instr, var.rc, i);
               if (instr->operands[i].isKillBeforeDef()) {
                  info.bounds = def_reg;
                  res = get_reg_simple(ctx, reg_file, info);
                  is_dead_operand = true;
               }
               break;
            }
         }
      }
      if (!res && !def_reg.size) {
         /* If this is before definitions are handled, def_reg may be an empty interval. */
         info.bounds = bounds;
         res = get_reg_simple(ctx, reg_file, info);
      } else if (!res) {
         /* Try to find space within the bounds but outside of the definition */
         info.bounds = PhysRegInterval::from_until(bounds.lo(), MIN2(def_reg.lo(), bounds.hi()));
         res = get_reg_simple(ctx, reg_file, info);
         if (!res && def_reg.hi() <= bounds.hi()) {
            unsigned stride = DIV_ROUND_UP(info.stride, 4);
            unsigned lo = (def_reg.hi() + stride - 1) & ~(stride - 1);
            info.bounds = PhysRegInterval::from_until(PhysReg{lo}, bounds.hi());
            res = get_reg_simple(ctx, reg_file, info);
         }
      }

      if (res) {
         /* mark the area as blocked */
         reg_file.block(*res, var.rc);

         /* create parallelcopy pair (without definition id) */
         Temp tmp = Temp(id, var.rc);
         Operand pc_op = Operand(tmp);
         pc_op.setFixed(var.reg);
         Definition pc_def = Definition(*res, pc_op.regClass());
         parallelcopies.emplace_back(pc_op, pc_def);
         continue;
      }

      PhysReg best_pos = bounds.lo();
      unsigned num_moves = 0xFF;
      unsigned num_vars = 0;

      /* we use a sliding window to find potential positions */
      unsigned stride = DIV_ROUND_UP(info.stride, 4);
      for (PhysRegInterval reg_win{bounds.lo(), size}; reg_win.hi() <= bounds.hi();
           reg_win += stride) {
         if (!is_dead_operand && intersects(reg_win, def_reg))
            continue;

         /* second, check that we have at most k=num_moves elements in the window
          * and no element is larger than the currently processed one */
         unsigned k = 0;
         unsigned n = 0;
         unsigned last_var = 0;
         bool found = true;
         for (PhysReg j : reg_win) {
            if (reg_file[j] == 0 || reg_file[j] == last_var)
               continue;

            if (reg_file.is_blocked(j) || k > num_moves) {
               found = false;
               break;
            }
            if (reg_file[j] == 0xF0000000) {
               k += 1;
               n++;
               continue;
            }
            /* we cannot split live ranges of linear vgprs */
            if (ctx.assignments[reg_file[j]].rc.is_linear_vgpr()) {
               found = false;
               break;
            }
            bool is_kill = false;
            for (const Operand& op : instr->operands) {
               if (op.isTemp() && op.isKillBeforeDef() && op.tempId() == reg_file[j]) {
                  is_kill = true;
                  break;
               }
            }
            if (!is_kill && ctx.assignments[reg_file[j]].rc.size() >= size) {
               found = false;
               break;
            }

            k += ctx.assignments[reg_file[j]].rc.size();
            last_var = reg_file[j];
            n++;
            if (k > num_moves || (k == num_moves && n <= num_vars)) {
               found = false;
               break;
            }
         }

         if (found) {
            best_pos = reg_win.lo();
            num_moves = k;
            num_vars = n;
         }
      }

      /* FIXME: we messed up and couldn't find space for the variables to be copied */
      if (num_moves == 0xFF)
         return false;

      PhysRegInterval reg_win{best_pos, size};

      /* collect variables and block reg file */
      std::vector<unsigned> new_vars = collect_vars(ctx, reg_file, reg_win);

      /* mark the area as blocked */
      reg_file.block(reg_win.lo(), var.rc);
      adjust_max_used_regs(ctx, var.rc, reg_win.lo());

      if (!get_regs_for_copies(ctx, reg_file, parallelcopies, new_vars, instr, def_reg))
         return false;

      /* create parallelcopy pair (without definition id) */
      Temp tmp = Temp(id, var.rc);
      Operand pc_op = Operand(tmp);
      pc_op.setFixed(var.reg);
      Definition pc_def = Definition(reg_win.lo(), pc_op.regClass());
      parallelcopies.emplace_back(pc_op, pc_def);
   }

   return true;
}

std::optional<PhysReg>
get_reg_impl(ra_ctx& ctx, const RegisterFile& reg_file, std::vector<parallelcopy>& parallelcopies,
             const DefInfo& info, aco_ptr<Instruction>& instr)
{
   const PhysRegInterval& bounds = info.bounds;
   uint32_t size = info.size;
   uint32_t stride = DIV_ROUND_UP(info.stride, 4);
   RegClass rc = info.rc;

   /* check how many free regs we have */
   unsigned regs_free = reg_file.count_zero(get_reg_bounds(ctx, rc));

   /* mark and count killed operands */
   unsigned killed_ops = 0;
   std::bitset<256> is_killed_operand; /* per-register */
   std::bitset<256> is_precolored;     /* per-register */
   for (unsigned j = 0; !is_phi(instr) && j < instr->operands.size(); j++) {
      Operand& op = instr->operands[j];
      if (op.isTemp() && op.isPrecolored() && !op.isFirstKillBeforeDef() &&
          bounds.contains(op.physReg())) {
         for (unsigned i = 0; i < op.size(); ++i) {
            is_precolored[(op.physReg() & 0xff) + i] = true;
         }
      }
      if (op.isTemp() && op.isFirstKillBeforeDef() && bounds.contains(op.physReg()) &&
          !reg_file.test(PhysReg{op.physReg().reg()}, align(op.bytes() + op.physReg().byte(), 4))) {
         assert(op.isFixed());

         for (unsigned i = 0; i < op.size(); ++i) {
            is_killed_operand[(op.physReg() & 0xff) + i] = true;
         }

         killed_ops += op.getTemp().size();
      }
   }
   for (unsigned j = 0; !is_phi(instr) && j < instr->definitions.size(); j++) {
      Definition& def = instr->definitions[j];
      if (def.isTemp() && def.isPrecolored() && bounds.contains(def.physReg())) {
         for (unsigned i = 0; i < def.size(); ++i) {
            is_precolored[(def.physReg() & 0xff) + i] = true;
         }
      }
   }

   assert((regs_free + ctx.num_linear_vgprs) >= size);

   /* we might have to move dead operands to dst in order to make space */
   unsigned op_moves = 0;

   if (size > (regs_free - killed_ops))
      op_moves = size - (regs_free - killed_ops);

   /* find the best position to place the definition */
   PhysRegInterval best_win = {bounds.lo(), size};
   unsigned num_moves = 0xFF;
   unsigned num_vars = 0;

   /* we use a sliding window to check potential positions */
   for (PhysRegInterval reg_win = {bounds.lo(), size}; reg_win.hi() <= bounds.hi();
        reg_win += stride) {
      /* first check if the register window starts in the middle of an
       * allocated variable: this is what we have to fix to allow for
       * num_moves > size */
      if (reg_win.lo() > bounds.lo() && !reg_file.is_empty_or_blocked(reg_win.lo()) &&
          reg_file.get_id(reg_win.lo()) == reg_file.get_id(reg_win.lo().advance(-1)))
         continue;
      if (reg_win.hi() < bounds.hi() && !reg_file.is_empty_or_blocked(reg_win.hi().advance(-1)) &&
          reg_file.get_id(reg_win.hi().advance(-1)) == reg_file.get_id(reg_win.hi()))
         continue;

      /* second, check that we have at most k=num_moves elements in the window
       * and no element is larger than the currently processed one */
      unsigned k = op_moves;
      unsigned n = 0;
      unsigned remaining_op_moves = op_moves;
      unsigned last_var = 0;
      bool found = true;
      bool aligned = rc == RegClass::v4 && reg_win.lo() % 4 == 0;
      for (const PhysReg j : reg_win) {
         /* dead operands effectively reduce the number of estimated moves */
         if (is_killed_operand[j & 0xFF]) {
            if (remaining_op_moves) {
               k--;
               remaining_op_moves--;
            }
            continue;
         }
         if (is_precolored[j & 0xFF] || reg_file[j] == 0xFFFFFFFF) {
            found = false;
            break;
         }

         if (reg_file[j] == 0 || reg_file[j] == last_var)
            continue;

         if (reg_file[j] == 0xF0000000) {
            k += 1;
            n++;
            continue;
         }

         if (ctx.assignments[reg_file[j]].rc.size() >= size) {
            found = false;
            break;
         }

         /* we cannot split live ranges of linear vgprs */
         if (ctx.assignments[reg_file[j]].rc.is_linear_vgpr()) {
            found = false;
            break;
         }

         k += ctx.assignments[reg_file[j]].rc.size();
         n++;
         last_var = reg_file[j];
      }

      if (!found || k > num_moves)
         continue;
      if (k == num_moves && n < num_vars)
         continue;
      if (!aligned && k == num_moves && n == num_vars)
         continue;

      if (found) {
         best_win = reg_win;
         num_moves = k;
         num_vars = n;
      }
   }

   if (num_moves == 0xFF)
      return {};

   /* now, we figured the placement for our definition */
   RegisterFile tmp_file(reg_file);

   /* p_create_vector: also re-place killed operands in the definition space */
   if (instr->opcode == aco_opcode::p_create_vector)
      tmp_file.fill_killed_operands(instr.get());

   std::vector<unsigned> vars = collect_vars(ctx, tmp_file, best_win);

   /* re-enable killed operands */
   if (!is_phi(instr) && instr->opcode != aco_opcode::p_create_vector)
      tmp_file.fill_killed_operands(instr.get());

   std::vector<parallelcopy> pc;
   if (!get_regs_for_copies(ctx, tmp_file, pc, vars, instr, best_win))
      return {};

   parallelcopies.insert(parallelcopies.end(), pc.begin(), pc.end());

   adjust_max_used_regs(ctx, rc, best_win.lo());
   return best_win.lo();
}

bool
get_reg_specified(ra_ctx& ctx, const RegisterFile& reg_file, RegClass rc,
                  aco_ptr<Instruction>& instr, PhysReg reg, int operand)
{
   /* catch out-of-range registers */
   if (reg >= PhysReg{512})
      return false;

   DefInfo info(ctx, instr, rc, operand);

   if (reg.reg_b % info.data_stride)
      return false;

   assert(util_is_power_of_two_nonzero(info.stride));
   reg.reg_b &= ~(info.stride - 1);

   PhysRegInterval reg_win = {PhysReg(reg.reg()), info.rc.size()};
   PhysRegInterval vcc_win = {vcc, 2};
   /* VCC is outside the bounds */
   bool is_vcc =
      info.rc.type() == RegType::sgpr && vcc_win.contains(reg_win) && ctx.program->needs_vcc;
   bool is_m0 = info.rc == s1 && reg == m0 && can_write_m0(instr);
   if (!info.bounds.contains(reg_win) && !is_vcc && !is_m0)
      return false;

   if (instr_info.classes[(int)instr->opcode] == instr_class::valu_pseudo_scalar_trans) {
      /* RDNA4 ISA doc, 7.10. Pseudo-scalar Transcendental ALU ops:
       * - VCC may not be used as a destination
       */
      if (vcc_win.contains(reg_win))
         return false;
   }

   if (reg_file.test(reg, info.rc.bytes()))
      return false;

   adjust_max_used_regs(ctx, info.rc, reg_win.lo());
   return true;
}

bool
increase_register_file(ra_ctx& ctx, RegClass rc)
{
   if (rc.type() == RegType::vgpr && ctx.num_linear_vgprs == 0 &&
       ctx.vgpr_bounds < ctx.limit.vgpr) {
      /* If vgpr_bounds is less than max_reg_demand.vgpr, this should be a no-op. */
      update_vgpr_sgpr_demand(
         ctx.program, RegisterDemand(ctx.vgpr_bounds + 1, ctx.program->max_reg_demand.sgpr));

      ctx.vgpr_bounds = ctx.program->max_reg_demand.vgpr;
   } else if (rc.type() == RegType::sgpr && ctx.program->max_reg_demand.sgpr < ctx.limit.sgpr) {
      update_vgpr_sgpr_demand(
         ctx.program, RegisterDemand(ctx.program->max_reg_demand.vgpr, ctx.sgpr_bounds + 1));

      ctx.sgpr_bounds = ctx.program->max_reg_demand.sgpr;
   } else {
      return false;
   }

   return true;
}

struct IDAndRegClass {
   IDAndRegClass(unsigned id_, RegClass rc_) : id(id_), rc(rc_) {}

   unsigned id;
   RegClass rc;
};

struct IDAndInfo {
   IDAndInfo(unsigned id_, DefInfo info_) : id(id_), info(info_) {}

   unsigned id;
   DefInfo info;
};

void
add_rename(ra_ctx& ctx, Temp orig_val, Temp new_val)
{
   ctx.renames[ctx.block->index][orig_val.id()] = new_val;
   ctx.orig_names.emplace(new_val.id(), orig_val);
   ctx.assignments[orig_val.id()].renamed = true;
}

/* Reallocates vars by sorting them and placing each variable after the previous
 * one. If one of the variables has 0xffffffff as an ID, the register assigned
 * for that variable will be returned.
 */
PhysReg
compact_relocate_vars(ra_ctx& ctx, const std::vector<IDAndRegClass>& vars,
                      std::vector<parallelcopy>& parallelcopies, PhysReg start)
{
   /* This function assumes RegisterDemand/live_var_analysis rounds up sub-dword
    * temporary sizes to dwords.
    */
   std::vector<IDAndInfo> sorted;
   for (IDAndRegClass var : vars) {
      DefInfo info(ctx, ctx.pseudo_dummy, var.rc, -1);
      sorted.emplace_back(var.id, info);
   }

   std::sort(
      sorted.begin(), sorted.end(),
      [=, &ctx](const IDAndInfo& a, const IDAndInfo& b)
      {
         unsigned a_stride = MAX2(a.info.stride, 4);
         unsigned b_stride = MAX2(b.info.stride, 4);
         /* Since the SGPR bounds should always be a multiple of two, we can place
          * variables in this order:
          * - the usual 4 SGPR aligned variables
          * - then the 0xffffffff variable
          * - then the unaligned variables
          * - and finally the 2 SGPR aligned variables
          * This way, we should always be able to place variables if the 0xffffffff one
          * had a NPOT size.
          *
          * This also lets us avoid placing the 0xffffffff variable in VCC if it's s1/s2
          * (required for pseudo-scalar transcendental) and places it first if it's a
          * VGPR variable (required for ImageGather4D16Bug).
          */
         assert(a.info.rc.type() != RegType::sgpr || get_reg_bounds(ctx, a.info.rc).size % 2 == 0);
         assert(a_stride == 16 || a_stride == 8 || a_stride == 4);
         assert(b_stride == 16 || b_stride == 8 || b_stride == 4);
         assert(a.info.rc.size() % (a_stride / 4u) == 0);
         assert(b.info.rc.size() % (b_stride / 4u) == 0);
         if ((a_stride == 16) != (b_stride == 16))
            return a_stride > b_stride;
         if (a.id == 0xffffffff || b.id == 0xffffffff)
            return a.id == 0xffffffff;
         if (a_stride != b_stride)
            return a_stride < b_stride;
         return ctx.assignments[a.id].reg < ctx.assignments[b.id].reg;
      });

   PhysReg next_reg = start;
   PhysReg space_reg;
   for (IDAndInfo& var : sorted) {
      next_reg.reg_b = align(next_reg.reg_b, MAX2(var.info.stride, 4));

      /* 0xffffffff is a special variable ID used reserve a space for killed
       * operands and definitions.
       */
      if (var.id != 0xffffffff) {
         if (next_reg != ctx.assignments[var.id].reg) {
            RegClass rc = ctx.assignments[var.id].rc;
            Temp tmp(var.id, rc);

            Operand pc_op(tmp);
            pc_op.setFixed(ctx.assignments[var.id].reg);
            Definition pc_def(next_reg, rc);
            parallelcopies.emplace_back(pc_op, pc_def);
         }
      } else {
         space_reg = next_reg;
      }

      adjust_max_used_regs(ctx, var.info.rc, next_reg);

      next_reg = next_reg.advance(var.info.rc.size() * 4);
   }

   return space_reg;
}

bool
is_vector_intact(ra_ctx& ctx, const RegisterFile& reg_file, const vector_info& vec_info)
{
   unsigned size = 0;
   for (unsigned i = 0; i < vec_info.num_parts; i++)
      size += vec_info.parts[i].bytes();

   PhysReg first{512};
   int offset = 0;
   for (unsigned i = 0; i < vec_info.num_parts; i++) {
      Operand op = vec_info.parts[i];

      if (ctx.assignments[op.tempId()].assigned) {
         PhysReg reg = ctx.assignments[op.tempId()].reg;

         if (first.reg() == 512) {
            PhysRegInterval bounds = get_reg_bounds(ctx, RegType::vgpr, false);
            first = reg.advance(-offset);
            PhysRegInterval vec = PhysRegInterval{first, DIV_ROUND_UP(size, 4)};
            if (!bounds.contains(vec)) /* not enough space for other operands */
               return false;
         } else {
            if (reg != first.advance(offset)) /* not at the best position */
               return false;
         }
      } else {
         /* If there's an unexpected temporary, this operand is unlikely to be
          * placed in the best position.
          */
         if (first.reg() != 512 && reg_file.test(first.advance(offset), op.bytes()))
            return false;
      }

      offset += op.bytes();
   }

   return true;
}

std::optional<PhysReg>
get_reg_vector(ra_ctx& ctx, const RegisterFile& reg_file, Temp temp, aco_ptr<Instruction>& instr,
               int operand)
{
   const vector_info& vec = ctx.vectors[temp.id()];
   if (!vec.is_weak || is_vector_intact(ctx, reg_file, vec)) {
      unsigned our_offset = 0;
      for (unsigned i = 0; i < vec.index; i++)
         our_offset += vec.parts[i].bytes();

      unsigned their_offset = 0;
      /* check for every operand of the vector
       * - whether the operand is assigned and
       * - we can use the register relative to that operand
       */
      for (unsigned i = 0; i < vec.num_parts; i++) {
         const Operand& op = vec.parts[i];
         if (i != vec.index && op.isTemp() && op.getTemp().type() == temp.type() &&
             (ctx.assignments[op.tempId()].assigned ||
              ctx.assignments[op.tempId()].precolor_affinity)) {
            PhysReg reg = ctx.assignments[op.tempId()].reg;
            reg.reg_b += (our_offset - their_offset);
            if (get_reg_specified(ctx, reg_file, temp.regClass(), instr, reg, operand)) {
               ctx.assignments[vec.parts[vec.index].tempId()].set_precolor_affinity(reg);
               return reg;
            }

            /* return if MIMG vaddr components don't remain vector-aligned */
            if (vec.is_weak)
               return {};
         }
         their_offset += op.bytes();
      }

      /* We didn't find a register relative to other vector operands.
       * Try to find new space which fits the whole vector.
       */
      RegClass vec_rc = RegClass::get(temp.type(), their_offset);
      DefInfo info(ctx, ctx.pseudo_dummy, vec_rc, -1);
      std::optional<PhysReg> reg = get_reg_simple(ctx, reg_file, info);
      if (reg) {
         reg->reg_b += our_offset;
         /* make sure to only use byte offset if the instruction supports it */
         if (get_reg_specified(ctx, reg_file, temp.regClass(), instr, *reg, operand)) {
            ctx.assignments[vec.parts[vec.index].tempId()].set_precolor_affinity(reg.value());
            return reg;
         }
      }
   }
   return {};
}

bool
compact_linear_vgprs(ra_ctx& ctx, const RegisterFile& reg_file,
                     std::vector<parallelcopy>& parallelcopies)
{
   PhysRegInterval linear_vgpr_bounds = get_reg_bounds(ctx, RegType::vgpr, true);
   int zeros = reg_file.count_zero(linear_vgpr_bounds);
   if (zeros == 0)
      return false;

   std::vector<IDAndRegClass> vars;
   for (unsigned id : find_vars(ctx, reg_file, linear_vgpr_bounds))
      vars.emplace_back(id, ctx.assignments[id].rc);

   ctx.num_linear_vgprs -= zeros;
   compact_relocate_vars(ctx, vars, parallelcopies, get_reg_bounds(ctx, RegType::vgpr, true).lo());

   return true;
}

/* Allocates a linear VGPR. We allocate them at the end of the register file and keep them separate
 * from normal VGPRs. This is for two reasons:
 * - Because we only ever move linear VGPRs into an empty space or a space previously occupied by a
 *   linear one, we never have to swap a normal VGPR and a linear one.
 * - As linear VGPR's live ranges only start and end on top-level blocks, we never have to move a
 *   linear VGPR in control flow.
 */
PhysReg
alloc_linear_vgpr(ra_ctx& ctx, const RegisterFile& reg_file, aco_ptr<Instruction>& instr,
                  Definition& def,
                  std::vector<parallelcopy>& parallelcopies)
{
   assert(instr->opcode == aco_opcode::p_start_linear_vgpr || instr->opcode == aco_opcode::p_startpgm);
   if (instr->opcode == aco_opcode::p_start_linear_vgpr)
      assert(instr->definitions.size() == 1 && instr->definitions[0].bytes() % 4 == 0);

   RegClass rc = def.regClass();

   /* Try to choose an unused space in the linear VGPR bounds. */
   for (unsigned i = rc.size(); i <= ctx.num_linear_vgprs; i++) {
      PhysReg reg(256 + ctx.vgpr_bounds - i);
      if (!reg_file.test(reg, rc.bytes())) {
         adjust_max_used_regs(ctx, rc, reg);
         return reg;
      }
   }

   PhysRegInterval old_normal_bounds = get_reg_bounds(ctx, RegType::vgpr, false);

   /* Compact linear VGPRs, grow the bounds if necessary, and choose a space at the beginning: */
   compact_linear_vgprs(ctx, reg_file, parallelcopies);

   PhysReg reg(256 + ctx.vgpr_bounds - (ctx.num_linear_vgprs + rc.size()));
   /* Space that was for normal VGPRs, but is now for linear VGPRs. */
   PhysRegInterval new_win = PhysRegInterval::from_until(reg, MAX2(old_normal_bounds.hi(), reg));

   RegisterFile tmp_file(reg_file);
   PhysRegInterval reg_win{reg, rc.size()};
   std::vector<unsigned> blocking_vars = collect_vars(ctx, tmp_file, new_win);

   /* Re-enable killed operands */
   tmp_file.fill_killed_operands(instr.get());

   /* Find new assignments for blocking vars. */
   std::vector<parallelcopy> pc;
   if (!ctx.policy.skip_optimistic_path &&
       get_regs_for_copies(ctx, tmp_file, pc, blocking_vars, instr, reg_win)) {
      parallelcopies.insert(parallelcopies.end(), pc.begin(), pc.end());
   } else {
      /* Fallback algorithm: reallocate all variables at once. */
      std::vector<IDAndRegClass> vars;
      for (unsigned id : find_vars(ctx, reg_file, old_normal_bounds))
         vars.emplace_back(id, ctx.assignments[id].rc);
      compact_relocate_vars(ctx, vars, parallelcopies, PhysReg(256));

      std::vector<IDAndRegClass> killed_op_vars;
      for (Operand& op : instr->operands) {
         if (op.isTemp() && op.isFirstKillBeforeDef() && op.regClass().type() == RegType::vgpr)
            killed_op_vars.emplace_back(op.tempId(), op.regClass());
      }
      compact_relocate_vars(ctx, killed_op_vars, parallelcopies, reg_win.lo());
   }

   /* If this is updated earlier, a killed operand can't be placed inside the definition. */
   ctx.num_linear_vgprs += rc.size();

   adjust_max_used_regs(ctx, rc, reg);
   return reg;
}

bool
should_compact_linear_vgprs(ra_ctx& ctx, const RegisterFile& reg_file)
{
   if (!(ctx.block->kind & block_kind_top_level) || ctx.block->linear_succs.empty())
      return false;

   /* Since we won't be able to copy linear VGPRs to make space when in control flow, we have to
    * ensure in advance that there is enough space for normal VGPRs. */
   unsigned max_vgpr_usage = 0;
   unsigned next_toplevel = ctx.block->index + 1;
   for (; !(ctx.program->blocks[next_toplevel].kind & block_kind_top_level); next_toplevel++) {
      max_vgpr_usage =
         MAX2(max_vgpr_usage, (unsigned)ctx.program->blocks[next_toplevel].register_demand.vgpr);
   }
   max_vgpr_usage =
      MAX2(max_vgpr_usage, (unsigned)ctx.program->blocks[next_toplevel].live_in_demand.vgpr);

   for (unsigned tmp : find_vars(ctx, reg_file, get_reg_bounds(ctx, RegType::vgpr, true)))
      max_vgpr_usage -= ctx.assignments[tmp].rc.size();

   return max_vgpr_usage > get_reg_bounds(ctx, RegType::vgpr, false).size;
}

PhysReg
get_reg(ra_ctx& ctx, const RegisterFile& reg_file, Temp temp,
        std::vector<parallelcopy>& parallelcopies, aco_ptr<Instruction>& instr,
        int operand_index = -1)
{
   /* Note that "temp" might already be filled in the register file if we're making a copy of the
    * temporary.
    */

   auto split_vec = ctx.split_vectors.find(temp.id());
   if (split_vec != ctx.split_vectors.end()) {
      unsigned offset = 0;
      for (Definition def : split_vec->second->definitions) {
         if (ctx.assignments[def.tempId()].affinity) {
            assignment& affinity = ctx.assignments[ctx.assignments[def.tempId()].affinity];
            if (affinity.assigned) {
               PhysReg reg = affinity.reg;
               reg.reg_b -= offset;
               if (get_reg_specified(ctx, reg_file, temp.regClass(), instr, reg, operand_index))
                  return reg;
            }
         }
         offset += def.bytes();
      }
   }

   if (ctx.assignments[temp.id()].affinity) {
      assignment& affinity = ctx.assignments[ctx.assignments[temp.id()].affinity];
      if (affinity.assigned) {
         if (get_reg_specified(ctx, reg_file, temp.regClass(), instr, affinity.reg, operand_index))
            return affinity.reg;
      }
   }
   if (ctx.assignments[temp.id()].precolor_affinity) {
      if (get_reg_specified(ctx, reg_file, temp.regClass(), instr, ctx.assignments[temp.id()].reg,
                            operand_index))
         return ctx.assignments[temp.id()].reg;
   }

   std::optional<PhysReg> res;

   if (ctx.vectors.find(temp.id()) != ctx.vectors.end()) {
      res = get_reg_vector(ctx, reg_file, temp, instr, operand_index);
      if (res)
         return *res;
   }

   if (temp.size() == 1 && operand_index == -1) {
      for (const Operand& op : instr->operands) {
         if (op.isTemp() && op.isFirstKillBeforeDef() && op.regClass() == temp.regClass()) {
            assert(op.isFixed());
            if (op.physReg() == vcc || op.physReg() == vcc_hi)
               continue;
            if (get_reg_specified(ctx, reg_file, temp.regClass(), instr, op.physReg(),
                                  operand_index))
               return op.physReg();
         }
      }
   }

   DefInfo info(ctx, instr, temp.regClass(), operand_index);

   if (!ctx.policy.skip_optimistic_path && !ctx.policy.use_compact_relocate) {
      /* try to find space without live-range splits */
      res = get_reg_simple(ctx, reg_file, info);

      if (res)
         return *res;
   }

   if (!ctx.policy.use_compact_relocate) {
      /* try to find space with live-range splits */
      res = get_reg_impl(ctx, reg_file, parallelcopies, info, instr);

      if (res)
         return *res;
   }

   /* try compacting the linear vgprs to make more space */
   std::vector<parallelcopy> pc;
   if (info.rc.type() == RegType::vgpr && (ctx.block->kind & block_kind_top_level) &&
       compact_linear_vgprs(ctx, reg_file, pc)) {
      parallelcopies.insert(parallelcopies.end(), pc.begin(), pc.end());

      RegisterFile tmp_file(reg_file);
      for (parallelcopy& copy : pc)
         tmp_file.clear(copy.op);

      /* Block these registers in case we try compacting linear VGPRs in the recursive get_reg(). */
      for (parallelcopy& copy : pc)
         tmp_file.block(copy.def.physReg(), copy.def.regClass());

      return get_reg(ctx, tmp_file, temp, parallelcopies, instr, operand_index);
   }

   /* We should only fail here because keeping under the limit would require
    * too many moves. */
   assert(reg_file.count_zero(get_reg_bounds(ctx, info.rc)) >= info.size);

   /* try using more registers */
   if (!increase_register_file(ctx, info.rc)) {
      /* fallback algorithm: reallocate all variables at once (linear VGPRs should already be
       * compact at the end) */
      const PhysRegInterval regs = get_reg_bounds(ctx, info.rc);

      unsigned def_size = info.rc.size();
      std::vector<IDAndRegClass> def_vars;
      for (Definition def : instr->definitions) {
         if (def.isPrecolored()) {
            assert(!regs.contains({def.physReg(), def.size()}));
            continue;
         }
         if (ctx.assignments[def.tempId()].assigned && def.regClass().type() == info.rc.type()) {
            def_size += def.regClass().size();
            def_vars.emplace_back(def.tempId(), def.regClass());
         }
      }

      unsigned killed_op_size = 0;
      std::vector<IDAndRegClass> killed_op_vars;
      for (Operand op : instr->operands) {
         if (op.isPrecolored()) {
            assert(!regs.contains({op.physReg(), op.size()}));
            continue;
         }
         if (op.isTemp() && op.isFirstKillBeforeDef() && op.regClass().type() == info.rc.type()) {
            killed_op_size += op.regClass().size();
            killed_op_vars.emplace_back(op.tempId(), op.regClass());
         }
      }

      /* reallocate passthrough variables and non-killed operands */
      std::vector<IDAndRegClass> vars;
      for (unsigned id : find_vars(ctx, reg_file, regs))
         vars.emplace_back(id, ctx.assignments[id].rc);
      vars.emplace_back(0xffffffff, RegClass(info.rc.type(), MAX2(def_size, killed_op_size)));

      PhysReg space = compact_relocate_vars(ctx, vars, parallelcopies, regs.lo());

      /* reallocate killed operands */
      compact_relocate_vars(ctx, killed_op_vars, parallelcopies, space);

      /* reallocate definitions */
      def_vars.emplace_back(0xffffffff, info.rc);
      return compact_relocate_vars(ctx, def_vars, parallelcopies, space);
   }

   return get_reg(ctx, reg_file, temp, parallelcopies, instr, operand_index);
}

PhysReg
get_reg_create_vector(ra_ctx& ctx, const RegisterFile& reg_file, Temp temp,
                      std::vector<parallelcopy>& parallelcopies, aco_ptr<Instruction>& instr)
{
   RegClass rc = temp.regClass();
   /* create_vector instructions have different costs w.r.t. register coalescing */
   uint32_t size = rc.size();
   uint32_t bytes = rc.bytes();
   uint32_t stride = get_stride(rc);
   PhysRegInterval bounds = get_reg_bounds(ctx, rc);

   // TODO: improve p_create_vector for sub-dword vectors

   PhysReg best_pos{0xFFF};
   unsigned num_moves = 0xFF;
   bool best_avoid = true;
   uint32_t correct_pos_mask = 0;

   /* test for each operand which definition placement causes the least shuffle instructions */
   for (unsigned i = 0, offset = 0; i < instr->operands.size();
        offset += instr->operands[i].bytes(), i++) {
      // TODO: think about, if we can alias live operands on the same register
      if (!instr->operands[i].isTemp() || instr->operands[i].getTemp().type() != rc.type() ||
          reg_file.test(instr->operands[i].physReg(), instr->operands[i].bytes()))
         continue;

      if (offset > instr->operands[i].physReg().reg_b)
         continue;

      unsigned reg_lower = instr->operands[i].physReg().reg_b - offset;
      if (reg_lower % 4)
         continue;
      PhysRegInterval reg_win = {PhysReg{reg_lower / 4}, size};
      unsigned k = 0;

      /* no need to check multiple times */
      if (reg_win.lo() == best_pos)
         continue;

      /* check borders */
      // TODO: this can be improved */
      if (!bounds.contains(reg_win) || reg_win.lo() % stride != 0)
         continue;
      if (reg_win.lo() > bounds.lo() && reg_file[reg_win.lo()] != 0 &&
          reg_file.get_id(reg_win.lo()) == reg_file.get_id(reg_win.lo().advance(-1)))
         continue;
      if (reg_win.hi() < bounds.hi() && reg_file[reg_win.hi().advance(-4)] != 0 &&
          reg_file.get_id(reg_win.hi().advance(-1)) == reg_file.get_id(reg_win.hi()))
         continue;

      /* count variables to be moved and check "avoid" */
      bool avoid = false;
      bool linear_vgpr = false;
      for (PhysReg j : reg_win) {
         if (reg_file[j] != 0) {
            if (reg_file[j] == 0xF0000000) {
               PhysReg reg;
               reg.reg_b = j * 4;
               unsigned bytes_left = bytes - ((unsigned)j - reg_win.lo()) * 4;
               for (unsigned byte_idx = 0; byte_idx < MIN2(bytes_left, 4); byte_idx++, reg.reg_b++)
                  k += reg_file.test(reg, 1);
            } else {
               k += 4;
               linear_vgpr |= ctx.assignments[reg_file[j]].rc.is_linear_vgpr();
            }
         }
         avoid |= ctx.war_hint[j];
      }

      /* we cannot split live ranges of linear vgprs */
      if (linear_vgpr)
         continue;

      if (avoid && !best_avoid)
         continue;

      /* count operands in wrong positions */
      uint32_t correct_pos_mask_new = 0;
      for (unsigned j = 0, offset2 = 0; j < instr->operands.size();
           offset2 += instr->operands[j].bytes(), j++) {
         Operand& op = instr->operands[j];
         if (op.isTemp() && op.physReg().reg_b == reg_win.lo() * 4 + offset2)
            correct_pos_mask_new |= 1 << j;
         else
            k += op.bytes();
      }
      bool aligned = rc == RegClass::v4 && reg_win.lo() % 4 == 0;
      if (k > num_moves || (!aligned && k == num_moves))
         continue;

      best_pos = reg_win.lo();
      num_moves = k;
      best_avoid = avoid;
      correct_pos_mask = correct_pos_mask_new;
   }

   /* too many moves: try the generic get_reg() function */
   if (num_moves >= 2 * bytes) {
      return get_reg(ctx, reg_file, temp, parallelcopies, instr);
   } else if (num_moves > bytes) {
      DefInfo info(ctx, instr, rc, -1);
      std::optional<PhysReg> res = get_reg_simple(ctx, reg_file, info);
      if (res)
         return *res;
   }

   /* re-enable killed operands which are in the wrong position */
   RegisterFile tmp_file(reg_file);
   tmp_file.fill_killed_operands(instr.get());

   for (unsigned i = 0; i < instr->operands.size(); i++) {
      if ((correct_pos_mask >> i) & 1u && instr->operands[i].isKillBeforeDef())
         tmp_file.clear(instr->operands[i]);
   }

   /* collect variables to be moved */
   std::vector<unsigned> vars = collect_vars(ctx, tmp_file, PhysRegInterval{best_pos, size});

   bool success = false;
   std::vector<parallelcopy> pc;
   success = get_regs_for_copies(ctx, tmp_file, pc, vars, instr, PhysRegInterval{best_pos, size});

   if (!success) {
      if (!increase_register_file(ctx, temp.regClass())) {
         /* use the fallback algorithm in get_reg() */
         return get_reg(ctx, reg_file, temp, parallelcopies, instr);
      }
      return get_reg_create_vector(ctx, reg_file, temp, parallelcopies, instr);
   }

   parallelcopies.insert(parallelcopies.end(), pc.begin(), pc.end());
   adjust_max_used_regs(ctx, rc, best_pos);

   return best_pos;
}

void
handle_pseudo(ra_ctx& ctx, const RegisterFile& reg_file, Instruction* instr)
{
   if (instr->format != Format::PSEUDO)
      return;

   /* all instructions which use handle_operands() need this information */
   switch (instr->opcode) {
   case aco_opcode::p_extract_vector:
   case aco_opcode::p_create_vector:
   case aco_opcode::p_split_vector:
   case aco_opcode::p_parallelcopy:
   case aco_opcode::p_start_linear_vgpr: break;
   default: return;
   }

   bool writes_linear = false;
   /* if all definitions are logical vgpr, no need to care for SCC */
   for (Definition& def : instr->definitions) {
      if (def.getTemp().regClass().is_linear())
         writes_linear = true;
   }
   /* if all operands are constant, no need to care either */
   bool reads_linear = false;
   for (Operand& op : instr->operands) {
      if (op.isTemp() && op.getTemp().regClass().is_linear())
         reads_linear = true;
   }

   if (!writes_linear || !reads_linear)
      return;

   instr->pseudo().needs_scratch_reg = true;

   if (!reg_file[scc]) {
      instr->pseudo().scratch_sgpr = scc;
      return;
   }

   int reg = ctx.max_used_sgpr;
   for (; reg >= 0 && reg_file[PhysReg{(unsigned)reg}]; reg--)
      ;
   if (reg < 0) {
      reg = ctx.max_used_sgpr + 1;
      for (; reg < ctx.program->max_reg_demand.sgpr && reg_file[PhysReg{(unsigned)reg}]; reg++)
         ;
   }

   adjust_max_used_regs(ctx, s1, reg);
   instr->pseudo().scratch_sgpr = PhysReg{(unsigned)reg};
}

bool
operand_can_use_reg(amd_gfx_level gfx_level, aco_ptr<Instruction>& instr, unsigned idx, PhysReg reg,
                    RegClass rc)
{
   if (reg.byte()) {
      unsigned stride = get_subdword_operand_stride(gfx_level, instr, idx, rc);
      if (reg.byte() % stride)
         return false;
   }

   switch (instr->format) {
   case Format::SMEM:
      return reg != scc && reg != exec &&
             (reg != m0 || idx == 1 || idx == 3) && /* offset can be m0 */
             (reg != vcc || (instr->definitions.empty() && idx == 2) ||
              gfx_level >= GFX10); /* sdata can be vcc */
   case Format::MUBUF:
   case Format::MTBUF: return idx != 2 || gfx_level < GFX12 || reg != scc;
   case Format::SOPK:
      if (idx == 0 && reg == scc)
         return false;
      FALLTHROUGH;
   case Format::SOP2:
   case Format::SOP1: {
      auto tied_defs = get_tied_defs(instr.get());
      return std::all_of(tied_defs.begin(), tied_defs.end(),
                         [idx](auto op_idx) { return op_idx != idx; }) ||
             is_sgpr_writable_without_side_effects(gfx_level, reg);
   }
   default:
      // TODO: there are more instructions with restrictions on registers
      return true;
   }
}

void
handle_fixed_operands(ra_ctx& ctx, RegisterFile& register_file,
                      std::vector<parallelcopy>& parallelcopy, aco_ptr<Instruction>& instr)
{
   assert(instr->operands.size() <= 128);
   assert(parallelcopy.empty());

   RegisterFile tmp_file(register_file);

   BITSET_DECLARE(live_reg_assigned, 128) = {0};
   BITSET_DECLARE(mask, 128) = {0};

   for (unsigned i = 0; i < instr->operands.size(); i++) {
      Operand& op = instr->operands[i];

      if (!op.isPrecolored())
         continue;

      assert(op.isTemp());
      PhysReg src = ctx.assignments[op.tempId()].reg;
      if (!BITSET_TEST(live_reg_assigned, i) && !op.isKill()) {
         /* Figure out which register the temp will continue living in after the instruction. */
         PhysReg live_reg = op.physReg();
         bool found = false;
         for (unsigned j = i; j < instr->operands.size(); ++j) {
            const Operand& op2 = instr->operands[j];
            if (!op2.isPrecolored() || op2.tempId() != op.tempId())
               continue;

            /* Don't consider registers interfering with definitions - we'd have to immediately copy
             * the temporary somewhere else to make space for the interfering definition.
             */
            if (op2.isClobbered())
               continue;

            /* Choose the first register that doesn't interfere with definitions. If the temp can
             * continue residing in the same register it's currently in, just choose that.
             */
            if (!found || op2.physReg() == src) {
               live_reg = op2.physReg();
               found = true;
               if (op2.physReg() == src)
                  break;
            }
         }

         for (unsigned j = i; j < instr->operands.size(); ++j) {
            if (!instr->operands[j].isPrecolored() || instr->operands[j].tempId() != op.tempId())
               continue;

            /* Fix up operand kill flags according to the live_reg choice we made. */
            if (instr->operands[j].physReg() == live_reg) {
               instr->operands[j].setCopyKill(false);
               instr->operands[j].setKill(false);
            } else {
               instr->operands[j].setCopyKill(true);
            }
            BITSET_SET(live_reg_assigned, j);
         }
      }

      adjust_max_used_regs(ctx, op.regClass(), op.physReg());

      if (op.physReg() == src) {
         tmp_file.block(op.physReg(), op.regClass());
         continue;
      }

      /* An instruction can have at most one operand precolored to the same register. */
      assert(std::none_of(parallelcopy.begin(), parallelcopy.end(),
                          [&](auto copy) { return copy.def.physReg() == op.physReg(); }));

      /* clear from register_file so fixed operands are not collected by collect_vars(), but if src
       * has been blocked by a previous operand, don't undo the blocking
       */
      if (!tmp_file.is_blocked(src))
         tmp_file.clear(src, op.regClass());

      BITSET_SET(mask, i);

      Operand pc_op(instr->operands[i].getTemp());
      pc_op.setFixed(src);
      Definition pc_def = Definition(op.physReg(), pc_op.regClass());
      parallelcopy.emplace_back(pc_op, pc_def, op.isCopyKill() ? i : -1);
   }

   if (BITSET_IS_EMPTY(mask))
      return;

   unsigned i;
   std::vector<unsigned> blocking_vars;
   BITSET_FOREACH_SET (i, mask, instr->operands.size()) {
      Operand& op = instr->operands[i];
      PhysRegInterval target{op.physReg(), op.size()};
      std::vector<unsigned> blocking_vars2 = collect_vars(ctx, tmp_file, target);
      blocking_vars.insert(blocking_vars.end(), blocking_vars2.begin(), blocking_vars2.end());

      /* prevent get_regs_for_copies() from using these registers */
      tmp_file.block(op.physReg(), op.regClass());
   }

   get_regs_for_copies(ctx, tmp_file, parallelcopy, blocking_vars, instr, PhysRegInterval());
   update_renames(ctx, register_file, parallelcopy, instr, true);
}

void
get_reg_for_operand(ra_ctx& ctx, RegisterFile& register_file,
                    std::vector<parallelcopy>& parallelcopy, aco_ptr<Instruction>& instr,
                    Operand& operand, unsigned operand_index)
{
   /* clear the operand in case it's only a stride mismatch */
   PhysReg src = ctx.assignments[operand.tempId()].reg;
   register_file.clear(src, operand.regClass());
   PhysReg dst = get_reg(ctx, register_file, operand.getTemp(), parallelcopy, instr, operand_index);

   Operand pc_op = operand;
   pc_op.setFixed(src);
   Definition pc_def = Definition(dst, pc_op.regClass());
   parallelcopy.emplace_back(pc_op, pc_def);
   update_renames(ctx, register_file, parallelcopy, instr, true);
   operand.setFixed(dst);
}

void
handle_vector_operands(ra_ctx& ctx, RegisterFile& register_file,
                       std::vector<parallelcopy>& parallelcopies, aco_ptr<Instruction>& instr,
                       unsigned operand_index)
{
   assert(operand_index == 0 || !instr->operands[operand_index - 1].isVectorAligned());
   /* Count the operands that form part of the vector. */
   uint32_t num_operands = 1;
   uint32_t num_bytes = instr->operands[operand_index].bytes();
   for (unsigned i = operand_index + 1; instr->operands[i - 1].isVectorAligned(); i++) {
      /* If it's not late kill, we might end up trying to kill/re-enable the operands
       * before resolve_vector_operands(), which wouldn't work.
       */
      assert(!instr->operands[operand_index].isLateKill() || instr->operands[i].isLateKill());
      num_operands++;
      num_bytes += instr->operands[i].bytes();
   }
   RegClass rc = instr->operands[operand_index].regClass().resize(num_bytes);

   /* Create temporary p_create_vector instruction and make use of get_reg_create_vector. */
   aco_ptr<Instruction> vec{
      create_instruction(aco_opcode::p_create_vector, Format::PSEUDO, num_operands, 1)};
   for (unsigned i = 0; i < num_operands; i++) {
      vec->operands[i] = instr->operands[operand_index + i];
      vec->operands[i].setFixed(ctx.assignments[vec->operands[i].tempId()].reg);
      /* The operands might not be late-kill if they are tied to a definition. */
      vec->operands[i].setLateKill(true);
   }
   Temp vec_temp = ctx.program->allocateTmp(rc);
   ctx.assignments.emplace_back();
   vec->definitions[0] = Definition(vec_temp);

   PhysReg reg = get_reg_create_vector(ctx, register_file, vec_temp, parallelcopies, vec);
   vec->definitions[0].setFixed(reg);
   ctx.assignments[vec_temp.id()].set(vec->definitions[0]);

   /* For now, fix instruction operands to previous registers. They are essentially ignored until
    * we call resolve_vector_operands(). For the remaining handling of this instruction, we'll
    * use the p_create_vector definition as temporary.
    */
   for (unsigned i = operand_index; i < operand_index + num_operands; i++)
      instr->operands[i].setFixed(ctx.assignments[instr->operands[i].tempId()].reg);

   update_renames(ctx, register_file, parallelcopies, instr, true);

   /* If the vector operand is not lateKill, then a (tied) definition might need the same register
    * space. Make sure that the not-killed components are moved to a different register.
    */
   if (!instr->operands[operand_index].isLateKill()) {
      RegisterFile tmp_file = register_file;
      tmp_file.block(reg, vec->definitions[0].regClass());
      for (unsigned i = 0; i < instr->operands.size(); ++i) {
         if (instr->operands[i].isVectorAligned() ||
             (i && instr->operands[i - 1].isVectorAligned()))
            tmp_file.block(ctx.assignments[instr->operands[i].tempId()].reg,
                           instr->operands[i].regClass());
      }
      std::vector<unsigned> vars;
      for (unsigned i = operand_index; i < operand_index + num_operands; i++) {
         if (!instr->operands[i].isKill())
            vars.push_back(instr->operands[i].tempId());
      }
      get_regs_for_copies(ctx, tmp_file, parallelcopies, vars, instr, {});
   }

   update_renames(ctx, register_file, parallelcopies, instr, true, false, true);
   ctx.vector_operands.emplace_back(
      vector_operand{vec->definitions[0], operand_index, num_operands});
   register_file.fill(vec->definitions[0]);
}

void
resolve_vector_operands(ra_ctx& ctx, RegisterFile& reg_file,
                        std::vector<parallelcopy>& parallelcopies, aco_ptr<Instruction>& instr)
{
   /* Vector Operands are temporarily stored as a single SSA value in the register file.
    * Replace it again with the individual components and create the necessary parallelcopies.
    */
   for (vector_operand vec : ctx.vector_operands) {
      /* Remove the temporary p_create_vector definition from register file. */
      reg_file.clear(vec.def);

      PhysReg reg = vec.def.physReg();
      for (unsigned i = vec.start; i < vec.start + vec.num_part; i++) {
         Operand& op = instr->operands[i];
         /* Assert that no already handled parallelcopy moved the operand. */
         assert(std::none_of(parallelcopies.begin(), parallelcopies.end(),
                             [=](parallelcopy copy)
                             {
                                return copy.op.getTemp() == op.getTemp() && copy.def.isTemp() &&
                                       copy.def.physReg() == op.physReg();
                             }));

         /* Add a parallelcopy for each operand which is not in the correct position. */
         if (op.physReg() != reg) {
            Definition pc_def = Definition(reg, op.regClass());
            Operand pc_op = Operand(op.getTemp(), op.physReg());
            parallelcopies.emplace_back(pc_op, pc_def, op.isCopyKill() ? i : -1);
         } else {
            reg_file.fill(Definition(op.getTemp(), reg));
         }

         reg = reg.advance(op.bytes());
      }
   }
   ctx.vector_operands.clear();

   /* Update operand temporaries and fill non-killed operands. */
   update_renames(ctx, reg_file, parallelcopies, instr, true, false);
}

PhysReg
get_reg_phi(ra_ctx& ctx, IDSet& live_in, RegisterFile& register_file,
            std::vector<aco_ptr<Instruction>>& instructions, Block& block,
            aco_ptr<Instruction>& phi, Temp tmp)
{
   std::vector<parallelcopy> parallelcopy;
   PhysReg reg = get_reg(ctx, register_file, tmp, parallelcopy, phi);
   update_renames(ctx, register_file, parallelcopy, phi);

   /* process parallelcopy */
   for (struct parallelcopy pc : parallelcopy) {
      /* see if it's a copy from a different phi */
      // TODO: prefer moving some previous phis over live-ins
      // TODO: somehow prevent phis fixed before the RA from being updated (shouldn't be a
      // problem in practice since they can only be fixed to exec)
      Instruction* prev_phi = NULL;
      for (auto phi_it = instructions.begin(); phi_it != instructions.end(); ++phi_it) {
         if ((*phi_it)->definitions[0].tempId() == pc.op.tempId())
            prev_phi = phi_it->get();
      }
      if (prev_phi) {
         /* if so, just update that phi's register */
         prev_phi->definitions[0].setFixed(pc.def.physReg());
         register_file.fill(prev_phi->definitions[0]);
         ctx.assignments[prev_phi->definitions[0].tempId()] = {pc.def.physReg(), pc.def.regClass()};
         continue;
      }

      /* rename */
      auto orig_it = ctx.orig_names.find(pc.op.tempId());
      Temp orig = orig_it != ctx.orig_names.end() ? orig_it->second : pc.op.getTemp();
      add_rename(ctx, orig, pc.def.getTemp());

      /* otherwise, this is a live-in and we need to create a new phi
       * to move it in this block's predecessors */
      aco_opcode opcode =
         pc.op.getTemp().is_linear() ? aco_opcode::p_linear_phi : aco_opcode::p_phi;
      Block::edge_vec& preds =
         pc.op.getTemp().is_linear() ? block.linear_preds : block.logical_preds;
      aco_ptr<Instruction> new_phi{create_instruction(opcode, Format::PSEUDO, preds.size(), 1)};
      new_phi->definitions[0] = pc.def;
      for (unsigned i = 0; i < preds.size(); i++)
         new_phi->operands[i] = Operand(pc.op);
      instructions.emplace_back(std::move(new_phi));

      /* Remove from live_in, because handle_loop_phis() would re-create this phi later if this is
       * a loop header.
       */
      live_in.erase(orig.id());
   }

   return reg;
}

void
get_regs_for_phis(ra_ctx& ctx, Block& block, RegisterFile& register_file,
                  std::vector<aco_ptr<Instruction>>& instructions, IDSet& live_in)
{
   /* move all phis to instructions */
   bool has_linear_phis = false;
   for (aco_ptr<Instruction>& phi : block.instructions) {
      if (!is_phi(phi))
         break;
      if (!phi->definitions[0].isKill()) {
         has_linear_phis |= phi->opcode == aco_opcode::p_linear_phi;
         instructions.emplace_back(std::move(phi));
      }
   }

   /* assign phis with all-matching registers to that register */
   for (aco_ptr<Instruction>& phi : instructions) {
      Definition& definition = phi->definitions[0];
      if (definition.isFixed())
         continue;

      if (!phi->operands[0].isTemp())
         continue;

      PhysReg reg = phi->operands[0].physReg();
      auto OpsSame = [=](const Operand& op) -> bool
      { return op.isTemp() && (!op.isFixed() || op.physReg() == reg); };
      bool all_same = std::all_of(phi->operands.cbegin() + 1, phi->operands.cend(), OpsSame);
      if (!all_same)
         continue;

      if (!get_reg_specified(ctx, register_file, definition.regClass(), phi, reg, -1))
         continue;

      definition.setFixed(reg);
      register_file.fill(definition);
      ctx.assignments[definition.tempId()].set(definition);
   }

   /* try to find a register that is used by at least one operand */
   for (aco_ptr<Instruction>& phi : instructions) {
      Definition& definition = phi->definitions[0];
      if (definition.isFixed())
         continue;

      /* use affinity if available */
      if (ctx.assignments[definition.tempId()].affinity &&
          ctx.assignments[ctx.assignments[definition.tempId()].affinity].assigned) {
         assignment& affinity = ctx.assignments[ctx.assignments[definition.tempId()].affinity];
         assert(affinity.rc == definition.regClass());
         if (get_reg_specified(ctx, register_file, definition.regClass(), phi, affinity.reg, -1)) {
            definition.setFixed(affinity.reg);
            register_file.fill(definition);
            ctx.assignments[definition.tempId()].set(definition);
            continue;
         }
      }

      /* by going backwards, we aim to avoid copies in else-blocks */
      for (int i = phi->operands.size() - 1; i >= 0; i--) {
         const Operand& op = phi->operands[i];
         if (!op.isTemp() || !op.isFixed())
            continue;

         PhysReg reg = op.physReg();
         if (get_reg_specified(ctx, register_file, definition.regClass(), phi, reg, -1)) {
            definition.setFixed(reg);
            register_file.fill(definition);
            ctx.assignments[definition.tempId()].set(definition);
            break;
         }
      }
   }

   /* find registers for phis where the register was blocked or no operand was assigned */

   /* Don't use iterators because get_reg_phi() can add phis to the end of the vector. */
   for (unsigned i = 0; i < instructions.size(); i++) {
      aco_ptr<Instruction>& phi = instructions[i];
      Definition& definition = phi->definitions[0];
      if (definition.isFixed())
         continue;

      definition.setFixed(
         get_reg_phi(ctx, live_in, register_file, instructions, block, phi, definition.getTemp()));

      register_file.fill(definition);
      ctx.assignments[definition.tempId()].set(definition);
   }

   /* Provide a scratch register in case we need to preserve SCC */
   if (has_linear_phis || block.kind & block_kind_loop_header) {
      PhysReg scratch_reg = scc;
      if (register_file[scc]) {
         scratch_reg = get_reg_phi(ctx, live_in, register_file, instructions, block, ctx.phi_dummy,
                                   Temp(0, s1));
         if (block.kind & block_kind_loop_header)
            ctx.loop_header.back().second = scratch_reg;
      }

      for (aco_ptr<Instruction>& phi : instructions) {
         phi->pseudo().scratch_sgpr = scratch_reg;
         phi->pseudo().needs_scratch_reg = true;
      }
   }
}

inline Temp
read_variable(ra_ctx& ctx, Temp val, unsigned block_idx)
{
   /* This variable didn't get renamed, yet. */
   if (!ctx.assignments[val.id()].renamed)
      return val;

   auto it = ctx.renames[block_idx].find(val.id());
   if (it == ctx.renames[block_idx].end())
      return val;
   else
      return it->second;
}

Temp
handle_live_in(ra_ctx& ctx, Temp val, Block* block)
{
   /* This variable didn't get renamed, yet. */
   if (!ctx.assignments[val.id()].renamed)
      return val;

   Block::edge_vec& preds = val.is_linear() ? block->linear_preds : block->logical_preds;
   if (preds.size() == 0)
      return val;

   if (preds.size() == 1) {
      /* if the block has only one predecessor, just look there for the name */
      return read_variable(ctx, val, preds[0]);
   }

   /* there are multiple predecessors and the block is sealed */
   Temp* const ops = (Temp*)alloca(preds.size() * sizeof(Temp));

   /* get the rename from each predecessor and check if they are the same */
   Temp new_val;
   bool needs_phi = false;
   for (unsigned i = 0; i < preds.size(); i++) {
      ops[i] = read_variable(ctx, val, preds[i]);
      if (i == 0)
         new_val = ops[i];
      else
         needs_phi |= !(new_val == ops[i]);
   }

   if (needs_phi) {
      assert(!val.regClass().is_linear_vgpr());

      /* the variable has been renamed differently in the predecessors: we need to insert a phi */
      aco_opcode opcode = val.is_linear() ? aco_opcode::p_linear_phi : aco_opcode::p_phi;
      aco_ptr<Instruction> phi{create_instruction(opcode, Format::PSEUDO, preds.size(), 1)};
      new_val = ctx.program->allocateTmp(val.regClass());
      phi->definitions[0] = Definition(new_val);
      ctx.assignments.emplace_back();
      assert(ctx.assignments.size() == ctx.program->peekAllocationId());
      for (unsigned i = 0; i < preds.size(); i++) {
         /* update the operands so that it uses the new affinity */
         phi->operands[i] = Operand(ops[i]);
         assert(ctx.assignments[ops[i].id()].assigned);
         assert(ops[i].regClass() == new_val.regClass());
         phi->operands[i].setFixed(ctx.assignments[ops[i].id()].reg);
      }
      block->instructions.insert(block->instructions.begin(), std::move(phi));
   }

   return new_val;
}

void
handle_loop_phis(ra_ctx& ctx, const IDSet& live_in, uint32_t loop_header_idx,
                 uint32_t loop_exit_idx, PhysReg scratch_reg)
{
   Block& loop_header = ctx.program->blocks[loop_header_idx];
   aco::unordered_map<uint32_t, Temp> renames(ctx.memory);

   /* create phis for variables renamed during the loop */
   for (unsigned t : live_in) {
      if (!ctx.assignments[t].renamed)
         continue;

      Temp val = Temp(t, ctx.program->temp_rc[t]);
      Temp prev = read_variable(ctx, val, loop_header_idx - 1);
      Temp renamed = handle_live_in(ctx, val, &loop_header);
      if (renamed == prev)
         continue;

      /* insert additional renames at block end, but don't overwrite */
      renames[prev.id()] = renamed;
      ctx.orig_names[renamed.id()] = val;
      for (unsigned idx = loop_header_idx; idx < loop_exit_idx; idx++) {
         auto it = ctx.renames[idx].emplace(val.id(), renamed);
         /* if insertion is unsuccessful, update if necessary */
         if (!it.second && it.first->second == prev)
            it.first->second = renamed;
      }

      /* update loop-carried values of the phi created by handle_live_in() */
      for (unsigned i = 1; i < loop_header.instructions[0]->operands.size(); i++) {
         Operand& op = loop_header.instructions[0]->operands[i];
         if (op.getTemp() == prev)
            op.setTemp(renamed);
      }

      /* use the assignment from the loop preheader and fix def reg */
      assignment& var = ctx.assignments[prev.id()];
      ctx.assignments[renamed.id()] = var;
      loop_header.instructions[0]->definitions[0].setFixed(var.reg);

      /* Set scratch register */
      loop_header.instructions[0]->pseudo().scratch_sgpr = scratch_reg;
      loop_header.instructions[0]->pseudo().needs_scratch_reg = true;
   }

   /* rename loop carried phi operands */
   for (unsigned i = renames.size(); i < loop_header.instructions.size(); i++) {
      aco_ptr<Instruction>& phi = loop_header.instructions[i];
      if (!is_phi(phi))
         break;
      const Block::edge_vec& preds =
         phi->opcode == aco_opcode::p_phi ? loop_header.logical_preds : loop_header.linear_preds;
      for (unsigned j = 1; j < phi->operands.size(); j++) {
         Operand& op = phi->operands[j];
         if (!op.isTemp())
            continue;

         /* Find the original name, since this operand might not use the original name if the phi
          * was created after init_reg_file().
          */
         auto it = ctx.orig_names.find(op.tempId());
         Temp orig = it != ctx.orig_names.end() ? it->second : op.getTemp();

         op.setTemp(read_variable(ctx, orig, preds[j]));
         op.setFixed(ctx.assignments[op.tempId()].reg);
      }
   }

   /* return early if no new phi was created */
   if (renames.empty())
      return;

   /* propagate new renames through loop */
   for (unsigned idx = loop_header_idx; idx < loop_exit_idx; idx++) {
      Block& current = ctx.program->blocks[idx];
      /* rename all uses in this block */
      for (aco_ptr<Instruction>& instr : current.instructions) {
         /* phis are renamed after RA */
         if (idx == loop_header_idx && is_phi(instr))
            continue;

         for (Operand& op : instr->operands) {
            if (!op.isTemp())
               continue;

            auto rename = renames.find(op.tempId());
            if (rename != renames.end()) {
               assert(rename->second.id());
               op.setTemp(rename->second);
            }
         }
      }
   }
}

/**
 * This function serves the purpose to correctly initialize the register file
 * at the beginning of a block (before any existing phis).
 * In order to do so, all live-in variables are entered into the RegisterFile.
 * Reg-to-reg moves (renames) from previous blocks are taken into account and
 * the SSA is repaired by inserting corresponding phi-nodes.
 */
RegisterFile
init_reg_file(ra_ctx& ctx, const std::vector<IDSet>& live_out_per_block, Block& block)
{
   if (block.kind & block_kind_loop_exit) {
      uint32_t header = ctx.loop_header.back().first;
      PhysReg scratch_reg = ctx.loop_header.back().second;
      ctx.loop_header.pop_back();
      handle_loop_phis(ctx, live_out_per_block[header], header, block.index, scratch_reg);
   }

   RegisterFile register_file;
   const IDSet& live_in = live_out_per_block[block.index];
   assert(block.index != 0 || live_in.empty());

   if (block.kind & block_kind_loop_header) {
      ctx.loop_header.emplace_back(block.index, PhysReg{scc});
      /* already rename phis incoming value */
      for (aco_ptr<Instruction>& instr : block.instructions) {
         if (!is_phi(instr))
            break;
         Operand& operand = instr->operands[0];
         if (operand.isTemp()) {
            operand.setTemp(read_variable(ctx, operand.getTemp(), block.index - 1));
            operand.setFixed(ctx.assignments[operand.tempId()].reg);
         }
      }
      for (unsigned t : live_in) {
         Temp val = Temp(t, ctx.program->temp_rc[t]);
         Temp renamed = read_variable(ctx, val, block.index - 1);
         if (renamed != val)
            add_rename(ctx, val, renamed);
         assignment& var = ctx.assignments[renamed.id()];
         assert(var.assigned);
         register_file.fill(Definition(renamed, var.reg));
      }
   } else {
      /* rename phi operands */
      for (aco_ptr<Instruction>& instr : block.instructions) {
         if (!is_phi(instr))
            break;
         const Block::edge_vec& preds =
            instr->opcode == aco_opcode::p_phi ? block.logical_preds : block.linear_preds;

         for (unsigned i = 0; i < instr->operands.size(); i++) {
            Operand& operand = instr->operands[i];
            if (!operand.isTemp())
               continue;
            operand.setTemp(read_variable(ctx, operand.getTemp(), preds[i]));
            operand.setFixed(ctx.assignments[operand.tempId()].reg);
         }
      }
      for (unsigned t : live_in) {
         Temp val = Temp(t, ctx.program->temp_rc[t]);
         Temp renamed = handle_live_in(ctx, val, &block);
         assignment& var = ctx.assignments[renamed.id()];
         /* due to live-range splits, the live-in might be a phi, now */
         if (var.assigned) {
            register_file.fill(Definition(renamed, var.reg));
         }
         if (renamed != val) {
            add_rename(ctx, val, renamed);
         }
      }
   }

   return register_file;
}

bool
vop3_can_use_vop2acc(ra_ctx& ctx, Instruction* instr)
{
   if (!instr->isVOP3() && !instr->isVOP3P())
      return false;

   switch (instr->opcode) {
   case aco_opcode::v_mad_f32:
   case aco_opcode::v_mad_f16:
   case aco_opcode::v_mad_legacy_f16: break;
   case aco_opcode::v_fma_f32:
   case aco_opcode::v_pk_fma_f16:
   case aco_opcode::v_fma_f16:
   case aco_opcode::v_dot4_i32_i8:
      if (ctx.program->gfx_level < GFX10)
         return false;
      break;
   case aco_opcode::v_mad_legacy_f32:
      if (!ctx.program->dev.has_mac_legacy32)
         return false;
      break;
   case aco_opcode::v_fma_legacy_f32:
      if (!ctx.program->dev.has_fmac_legacy32)
         return false;
      break;
   default: return false;
   }

   if (!instr->operands[2].isOfType(RegType::vgpr) || !instr->operands[2].isKillBeforeDef() ||
       (!instr->operands[0].isOfType(RegType::vgpr) && !instr->operands[1].isOfType(RegType::vgpr)))
      return false;

   if (instr->isVOP3P()) {
      for (unsigned i = 0; i < 3; i++) {
         if (instr->operands[i].isLiteral())
            continue;

         if (instr->valu().opsel_lo[i])
            return false;

         /* v_pk_fmac_f16 inline constants are replicated to hi bits starting with gfx11. */
         if (instr->valu().opsel_hi[i] ==
             (instr->operands[i].isConstant() && ctx.program->gfx_level >= GFX11))
            return false;
      }
   } else {
      if (instr->valu().opsel & (ctx.program->gfx_level < GFX11 ? 0xf : ~0x3))
         return false;
      for (unsigned i = 0; i < 2; i++) {
         if (!instr->operands[i].isOfType(RegType::vgpr) && instr->valu().opsel[i])
            return false;
      }
   }

   unsigned im_mask = instr->isDPP16() && instr->isVOP3() ? 0x3 : 0;
   if (instr->valu().omod || instr->valu().clamp || (instr->valu().abs & ~im_mask) ||
       (instr->valu().neg & ~im_mask))
      return false;

   return true;
}

bool
sop2_can_use_sopk(ra_ctx& ctx, Instruction* instr)
{
   if (instr->opcode != aco_opcode::s_add_i32 && instr->opcode != aco_opcode::s_add_u32 &&
       instr->opcode != aco_opcode::s_mul_i32 && instr->opcode != aco_opcode::s_cselect_b32)
      return false;

   if (instr->opcode == aco_opcode::s_add_u32 && !instr->definitions[1].isKill())
      return false;

   uint32_t literal_idx = 0;

   if (instr->opcode != aco_opcode::s_cselect_b32 && instr->operands[1].isLiteral())
      literal_idx = 1;

   if (!instr->operands[!literal_idx].isTemp() || !instr->operands[!literal_idx].isKillBeforeDef())
      return false;

   if (!instr->operands[literal_idx].isLiteral())
      return false;

   const uint32_t i16_mask = 0xffff8000u;
   uint32_t value = instr->operands[literal_idx].constantValue();
   if ((value & i16_mask) && (value & i16_mask) != i16_mask)
      return false;

   return true;
}

void
create_phi_vector_affinities(ra_ctx& ctx, aco_ptr<Instruction>& instr,
                             std::map<Operand*, std::vector<vector_info>>& vector_phis)
{
   auto it = ctx.vectors.find(instr->definitions[0].tempId());
   if (it == ctx.vectors.end())
      return;
   vector_info& dest_vector = it->second;

   auto pair = vector_phis.try_emplace(dest_vector.parts, instr->operands.size(), dest_vector);
   std::vector<vector_info>& src_vectors = pair.first->second;
   if (pair.second) {
      RegType type = instr->definitions[0].regClass().type();

      for (vector_info& src_vector : src_vectors) {
         src_vector.parts =
            (Operand*)ctx.memory.allocate(sizeof(Operand) * src_vector.num_parts, alignof(Operand));
         for (unsigned j = 0; j < src_vector.num_parts; j++)
            src_vector.parts[j] = Operand(RegClass::get(type, dest_vector.parts[j].bytes()));
      }
   }

   for (int i = instr->operands.size() - 1; i >= 0; i--) {
      const Operand& op = instr->operands[i];
      if (!op.isTemp() || op.regClass() != instr->definitions[0].regClass())
         continue;

      src_vectors[i].parts[dest_vector.index] = op;
      src_vectors[i].index = dest_vector.index;
      ctx.vectors[op.tempId()] = src_vectors[i];
   }
}

void
get_affinities(ra_ctx& ctx)
{
   std::vector<std::vector<Temp>> phi_resources;
   aco::unordered_map<uint32_t, uint32_t> temp_to_phi_resources(ctx.memory);

   for (auto block_rit = ctx.program->blocks.rbegin(); block_rit != ctx.program->blocks.rend();
        block_rit++) {
      Block& block = *block_rit;

      std::vector<aco_ptr<Instruction>>::reverse_iterator rit;
      for (rit = block.instructions.rbegin(); rit != block.instructions.rend(); ++rit) {
         aco_ptr<Instruction>& instr = *rit;
         if (is_phi(instr))
            break;

         /* add vector affinities */
         if (instr->opcode == aco_opcode::p_create_vector) {
            for (unsigned i = 0; i < instr->operands.size(); i++) {
               Operand op = instr->operands[i];
               if (op.isTemp() && op.isFirstKill() &&
                   op.getTemp().type() == instr->definitions[0].getTemp().type())
                  ctx.vectors[op.tempId()] = vector_info(instr.get(), i);
            }
         } else if (instr->format == Format::MIMG && instr->operands.size() > 4 &&
                    !instr->mimg().strict_wqm) {

            bool is_vector = false;
            unsigned op_count = instr->operands.size() - (instr->mimg().disable_wqm * 2);
            for (unsigned i = 3, vector_begin = 3; i < op_count; i++) {
               if (is_vector || instr->operands[i].isVectorAligned())
                  ctx.vectors[instr->operands[i].tempId()] =
                     vector_info(instr.get(), i - vector_begin, vector_begin);
               else if (ctx.program->gfx_level < GFX12 && !instr->operands[3].isVectorAligned())
                  ctx.vectors[instr->operands[i].tempId()] =
                     vector_info(instr.get(), i - 3, 3, true);
               is_vector = instr->operands[i].isVectorAligned();
               vector_begin = is_vector ? vector_begin : i + 1;
            }
         } else if (instr->opcode == aco_opcode::p_split_vector &&
                    instr->operands[0].isFirstKillBeforeDef()) {
            ctx.split_vectors[instr->operands[0].tempId()] = instr.get();
         } else if (instr->isVOPC() && !instr->isVOP3()) {
            if (!instr->isSDWA() || ctx.program->gfx_level == GFX8)
               ctx.assignments[instr->definitions[0].tempId()].set_precolor_affinity(vcc);
         } else if (instr->isVOP2() && !instr->isVOP3()) {
            if (instr->operands.size() == 3 && instr->operands[2].isTemp() &&
                instr->operands[2].regClass().type() == RegType::sgpr)
               ctx.assignments[instr->operands[2].tempId()].set_precolor_affinity(vcc);
            if (instr->definitions.size() == 2)
               ctx.assignments[instr->definitions[1].tempId()].set_precolor_affinity(vcc);
         } else if (instr->opcode == aco_opcode::s_and_b32 ||
                    instr->opcode == aco_opcode::s_and_b64) {
            /* If SCC is used by a branch, we might be able to use
             * s_cbranch_vccz/s_cbranch_vccnz if the operand is VCC.
             */
            if (!instr->definitions[1].isKill() && instr->operands[0].isTemp() &&
                instr->operands[1].isFixed() && instr->operands[1].physReg() == exec)
               ctx.assignments[instr->operands[0].tempId()].set_precolor_affinity(vcc);
         } else if (instr->format == Format::DS) {
            bool is_vector = false;
            for (unsigned i = 0, vector_begin = 0; i < instr->operands.size(); i++) {
               if (is_vector || instr->operands[i].isVectorAligned())
                  ctx.vectors[instr->operands[i].tempId()] =
                     vector_info(instr.get(), i - vector_begin, vector_begin);
               is_vector = instr->operands[i].isVectorAligned();
               vector_begin = is_vector ? vector_begin : i + 1;
            }
         }

         /* Collect register-affinities for precolored operands. */
         for (Operand op : instr->operands) {
            if (op.isTemp() && op.isPrecolored())
               ctx.assignments[op.tempId()].set_precolor_affinity(op.physReg());
         }

         auto tied_defs = get_tied_defs(instr.get());
         for (unsigned i = 0; i < instr->definitions.size(); i++) {
            const Definition& def = instr->definitions[i];
            if (!def.isTemp())
               continue;

            auto it = temp_to_phi_resources.find(def.tempId());
            if (it != temp_to_phi_resources.end()) {
               /* mark last-seen phi operand */
               phi_resources[it->second][0] = def.getTemp();
            }

            /* try to coalesce affinities with parallelcopies */
            Operand op;
            if (instr->opcode == aco_opcode::p_parallelcopy) {
               op = instr->operands[i];
            } else if (i < tied_defs.size()) {
               op = instr->operands[tied_defs[i]];
            } else if (vop3_can_use_vop2acc(ctx, instr.get())) {
               op = instr->operands[2];
            } else if (i == 0 && sop2_can_use_sopk(ctx, instr.get())) {
               op = instr->operands[instr->operands[0].isLiteral()];
            } else {
               continue;
            }

            if (op.isTemp() && op.isFirstKillBeforeDef() && def.regClass() == op.regClass()) {
               if (ctx.assignments[def.tempId()].precolor_affinity)
                  ctx.assignments[op.tempId()].set_precolor_affinity(
                     ctx.assignments[def.tempId()].reg);
               if (it != temp_to_phi_resources.end()) {
                  phi_resources[it->second].emplace_back(op.getTemp());
                  temp_to_phi_resources[op.tempId()] = it->second;
               }
               auto vec_it = ctx.vectors.find(def.tempId());
               if (vec_it != ctx.vectors.end())
                  ctx.vectors[op.tempId()] = vec_it->second;
            }
         }
      }

      /* collect phi affinities */
      std::map<Operand*, std::vector<vector_info>> vector_phis;
      for (; rit != block.instructions.rend(); ++rit) {
         aco_ptr<Instruction>& instr = *rit;
         assert(is_phi(instr));

         if (instr->definitions[0].isKill() || instr->definitions[0].isFixed())
            continue;

         assert(instr->definitions[0].isTemp());
         Temp def = instr->definitions[0].getTemp();
         auto it = temp_to_phi_resources.find(def.id());
         unsigned index = phi_resources.size();
         std::vector<Temp>* affinity_related;
         if (it != temp_to_phi_resources.end()) {
            index = it->second;
            phi_resources[index][0] = def;
            affinity_related = &phi_resources[index];
         } else {
            phi_resources.emplace_back(std::vector<Temp>{def});
            affinity_related = &phi_resources.back();
         }

         for (const Operand& op : instr->operands) {
            if (op.isTemp() && op.isKill() && op.regClass() == def.regClass()) {
               affinity_related->emplace_back(op.getTemp());
               if (ctx.assignments[def.id()].precolor_affinity)
                  ctx.assignments[op.tempId()].set_precolor_affinity(ctx.assignments[def.id()].reg);
               if (block.kind & block_kind_loop_header)
                  continue;
               temp_to_phi_resources[op.tempId()] = index;
            }
         }

         create_phi_vector_affinities(ctx, instr, vector_phis);
      }

      /* visit the loop header phis first in order to create nested affinities */
      if (block.kind & block_kind_loop_exit) {
         /* find loop header */
         auto header_rit = block_rit;
         while ((header_rit + 1)->loop_nest_depth > block.loop_nest_depth)
            header_rit++;

         for (aco_ptr<Instruction>& phi : header_rit->instructions) {
            if (!is_phi(phi))
               break;
            if (phi->definitions[0].isKill() || phi->definitions[0].isFixed())
               continue;

            /* create an (empty) merge-set for the phi-related variables */
            auto it = temp_to_phi_resources.find(phi->definitions[0].tempId());
            unsigned index = phi_resources.size();
            if (it == temp_to_phi_resources.end()) {
               temp_to_phi_resources[phi->definitions[0].tempId()] = index;
               phi_resources.emplace_back(std::vector<Temp>{phi->definitions[0].getTemp()});
            } else {
               index = it->second;
            }
            for (unsigned i = 1; i < phi->operands.size(); i++) {
               const Operand& op = phi->operands[i];
               if (op.isTemp() && op.isKill() && op.regClass() == phi->definitions[0].regClass()) {
                  temp_to_phi_resources[op.tempId()] = index;
               }
            }
         }
      }
   }
   /* create affinities */
   for (std::vector<Temp>& vec : phi_resources) {
      for (unsigned i = 1; i < vec.size(); i++)
         if (vec[i].id() != vec[0].id())
            ctx.assignments[vec[i].id()].affinity = vec[0].id();
   }
}

void
optimize_encoding_vop2(ra_ctx& ctx, RegisterFile& register_file, aco_ptr<Instruction>& instr)
{
   if (!vop3_can_use_vop2acc(ctx, instr.get()))
      return;

   for (unsigned i = ctx.program->gfx_level < GFX11 ? 0 : 2; i < 3; i++) {
      if (instr->operands[i].physReg().byte())
         return;
   }

   unsigned def_id = instr->definitions[0].tempId();
   if (ctx.assignments[def_id].affinity) {
      assignment& affinity = ctx.assignments[ctx.assignments[def_id].affinity];
      if (affinity.assigned && affinity.reg != instr->operands[2].physReg() &&
          (!register_file.test(affinity.reg, instr->operands[2].bytes()) ||
           std::any_of(instr->operands.begin(), instr->operands.end(), [&](Operand op)
                       { return op.isKillBeforeDef() && op.physReg() == affinity.reg; })))
         return;
   } else if (ctx.assignments[def_id].precolor_affinity) {
      if (ctx.assignments[def_id].reg != instr->operands[2].physReg())
         return;
   }

   if (!instr->operands[1].isOfType(RegType::vgpr))
      instr->valu().swapOperands(0, 1);

   if (instr->isVOP3P() && instr->operands[0].isLiteral()) {
      unsigned literal = instr->operands[0].constantValue();
      unsigned lo = (literal >> (instr->valu().opsel_lo[0] * 16)) & 0xffff;
      unsigned hi = (literal >> (instr->valu().opsel_hi[0] * 16)) & 0xffff;
      instr->operands[0] = Operand::literal32(lo | (hi << 16));
   }

   instr->format = (Format)(((unsigned)withoutVOP3(instr->format) & ~(unsigned)Format::VOP3P) |
                            (unsigned)Format::VOP2);
   instr->valu().opsel_lo = 0;
   instr->valu().opsel_hi = 0;
   switch (instr->opcode) {
   case aco_opcode::v_mad_f32: instr->opcode = aco_opcode::v_mac_f32; break;
   case aco_opcode::v_fma_f32: instr->opcode = aco_opcode::v_fmac_f32; break;
   case aco_opcode::v_mad_f16:
   case aco_opcode::v_mad_legacy_f16: instr->opcode = aco_opcode::v_mac_f16; break;
   case aco_opcode::v_fma_f16: instr->opcode = aco_opcode::v_fmac_f16; break;
   case aco_opcode::v_pk_fma_f16: instr->opcode = aco_opcode::v_pk_fmac_f16; break;
   case aco_opcode::v_dot4_i32_i8: instr->opcode = aco_opcode::v_dot4c_i32_i8; break;
   case aco_opcode::v_mad_legacy_f32: instr->opcode = aco_opcode::v_mac_legacy_f32; break;
   case aco_opcode::v_fma_legacy_f32: instr->opcode = aco_opcode::v_fmac_legacy_f32; break;
   default: break;
   }
}

void
optimize_encoding_sopk(ra_ctx& ctx, RegisterFile& register_file, aco_ptr<Instruction>& instr)
{
   /* try to optimize sop2 with literal source to sopk */
   if (!sop2_can_use_sopk(ctx, instr.get()))
      return;
   unsigned literal_idx = instr->operands[1].isLiteral();

   PhysReg op_reg = instr->operands[!literal_idx].physReg();
   if (!is_sgpr_writable_without_side_effects(ctx.program->gfx_level, op_reg))
      return;

   unsigned def_id = instr->definitions[0].tempId();
   if (ctx.assignments[def_id].affinity) {
      assignment& affinity = ctx.assignments[ctx.assignments[def_id].affinity];
      if (affinity.assigned && affinity.reg != op_reg &&
          (!register_file.test(affinity.reg, instr->operands[!literal_idx].bytes()) ||
           std::any_of(instr->operands.begin(), instr->operands.end(), [&](Operand op)
                       { return op.isKillBeforeDef() && op.physReg() == affinity.reg; })))
         return;
   } else if (ctx.assignments[def_id].precolor_affinity) {
      if (ctx.assignments[def_id].reg != op_reg)
         return;
   }

   instr->format = Format::SOPK;
   instr->salu().imm = instr->operands[literal_idx].constantValue() & 0xffff;
   if (literal_idx == 0)
      std::swap(instr->operands[0], instr->operands[1]);
   if (instr->operands.size() > 2)
      std::swap(instr->operands[1], instr->operands[2]);
   instr->operands.pop_back();

   switch (instr->opcode) {
   case aco_opcode::s_add_u32:
   case aco_opcode::s_add_i32: instr->opcode = aco_opcode::s_addk_i32; break;
   case aco_opcode::s_mul_i32: instr->opcode = aco_opcode::s_mulk_i32; break;
   case aco_opcode::s_cselect_b32: instr->opcode = aco_opcode::s_cmovk_i32; break;
   default: UNREACHABLE("illegal instruction");
   }
}

void
optimize_encoding(ra_ctx& ctx, RegisterFile& register_file, aco_ptr<Instruction>& instr)
{
   if (instr->isVALU())
      optimize_encoding_vop2(ctx, register_file, instr);
   if (instr->isSALU())
      optimize_encoding_sopk(ctx, register_file, instr);
}

void
undo_renames(ra_ctx& ctx, std::vector<parallelcopy>& parallelcopies,
             aco_ptr<Instruction>& instr)
{
   /* Undo renaming if possible in order to reduce latency.
    *
    * This can also remove a use of a SCC->SGPR copy, which can then be removed completely if the
    * post-RA optimizer eliminates the copy by duplicating the instruction that produced the SCC */
   for (parallelcopy copy : parallelcopies) {
      bool first[2] = {true, true};
      for (unsigned i = 0; i < instr->operands.size(); i++) {
         Operand& op = instr->operands[i];
         if (!op.isTemp() || op.getTemp() != copy.def.getTemp()) {
            first[1] &= !op.isTemp() || op.getTemp() != copy.op.getTemp();
            continue;
         }

         bool use_original = !op.isPrecolored() && !op.isLateKill();
         use_original &= operand_can_use_reg(ctx.program->gfx_level, instr, i, copy.op.physReg(),
                                             copy.op.regClass());

         if (use_original) {
            const PhysRegInterval copy_reg = {copy.op.physReg(), copy.op.size()};
            for (parallelcopy& pc : parallelcopies) {
               const PhysRegInterval def_reg = {pc.def.physReg(), pc.def.size()};
               use_original &= !intersects(def_reg, copy_reg);
            }
         }

         /* Avoid unrenaming killed operands because it can increase the cost of instructions like
          * p_create_vector. */
         use_original &= !op.isKillBeforeDef();

         if (first[use_original])
            op.setFirstKill(use_original || op.isKill());
         else
            op.setKill(use_original || op.isKill());
         first[use_original] = false;

         if (use_original) {
            op.setTemp(copy.op.getTemp());
            op.setFixed(copy.op.physReg());
         }
      }
   }
}

void
handle_operands_tied_to_definitions(ra_ctx& ctx, std::vector<parallelcopy>& parallelcopies,
                                    aco_ptr<Instruction>& instr, RegisterFile& reg_file,
                                    aco::small_vec<uint32_t, 2> tied_defs)
{
   for (uint32_t op_idx : tied_defs) {
      Operand& op = instr->operands[op_idx];
      assert((!op.isLateKill() || op.isCopyKill()) && !op.isPrecolored());

      /* If the operand is copy-kill, then there's an earlier operand which is also tied to a
       * definition but uses the same temporary as this one.
       *
       * We also need to copy if there is different definition which is precolored and intersects
       * with this operand, but we don't bother since it shouldn't happen.
       *
       * Vector-aligned operands get copied in resolve_vector_operands and proper register
       * assignment is already ensured.
       */
      if ((!op.isKill() || op.isCopyKill()) && !op.isVectorAligned()) {
         PhysReg reg = get_reg(ctx, reg_file, op.getTemp(), parallelcopies, instr, op_idx);

         /* update_renames() in case we moved this operand. */
         update_renames(ctx, reg_file, parallelcopies, instr, true);

         Operand pc_op(op.getTemp());
         pc_op.setFixed(ctx.assignments[op.tempId()].reg);
         Definition pc_def(reg, op.regClass());
         parallelcopies.emplace_back(pc_op, pc_def, op_idx);

         update_renames(ctx, reg_file, parallelcopies, instr, true);
      }

      /* Flag the operand's temporary as lateKill. This serves as placeholder
       * for the tied definition until the instruction is fully handled.
       */
      bool is_vector;
      do {
         for (Operand& other_op : instr->operands) {
            if (other_op.isTemp() && other_op.getTemp() == instr->operands[op_idx].getTemp())
               other_op.setLateKill(true);
         }
         is_vector = instr->operands[op_idx].isVectorAligned();
         ++op_idx;
      } while (is_vector);
   }
}

void
assign_tied_definitions(ra_ctx& ctx, aco_ptr<Instruction>& instr, RegisterFile& reg_file,
                        aco::small_vec<uint32_t, 2> tied_defs)
{
   unsigned fixed_def_idx = 0;
   for (auto op_idx : tied_defs) {
      Definition& def = instr->definitions[fixed_def_idx++];
      Operand& op = instr->operands[op_idx];
      assert(op.isKill());
      if (op.isVectorAligned()) {
         ASSERTED uint32_t vector_size = 0;
         bool is_vector;
         unsigned vec_idx = op_idx;
         do {
            vector_size += instr->operands[vec_idx].size();
            is_vector = instr->operands[vec_idx].isVectorAligned();
            ++vec_idx;
         } while (is_vector);
         assert(def.regClass().type() == op.regClass().type() && def.size() <= vector_size);
      } else {
         assert(def.regClass().type() == op.regClass().type() && def.size() <= op.size());
      }

      def.setFixed(op.physReg());
      ctx.assignments[def.tempId()].set(def);
      reg_file.clear(op);
      reg_file.fill(def);

      bool is_vector;
      do {
         for (Operand& other_op : instr->operands) {
            if (other_op.isTemp() && other_op.getTemp() == instr->operands[op_idx].getTemp())
               other_op.setLateKill(false);
         }
         is_vector = instr->operands[op_idx].isVectorAligned();
         ++op_idx;
      } while (is_vector);
   }
}

void
emit_parallel_copy_internal(ra_ctx& ctx, std::vector<parallelcopy>& parallelcopy,
                            aco_ptr<Instruction>& instr,
                            std::vector<aco_ptr<Instruction>>& instructions, bool temp_in_scc,
                            RegisterFile& register_file)
{
   if (parallelcopy.empty())
      return;

   aco_ptr<Instruction> pc;
   pc.reset(create_instruction(aco_opcode::p_parallelcopy, Format::PSEUDO, parallelcopy.size(),
                               parallelcopy.size()));
   bool linear_vgpr = false;
   bool may_swap_sgprs = false;
   std::bitset<256> sgpr_operands;
   for (unsigned i = 0; i < parallelcopy.size(); i++) {
      linear_vgpr |= parallelcopy[i].op.regClass().is_linear_vgpr();

      if (!may_swap_sgprs && parallelcopy[i].op.isTemp() &&
          parallelcopy[i].op.getTemp().type() == RegType::sgpr) {
         unsigned op_reg = parallelcopy[i].op.physReg().reg();
         unsigned def_reg = parallelcopy[i].def.physReg().reg();
         for (unsigned j = 0; j < parallelcopy[i].op.size(); j++) {
            sgpr_operands.set(op_reg + j);
            if (sgpr_operands.test(def_reg + j))
               may_swap_sgprs = true;
         }
      }

      pc->operands[i] = parallelcopy[i].op;
      pc->definitions[i] = parallelcopy[i].def;
      assert(pc->operands[i].size() == pc->definitions[i].size());

      if (parallelcopy[i].copy_kill < 0) {
         /* it might happen that the operand is already renamed. we have to restore the
          * original name. */
         auto it =
            ctx.orig_names.find(pc->operands[i].tempId());
         Temp orig = it != ctx.orig_names.end() ? it->second : pc->operands[i].getTemp();
         add_rename(
         ctx, orig, pc->definitions[i].getTemp());
      }
   }

   if (temp_in_scc && (may_swap_sgprs || linear_vgpr)) {
      /* disable definitions and re-enable operands */
      RegisterFile tmp_file(register_file);
      for (const Definition& def : instr->definitions) {
         if (def.isTemp() && !def.isKill())
            tmp_file.clear(def);
      }
      for (const Operand& op : instr->operands) {
         if (op.isTemp() && op.isFirstKill())
            tmp_file.block(op.physReg(), op.regClass());
      }

      handle_pseudo(ctx, tmp_file, pc.get());
   } else {
      pc->pseudo().needs_scratch_reg = may_swap_sgprs || linear_vgpr;
      pc->pseudo().scratch_sgpr = scc;
   }

   instructions.emplace_back(std::move(pc));

   parallelcopy.clear();
}

void
emit_parallel_copy(ra_ctx& ctx, std::vector<parallelcopy>& copies,
                   aco_ptr<Instruction>& instr, std::vector<aco_ptr<Instruction>>& instructions,
                   bool temp_in_scc, RegisterFile& register_file)
{
   if (copies.empty())
      return;

   std::vector<parallelcopy> linear_vgpr;
   if (ctx.num_linear_vgprs) {
      auto next = copies.begin();
      for (auto it = copies.begin(); it != copies.end(); ++it) {
         if (it->op.regClass().is_linear_vgpr()) {
            linear_vgpr.push_back(*it);
            continue;
         }

         if (next != it)
            *next = *it;
         ++next;
      }
      copies.erase(next, copies.end());
   }

   /* Because of how linear VGPRs are allocated, we should never have to move a linear VGPR into the
    * space of a normal one. This means the copy can be done entirely before normal VGPR copies. */
   emit_parallel_copy_internal(ctx, linear_vgpr, instr, instructions, temp_in_scc,
                               register_file);
   emit_parallel_copy_internal(ctx, copies, instr, instructions, temp_in_scc,
                               register_file);
}

std::vector<Instruction*>
split_blocking_vectors(ra_ctx& ctx, std::vector<unsigned>& vars, RegisterFile& file)
{
   std::vector<unsigned> blocked_defs;
   std::vector<Instruction*> splits;

   for (auto var_it = vars.begin(); var_it != vars.end();) {
      unsigned id = *var_it;
      RegClass rc = ctx.program->temp_rc[id];
      if (rc.size() == 1) {
         ++var_it;
         continue;
      }

      /* If the vector is only partially blocking, clear the non-blocking parts */
      PhysReg start = ctx.assignments[id].reg;
      for (PhysReg reg = start; reg < reg + rc.size(); reg = reg.advance(4))
         if (!file.is_blocked(reg))
            file.clear(reg, RegClass(ctx.program->temp_rc[id].type(), 1));

      aco_ptr<Instruction> split = aco_ptr<Instruction>(
         create_instruction(aco_opcode::p_split_vector, Format::PSEUDO, 1, rc.size()));
      split->operands[0] = Operand(Temp(id, rc), start);

      for (unsigned def_idx = 0; def_idx < rc.size(); ++def_idx) {
         RegClass def_rc = RegClass::get(rc.type(), MIN2(4, rc.bytes() - def_idx * 4));
         Temp tmp = ctx.program->allocateTmp(def_rc);
         PhysReg reg = start.advance(def_idx * 4);
         ctx.assignments.emplace_back(reg, def_rc);
         split->definitions[def_idx] = Definition(tmp, reg);

         if (get_reg_specified(ctx, file, def_rc, split, reg, 0))
            file.fill(split->definitions[def_idx]);
         else
            blocked_defs.push_back(tmp.id());
      }
      splits.push_back(split.release());
      var_it = vars.erase(var_it);
   }

   vars.insert(vars.end(), blocked_defs.begin(), blocked_defs.end());

   return splits;
}

void
recreate_blocking_vectors(ra_ctx& ctx, const std::vector<Instruction*>& splits,
                          std::vector<aco_ptr<Instruction>>& instructions, RegisterFile& reg_file)
{
   for (const auto& split : splits) {
      std::vector<parallelcopy> parallelcopies;
      RegClass rc = split->operands[0].regClass();
      Temp tmp = ctx.program->allocateTmp(rc);
      ctx.assignments.emplace_back();

      aco_ptr<Instruction> vec = aco_ptr<Instruction>(create_instruction(
         aco_opcode::p_create_vector, Format::PSEUDO, split->definitions.size(), 1));

      for (unsigned op_idx = 0; op_idx < split->definitions.size(); ++op_idx)
         vec->operands[op_idx] =
            Operand(split->definitions[op_idx].getTemp(), split->definitions[op_idx].physReg());

      bool temp_in_scc = reg_file[scc];

      PhysReg reg = get_reg_create_vector(ctx, reg_file, tmp, parallelcopies, vec);
      vec->definitions[0] = Definition(tmp, reg);
      reg_file.fill(vec->definitions[0]);
      ctx.assignments[tmp.id()].set(vec->definitions[0]);

      update_renames(ctx, reg_file, parallelcopies, vec);
      emit_parallel_copy(ctx, parallelcopies, vec, instructions, temp_in_scc, reg_file);
      instructions.push_back(std::move(vec));

      add_rename(ctx, split->operands[0].getTemp(), tmp);
   }
}


void
handle_last_reload_preserved(ra_ctx& ctx, aco_ptr<Instruction>& instr,
                             std::vector<parallelcopy>& parallelcopy, RegisterFile& register_file)
{
   BITSET_DECLARE(preserved_regs, 512);
   ctx.program->callee_abi.preservedRegisters(preserved_regs);

   std::vector<unsigned> vars = collect_vars_from_bitset(ctx, register_file, preserved_regs);

   unsigned start, end;
   BITSET_FOREACH_RANGE (start, end, preserved_regs, 512)
      register_file.block(PhysRegInterval{PhysReg{start}, end - start});

   ASSERTED bool success = false;
   success = get_regs_for_copies(ctx, register_file, parallelcopy, vars, instr, PhysRegInterval{});
   assert(success);

   update_renames(ctx, register_file, parallelcopy, instr, false, false);
}

void
handle_call(ra_ctx& ctx, aco_ptr<Instruction>& instr, BITSET_DECLARE(call_clobbered_regs, 512),
            std::vector<Instruction*>& vector_splits, std::vector<parallelcopy>& parallelcopy,
            RegisterFile& register_file)
{
   /* create parallelcopy pair to move blocking vars */
   RegisterFile tmp_file = register_file;
   std::vector<unsigned> vars = collect_vars_from_bitset(ctx, tmp_file, call_clobbered_regs);

   tmp_file.fill_killed_operands(instr.get());
   unsigned start, end;
   BITSET_FOREACH_RANGE (start, end, call_clobbered_regs, 512) {
      PhysRegInterval interval = PhysRegInterval{PhysReg{start}, end - start};
      tmp_file.block(interval);
   }

   std::vector<struct parallelcopy> copies;
   bool success = get_regs_for_copies(ctx, tmp_file, copies, vars, instr, PhysRegInterval{});
   if (success) {
      parallelcopy.insert(parallelcopy.end(), copies.begin(), copies.end());
   } else {
      vector_splits = split_blocking_vectors(ctx, vars, tmp_file);
      success = get_regs_for_copies(ctx, tmp_file, parallelcopy, vars, instr, PhysRegInterval{});
      /* With all blocking vars being scalar, assigning registers should always succeed. */
      assert(success);
   }

   update_renames(ctx, register_file, parallelcopy, instr);
}

} /* end namespace */

void
register_allocation(Program* program, ra_test_policy policy)
{
   ra_ctx ctx(program, policy);
   get_affinities(ctx);

   for (Block& block : program->blocks) {
      ctx.block = &block;

      /* initialize register file */
      RegisterFile register_file = init_reg_file(ctx, program->live.live_in, block);
      ctx.war_hint.reset();
      ctx.rr_vgpr_it = {PhysReg{256}};
      ctx.rr_sgpr_it = {PhysReg{0}};

      std::vector<aco_ptr<Instruction>> instructions;
      instructions.reserve(block.instructions.size());

      /* this is a slight adjustment from the paper as we already have phi nodes:
       * We consider them incomplete phis and only handle the definition. */
      get_regs_for_phis(ctx, block, register_file, instructions,
                        program->live.live_in[block.index]);

      /* Handle all other instructions of the block */
      auto NonPhi = [](aco_ptr<Instruction>& instr) -> bool { return instr && !is_phi(instr); };
      auto instr_it = std::find_if(block.instructions.begin(), block.instructions.end(), NonPhi);
      for (; instr_it != block.instructions.end(); ++instr_it) {
         aco_ptr<Instruction>& instr = *instr_it;
         std::vector<parallelcopy> parallelcopy;
         std::vector<Instruction*> vector_splits;
         assert(!is_phi(instr));

         /* handle operands */
         bool fixed = false;
         for (unsigned i = 0; i < instr->operands.size(); ++i) {
            auto& operand = instr->operands[i];
            if (!operand.isTemp())
               continue;

            /* rename operands */
            operand.setTemp(read_variable(ctx, operand.getTemp(), block.index));
            assert(ctx.assignments[operand.tempId()].assigned);

            fixed |=
               operand.isPrecolored() && ctx.assignments[operand.tempId()].reg != operand.physReg();

            /* Avoid emitting parallelcopies for vector operands. handle_vector_operands() will
             * temporarily place vector SSA-defs into the register file while handling this
             * instruction. */
            if (operand.isVectorAligned() || (i && instr->operands[i - 1].isVectorAligned()))
               register_file.clear(
                  Operand(operand.getTemp(), ctx.assignments[operand.tempId()].reg));
         }

         bool is_writelane = instr->opcode == aco_opcode::v_writelane_b32 ||
                             instr->opcode == aco_opcode::v_writelane_b32_e64;
         if (program->gfx_level <= GFX9 && is_writelane && instr->operands[0].isTemp() &&
             instr->operands[1].isTemp()) {
            /* v_writelane_b32 can take two sgprs but only if one is m0. */
            if (ctx.assignments[instr->operands[0].tempId()].reg != m0 &&
                ctx.assignments[instr->operands[1].tempId()].reg != m0) {
               instr->operands[0].setPrecolored(m0);
               fixed = true;
            }
         }

         if (fixed)
            handle_fixed_operands(ctx, register_file, parallelcopy, instr);

         for (unsigned i = 0; i < instr->operands.size(); ++i) {
            auto& operand = instr->operands[i];
            if (!operand.isTemp() || operand.isFixed())
               continue;

            if (operand.isVectorAligned()) {
               handle_vector_operands(ctx, register_file, parallelcopy, instr, i);
               continue;
            }

            PhysReg reg = ctx.assignments[operand.tempId()].reg;
            if (operand_can_use_reg(program->gfx_level, instr, i, reg, operand.regClass()))
               operand.setFixed(reg);
            else
               get_reg_for_operand(ctx, register_file, parallelcopy, instr, operand, i);

            if (instr->isEXP() || (instr->isVMEM() && i == 3 && ctx.program->gfx_level == GFX6) ||
                (instr->isDS() && instr->ds().gds)) {
               for (unsigned j = 0; j < operand.size(); j++)
                  ctx.war_hint.set(operand.physReg().reg() + j);
            }
         }
         bool temp_in_scc = register_file[scc];

         optimize_encoding(ctx, register_file, instr);

         auto tied_defs = get_tied_defs(instr.get());
         handle_operands_tied_to_definitions(ctx, parallelcopy, instr, register_file, tied_defs);

         /* remove dead vars from register file */
         for (const Operand& op : instr->operands) {
            if (op.isTemp() && op.isFirstKillBeforeDef())
               register_file.clear(op);
         }

         /* If this is the last p_reload_preserved, we have no more opportunities to restore
          * overwritten ABI-preserved registers. Make sure all remaining temporaries are outside
          * preserved registers, then block the preserved registers to mark them unusable.
          */
         if (instr->opcode == aco_opcode::p_reload_preserved && block.linear_succs.empty()) {
            handle_last_reload_preserved(ctx, instr, parallelcopy, register_file);
         }

         BITSET_DECLARE(call_clobbered_regs, 512);
         if (instr->isCall()) {
            /* Calculate preserved registers, then invert */
            instr->call().abi.preservedRegisters(call_clobbered_regs, ctx.limit);
            for (auto& op : instr->operands) {
               if (!op.isTemp() || !op.isPrecolored() || op.isClobbered())
                  continue;
               for (unsigned i = 0; i < op.size(); ++i)
                  BITSET_SET(call_clobbered_regs, op.physReg().reg() + i);
            }
            BITSET_NOT(call_clobbered_regs);

            /* Allow linear VGPRs in the clobbered range.
             * Linear VGPRs are spilled in spill_preserved, and the stack pointer is always
             * guaranteed to be preserved.
             */
            BITSET_CLEAR_RANGE(call_clobbered_regs, 256 + ctx.vgpr_bounds - ctx.num_linear_vgprs,
                               256 + ctx.vgpr_bounds);

            handle_call(ctx, instr, call_clobbered_regs, vector_splits, parallelcopy,
                        register_file);
         }

         /* handle fixed definitions first */
         for (unsigned i = 0; i < instr->definitions.size(); ++i) {
            auto& definition = instr->definitions[i];
            if (!definition.isFixed())
               continue;

            assert(i >= tied_defs.size());

            adjust_max_used_regs(ctx, definition.regClass(), definition.physReg());
            /* check if the target register is blocked */
            if (register_file.test(definition.physReg(), definition.bytes())) {
               const PhysRegInterval def_regs{definition.physReg(), definition.size()};

               /* create parallelcopy pair to move blocking vars */
               std::vector<unsigned> vars = collect_vars(ctx, register_file, def_regs);

               RegisterFile tmp_file(register_file);
               /* re-enable the killed operands, so that we don't move the blocking vars there */
               tmp_file.fill_killed_operands(instr.get());
               if (instr->isCall()) {
                  unsigned start, end;
                  BITSET_FOREACH_RANGE (start, end, call_clobbered_regs, 512) {
                     PhysRegInterval interval = PhysRegInterval{PhysReg{start}, end - start};
                     tmp_file.block(interval);
                  }
               }

               ASSERTED bool success = false;
               success = get_regs_for_copies(ctx, tmp_file, parallelcopy, vars, instr, def_regs);
               assert(success);

               update_renames(ctx, register_file, parallelcopy, instr);
            }

            if (!definition.isTemp())
               continue;

            ctx.assignments[definition.tempId()].set(definition);
            register_file.fill(definition);
         }

         /* handle normal definitions */
         for (unsigned i = 0; i < instr->definitions.size(); ++i) {
            Definition* definition = &instr->definitions[i];

            if (definition->isFixed() || !definition->isTemp() || i < tied_defs.size())
               continue;

            /* find free reg */
            if (instr->opcode == aco_opcode::p_start_linear_vgpr ||
                (instr->opcode == aco_opcode::p_startpgm &&
                 definition->regClass().is_linear_vgpr())) {
               /* Allocation of linear VGPRs is special. */
               definition->setFixed(alloc_linear_vgpr(ctx, register_file, instr, *definition, parallelcopy));
               update_renames(ctx, register_file, parallelcopy, instr);
            } else if (instr->opcode == aco_opcode::p_split_vector) {
               PhysReg reg = instr->operands[0].physReg();
               RegClass rc = definition->regClass();
               for (unsigned j = 0; j < i; j++)
                  reg.reg_b += instr->definitions[j].bytes();
               if (get_reg_specified(ctx, register_file, rc, instr, reg, -1)) {
                  definition->setFixed(reg);
               } else if (i == 0) {
                  RegClass vec_rc = RegClass::get(rc.type(), instr->operands[0].bytes());
                  DefInfo info(ctx, ctx.pseudo_dummy, vec_rc, -1);
                  std::optional<PhysReg> res = get_reg_simple(ctx, register_file, info);
                  if (res && get_reg_specified(ctx, register_file, rc, instr, *res, -1))
                     definition->setFixed(*res);
               } else if (instr->definitions[i - 1].isFixed()) {
                  reg = instr->definitions[i - 1].physReg();
                  reg.reg_b += instr->definitions[i - 1].bytes();
                  if (get_reg_specified(ctx, register_file, rc, instr, reg, -1))
                     definition->setFixed(reg);
               }
            } else if (instr->opcode == aco_opcode::p_parallelcopy) {
               PhysReg reg = instr->operands[i].physReg();
               if (instr->operands[i].isTemp() &&
                   instr->operands[i].getTemp().type() == definition->getTemp().type() &&
                   !register_file.test(reg, definition->bytes()))
                  definition->setFixed(reg);
            } else if (instr->opcode == aco_opcode::p_extract_vector) {
               PhysReg reg = instr->operands[0].physReg();
               reg.reg_b += definition->bytes() * instr->operands[1].constantValue();
               if (get_reg_specified(ctx, register_file, definition->regClass(), instr, reg, -1))
                  definition->setFixed(reg);
            } else if (instr->opcode == aco_opcode::p_create_vector) {
               PhysReg reg = get_reg_create_vector(ctx, register_file, definition->getTemp(),
                                                   parallelcopy, instr);
               update_renames(ctx, register_file, parallelcopy, instr);
               definition->setFixed(reg);
            } else if (instr_info.classes[(int)instr->opcode] == instr_class::wmma &&
                       instr->operands[2].isTemp() && instr->operands[2].isKill() &&
                       instr->operands[2].regClass() == definition->regClass()) {
               /* For WMMA, the dest needs to either be equal to operands[2], or not overlap it.
                * Here we set a policy of forcing them the same if operands[2] gets killed (and
                * otherwise they don't overlap). This may not be optimal if RA would select a
                * different location due to affinity, but that gets complicated very quickly. */
               definition->setFixed(instr->operands[2].physReg());
            }

            if (!definition->isFixed()) {
               Temp tmp = definition->getTemp();
               PhysReg reg = get_reg(ctx, register_file, tmp, parallelcopy, instr);
               definition->setFixed(reg);
               update_renames(ctx, register_file, parallelcopy, instr);
               if (definition->regClass().is_subdword() && definition->bytes() < 4 &&
                   (reg.byte() || register_file.test(reg, 4))) {
                  bool allow_16bit_write = reg.byte() % 2 == 0 && !register_file.test(reg, 2);
                  add_subdword_definition(program, instr, reg, allow_16bit_write);
                  /* add_subdword_definition can invalidate the reference */
                  definition = &instr->definitions[i];
               }
            }

            assert(
               definition->isFixed() &&
               ((definition->getTemp().type() == RegType::vgpr && definition->physReg() >= 256) ||
                (definition->getTemp().type() != RegType::vgpr && definition->physReg() < 256)));
            ctx.assignments[definition->tempId()].set(*definition);
            register_file.fill(*definition);
         }

         if (!ctx.vector_operands.empty())
            resolve_vector_operands(ctx, register_file, parallelcopy, instr);

         /* This ignores tied defs and vector aligned operands because they are late-kill. */
         undo_renames(ctx, parallelcopy, instr);

         assign_tied_definitions(ctx, instr, register_file, tied_defs);

         handle_pseudo(ctx, register_file, instr.get());

         /* kill definitions and late-kill operands and ensure that sub-dword operands can actually
          * be read */
         for (const Definition& def : instr->definitions) {
            if (def.isTemp() && def.isKill())
               register_file.clear(def);
         }
         for (unsigned i = 0; i < instr->operands.size(); i++) {
            const Operand& op = instr->operands[i];
            if (op.isTemp() && op.isFirstKill() && op.isLateKill())
               register_file.clear(op);
            if (op.isTemp() && op.physReg().byte() != 0)
               add_subdword_operand(ctx, instr, i, op.physReg().byte(), op.regClass());
         }

         for (auto split : vector_splits)
            instructions.emplace_back(split);

         emit_parallel_copy(ctx, parallelcopy, instr, instructions, temp_in_scc, register_file);

         /* some instructions need VOP3 encoding if operand/definition is not assigned to VCC */
         bool instr_needs_vop3 =
            !instr->isVOP3() &&
            ((withoutDPP(instr->format) == Format::VOPC &&
              instr->definitions[0].physReg() != vcc) ||
             (instr->opcode == aco_opcode::v_cndmask_b32 && instr->operands[2].physReg() != vcc) ||
             ((instr->opcode == aco_opcode::v_add_co_u32 ||
               instr->opcode == aco_opcode::v_addc_co_u32 ||
               instr->opcode == aco_opcode::v_sub_co_u32 ||
               instr->opcode == aco_opcode::v_subb_co_u32 ||
               instr->opcode == aco_opcode::v_subrev_co_u32 ||
               instr->opcode == aco_opcode::v_subbrev_co_u32) &&
              instr->definitions[1].physReg() != vcc) ||
             ((instr->opcode == aco_opcode::v_addc_co_u32 ||
               instr->opcode == aco_opcode::v_subb_co_u32 ||
               instr->opcode == aco_opcode::v_subbrev_co_u32) &&
              instr->operands[2].physReg() != vcc));
         if (instr_needs_vop3) {

            /* If the first operand is a literal, we have to move it to an sgpr
             * for generations without VOP3+literal support.
             * Both literals and sgprs count towards the constant bus limit,
             * so this is always valid.
             */
            if (instr->operands.size() && instr->operands[0].isLiteral() &&
                program->gfx_level < GFX10) {
               /* Re-use the register we already allocated for the definition.
                * This works because the instruction cannot have any other SGPR operand.
                */
               Temp tmp = program->allocateTmp(instr->operands[0].size() == 2 ? s2 : s1);
               const Definition& def =
                  instr->isVOPC() ? instr->definitions[0] : instr->definitions.back();
               assert(def.regClass() == s2);
               ctx.assignments.emplace_back(def.physReg(), tmp.regClass());

               Instruction* copy =
                  create_instruction(aco_opcode::p_parallelcopy, Format::PSEUDO, 1, 1);
               copy->operands[0] = instr->operands[0];
               if (copy->operands[0].bytes() < 4)
                  copy->operands[0] = Operand::c32(copy->operands[0].constantValue());
               copy->definitions[0] = Definition(tmp);
               copy->definitions[0].setFixed(def.physReg());

               instr->operands[0] = Operand(tmp);
               instr->operands[0].setFixed(def.physReg());
               instr->operands[0].setFirstKill(true);

               instructions.emplace_back(copy);
            }

            /* change the instruction to VOP3 to enable an arbitrary register pair as dst */
            instr->format = asVOP3(instr->format);
         }

         instructions.emplace_back(std::move(*instr_it));

         /* If we split any vectors to resolve packing constraints, recreate the vectors here.
          * TODO: we could actually defer vector creation until next use or end of block. For now,
          * having to split vectors because of register space constraints is a theoretical edge case
          * that isn't really hit in practice, so spending effort on it wouldn't yield any benefit.
          */
         if (!vector_splits.empty())
            recreate_blocking_vectors(ctx, vector_splits, instructions, register_file);
      } /* end for Instr */

      if ((block.kind & block_kind_top_level) && block.linear_succs.empty()) {
         /* Reset this for block_kind_resume. */
         ctx.num_linear_vgprs = 0;

         ASSERTED PhysRegInterval vgpr_bounds = get_reg_bounds(ctx, RegType::vgpr, false);
         ASSERTED PhysRegInterval sgpr_bounds = get_reg_bounds(ctx, RegType::sgpr, false);
         assert(register_file.count_zero_or_blocked(vgpr_bounds) == ctx.vgpr_bounds);
         assert(register_file.count_zero_or_blocked(sgpr_bounds) == ctx.sgpr_bounds);
      } else if (should_compact_linear_vgprs(ctx, register_file)) {
         aco_ptr<Instruction> br = std::move(instructions.back());
         instructions.pop_back();

         bool temp_in_scc =
            register_file[scc] || (!br->operands.empty() && br->operands[0].physReg() == scc);

         std::vector<parallelcopy> parallelcopy;
         compact_linear_vgprs(ctx, register_file, parallelcopy);
         update_renames(ctx, register_file, parallelcopy, br);
         emit_parallel_copy_internal(ctx, parallelcopy, br, instructions, temp_in_scc, register_file);

         instructions.push_back(std::move(br));
      }

      block.instructions = std::move(instructions);
   } /* end for BB */

   /* num_gpr = rnd_up(max_used_gpr + 1) */
   program->config->num_vgprs =
      std::min<uint16_t>(get_vgpr_alloc(program, ctx.max_used_vgpr + 1), 256);
   program->config->num_sgprs = get_sgpr_alloc(program, ctx.max_used_sgpr + 1);

   program->progress = CompilationProgress::after_ra;
}

} // namespace aco
