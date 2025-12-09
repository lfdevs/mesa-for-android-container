/*
 * Copyright © 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <ctype.h>

#include "util/ralloc.h"
#include "intel/compiler/brw/brw_asm.h"

#include "executor.h"

static bool
startswith(const char *prefix, const char *s)
{
   return !strncmp(prefix, s, strlen(prefix));
}

static char *
skip_prefix(char *prefix, char *start)
{
   assert(startswith(prefix, start));
   char *c = start += strlen(prefix);
   return c;
}

static bool
is_comment(const char *c)
{
   assert(c);
   return c[0] && c[0] == '/' && c[1] == '/';
}

typedef struct {
   char **args;
   int    count;
} parse_args_result;

static parse_args_result
parse_args(void *mem_ctx, char *c)
{
   parse_args_result r = {0};

   while (*c) {
      /* Skip spaces. */
      while (*c && isspace(*c))
         c++;

      if (!*c || is_comment(c))
         break;

      char *start = c;
      while (*c && !isspace(*c) && !is_comment(c))
         c++;
      r.args = reralloc_array_size(mem_ctx, r.args, sizeof(char *), r.count + 1);
      r.args[r.count++] = ralloc_strndup(mem_ctx, start, c - start);
   }

   return r;
}

static void
executor_macro_mov(executor_context *ec, char **src, char *line)
{
   char *c = skip_prefix("@mov", line);
   parse_args_result r = parse_args(ec->mem_ctx, c);

   if (r.count != 2)
      failf("@mov needs 2 arguments, found %d\n", r.count);

   const char *reg = r.args[0];
   char *value     = r.args[1];

   if (strchr(value, '.')) {
      union {
         float f;
         uint32_t u;
      } val;

      val.f = strtof(value, NULL);

      switch (ec->devinfo->verx10) {
      case 90:
      case 110:
      case 120:
      case 125: {
         ralloc_asprintf_append(src, "mov(8) %s<1>F 0x%08xF /* %f */ { align1 1Q };\n", reg, val.u, val.f);
         break;
      }
      case 200:
      case 300: {
         ralloc_asprintf_append(src, "mov(16) %s<1>F 0x%08xF /* %f */ { align1 1H };\n", reg, val.u, val.f);
         break;
      }
      default:
         UNREACHABLE("invalid gfx version");
      }

   } else {
      for (char *c = value; *c; c++)
         *c = tolower(*c);
      switch (ec->devinfo->verx10) {
      case 90:
      case 110:
      case 120:
      case 125: {
         ralloc_asprintf_append(src, "mov(8) %s<1>UD %sUD { align1 1Q };\n", reg, value);
         break;
      }

      case 200:
      case 300: {
         ralloc_asprintf_append(src, "mov(16) %s<1>UD %sUD { align1 1H };\n", reg, value);
         break;
      }

      default:
         UNREACHABLE("invalid gfx version");
      }
   }
}

static void
executor_macro_syncnop(executor_context *ec, char **src, char *line)
{
   switch (ec->devinfo->verx10) {
   case 90:
   case 110: {
      /* Not needed. */
      break;
   }

   case 120: {
      ralloc_strcat(src, "sync nop(8)  null<0,1,0>UD  { align1 WE_all 1H @1 $1.dst };\n");
      break;
   }

   case 125:
   case 200:
   case 300: {
      ralloc_strcat(src, "sync nop(8)  null<0,1,0>UD  { align1 WE_all 1H A@1 $1.dst };\n");
      break;
   }

   default:
      UNREACHABLE("invalid gfx version");
   }
}

static void
executor_macro_eot(executor_context *ec, char **src, char *line)
{
   switch (ec->devinfo->verx10) {
   case 90:
   case 110: {
      ralloc_strcat(src,
         "mov(8)          g127<1>UD  g0<8;8,1>UD    { align1 WE_all 1Q };\n"
         "send(8)         null<1>UW  g127<0,1,0>UD  0x82000010\n"
         "    ts/btd MsgDesc: mlen 1 rlen 0 { align1 WE_all 1Q EOT };\n");
      break;
   }
   case 120: {
      ralloc_strcat(src,
         "mov(8)          g127<1>UD  g0<8;8,1>UD  { align1 WE_all 1Q };\n"
         "send(8)         nullUD     g127UD       nullUD  0x02000000  0x00000000\n"
         "    ts/btd MsgDesc:  mlen 1 ex_mlen 0 rlen 0 { align1 WE_all 1Q @1 EOT };\n");
      break;
   }

   case 125: {
      ralloc_strcat(src,
         "mov(8)         g127<1>UD  g0<8;8,1>UD  { align1 WE_all 1Q };\n"
         "send(8)        nullUD     g127UD       nullUD  0x02000000  0x00000000\n"
         "    gateway MsgDesc: (open)  mlen 1 ex_mlen 0 rlen 0 { align1 WE_all 1Q A@1 EOT };\n");
         break;
   }

   case 200:
   case 300: {
      ralloc_strcat(src,
         "mov(16)         g127<1>UD  g0<1,1,0>UD  { align1 WE_all 1H };\n"
         "send(16)        nullUD     g127UD       nullUD  0x02000000  0x00000000\n"
         "    gateway MsgDesc: (open)  mlen 1 ex_mlen 0 rlen 0 { align1 WE_all 1H I@1 EOT };\n");
         break;
   }
   default:
      UNREACHABLE("invalid gfx version");
   }
}

