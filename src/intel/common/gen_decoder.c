/*
 * Copyright © 2016 Intel Corporation
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
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <expat.h>
#include <inttypes.h>
#include <zlib.h>

#include <util/macros.h>
#include <util/ralloc.h>

#include "gen_decoder.h"

#include "genxml/genX_xml.h"

#define XML_BUFFER_SIZE 4096

#define MAX(a, b) ((a) < (b) ? (b) : (a))

#define MAKE_GEN(major, minor) ( ((major) << 8) | (minor) )

struct gen_spec {
   uint32_t gen;

   int ncommands;
   struct gen_group *commands[256];
   int nstructs;
   struct gen_group *structs[256];
   int nregisters;
   struct gen_group *registers[256];
   int nenums;
   struct gen_enum *enums[256];
};

struct location {
   const char *filename;
   int line_number;
};

struct parser_context {
   XML_Parser parser;
   int foo;
   struct location loc;
   const char *platform;

   struct gen_group *group;
   struct gen_enum *enoom;

   int nvalues;
   struct gen_value *values[256];

   struct gen_spec *spec;
};

const char *
gen_group_get_name(struct gen_group *group)
{
   return group->name;
}

uint32_t
gen_group_get_opcode(struct gen_group *group)
{
   return group->opcode;
}

struct gen_group *
gen_spec_find_struct(struct gen_spec *spec, const char *name)
{
   for (int i = 0; i < spec->nstructs; i++)
      if (strcmp(spec->structs[i]->name, name) == 0)
         return spec->structs[i];

   return NULL;
}

struct gen_group *
gen_spec_find_register(struct gen_spec *spec, uint32_t offset)
{
   for (int i = 0; i < spec->nregisters; i++)
      if (spec->registers[i]->register_offset == offset)
         return spec->registers[i];

   return NULL;
}

struct gen_group *
gen_spec_find_register_by_name(struct gen_spec *spec, const char *name)
{
   for (int i = 0; i < spec->nregisters; i++) {
      if (strcmp(spec->registers[i]->name, name) == 0)
         return spec->registers[i];
   }

   return NULL;
}

struct gen_enum *
gen_spec_find_enum(struct gen_spec *spec, const char *name)
{
   for (int i = 0; i < spec->nenums; i++)
      if (strcmp(spec->enums[i]->name, name) == 0)
         return spec->enums[i];

   return NULL;
}

uint32_t
gen_spec_get_gen(struct gen_spec *spec)
{
   return spec->gen;
}

static void __attribute__((noreturn))
fail(struct location *loc, const char *msg, ...)
{
   va_list ap;

   va_start(ap, msg);
   fprintf(stderr, "%s:%d: error: ",
           loc->filename, loc->line_number);
   vfprintf(stderr, msg, ap);
   fprintf(stderr, "\n");
   va_end(ap);
   exit(EXIT_FAILURE);
}

static void *
fail_on_null(void *p)
{
   if (p == NULL) {
      fprintf(stderr, "aubinator: out of memory\n");
      exit(EXIT_FAILURE);
   }

   return p;
}

static char *
xstrdup(const char *s)
{
   return fail_on_null(strdup(s));
}

static void *
zalloc(size_t s)
{
   return calloc(s, 1);
}

static void *
xzalloc(size_t s)
{
   return fail_on_null(zalloc(s));
}

static void
get_group_offset_count(const char **atts, uint32_t *offset, uint32_t *count,
                       uint32_t *size, bool *variable)
{
   char *p;
   int i;

   for (i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "count") == 0) {
         *count = strtoul(atts[i + 1], &p, 0);
         if (*count == 0)
            *variable = true;
      } else if (strcmp(atts[i], "start") == 0) {
         *offset = strtoul(atts[i + 1], &p, 0);
      } else if (strcmp(atts[i], "size") == 0) {
         *size = strtoul(atts[i + 1], &p, 0);
      }
   }
   return;
}

static struct gen_group *
create_group(struct parser_context *ctx,
             const char *name,
             const char **atts,
             struct gen_group *parent)
{
   struct gen_group *group;

   group = xzalloc(sizeof(*group));
   if (name)
      group->name = xstrdup(name);

   group->spec = ctx->spec;
   group->group_offset = 0;
   group->group_count = 0;
   group->variable = false;

   if (parent) {
      group->parent = parent;
      get_group_offset_count(atts,
                             &group->group_offset,
                             &group->group_count,
                             &group->group_size,
                             &group->variable);
   }

   return group;
}

static struct gen_enum *
create_enum(struct parser_context *ctx, const char *name, const char **atts)
{
   struct gen_enum *e;

   e = xzalloc(sizeof(*e));
   if (name)
      e->name = xstrdup(name);

   e->nvalues = 0;

   return e;
}

static void
get_register_offset(const char **atts, uint32_t *offset)
{
   char *p;
   int i;

   for (i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "num") == 0)
         *offset = strtoul(atts[i + 1], &p, 0);
   }
   return;
}

static void
get_start_end_pos(int *start, int *end)
{
   /* start value has to be mod with 32 as we need the relative
    * start position in the first DWord. For the end position, add
    * the length of the field to the start position to get the
    * relative postion in the 64 bit address.
    */
   if (*end - *start > 32) {
      int len = *end - *start;
      *start = *start % 32;
      *end = *start + len;
   } else {
      *start = *start % 32;
      *end = *end % 32;
   }

   return;
}

