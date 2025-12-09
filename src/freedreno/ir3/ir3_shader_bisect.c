#include "ir3_shader.h"

#include "util/hex.h"

static const char *ir3_shader_bisect_dump_ids_path = NULL;
static const char *ir3_shader_bisect_lo = NULL;
static const char *ir3_shader_bisect_hi = NULL;
static const char *ir3_shader_bisect_disasm = NULL;

DEBUG_GET_ONCE_OPTION(ir3_shader_bisect_dump_ids_path,
                      "IR3_SHADER_BISECT_DUMP_IDS_PATH", NULL);
DEBUG_GET_ONCE_OPTION(ir3_shader_bisect_lo, "IR3_SHADER_BISECT_LO", NULL);
DEBUG_GET_ONCE_OPTION(ir3_shader_bisect_hi, "IR3_SHADER_BISECT_HI", NULL);
DEBUG_GET_ONCE_OPTION(ir3_shader_bisect_disasm, "IR3_SHADER_BISECT_DISASM",
                      NULL);

#define BISECT_ID_SIZE (2 * (CACHE_KEY_SIZE + 1) + 1)

void
ir3_shader_bisect_init()
{
   ir3_shader_bisect_dump_ids_path =
      __normal_user() ? debug_get_option_ir3_shader_bisect_dump_ids_path()
                      : NULL;
   ir3_shader_bisect_lo = debug_get_option_ir3_shader_bisect_lo();
   ir3_shader_bisect_hi = debug_get_option_ir3_shader_bisect_hi();
   ir3_shader_bisect_disasm = debug_get_option_ir3_shader_bisect_disasm();

   if (ir3_shader_bisect_dump_ids_path) {
      FILE *f = fopen(ir3_shader_bisect_dump_ids_path, "w");
      assert(f && "Failed to open ir3_shader_bisect_dump_ids_path");
      fclose(f);
   }
}

bool
ir3_shader_bisect_need_shader_key()
{
   return ir3_shader_bisect_dump_ids_path || ir3_shader_bisect_lo ||
          ir3_shader_bisect_hi || ir3_shader_bisect_disasm;
}

static void
get_shader_bisect_id(struct ir3_shader_variant *v, char id[BISECT_ID_SIZE])
{
   uint8_t id_bin[sizeof(v->shader->cache_key) + 1];
   memcpy(id_bin, v->shader->cache_key, sizeof(v->shader->cache_key));
   id_bin[sizeof(v->shader->cache_key)] = v->id;
   mesa_bytes_to_hex(id, id_bin, sizeof(id_bin));
}

void
ir3_shader_bisect_dump_id(struct ir3_shader_variant *v)
{
   if (!ir3_shader_bisect_dump_ids_path) {
      return;
   }

   FILE *f = fopen(ir3_shader_bisect_dump_ids_path, "a");
   assert(f);

   char id[BISECT_ID_SIZE];
   get_shader_bisect_id(v, id);
   fprintf(f, "%s\n", id);
}

bool
ir3_shader_bisect_select(struct ir3_shader_variant *v)
{
   if (!ir3_shader_bisect_lo && !ir3_shader_bisect_hi) {
      return false;
   }

   char id[BISECT_ID_SIZE];
   get_shader_bisect_id(v, id);

   if (ir3_shader_bisect_lo && strcmp(id, ir3_shader_bisect_lo) < 0) {
      return false;
   }

   if (ir3_shader_bisect_hi && strcmp(id, ir3_shader_bisect_hi) > 0) {
      return false;
   }

   return true;
}

bool
ir3_shader_bisect_disasm_select(struct ir3_shader_variant *v)
{
   if (!ir3_shader_bisect_disasm) {
      return false;
   }

   char id[BISECT_ID_SIZE];
   get_shader_bisect_id(v, id);
   return strcmp(ir3_shader_bisect_disasm, id) == 0;
}
