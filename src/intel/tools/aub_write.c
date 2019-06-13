/*
 * Copyright © 2015 Intel Corporation
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

#include "aub_write.h"

#include <inttypes.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "drm-uapi/i915_drm.h"
#include "intel_aub.h"
#include "gen_context.h"

#ifndef ALIGN
#define ALIGN(x, y) (((x) + (y)-1) & ~((y)-1))
#endif

#define MI_BATCH_NON_SECURE_I965 (1 << 8)

#define min(a, b) ({                            \
         __typeof(a) _a = (a);                  \
         __typeof(b) _b = (b);                  \
         _a < _b ? _a : _b;                     \
      })

#define max(a, b) ({                            \
         __typeof(a) _a = (a);                  \
         __typeof(b) _b = (b);                  \
         _a > _b ? _a : _b;                     \
      })

static void
mem_trace_memory_write_header_out(struct aub_file *aub, uint64_t addr,
                                  uint32_t len, uint32_t addr_space,
                                  const char *desc);

static void __attribute__ ((format(__printf__, 2, 3)))
fail_if(int cond, const char *format, ...)
{
   va_list args;

   if (!cond)
      return;

   va_start(args, format);
   vfprintf(stderr, format, args);
   va_end(args);

   raise(SIGTRAP);
}

static inline uint32_t
align_u32(uint32_t v, uint32_t a)
{
   return (v + a - 1) & ~(a - 1);
}

static void
aub_ppgtt_table_finish(struct aub_ppgtt_table *table, int level)
{
   if (level == 1)
      return;

   for (unsigned i = 0; i < ARRAY_SIZE(table->subtables); i++) {
      if (table->subtables[i]) {
         aub_ppgtt_table_finish(table->subtables[i], level - 1);
         free(table->subtables[i]);
      }
   }
}

static void
data_out(struct aub_file *aub, const void *data, size_t size)
{
   if (size == 0)
      return;

   fail_if(fwrite(data, 1, size, aub->file) == 0,
           "Writing to output failed\n");
}

static void
dword_out(struct aub_file *aub, uint32_t data)
{
   data_out(aub, &data, sizeof(data));
}

static void
write_execlists_header(struct aub_file *aub, const char *name)
{
   char app_name[8 * 4];
   int app_name_len, dwords;

   app_name_len =
      snprintf(app_name, sizeof(app_name), "PCI-ID=0x%X %s",
               aub->pci_id, name);
   app_name_len = ALIGN(app_name_len, sizeof(uint32_t));

   dwords = 5 + app_name_len / sizeof(uint32_t);
   dword_out(aub, CMD_MEM_TRACE_VERSION | (dwords - 1));
   dword_out(aub, AUB_MEM_TRACE_VERSION_FILE_VERSION);
   dword_out(aub, aub->devinfo.simulator_id << AUB_MEM_TRACE_VERSION_DEVICE_SHIFT);
   dword_out(aub, 0);      /* version */
   dword_out(aub, 0);      /* version */
   data_out(aub, app_name, app_name_len);
}

static void
write_legacy_header(struct aub_file *aub, const char *name)
{
   char app_name[8 * 4];
   char comment[16];
   int comment_len, comment_dwords, dwords;

   comment_len = snprintf(comment, sizeof(comment), "PCI-ID=0x%x", aub->pci_id);
   comment_dwords = ((comment_len + 3) / 4);

   /* Start with a (required) version packet. */
   dwords = 13 + comment_dwords;
   dword_out(aub, CMD_AUB_HEADER | (dwords - 2));
   dword_out(aub, (4 << AUB_HEADER_MAJOR_SHIFT) |
                  (0 << AUB_HEADER_MINOR_SHIFT));

   /* Next comes a 32-byte application name. */
   strncpy(app_name, name, sizeof(app_name));
   app_name[sizeof(app_name) - 1] = 0;
   data_out(aub, app_name, sizeof(app_name));

   dword_out(aub, 0); /* timestamp */
   dword_out(aub, 0); /* timestamp */
   dword_out(aub, comment_len);
   data_out(aub, comment, comment_dwords * 4);
}