static inline uint64_t
mask(int start, int end)
{
   uint64_t v;

   v = ~0ULL >> (63 - end + start);

   return v << start;
}

static inline uint64_t
field(uint64_t value, int start, int end)
{
   get_start_end_pos(&start, &end);
   return (value & mask(start, end)) >> (start);
}

static inline uint64_t
field_address(uint64_t value, int start, int end)
{
   /* no need to right shift for address/offset */
   get_start_end_pos(&start, &end);
   return (value & mask(start, end));
}

static struct gen_type
string_to_type(struct parser_context *ctx, const char *s)
{
   int i, f;
   struct gen_group *g;
   struct gen_enum *e;

   if (strcmp(s, "int") == 0)
      return (struct gen_type) { .kind = GEN_TYPE_INT };
   else if (strcmp(s, "uint") == 0)
      return (struct gen_type) { .kind = GEN_TYPE_UINT };
   else if (strcmp(s, "bool") == 0)
      return (struct gen_type) { .kind = GEN_TYPE_BOOL };
   else if (strcmp(s, "float") == 0)
      return (struct gen_type) { .kind = GEN_TYPE_FLOAT };
   else if (strcmp(s, "address") == 0)
      return (struct gen_type) { .kind = GEN_TYPE_ADDRESS };
   else if (strcmp(s, "offset") == 0)
      return (struct gen_type) { .kind = GEN_TYPE_OFFSET };
   else if (sscanf(s, "u%d.%d", &i, &f) == 2)
      return (struct gen_type) { .kind = GEN_TYPE_UFIXED, .i = i, .f = f };
   else if (sscanf(s, "s%d.%d", &i, &f) == 2)
      return (struct gen_type) { .kind = GEN_TYPE_SFIXED, .i = i, .f = f };
   else if (g = gen_spec_find_struct(ctx->spec, s), g != NULL)
      return (struct gen_type) { .kind = GEN_TYPE_STRUCT, .gen_struct = g };
   else if (e = gen_spec_find_enum(ctx->spec, s), e != NULL)
      return (struct gen_type) { .kind = GEN_TYPE_ENUM, .gen_enum = e };
   else if (strcmp(s, "mbo") == 0)
      return (struct gen_type) { .kind = GEN_TYPE_MBO };
   else
      fail(&ctx->loc, "invalid type: %s", s);
}

