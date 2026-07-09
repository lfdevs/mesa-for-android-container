/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ac_spm_config.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ac_perfcounter.h"
#include "util/u_string.h"
#include "util/os_file.h"
#include "util/u_dynarray.h"
#include "util/u_memory.h"

/* AC_SPM_RAW_COUNTER_ID_USER_BASE is defined in ac_spm.h. */

// Strip C-style block comments (/*...*/, possibly multi-line) in place by
// replacing them with spaces (preserving line numbers/offsets).
static void
strip_block_comments(char *s)
{
   bool in_comment = false;

   for (char *p = s; *p; ) {
      if (!in_comment) {
         if (p[0] == '/' && p[1] == '*') {
            *p++ = ' ';
            *p++ = ' ';
            in_comment = true;
         } else {
            ++p;
         }
      } else {
         if (p[0] == '*' && p[1] == '/') {
            *p++ = ' ';
            *p++ = ' ';
            in_comment = false;
         } else {
            if (*p != '\n')
               *p = ' ';
            ++p;
         }
      }
   }
}

static char *
trim(char *s)
{
   while (*s && isspace((unsigned char)*s))
      ++s;
   if (!*s)
      return s;
   char *end = s + strlen(s);
   while (end > s && isspace((unsigned char)end[-1]))
      --end;
   *end = '\0';
   return s;
}

static struct ac_pc_block *
find_block_by_name(const struct ac_perfcounters *pc, const char *name)
{
   for (unsigned i = 0; i < pc->num_blocks; i++) {
      struct ac_pc_block *block = &pc->blocks[i];
      if (block->b && block->b->b && block->b->b->name &&
          !strcmp(block->b->b->name, name))
         return block;
   }
   return NULL;
}

static bool
parse_op(const char *s, enum ac_spm_raw_counter_op *op_out)
{
   if (!strcasecmp(s, "sum")) {
      *op_out = AC_SPM_RAW_COUNTER_OP_SUM;
      return true;
   }
   if (!strcasecmp(s, "max")) {
      *op_out = AC_SPM_RAW_COUNTER_OP_MAX;
      return true;
   }
   if (!strcasecmp(s, "avg")) {
      *op_out = AC_SPM_RAW_COUNTER_OP_AVG;
      return true;
   }
   return false;
}

struct parse_state {
   /* Group currently being filled (-1 if none). */
   int current_group;

   /* Growable arrays. */
   struct util_dynarray groups;        /* struct ac_spm_user_group */
   struct util_dynarray counters;      /* struct ac_spm_user_counter */
   struct util_dynarray descrs;        /* struct ac_spm_counter_descr */
   struct util_dynarray create_infos;  /* struct ac_spm_counter_create_info */
};

/* The name strings below are allocated with strdup() and must be released
 * with the libc free(), not Mesa's FREE().
 */
static void
free_parse_state(struct parse_state *ps)
{
   util_dynarray_foreach(&ps->groups, struct ac_spm_user_group, g)
      free(g->name);
   util_dynarray_foreach(&ps->counters, struct ac_spm_user_counter, c)
      free(c->name);
   util_dynarray_fini(&ps->groups);
   util_dynarray_fini(&ps->counters);
   util_dynarray_fini(&ps->descrs);
   util_dynarray_fini(&ps->create_infos);
   memset(ps, 0, sizeof(*ps));
}

/* If `s` is wrapped in matching '"' or '\'' quotes, strip them in place
 * and return a pointer to the unquoted content. Otherwise returns `s`
 * unchanged. Trailing whitespace after a closing quote is tolerated.
 */
static char *
strip_quotes(char *s)
{
   if (!s || !*s)
      return s;
   char q = s[0];
   if (q != '"' && q != '\'')
      return s;
   size_t len = strlen(s);
   /* Allow trailing spaces after the closing quote. */
   while (len > 1 && isspace((unsigned char)s[len - 1]))
      len--;
   if (len < 2 || s[len - 1] != q)
      return s; /* unbalanced — leave as-is */
   s[len - 1] = '\0';
   return s + 1;
}

