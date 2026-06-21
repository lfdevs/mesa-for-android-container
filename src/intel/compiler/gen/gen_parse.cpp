/*
 * Copyright © 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

#include <charconv>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "util/bitscan.h"
#include "util/lut.h"
#include "util/ralloc.h"

#include "gen_private.h"

namespace {

#define SV_FMT(s) static_cast<int>((s).size()), (s).data()
#define SV_ARGS(s) (s).data(), (s).size()

struct cut_result {
   std::string_view before;
   std::string_view after;
   bool found;
};

static cut_result
cut(std::string_view s, char c)
{
   const size_t pos = s.find(c);
   if (pos == std::string_view::npos)
      return { s, std::string_view(), false };

   return { s.substr(0, pos), s.substr(pos + 1), true };
}

static std::string_view
trim(std::string_view s)
{
   size_t begin = 0;
   while (begin < s.size() && isspace((unsigned char)s[begin]))
      begin++;

   size_t end = s.size();
   while (end > begin && isspace((unsigned char)s[end - 1]))
      end--;

   return s.substr(begin, end - begin);
}

static std::string_view
strip_line_comment(std::string_view s)
{
   const size_t pos = s.find("//");
   if (pos == std::string_view::npos)
      return s;

   return s.substr(0, pos);
}

enum {
   GEN_CHANNEL_X = 0,
   GEN_CHANNEL_Y = 1,
   GEN_CHANNEL_Z = 2,
   GEN_CHANNEL_W = 3,
};

static inline unsigned
gen_msg_dest_len(const struct intel_device_info *devinfo,
                     enum lsc_data_size data_sz,
                     unsigned n)
{
   return DIV_ROUND_UP(lsc_data_size_bytes(data_sz) * n,
                       devinfo->grf_size);
}

static inline unsigned
gen_msg_addr_len(const struct intel_device_info *devinfo,
                     enum lsc_addr_size addr_sz,
                     unsigned n)
{
   return DIV_ROUND_UP(lsc_addr_size_bytes(addr_sz) * n,
                       devinfo->grf_size);
}

static bool is_alpha(char c)  { return isalpha((unsigned char)c); }
static bool is_digit(char c)  { return isdigit((unsigned char)c); }
static bool is_alnum(char c)  { return isalnum((unsigned char)c); }
static bool is_space(char c)  { return isspace((unsigned char)c); }

static bool is_ident_start(char c) { return is_alpha(c) || c == '_'; }
static bool is_ident_char(char c)  { return is_alnum(c) || c == '_'; }

static bool
is_ident(std::string_view s)
{
   if (s.empty() || !is_ident_start(s[0]))
      return false;
   for (char c : s) {
      if (!is_ident_char(c))
         return false;
   }
   return true;
}

static bool
is_number_prefix(char c)
{
   return isdigit((unsigned char)c) || c == '+' || c == '-';
}

static bool
starts_with(std::string_view s, std::string_view prefix)
{
   return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

static std::optional<size_t>
find_matching_paren(std::string_view s)
{
   if (s.empty() || s[0] != '(')
      return {};

   unsigned depth = 0;
   for (size_t i = 0; i < s.size(); i++) {
      if (s[i] == '(') {
         depth++;
      } else if (s[i] == ')') {
         depth--;
         if (depth == 0)
            return i;
      }
   }

   return {};
}

static bool
send_has_symbolic_src1(enum lsc_opcode op)
{
   return lsc_opcode_is_store(op) ||
          (lsc_opcode_is_atomic(op) && lsc_op_num_data_values(op) > 0);
}

static bool
is_notification(const gen_operand &op)
{
   return op.file == GEN_ARF &&
          (op.nr & 0xf0) == GEN_ARF_NOTIFICATION_COUNT;
}

struct gen_parser {
   struct label_use {
      int inst_idx;
      unsigned src_index;
      unsigned line;
      std::string_view label;
   };

   gen_parse_params *params;
   const intel_device_info *devinfo;
   std::string_view text = {};
   std::vector<gen_inst> insts;
   std::map<std::string_view, int> labels;
   std::vector<label_use> label_uses;
   std::vector<gen_error> errors;

   /* Per-line state. */
   std::string_view line;
   unsigned line_no = 0;
   gen_inst inst = {};
   gen_format format = GEN_FORMAT_ILLEGAL;
   unsigned num_sources = 0;
   bool has_dst = false;
   bool dst_has_writemask = false;
   bool src_has_swizzle[3] = {};

   struct lsc_parse_info {
      enum lsc_opcode            op          = LSC_OP_LOAD;
      enum lsc_addr_surface_type addr_type   = LSC_ADDR_SURFTYPE_FLAT;
      enum lsc_addr_size         addr_size   = LSC_ADDR_SIZE_A32;
      enum lsc_data_size         data_size   = LSC_DATA_SIZE_D32;
      enum lsc_cmask             cmask       = LSC_CMASK_X;
      enum lsc_fence_scope       fence_scope = LSC_FENCE_LOCAL;
      enum lsc_flush_type        flush_type  = LSC_FLUSH_TYPE_NONE;

      unsigned num_values   = 1;
      unsigned cache_ctrl   = 0;
      bool     transpose    = false;
      bool     route_to_lsc = false;
      bool     valid        = false;
   };

   lsc_parse_info lsc;

   explicit gen_parser(gen_parse_params *params)
      : params(params),
        devinfo(params->devinfo)
   {
      assert(devinfo);
      assert(params->mem_ctx);
      assert(params->insts == NULL);
      assert(params->errors == NULL);
      assert(params->text || params->text_size == 0);
      assert(params->text_size >= 0);
   }

   bool
   parse()
   {
      if (params->text_size)
         text = std::string_view(params->text, params->text_size);

      while (!text.empty()) {
         line_no++;

         auto [before, after, _] = cut(text, '\n');
         line = trim(strip_line_comment(before));

         if (!line.empty()) {
            if (!try_parse_label())
               parse_instruction();
         }

         text = after;
      }

      if (errors.empty())
         resolve_labels();

      if (!errors.empty()) {
         params->errors = ralloc_array(params->mem_ctx, gen_error, errors.size());
         params->num_errors = errors.size();
         memcpy(params->errors, errors.data(), errors.size() * sizeof(gen_error));
         return false;
      }

      params->num_insts = insts.size();
      if (!insts.empty()) {
         params->insts = ralloc_array(params->mem_ctx, gen_inst, insts.size());
         memcpy(params->insts, insts.data(), insts.size() * sizeof(gen_inst));
      }
      return true;
   }

   bool
   try_parse_label()
   {
      auto [name, rest, found] = cut(line, ':');
      if (!found || !is_ident(name) || !trim(rest).empty())
         return false;

      if (labels.find(name) != labels.end())
         errorf("duplicate label '%.*s'", SV_FMT(name));
      else
         labels[name] = insts.size();

      return true;
   }

   bool
   parse_instruction()
   {
      inst = {};
      format = GEN_FORMAT_ILLEGAL;
      num_sources = 0;
      has_dst = false;
      dst_has_writemask = false;
      src_has_swizzle[0] = false;
      src_has_swizzle[1] = false;
      src_has_swizzle[2] = false;
      lsc = {};

      if (!parse_prefix())
         return false;

      if (!parse_opcode())
         return false;

      skip_ws();

      format = gen_inst_format(inst.opcode);
      num_sources = gen_inst_num_sources(devinfo, &inst);
      has_dst = gen_inst_has_dst(inst.opcode);

      if (!parse_execution())
         return false;

      if (!parse_cmod())
         return false;

      std::vector<label_use> inst_label_uses;

      switch (format) {
      case GEN_FORMAT_BASIC_ONE_SRC:
      case GEN_FORMAT_BASIC_TWO_SRC:
      case GEN_FORMAT_BASIC_THREE_SRC:
      case GEN_FORMAT_DPAS_THREE_SRC:
         if (!parse_basic_format())
            return false;
         break;

      case GEN_FORMAT_SEND:
         if (!parse_send_format())
            return false;
         break;

      case GEN_FORMAT_BRANCH_ONE_SRC:
      case GEN_FORMAT_BRANCH_TWO_SRC:
         if (!parse_branch_format(inst_label_uses))
            return false;
         break;

      case GEN_FORMAT_NOP:
      case GEN_FORMAT_ILLEGAL:
         /* Nothing to do. */
         break;
      }

      if (!parse_options())
         return false;

      if (!line.empty())
         return errorf("unexpected trailing text");

      if (inst.align16) {
         if (has_dst && !dst_has_writemask)
            inst.dst.writemask = 0xF;

         for (unsigned i = 0; i < num_sources; i++) {
            if (inst.src[i].file != GEN_IMM && !src_has_swizzle[i]) {
               inst.src[i].swizzle = (GEN_CHANNEL_X << 0) |
                                     (GEN_CHANNEL_Y << 2) |
                                     (GEN_CHANNEL_Z << 4) |
                                     (GEN_CHANNEL_W << 6);
            }
         }
      }

      label_uses.insert(label_uses.end(), inst_label_uses.begin(),
                        inst_label_uses.end());

      insts.push_back(inst);
      return true;
   }

   bool
   parse_prefix()
   {
      if (consume('(')) {
         if (consume('W'))
            inst.no_mask = true;

         if (!inst.no_mask || consume('&')) {
            if (!parse_predicate_ref())
               return false;
         }

         if (!consume(')'))
            return errorf("expected ')'");

         skip_ws();
      }

      return true;
   }

   bool
   parse_opcode()
   {
      auto opcode = consume_ident_token();

      bool valid;
      inst.opcode = gen_opcode_from_string(SV_ARGS(opcode), &valid);
      if (valid) {
         if (!parse_send_suffixes())
            return false;

      } else if (devinfo->has_lsc) {
         inst.opcode = GEN_OP_SEND;

         lsc.op = gen_lsc_opcode_from_string(SV_ARGS(opcode), &valid);
         if (!valid)
            return errorf("unknown LSC symbolic opcode '%.*s'", SV_FMT(opcode));

         if (!consume('.'))
            return errorf("expected SFID after LSC opcode");
         auto sfid = consume_ident_token();
         inst.send.sfid = gen_sfid_from_string(devinfo, SV_ARGS(sfid), &valid);
         if (!valid)
            return errorf("unsupported LSC symbolic SFID '%.*s'", SV_FMT(sfid));
         if (devinfo->ver < 20 && inst.send.sfid == GEN_SFID_URB)
            return errorf("LSC symbolic URB syntax requires Xe2+");

         lsc.valid = true;

         if (lsc.op == LSC_OP_FENCE) {
            if (!parse_lsc_fence_suffixes())
               return false;
         } else {
            if (!parse_lsc_memory_suffixes())
               return false;
         }

      } else {
         return errorf("couldn't parse opcode");
      }

      skip_ws();
      return true;
   }

   bool
   parse_send_suffixes()
   {
      bool valid;

      if (consume(".sat"))
         inst.saturate = true;

      skip_ws();

      if (consume('.')) {
         if (inst.opcode == GEN_OP_MATH) {
            auto func = consume_ident_token();
            inst.math.func = gen_math_function_from_string(SV_ARGS(func), &valid);
            if (!valid)
               return errorf("invalid math function '%.*s'", SV_FMT(func));

         } else if (inst.opcode == GEN_OP_SYNC) {
            auto func = consume_ident_token();
            inst.sync.func = gen_sync_function_from_string(SV_ARGS(func), &valid);
            if (!valid)
               return errorf("invalid sync function '%.*s'", SV_FMT(func));

         } else if (gen_inst_is_send(&inst)) {
            auto sfid = consume_ident_token();
            inst.send.sfid = gen_sfid_from_string(devinfo, SV_ARGS(sfid), &valid);
            if (!valid)
               return errorf("invalid SFID '%.*s'", SV_FMT(sfid));

         } else if (inst.opcode == GEN_OP_BFN) {
            if (peek() == '(') {
               const std::optional<size_t> close = find_matching_paren(line);
               if (!close)
                  return errorf("invalid BFN function control");

               const std::string bfn(consume_n(*close + 1));
               bool ok = false;
               inst.boolean_func_ctrl = util_lut3_parse(bfn.c_str(), &ok);
               if (!ok)
                  return errorf("invalid BFN function control");
            } else {
               uint64_t v = 0;
               if (!consume_unsigned(v) || v > 0xff)
                  return errorf("invalid BFN function control");
               inst.boolean_func_ctrl = v;
            }

         } else if (inst.opcode == GEN_OP_DPAS) {
            unsigned sdepth = 0;
            unsigned rcount = 0;
            if (!consume_uint(sdepth) || !consume('x') || !consume_uint(rcount))
               return errorf("invalid DPAS function control");
            inst.dpas.sdepth = sdepth;
            inst.dpas.rcount = rcount;

         } else if (gen_has_branch_ctrl(inst.opcode)) {
            if (consume_ident_token() == "b")
               inst.branch_control = true;
         }
      }

      /* We also allow .sat after. */
      if (consume(".sat"))
         inst.saturate = true;

      skip_ws();
      return true;
   }

   bool
   parse_lsc_fence_suffixes()
   {
      bool valid;

      consume('.');

      auto fence_scope = consume_ident_token();
      lsc.fence_scope = gen_lsc_fence_scope_from_string(SV_ARGS(fence_scope), &valid);
      if (!valid)
         return errorf("unknown LSC fence scope '%.*s'", SV_FMT(fence_scope));

      consume('.');

      auto flush_type = consume_ident_token();
      lsc.flush_type = gen_lsc_flush_type_from_string(SV_ARGS(flush_type), &valid);
      if (!valid)
         return errorf("unknown LSC fence flush type '%.*s'", SV_FMT(flush_type));

      lsc.addr_type = LSC_ADDR_SURFTYPE_FLAT;

      if (consume(".route_to_lsc"))
         lsc.route_to_lsc = true;

      skip_ws();
      return true;
   }

   bool
   parse_lsc_memory_suffixes()
   {
      bool valid;

      const bool allow_vector = !lsc_opcode_has_cmask(lsc.op) &&
                                !lsc_opcode_is_atomic(lsc.op);

      /* Data descriptor: dN[xM[t]]. */
      consume('.');
      auto data_name =
         consume_while([](char c) { return is_ident_char(c) && c != 'x'; });
      lsc.data_size = gen_lsc_data_size_from_string(SV_ARGS(data_name), &valid);
      if (!valid)
         return errorf("malformed LSC data descriptor '%.*s'", SV_FMT(data_name));

      lsc.num_values = 1;
      lsc.transpose = false;
      if (consume('x')) {
         if (!allow_vector)
            return errorf("LSC opcode does not allow vector data form");
         unsigned n = 0;
         if (!consume_uint(n))
            return errorf("malformed LSC vector count");
         lsc.num_values = n;
         if (consume('t'))
            lsc.transpose = true;
      }

      /* Optional component mask. */
      if (lsc_opcode_has_cmask(lsc.op)) {
         consume('.');
         auto cmask = consume_ident_token();
         lsc.cmask = gen_lsc_cmask_from_string(SV_ARGS(cmask), &valid);
         if (!valid)
            return errorf("unknown LSC component mask '%.*s'", SV_FMT(cmask));
      }

      /* Address size. */
      consume('.');
      auto addr_size = consume_ident_token();
      lsc.addr_size = gen_lsc_addr_size_from_string(SV_ARGS(addr_size), &valid);
      if (!valid)
         return errorf("unknown LSC address size '%.*s'", SV_FMT(addr_size));

      /* Optional cache control: ".l1xx.l3yy" form. Try to consume two
       * dotted ident tokens; if they don't form a valid cache_ctrl, restore
       * line so the dot is left for the surface selector below.
       */
      {
         const auto saved = line;
         bool ok = false;
         if (consume('.')) {
            const auto a = consume_ident_token();
            if (consume('.')) {
               const auto b = consume_ident_token();
               std::string name(a);
               name += '.';
               name += b;
               const unsigned cc =
                  gen_lsc_cache_ctrl_from_string(devinfo, lsc.op,
                                                 SV_ARGS(name), &valid);
               if (valid) {
                  lsc.cache_ctrl = cc;
                  ok = true;
               }
            }
         }
         if (!ok)
            line = saved;
      }

      /* Optional surface selector. */
      lsc.addr_type = LSC_ADDR_SURFTYPE_FLAT;
      if (consume('.')) {
         if (consume("flat")) {
            lsc.addr_type = LSC_ADDR_SURFTYPE_FLAT;
         } else if (consume("bss[a0.")) {
            unsigned subreg = 0;
            if (!consume_uint(subreg) || !consume(']'))
               return errorf("malformed bindless surface reference");
            lsc.addr_type = LSC_ADDR_SURFTYPE_BSS;
            inst.send.ex_desc_is_reg = true;
            inst.send.ex_desc_subnr = subreg * 2;
         } else if (consume("ss[a0.")) {
            unsigned subreg = 0;
            if (!consume_uint(subreg) || !consume(']'))
               return errorf("malformed surface-state reference");
            lsc.addr_type = LSC_ADDR_SURFTYPE_SS;
            inst.send.ex_desc_is_reg = true;
            inst.send.ex_desc_subnr = subreg * 2;
         } else if (consume("bti[")) {
            unsigned bti = 0;
            if (!consume_uint(bti) || !consume(']'))
               return errorf("malformed BTI surface reference");
            lsc.addr_type = LSC_ADDR_SURFTYPE_BTI;
            gen_lsc_ex_desc ex_desc = {};
            ex_desc.addr_type = LSC_ADDR_SURFTYPE_BTI;
            ex_desc.bti.index = bti;
            inst.send.ex_desc_imm =
               gen_lsc_ex_desc_encode(devinfo, &ex_desc, NULL);
         } else {
            auto bad = consume_ident_token();
            return errorf("unknown LSC surface selector '%.*s'", SV_FMT(bad));
         }
      }

      if (inst.send.sfid == GEN_SFID_SLM &&
          lsc.addr_type != LSC_ADDR_SURFTYPE_FLAT)
         return errorf("SLM symbolic syntax does not take a surface selector");

      if (inst.send.sfid == GEN_SFID_URB &&
          lsc.addr_type != LSC_ADDR_SURFTYPE_FLAT)
         return errorf("URB symbolic syntax does not take a surface selector");

      return true;
   }

   bool
   parse_execution()
   {
      if (peek() == '(' && is_digit(peek(1))) {
         if (!consume('('))
            return errorf("expected '('");

         unsigned exec_size = 0;
         if (!consume_uint(exec_size))
            return errorf("expected execution size");
         inst.exec_size = exec_size;

         if (consume('|')) {
            if (!consume('M'))
               return errorf("expected 'M'");

            unsigned chan_offset = 0;
            if (!consume_uint(chan_offset))
               return errorf("expected channel offset");
            inst.chan_offset = chan_offset;
         }

         if (!consume(')'))
            return errorf("expected ')'");

         skip_ws();
      }

      return true;
   }

   bool
   parse_cmod()
   {
      if (peek() == '(' && is_alpha(peek(1))) {
         if (!consume('('))
            return errorf("expected '('");

         const std::string_view name = consume_while(is_alpha);
         if (!consume(')'))
            return errorf("expected ')'");

         bool valid;
         inst.cmod = gen_condition_from_string(SV_ARGS(name), &valid);
         if (!valid) {
            return errorf("unknown conditional modifier '%.*s'", SV_FMT(name));
         }

         if (!parse_flag_ref(false))
            return false;

         skip_ws();
      }

      return true;
   }

   bool
   parse_basic_format()
   {
      if (has_dst) {
         if (!parse_dst())
            return false;
      }

      if (inst.opcode == GEN_OP_WAIT) {
         inst.no_mask = true;
         inst.src[0].file = inst.dst.file;
         inst.src[0].nr = inst.dst.nr;
         inst.src[0].subnr = inst.dst.subnr;
         inst.src[0].type = inst.dst.type;
         inst.src[0].region = { 0, 1, 0 };
         return true;
      }

      for (unsigned i = 0; i < num_sources; i++) {
         if (!parse_src(inst.src[i], i))
            return false;
      }
      return true;
   }

   bool
   parse_branch_format(std::vector<label_use> &inst_label_uses)
   {
      /* Can omit operands for most structured control flow. */
      if (inst.opcode == GEN_OP_IF ||
          inst.opcode == GEN_OP_ELSE ||
          inst.opcode == GEN_OP_ENDIF ||
          inst.opcode == GEN_OP_BREAK ||
          inst.opcode == GEN_OP_CONTINUE ||
          inst.opcode == GEN_OP_HALT) {
         if (!peek() || peek() == '{')
            return true;
      }

      if (!consume("jip:") &&
          (inst.opcode == GEN_OP_JMPI ||
           inst.opcode == GEN_OP_BRD ||
           inst.opcode == GEN_OP_BRC)) {
         /* When those instructions are used and there's no 'jip:', parse the
          * operand as regular source operands (which can be registers or
          * immediates).  For label targets with those use the prefix.
          */
         if (!parse_src(inst.src[0], 0))
            return false;

      } else {
         /* Note that 'jip:' prefix was already consumed if present. */
         if (!parse_branch_target(0, inst_label_uses))
            return false;

         if (format == GEN_FORMAT_BRANCH_TWO_SRC) {
            skip_ws();
            if (!consume("uip:")) {
               return errorf("expected 'uip:'");
            }

            if (!parse_branch_target(1, inst_label_uses))
               return false;
         }
      }

      return true;
   }

   bool
   errorf(const char *fmt, ...)
   {
      va_list args;
      va_start(args, fmt);
      errors.push_back({ line_no, ralloc_vasprintf(params->mem_ctx, fmt, args) });
      va_end(args);
      return false;
   }

   char
   peek(unsigned n = 0) const
   {
      return n < line.size() ? line[n] : '\0';
   }

   template <typename Pred>
   std::string_view peek_while(Pred pred) const
   {
      size_t n = 0;
      while (n < line.size() && pred(line[n]))
         n++;
      return line.substr(0, n);
   }

   template <typename Pred>
   std::string_view peek_until(Pred pred) const
   {
      return peek_while([&](char c) { return !pred(c); });
   }

   template <typename Pred>
   std::string_view consume_while(Pred pred)
   {
      const auto r = peek_while(pred);
      line.remove_prefix(r.size());
      return r;
   }

   std::string_view
   consume_n(size_t n)
   {
      assert(n <= line.size());
      const auto r = line.substr(0, n);
      line.remove_prefix(n);
      return r;
   }

   void
   skip_ws()
   {
      consume_while(is_space);
   }

   bool
   consume(char c)
   {
      if (peek() != c)
         return false;
      line.remove_prefix(1);
      return true;
   }

   bool
   consume(std::string_view prefix)
   {
      if (!starts_with(line, prefix))
         return false;
      line.remove_prefix(prefix.size());
      return true;
   }

   bool
   consume_ident(std::string_view ident)
   {
      if (!starts_with(line, ident) || is_ident_char(peek(ident.size())))
         return false;
      line.remove_prefix(ident.size());
      return true;
   }

   std::string_view
   consume_ident_token()
   {
      if (!is_ident_start(peek()))
         return {};
      return consume_while(is_ident_char);
   }

   bool
   consume_uint(unsigned &value)
   {
      if (!is_digit(peek()))
         return false;

      uint64_t v = 0;
      const char *begin = line.data();
      const char *end = line.data() + line.size();
      const auto result = std::from_chars(begin, end, v, 10);
      if (result.ptr == begin || result.ec != std::errc())
         return false;

      line.remove_prefix(result.ptr - begin);
      value = v;
      return true;
   }

   bool
   consume_integer(bool &negative, uint64_t &magnitude)
   {
      const std::string_view saved = line;

      negative = false;
      if (peek() == '+' || peek() == '-') {
         negative = peek() == '-';
         line.remove_prefix(1);
      }

      int base = 10;
      if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
         base = 16;
         line.remove_prefix(2);
      }

      const char *begin = line.data();
      const char *end = line.data() + line.size();
      magnitude = 0;
      const auto result = std::from_chars(begin, end, magnitude, base);
      if (result.ptr == begin || result.ec != std::errc()) {
         line = saved;
         return false;
      }
      line.remove_prefix(result.ptr - begin);
      return true;
   }

   bool
   consume_unsigned(uint64_t &value)
   {
      bool negative;
      uint64_t magnitude;
      if (!consume_integer(negative, magnitude))
         return false;
      value = negative ? 0ull - magnitude : magnitude;
      return true;
   }

   bool
   consume_signed(int64_t &value)
   {
      const std::string_view saved = line;
      bool negative;
      uint64_t magnitude;
      if (!consume_integer(negative, magnitude))
         return false;

      const uint64_t max_pos =
         static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
      const uint64_t limit = negative ? max_pos + 1 : max_pos;
      if (magnitude > limit) {
         line = saved;
         return false;
      }

      if (!negative)
         value = static_cast<int64_t>(magnitude);
      else if (magnitude == limit)
         value = std::numeric_limits<int64_t>::min();
      else
         value = -static_cast<int64_t>(magnitude);
      return true;
   }


   bool
   consume_optional_zero_subreg(const char *what)
   {
      if (!consume('.'))
         return true;

      unsigned subnr = 0;
      if (!consume_uint(subnr)) {
         return errorf("malformed %s subregister", what);
      }

      if (subnr != 0) {
         return errorf("%s subregister must be 0", what);
      }

      return true;
   }

   void
   get_lsc_lengths(unsigned &mlen, unsigned &ex_mlen, unsigned &rlen) const
   {
      mlen = 0;
      ex_mlen = 0;
      rlen = 0;

      if (lsc.op == LSC_OP_FENCE) {
         mlen = 1;
      } else {
         const unsigned coord_components = inst.send.sfid == GEN_SFID_TGM ? 4 : 1;
         mlen = gen_msg_addr_len(devinfo, lsc.addr_size,
                                 inst.exec_size * coord_components);
      }

      unsigned values;
      if (lsc.op == LSC_OP_FENCE)
         values = 0;
      else if (lsc_opcode_has_cmask(lsc.op))
         values = util_bitcount((unsigned)lsc.cmask);
      else if (lsc_opcode_is_atomic(lsc.op))
         values = lsc_op_num_data_values(lsc.op);
      else
         values = lsc.num_values;

      if (send_has_symbolic_src1(lsc.op)) {
         ex_mlen = values == 0 ? 0 :
                   gen_msg_dest_len(devinfo, lsc.data_size,
                                    inst.exec_size * values);
      }

      if (is_null(inst.dst))
         rlen = 0;
      else if (lsc.op == LSC_OP_FENCE)
         rlen = 1;
      else if (lsc_opcode_is_store(lsc.op))
         rlen = 0;
      else if (lsc_opcode_is_atomic(lsc.op))
         rlen = gen_msg_dest_len(devinfo, lsc.data_size, inst.exec_size);
      else
         rlen = gen_msg_dest_len(devinfo, lsc.data_size, inst.exec_size * values);
   }

   bool
   validate_lsc_lengths(int dst_length, int src0_length,  int src1_length,
                        unsigned mlen,  unsigned ex_mlen, unsigned rlen)
   {
      /* Symbolic LSC syntax fully determines mlen/rlen/ex_mlen. The ':N'
       * suffixes on the operands are optional; when present they must match
       * the derived values.
       */
      if (dst_length >= 0 && (unsigned)dst_length != rlen) {
         return errorf("dst length ':%d' does not match mnemonic-implied rlen=%u",
                       dst_length, rlen);
      }
      if (src0_length >= 0 && (unsigned)src0_length != mlen) {
         return errorf("src0 length ':%d' does not match mnemonic-implied mlen=%u",
                       src0_length, mlen);
      }
      if (src1_length >= 0 && (unsigned)src1_length != ex_mlen) {
         return errorf("src1 length ':%d' does not match mnemonic-implied ex_mlen=%u",
                       src1_length, ex_mlen);
      }

      return true;
   }

   bool
   expect_operand()
   {
      skip_ws();
      if (line.empty() || peek() == '{')
         return errorf("expected operand");
      return true;
   }

   bool
   parse_predicate_ref()
   {
      if (consume('~'))
         inst.pred_inv = true;

      if (!parse_flag_ref(true))
         return false;

      if (!inst.pred_control)
         inst.pred_control = GEN_PREDICATE_NORMAL;

      return true;
   }

   bool
   parse_flag_ref(bool include_pred_suffix)
   {
      if (!consume('f'))
         return errorf("expected flag register reference");

      unsigned nr = 0, subnr = 0;
      if (!consume_uint(nr) || !consume('.') || !consume_uint(subnr))
         return errorf("malformed flag register reference");

      if (!include_pred_suffix && inst.pred_control &&
          (inst.flag_nr != nr || inst.flag_subnr != subnr)) {
         return errorf("predicate and conditional modifier must use the same flag register");
      }

      inst.flag_nr = nr;
      inst.flag_subnr = subnr;

      if (!include_pred_suffix)
         return true;

      if (!consume('.'))
         return true;

      const std::string_view suffix = consume_while(is_alnum);

      const bool suffix_align16 = suffix == "x" || suffix == "y" ||
                                  suffix == "z" || suffix == "w";
      bool valid;
      inst.pred_control = gen_predicate_from_string(devinfo,
                                                    inst.align16 || suffix_align16,
                                                    SV_ARGS(suffix), &valid);
      if (!valid)
         return errorf("unknown predicate control '%.*s'", SV_FMT(suffix));

      if (suffix_align16)
         inst.align16 = true;

      return true;
   }

   bool
   parse_dst()
   {
      if (!expect_operand())
         return false;

      if (!parse_register_base(inst.dst))
         return false;

      if (inst.dst.file == GEN_IMM)
         return errorf("destination cannot be immediate");

      if (consume('<')) {
         unsigned hstride = 0;
         if (!consume_uint(hstride) || !consume('>'))
            return errorf("malformed destination region");
         inst.dst.region.hstride = hstride;
      } else {
         inst.dst.region.hstride = 1;
      }

      if (consume('.')) {
         inst.align16 = true;
         dst_has_writemask = true;
         unsigned writemask = 0;
         if (!parse_writemask(writemask))
            return false;
         inst.dst.writemask = writemask;
      }

      auto dst_type = parse_type_if_present();
      if (!dst_type)
         return false;
      inst.dst.type = *dst_type;

      if (!inst.dst.indirect &&
          (inst.dst.file == GEN_GRF || is_notification(inst.dst))) {
         const unsigned type_size = MAX2(gen_type_size_bytes(inst.dst.type), 1u);
         inst.dst.subnr *= type_size;
      }

      return true;
   }

   bool
   parse_src(gen_operand &src, unsigned src_idx)
   {
      if (!expect_operand())
         return false;

      const bool is_3src = format == GEN_FORMAT_BASIC_THREE_SRC;

      if (consume('-'))
         src.negate = true;
      if (consume("(abs)"))
         src.abs = true;

      if (is_number_prefix(peek())) {
         if (!parse_immediate(src))
            return false;
         if (src.negate) {
            src.imm = (uint64_t)-(int64_t)src.imm;
            src.negate = false;
         }
      } else {
         if (!parse_register_base(src))
            return false;

         bool has_explicit_region = false;
         if (consume('<')) {
            has_explicit_region = true;
            if (is_3src) {
               if (!parse_src_region_3src(src.region, src_idx))
                  return false;
            } else {
               if (!parse_src_region(src.region))
                  return false;
            }
         } else {
            src.region = { 1, 1, 0 };
         }

         if (consume('.')) {
            if (is_3src) {
               if (!consume_ident("r"))
                  return errorf("expected 3-src replicate-control suffix '.r'");

               inst.align16 = true;
               src.rep_ctrl = true;
               if (!has_explicit_region)
                  src.region = { 0, 1, 0 };
            } else {
               inst.align16 = true;
               src_has_swizzle[src_idx] = true;
               unsigned swizzle = 0;
               if (!parse_swizzle(swizzle))
                  return false;
               src.swizzle = swizzle;
            }
         }

         auto src_type = parse_type_if_present();
         if (!src_type)
            return false;
         src.type = *src_type;

         if (!src.indirect && (src.file == GEN_GRF || is_notification(src))) {
            const unsigned type_size = MAX2(gen_type_size_bytes(src.type), 1u);
            src.subnr *= type_size;
         }
      }

      return true;
   }


   bool
   parse_register_base(gen_operand &op)
   {
      const bool is_send = gen_inst_is_send(&inst);

      if (consume("r[")) {
         if (is_send && consume('s')) {
            unsigned idx = 0;
            if (!consume_uint(idx))
               return errorf("expected scalar register index");
            op.file = GEN_ARF;
            op.nr = GEN_ARF_SCALAR + idx;
            if (consume('.')) {
               unsigned subnr = 0;
               if (!consume_uint(subnr))
                  return errorf("malformed scalar subregister");
               op.subnr = subnr;
            }
            if (!consume(']'))
               return errorf("expected ']'");
            return true;
         }

         op.file = GEN_GRF;
         op.indirect = true;
         if (!consume_ident("a0"))
            return errorf("expected a0 in indirect register");

         if (!consume('.'))
            return errorf("expected address subregister");

         unsigned subnr = 0;
         if (!consume_uint(subnr))
            return errorf("malformed address subregister");
         op.subnr = subnr;

         skip_ws();
         if (consume('+')) {
            skip_ws();
            unsigned offset = 0;
            if (!consume_uint(offset))
               return errorf("expected indirect register offset");
            op.addr_imm = offset;
         } else if (consume('-')) {
            skip_ws();
            unsigned offset = 0;
            if (!consume_uint(offset))
               return errorf("expected indirect register offset");
            op.addr_imm = -(int)offset;
         }

         if (!consume(']')) {
            return errorf("expected ']'");
         }

         return true;
      }

      if (peek() == 'r' && isdigit((unsigned char)peek(1))) {
         consume('r');
         op.file = GEN_GRF;
         if (!consume_uint(op.nr))
            return errorf("expected GRF number");

         if (peek() == '.' && isdigit((unsigned char)peek(1))) {
            consume('.');
            if (is_send)
               return errorf("send payload register cannot use a subregister");
            unsigned subnr = 0;
            if (!consume_uint(subnr))
               return errorf("expected GRF subregister");
            op.subnr = subnr;
         }

         return true;
      }

      if (consume_ident("bad")) {
         op.file = GEN_BAD_FILE;
         return true;
      }

      return parse_arf_base(op);
   }

   bool
   parse_arf_base(gen_operand &op)
   {
      op.file = GEN_ARF;

      if (consume_ident("null")) {
         op.nr = GEN_ARF_NULL;
         return true;
      }

      if (consume_ident("ip")) {
         op.nr = GEN_ARF_IP;
         return true;
      }

      if (consume("tdr")) {
         unsigned idx = 0;
         if (!consume_uint(idx))
            return errorf("expected TDR index");
         op.nr = GEN_ARF_TDR + idx;
         return consume_optional_zero_subreg("TDR");
      }

      if (consume("mask")) {
         unsigned idx = 0;
         if (!consume_uint(idx))
            return errorf("expected mask register index");
         op.nr = GEN_ARF_MASK + idx;
         return consume_optional_zero_subreg("mask register");
      }

      if (consume("acc")) {
         unsigned idx = 0;
         if (!consume_uint(idx))
            return errorf("expected accumulator index");
         op.nr = GEN_ARF_ACCUMULATOR + idx;
         if (consume('.')) {
            unsigned subnr = 0;
            if (!consume_uint(subnr)) {
               return errorf("malformed accumulator subregister");
            }
            op.subnr = subnr;
         }
         return true;
      }

      if (consume("arf")) {
         unsigned nr = 0;
         if (!consume_uint(nr))
            return errorf("expected ARF number");
         op.nr = nr;
         return true;
      }

      if (consume("sr")) {
         unsigned idx = 0;
         if (!consume_uint(idx))
            return errorf("expected state register index");
         op.nr = GEN_ARF_STATE + idx;
         return consume_optional_zero_subreg("state register");
      }

      if (consume("tm")) {
         unsigned idx = 0;
         if (!consume_uint(idx))
            return errorf("expected timestamp register index");
         op.nr = GEN_ARF_TIMESTAMP + idx;
         return consume_optional_zero_subreg("timestamp register");
      }

      if (consume("cr")) {
         unsigned idx = 0;
         if (!consume_uint(idx))
            return errorf("expected control register index");
         op.nr = GEN_ARF_CONTROL + idx;
         if (consume('.')) {
            unsigned subnr = 0;
            if (!consume_uint(subnr))
               return errorf("malformed control subregister");
            op.subnr = subnr;
         }
         return true;
      }

      if (consume('a')) {
         unsigned idx = 0;
         if (!consume_uint(idx))
            return errorf("expected address register index");
         op.nr = GEN_ARF_ADDRESS + idx;
         if (consume('.')) {
            unsigned subnr = 0;
            if (!consume_uint(subnr))
               return errorf("malformed address subregister");
            op.subnr = subnr;
         }
         return true;
      }

      if (consume('f')) {
         unsigned idx = 0;
         if (!consume_uint(idx))
            return errorf("expected flag register index");
         op.nr = GEN_ARF_FLAG | idx;
         if (consume('.')) {
            unsigned subnr = 0;
            if (!consume_uint(subnr))
               return errorf("expected flag subregister");
            op.subnr = subnr;
         }
         return true;
      }

      if (consume('s')) {
         unsigned idx = 0;
         if (!consume_uint(idx))
            return errorf("expected scalar register index");
         op.nr = GEN_ARF_SCALAR + idx;
         if (consume('.')) {
            unsigned subnr = 0;
            if (!consume_uint(subnr))
               return errorf("malformed scalar subregister");
            op.subnr = subnr;
         }
         return true;
      }

      if (consume('n')) {
         unsigned idx = 0;
         if (!consume_uint(idx))
            return errorf("expected notification count register index");
         op.nr = GEN_ARF_NOTIFICATION_COUNT + idx;
         if (consume('.')) {
            unsigned subnr = 0;
            if (!consume_uint(subnr))
               return errorf("expected notification count subregister");
            op.subnr = subnr;
         }
         return true;
      }

      return errorf("unknown register");
   }

   std::optional<gen_reg_type>
   parse_type_if_present()
   {
      if (!consume(':'))
         return GEN_TYPE_UD;

      const std::string_view name = consume_while(is_alnum);
      bool valid;
      gen_reg_type type = gen_reg_type_from_string(SV_ARGS(name), &valid);
      if (!valid) {
         errorf("unknown type '%.*s'", SV_FMT(name));
         return {};
      }

      return type;
   }

   bool
   parse_writemask(unsigned &writemask)
   {
      writemask = 0;
      while (!line.empty()) {
         const char ch = peek();
         unsigned bit = 0;
         switch (ch) {
         case 'x': bit = 1u << GEN_CHANNEL_X; break;
         case 'y': bit = 1u << GEN_CHANNEL_Y; break;
         case 'z': bit = 1u << GEN_CHANNEL_Z; break;
         case 'w': bit = 1u << GEN_CHANNEL_W; break;
         default:
            return true;
         }
         writemask |= bit;
         line.remove_prefix(1);
      }

      return true;
   }

   bool
   parse_swizzle(unsigned &swizzle)
   {
      swizzle = 0;
      unsigned num = 0;
      while (num < 4) {
         bool match = true;
         switch (peek()) {
         case 'x': swizzle |= 0u << (2 * num); break;
         case 'y': swizzle |= 1u << (2 * num); break;
         case 'z': swizzle |= 2u << (2 * num); break;
         case 'w': swizzle |= 3u << (2 * num); break;
         default:  match = false; break;
         }
         if (!match)
            break;
         num++;
         line.remove_prefix(1);
      }

      if (num != 1 && num != 4)
         return errorf("swizzle must have 1 or 4 components");

      if (num == 1) {
         const unsigned c = swizzle;
         swizzle = c | (c << 2) | (c << 4) | (c << 6);
      }
      return true;
   }

   bool
   parse_src_region(gen_region &region)
   {
      const bool vxh = consume_ident("VxH");
      if (vxh) {
         region.vstride = GEN_VSTRIDE_ONE_DIMENSIONAL;
      } else {
         unsigned vstride = 0;
         if (!consume_uint(vstride))
            return errorf("expected source vertical stride");
         region.vstride = vstride;
      }

      if (!vxh && peek() == '>') {
         region.width = 1;
         region.hstride = 0;
      } else {
         unsigned width = 0, hstride = 0;
         if (!consume(';') || !consume_uint(width) ||
             !consume(',') || !consume_uint(hstride)) {
            return errorf("malformed source region");
         }
         region.width = width;
         region.hstride = hstride;
      }

      if (!consume('>'))
         return errorf("expected '>' after source region");

      return true;
   }

   bool
   parse_src_region_3src(gen_region &region, unsigned src_idx)
   {
      if (peek() == '0' && peek(1) == '>') {
         consume('0');
         region = { 0, 1, 0 };
         if (!consume('>'))
            return errorf("expected '>' after source region");
         return true;
      }

      unsigned a = 0, b = 0;
      if (!consume_uint(a))
         return errorf("expected 3-src region");

      if (src_idx < 2) {
         if (!consume(';') || !consume_uint(b))
            return errorf("malformed 3-src region");

         region.vstride = a;
         region.hstride = b;
         region.width = gen_implied_width_for_3src_a1(a, b);

      } else {
         region.vstride = 0;
         region.width = 1;
         region.hstride = a;
      }

      if (!consume('>'))
         return errorf("expected '>' after source region");

      return true;
   }

   bool
   parse_immediate(gen_operand &op)
   {
      const auto [_, type_str, has_type] = cut(peek_until(is_space), ':');

      bool is_signed = false;
      if (has_type) {
         bool valid;
         const gen_reg_type t =
            gen_reg_type_from_string(SV_ARGS(type_str), &valid);
         is_signed = valid && (t == GEN_TYPE_D || t == GEN_TYPE_W ||
                               t == GEN_TYPE_B);
      }

      op.file = GEN_IMM;

      if (is_signed) {
         int64_t v = 0;
         if (!consume_signed(v))
            return errorf("malformed signed immediate");
         op.imm = (uint64_t)v;
      } else {
         uint64_t v = 0;
         if (!consume_unsigned(v))
            return errorf("malformed immediate");
         op.imm = v;
      }

      auto op_type = parse_type_if_present();
      if (!op_type)
         return false;
      op.type = *op_type;

      return true;
   }

   bool
   parse_send_format()
   {
      if (inst.exec_size == 0)
         inst.exec_size = 1;

      int dst_length = -1;
      int src0_length = -1;
      int src1_length = -1;

      if (!parse_send_reg(inst.dst, &dst_length))
         return false;
      if (!parse_send_reg(inst.src[0], &src0_length))
         return false;

      inst.src[1].file = GEN_ARF;
      inst.src[1].nr = GEN_ARF_NULL;

      if (lsc.valid) {
         if (send_has_symbolic_src1(lsc.op) &&
             !parse_send_reg(inst.src[1], &src1_length))
            return false;
      } else {
         skip_ws();
         /* In the legacy send format src1 is sometimes omitted. Recognize
          * the forms that mean "no src1": explicit "null:0", or the start
          * of the next field (ex_desc as register a0.X or an immediate).
          */
         if (consume("null:0")) {
            /* src1 is null; already pre-set above. */
         } else if (peek() != 'a' && !is_number_prefix(peek())) {
            if (!parse_send_reg(inst.src[1], &src1_length))
               return false;
         }
      }

      if (lsc.valid) {
         unsigned mlen, ex_mlen, rlen;
         get_lsc_lengths(mlen, ex_mlen, rlen);
         if (!validate_lsc_lengths(dst_length, src0_length, src1_length,
                                   mlen, ex_mlen, rlen))
            return false;

         inst.send.desc_is_reg = false;
         if (!inst.send.ex_desc_is_reg && ex_mlen > 0)
            inst.send.ex_desc_imm |= gen_message_ex_desc(devinfo, ex_mlen);
         inst.send.ex_desc_imm_extra = 0;
         inst.send.src1_len = ex_mlen;

         const uint32_t payload_desc =
            lsc.op == LSC_OP_FENCE ?
            lsc_fence_msg_desc(devinfo, lsc.fence_scope, lsc.flush_type,
                               lsc.route_to_lsc) :
            lsc_msg_desc(devinfo, lsc.op, lsc.addr_type, lsc.addr_size,
                         lsc.data_size,
                         lsc_opcode_has_cmask(lsc.op) ? (unsigned)lsc.cmask : lsc.num_values,
                         lsc.transpose, lsc.cache_ctrl);

         const gen_message_desc msg = {
            .msg_length = mlen,
            .response_length = rlen,
         };
         inst.send.desc_imm = gen_message_desc_encode(devinfo, &msg) |
                              payload_desc;
      } else {
         if (src1_length >= 0)
            inst.send.src1_len = src1_length;

         if (!parse_send_ex_desc())
            return false;

         if (!parse_send_desc())
            return false;
      }

      return true;
   }

   bool
   parse_send_reg(gen_operand &op, int *length)
   {
      if (!expect_operand())
         return false;

      const std::string_view start = line;
      if (!parse_register_base(op))
         return false;

      if (op.file == GEN_ARF && !op.indirect &&
          (op.nr & 0xf0) == GEN_ARF_SCALAR && start.front() == 's') {
         return errorf("scalar send payload register must use r[sN.subnr]");
      }

      op.type   = GEN_TYPE_UD;
      op.region = { 0, 1, 0 };

      if (length)
         *length = -1;

      if (consume(':')) {
         unsigned len = 0;
         if (!consume_uint(len))
            return errorf("expected send payload length");
         if (length)
            *length = len;
      }
      return true;
   }

   bool
   parse_send_ex_desc()
   {
      if (!expect_operand())
         return false;

      unsigned imm_extra = 0;
      bool has_imm_extra = false;
      const std::string_view saved = line;
      uint64_t value = 0;
      if (consume_unsigned(value) && consume(':')) {
         imm_extra = value;
         has_imm_extra = true;
      } else {
         line = saved;
      }

      if (consume("a0.")) {
         unsigned subreg = 0;
         if (!consume_uint(subreg))
            return errorf("expected send extended descriptor subregister");

         inst.send.ex_desc_is_reg = true;
         inst.send.ex_desc_subnr = subreg * 2;
         if (has_imm_extra)
            inst.send.ex_desc_imm_extra = imm_extra;
         return true;
      }

      if (has_imm_extra) {
         return errorf("immediate offset prefix requires register extended descriptor");
      }

      uint64_t desc_imm = 0;
      if (!consume_unsigned(desc_imm)) {
         return errorf("malformed send extended descriptor");
      }
      inst.send.ex_desc_imm = desc_imm;
      return true;
   }

   bool
   parse_send_desc()
   {
      if (!expect_operand())
         return false;

      if (consume("a0.0")) {
         inst.send.desc_is_reg = true;
         return true;
      }

      uint64_t value = 0;
      if (!consume_unsigned(value)) {
         return errorf("malformed send descriptor");
      }
      inst.send.desc_imm = value;

      /* Accept legacy raw SEND syntax that leaves the EOT bit embedded in the
       * descriptor immediate.
       */
      inst.send.eot |= inst.send.desc_imm >> 31;
      inst.send.desc_imm &= ~(1u << 31);
      return true;
   }

   bool
   parse_branch_target(unsigned src_index,
                       std::vector<label_use> &inst_label_uses)
   {
      if (!expect_operand())
         return false;

      if (is_number_prefix(peek())) {
         uint64_t v = 0;
         if (!consume_unsigned(v))
            return errorf("malformed branch target");

         inst.src[src_index].file = GEN_IMM;
         inst.src[src_index].type = GEN_TYPE_D;
         inst.src[src_index].imm = (int32_t)(uint32_t)v;

      } else if (is_ident_start(peek())) {
         const std::string_view tok = consume_ident_token();

         inst.src[src_index].file = GEN_IMM;
         inst.src[src_index].type = GEN_TYPE_D;
         inst.src[src_index].imm = 0;
         inst_label_uses.push_back({ (int)insts.size(), src_index, line_no, tok });

      } else {
         return errorf("unexpected branch target");
      }

      return true;
   }

   bool
   parse_options()
   {
      skip_ws();
      if (!consume('{'))
         return true;

      while (true) {
         skip_ws();
         if (!parse_option())
            return false;

         skip_ws();
         if (consume('}')) {
            skip_ws();
            return true;
         }
         if (!consume(','))
            return errorf("expected ',' or '}' in annotation");
      }
   }

   bool
   parse_option()
   {
      /* SBID: $<num>[.dst|.src]. */
      if (consume('$')) {
         unsigned sbid = 0;
         if (!consume_uint(sbid))
            return errorf("malformed SBID annotation");
         inst.swsb.sbid = sbid;

         if (consume('.')) {
            const auto mode = consume_ident_token();
            if (mode == "dst")
               inst.swsb.mode = GEN_SBID_DST;
            else if (mode == "src")
               inst.swsb.mode = GEN_SBID_SRC;
            else
               return errorf("unknown SBID annotation '$%u.%.*s'", sbid, SV_FMT(mode));
         } else {
            inst.swsb.mode = GEN_SBID_SET;
         }
         return true;
      }

      /* Bare regdist: @<num>. */
      if (consume('@')) {
         unsigned regdist = 0;
         if (!consume_uint(regdist))
            return errorf("malformed SWSB pipe annotation");
         inst.swsb.pipe = GEN_PIPE_NONE;
         inst.swsb.regdist = regdist;
         return true;
      }

      /* Pipe + regdist: <letter>@<num>. */
      if (peek(1) == '@') {
         const std::string_view pipe_name = line.substr(0, 1);
         bool valid;
         const gen_pipe pipe = gen_pipe_from_string(SV_ARGS(pipe_name), &valid);
         if (!valid)
            return errorf("unknown SWSB pipe annotation '%.*s'", SV_FMT(pipe_name));
         line.remove_prefix(1);
         consume('@');

         unsigned regdist = 0;
         if (!consume_uint(regdist))
            return errorf("malformed SWSB pipe annotation");
         inst.swsb.pipe = pipe;
         inst.swsb.regdist = regdist;
         return true;
      }

      /* Bare ident option. */
      const auto name = consume_ident_token();

      if (name == "AccWrEn") {
         inst.acc_wr_control = true;
      } else if (name == "Atomic") {
         if (devinfo->ver >= 12)
            inst.atomic_control = true;
         else
            inst.thread_control = GEN_THREAD_ATOMIC;
      } else if (name == "Breakpoint") {
         inst.debug_control = true;
      } else if (name == "Align16") {
         inst.align16 = true;
      } else if (name == "EOT") {
         inst.send.eot = true;
      } else if (name == "NoDDChk") {
         inst.no_dd_check = true;
      } else if (name == "NoDDClr") {
         inst.no_dd_clear = true;
      } else if (name == "Switch") {
         inst.thread_control = GEN_THREAD_SWITCH;
      } else if (name == "Serialize") {
         inst.fusion_control = true;
      } else if (name == "ExBSO") {
         inst.send.ex_bso = true;
      } else {
         return errorf("unknown annotation note '%.*s'", SV_FMT(name));
      }
      return true;
   }

   void
   resolve_labels()
   {
      for (const label_use &use : label_uses) {
         auto it = labels.find(use.label);
         if (it == labels.end()) {
            line_no = use.line;
            errorf("undefined label '%.*s'", SV_FMT(use.label));
            continue;
         }

         const int rel = (it->second - use.inst_idx) * 16;
         gen_inst &inst = insts[use.inst_idx];
         inst.src[use.src_index].imm = rel;
      }
   }

};

} /* namespace */

extern "C" bool
gen_parse(gen_parse_params *params)
{
   gen_parser p(params);
   return p.parse();
}
