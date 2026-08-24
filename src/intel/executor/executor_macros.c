/*
 * Copyright © 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "util/ralloc.h"

#include "executor.h"

static bool
is_comment(slice s)
{
   return s.len >= 2 && s.data[0] == '/' && s.data[1] == '/';
}

slice
strip_spaces(slice s)
{
   while (!slice_is_empty(s) && isspace((unsigned char)s.data[0]))
      s = slice_substr_from(s, 1);

   while (!slice_is_empty(s) && isspace((unsigned char)s.data[s.len - 1]))
      s = slice_substr_to(s, s.len - 1);

   return s;
}

slice
trim_comments(slice s)
{
   for (int i = 0; i < s.len - 1; i++) {
      if (s.data[i] == '/' && s.data[i + 1] == '/')
         return slice_substr_to(s, i);
   }
   return s;
}

bool
parse_int64(slice s, int64_t *value)
{
   char str[64];

   if (slice_is_empty(s) || s.len >= sizeof(str))
      return false;

   memcpy(str, s.data, s.len);
   str[s.len] = '\0';

   errno = 0;
   char *end = NULL;
   long long parsed = strtoll(str, &end, 0);
   if (errno == ERANGE || !str[0] || *end)
      return false;

   *value = parsed;
   return true;
}

static unsigned
reserved_grf_start(const executor_run *run)
{
   return run->hw_regs - 4;
}

static unsigned
reserved_grf_end(const executor_run *run)
{
   return run->hw_regs;
}

static unsigned
slots_per_grf(const executor_run *run)
{
   executor_context *ec = run->ec;
   return ec->devinfo->grf_size / sizeof(uint32_t);
}

static unsigned
grfs_per_macro_operand(const executor_run *run)
{
   return run->simd / slots_per_grf(run);
}

static unsigned
parse_macro_grf(const executor_run *run, slice reg)
{
   if (reg.len < 2 || reg.data[0] != 'r' ||
       !isdigit((unsigned char)reg.data[1]))
      failf("operand %.*s must be a non-reserved valid register",
            SLICE_FMT(reg));

   unsigned nr = 0;
   for (int i = 1; i < reg.len; i++) {
      if (!isdigit((unsigned char)reg.data[i]))
         failf("operand %.*s must be a non-reserved valid register",
               SLICE_FMT(reg));

      const unsigned digit = reg.data[i] - '0';
      if (nr > (run->hw_regs - 1 - digit) / 10)
         failf("operand %.*s must be a non-reserved valid register",
               SLICE_FMT(reg));

      nr = nr * 10 + digit;
   }

   const unsigned operand_grfs = grfs_per_macro_operand(run);
   if (nr < 2 || operand_grfs == 0 ||
       nr + operand_grfs > run->hw_regs)
      failf("operand %.*s must be a non-reserved valid register",
            SLICE_FMT(reg));

   if (MAX2(nr, reserved_grf_start(run)) <
       MIN2(nr + operand_grfs, reserved_grf_end(run)))
      failf("operand %.*s must be a non-reserved valid register",
            SLICE_FMT(reg));

   return nr;
}

static bool
executor_macro_grf_ranges_overlap(unsigned a, unsigned b, unsigned count)
{
   return MAX2(a, b) < MIN2(a + count, b + count);
}

static void
check_macro_grf_no_partial_overlap(const executor_run *run,
                                   const char *macro,
                                   slice dst, unsigned dst_nr,
                                   slice src, unsigned src_nr)
{
   const unsigned operand_grfs = grfs_per_macro_operand(run);

   if (operand_grfs <= 1 || dst_nr == src_nr)
      return;

   if (executor_macro_grf_ranges_overlap(dst_nr, src_nr, operand_grfs))
      failf("%s operands %.*s and %.*s partially overlap for simd%u",
            macro, SLICE_FMT(dst), SLICE_FMT(src), run->simd);
}

typedef struct {
   slice *args;
   int    count;
} parse_args_result;

static parse_args_result
parse_args(void *mem_ctx, slice args)
{
   parse_args_result r = {0};
   args = trim_comments(args);

   while (true) {
      args = strip_spaces(args);

      if (slice_is_empty(args))
         break;

      slice_cut_result cut = slice_cut_any(args, " \t");
      r.args = reralloc_array_size(mem_ctx, r.args, sizeof(slice), r.count + 1);
      r.args[r.count++] = cut.before;
      args = cut.after;
   }

   return r;
}

static const char *
executor_macro_swsb(const executor_run *run)
{
   executor_context *ec = run->ec;
   return ec->devinfo->verx10 < 120 ? "" :
          ec->devinfo->verx10 < 125 ? "@1" : "A@1";
}

static const char *
hw_thread_id_reg(const executor_run *run)
{
   executor_context *ec = run->ec;
   return ec->devinfo->verx10 < 125 ? "r1<0>:ud" : "r0.2<0>:ud";
}

static void
executor_macro_mov(executor_run *run, char **src, slice args)
{
   parse_args_result r = parse_args(run->tmp_ctx, args);

   if (r.count != 2)
      failf("@mov needs 2 arguments, found %d\n", r.count);

   unsigned reg = parse_macro_grf(run, r.args[0]);
   char *value = slice_to_cstr(run->tmp_ctx, r.args[1]);
   const unsigned width = run->simd;
   const char *swsb = executor_macro_swsb(run);

   if (strchr(value, '.')) {
      union {
         float f;
         uint32_t u;
      } val;

      val.f = strtof(value, NULL);
      ralloc_asprintf_append(src,
         "mov (%u) r%u:f 0x%08x:f {%s}\n",
         width, reg, val.u, swsb);
   } else {
      for (char *x = value; *x; x++)
         *x = tolower(*x);

      ralloc_asprintf_append(src,
         "mov (%u) r%u %s {%s}\n",
         width, reg, value, swsb);
   }
}

static void
executor_emit_syncnop(executor_context *ec, char **src)
{
   switch (ec->devinfo->verx10) {
   case 90:
   case 110: {
      /* Not needed. */
      break;
   }

   case 120: {
      ralloc_strcat(src,
         "(W) sync.nop (8) null {@1,$1.dst}\n");
      break;
   }

   case 125:
   case 200:
   case 300:
   case 350: {
      ralloc_strcat(src,
         "(W) sync.nop (8) null {A@1,$1.dst}\n");
      break;
   }

   default:
      UNREACHABLE("invalid gfx version");
   }
}

