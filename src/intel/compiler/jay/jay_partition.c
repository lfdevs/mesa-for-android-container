/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "util/u_math.h"
#include "jay_ir.h"
#include "jay_private.h"

/*
 * In addition to having enough total registers globally, the partition needs to
 * have enough contiguous registers in each file/stride to allocate any single
 * instruction in isolation. analyze_per_inst calculates bounds on the numbers
 * of required contiguous registers.
 *
 * This analysis is overly conservative, implicitly assuming that all operands
 * of a given file/stride must be contiguous. This could be improved with a lot
 * more bookkeeping, but it's unclear if it matters much in practice.
 *
 * Note the util_next_power_of_two below is critical for correctness. Although
 * RA handles non-power-of-two vectors, it aligns vectors to the next
 * power-of-two size. That doesn't affect global register demand, but it needs
 * to be reflected in our per-instruction minimum.
 *
 * As an example, an instruction with sources (vec3, vec2) requires at least 6
 * contiguous registers to satisfy RA, since the vec3 gets vec4 alignment and
 * the vec2 gets vec2 alignment. In principal, future RA improvements might
 * relax this, but we're not there yet.
 */
struct instruction_req {
   unsigned gpr[JAY_NUM_STRIDES];
   unsigned ugpr;
};

static struct instruction_req
analyze_per_inst(jay_shader *shader)
{
   struct instruction_req global = { 0 };

   jay_foreach_inst_in_shader(shader, func, I) {
      struct instruction_req local = { 0 };

      for (int i = -1; i < I->num_srcs; ++i) {
         jay_def x = i >= 0 ? I->src[i] : I->dst;
         if (!jay_is_null(x)) {
            unsigned size = util_next_power_of_two(jay_num_values(x));

            if (x.file == UGPR) {
               local.ugpr += size;
            } else if (x.file == GPR && i >= 0) {
               enum jay_stride min_stride = jay_src_stride_minmax(I, i, false);
               enum jay_stride max_stride = jay_src_stride_minmax(I, i, true);

               /* We want to reduce fragmentation of the partition as much as
                * possible, so assume if the destination didn't already force a
                * non-32-bit stride, assume sources are 32-bit strided.
                */
               if (min_stride <= JAY_STRIDE_4 && JAY_STRIDE_4 <= max_stride) {
                  local.gpr[JAY_STRIDE_4] += size;
               } else {
                  local.gpr[min_stride] += size;
               }
            } else if (x.file == GPR) {
               enum jay_stride min_stride = jay_dst_stride_minmax(I, false);
               local.gpr[min_stride] += size;
            }
         }
      }

      /* Take the worst-case for each block */
      global.ugpr = MAX2(global.ugpr, local.ugpr);

      for (unsigned i = 0; i < ARRAY_SIZE(global.gpr); ++i) {
         global.gpr[i] = MAX2(global.gpr[i], local.gpr[i]);
      }
   }

   return global;
}

/*
 * The partition is specified in a convenient form within jay_partition_grf(),
 * as a flat array of jay_partition_builder structs, which build_partition
 * translates to the more complicated jay_partition structs.
 */
struct jay_partition_builder {
   enum jay_file file;
   enum jay_stride stride;
   signed len_grf;
   enum jay_block_type type;
};

