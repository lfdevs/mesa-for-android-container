/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "util/u_math.h"
#include "jay_ir.h"
#include "jay_private.h"
#include "shader_enums.h"

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
               local.gpr[min_stride] += size;
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
build_partition(jay_shader *shader,
                unsigned hw_grfs,
                struct jay_partition_builder *b,
                unsigned n)
{
   unsigned base_grf = 0, base_gpr[JAY_NUM_RA_FILES] = { 0 };
   struct jay_partition *p = &shader->partition;

   *p = (struct jay_partition){
      .units_x16[UGPR] = jay_ugpr_per_grf(shader) * 16,
      .units_x16[GPR] = 16 / jay_grf_per_gpr(shader),
      .units_x16[MEM] = 16 / jay_grf_per_gpr(shader),
      .units_x16[FLAG] = 16 / jay_grf_per_gpr(shader),
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

      p->blocks[file][p->nr_blocks[file]++] = (struct jay_register_block){
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
   BITSET_DECLARE(regs, JAY_MAX_PHYS_GRF) = { 0 };

   for (enum jay_file file = 0; file < JAY_NUM_GRF_FILES; ++file) {
      for (unsigned b = 0; b < p->nr_blocks[file]; ++b) {
         struct jay_register_block B = p->blocks[file][b];
         unsigned len_grf = (B.len_gpr * 16) / p->units_x16[file];
         if (B.type == JAY_BLOCK_ACCUM) {
            continue;
         }

         assert(len_grf > 0 && "no empty partitions");
         assert(B.start_grf + len_grf <= hw_grfs && "GRF file size");
         assert(!BITSET_TEST_COUNT(regs, B.start_grf, len_grf) && "uniqueness");

         /* This requirement avoids invalid constructions like g127<2> */
         if (file == GPR && B.stride == JAY_STRIDE_8) {
            assert(util_is_aligned(len_grf, 2 * jay_grf_per_gpr(shader)) &&
                   "must be a multiple of 2 GPRs");
         }

         BITSET_SET_COUNT(regs, B.start_grf, len_grf);
      }
   }

   assert(BITSET_COUNT(regs) == hw_grfs && "all GRFs mapped");
}

static inline unsigned
register_limit(jay_shader *s, enum jay_file file, unsigned limit)
{
   /* If testing spilling, set limit tightly. */
   if (jay_debug & JAY_DBG_SPILL) {
      /* UGPR spilling needs 1 extra register throughout the program but 2
       * extra registers right at the beginning with all the preloading.
       */
      struct instruction_req req = analyze_per_inst(s);
      limit =
         file == UGPR ? req.ugpr + 1 : req.gpr[0] + req.gpr[1] + req.gpr[2];

      jay_foreach_function(s, f) {
         jay_foreach_preload(f, I) {
            if (I->dst.file == file) {
               uint32_t max = jay_preload_reg(I) +
                              jay_num_values(I->dst) +
                              (file == GPR ? 0 : 2);
               limit = MAX2(limit, max);
            }
         }
      }
   }

   return limit;
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
   const struct intel_device_info *devinfo = shader->devinfo;
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
   } else if (shader->stage == MESA_SHADER_TASK ||
              shader->stage == MESA_SHADER_MESH) {
      payload_u[0] = 3 * grf_per_gpr;
      payload_4[0] = 2;
      eot_4 = 16;
      eot_u = 1;
   } else if (shader->stage == MESA_SHADER_TESS_CTRL) {
      payload_4[0] =
         1 +
         shader->prog_data->tcs.include_primitive_id +
         (shader->prog_data->tcs.input_vertices ?: BRW_MAX_TCS_INPUT_VERTICES);
      payload_u[1] = shader->push_grfs;
      eot_4 = 16;
   } else if (shader->stage == MESA_SHADER_GEOMETRY) {
      /* Payload layout for geo shader (bspec 65389):
       * 16       UGPR   Various fields stored in various subregisters.
       * 1        GPR    Per-lane Instance IDs & URB Handles.
       * if invocations == 1:
       *    1-6   GPR    ICP URB input handles
       * else if invocations > 1:
       *    32    UGPR   ICP URB input handles
       * Varies   UGPR   Constant Data
       * Varies   GPR    Pushed URB vertex data
       *
       * Note that even though ICP URB input handles are technically optional,
       * we always enable them anyway.
       */
      unsigned urb_push_size = shader->prog_data->vue.urb_read_length *
                               8 *
                               shader->prog_data->gs.vertices_in;

      unsigned prim_id_size =
         (shader->prog_data->gs.include_primitive_id ? 1 : 0);

      if (shader->prog_data->gs.invocations == 1) {
         unsigned urb_icp_size = shader->prog_data->gs.vertices_in;
         payload_4[0] = 1 + prim_id_size + urb_icp_size;
         payload_u[1] = shader->push_grfs;
         payload_4[1] = urb_push_size;
      } else {
         payload_4[0] = 1 + prim_id_size;
         payload_u[1] = shader->prog_data->gs.vertices_in + shader->push_grfs;
         payload_4[1] = urb_push_size;
      }
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

      if (devinfo->ver < 20) {
         payload_u[0] = 2;
      }

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
   unsigned ugpr_limit = register_limit(shader, UGPR, 64 * ugpr_per_grf);
   unsigned hw_flags = jay_num_flags(shader);
   unsigned flag_limit = hw_flags - shader->helpers_tracked;

   jay_foreach_function(shader, f) {
      jay_compute_liveness(f);
      jay_calculate_last_use(f);
      jay_calculate_register_demands(f);

      if (f->demand[FLAG] > flag_limit) {
         jay_spill(f, FLAG, flag_limit);
         jay_compute_liveness(f);
         jay_calculate_last_use(f);
         jay_calculate_register_demands(f);
      }

      if (f->demand[UGPR] > ugpr_limit) {
         jay_spill(f, UGPR, ugpr_limit);
         jay_compute_liveness(f);
         jay_calculate_last_use(f);
         jay_calculate_register_demands(f);
      }

      assert(f->demand[UGPR] <= ugpr_limit);

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

   unsigned uniform_grfs, nonuniform_grfs, hw_grfs;
   unsigned spilling_grfs = 0, mem_slots = 0;
   unsigned special_4 = payload_4[0] + payload_4[1] + eot_4;
   unsigned special_u = payload_u[0] + payload_u[1] + eot_u;

   /* If the minimum vector length can't fit in any single existing block, we
    * will need a new block for it. This is quite conservative.
    */
   unsigned min_ugprs = special_u * ugpr_per_grf;
   if (instr_req.ugpr > payload_u[0] * ugpr_per_grf &&
       instr_req.ugpr > payload_u[1] * ugpr_per_grf &&
       instr_req.ugpr > eot_u * ugpr_per_grf) {

      min_ugprs += instr_req.ugpr;
   }

   unsigned increment[JAY_NUM_STRIDES] = { grf_per_gpr, grf_per_gpr,
                                           2 * grf_per_gpr };
   unsigned min_grf[JAY_NUM_STRIDES] = {};
   unsigned min_grf_for_gprs = 0;
   for (unsigned i = 0; i < JAY_NUM_STRIDES; ++i) {
      min_grf[i] = align(instr_req.gpr[i] * grf_per_gpr, increment[i]);
      min_grf_for_gprs += min_grf[i];
   }

   unsigned estimate_nonunif_grf =
      MAX2(demand[GPR] * grf_per_gpr, min_grf[JAY_STRIDE_4]) +
      min_grf[JAY_STRIDE_8] +
      min_grf[JAY_STRIDE_2] +
      special_4;

   unsigned mapped_accums = grf_per_gpr == 1 ? 2 : 0;

   for (unsigned spilling = 0; spilling <= 1; spilling++) {
      /* There is an interdependence between partition choice and spilling,
       * because spilling requires reserved UGPRs for the lowered SENDs. The
       * solution is to first try to build a partition that forbids spilling,
       * and if that fails, build one with it.
       */
      spilling_grfs = spilling ? shader->dispatch_width / ugpr_per_grf : 0;
      uniform_grfs = DIV_ROUND_UP(demand[UGPR], ugpr_per_grf) + spilling_grfs;

      hw_grfs =
         intel_vrt_register_file_size(shader->devinfo,
                                      uniform_grfs + estimate_nonunif_grf);

      /* We want to determine a good GPR/UGPR split by the demand calculation.
       * At minimum we need to not spill UGPRs, but if GPR pressure is low we
       * want to take extra UGPRs to reduce shuffling.
       */
      unsigned bonus_grfs = 4 * grf_per_gpr;
      if ((uniform_grfs + estimate_nonunif_grf + bonus_grfs) <= hw_grfs) {
         uniform_grfs += bonus_grfs;
      }

      /* Finally, we need to snap to GPR bounds */
      uniform_grfs =
         CLAMP(uniform_grfs,
               DIV_ROUND_UP(min_ugprs, ugpr_per_grf) + spilling_grfs,
               hw_grfs - min_grf_for_gprs);
      uniform_grfs = align(uniform_grfs, grf_per_gpr);
      nonuniform_grfs = hw_grfs - uniform_grfs;

      /* Set the targets for the virtual register file accordingly */
      shader->num_regs[GPR] = (nonuniform_grfs / grf_per_gpr) + mapped_accums;
      shader->num_regs[UGPR] = uniform_grfs * ugpr_per_grf;

      /* If we're under the limit without spilling, we're good to go. */
      if (demand[GPR] <= register_limit(shader, GPR, shader->num_regs[GPR])) {
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

   /* Now that we've decided how many GRFs to use for GPRs, we need to partition
    * those GRFs by stride. This does not affect spilling but it has a
    * significant effect on moves inserted by RA. We use a simple heuristic to
    * pick a balanced partition: give each stride GRFs proportionate to the
    * number of SSA defs with that associated stride, plus a slight bias towards
    * 32-bit to avoid divsion by zero. This reflects our intuition that shaders
    * heavy on 16-bit (or 64-bit) arithmetic should have more 16-bit (or 64-bit)
    * registers overall.
    */
   unsigned counts[3] = { [JAY_STRIDE_4] = 1 };
   jay_foreach_inst_in_shader(shader, block, I) {
      if (I->dst.file == GPR) {
         counts[jay_dst_stride_minmax(I, false)] += jay_num_values(I->dst);
      }
   }

   min_grf[JAY_STRIDE_4] = MAX2(min_grf[JAY_STRIDE_4], special_4);

   unsigned picked_grf[JAY_NUM_STRIDES] = {}, total = 0;

   /* Assign minimum required for each stride first */
   for (unsigned i = 0; i < JAY_NUM_STRIDES; ++i) {
      assert(util_is_aligned(min_grf[i], increment[i]));
      picked_grf[i] = min_grf[i];
      total += picked_grf[i];
   }
   assert(total <= nonuniform_grfs);

   /* Proportionally assign remaining space */
   if (total < nonuniform_grfs) {
      unsigned denom_i = counts[0] + counts[1] + counts[2];
      float factor = (nonuniform_grfs - total) / ((float) denom_i);
      for (unsigned i = 0; i < JAY_NUM_STRIDES; ++i) {
         unsigned ideal = counts[i] * factor;
         unsigned extra = ROUND_DOWN_TO(ideal, increment[i]);
         picked_grf[i] += extra;
         total += extra;
      }
   }
   assert(total <= nonuniform_grfs);

   /* If we have GRFs to spare due to rounding, put them on 32-bit */
   if (total < nonuniform_grfs) {
      picked_grf[JAY_STRIDE_4] += nonuniform_grfs - total;
   }
   assert(picked_grf[0] + picked_grf[1] + picked_grf[2] == nonuniform_grfs);

   struct jay_partition_builder blocks[] = {
      /* Stage-specific payload */
      { UGPR, 0, payload_u[0] },
      { GPR, JAY_STRIDE_4, payload_4[0] },
      { UGPR, 0, payload_u[1] },
      { GPR, JAY_STRIDE_4, payload_4[1] },

      /* General registers */
      { UGPR, 0, uniform_grfs - special_u - spilling_grfs },
      { GPR, JAY_STRIDE_4, picked_grf[JAY_STRIDE_4] - special_4 },
      { GPR, JAY_STRIDE_8, picked_grf[JAY_STRIDE_8] },
      { GPR, JAY_STRIDE_2, picked_grf[JAY_STRIDE_2] },

      /* Spilling registers */
      { UGPR, 0, spilling_grfs, JAY_BLOCK_SPILL },
      { MEM, JAY_STRIDE_4, mem_slots },

      /* EOT */
      { UGPR, 0, eot_u, JAY_BLOCK_EOT },
      { GPR, JAY_STRIDE_4, eot_4, JAY_BLOCK_EOT },

      /* Accumulator block */
      { GPR, JAY_STRIDE_4, mapped_accums * grf_per_gpr, JAY_BLOCK_ACCUM },
      { FLAG, 0, flag_limit * jay_grf_per_gpr(shader) },
   };

   shader->num_regs[FLAG] = hw_flags;

   build_partition(shader, hw_grfs, blocks, ARRAY_SIZE(blocks));

   /* By construction of our partition, the entire GRF is used. */
   shader->prog_data->base.grf_used = hw_grfs;

   /* Spill as needed to fit within the partition we picked. */
   jay_foreach_function(shader, f) {
      unsigned limit = register_limit(shader, GPR, shader->num_regs[GPR]);
      bool spilled = f->demand[GPR] > limit;

      if (spilled) {
         jay_spill(f, GPR, limit);
         jay_validate(f->shader, "spilling");
         jay_compute_liveness(f);
         jay_calculate_last_use(f);
         jay_calculate_register_demands(f);
      }

      if (f->demand[GPR] > limit) {
         fprintf(stderr, "limit %u but demand %u\n", limit, f->demand[GPR]);
         fflush(stdout);
         UNREACHABLE("spiller bug");
      }

      assert(f->demand[UGPR] <= f->shader->num_regs[UGPR] &&
             "UGPRs spilled above");
   }
}

#define ANSI_END    "\033[0m"
#define ANSI_BOLD   "\033[1m"
#define ANSI_ITALIC "\033[3m"

static const char *jay_files[JAY_NUM_RA_FILES] = {
   [GPR] = "GPR", [UGPR] = "UGPR", [MEM] = "MEM", [FLAG] = "FLAG"
};

void
jay_print_partition(struct jay_partition *p)
{
   jay_foreach_ra_file(file) {
      if (p->nr_blocks[file]) {
         printf("%s" ANSI_BOLD "    GRF      %s%s" ANSI_END "\n",
                file ? "\n" : "", jay_files[file],
                file == GPR ? "    Stride" : "");
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