static void
aub_write_header(struct aub_file *aub, const char *app_name)
{
   if (aub_use_execlists(aub))
      write_execlists_header(aub, app_name);
   else
      write_legacy_header(aub, app_name);
}

void
aub_file_init(struct aub_file *aub, FILE *file, FILE *debug, uint16_t pci_id, const char *app_name)
{
   memset(aub, 0, sizeof(*aub));

   aub->verbose_log_file = debug;
   aub->file = file;
   aub->pci_id = pci_id;
   fail_if(!gen_get_device_info(pci_id, &aub->devinfo),
           "failed to identify chipset=0x%x\n", pci_id);
   aub->addr_bits = aub->devinfo.gen >= 8 ? 48 : 32;

   aub_write_header(aub, app_name);

   aub->phys_addrs_allocator = 0;
   aub->pml4.phys_addr = aub->phys_addrs_allocator++ << 12;

   mem_trace_memory_write_header_out(aub, 0,
                                     GEN8_PTE_SIZE,
                                     AUB_MEM_TRACE_MEMORY_ADDRESS_SPACE_GGTT_ENTRY,
                                     "GGTT PT");
   dword_out(aub, 1);
   dword_out(aub, 0);
}

void
aub_file_finish(struct aub_file *aub)
{
   aub_ppgtt_table_finish(&aub->pml4, 4);
   fclose(aub->file);
}

uint32_t
aub_gtt_size(struct aub_file *aub)
{
   return NUM_PT_ENTRIES * (aub->addr_bits > 32 ? GEN8_PTE_SIZE : PTE_SIZE);
}

static void
mem_trace_memory_write_header_out(struct aub_file *aub, uint64_t addr,
                                  uint32_t len, uint32_t addr_space,
                                  const char *desc)
{
   uint32_t dwords = ALIGN(len, sizeof(uint32_t)) / sizeof(uint32_t);

   if (aub->verbose_log_file) {
      fprintf(aub->verbose_log_file,
              "  MEM WRITE (0x%016" PRIx64 "-0x%016" PRIx64 ") %s\n",
              addr, addr + len, desc);
   }

   dword_out(aub, CMD_MEM_TRACE_MEMORY_WRITE | (5 + dwords - 1));
   dword_out(aub, addr & 0xFFFFFFFF);   /* addr lo */
   dword_out(aub, addr >> 32);   /* addr hi */
   dword_out(aub, addr_space);   /* gtt */
   dword_out(aub, len);
}

static void
register_write_out(struct aub_file *aub, uint32_t addr, uint32_t value)
{
   uint32_t dwords = 1;

   if (aub->verbose_log_file) {
      fprintf(aub->verbose_log_file,
              "  MMIO WRITE (0x%08x = 0x%08x)\n", addr, value);
   }

   dword_out(aub, CMD_MEM_TRACE_REGISTER_WRITE | (5 + dwords - 1));
   dword_out(aub, addr);
   dword_out(aub, AUB_MEM_TRACE_REGISTER_SIZE_DWORD |
                  AUB_MEM_TRACE_REGISTER_SPACE_MMIO);
   dword_out(aub, 0xFFFFFFFF);   /* mask lo */
   dword_out(aub, 0x00000000);   /* mask hi */
   dword_out(aub, value);
}

