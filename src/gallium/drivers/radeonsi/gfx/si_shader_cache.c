/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "si_gfx.h"
#include "si_pipe.h"
#include "si_shader.h"

#include "util/blob.h"
#include "util/crc32.h"
#include "util/disk_cache.h"
#include "util/hash_table.h"
#include "nir.h"
#include "nir_serialize.h"

/**
 * Return the IR key for the shader cache.
 */
void si_get_ir_cache_key(struct si_shader_selector *sel, bool ngg, bool es,
                         unsigned wave_size, unsigned char ir_blake3_cache_key[BLAKE3_KEY_LEN])
{
   struct blob blob = {};
   unsigned ir_size;
   void *ir_binary;

   if (sel->nir_binary) {
      ir_binary = sel->nir_binary;
      ir_size = sel->nir_size;
   } else {
      assert(sel->nir);

      blob_init(&blob);
      /* Keep debug info if NIR debug prints are in use. */
      nir_serialize(&blob, sel->nir, NIR_DEBUG(PRINT) == 0);
      ir_binary = blob.data;
      ir_size = blob.size;
   }

   /* These settings affect the compilation, but they are not derived
    * from the input shader IR.
    */
   unsigned shader_variant_flags = 0;

   if (ngg)
      shader_variant_flags |= 1 << 0;
   /* bit gap */
   if (wave_size == 32)
      shader_variant_flags |= 1 << 2;
   /* bit gap */
   /* use_ngg_culling disables NGG passthrough for non-culling shaders to reduce context
    * rolls, which can be changed with AMD_DEBUG=nonggc or AMD_DEBUG=nggc.
    */
   if (sel->screen->use_ngg_culling)
      shader_variant_flags |= 1 << 4;
   if (sel->screen->record_llvm_ir)
      shader_variant_flags |= 1 << 5;
   if (sel->screen->info.has_image_opcodes)
      shader_variant_flags |= 1 << 6;
   if (sel->screen->options.no_infinite_interp)
      shader_variant_flags |= 1 << 7;
   if (sel->screen->options.clamp_div_by_zero)
      shader_variant_flags |= 1 << 8;
   if ((sel->stage == MESA_SHADER_VERTEX ||
        sel->stage == MESA_SHADER_TESS_EVAL ||
        sel->stage == MESA_SHADER_GEOMETRY) &&
       !es &&
       sel->screen->options.vrs2x2)
      shader_variant_flags |= 1 << 10;
   if (sel->screen->options.inline_uniforms)
      shader_variant_flags |= 1 << 11;
   if (sel->screen->options.clear_lds)
      shader_variant_flags |= 1 << 12;

   blake3_hasher ctx;
   _mesa_blake3_init(&ctx);
   _mesa_blake3_update(&ctx, &shader_variant_flags, 4);
   _mesa_blake3_update(&ctx, ir_binary, ir_size);
   _mesa_blake3_final(&ctx, ir_blake3_cache_key);

   if (ir_binary == blob.data)
      blob_finish(&blob);
}

/** Copy "data" to "ptr" and return the next dword following copied data. */
static uint32_t *write_data(uint32_t *ptr, const void *data, unsigned size)
{
   /* data may be NULL if size == 0 */
   if (size)
      memcpy(ptr, data, size);
   ptr += DIV_ROUND_UP(size, 4);
   return ptr;
}

/** Read data from "ptr". Return the next dword following the data. */
static uint32_t *read_data(uint32_t *ptr, void *data, unsigned size)
{
   memcpy(data, ptr, size);
   ptr += DIV_ROUND_UP(size, 4);
   return ptr;
}

/**
 * Write the size as uint followed by the data. Return the next dword
 * following the copied data.
 */
static uint32_t *write_chunk(uint32_t *ptr, const void *data, unsigned size)
{
   *ptr++ = size;
   return write_data(ptr, data, size);
}

/**
 * Read the size as uint followed by the data. Return both via parameters.
 * Return the next dword following the data.
 */
static uint32_t *read_chunk(uint32_t *ptr, void **data, unsigned *size)
{
   *size = *ptr++;
   assert(*data == NULL);
   if (!*size)
      return ptr;
   *data = malloc(*size);
   return read_data(ptr, *data, *size);
}

struct si_shader_blob_head {
   uint32_t size;
   uint32_t type;
   uint32_t crc32;
};

/**
 * Return the shader binary in a buffer.
 */