static struct gen_field *
create_field(struct parser_context *ctx, const char **atts)
{
   struct gen_field *field;
   char *p;
   int i;

   field = xzalloc(sizeof(*field));

   for (i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "name") == 0)
         field->name = xstrdup(atts[i + 1]);
      else if (strcmp(atts[i], "start") == 0)
         field->start = strtoul(atts[i + 1], &p, 0);
      else if (strcmp(atts[i], "end") == 0) {
         field->end = strtoul(atts[i + 1], &p, 0);
      } else if (strcmp(atts[i], "type") == 0)
         field->type = string_to_type(ctx, atts[i + 1]);
      else if (strcmp(atts[i], "default") == 0 &&
               field->start >= 16 && field->end <= 31) {
         field->has_default = true;
         field->default_value = strtoul(atts[i + 1], &p, 0);
      }
   }

   return field;
}

static struct gen_value *
create_value(struct parser_context *ctx, const char **atts)
{
   struct gen_value *value = xzalloc(sizeof(*value));

   for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "name") == 0)
         value->name = xstrdup(atts[i + 1]);
      else if (strcmp(atts[i], "value") == 0)
         value->value = strtoul(atts[i + 1], NULL, 0);
   }

   return value;
}

static void
create_and_append_field(struct parser_context *ctx,
                        const char **atts)
{
   if (ctx->group->nfields == ctx->group->fields_size) {
      ctx->group->fields_size = MAX(ctx->group->fields_size * 2, 2);
      ctx->group->fields =
         (struct gen_field **) realloc(ctx->group->fields,
                                       sizeof(ctx->group->fields[0]) *
                                       ctx->group->fields_size);
   }

   ctx->group->fields[ctx->group->nfields++] = create_field(ctx, atts);
}

static void
start_element(void *data, const char *element_name, const char **atts)
{
   struct parser_context *ctx = data;
   int i;
   const char *name = NULL;
   const char *gen = NULL;

   ctx->loc.line_number = XML_GetCurrentLineNumber(ctx->parser);

   for (i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "name") == 0)
         name = atts[i + 1];
      else if (strcmp(atts[i], "gen") == 0)
         gen = atts[i + 1];
   }

   if (strcmp(element_name, "genxml") == 0) {
      if (name == NULL)
         fail(&ctx->loc, "no platform name given");
      if (gen == NULL)
         fail(&ctx->loc, "no gen given");

      ctx->platform = xstrdup(name);
      int major, minor;
      int n = sscanf(gen, "%d.%d", &major, &minor);
      if (n == 0)
         fail(&ctx->loc, "invalid gen given: %s", gen);
      if (n == 1)
         minor = 0;

      ctx->spec->gen = MAKE_GEN(major, minor);
   } else if (strcmp(element_name, "instruction") == 0 ||
              strcmp(element_name, "struct") == 0) {
      ctx->group = create_group(ctx, name, atts, NULL);
   } else if (strcmp(element_name, "register") == 0) {
      ctx->group = create_group(ctx, name, atts, NULL);
      get_register_offset(atts, &ctx->group->register_offset);
   } else if (strcmp(element_name, "group") == 0) {
      struct gen_group *previous_group = ctx->group;
      while (previous_group->next)
         previous_group = previous_group->next;

      struct gen_group *group = create_group(ctx, "", atts, ctx->group);
      previous_group->next = group;
      ctx->group = group;
   } else if (strcmp(element_name, "field") == 0) {
      create_and_append_field(ctx, atts);
   } else if (strcmp(element_name, "enum") == 0) {
      ctx->enoom = create_enum(ctx, name, atts);
   } else if (strcmp(element_name, "value") == 0) {
      ctx->values[ctx->nvalues++] = create_value(ctx, atts);
      assert(ctx->nvalues < ARRAY_SIZE(ctx->values));
   }

}