static void
populate_ppgtt_table(struct aub_file *aub, struct aub_ppgtt_table *table,
                     int start, int end, int level)
{
   uint64_t entries[512] = {0};
   int dirty_start = 512, dirty_end = 0;

   if (aub->verbose_log_file) {
      fprintf(aub->verbose_log_file,
              "  PPGTT (0x%016" PRIx64 "), lvl %d, start: %x, end: %x\n",
              table->phys_addr, level, start, end);
   }

   for (int i = start; i <= end; i++) {
      if (!table->subtables[i]) {
         dirty_start = min(dirty_start, i);
         dirty_end = max(dirty_end, i);
         if (level == 1) {
            table->subtables[i] =
               (void *)(aub->phys_addrs_allocator++ << 12);
            if (aub->verbose_log_file) {
               fprintf(aub->verbose_log_file,
                       "   Adding entry: %x, phys_addr: 0x%016" PRIx64 "\n",
                       i, (uint64_t)table->subtables[i]);
            }
         } else {
            table->subtables[i] =
               calloc(1, sizeof(struct aub_ppgtt_table));
            table->subtables[i]->phys_addr =
               aub->phys_addrs_allocator++ << 12;
            if (aub->verbose_log_file) {
               fprintf(aub->verbose_log_file,
                       "   Adding entry: %x, phys_addr: 0x%016" PRIx64 "\n",
                       i, table->subtables[i]->phys_addr);
            }
         }
      }
      entries[i] = 3 /* read/write | present */ |
         (level == 1 ? (uint64_t)table->subtables[i] :
          table->subtables[i]->phys_addr);
   }

   if (dirty_start <= dirty_end) {
      uint64_t write_addr = table->phys_addr + dirty_start *
         sizeof(uint64_t);
      uint64_t write_size = (dirty_end - dirty_start + 1) *
         sizeof(uint64_t);
      mem_trace_memory_write_header_out(aub, write_addr, write_size,
                                        AUB_MEM_TRACE_MEMORY_ADDRESS_SPACE_PHYSICAL,
                                        "PPGTT update");
      data_out(aub, entries + dirty_start, write_size);
   }
}

void
aub_map_ppgtt(struct aub_file *aub, uint64_t start, uint64_t size)
{
   uint64_t l4_start = start & 0xff8000000000;
   uint64_t l4_end = ((start + size - 1) | 0x007fffffffff) & 0xffffffffffff;

#define L4_index(addr) (((addr) >> 39) & 0x1ff)
#define L3_index(addr) (((addr) >> 30) & 0x1ff)
#define L2_index(addr) (((addr) >> 21) & 0x1ff)
#define L1_index(addr) (((addr) >> 12) & 0x1ff)

#define L3_table(addr) (aub->pml4.subtables[L4_index(addr)])
#define L2_table(addr) (L3_table(addr)->subtables[L3_index(addr)])
#define L1_table(addr) (L2_table(addr)->subtables[L2_index(addr)])

   if (aub->verbose_log_file) {
      fprintf(aub->verbose_log_file,
              " Mapping PPGTT address: 0x%" PRIx64 ", size: %" PRIu64"\n",
              start, size);
   }

   populate_ppgtt_table(aub, &aub->pml4, L4_index(l4_start), L4_index(l4_end), 4);

   for (uint64_t l4 = l4_start; l4 < l4_end; l4 += (1ULL << 39)) {
      uint64_t l3_start = max(l4, start & 0xffffc0000000);
      uint64_t l3_end = min(l4 + (1ULL << 39) - 1,
                            ((start + size - 1) | 0x00003fffffff) & 0xffffffffffff);
      uint64_t l3_start_idx = L3_index(l3_start);
      uint64_t l3_end_idx = L3_index(l3_end);

      populate_ppgtt_table(aub, L3_table(l4), l3_start_idx, l3_end_idx, 3);

      for (uint64_t l3 = l3_start; l3 < l3_end; l3 += (1ULL << 30)) {
         uint64_t l2_start = max(l3, start & 0xffffffe00000);
         uint64_t l2_end = min(l3 + (1ULL << 30) - 1,
                               ((start + size - 1) | 0x0000001fffff) & 0xffffffffffff);
         uint64_t l2_start_idx = L2_index(l2_start);
         uint64_t l2_end_idx = L2_index(l2_end);

         populate_ppgtt_table(aub, L2_table(l3), l2_start_idx, l2_end_idx, 2);

         for (uint64_t l2 = l2_start; l2 < l2_end; l2 += (1ULL << 21)) {
            uint64_t l1_start = max(l2, start & 0xfffffffff000);
            uint64_t l1_end = min(l2 + (1ULL << 21) - 1,
                                  ((start + size - 1) | 0x000000000fff) & 0xffffffffffff);
            uint64_t l1_start_idx = L1_index(l1_start);
            uint64_t l1_end_idx = L1_index(l1_end);

            populate_ppgtt_table(aub, L1_table(l2), l1_start_idx, l1_end_idx, 1);
         }
      }
   }
}