static void
executor_macro_syncnop(executor_run *run, char **src, slice line)
{
   executor_emit_syncnop(run->ec, src);
}

static void
executor_macro_eot(executor_run *run, char **src, slice line)
{
   executor_context *ec = run->ec;
   const unsigned rsvd_base = reserved_grf_start(run);

   switch (ec->devinfo->verx10) {
   case 90:
   case 110: {
      ralloc_asprintf_append(src,
         "(W) mov (8) r%u r0\n"
         "(W) sends.ts (1) null r%u:1 null:0 0x00000000 0x82000010 {EOT}\n",
         rsvd_base, rsvd_base);
      break;
   }

   case 120: {
      ralloc_asprintf_append(src,
         "(W) mov (8) r%u r0\n"
         "(W) send.ts (1) null r%u:1 null:0 0x00000000 0x02000000 {@1,EOT}\n",
         rsvd_base, rsvd_base);
      break;
   }

   case 125:
   case 200:
   case 300:
   case 350: {
      const unsigned slots = ec->devinfo->verx10 >= 200 ? 16 : 8;
      ralloc_asprintf_append(src,
         "(W) mov (%u) r%u r0\n"
         "(W) send.gtwy (1) null r%u:1 null:0 0x00000000 0x02000000 {A@1,EOT}\n",
         slots, rsvd_base, rsvd_base);
      break;
   }

   default:
      UNREACHABLE("invalid gfx version");
   }
}