static uint32_t *si_get_shader_binary(struct si_shader *shader)
{
   /* There is always a size of data followed by the data itself. */
   unsigned llvm_ir_size =
      shader->binary.llvm_ir_string ? strlen(shader->binary.llvm_ir_string) + 1 : 0;

   /* Refuse to allocate overly large buffers and guard against integer
    * overflow. */
   if (shader->binary.code_size > UINT_MAX / 4 || llvm_ir_size > UINT_MAX / 4 ||
       shader->binary.num_symbols > UINT_MAX / 32)
      return NULL;

   unsigned size = sizeof(struct si_shader_blob_head) +
                   align(sizeof(shader->config), 4) +
                   align(sizeof(shader->info), 4) +
                   4 + 4 + align(shader->binary.code_size, 4) +
                   4 + shader->binary.num_symbols * 8 +
                   4 + align(llvm_ir_size, 4) +
                   4 + align(shader->binary.disasm_size, 4);
   uint32_t *buffer = (uint32_t*)CALLOC(1, size);
   if (!buffer)
      return NULL;

   struct si_shader_blob_head *head = (struct si_shader_blob_head *)buffer;
   head->type = shader->binary.type;
   head->size = size;

   uint32_t *data = buffer + sizeof(*head) / 4;
   uint32_t *ptr = data;

   ptr = write_data(ptr, &shader->config, sizeof(shader->config));
   ptr = write_data(ptr, &shader->info, sizeof(shader->info));
   ptr = write_data(ptr, &shader->binary.exec_size, 4);
   ptr = write_chunk(ptr, shader->binary.code_buffer, shader->binary.code_size);
   ptr = write_chunk(ptr, shader->binary.symbols, shader->binary.num_symbols * 8);
   ptr = write_chunk(ptr, shader->binary.llvm_ir_string, llvm_ir_size);
   ptr = write_chunk(ptr, shader->binary.disasm_string, shader->binary.disasm_size);
   assert((char *)ptr - (char *)buffer == (ptrdiff_t)size);

   /* Compute CRC32. */
   head->crc32 = util_hash_crc32(data, size - sizeof(*head));

   return buffer;
}

static bool si_load_shader_binary(struct si_shader *shader, void *binary)
{
   struct si_shader_blob_head *head = (struct si_shader_blob_head *)binary;
   unsigned chunk_size;
   unsigned code_size;

   uint32_t *ptr = (uint32_t *)binary + sizeof(*head) / 4;
   if (util_hash_crc32(ptr, head->size - sizeof(*head)) != head->crc32) {
      mesa_loge("binary shader has invalid CRC32");
      return false;
   }

   shader->binary.type = (enum si_shader_binary_type)head->type;
   ptr = read_data(ptr, &shader->config, sizeof(shader->config));
   ptr = read_data(ptr, &shader->info, sizeof(shader->info));
   ptr = read_data(ptr, &shader->binary.exec_size, 4);
   ptr = read_chunk(ptr, (void **)&shader->binary.code_buffer, &code_size);
   shader->binary.code_size = code_size;
   ptr = read_chunk(ptr, (void **)&shader->binary.symbols, &chunk_size);
   shader->binary.num_symbols = chunk_size / 8;
   ptr = read_chunk(ptr, (void **)&shader->binary.llvm_ir_string, &chunk_size);
   ptr = read_chunk(ptr, (void **)&shader->binary.disasm_string, &chunk_size);
   shader->binary.disasm_size = chunk_size;

   if (!shader->is_gs_copy_shader &&
       shader->selector->stage == MESA_SHADER_GEOMETRY && !shader->key.ge.as_ngg) {
      shader->gs_copy_shader = CALLOC_STRUCT(si_shader);
      if (!shader->gs_copy_shader)
         return false;

      shader->gs_copy_shader->is_gs_copy_shader = true;

      if (!si_load_shader_binary(shader->gs_copy_shader, (uint8_t*)binary + head->size)) {
         FREE(shader->gs_copy_shader);
         shader->gs_copy_shader = NULL;
         return false;
      }

      util_queue_fence_init(&shader->gs_copy_shader->ready);
      shader->gs_copy_shader->selector = shader->selector;
      shader->gs_copy_shader->is_gs_copy_shader = true;
      shader->gs_copy_shader->wave_size =
         si_determine_wave_size(shader->selector->screen, shader->gs_copy_shader);

      si_shader_binary_upload(shader->selector->screen, shader->gs_copy_shader, 0);
   }

   return true;
}

/**
 * Insert a shader into the cache. It's assumed the shader is not in the cache.
 * Use si_shader_cache_load_shader before calling this.
 */
void si_shader_cache_insert_shader(struct si_screen *sscreen, unsigned char ir_blake3_cache_key[BLAKE3_KEY_LEN],
                                   struct si_shader *shader, bool insert_into_disk_cache)
{
   uint32_t *hw_binary;
   struct hash_entry *entry;
   uint8_t key[CACHE_KEY_SIZE];
   bool memory_cache_full = sscreen->shader_cache_size >= sscreen->shader_cache_max_size;

   if (!insert_into_disk_cache && memory_cache_full)
      return;

   entry = _mesa_hash_table_search(sscreen->shader_cache, ir_blake3_cache_key);
   if (entry)
      return; /* already added */

   hw_binary = si_get_shader_binary(shader);
   if (!hw_binary)
      return;

   unsigned size = *hw_binary;