static bool
parse_group_line(struct parse_state *ps, char *line, unsigned lineno)
{
   /* line was trimmed and known to start with '['. */
   char *end = strrchr(line, ']');
   if (!end || end == line + 1) {
      fprintf(stderr,
              "ac/spm-config: line %u: malformed section header '%s'\n",
              lineno, line);
      return false;
   }
   *end = '\0';
   char *inner = trim(line + 1);
   if (!*inner) {
      fprintf(stderr,
              "ac/spm-config: line %u: empty section header\n", lineno);
      return false;
   }

   /* [NAME] or ["NAME"] — start a new group. NAME may optionally be
    * wrapped in double or single quotes, which allows ']' or '=' inside
    * the displayed name (e.g. "Memory (%)").
    */
   char *name;
   if (inner[0] == '"' || inner[0] == '\'') {
      name = strip_quotes(inner);
   } else if (strchr(inner, '=')) {
      fprintf(stderr,
              "ac/spm-config: line %u: unknown section header '[%s]' "
              "(use [NAME] or [\"NAME\"])\n",
              lineno, inner);
      return false;
   } else {
      name = inner;
   }
   if (!*name) {
      fprintf(stderr,
              "ac/spm-config: line %u: empty group name\n", lineno);
      return false;
   }

   struct ac_spm_user_group *g =
      util_dynarray_grow(&ps->groups, struct ac_spm_user_group, 1);
   if (!g)
      return false;
   *g = (struct ac_spm_user_group){
      .name = strdup(name),
      .first_counter = util_dynarray_num_elements(
         &ps->counters, struct ac_spm_user_counter),
   };
   if (!g->name)
      return false;

   ps->current_group =
      (int)util_dynarray_num_elements(&ps->groups,
                                      struct ac_spm_user_group) - 1;
   return true;
}

static bool
parse_counter_line(struct parse_state *ps,
                   const struct ac_perfcounters *pc,
                   char *line, unsigned lineno)
{
   if (ps->current_group < 0) {
      fprintf(stderr,
              "ac/spm-config: line %u: counter outside of any [group]\n",
              lineno);
      return false;
   }

   /* Split NAME=REST. */
   char *eq = strchr(line, '=');
   if (!eq) {
      fprintf(stderr,
              "ac/spm-config: line %u: missing '=' in '%s'\n",
              lineno, line);
      return false;
   }
   *eq = '\0';
   char *name = trim(line);
   char *rest = trim(eq + 1);
   if (!*name) {
      fprintf(stderr,
              "ac/spm-config: line %u: empty counter name\n", lineno);
      return false;
   }

   /* Tokenize REST as BLOCK,EVENT_ID,INSTANCE,OP. */
   char *tokens[4] = {0};
   unsigned n = 0;
   for (char *tok = rest; tok && n < 4; ++n) {
      char *comma = strchr(tok, ',');
      if (comma)
         *comma = '\0';
      tokens[n] = trim(tok);
      tok = comma ? comma + 1 : NULL;
   }
   if (n != 4) {
      fprintf(stderr,
              "ac/spm-config: line %u: expected NAME=BLOCK,EVENT_ID,"
              "INSTANCE,OP\n", lineno);
      return false;
   }

   struct ac_pc_block *block = find_block_by_name(pc, tokens[0]);
   if (!block) {
      fprintf(stderr,
              "ac/spm-config: line %u: unknown block '%s'\n",
              lineno, tokens[0]);
      return false;
   }

   errno = 0;
   char *endp = NULL;
   unsigned long event_id = strtoul(tokens[1], &endp, 0);
   if (errno || !endp || *endp) {
      fprintf(stderr,
              "ac/spm-config: line %u: invalid event id '%s'\n",
              lineno, tokens[1]);
      return false;
   }

   bool all_instances = !strcasecmp(tokens[2], "all");
   unsigned long instance = 0;
   if (!all_instances) {
      errno = 0;
      endp = NULL;
      instance = strtoul(tokens[2], &endp, 0);
      if (errno || !endp || *endp) {
         fprintf(stderr,
                 "ac/spm-config: line %u: invalid instance '%s' "
                 "(use a number or ALL)\n", lineno, tokens[2]);
         return false;
      }
      if (instance >= block->num_instances) {
         fprintf(stderr,
                 "ac/spm-config: line %u: instance %lu out of range "
                 "(block %s has %u instances)\n",
                 lineno, instance, tokens[0], block->num_instances);
         return false;
      }
   }

   enum ac_spm_raw_counter_op op;
   if (!parse_op(tokens[3], &op)) {
      fprintf(stderr,
              "ac/spm-config: line %u: unsupported op '%s' (use "
              "'sum', 'max' or 'avg')\n",
              lineno, tokens[3]);
      return false;
   }

   /* Reserve a slot for this user counter. */
   const uint32_t counter_index = util_dynarray_num_elements(
      &ps->counters, struct ac_spm_user_counter);
   struct ac_spm_user_counter *uc =
      util_dynarray_grow(&ps->counters, struct ac_spm_user_counter, 1);
   if (!uc)
      return false;
   *uc = (struct ac_spm_user_counter){
      .name = strdup(name),
      .op = op,
      .group_index = (uint32_t)ps->current_group,
      .first_create_info = util_dynarray_num_elements(
         &ps->create_infos, struct ac_spm_counter_create_info),
   };
   if (!uc->name)
      return false;

   /* Each user counter gets a unique runtime raw counter id. */
   const uint32_t raw_id = AC_SPM_RAW_COUNTER_ID_USER_BASE + counter_index;

   const unsigned first = all_instances ? 0 : (unsigned)instance;
   const unsigned last = all_instances ? block->num_instances
                                       : (unsigned)instance + 1;

   for (unsigned i = first; i < last; i++) {
      struct ac_spm_counter_descr *d = util_dynarray_grow(
         &ps->descrs, struct ac_spm_counter_descr, 1);
      struct ac_spm_counter_create_info *ci = util_dynarray_grow(
         &ps->create_infos, struct ac_spm_counter_create_info, 1);
      if (!d || !ci)
         return false;

      *d = (struct ac_spm_counter_descr){
         .id = (enum ac_spm_raw_counter_id)raw_id,
         .gpu_block = block->b->b->gpu_block,
         .event_id = (uint32_t)event_id,
      };
      /* ci->b is re-pointed in finalize_descriptors() once the descrs
       * array is stable; the value stored here is only a placeholder.
       */
      *ci = (struct ac_spm_counter_create_info){
         .b = d,
         .instance = i,
      };
   }

   uc->num_create_infos =
      util_dynarray_num_elements(&ps->create_infos,
                                 struct ac_spm_counter_create_info)
      - uc->first_create_info;

   struct ac_spm_user_group *cur_group = util_dynarray_element(
      &ps->groups, struct ac_spm_user_group, ps->current_group);
   cur_group->num_counters++;
   return true;
}

