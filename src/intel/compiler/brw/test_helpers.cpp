/*
 * Copyright © 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "test_helpers.h"
#include "brw_builder.h"

std::string
brw_shader_to_string(brw_shader *s)
{
   if (!s->cfg)
      brw_calculate_cfg(*s);

   char *str = NULL;
   size_t size = 0;

   FILE *f = open_memstream(&str, &size);
   brw_print_instructions(*s, f);
   fclose(f);

   std::string result(str);
   free(str);
   return result;
}

void
brw_reindex_vgrfs(brw_shader *s)
{
   struct Entry {
      int new_nr;
      int size;
      bool valid;
   };

   unsigned next_nr = 0;
   Entry *map = rzalloc_array(NULL, Entry, s->alloc.count);

   /* Build map and update references to VGRFs instructions as
    * they are seen.  First destination then sources.
    */
   foreach_block_and_inst(block, brw_inst, inst, s->cfg) {
      if (inst->dst.file == VGRF) {
         Entry &entry = map[inst->dst.nr];
         if (!entry.valid) {
            entry.new_nr = next_nr++;
            entry.size = s->alloc.sizes[inst->dst.nr];
            entry.valid = true;
         }
         inst->dst.nr = entry.new_nr;
      }

      for (int i = 0; i < inst->sources; i++) {
         if (inst->src[i].file == VGRF) {
            Entry &entry = map[inst->src[i].nr];
            if (!entry.valid) {
               entry.new_nr = next_nr++;
               entry.size = s->alloc.sizes[inst->src[i].nr];
               entry.valid = true;
            }
            inst->src[i].nr = entry.new_nr;
         }
      }
   }

   /* Move unused entries to the end. */
   for (unsigned i = 0; i < s->alloc.count; i++) {
      Entry &entry = map[i];
      if (!entry.valid) {
         entry.new_nr = next_nr++;
         entry.size = s->alloc.sizes[i];
         entry.valid = true;
      }
   }

   assert(next_nr == s->alloc.count);

   /* Update sizes. */
   for (unsigned i = 0; i < s->alloc.count; i++)
      s->alloc.sizes[map[i].new_nr] = map[i].size;

   ralloc_free(map);

   s->invalidate_analysis(BRW_DEPENDENCY_VARIABLES);
}

void
EXPECT_SHADERS_MATCH(brw_shader *a, brw_shader *b)
{
   if (!a->cfg)
      brw_calculate_cfg(*a);
   if (!b->cfg)
      brw_calculate_cfg(*b);

   brw_validate(*a);
   brw_validate(*b);

   brw_reindex_vgrfs(a);
   brw_reindex_vgrfs(b);

   std::string sa = brw_shader_to_string(a);
   std::string sb = brw_shader_to_string(b);

   EXPECT_EQ(sa, sb);
}

class TestHelpers : public brw_shader_pass_test {};

TEST_F(TestHelpers, ReindexVGRFsDestinations)
{
   brw_builder bld = make_shader();

   brw_reg d = bld.vgrf(BRW_TYPE_UD);
   brw_reg c = bld.vgrf(BRW_TYPE_UD);
   brw_reg b = bld.vgrf(BRW_TYPE_UD);
   brw_reg a = bld.vgrf(BRW_TYPE_UD);

   brw_inst *mov_a = bld.MOV(a, brw_vec8_grf(0, 0));
   brw_inst *mov_b = bld.MOV(b, a);
   brw_inst *mov_c = bld.MOV(c, b);
   brw_inst *add_d = bld.ADD(d, a, b);

   EXPECT_EQ(mov_a->dst.nr, 3);
   EXPECT_EQ(mov_b->dst.nr, 2);
   EXPECT_EQ(mov_c->dst.nr, 1);
   EXPECT_EQ(add_d->dst.nr, 0);

   brw_calculate_cfg(*bld.shader);
   brw_reindex_vgrfs(bld.shader);

   EXPECT_EQ(mov_a->dst.nr, 0);
   EXPECT_EQ(mov_b->dst.nr, 1);
   EXPECT_EQ(mov_c->dst.nr, 2);
   EXPECT_EQ(add_d->dst.nr, 3);
}