static void
end_element(void *data, const char *name)
{
   struct parser_context *ctx = data;
   struct gen_spec *spec = ctx->spec;

   if (strcmp(name, "instruction") == 0 ||
       strcmp(name, "struct") == 0 ||
       strcmp(name, "register") == 0) {
      struct gen_group *group = ctx->group;

      ctx->group = ctx->group->parent;

      for (int i = 0; i < group->nfields; i++) {
         if (group->fields[i]->start >= 16 &&
             group->fields[i]->end <= 31 &&
             group->fields[i]->has_default) {
            group->opcode_mask |=
               mask(group->fields[i]->start % 32, group->fields[i]->end % 32);
            group->opcode |=
               group->fields[i]->default_value << group->fields[i]->start;
         }
      }

      if (strcmp(name, "instruction") == 0)
         spec->commands[spec->ncommands++] = group;
      else if (strcmp(name, "struct") == 0)
         spec->structs[spec->nstructs++] = group;
      else if (strcmp(name, "register") == 0)
         spec->registers[spec->nregisters++] = group;

      assert(spec->ncommands < ARRAY_SIZE(spec->commands));
      assert(spec->nstructs < ARRAY_SIZE(spec->structs));
      assert(spec->nregisters < ARRAY_SIZE(spec->registers));
   } else if (strcmp(name, "group") == 0) {
      ctx->group = ctx->group->parent;
   } else if (strcmp(name, "field") == 0) {
      assert(ctx->group->nfields > 0);
      struct gen_field *field = ctx->group->fields[ctx->group->nfields - 1];
      size_t size = ctx->nvalues * sizeof(ctx->values[0]);
      field->inline_enum.values = xzalloc(size);
      field->inline_enum.nvalues = ctx->nvalues;
      memcpy(field->inline_enum.values, ctx->values, size);
      ctx->nvalues = 0;
   } else if (strcmp(name, "enum") == 0) {
      struct gen_enum *e = ctx->enoom;
      size_t size = ctx->nvalues * sizeof(ctx->values[0]);
      e->values = xzalloc(size);
      e->nvalues = ctx->nvalues;
      memcpy(e->values, ctx->values, size);
      ctx->nvalues = 0;
      ctx->enoom = NULL;
      spec->enums[spec->nenums++] = e;
   }
}

static void
character_data(void *data, const XML_Char *s, int len)
{
}

static int
devinfo_to_gen(const struct gen_device_info *devinfo)
{
   int value = 10 * devinfo->gen;

   if (devinfo->is_baytrail || devinfo->is_haswell)
      value += 5;

   return value;
}

static uint32_t zlib_inflate(const void *compressed_data,
                             uint32_t compressed_len,
                             void **out_ptr)
{
   struct z_stream_s zstream;
   void *out;

   memset(&zstream, 0, sizeof(zstream));

   zstream.next_in = (unsigned char *)compressed_data;
   zstream.avail_in = compressed_len;

   if (inflateInit(&zstream) != Z_OK)
      return 0;

   out = malloc(4096);
   zstream.next_out = out;
   zstream.avail_out = 4096;

   do {
      switch (inflate(&zstream, Z_SYNC_FLUSH)) {
      case Z_STREAM_END:
         goto end;
      case Z_OK:
         break;
      default:
         inflateEnd(&zstream);
         return 0;
      }

      if (zstream.avail_out)
         break;

      out = realloc(out, 2*zstream.total_out);
      if (out == NULL) {
         inflateEnd(&zstream);
         return 0;
      }

      zstream.next_out = (unsigned char *)out + zstream.total_out;
      zstream.avail_out = zstream.total_out;
   } while (1);
 end:
   inflateEnd(&zstream);
   *out_ptr = out;
   return zstream.total_out;
}