static uint64_t
ppgtt_lookup(struct aub_file *aub, uint64_t ppgtt_addr)
{
   return (uint64_t)L1_table(ppgtt_addr)->subtables[L1_index(ppgtt_addr)];
}

static const struct engine {
   const char *name;
   enum drm_i915_gem_engine_class engine_class;
   uint32_t hw_class;
   uint32_t elsp_reg;
   uint32_t elsq_reg;
   uint32_t status_reg;
   uint32_t control_reg;
} engines[] = {
   [I915_ENGINE_CLASS_RENDER] = {
      .name = "RENDER",
      .engine_class = I915_ENGINE_CLASS_RENDER,
      .hw_class = 1,
      .elsp_reg = EXECLIST_SUBMITPORT_RCSUNIT,
      .elsq_reg = EXECLIST_SQ_CONTENTS0_RCSUNIT,
      .status_reg = EXECLIST_STATUS_RCSUNIT,
      .control_reg = EXECLIST_CONTROL_RCSUNIT,
   },
   [I915_ENGINE_CLASS_VIDEO] = {
      .name = "VIDEO",
      .engine_class = I915_ENGINE_CLASS_VIDEO,
      .hw_class = 3,
      .elsp_reg = EXECLIST_SUBMITPORT_VCSUNIT0,
      .elsq_reg = EXECLIST_SQ_CONTENTS0_VCSUNIT0,
      .status_reg = EXECLIST_STATUS_VCSUNIT0,
      .control_reg = EXECLIST_CONTROL_VCSUNIT0,
   },
   [I915_ENGINE_CLASS_COPY] = {
      .name = "BLITTER",
      .engine_class = I915_ENGINE_CLASS_COPY,
      .hw_class = 2,
      .elsp_reg = EXECLIST_SUBMITPORT_BCSUNIT,
      .elsq_reg = EXECLIST_SQ_CONTENTS0_BCSUNIT,
      .status_reg = EXECLIST_STATUS_BCSUNIT,
      .control_reg = EXECLIST_CONTROL_BCSUNIT,
   },
};

static const struct engine *
engine_from_engine_class(enum drm_i915_gem_engine_class engine_class)
{
   switch (engine_class) {
   case I915_ENGINE_CLASS_RENDER:
   case I915_ENGINE_CLASS_COPY:
   case I915_ENGINE_CLASS_VIDEO:
      return &engines[engine_class];
   default:
      unreachable("unknown ring");
   }
}

static void
get_context_init(const struct gen_device_info *devinfo,
                 const struct gen_context_parameters *params,
                 enum drm_i915_gem_engine_class engine_class,
                 uint32_t *data,
                 uint32_t *size)
{
   static const gen_context_init_t gen8_contexts[] = {
      [I915_ENGINE_CLASS_RENDER] = gen8_render_context_init,
      [I915_ENGINE_CLASS_COPY] = gen8_blitter_context_init,
      [I915_ENGINE_CLASS_VIDEO] = gen8_video_context_init,
   };
   static const gen_context_init_t gen10_contexts[] = {
      [I915_ENGINE_CLASS_RENDER] = gen10_render_context_init,
      [I915_ENGINE_CLASS_COPY] = gen10_blitter_context_init,
      [I915_ENGINE_CLASS_VIDEO] = gen10_video_context_init,
   };

   assert(devinfo->gen >= 8);

   if (devinfo->gen <= 10)
      gen8_contexts[engine_class](params, data, size);
   else
      gen10_contexts[engine_class](params, data, size);
}