static void
build_partition(jay_shader *shader, struct jay_partition_builder *b, unsigned n)
{
   unsigned base_grf = 0, base_gpr[JAY_NUM_RA_FILES] = { 0 };
   struct jay_partition *p = &shader->partition;

   *p = (struct jay_partition) {
      .units_x16[UGPR] = jay_ugpr_per_grf(shader) * 16,
      .units_x16[GPR] = 16 / jay_grf_per_gpr(shader),
      .units_x16[MEM] = 16 / jay_grf_per_gpr(shader),
   };

   /* Drop empty blocks and merge the resulting neighbours. This avoids needless
    * partition boundaries that would unnecessarily constrain RA, while allowing
    * the caller the convenience of passing in holes sometimes.
    */
   signed j = -1;
   for (unsigned i = 0; i < n; ++i) {
      struct jay_partition_builder B = b[i];
      if (j >= 0 &&
          B.file == b[j].file &&
          B.stride == b[j].stride &&
          B.type == b[j].type) {

         b[j].len_grf += B.len_grf;
      } else if (B.len_grf) {
         b[++j] = B;
      }
   }

   /* Translate to jay_register_block */
   for (signed i = 0; i <= j; ++i) {
      enum jay_file file = b[i].file;
      unsigned len_gpr = (b[i].len_grf * p->units_x16[file]) / 16;
      bool grf = file < JAY_NUM_GRF_FILES;
      assert(p->nr_blocks[file] < JAY_PARTITION_BLOCKS);

      p->blocks[file][p->nr_blocks[file]++] = (struct jay_register_block) {
         .start_grf = grf ? base_grf : 0,
         .start_gpr = base_gpr[file],
         .len_gpr = (b[i].len_grf * p->units_x16[file]) / 16,
         .stride = b[i].stride,
         .type = b[i].type,
      };

      if (file < JAY_NUM_GRF_FILES) {
         base_grf += b[i].len_grf;
         base_gpr[file] += len_gpr;
      }
   }

   /* Validate the well formedness of the partition we built above */
   BITSET_DECLARE(regs, JAY_NUM_PHYS_GRF) = { 0 };

   for (enum jay_file file = 0; file < JAY_NUM_GRF_FILES; ++file) {
      for (unsigned b = 0; b < p->nr_blocks[file]; ++b) {
         struct jay_register_block B = p->blocks[file][b];
         unsigned len_grf = (B.len_gpr * 16) / p->units_x16[file];
         if (B.type == JAY_BLOCK_ACCUM) {
            continue;
         }

         assert(len_grf > 0 && "no empty partitions");
         assert(B.start_grf + len_grf <= JAY_NUM_PHYS_GRF && "GRF file size");
         assert(!BITSET_TEST_COUNT(regs, B.start_grf, len_grf) && "uniqueness");

         /* This requirement avoids invalid constructions like g127<2> */
         if (file == GPR && B.stride == JAY_STRIDE_8) {
            assert(util_is_aligned(len_grf, 2 * jay_grf_per_gpr(shader)) &&
                   "must be a multiple of 2 GPRs");
         }

         BITSET_SET_COUNT(regs, B.start_grf, len_grf);
      }
   }

   assert(BITSET_COUNT(regs) == JAY_NUM_PHYS_GRF && "all GRFs mapped");
}

/*
 * Partition the register file for the entire shader.
 *
 * All functions must share the same partition for correctness with non-uniform
 * function calls. For unlinked library functions, we must use the ABI
 * partition (TODO).
 */