   if (shader->selector->stage == MESA_SHADER_GEOMETRY && !shader->key.ge.as_ngg) {
      uint32_t *gs_copy_binary = si_get_shader_binary(shader->gs_copy_shader);
      if (!gs_copy_binary) {
         FREE(hw_binary);
         return;
      }

      /* Combine both binaries. */
      size += *gs_copy_binary;
      uint32_t *combined_binary = (uint32_t*)MALLOC(size);
      if (!combined_binary) {
         FREE(hw_binary);
         FREE(gs_copy_binary);
         return;
      }

      memcpy(combined_binary, hw_binary, *hw_binary);
      memcpy(combined_binary + *hw_binary / 4, gs_copy_binary, *gs_copy_binary);
      FREE(hw_binary);
      FREE(gs_copy_binary);
      hw_binary = combined_binary;
   }

   if (!memory_cache_full) {
      if (_mesa_hash_table_insert(sscreen->shader_cache,
                                  mem_dup(ir_blake3_cache_key, 20),
                                  hw_binary) == NULL) {
          FREE(hw_binary);
          return;
      }

      sscreen->shader_cache_size += size;
   }

   if (sscreen->disk_shader_cache && insert_into_disk_cache) {
      disk_cache_compute_key(sscreen->disk_shader_cache, ir_blake3_cache_key, 20, key);
      disk_cache_put(sscreen->disk_shader_cache, key, hw_binary, size, NULL);
   }

   if (memory_cache_full)
      FREE(hw_binary);
}

bool si_shader_cache_load_shader(struct si_screen *sscreen, unsigned char ir_blake3_cache_key[BLAKE3_KEY_LEN],
                                 struct si_shader *shader)
{
   struct hash_entry *entry = _mesa_hash_table_search(sscreen->shader_cache, ir_blake3_cache_key);

   if (entry) {
      if (si_load_shader_binary(shader, entry->data)) {
         p_atomic_inc(&sscreen->num_memory_shader_cache_hits);
         return true;
      }
   }
   p_atomic_inc(&sscreen->num_memory_shader_cache_misses);

   if (!sscreen->disk_shader_cache)
      return false;

   unsigned char blake3[CACHE_KEY_SIZE];
   disk_cache_compute_key(sscreen->disk_shader_cache, ir_blake3_cache_key, 20, blake3);

   size_t total_size;
   uint32_t *buffer = (uint32_t*)disk_cache_get(sscreen->disk_shader_cache, blake3, &total_size);
   if (buffer) {
      unsigned size = *buffer;
      unsigned gs_copy_binary_size = 0;

      /* The GS copy shader binary is after the GS binary. */
      if (shader->selector->stage == MESA_SHADER_GEOMETRY && !shader->key.ge.as_ngg)
         gs_copy_binary_size = buffer[size / 4];

      if (total_size >= sizeof(uint32_t) && size + gs_copy_binary_size == total_size) {
         if (si_load_shader_binary(shader, buffer)) {
            free(buffer);
            si_shader_cache_insert_shader(sscreen, ir_blake3_cache_key, shader, false);
            p_atomic_inc(&sscreen->num_disk_shader_cache_hits);
            return true;
         }
      } else {
         /* Something has gone wrong discard the item from the cache and
          * rebuild/link from source.
          */
         assert(!"Invalid radeonsi shader disk cache item!");
         disk_cache_remove(sscreen->disk_shader_cache, blake3);
      }
   }

   free(buffer);
   p_atomic_inc(&sscreen->num_disk_shader_cache_misses);
   return false;
}

static uint32_t si_shader_cache_key_hash(const void *key)
{
   /* Take the first dword of BLAKE3. */
   return *(uint32_t *)key;
}

static bool si_shader_cache_key_equals(const void *a, const void *b)
{
   /* Compare BLAKE3s. */
   return memcmp(a, b, 20) == 0;
}

static void si_destroy_shader_cache_entry(struct hash_entry *entry)
{
   FREE((void *)entry->key);
   FREE(entry->data);
}

bool si_init_shader_cache(struct si_screen *sscreen)
{
   (void)simple_mtx_init(&sscreen->shader_cache_mutex, mtx_plain);
   sscreen->shader_cache =
      _mesa_hash_table_create(NULL, si_shader_cache_key_hash, si_shader_cache_key_equals);
   sscreen->shader_cache_size = 0;
   /* Maximum size: 64MB on 32 bits, 1GB else */
   sscreen->shader_cache_max_size = ((sizeof(void *) == 4) ? 64 : 1024) * 1024 * 1024;

   return sscreen->shader_cache != NULL;
}

void si_destroy_shader_cache(struct si_screen *sscreen)
{
   if (sscreen->shader_cache)
      _mesa_hash_table_destroy(sscreen->shader_cache, si_destroy_shader_cache_entry);
   simple_mtx_destroy(&sscreen->shader_cache_mutex);
}

void si_init_screen_live_shader_cache(struct si_screen *sscreen)
{
   util_live_shader_cache_init(&sscreen->live_shader_cache, si_create_shader_selector,
                               si_destroy_shader_selector);
}