static uint32_t
write_engine_execlist_setup(struct aub_file *aub,
                            enum drm_i915_gem_engine_class engine_class)
{
   const struct engine *cs = engine_from_engine_class(engine_class);
   uint32_t context_size;

   get_context_init(&aub->devinfo, NULL, engine_class, NULL, &context_size);

   /* GGTT PT */
   uint64_t phys_addr = aub->phys_addrs_allocator << 12;
   uint32_t total_size = RING_SIZE + PPHWSP_SIZE + context_size;
   uint32_t ggtt_ptes = DIV_ROUND_UP(total_size, 4096);
   char name[80];

   aub->phys_addrs_allocator += ggtt_ptes;

   snprintf(name, sizeof(name), "%s GGTT PT", cs->name);
   mem_trace_memory_write_header_out(aub,
                                     sizeof(uint64_t) * (phys_addr >> 12),
                                     ggtt_ptes * GEN8_PTE_SIZE,
                                     AUB_MEM_TRACE_MEMORY_ADDRESS_SPACE_GGTT_ENTRY,
                                     name);
   for (uint32_t i = 0; i < ggtt_ptes; i++) {
      dword_out(aub, 1 + 0x1000 * i + phys_addr);
      dword_out(aub, 0);
   }

   /* RING */
   aub->engine_setup[engine_class].ring_addr = phys_addr;
   snprintf(name, sizeof(name), "%s RING", cs->name);
   mem_trace_memory_write_header_out(aub, phys_addr, RING_SIZE,
                                     AUB_MEM_TRACE_MEMORY_ADDRESS_SPACE_GGTT,
                                     name);
   for (uint32_t i = 0; i < RING_SIZE; i += sizeof(uint32_t))
      dword_out(aub, 0);
   phys_addr += RING_SIZE;

   /* PPHWSP */
   aub->engine_setup[engine_class].pphwsp_addr = phys_addr;
   aub->engine_setup[engine_class].descriptor = cs->hw_class | phys_addr | CONTEXT_FLAGS;
   snprintf(name, sizeof(name), "%s PPHWSP", cs->name);
   mem_trace_memory_write_header_out(aub, phys_addr,
                                     PPHWSP_SIZE + context_size,
                                     AUB_MEM_TRACE_MEMORY_ADDRESS_SPACE_GGTT,
                                     name);
   for (uint32_t i = 0; i < PPHWSP_SIZE; i += sizeof(uint32_t))
      dword_out(aub, 0);

   /* CONTEXT */
   struct gen_context_parameters params = {
      .ring_addr = aub->engine_setup[engine_class].ring_addr,
      .ring_size = RING_SIZE,
      .pml4_addr = aub->pml4.phys_addr,
   };
   uint32_t *context_data = calloc(1, context_size);
   get_context_init(&aub->devinfo, &params, engine_class, context_data, &context_size);
   data_out(aub, context_data, context_size);
   free(context_data);

   return total_size;
}

static void
write_execlists_default_setup(struct aub_file *aub)
{
   write_engine_execlist_setup(aub, I915_ENGINE_CLASS_RENDER);
   write_engine_execlist_setup(aub, I915_ENGINE_CLASS_COPY);
   write_engine_execlist_setup(aub, I915_ENGINE_CLASS_VIDEO);

   register_write_out(aub, HWS_PGA_RCSUNIT, aub->engine_setup[I915_ENGINE_CLASS_RENDER].pphwsp_addr);
   register_write_out(aub, HWS_PGA_VCSUNIT0, aub->engine_setup[I915_ENGINE_CLASS_VIDEO].pphwsp_addr);
   register_write_out(aub, HWS_PGA_BCSUNIT, aub->engine_setup[I915_ENGINE_CLASS_COPY].pphwsp_addr);

   register_write_out(aub, GFX_MODE_RCSUNIT, 0x80008000 /* execlist enable */);
   register_write_out(aub, GFX_MODE_VCSUNIT0, 0x80008000 /* execlist enable */);
   register_write_out(aub, GFX_MODE_BCSUNIT, 0x80008000 /* execlist enable */);
}