static void
executor_macro_barrier(executor_run *run,
                       char **src, slice args)
{
   executor_context *ec = run->ec;
   parse_args_result r = parse_args(run->tmp_ctx, args);

   if (r.count != 0)
      failf("@barrier takes no arguments, found %d\n", r.count);

   if (run->hw_threads <= 1)
      return;

   const unsigned rsvd_base = reserved_grf_start(run);

   switch (ec->devinfo->verx10) {
   case 90:
   case 110: {
      const uint32_t barrier_id_mask =
         ec->devinfo->ver == 9 ? 0x8f000000u : 0x7f000000u;

      if (run->slm_size != 0) {
         ralloc_asprintf_append(src,
            "(W) send.hdc0 (1) r%u:1 r0:1 0x00000000 0x0211e0fe\n"
            "(W) mov (1) r%u r%u<0;1,0>:ud\n",
            rsvd_base, rsvd_base, rsvd_base);
      }

      ralloc_asprintf_append(src,
         "(W) mov (8) r%u 0x0\n"
         "(W) and (1) r%u.2 r0.2<0>:ud 0x%08x:ud\n"
         "(W) sends.gtwy (1) null r%u:1 null:0 0x00000000 0x02000004\n"
         "(W) wait (1) n0\n",
         rsvd_base, rsvd_base, barrier_id_mask, rsvd_base);
      break;
   }

   case 120:
      if (run->slm_size != 0) {
         ralloc_asprintf_append(src,
            "(W) send.hdc0 (1) r%u:1 r0:1 0x00000000 0x0211e0fe {@1,$1}\n",
            rsvd_base);
         executor_emit_syncnop(ec, src);
      }

      ralloc_asprintf_append(src,
         "(W) mov (8) r%u 0x0 {@1}\n"
         "(W) and (1) r%u.2 r0.2<0>:ud 0x7f000000:ud {@1}\n"
         "(W) send.gtwy (1) null r%u:1 null:0 0x00000000 0x02000004 {@1,$1}\n"
         "(W) sync.bar (1) null\n",
         rsvd_base, rsvd_base, rsvd_base);
      break;

   case 125:
      if (run->slm_size != 0) {
         ralloc_asprintf_append(src,
            "(W) fence.slm.threadgroup.none (1) r%u:1 r0:1 {A@1,$1}\n",
            rsvd_base);
         executor_emit_syncnop(ec, src);
      }

      ralloc_asprintf_append(src,
         "(W) mov (8) r%u 0x0 {A@1}\n"
         "(W) mov (2) r%u.10:ub r0.11<0;1,0>:ub {A@1}\n"
         "(W) send.gtwy (1) null r%u:1 null:0 0x00000000 0x02000004 {A@1,$1}\n"
         "(W) sync.bar (1) null\n",
         rsvd_base, rsvd_base, rsvd_base);
      break;

   case 200:
   case 300:
   case 350:
      if (run->slm_size != 0) {
         ralloc_asprintf_append(src,
            "(W) fence.slm.threadgroup.none (1) r%u:1 r0:1 {A@1,$1}\n",
            rsvd_base);
         executor_emit_syncnop(ec, src);
      }

      ralloc_asprintf_append(src,
         "(W) mov (16) r%u 0x0 {A@1}\n"
         "(W) mov (2) r%u.10:ub r0.11<0;1,0>:ub {A@1}\n"
         "(W) or (1) r%u.2 r%u.2<0> 0x100 {A@1}\n"
         "(W) send.gtwy (1) null r%u:1 null:0 0x00000000 0x02000004 {A@1,$1}\n"
         "(W) sync.bar (1) null\n",
         rsvd_base, rsvd_base, rsvd_base, rsvd_base, rsvd_base);
      break;

   default:
      UNREACHABLE("invalid gfx version");
   }
}