struct gen_spec *
gen_spec_load(const struct gen_device_info *devinfo)
{
   struct parser_context ctx;
   void *buf;
   uint8_t *text_data;
   uint32_t text_offset = 0, text_length = 0, total_length;
   uint32_t gen_10 = devinfo_to_gen(devinfo);

   for (int i = 0; i < ARRAY_SIZE(genxml_files_table); i++) {
      if (genxml_files_table[i].gen_10 == gen_10) {
         text_offset = genxml_files_table[i].offset;
         text_length = genxml_files_table[i].length;
         break;
      }
   }

   if (text_length == 0) {
      fprintf(stderr, "unable to find gen (%u) data\n", gen_10);
      return NULL;
   }

   memset(&ctx, 0, sizeof ctx);
   ctx.parser = XML_ParserCreate(NULL);
   XML_SetUserData(ctx.parser, &ctx);
   if (ctx.parser == NULL) {
      fprintf(stderr, "failed to create parser\n");
      return NULL;
   }

   XML_SetElementHandler(ctx.parser, start_element, end_element);
   XML_SetCharacterDataHandler(ctx.parser, character_data);

   ctx.spec = xzalloc(sizeof(*ctx.spec));

   total_length = zlib_inflate(compress_genxmls,
                               sizeof(compress_genxmls),
                               (void **) &text_data);
   assert(text_offset + text_length <= total_length);

   buf = XML_GetBuffer(ctx.parser, text_length);
   memcpy(buf, &text_data[text_offset], text_length);

   if (XML_ParseBuffer(ctx.parser, text_length, true) == 0) {
      fprintf(stderr,
              "Error parsing XML at line %ld col %ld byte %ld/%u: %s\n",
              XML_GetCurrentLineNumber(ctx.parser),
              XML_GetCurrentColumnNumber(ctx.parser),
              XML_GetCurrentByteIndex(ctx.parser), text_length,
              XML_ErrorString(XML_GetErrorCode(ctx.parser)));
      XML_ParserFree(ctx.parser);
      free(text_data);
      return NULL;
   }

   XML_ParserFree(ctx.parser);
   free(text_data);

   return ctx.spec;
}

struct gen_spec *
gen_spec_load_from_path(const struct gen_device_info *devinfo,
                        const char *path)
{
   struct parser_context ctx;
   size_t len, filename_len = strlen(path) + 20;
   char *filename = malloc(filename_len);
   void *buf;
   FILE *input;

   len = snprintf(filename, filename_len, "%s/gen%i.xml",
                  path, devinfo_to_gen(devinfo));
   assert(len < filename_len);

   input = fopen(filename, "r");
   if (input == NULL) {
      fprintf(stderr, "failed to open xml description\n");
      free(filename);
      return NULL;
   }

   memset(&ctx, 0, sizeof ctx);
   ctx.parser = XML_ParserCreate(NULL);
   XML_SetUserData(ctx.parser, &ctx);
   if (ctx.parser == NULL) {
      fprintf(stderr, "failed to create parser\n");
      fclose(input);
      free(filename);
      return NULL;
   }

   XML_SetElementHandler(ctx.parser, start_element, end_element);
   XML_SetCharacterDataHandler(ctx.parser, character_data);
   ctx.loc.filename = filename;
   ctx.spec = xzalloc(sizeof(*ctx.spec));

   do {
      buf = XML_GetBuffer(ctx.parser, XML_BUFFER_SIZE);
      len = fread(buf, 1, XML_BUFFER_SIZE, input);
      if (len == 0) {
         fprintf(stderr, "fread: %m\n");
         free(ctx.spec);
         ctx.spec = NULL;
         goto end;
      }
      if (XML_ParseBuffer(ctx.parser, len, len == 0) == 0) {
         fprintf(stderr,
                 "Error parsing XML at line %ld col %ld: %s\n",
                 XML_GetCurrentLineNumber(ctx.parser),
                 XML_GetCurrentColumnNumber(ctx.parser),
                 XML_ErrorString(XML_GetErrorCode(ctx.parser)));
         free(ctx.spec);
         ctx.spec = NULL;
         goto end;
      }
   } while (len > 0);

 end:
   XML_ParserFree(ctx.parser);

   fclose(input);
   free(filename);

   return ctx.spec;
}

struct gen_group *
gen_spec_find_instruction(struct gen_spec *spec, const uint32_t *p)
{
   for (int i = 0; i < spec->ncommands; i++) {
      uint32_t opcode = *p & spec->commands[i]->opcode_mask;
      if (opcode == spec->commands[i]->opcode)
         return spec->commands[i];
   }

   return NULL;
}

