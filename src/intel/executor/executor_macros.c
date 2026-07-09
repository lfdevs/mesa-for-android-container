/*
 * Copyright © 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <ctype.h>
#include <stdlib.h>

#include "util/ralloc.h"

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
   const unsigned width = ec->devinfo->ver >= 20 ? 16 : 8;

   if (strchr(value, '.')) {
      union {
         float f;
         uint32_t u;
      } val;

      val.f = strtof(value, NULL);
      ralloc_asprintf_append(src,
         "mov (%u) %s:f 0x%08x:f\n",
         width, reg, val.u);
   } else {
      for (char *x = value; *x; x++)
         *x = tolower(*x);

      ralloc_asprintf_append(src,
         "mov (%u) %s %s\n",
         width, reg, value);
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
executor_macro_eot(executor_context *ec, char **src, char *line)
{
   switch (ec->devinfo->verx10) {
   case 90:
   case 110: {
      ralloc_strcat(src,
         "(W) mov (8) r127 r0\n"
         "(W) send.ts (8) null r127:1 null:0 0x00000000 0x82000010 {EOT}\n");
      break;
   }

   case 120: {
      ralloc_strcat(src,
         "(W) mov (8) r127 r0\n"
         "(W) send.ts (8) null r127:1 null:0 0x00000000 0x02000000 {@1,EOT}\n");
      break;
   }

   case 125: {
      ralloc_strcat(src,
         "(W) mov (8) r127 r0\n"
         "(W) send.gtwy (8) null r127:1 null:0 0x00000000 0x02000000 {A@1,EOT}\n");
      break;
   }

   case 200:
   case 300:
   case 350: {
      ralloc_strcat(src,
         "(W) mov (16) r127 r0\n"
         "(W) send.gtwy (16) null r127:1 null:0 0x00000000 0x02000000 {I@1,EOT}\n");
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
   case 120: {
      ralloc_asprintf_append(src,
         "(W) mov (8) r127:uw 0x76543210:v\n"
         "(W) mov (8) %s r127:uw {@1}\n",
         reg);
      break;
   }

   case 125: {
      ralloc_asprintf_append(src,
         "(W) mov (8) r127:uw 0x76543210:v\n"
         "(W) mov (8) %s r127:uw {A@1}\n",
         reg);
      break;
   }

   case 200:
   case 300:
   case 350: {
      ralloc_asprintf_append(src,
         "(W) mov (8) r127:uw 0x76543210:v\n"
         "(W) add (8) r127.8:uw r127:uw 8:uw {A@1}\n"
         "(W) mov (16) %s r127:uw {A@1}\n",
         reg);
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
      const char *send_op = ec->devinfo->verx10 < 120 ? "sends.hdc1" : "send.hdc1";
      ralloc_asprintf_append(src,
         "mul (8) r127 %s 0x4:uw {@1}\n"
         "add (8) r127 r127 0x%08x {@1}\n"
         "%s (8) null r127:1 %s:1 0x00000040 0x02026efd {@1,$1}\n",
         offset_reg, base_addr, send_op, data_reg);
      executor_macro_syncnop(ec, src, "@syncnop");
      break;
   }

   case 125: {
      ralloc_asprintf_append(src,
         "mul (8) r127 %s 0x4:uw {A@1}\n"
         "add (8) r127 r127 0x%08x {A@1}\n"
         "store.ugm.d32.a32 (8) null r127:1 %s:1 {A@1,$1}\n",
         offset_reg, base_addr, data_reg);
      executor_macro_syncnop(ec, src, "@syncnop");
      break;
   }

   case 200:
   case 300:
   case 350: {
      ralloc_asprintf_append(src,
         "mul (16) r127 %s 0x4:uw {A@1}\n"
         "add (16) r127 r127 0x%08x {A@1}\n"
         "store.ugm.d32.a32 (16) null r127:1 %s:1 {A@1,$1}\n",
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
      const char *send_op = ec->devinfo->verx10 < 120 ? "sends.hdc1" : "send.hdc1";
      ralloc_asprintf_append(src,
         "mul (8) r127 %s 0x4:uw {@1}\n"
         "add (8) r127 r127 0x%08x {@1}\n"
         "%s (8) %s r127:1 null:0 0x00000000 0x02106efd {@1,$1}\n",
         offset_reg, base_addr, send_op, data_reg);
      executor_macro_syncnop(ec, src, "@syncnop");
      break;
   }

   case 125: {
      ralloc_asprintf_append(src,
         "mul (8) r127 %s 0x4:uw {A@1}\n"
         "add (8) r127 r127 0x%08x {A@1}\n"
         "load.ugm.d32.a32 (8) %s:1 r127:1 {A@1,$1}\n",
         offset_reg, base_addr, data_reg);
      executor_macro_syncnop(ec, src, "@syncnop");
      break;
   }

   case 200:
   case 300:
   case 350: {
      ralloc_asprintf_append(src,
         "mul (16) r127 %s 0x4:uw {A@1}\n"
         "add (16) r127 r127 0x%08x {A@1}\n"
         "load.ugm.d32.a32 (16) %s:1 r127:1 {A@1,$1}\n",
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