static void
emit_local_id(executor_run *run, char **src, unsigned nr)
{
   const unsigned slots = slots_per_grf(run);
   const unsigned operand_grfs = grfs_per_macro_operand(run);
   const char *swsb = executor_macro_swsb(run);
   const unsigned rsvd_base = reserved_grf_start(run);

   ralloc_asprintf_append(src, "(W) mov (8) r%u:uw 0x76543210:v {%s}\n",
                          rsvd_base, swsb);
   if (slots > 8)
      ralloc_asprintf_append(src, "(W) add (8) r%u.8:uw r%u:uw 8:uw {%s}\n",
                             rsvd_base, rsvd_base, swsb);

   for (unsigned i = 0; i < operand_grfs; i++) {
      ralloc_asprintf_append(src, "(W) mov (%u) r%u r%u:uw {%s}\n",
                             slots, nr + i, rsvd_base, swsb);
      if (i > 0)
         ralloc_asprintf_append(src, "(W) add (%u) r%u r%u 0x%x:ud {%s}\n",
                                slots, nr + i, nr + i, i * slots, swsb);
   }

   if (run->hw_threads <= 1)
      return;

   const unsigned shift = run->simd == 32 ? 5 : run->simd == 16 ? 4 : 3;
   ralloc_asprintf_append(src,
      "(W) and (8) r%u %s 0xff:ud {%s}\n"
      "(W) shl (8) r%u r%u %u:ud {%s}\n",
      rsvd_base, hw_thread_id_reg(run), swsb, rsvd_base, rsvd_base, shift, swsb);

   for (unsigned i = 0; i < operand_grfs; i++)
      ralloc_asprintf_append(src, "(W) add (%u) r%u r%u r%u.0<0>:ud {%s}\n",
                             slots, nr + i, nr + i, rsvd_base, swsb);
}

static void
executor_macro_id(executor_run *run,
                  char **src, slice args)
{
   parse_args_result r = parse_args(run->tmp_ctx, args);

   if (r.count != 1)
      failf("@id needs 1 argument, found %d", r.count);

   unsigned nr = parse_macro_grf(run, r.args[0]);
   emit_local_id(run, src, nr);
}

static void
executor_macro_tg(executor_run *run,
                  char **src, slice args)
{
   parse_args_result r = parse_args(run->tmp_ctx, args);

   if (r.count != 1)
      failf("@tg needs 1 argument, found %d", r.count);

   const unsigned slots = slots_per_grf(run);
   const unsigned operand_grfs = grfs_per_macro_operand(run);
   const char *swsb = executor_macro_swsb(run);
   unsigned nr = parse_macro_grf(run, r.args[0]);

   for (unsigned i = 0; i < operand_grfs; i++)
      ralloc_asprintf_append(src, "(W) mov (%u) r%u r0.1<0>:ud {%s}\n",
                             slots, nr + i, swsb);
}

static void
executor_macro_globalid(executor_run *run,
                        char **src, slice args)
{
   parse_args_result r = parse_args(run->tmp_ctx, args);

   if (r.count != 1)
      failf("@globalid needs 1 argument, found %d", r.count);

   unsigned nr = parse_macro_grf(run, r.args[0]);
   emit_local_id(run, src, nr);
   if (run->thread_groups <= 1)
      return;

   const unsigned slots = slots_per_grf(run);
   const unsigned operand_grfs = grfs_per_macro_operand(run);
   const char *swsb = executor_macro_swsb(run);
   const uint32_t group_size = run->hw_threads * run->simd;
   const unsigned rsvd_base = reserved_grf_start(run);

   ralloc_asprintf_append(src, "(W) mul (8) r%u r0.1<0>:ud 0x%x:ud {%s}\n",
                          rsvd_base, group_size, swsb);

   for (unsigned i = 0; i < operand_grfs; i++)
      ralloc_asprintf_append(src, "(W) add (%u) r%u r%u r%u.0<0>:ud {%s}\n",
                             slots, nr + i, nr + i, rsvd_base, swsb);
}

static void
executor_macro_emit_mov_u32(const executor_run *run,
                            char **src, unsigned nr, uint32_t value)
{
   const unsigned exec_size = slots_per_grf(run);
   const unsigned operand_grfs = grfs_per_macro_operand(run);
   const char *swsb = executor_macro_swsb(run);

   for (unsigned i = 0; i < operand_grfs; i++)
      ralloc_asprintf_append(src, "mov (%u) r%u 0x%08x {%s}\n",
                             exec_size, nr + i, value, swsb);
}