static void write_legacy_default_setup(struct aub_file *aub)
{
   uint32_t entry = 0x200003;

   /* Set up the GTT. The max we can handle is 64M */
   dword_out(aub, CMD_AUB_TRACE_HEADER_BLOCK |
                  ((aub->addr_bits > 32 ? 6 : 5) - 2));
   dword_out(aub, AUB_TRACE_MEMTYPE_GTT_ENTRY |
                  AUB_TRACE_TYPE_NOTYPE | AUB_TRACE_OP_DATA_WRITE);
   dword_out(aub, 0); /* subtype */
   dword_out(aub, 0); /* offset */
   dword_out(aub, aub_gtt_size(aub)); /* size */
   if (aub->addr_bits > 32)
      dword_out(aub, 0);
   for (uint32_t i = 0; i < NUM_PT_ENTRIES; i++) {
      dword_out(aub, entry + 0x1000 * i);
      if (aub->addr_bits > 32)
         dword_out(aub, 0);
   }
}

/**
 * Sets up a default GGTT/PPGTT address space and execlists context (when
 * supported).
 */
void
aub_write_default_setup(struct aub_file *aub)
{
   if (aub_use_execlists(aub))
      write_execlists_default_setup(aub);
   else
      write_legacy_default_setup(aub);

   aub->has_default_setup = true;
}

void
aub_write_ggtt(struct aub_file *aub, uint64_t virt_addr, uint64_t size, const void *data)
{
   if (aub->verbose_log_file) {
      fprintf(aub->verbose_log_file,
              " Writting GGTT address: 0x%" PRIx64 ", size: %" PRIu64"\n",
              virt_addr, size);
   }

   /* Default setup assumes a 1 to 1 mapping between physical and virtual GGTT
    * addresses. This is somewhat incompatible with the aub_write_ggtt()
    * function. In practice it doesn't matter as the GGTT writes are used to
    * replace the default setup and we've taken care to setup the PML4 as the
    * top of the GGTT.
    */
   assert(!aub->has_default_setup);

   /* Makes the code below a bit simpler. In practice all of the write we
    * receive from error2aub are page aligned.
    */
   assert(virt_addr % 4096 == 0);
   assert((aub->phys_addrs_allocator + size) < (1UL << 32));

   /* GGTT PT */
   uint32_t ggtt_ptes = DIV_ROUND_UP(size, 4096);
   uint64_t phys_addr = aub->phys_addrs_allocator << 12;
   aub->phys_addrs_allocator += ggtt_ptes;

   if (aub->verbose_log_file) {
      fprintf(aub->verbose_log_file,
              " Writting GGTT address: 0x%" PRIx64 ", size: %" PRIu64" phys_addr=0x%lx entries=%u\n",
              virt_addr, size, phys_addr, ggtt_ptes);
   }

   mem_trace_memory_write_header_out(aub,
                                     (virt_addr >> 12) * GEN8_PTE_SIZE,
                                     ggtt_ptes * GEN8_PTE_SIZE,
                                     AUB_MEM_TRACE_MEMORY_ADDRESS_SPACE_GGTT_ENTRY,
                                     "GGTT PT");
   for (uint32_t i = 0; i < ggtt_ptes; i++) {
      dword_out(aub, 1 + phys_addr + i * 4096);
      dword_out(aub, 0);
   }

   /* We write the GGTT buffer through the GGTT aub command rather than the
    * PHYSICAL aub command. This is because the Gen9 simulator seems to have 2
    * different set of memory pools for GGTT and physical (probably someone
    * didn't really understand the concept?).
    */
   static const char null_block[8 * 4096];
   for (uint64_t offset = 0; offset < size; offset += 4096) {
      uint32_t block_size = min(4096, size - offset);

      mem_trace_memory_write_header_out(aub, virt_addr + offset, block_size,
                                        AUB_MEM_TRACE_MEMORY_ADDRESS_SPACE_GGTT,
                                        "GGTT buffer");
      data_out(aub, (char *) data + offset, block_size);

      /* Pad to a multiple of 4 bytes. */
      data_out(aub, null_block, -block_size & 3);
   }
}

