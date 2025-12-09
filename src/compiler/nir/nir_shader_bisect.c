#include "nir.h"

#include "util/hex.h"
#include "util/log.h"
#include "util/mesa-blake3.h"
#include "util/u_call_once.h"

/** @file
 * Shader bisect support.
 *
 * Simply use nir_shader_bisect_select() to control some bad behavior you've
 * identified (calling a shader pass or executing some bad part of a
 * shader_pass), then run your application under nir_shader_bisect.py to be
 * interactively guided through bisecting down to which NIR shader in your
 * program is being badly affected by the code in question.
 *
 * Note that doing this requires (unless someone rigs up cache key handling)
 * MESA_SHADER_DISABLE_CACHE=1, which is also set by nir_shader_bisect.py.
 */

/* These are expected to be hex dumps of blake3 bytes, no space and no leading
 * '0x'
 */
static const char *nir_shader_bisect_lo = NULL;
static const char *nir_shader_bisect_hi = NULL;

DEBUG_GET_ONCE_OPTION(nir_shader_bisect_lo, "NIR_SHADER_BISECT_LO", NULL);
DEBUG_GET_ONCE_OPTION(nir_shader_bisect_hi, "NIR_SHADER_BISECT_HI", NULL);

static void
nir_shader_bisect_init(void)
{
   nir_shader_bisect_lo = debug_get_option_nir_shader_bisect_lo();
   nir_shader_bisect_hi = debug_get_option_nir_shader_bisect_hi();
   if (nir_shader_bisect_lo)
      assert(strlen(nir_shader_bisect_lo) + 1 == BLAKE3_HEX_LEN);
   if (nir_shader_bisect_hi)
      assert(strlen(nir_shader_bisect_hi) + 1 == BLAKE3_HEX_LEN);
}

bool
nir_shader_bisect_select(nir_shader *s)
{
   static once_flag once = ONCE_FLAG_INIT;
   call_once(&once, nir_shader_bisect_init);

   if (!nir_shader_bisect_lo && !nir_shader_bisect_hi)
      return false;

   char id[BLAKE3_HEX_LEN];
   _mesa_blake3_format(id, s->info.source_blake3);

   if (nir_shader_bisect_lo && strcmp(id, nir_shader_bisect_lo) < 0)
      return false;

   if (nir_shader_bisect_hi && strcmp(id, nir_shader_bisect_hi) > 0)
      return false;

   uint32_t u32[BLAKE3_OUT_LEN32] = { 0 };
   for (unsigned i = 0; i < BLAKE3_OUT_LEN; i++)
      u32[i / 4] |= (uint32_t)s->info.source_blake3[i] << ((i % 4) * 8);

   /* Provide feedback of both the source_blake3 and the blake3_format id to the
    * script of what shaders got affected, so it can bisect on the set of
    * shaders remaining for the env vars, and print out a final blake3 when we
    * get down to 1 shader.
    */
   mesa_logi("NIR bisect selected source_blake3: {0x%08x, 0x%08x, 0x%08x, 0x%08x, 0x%08x, 0x%08x, 0x%08x, 0x%08x} (%s)\n",
             u32[0], u32[1], u32[2], u32[3],
             u32[4], u32[5], u32[6], u32[7], id);

   return true;
}