static void
executor_macro_store(executor_run *run, char **src, slice args)
{
   executor_context *ec = run->ec;
   parse_args_result r = parse_args(run->tmp_ctx, args);

   if (r.count != 2)
      failf("@store needs 2 arguments, found %d\n", r.count);

   unsigned addr_reg = parse_macro_grf(run, r.args[0]);
   unsigned data_reg = parse_macro_grf(run, r.args[1]);
   const unsigned exec_size = slots_per_grf(run);
   const unsigned operand_grfs = grfs_per_macro_operand(run);

   for (unsigned i = 0; i < operand_grfs; i++) {
      switch (ec->devinfo->verx10) {
      case 90:
      case 110:
      case 120: {
         const char *send_op = ec->devinfo->verx10 < 120 ? "sends.hdc1" : "send.hdc1";
         ralloc_asprintf_append(src,
            "%s (%u) null r%u:1 r%u:1 0x00000040 0x02026efd {@1,$1}\n",
            send_op, exec_size, addr_reg + i, data_reg + i);
         break;
      }

      case 125:
      case 200:
      case 300:
      case 350: {
         ralloc_asprintf_append(src,
            "store.ugm.d32.a32 (%u) r%u:1 r%u:1 {A@1,$1}\n",
            exec_size, addr_reg + i, data_reg + i);
         break;
      }

      default:
         UNREACHABLE("invalid gfx version");
      }
   }

   executor_emit_syncnop(ec, src);
}

static void
executor_macro_load(executor_run *run, char **src, slice args)
{
   executor_context *ec = run->ec;
   parse_args_result r = parse_args(run->tmp_ctx, args);

   if (r.count != 2)
      failf("@load needs 2 arguments, found %d\n", r.count);

   /* Order follows underlying SEND, destination first. */
   unsigned data_reg = parse_macro_grf(run, r.args[0]);
   unsigned addr_reg = parse_macro_grf(run, r.args[1]);
   check_macro_grf_no_partial_overlap(run, "@load",
                                      r.args[0], data_reg,
                                      r.args[1], addr_reg);

   const unsigned exec_size = slots_per_grf(run);
   const unsigned operand_grfs = grfs_per_macro_operand(run);

   for (unsigned i = 0; i < operand_grfs; i++) {
      switch (ec->devinfo->verx10) {
      case 90:
      case 110:
      case 120: {
         const char *send_op = ec->devinfo->verx10 < 120 ? "sends.hdc1" : "send.hdc1";
         ralloc_asprintf_append(src,
            "%s (%u) r%u r%u:1 null:0 0x00000000 0x02106efd {@1,$1}\n",
            send_op, exec_size, data_reg + i, addr_reg + i);
         break;
      }

      case 125:
      case 200:
      case 300:
      case 350: {
         ralloc_asprintf_append(src,
            "load.ugm.d32.a32 (%u) r%u:1 r%u:1 {A@1,$1}\n",
            exec_size, data_reg + i, addr_reg + i);
         break;
      }

      default:
         UNREACHABLE("invalid gfx version");
      }
   }

   executor_emit_syncnop(ec, src);
}