void
jay_partition_grf(jay_shader *shader)
{
   unsigned grf_per_gpr = jay_grf_per_gpr(shader);
   unsigned ugpr_per_grf = jay_ugpr_per_grf(shader);

   /* Determine the shape of the payload/EOT sections upfront. */
   unsigned payload_4[2] = { 0, 0 }, payload_u[2] = { grf_per_gpr, 0 };
   unsigned eot_u = 0, eot_4 = 0;

   if (shader->stage == MESA_SHADER_VERTEX) {
      payload_4[0] = 1;
      payload_4[1] = shader->prog_data->vue.urb_read_length * 8;
      payload_u[1] = shader->push_grfs;
      eot_4 = 16;
   } else if (shader->stage == MESA_SHADER_TESS_EVAL) {
      payload_4[0] = 4; /* tesscoord and URB output handles */
      /* inputs and push constants use the general UGPR section */
      eot_4 = 16;
   } else if (shader->stage == MESA_SHADER_FRAGMENT) {
      /* The SIMD32 fragment payload splits GPRs into low and high GRFs, with
       * UGPRs mixed in between. jay_insert_payload_swizzle deals with this and
       * swizzles things appropriately, we just need the partition to have two
       * separate GPR block with a UGPR block in between. That requires the
       * number of GPRs in the payload to be even.
       */
      assert(util_is_aligned(shader->payload_gprs, grf_per_gpr) &&
             "payload constraint");

      payload_4[0] = shader->payload_gprs;
      payload_u[1] = (shader->payload_ugprs / ugpr_per_grf) - payload_u[0];
      payload_4[1] = grf_per_gpr == 2 ? shader->payload_gprs : 0;
      eot_4 = 14;
      eot_u = 1;
   } else {
      eot_u = 1;
   }

   /* EOT blocks are only relevant for platforms with Early EOT, otherwise any
    * register works fine.
    */
   if (!jay_has_early_eot(shader)) {
      eot_4 = eot_u = 0;
   }

   /* Calculate the maximum register demand across all functions in the shader.
    * We will use this to choose a good partition.
    */
   unsigned demand[JAY_NUM_GRF_FILES] = { 0 };
   struct instruction_req instr_req = analyze_per_inst(shader);

   jay_foreach_function(shader, f) {
      jay_compute_liveness(f);
      jay_calculate_register_demands(f);

      demand[GPR] = MAX2(demand[GPR], f->demand[GPR]);
      demand[UGPR] = MAX2(demand[UGPR], f->demand[UGPR]);
   }

   /* We must have enough register file space for the register payload, plus the
    * reserved UGPRs in the case we spill. That UGPR interferes with everything
    * we preload so it needs to be reserved specially here for the worst case.
    */
   jay_foreach_preload(jay_shader_get_entrypoint(shader), I) {
      unsigned end = jay_preload_reg(I) + jay_num_values(I->dst);
      unsigned extra = I->dst.file == UGPR ? shader->dispatch_width : 0;
      assert(I->dst.file < JAY_NUM_GRF_FILES);
      demand[I->dst.file] = MAX2(demand[I->dst.file], end + extra);
   }

   unsigned uniform_grfs, nonuniform_grfs;
   unsigned spilling_grfs = 0, mem_slots = 0;
   unsigned special_4 = payload_4[0] + payload_4[1] + eot_4, special_u;

   /* Our current stride partition heuristic is rather dumb: allocate as few
    * non-32-bit GRFs as possible, and assign the rest to 32-bit. This performs
    * well for 32-bit code but poorly for 16-bit/64-bit heavy routines.
    * We'll need stride-aware demand calculations to fix that (TODO).
    */
   unsigned grf_8 = align(instr_req.gpr[JAY_STRIDE_8], 2) * grf_per_gpr;
   unsigned grf_2 = instr_req.gpr[JAY_STRIDE_2] * grf_per_gpr;
   unsigned mapped_accums = grf_per_gpr == 1 ? 2 : 0;

   for (unsigned spilling = 0; spilling <= 1; spilling++) {
      /* There is an interdependence between partition choice and spilling,
       * because spilling requires reserved UGPRs for the lowered SENDs. The
       * solution is to first try to build a partition that forbids spilling,
       * and if that fails, build one with it.
       */
      spilling_grfs = spilling ? shader->dispatch_width / ugpr_per_grf : 0;
      special_u = payload_u[0] + payload_u[1] + spilling_grfs + eot_u;

      /* We want to determine a good GPR/UGPR split by the demand calculation.
       * At minimum we need to not spill UGPRs, but if GPR pressure is low we
       * want to take extra UGPRs to reduce shuffling.
       */
      uniform_grfs = DIV_ROUND_UP(demand[UGPR], ugpr_per_grf) + spilling_grfs;
      unsigned bonus_grfs = 4 * grf_per_gpr;
      unsigned estimate_nonunif_grf =
         (demand[GPR] * grf_per_gpr) + grf_8 + grf_2 + special_4;

      if ((uniform_grfs + estimate_nonunif_grf + bonus_grfs) <=
          JAY_NUM_PHYS_GRF) {
         uniform_grfs += bonus_grfs;
      }

      /* If the minimum vector length can't fit in any single existing block, we
       * will need a new block for it. This is quite conservative.
       */
      unsigned min_ugprs = special_u * ugpr_per_grf;
      if (instr_req.ugpr > payload_u[0] * ugpr_per_grf &&
          instr_req.ugpr > payload_u[1] * ugpr_per_grf &&
          instr_req.ugpr > eot_u * ugpr_per_grf) {

         min_ugprs += instr_req.ugpr;
      }

      /* Finally, we need to snap to GPR bounds */
      uniform_grfs = CLAMP(uniform_grfs, DIV_ROUND_UP(min_ugprs, ugpr_per_grf),
                           128 - (32 * grf_per_gpr));
      uniform_grfs = align(uniform_grfs, grf_per_gpr);
      nonuniform_grfs = JAY_NUM_PHYS_GRF - uniform_grfs;

      /* Set the targets for the virtual register file accordingly */
      shader->num_regs[GPR] = (nonuniform_grfs / grf_per_gpr) + mapped_accums;
      shader->num_regs[UGPR] = uniform_grfs * ugpr_per_grf;

      /* jay_gpr_limit depends on shader->num_regs[GPR]. If we're under the
       * limit without spilling, we're good to go.
       */
      if (demand[GPR] <= jay_gpr_limit(shader) && !spilling) {
         break;
      }
   }

   /* This should be an acceptable upper limit since we assign memory
    * tightly thanks to the usual SSA allocator guarantees.
    */
   if (spilling_grfs) {
      mem_slots = demand[GPR] * grf_per_gpr;
      shader->num_regs[MEM] = demand[GPR];
   }

   struct jay_partition_builder blocks[] = {
      /* Stage-specific payload */
      { UGPR, 0, payload_u[0] },
      { GPR, JAY_STRIDE_4, payload_4[0] },
      { UGPR, 0, payload_u[1] },
      { GPR, JAY_STRIDE_4, payload_4[1] },

      /* General registers */
      { UGPR, 0, uniform_grfs - special_u },
      { GPR, JAY_STRIDE_4, nonuniform_grfs - (special_4 + grf_8 + grf_2) },
      { GPR, JAY_STRIDE_8, grf_8 },
      { GPR, JAY_STRIDE_2, grf_2 },

      /* Spilling registers */
      { UGPR, 0, spilling_grfs, JAY_BLOCK_SPILL },
      { MEM, JAY_STRIDE_4, mem_slots },

      /* EOT */
      { UGPR, 0, eot_u, JAY_BLOCK_EOT },
      { GPR, JAY_STRIDE_4, eot_4, JAY_BLOCK_EOT },

      /* Accumulator block */
      { GPR, JAY_STRIDE_4, mapped_accums * grf_per_gpr, JAY_BLOCK_ACCUM },
   };

   build_partition(shader, blocks, ARRAY_SIZE(blocks));

   /* By construction of our partition, the entire GRF is used. */
   shader->prog_data->base.grf_used = JAY_NUM_PHYS_GRF;
}