static void
executor_macro_id(executor_context *ec, char **src, char *line)
{
   char *c = skip_prefix("@id", line);
   parse_args_result r = parse_args(ec->mem_ctx, c);

   if (r.count != 1)
      failf("@id needs 1 argument, found %d\n", r.count);

   const char *reg = r.args[0];

   switch (ec->devinfo->verx10) {
   case 90:
   case 110:
   case 120:
   case 125: {
      ralloc_asprintf_append(src,
         "mov(8)  g127<1>UW  0x76543210V    { align1 WE_all 1Q };\n"
         "mov(8)  %s<1>UD    g127<8,8,1>UW  { align1 WE_all 1Q @1 };\n", reg);
      break;
   }

   case 200:
   case 300: {
      ralloc_asprintf_append(src,
         "mov(8)  g127<1>UW    0x76543210V         { align1 WE_all 1Q };\n"
         "add(8)  g127.8<1>UW  g127<1,1,0>UW  8UW  { align1 WE_all 1Q @1 };\n"
         "mov(16) %s<1>UD      g127<8,8,1>UW       { align1 WE_all 1Q @1 };\n", reg);
      break;
   }

   default:
      UNREACHABLE("invalid gfx version");
   }
}

static void
executor_macro_write(executor_context *ec, char **src, char *line)
{
   char *c = skip_prefix("@write", line);
   parse_args_result r = parse_args(ec->mem_ctx, c);

   if (r.count != 2)
      failf("@write needs 2 arguments, found %d\n", r.count);

   const char *offset_reg = r.args[0];
   const char *data_reg   = r.args[1];

   assert(ec->bo.data.addr <= 0xFFFFFFFF);
   uint32_t base_addr = ec->bo.data.addr;

   switch (ec->devinfo->verx10) {
   case 90:
   case 110:
   case 120: {
      const char *send_suffix = ec->devinfo->verx10 < 120 ? "s" : "";
      ralloc_asprintf_append(src,
         "mul(8)          g127<1>UD  %s<8;8,1>UD    0x4UW     { align1 @1 1Q };\n"
         "add(8)          g127<1>UD  g127<8;8,1>UD  0x%08xUD  { align1 @1 1Q };\n"
         "send%s(8)       nullUD     g127UD         %sUD      0x2026efd   0x00000040\n"
         "    hdc1 MsgDesc: (DC untyped surface write, Surface = 253, "
         "                        SIMD8, Mask = 0xe) mlen 1 ex_mlen 1 rlen 0 "
         "    { align1 1Q @1 $1 };\n",
         offset_reg, base_addr, send_suffix, data_reg);
      executor_macro_syncnop(ec, src, "@syncnop");
      break;
   }

   case 125: {
      ralloc_asprintf_append(src,
         "mul(8)          g127<1>UD  %s<1;1,0>UD    0x4UW     { align1 @1 1Q };\n"
         "add(8)          g127<1>UD  g127<1;1,0>UD  0x%08xUD  { align1 @1 1Q };\n"
         "send(8)         nullUD     g127UD         %sUD      0x02000504 0x00000040\n"
         "    ugm MsgDesc: ( store, a32, d32, x, L1STATE_L3MOCS dst_len = 0, "
         "                   src0_len = 1, src1_len = 1, flat )  base_offset 0 "
         "    { align1 1Q A@1 $1 };\n",
         offset_reg, base_addr, data_reg);
      executor_macro_syncnop(ec, src, "@syncnop");
      break;
   }

   case 200:
   case 300: {
      ralloc_asprintf_append(src,
         "mul(16)          g127<1>UD  %s<1;1,0>UD    0x4UW     { align1 @1 1Q };\n"
         "add(16)          g127<1>UD  g127<1;1,0>UD  0x%08xUD  { align1 @1 1Q };\n"
         "send(16)         nullUD     g127UD         %sUD      0x02000504 0x00000040\n"
         "    ugm MsgDesc: ( store, a32, d32, x, L1STATE_L3MOCS dst_len = 0, "
         "                   src0_len = 1, src1_len = 1, flat ) base_offset 0  "
         "    { align1 1Q A@1 $1 };\n",
         offset_reg, base_addr, data_reg);
      executor_macro_syncnop(ec, src, "@syncnop");
      break;
   }

   default:
      UNREACHABLE("invalid gfx version");
   }
}