TEST_F(TestHelpers, ReindexVGRFsSources)
{
   brw_builder bld = make_shader();

   brw_reg d = bld.vgrf(BRW_TYPE_UD);
   brw_reg c = bld.vgrf(BRW_TYPE_UD);
   brw_reg b = bld.vgrf(BRW_TYPE_UD);
   brw_reg a = bld.vgrf(BRW_TYPE_UD);

   brw_inst *mov_a = bld.MOV(brw_vec8_grf(0,  0), a);
   brw_inst *mov_b = bld.MOV(brw_vec8_grf(7,  0), b);
   brw_inst *add_cd = bld.ADD(brw_vec8_grf(14, 0), c, d);

   EXPECT_EQ(mov_a->src[0].nr, 3);
   EXPECT_EQ(mov_b->src[0].nr, 2);
   EXPECT_EQ(add_cd->src[0].nr, 1);
   EXPECT_EQ(add_cd->src[1].nr, 0);

   brw_calculate_cfg(*bld.shader);
   brw_reindex_vgrfs(bld.shader);

   EXPECT_EQ(mov_a->src[0].nr, 0);
   EXPECT_EQ(mov_b->src[0].nr, 1);
   EXPECT_EQ(add_cd->src[0].nr, 2);
   EXPECT_EQ(add_cd->src[1].nr, 3);
}

TEST_F(TestHelpers, ReindexVGRFsIncludesSizes)
{
   brw_builder bld = make_shader();

   brw_reg d = bld.vgrf(BRW_TYPE_UD, 1);
   brw_reg c = bld.vgrf(BRW_TYPE_UD, 6);
   brw_reg b = bld.vgrf(BRW_TYPE_UD, 8);
   brw_reg a = bld.vgrf(BRW_TYPE_UD, 2);

   brw_inst *mov_a = bld.MOV(a, brw_imm_d(2222));
   brw_inst *mov_b = bld.MOV(b, brw_imm_d(8888));
   brw_inst *mov_c = bld.MOV(c, brw_imm_d(6666));
   brw_inst *mov_d = bld.MOV(d, brw_imm_d(1111));

   EXPECT_EQ(mov_a->dst.nr, 3);
   EXPECT_EQ(mov_b->dst.nr, 2);
   EXPECT_EQ(mov_c->dst.nr, 1);
   EXPECT_EQ(mov_d->dst.nr, 0);

   brw_calculate_cfg(*bld.shader);
   brw_reindex_vgrfs(bld.shader);

   EXPECT_EQ(mov_a->dst.nr, 0);
   EXPECT_EQ(mov_b->dst.nr, 1);
   EXPECT_EQ(mov_c->dst.nr, 2);
   EXPECT_EQ(mov_d->dst.nr, 3);

   brw_shader *s = bld.shader;
   EXPECT_EQ(s->alloc.count, 4);
   EXPECT_EQ(s->alloc.sizes[0], 2);
   EXPECT_EQ(s->alloc.sizes[1], 8);
   EXPECT_EQ(s->alloc.sizes[2], 6);
   EXPECT_EQ(s->alloc.sizes[3], 1);
}

TEST_F(TestHelpers, ReindexVGRFsUnusedAreMovedToTheEnd)
{
   brw_builder bld = make_shader();

   brw_reg a = bld.vgrf(BRW_TYPE_UD, 4);
   UNUSED brw_reg b = bld.vgrf(BRW_TYPE_UD, 6);
   brw_reg c = bld.vgrf(BRW_TYPE_UD, 8);

   brw_inst *add = bld.ADD(a, a, c);

   brw_calculate_cfg(*bld.shader);
   brw_reindex_vgrfs(bld.shader);

   EXPECT_EQ(add->src[0].nr, 0);
   EXPECT_EQ(add->src[1].nr, 1);

   brw_shader *s = bld.shader;
   EXPECT_EQ(s->alloc.count, 3);
   EXPECT_EQ(s->alloc.sizes[0], 4);
   EXPECT_EQ(s->alloc.sizes[1], 8);
   EXPECT_EQ(s->alloc.sizes[2], 6);
}

TEST_F(TestHelpers, MatchShadersSameStructureDifferentNumbers)
{
   brw_builder a = make_shader();
   brw_builder b = make_shader();

   int nr_from_a;
   int nr_from_b;

   /* Build using the builder automatic allocations. */
   {
      brw_inst *add = NULL;

      brw_reg src0 = a.MOV(brw_ud8_grf(1, 0));
      brw_reg src1 = a.MOV(brw_ud8_grf(7, 0));
                     a.ADD(src0, src1, &add);

      EXPECT_EQ(add->dst.file, VGRF);
      nr_from_a = add->dst.nr;
   }

   /* Build using explicitly created VGRFs. */
   {
      brw_reg dst  = b.vgrf(BRW_TYPE_UD);
      brw_reg src0 = b.vgrf(BRW_TYPE_UD);
      brw_reg src1 = b.vgrf(BRW_TYPE_UD);

      b.MOV(src0, brw_ud8_grf(1, 0));
      b.MOV(src1, brw_ud8_grf(7, 0));
      b.ADD(dst, src0, src1);

      nr_from_b = dst.nr;
   }

   EXPECT_NE(nr_from_a, nr_from_b);
   EXPECT_SHADERS_MATCH(a, b);
}