static bool
finalize_descriptors(struct ac_spm_user_config *cfg)
{
   /* Re-point each create_info->b to its descriptor.
    *
    * During parsing the descrs[] array can be reallocated, which
    * invalidates the per-create_info ->b pointers stored earlier. Fix
    * them up now that the array is stable.
    */
   for (uint32_t i = 0; i < cfg->num_create_infos; i++)
      cfg->create_infos[i].b = &cfg->descrs[i];

   /* Each user counter is auto-promoted to one pass-through derived
    * counter. Fill its descriptor in-place.
    */
   for (uint32_t i = 0; i < cfg->num_counters; i++) {
      struct ac_spm_user_counter *uc = &cfg->counters[i];
      struct ac_spm_derived_counter_descr *d = &uc->derived_descr;

      d->id = (enum ac_spm_counter_id)i;
      d->name = uc->name;
      d->desc = "";
      d->usage = AC_SPM_USAGE_ITEMS;
      d->num_components = 0;
   }

   /* Fill group descriptors and wire group->counter indirection. */
   for (uint32_t i = 0; i < cfg->num_groups; i++) {
      struct ac_spm_user_group *g = &cfg->groups[i];
      struct ac_spm_derived_group_descr *d = &g->derived_descr;

      if (g->num_counters > AC_SPM_MAX_COUNTERS_PER_GROUP) {
         fprintf(stderr,
                 "ac/spm-config: group '%s' has %u counters; max is %u\n",
                 g->name, g->num_counters,
                 AC_SPM_MAX_COUNTERS_PER_GROUP);
         return false;
      }
      for (uint32_t j = 0; j < g->num_counters; j++) {
         struct ac_spm_user_counter *uc =
            &cfg->counters[g->first_counter + j];
         uc->derived_descr.group_id = (enum ac_spm_group_id)i;
         d->counters[j] = &uc->derived_descr;
      }
      d->id = (enum ac_spm_group_id)i;
      d->name = g->name;
      d->num_counters = g->num_counters;
   }

   /* Reject configs that exceed the static arrays in
    * ac_spm_derived_trace.
    */
   if (cfg->num_groups > AC_SPM_DERIVED_GROUP_MAX) {
      fprintf(stderr,
              "ac/spm-config: too many groups (%u, max %u)\n",
              cfg->num_groups, AC_SPM_DERIVED_GROUP_MAX);
      return false;
   }
   if (cfg->num_counters > AC_SPM_DERIVED_COUNTER_MAX) {
      fprintf(stderr,
              "ac/spm-config: too many counters (%u, max %u)\n",
              cfg->num_counters, AC_SPM_DERIVED_COUNTER_MAX);
      return false;
   }

   return true;
}