static void
executor_macro_read(executor_context *ec, char **src, char *line)
{
   char *c = skip_prefix("@read", line);
   parse_args_result r = parse_args(ec->mem_ctx, c);

   if (r.count != 2)
      failf("@read needs 2 arguments, found %d\n", r.count);

   /* Order follows underlying SEND, destination first. */
   const char *data_reg   = r.args[0];
   const char *offset_reg = r.args[1];

   assert(ec->bo.data.addr <= 0xFFFFFFFF);
   uint32_t base_addr = ec->bo.data.addr;

   switch (ec->devinfo->verx10) {
   case 90:
   case 110:
   case 120: {
      const char *send_suffix = ec->devinfo->verx10 < 120 ? "s" : "";
      ralloc_asprintf_append(src,
         "mul(8)          g127<1>UD  %s<8;8,1>UD    0x4UW     { align1 @1 1Q };\n"
         "add(8)          g127<1>UD  g127<8;8,1>UD  0x%08xUD  { align1 @1 1Q };\n"
         "send%s(8)       %sUD       g127UD         nullUD    0x2106efd   0x00000000\n"
         "    hdc1 MsgDesc: (DC untyped surface read, Surface = 253, "
         "                        SIMD8, Mask = 0xe) mlen 1 ex_mlen 0 rlen 1 "
         "    { align1 1Q @1 $1 };\n",
         offset_reg, base_addr, send_suffix, data_reg);
      executor_macro_syncnop(ec, src, "@syncnop");
      break;
   }

   case 125: {
      ralloc_asprintf_append(src,
         "mul(8)          g127<1>UD  %s<1;1,0>UD    0x4UW     { align1 @1 1Q };\n"
         "add(8)          g127<1>UD  g127<1;1,0>UD  0x%08xUD  { align1 @1 1Q };\n"
         "send(8)         %sUD       g127UD         nullUD    0x02100500 0x00000000\n"
         "    ugm MsgDesc: ( load, a32, d32, x, L1STATE_L3MOCS dst_len = 1, "
         "                   src0_len = 1, flat ) src1_len = 0  base_offset 0 "
         "    { align1 1Q A@1 $1 };\n",
         offset_reg, base_addr, data_reg);
      executor_macro_syncnop(ec, src, "@syncnop");
      break;
   }

   case 200:
   case 300: {
      ralloc_asprintf_append(src,
         "mul(16)         g127<1>UD  %s<1;1,0>UD    0x4UW     { align1 @1 1Q };\n"
         "add(16)         g127<1>UD  g127<1;1,0>UD  0x%08xUD  { align1 @1 1Q };\n"
         "send(16)        %sUD       g127UD         nullUD    0x02100500 0x00000000\n"
         "    ugm MsgDesc: ( load, a32, d32, x, L1STATE_L3MOCS dst_len = 1, "
         "                   src0_len = 1, flat ) src1_len = 0  base_offset 0 "
         "    { align1 1Q A@1 $1 };\n",
         offset_reg, base_addr, data_reg);
      executor_macro_syncnop(ec, src, "@syncnop");
      break;
   }

   default:
      UNREACHABLE("invalid gfx version");
   }
}

static char *
find_macro_symbol(char *line)
{
   char *c = line;
   while (isspace(*c)) c++;
   return *c == '@' ? c : NULL;
}

static bool
match_macro_name(const char *name, const char *line)
{
   if (!startswith(name, line))
      return false;
   line += strlen(name);
   return !*line || isspace(*line) || is_comment(line);
}

const char *
executor_apply_macros(executor_context *ec, const char *original_src)
{
   char *scratch = ralloc_strdup(ec->mem_ctx, original_src);

   /* Create a ralloc'ed empty string so can call append to it later. */
   char *src = ralloc_strdup(ec->mem_ctx, "");

   /* TODO: Create a @send macro for common combinations of MsgDesc. */
   static const struct {
      const char *name;
      void (*func)(executor_context *ec, char **output, char *line);
   } macros[] = {
      { "@eot",      executor_macro_eot },
      { "@mov",      executor_macro_mov },
      { "@write",    executor_macro_write },
      { "@read",     executor_macro_read },
      { "@id",       executor_macro_id },
      { "@syncnop",  executor_macro_syncnop },
   };

   char *next = scratch;
   while (next) {
      char *line = next;
      char *end = line;

      while (*end && *end != '\n') end++;
      next = *end ? end + 1 : NULL;
      *end = '\0';

      char *macro = find_macro_symbol(line);
      if (!macro) {
         ralloc_asprintf_append(&src, "%s\n", line);
      } else {
         bool found = false;
         for (int i = 0; i < ARRAY_SIZE(macros); i++) {
            if (match_macro_name(macros[i].name, macro)) {
               macros[i].func(ec, &src, macro);
               found = true;
               break;
            }
         }
         if (!found)
            failf("unsupported macro line: %s", macro);
      }
   }

   return src;
}