int
gen_group_get_length(struct gen_group *group, const uint32_t *p)
{
   uint32_t h = p[0];
   uint32_t type = field(h, 29, 31);

   switch (type) {
   case 0: /* MI */ {
      uint32_t opcode = field(h, 23, 28);
      if (opcode < 16)
         return 1;
      else
         return field(h, 0, 7) + 2;
      break;
   }

   case 2: /* BLT */ {
      return field(h, 0, 7) + 2;
   }

   case 3: /* Render */ {
      uint32_t subtype = field(h, 27, 28);
      uint32_t opcode = field(h, 24, 26);
      uint16_t whole_opcode = field(h, 16, 31);
      switch (subtype) {
      case 0:
         if (whole_opcode == 0x6104 /* PIPELINE_SELECT_965 */)
            return 1;
         else if (opcode < 2)
            return field(h, 0, 7) + 2;
         else
            return -1;
      case 1:
         if (opcode < 2)
            return 1;
         else
            return -1;
      case 2: {
         if (opcode == 0)
            return field(h, 0, 7) + 2;
         else if (opcode < 3)
            return field(h, 0, 15) + 2;
         else
            return -1;
      }
      case 3:
         if (whole_opcode == 0x780b)
            return 1;
         else if (opcode < 4)
            return field(h, 0, 7) + 2;
         else
            return -1;
      }
   }
   }

   return -1;
}

void
gen_field_iterator_init(struct gen_field_iterator *iter,
                        struct gen_group *group,
                        const uint32_t *p,
                        bool print_colors)
{
   memset(iter, 0, sizeof(*iter));

   iter->group = group;
   iter->p = p;
   iter->print_colors = print_colors;
}

static const char *
gen_get_enum_name(struct gen_enum *e, uint64_t value)
{
   for (int i = 0; i < e->nvalues; i++) {
      if (e->values[i]->value == value) {
         return e->values[i]->name;
      }
   }
   return NULL;
}

static bool
iter_more_fields(const struct gen_field_iterator *iter)
{
   return iter->field_iter < iter->group->nfields;
}

static uint32_t
iter_group_offset_bits(const struct gen_field_iterator *iter,
                       uint32_t group_iter)
{
   return iter->group->group_offset + (group_iter * iter->group->group_size);
}

static bool
iter_more_groups(const struct gen_field_iterator *iter)
{
   if (iter->group->variable) {
      return iter_group_offset_bits(iter, iter->group_iter + 1) <
              (gen_group_get_length(iter->group, iter->p) * 32);
   } else {
      return (iter->group_iter + 1) < iter->group->group_count ||
         iter->group->next != NULL;
   }
}

static void
iter_advance_group(struct gen_field_iterator *iter)
{
   if (iter->group->variable)
      iter->group_iter++;
   else {
      if ((iter->group_iter + 1) < iter->group->group_count) {
         iter->group_iter++;
      } else {
         iter->group = iter->group->next;
         iter->group_iter = 0;
      }
   }

   iter->field_iter = 0;
}

static bool
iter_advance_field(struct gen_field_iterator *iter)
{
   while (!iter_more_fields(iter)) {
      if (!iter_more_groups(iter))
         return false;

      iter_advance_group(iter);
   }

   iter->field = iter->group->fields[iter->field_iter++];
   if (iter->field->name)
       strncpy(iter->name, iter->field->name, sizeof(iter->name));
   else
      memset(iter->name, 0, sizeof(iter->name));
   iter->dword = iter_group_offset_bits(iter, iter->group_iter) / 32 +
      iter->field->start / 32;
   iter->struct_desc = NULL;

   return true;
}