#define ANSI_END    "\033[0m"
#define ANSI_BOLD   "\033[1m"
#define ANSI_ITALIC "\033[3m"

void
jay_print_partition(struct jay_partition *p)
{
   jay_foreach_ra_file(file) {
      if (p->nr_blocks[file]) {
         const char *files[JAY_NUM_RA_FILES] = { "GPR", "UGPR", "MEM" };
         printf("%s" ANSI_BOLD "    GRF      %s%s" ANSI_END "\n",
                file ? "\n" : "", files[file], file == GPR ? "    Stride" : "");
      }

      for (unsigned b = 0; b < p->nr_blocks[file]; ++b) {
         struct jay_register_block B = p->blocks[file][b];
         unsigned len_grf = (B.len_gpr * 16) / p->units_x16[file];

         printf("  %3u…%-3u  %3u…%-3u", B.start_grf, B.start_grf + len_grf - 1,
                B.start_gpr, B.start_gpr + B.len_gpr - 1);

         if (file == GPR) {
            printf("  %u-bit", jay_stride_to_bits(B.stride));
         }

         const char *types[JAY_BLOCK_TYPES] = { "", " EOT", " Spill",
                                                " Accumulator" };
         printf(ANSI_ITALIC "%s" ANSI_END "\n", types[B.type]);
      }
   }

   printf("\n");
}