/**
 * Break up large objects into multiple writes.  Otherwise a 128kb VBO
 * would overflow the 16 bits of size field in the packet header and
 * everything goes badly after that.
 */
void
aub_write_trace_block(struct aub_file *aub,
                      uint32_t type, void *virtual,
                      uint32_t size, uint64_t gtt_offset)
{
   uint32_t block_size;
   uint32_t subtype = 0;
   static const char null_block[8 * 4096];

   for (uint32_t offset = 0; offset < size; offset += block_size) {
      block_size = min(8 * 4096, size - offset);

      if (aub_use_execlists(aub)) {
         block_size = min(4096, block_size);
         mem_trace_memory_write_header_out(aub,
                                           ppgtt_lookup(aub, gtt_offset + offset),
                                           block_size,
                                           AUB_MEM_TRACE_MEMORY_ADDRESS_SPACE_PHYSICAL,
                                           "Trace Block");
      } else {
         dword_out(aub, CMD_AUB_TRACE_HEADER_BLOCK |
                        ((aub->addr_bits > 32 ? 6 : 5) - 2));
         dword_out(aub, AUB_TRACE_MEMTYPE_GTT |
                        type | AUB_TRACE_OP_DATA_WRITE);
         dword_out(aub, subtype);
         dword_out(aub, gtt_offset + offset);
         dword_out(aub, align_u32(block_size, 4));
         if (aub->addr_bits > 32)
            dword_out(aub, (gtt_offset + offset) >> 32);
      }

      if (virtual)
         data_out(aub, ((char *) virtual) + offset, block_size);
      else
         data_out(aub, null_block, block_size);

      /* Pad to a multiple of 4 bytes. */
      data_out(aub, null_block, -block_size & 3);
   }
}

static void
aub_dump_ring_buffer_execlist(struct aub_file *aub,
                              const struct engine *cs,
                              uint64_t batch_offset)
{
   mem_trace_memory_write_header_out(aub, aub->engine_setup[cs->engine_class].ring_addr, 16,
                                     AUB_MEM_TRACE_MEMORY_ADDRESS_SPACE_GGTT,
                                     "RING MI_BATCH_BUFFER_START user");
   dword_out(aub, AUB_MI_BATCH_BUFFER_START | MI_BATCH_NON_SECURE_I965 | (3 - 2));
   dword_out(aub, batch_offset & 0xFFFFFFFF);
   dword_out(aub, batch_offset >> 32);
   dword_out(aub, 0 /* MI_NOOP */);

   mem_trace_memory_write_header_out(aub, aub->engine_setup[cs->engine_class].ring_addr + 8192 + 20, 4,
                                     AUB_MEM_TRACE_MEMORY_ADDRESS_SPACE_GGTT,
                                     "RING BUFFER HEAD");
   dword_out(aub, 0); /* RING_BUFFER_HEAD */
   mem_trace_memory_write_header_out(aub, aub->engine_setup[cs->engine_class].ring_addr + 8192 + 28, 4,
                                     AUB_MEM_TRACE_MEMORY_ADDRESS_SPACE_GGTT,
                                     "RING BUFFER TAIL");
   dword_out(aub, 16); /* RING_BUFFER_TAIL */
}