bool
gen_field_iterator_next(struct gen_field_iterator *iter)
{
   union {
      uint64_t qw;
      float f;
   } v;

   if (!iter_advance_field(iter))
      return false;

   if ((iter->field->end - iter->field->start) > 32)
      v.qw = ((uint64_t) iter->p[iter->dword+1] << 32) | iter->p[iter->dword];
   else
      v.qw = iter->p[iter->dword];

   const char *enum_name = NULL;

   switch (iter->field->type.kind) {
   case GEN_TYPE_UNKNOWN:
   case GEN_TYPE_INT: {
      uint64_t value = field(v.qw, iter->field->start, iter->field->end);
      snprintf(iter->value, sizeof(iter->value), "%"PRId64, value);
      enum_name = gen_get_enum_name(&iter->field->inline_enum, value);
      break;
   }
   case GEN_TYPE_UINT: {
      uint64_t value = field(v.qw, iter->field->start, iter->field->end);
      snprintf(iter->value, sizeof(iter->value), "%"PRIu64, value);
      enum_name = gen_get_enum_name(&iter->field->inline_enum, value);
      break;
   }
   case GEN_TYPE_BOOL: {
      const char *true_string =
         iter->print_colors ? "\e[0;35mtrue\e[0m" : "true";
      snprintf(iter->value, sizeof(iter->value), "%s",
               field(v.qw, iter->field->start, iter->field->end) ?
               true_string : "false");
      break;
   }
   case GEN_TYPE_FLOAT:
      snprintf(iter->value, sizeof(iter->value), "%f", v.f);
      break;
   case GEN_TYPE_ADDRESS:
   case GEN_TYPE_OFFSET:
      snprintf(iter->value, sizeof(iter->value), "0x%08"PRIx64,
               field_address(v.qw, iter->field->start, iter->field->end));
      break;
   case GEN_TYPE_STRUCT:
      snprintf(iter->value, sizeof(iter->value), "<struct %s>",
               iter->field->type.gen_struct->name);
      iter->struct_desc =
         gen_spec_find_struct(iter->group->spec,
                              iter->field->type.gen_struct->name);
      break;
   case GEN_TYPE_UFIXED:
      snprintf(iter->value, sizeof(iter->value), "%f",
               (float) field(v.qw, iter->field->start,
                             iter->field->end) / (1 << iter->field->type.f));
      break;
   case GEN_TYPE_SFIXED:
      /* FIXME: Sign extend extracted field. */
      snprintf(iter->value, sizeof(iter->value), "%s", "foo");
      break;
   case GEN_TYPE_MBO:
       break;
   case GEN_TYPE_ENUM: {
      uint64_t value = field(v.qw, iter->field->start, iter->field->end);
      snprintf(iter->value, sizeof(iter->value),
               "%"PRId64, value);
      enum_name = gen_get_enum_name(iter->field->type.gen_enum, value);
      break;
   }
   }

   if (strlen(iter->group->name) == 0) {
      int length = strlen(iter->name);
      snprintf(iter->name + length, sizeof(iter->name) - length,
               "[%i]", iter->group_iter);
   }

   if (enum_name) {
      int length = strlen(iter->value);
      snprintf(iter->value + length, sizeof(iter->value) - length,
               " (%s)", enum_name);
   }

   return true;
}

static void
print_dword_header(FILE *outfile,
                   struct gen_field_iterator *iter, uint64_t offset)
{
   fprintf(outfile, "0x%08"PRIx64":  0x%08x : Dword %d\n",
           offset + 4 * iter->dword, iter->p[iter->dword], iter->dword);
}

static bool
is_header_field(struct gen_group *group, struct gen_field *field)
{
   uint32_t bits;

   if (field->start >= 32)
      return false;

   bits = (1U << (field->end - field->start + 1)) - 1;
   bits <<= field->start;

   return (group->opcode_mask & bits) != 0;
}

void
gen_print_group(FILE *outfile, struct gen_group *group,
                uint64_t offset, const uint32_t *p, bool color)
{
   struct gen_field_iterator iter;
   int last_dword = 0;

   gen_field_iterator_init(&iter, group, p, color);
   while (gen_field_iterator_next(&iter)) {
      if (last_dword != iter.dword) {
         print_dword_header(outfile, &iter, offset);
         last_dword = iter.dword;
      }
      if (!is_header_field(group, iter.field)) {
         fprintf(outfile, "    %s: %s\n", iter.name, iter.value);
         if (iter.struct_desc) {
            uint64_t struct_offset = offset + 4 * iter.dword;
            gen_print_group(outfile, iter.struct_desc, struct_offset,
                            &p[iter.dword], color);
         }
      }
   }
}