static void
executor_macro_addr(executor_run *run, char **src, slice args)
{
   executor_context *ec = run->ec;
   parse_args_result r = parse_args(run->tmp_ctx, args);

   if (r.count != 2 && r.count != 3)
      failf("@addr needs 2 or 3 arguments, found %d\n", r.count);

   char *mem_key = slice_to_cstr(run->tmp_ctx, r.args[1]);

   const executor_mem_region *region = executor_find_mem_region(ec, mem_key);
   if (!region)
      failf("unknown memory object '%s'", mem_key);

   uint64_t base_addr = ec->bo.data.addr + region->offset;
   if (base_addr > UINT32_MAX)
      failf("@addr result 0x%llx exceeds 32-bit limit for a32 messages",
            (unsigned long long)base_addr);

   const unsigned exec_size = slots_per_grf(run);
   const unsigned operand_grfs = grfs_per_macro_operand(run);

   unsigned dst_nr = parse_macro_grf(run, r.args[0]);
   if (r.count == 2) {
      executor_macro_emit_mov_u32(run, src, dst_nr,
                                  (uint32_t)base_addr);
      return;
   }

   slice offset_slice = r.args[2];
   int64_t offset_dw = 0;
   if (parse_int64(offset_slice, &offset_dw) &&
       offset_dw >= 0 && offset_dw <= UINT32_MAX) {
      uint64_t byte_offset = (uint64_t)offset_dw * 4;
      if (byte_offset >= region->size)
         failf("@addr offset out of bounds");

      uint64_t addr = base_addr + byte_offset;
      if (addr > UINT32_MAX)
         failf("@addr result 0x%llx exceeds 32-bit limit for a32 messages",
               (unsigned long long)addr);

      executor_macro_emit_mov_u32(run, src, dst_nr,
                                  (uint32_t)addr);
      return;
   }

   const char *swsb = executor_macro_swsb(run);

   unsigned offset_nr = parse_macro_grf(run, offset_slice);
   check_macro_grf_no_partial_overlap(run, "@addr",
                                      r.args[0], dst_nr,
                                      offset_slice, offset_nr);

   /* Broadcast the uniform buffer base address into r127.0 once. */
   const unsigned rsvd_base = reserved_grf_start(run);
   ralloc_asprintf_append(src, "mov (8) r%u 0x%08x {%s}\n",
                          rsvd_base, (uint32_t)base_addr, swsb);

   /* addr[lane] = base + index[lane] * 4, one GRF at a time. */
   for (unsigned i = 0; i < operand_grfs; i++)
      ralloc_asprintf_append(src,
         "mul (%u) r%u r%u 0x4:uw {%s}\n"
         "add (%u) r%u r%u r%u.0<0>:ud {%s}\n",
         exec_size, dst_nr + i, offset_nr + i, swsb,
         exec_size, dst_nr + i, dst_nr + i, rsvd_base, swsb);
}

static slice
find_macro_symbol(slice line)
{
   line = strip_spaces(line);
   return !slice_is_empty(line) && line.data[0] == '@' ? line : (slice){};
}

static bool
match_macro_name(const char *name, slice line)
{
   slice name_slice = slice_from_cstr(name);
   if (!slice_starts_with(line, name_slice))
      return false;

   line = slice_substr_from(line, name_slice.len);
   return slice_is_empty(line) || isspace(*line.data) || is_comment(line);
}

const char *
executor_apply_macros(executor_run *run)
{
   slice remaining = run->original_src;

   /* Create a ralloc'ed empty string so can call append to it later. */
   char *src = ralloc_strdup(run->tmp_ctx, "");

   /* TODO: Create a @send macro for common combinations of MsgDesc. */
   static const struct {
      const char *name;
      void (*func)(executor_run *run, char **output, slice line);
   } macros[] = {
      { "@eot",      executor_macro_eot },
      { "@mov",      executor_macro_mov },
      { "@store",    executor_macro_store },
      { "@load",     executor_macro_load },
      { "@addr",     executor_macro_addr },
      { "@id",       executor_macro_id },
      { "@tg",       executor_macro_tg },
      { "@globalid", executor_macro_globalid },
      { "@barrier",  executor_macro_barrier },
      { "@param",    NULL },
      { "@syncnop",  executor_macro_syncnop },
   };

   while (!slice_is_empty(remaining)) {
      slice_cut_result cut = slice_cut_any(remaining, "\n\r");
      slice line = cut.before;
      remaining = cut.after;
      slice macro = find_macro_symbol(line);
      if (slice_is_empty(macro)) {
         ralloc_asprintf_append(&src, "%.*s\n", SLICE_FMT(line));
      } else {
         bool found = false;
         for (int i = 0; i < ARRAY_SIZE(macros); i++) {
            if (match_macro_name(macros[i].name, macro)) {
               slice args = slice_strip_prefix(macro, slice_from_cstr(macros[i].name));
               args = strip_spaces(args);
               if (macros[i].func)
                  macros[i].func(run, &src, args);
               found = true;
               break;
            }
         }
         if (!found)
            failf("unsupported macro line: %.*s", SLICE_FMT(macro));
      }
   }

   return src;
}