static void
aub_dump_execlist(struct aub_file *aub, const struct engine *cs, uint64_t descriptor)
{
   if (aub->devinfo.gen >= 11) {
      register_write_out(aub, cs->elsq_reg, descriptor & 0xFFFFFFFF);
      register_write_out(aub, cs->elsq_reg + sizeof(uint32_t), descriptor >> 32);
      register_write_out(aub, cs->control_reg, 1);
   } else {
      register_write_out(aub, cs->elsp_reg, 0);
      register_write_out(aub, cs->elsp_reg, 0);
      register_write_out(aub, cs->elsp_reg, descriptor >> 32);
      register_write_out(aub, cs->elsp_reg, descriptor & 0xFFFFFFFF);
   }

   dword_out(aub, CMD_MEM_TRACE_REGISTER_POLL | (5 + 1 - 1));
   dword_out(aub, cs->status_reg);
   dword_out(aub, AUB_MEM_TRACE_REGISTER_SIZE_DWORD |
                  AUB_MEM_TRACE_REGISTER_SPACE_MMIO);
   if (aub->devinfo.gen >= 11) {
      dword_out(aub, 0x00000001);   /* mask lo */
      dword_out(aub, 0x00000000);   /* mask hi */
      dword_out(aub, 0x00000001);
   } else {
      dword_out(aub, 0x00000010);   /* mask lo */
      dword_out(aub, 0x00000000);   /* mask hi */
      dword_out(aub, 0x00000000);
   }
}

static void
aub_dump_ring_buffer_legacy(struct aub_file *aub,
                            uint64_t batch_offset,
                            uint64_t offset,
                            enum drm_i915_gem_engine_class engine_class)
{
   uint32_t ringbuffer[4096];
   unsigned aub_mi_bbs_len;
   int ring_count = 0;
   static const int engine_class_to_ring[] = {
      [I915_ENGINE_CLASS_RENDER] = AUB_TRACE_TYPE_RING_PRB0,
      [I915_ENGINE_CLASS_VIDEO]  = AUB_TRACE_TYPE_RING_PRB1,
      [I915_ENGINE_CLASS_COPY]   = AUB_TRACE_TYPE_RING_PRB2,
   };
   int ring = engine_class_to_ring[engine_class];

   /* Make a ring buffer to execute our batchbuffer. */
   memset(ringbuffer, 0, sizeof(ringbuffer));

   aub_mi_bbs_len = aub->addr_bits > 32 ? 3 : 2;
   ringbuffer[ring_count] = AUB_MI_BATCH_BUFFER_START | (aub_mi_bbs_len - 2);
   aub_write_reloc(&aub->devinfo, &ringbuffer[ring_count + 1], batch_offset);
   ring_count += aub_mi_bbs_len;

   /* Write out the ring.  This appears to trigger execution of
    * the ring in the simulator.
    */
   dword_out(aub, CMD_AUB_TRACE_HEADER_BLOCK |
                  ((aub->addr_bits > 32 ? 6 : 5) - 2));
   dword_out(aub, AUB_TRACE_MEMTYPE_GTT | ring | AUB_TRACE_OP_COMMAND_WRITE);
   dword_out(aub, 0); /* general/surface subtype */
   dword_out(aub, offset);
   dword_out(aub, ring_count * 4);
   if (aub->addr_bits > 32)
      dword_out(aub, offset >> 32);

   data_out(aub, ringbuffer, ring_count * 4);
}

void
aub_write_exec(struct aub_file *aub, uint64_t batch_addr,
               uint64_t offset, enum drm_i915_gem_engine_class engine_class)
{
   const struct engine *cs = engine_from_engine_class(engine_class);

   if (aub_use_execlists(aub)) {
      aub_dump_ring_buffer_execlist(aub, cs, batch_addr);
      aub_dump_execlist(aub, cs, aub->engine_setup[engine_class].descriptor);
   } else {
      /* Dump ring buffer */
      aub_dump_ring_buffer_legacy(aub, batch_addr, offset, engine_class);
   }
   fflush(aub->file);
}

void
aub_write_context_execlists(struct aub_file *aub, uint64_t context_addr,
                            enum drm_i915_gem_engine_class engine_class)
{
   const struct engine *cs = engine_from_engine_class(engine_class);
   uint64_t descriptor = ((uint64_t)1 << 62 | context_addr  | CONTEXT_FLAGS);
   aub_dump_execlist(aub, cs, descriptor);
}