bool
ac_spm_user_config_load(const char *path,
                        const struct ac_perfcounters *pc,
                        struct ac_spm_user_config **out)
{
   char *buf = os_read_file(path, NULL);
   if (!buf) {
      fprintf(stderr, "ac/spm-config: Failed to open '%s': %s\n",
              path, strerror(errno));
      return false;
   }

   strip_block_comments(buf);

   struct parse_state ps = { .current_group = -1 };

   unsigned lineno = 0;
   bool ok = true;
   for (char *line = buf, *next = NULL; line && *line; line = next) {
      lineno++;

      char *nl = strchr(line, '\n');
      if (nl) {
         *nl = '\0';
         next = nl + 1;
      } else {
         next = NULL;
      }

      /* Strip '#' comment. */
      char *hash = strchr(line, '#');
      if (hash)
         *hash = '\0';

      char *t = trim(line);
      if (!*t)
         continue;

      if (t[0] == '[') {
         if (!parse_group_line(&ps, t, lineno)) {
            ok = false;
            break;
         }
      } else if (t[0] == '@') {
         fprintf(stderr,
                 "ac/spm-config: line %u: '@' directives are not "
                 "supported\n", lineno);
         ok = false;
         break;
      } else {
         if (!parse_counter_line(&ps, pc, t, lineno)) {
            ok = false;
            break;
         }
      }
   }

   free(buf);

   if (!ok) {
      free_parse_state(&ps);
      return false;
   }

   if (util_dynarray_num_elements(&ps.create_infos,
                                  struct ac_spm_counter_create_info) == 0) {
      fprintf(stderr,
              "ac/spm-config: '%s' contains no counters\n", path);
      free_parse_state(&ps);
      return false;
   }

   struct ac_spm_user_config *cfg = CALLOC(1, sizeof(*cfg));
   if (!cfg) {
      free_parse_state(&ps);
      return false;
   }

   /* Trim the dynarrays so their capacity matches their size, then
    * transfer ownership of the backing buffers into cfg. The dynarrays
    * use libc realloc() (no mem_ctx), so the resulting pointers are
    * compatible with the FREE() calls in ac_spm_user_config_destroy().
    */
   util_dynarray_trim(&ps.descrs);
   util_dynarray_trim(&ps.create_infos);
   util_dynarray_trim(&ps.counters);
   util_dynarray_trim(&ps.groups);

   cfg->descrs = ps.descrs.data;
   cfg->create_infos = ps.create_infos.data;
   cfg->num_create_infos = util_dynarray_num_elements(
      &ps.create_infos, struct ac_spm_counter_create_info);
   cfg->counters = ps.counters.data;
   cfg->num_counters = util_dynarray_num_elements(
      &ps.counters, struct ac_spm_user_counter);
   cfg->groups = ps.groups.data;
   cfg->num_groups = util_dynarray_num_elements(
      &ps.groups, struct ac_spm_user_group);
   memset(&ps, 0, sizeof(ps));

   if (!finalize_descriptors(cfg)) {
      ac_spm_user_config_destroy(cfg);
      return false;
   }

   *out = cfg;
   return true;
}

void
ac_spm_user_config_destroy(struct ac_spm_user_config *config)
{
   if (!config)
      return;
   /* Names are strdup()'d, so release them with the libc free(). */
   for (uint32_t i = 0; i < config->num_counters; i++)
      free(config->counters[i].name);
   for (uint32_t i = 0; i < config->num_groups; i++)
      free(config->groups[i].name);
   FREE(config->counters);
   FREE(config->groups);
   FREE(config->descrs);
   FREE(config->create_infos);
   FREE(config);
}
