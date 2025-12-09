/*
 * Copyright © 2019 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "test_helpers.h"
#include "brw_builder.h"

class scoreboard_test : public brw_shader_pass_test {
protected:
   scoreboard_test()
   {
      set_gfx_verx10(120);
   }

   static brw_reg *
   vgrf_array(brw_builder &a, brw_reg_type type, int count)
   {
      brw_reg *r = rzalloc_array(a.shader->mem_ctx, brw_reg, count);
      for (int i = 0; i < count; i++)
         r[i] = vgrf(a, type);
      return r;
   }

   static brw_reg *
   vgrf_array(brw_builder &a, brw_builder &b, brw_reg_type type, int count)
   {
      brw_reg *r = rzalloc_array(a.shader->mem_ctx, brw_reg, count);
      for (int i = 0; i < count; i++)
         r[i] = vgrf(a, b, type);
      return r;
   }
};

brw_inst *
SYNC_NOP(const brw_builder &bld)
{
   return bld.uniform().SYNC(TGL_SYNC_NOP);
}

brw_inst *
emit_SEND(const brw_builder &bld, const brw_reg &dst,
          const brw_reg &desc, const brw_reg &payload)
{
   brw_reg uniform_desc = component(desc, 0);

   brw_send_inst *send = bld.SEND();
   send->dst = dst;

   send->src[SEND_SRC_DESC]     = uniform_desc;
   send->src[SEND_SRC_EX_DESC]  = uniform_desc;
   send->src[SEND_SRC_PAYLOAD1] = payload;
   send->src[SEND_SRC_PAYLOAD2] = brw_reg();

   send->mlen = 1;
   send->size_written = dst.component_size(send->exec_size);

   return send;
}

bool operator ==(const tgl_swsb &a, const tgl_swsb &b)
{
   return a.mode == b.mode &&
          a.pipe == b.pipe &&
          a.regdist == b.regdist &&
          (a.mode == TGL_SBID_NULL || a.sbid == b.sbid);
}

/* Parse SWSB for setting test expected results. */
static tgl_swsb
SWSB(const char *input)
{
   struct tgl_swsb swsb = {};

   bool seen_sbid    = false;
   bool seen_regdist = false;

   const char *s = input;
   while (*s) {
      if (*s == ' ') {
         s++;

      } else if (*s == '$') {
         if (seen_sbid)
            goto invalid;

         s++;

         if (!isdigit(*s))
            goto invalid;

         unsigned sbid = 0;
         sbid = (*s - '0');
         s++;

         if (isdigit(*s)) {
            sbid = (sbid * 10) + (*s - '0');
            s++;
         }

         if (isdigit(*s) || sbid >= 32)
            goto invalid;

         swsb.sbid = sbid;

         if (*s == '.') {
            s++;
            if (!strncmp(s, "src", 3)) {
               swsb.mode = TGL_SBID_SRC;
               s += 3;
            } else if (!strncmp(s, "dst", 3)) {
               swsb.mode = TGL_SBID_DST;
               s += 3;
            } else {
               goto invalid;
            }
         } else {
            swsb.mode = TGL_SBID_SET;
         }

         seen_sbid = true;

      } else {
         if (seen_regdist)
            goto invalid;

         if (*s != '@') {
            switch (*s) {
            case 'F': swsb.pipe = TGL_PIPE_FLOAT;  break;
            case 'I': swsb.pipe = TGL_PIPE_INT;    break;
            case 'L': swsb.pipe = TGL_PIPE_LONG;   break;
            case 'A': swsb.pipe = TGL_PIPE_ALL;    break;
            case 'M': swsb.pipe = TGL_PIPE_MATH;   break;
            case 'S': swsb.pipe = TGL_PIPE_SCALAR; break;
            default: goto invalid;
            }
            s++;
         } else {
            swsb.pipe = TGL_PIPE_NONE;
         }
         if (*s != '@')
            goto invalid;
         s++;
         if (*s < '0' || *s > '7')
            goto invalid;
         swsb.regdist = *s - '0';
         s++;

         seen_regdist = true;
      }
   }

   return swsb;

invalid:
   ADD_FAILURE() << "Couldn't parse SWSB: " << input;
   return {};
}

TEST_F(scoreboard_test, parse_swsb)
{
   struct {
      const char *input;
      tgl_swsb    output;
   } tests[] = {
      { "",            {                                                                         } },
      { "@1",          { .regdist = 1                                                            } },
      { "A@6",         { .regdist = 6, .pipe = TGL_PIPE_ALL                                      } },
      { "$3",          {                                        .sbid = 3,  .mode = TGL_SBID_SET } },
      { "$0.src",      {                                        .sbid = 0,  .mode = TGL_SBID_SRC } },
      { "@1 $4.dst",   { .regdist = 1,                          .sbid = 4,  .mode = TGL_SBID_DST } },
      { "F@2 $11.src", { .regdist = 2, .pipe = TGL_PIPE_FLOAT,  .sbid = 11, .mode = TGL_SBID_SRC } },
      { "S@5 $22",     { .regdist = 5, .pipe = TGL_PIPE_SCALAR, .sbid = 22, .mode = TGL_SBID_SET } },
      { "M@1",         { .regdist = 1, .pipe = TGL_PIPE_MATH                                     } },
      { "$1 I@1",      { .regdist = 1, .pipe = TGL_PIPE_INT,    .sbid = 1,  .mode = TGL_SBID_SET } },
      { "$31.src L@4", { .regdist = 4, .pipe = TGL_PIPE_LONG,   .sbid = 31, .mode = TGL_SBID_SRC } },
   };

   for (auto &t : tests)
      EXPECT_EQ(SWSB(t.input), t.output);
}

TEST_F(scoreboard_test, RAW_inorder_inorder)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg *g = vgrf_array(bld, exp, BRW_TYPE_D, 8);
   brw_reg  x = vgrf(bld, exp, BRW_TYPE_D);
   brw_reg  y = vgrf(bld, exp, BRW_TYPE_D);

   bld.ADD(   x, g[1], g[2]);
   bld.MUL(   y, g[3], g[4]);
   bld.AND(g[5],    x,    y);

   EXPECT_PROGRESS(brw_lower_scoreboard, bld);

   exp.ADD(   x, g[1], g[2]);
   exp.MUL(   y, g[3], g[4]);
   exp.AND(g[5],    x,    y)->sched = SWSB("F@1");

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(scoreboard_test, RAW_inorder_outoforder)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg *g = vgrf_array(bld, exp, BRW_TYPE_D, 8);
   brw_reg  x = vgrf(bld, exp, BRW_TYPE_D);

   bld.ADD(          x, g[1], g[2]);
   bld.MUL(       g[3], g[4], g[5]);
   emit_SEND(bld, g[6], g[7],    x);

   EXPECT_PROGRESS(brw_lower_scoreboard, bld);

   exp.ADD(          x, g[1], g[2]);
   exp.MUL(       g[3], g[4], g[5]);
   emit_SEND(exp, g[6], g[7],    x)->sched = SWSB("$0 @2");

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(scoreboard_test, RAW_outoforder_inorder)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg *g = vgrf_array(bld, exp, BRW_TYPE_D, 8);
   brw_reg  x = vgrf(bld, exp, BRW_TYPE_D);
   brw_reg  y = vgrf(bld, exp, BRW_TYPE_D);

   emit_SEND(bld,    x, g[1], g[2]);
   bld.MUL(          y, g[3], g[4]);
   bld.AND(       g[5],    x,    y);

   EXPECT_PROGRESS(brw_lower_scoreboard, bld);

   emit_SEND(exp,    x, g[1], g[2])->sched = SWSB("$0");
   exp.MUL(          y, g[3], g[4]);
   exp.AND(       g[5],    x,    y)->sched = SWSB("@1 $0.dst");

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(scoreboard_test, RAW_outoforder_outoforder)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg *g = vgrf_array(bld, exp, BRW_TYPE_D, 8);
   brw_reg  x = vgrf(bld, exp, BRW_TYPE_D);

   /* The second SEND depends on the first, and would need to refer to two
    * SBIDs.  Since it is not possible we expect a SYNC instruction to be
    * added.
    */
   emit_SEND(bld,    x, g[1], g[2]);
   emit_SEND(bld, g[3],    x, g[4]);

   EXPECT_PROGRESS(brw_lower_scoreboard, bld);

   emit_SEND(exp,    x, g[1], g[2])->sched = SWSB("$0");
   SYNC_NOP (exp                  )->sched = SWSB("$0.dst");
   emit_SEND(exp, g[3],    x, g[4])->sched = SWSB("$1");

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(scoreboard_test, WAR_inorder_inorder)
{
   brw_builder bld = make_shader();

   brw_reg *g = vgrf_array(bld, BRW_TYPE_D, 8);
   brw_reg  x = vgrf(bld, BRW_TYPE_D);

   bld.ADD(g[1],    x, g[2]);
   bld.MUL(g[3], g[4], g[5]);
   bld.AND(   x, g[6], g[7]);

   EXPECT_NO_PROGRESS(brw_lower_scoreboard, bld);
}

TEST_F(scoreboard_test, WAR_inorder_outoforder)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg *g = vgrf_array(bld, exp, BRW_TYPE_D, 8);
   brw_reg  x = vgrf(bld, exp, BRW_TYPE_D);

   bld.ADD(       g[1],    x, g[2]);
   bld.MUL(       g[3], g[4], g[5]);
   emit_SEND(bld,    x, g[6], g[7]);

   EXPECT_PROGRESS(brw_lower_scoreboard, bld);

   exp.ADD(       g[1],    x, g[2]);
   exp.MUL(       g[3], g[4], g[5]);
   emit_SEND(exp,    x, g[6], g[7])->sched = SWSB("@2 $0");

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(scoreboard_test, WAR_outoforder_inorder)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg *g = vgrf_array(bld, exp, BRW_TYPE_D, 10);
   brw_reg  x = vgrf(bld, exp, BRW_TYPE_D);

   emit_SEND(bld, g[1], g[2],    x);
   bld.MUL(       g[4], g[5], g[6]);
   bld.AND(          x, g[7], g[8]);

   EXPECT_PROGRESS(brw_lower_scoreboard, bld);

   emit_SEND(exp, g[1], g[2],    x)->sched = SWSB("$0");
   exp.MUL(       g[4], g[5], g[6]);
   exp.AND(          x, g[7], g[8])->sched = SWSB("$0.src");

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(scoreboard_test, WAR_outoforder_outoforder)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg *g = vgrf_array(bld, exp, BRW_TYPE_D, 10);
   brw_reg  x = vgrf(bld, exp, BRW_TYPE_D);

   emit_SEND(bld, g[1], g[2],    x);
   emit_SEND(bld,    x, g[3], g[4]);

   EXPECT_PROGRESS(brw_lower_scoreboard, bld);

   emit_SEND(exp, g[1], g[2],    x)->sched = SWSB("$0");
   SYNC_NOP (exp                  )->sched = SWSB("$0.src");
   emit_SEND(exp,    x, g[3], g[4])->sched = SWSB("$1");

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(scoreboard_test, WAW_inorder_inorder)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg *g = vgrf_array(bld, exp, BRW_TYPE_D, 8);
   brw_reg  x = vgrf(bld, exp, BRW_TYPE_D);

   bld.ADD(   x, g[1], g[2]);
   bld.MUL(g[3], g[4], g[5]);
   bld.AND(   x, g[6], g[7]);

   EXPECT_PROGRESS(brw_lower_scoreboard, bld);

   /* NOTE: We only need this RegDist if a long instruction is followed by a
    * short one.  The pass is currently conservative about this and adding the
    * annotation.
    */

   exp.ADD(   x, g[1], g[2]);
   exp.MUL(g[3], g[4], g[5]);
   exp.AND(   x, g[6], g[7])->sched = SWSB("@2");

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(scoreboard_test, WAW_inorder_outoforder)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg *g = vgrf_array(bld, exp, BRW_TYPE_D, 8);
   brw_reg  x = vgrf(bld, exp, BRW_TYPE_D);

   bld.ADD(          x, g[1], g[2]);
   bld.MUL(       g[3], g[4], g[5]);
   emit_SEND(bld,    x, g[6], g[7]);

   EXPECT_PROGRESS(brw_lower_scoreboard, bld);

   exp.ADD(          x, g[1], g[2]);
   exp.MUL(       g[3], g[4], g[5]);
   emit_SEND(exp,    x, g[6], g[7])->sched = SWSB("@2 $0");

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(scoreboard_test, WAW_outoforder_inorder)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg *g = vgrf_array(bld, exp, BRW_TYPE_D, 8);
   brw_reg  x = vgrf(bld, exp, BRW_TYPE_D);

   emit_SEND(bld,    x, g[1], g[2]);
   bld.MUL(       g[3], g[4], g[5]);
   bld.AND(          x, g[6], g[7]);

   EXPECT_PROGRESS(brw_lower_scoreboard, bld);

   emit_SEND(exp,    x, g[1], g[2])->sched = SWSB("$0");
   exp.MUL(       g[3], g[4], g[5]);
   exp.AND(          x, g[6], g[7])->sched = SWSB("$0.dst");

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(scoreboard_test, WAW_outoforder_outoforder)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg *g = vgrf_array(bld, exp, BRW_TYPE_D, 8);
   brw_reg  x = vgrf(bld, exp, BRW_TYPE_D);

   emit_SEND(bld, x, g[1], g[2]);
   emit_SEND(bld, x, g[3], g[4]);

   EXPECT_PROGRESS(brw_lower_scoreboard, bld);

   emit_SEND(exp, x, g[1], g[2])->sched = SWSB("$0");
   SYNC_NOP (exp               )->sched = SWSB("$0.dst");
   emit_SEND(exp, x, g[3], g[4])->sched = SWSB("$1");

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(scoreboard_test, loop1)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg *g = vgrf_array(bld, exp, BRW_TYPE_D, 8);
   brw_reg  x = vgrf(bld, exp, BRW_TYPE_D);

   bld.XOR(   x, g[1], g[2]);
   bld.DO();
   bld.ADD(   x, g[1], g[2]);
   bld.WHILE(BRW_PREDICATE_NORMAL);
   bld.MUL(   x, g[1], g[2]);

   EXPECT_PROGRESS(brw_lower_scoreboard, bld);

   exp.XOR(   x, g[1], g[2]);
   exp.DO();
   exp.ADD(   x, g[1], g[2])->sched = SWSB("@1");
   exp.WHILE()->predicate = BRW_PREDICATE_NORMAL;
   exp.MUL(   x, g[1], g[2])->sched = SWSB("@1");

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(scoreboard_test, loop2)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg *g = vgrf_array(bld, exp, BRW_TYPE_D, 8);
   brw_reg  x = vgrf(bld, exp, BRW_TYPE_D);

   bld.XOR(   x, g[1], g[2]);
   bld.XOR(g[3], g[1], g[2]);
   bld.XOR(g[4], g[1], g[2]);
   bld.XOR(g[5], g[1], g[2]);

   bld.DO();

   bld.ADD(   x, g[1], g[2]);
   bld.WHILE(BRW_PREDICATE_NORMAL);

   bld.MUL(   x, g[1], g[2]);

   EXPECT_PROGRESS(brw_lower_scoreboard, bld);

   /* Now the write in ADD has the tightest RegDist for both ADD and MUL. */

   exp.XOR(   x, g[1], g[2]);
   exp.XOR(g[3], g[1], g[2]);
   exp.XOR(g[4], g[1], g[2]);
   exp.XOR(g[5], g[1], g[2]);

   exp.DO();

   exp.ADD(   x, g[1], g[2])->sched = SWSB("@2");
   exp.WHILE()->predicate = BRW_PREDICATE_NORMAL;

   exp.MUL(   x, g[1], g[2])->sched = SWSB("@2");

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(scoreboard_test, loop3)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg *g = vgrf_array(bld, exp, BRW_TYPE_D, 8);
   brw_reg  x = vgrf(bld, exp, BRW_TYPE_D);

   bld.XOR(   x, g[1], g[2]);

   bld.DO();

   /* For the ADD in the loop body this extra distance will always apply. */
   bld.XOR(g[3], g[1], g[2]);
   bld.XOR(g[4], g[1], g[2]);
   bld.XOR(g[5], g[1], g[2]);
   bld.XOR(g[6], g[1], g[2]);

   bld.ADD(   x, g[1], g[2]);
   bld.WHILE(BRW_PREDICATE_NORMAL);

   bld.MUL(   x, g[1], g[2]);

   EXPECT_PROGRESS(brw_lower_scoreboard, bld);

   exp.XOR(   x, g[1], g[2]);

   exp.DO();

   /* Note these are inside the loop, and now depend on their previous
    * iteration.
    */
   exp.XOR(g[3], g[1], g[2])->sched = SWSB("@6");
   exp.XOR(g[4], g[1], g[2])->sched = SWSB("@6");
   exp.XOR(g[5], g[1], g[2])->sched = SWSB("@6");
   exp.XOR(g[6], g[1], g[2])->sched = SWSB("@6");

   exp.ADD(   x, g[1], g[2])->sched = SWSB("@5");
   exp.WHILE()->predicate = BRW_PREDICATE_NORMAL;

   exp.MUL(   x, g[1], g[2])->sched = SWSB("@1");

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(scoreboard_test, conditional1)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg *g = vgrf_array(bld, exp, BRW_TYPE_D, 8);
   brw_reg  x = vgrf(bld, exp, BRW_TYPE_D);

   bld.XOR(   x, g[1], g[2]);
   bld.IF();
   bld.ADD(   x, g[1], g[2]);
   bld.ENDIF();
   bld.MUL(   x, g[1], g[2]);

   EXPECT_PROGRESS(brw_lower_scoreboard, bld);

   exp.XOR(   x, g[1], g[2]);
   exp.IF();
   exp.ADD(   x, g[1], g[2])->sched = SWSB("@2");
   exp.ENDIF();
   exp.MUL(   x, g[1], g[2])->sched = SWSB("@2");

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(scoreboard_test, conditional2)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg *g = vgrf_array(bld, exp, BRW_TYPE_D, 8);
   brw_reg  x = vgrf(bld, exp, BRW_TYPE_D);

   bld.XOR(   x, g[1], g[2]);
   bld.XOR(g[3], g[1], g[2]);
   bld.XOR(g[4], g[1], g[2]);
   bld.XOR(g[5], g[1], g[2]);
   bld.IF();

   bld.ADD(   x, g[1], g[2]);

   bld.ENDIF();
   bld.MUL(   x, g[1], g[2]);

   EXPECT_PROGRESS(brw_lower_scoreboard, bld);

   exp.XOR(   x, g[1], g[2]);
   exp.XOR(g[3], g[1], g[2]);
   exp.XOR(g[4], g[1], g[2]);
   exp.XOR(g[5], g[1], g[2]);
   exp.IF();

   exp.ADD(   x, g[1], g[2])->sched = SWSB("@5");

   exp.ENDIF();
   exp.MUL(   x, g[1], g[2])->sched = SWSB("@2");

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(scoreboard_test, conditional3)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg *g = vgrf_array(bld, exp, BRW_TYPE_D, 8);
   brw_reg  x = vgrf(bld, exp, BRW_TYPE_D);

   bld.XOR(   x, g[1], g[2]);
   bld.IF();

   bld.XOR(g[3], g[1], g[2]);
   bld.XOR(g[4], g[1], g[2]);
   bld.XOR(g[5], g[1], g[2]);
   bld.ADD(   x, g[1], g[2]);

   bld.ENDIF();
   bld.MUL(   x, g[1], g[2]);

   EXPECT_PROGRESS(brw_lower_scoreboard, bld);

   exp.XOR(   x, g[1], g[2]);
   exp.IF();

   exp.XOR(g[3], g[1], g[2]);
   exp.XOR(g[4], g[1], g[2]);
   exp.XOR(g[5], g[1], g[2]);
   exp.ADD(   x, g[1], g[2])->sched = SWSB("@5");

   exp.ENDIF();
   exp.MUL(   x, g[1], g[2])->sched = SWSB("@2");

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(scoreboard_test, conditional4)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg *g = vgrf_array(bld, exp, BRW_TYPE_D, 8);
   brw_reg  x = vgrf(bld, exp, BRW_TYPE_D);

   bld.XOR(   x, g[1], g[2]);
   bld.IF();

   bld.ADD(   x, g[1], g[2]);
   bld.XOR(g[3], g[1], g[2]);
   bld.XOR(g[4], g[1], g[2]);
   bld.XOR(g[5], g[1], g[2]);

   bld.ENDIF();
   bld.MUL(   x, g[1], g[2]);

   EXPECT_PROGRESS(brw_lower_scoreboard, bld);

   exp.XOR(   x, g[1], g[2]);
   exp.IF();

   exp.ADD(   x, g[1], g[2])->sched = SWSB("@2");
   exp.XOR(g[3], g[1], g[2]);
   exp.XOR(g[4], g[1], g[2]);
   exp.XOR(g[5], g[1], g[2]);

   exp.ENDIF();
   exp.MUL(   x, g[1], g[2])->sched = SWSB("@3");

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(scoreboard_test, conditional5)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg *g = vgrf_array(bld, exp, BRW_TYPE_D, 8);
   brw_reg  x = vgrf(bld, exp, BRW_TYPE_D);

   bld.XOR(   x, g[1], g[2]);
   bld.IF();

   bld.ADD(   x, g[1], g[2]);
   bld.ELSE();

   bld.ROL(   x, g[1], g[2]);

   bld.ENDIF();
   bld.MUL(   x, g[1], g[2]);

   EXPECT_PROGRESS(brw_lower_scoreboard, bld);

   exp.XOR(   x, g[1], g[2]);
   exp.IF();

   exp.ADD(   x, g[1], g[2])->sched = SWSB("@2");
   exp.ELSE();

   exp.ROL(   x, g[1], g[2])->sched = SWSB("@2");

   exp.ENDIF();
   exp.MUL(   x, g[1], g[2])->sched = SWSB("@2");

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(scoreboard_test, conditional6)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg *g = vgrf_array(bld, exp, BRW_TYPE_D, 10);
   brw_reg  x = vgrf(bld, exp, BRW_TYPE_D);

   bld.XOR(   x, g[1], g[2]);
   bld.IF();

   bld.XOR(g[3], g[1], g[2]);
   bld.XOR(g[4], g[1], g[2]);
   bld.XOR(g[5], g[1], g[2]);
   bld.ADD(   x, g[1], g[2]);
   bld.ELSE();

   bld.XOR(g[6], g[1], g[2]);
   bld.XOR(g[7], g[1], g[2]);
   bld.XOR(g[8], g[1], g[2]);
   bld.XOR(g[9], g[1], g[2]);
   bld.ROL(   x, g[1], g[2]);

   bld.ENDIF();
   bld.MUL(   x, g[1], g[2]);

   EXPECT_PROGRESS(brw_lower_scoreboard, bld);

   exp.XOR(   x, g[1], g[2]);
   exp.IF();

   exp.XOR(g[3], g[1], g[2]);
   exp.XOR(g[4], g[1], g[2]);
   exp.XOR(g[5], g[1], g[2]);
   exp.ADD(   x, g[1], g[2])->sched = SWSB("@5");
   exp.ELSE();

   exp.XOR(g[6], g[1], g[2]);
   exp.XOR(g[7], g[1], g[2]);
   exp.XOR(g[8], g[1], g[2]);
   exp.XOR(g[9], g[1], g[2]);
   exp.ROL(   x, g[1], g[2])->sched = SWSB("@6");

   exp.ENDIF();
   exp.MUL(   x, g[1], g[2])->sched = SWSB("@2");

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(scoreboard_test, conditional7)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg *g = vgrf_array(bld, exp, BRW_TYPE_D, 10);
   brw_reg  x = vgrf(bld, exp, BRW_TYPE_D);

   bld.XOR(   x, g[1], g[2]);
   bld.IF();

   bld.ADD(   x, g[1], g[2]);
   bld.XOR(g[3], g[1], g[2]);
   bld.XOR(g[4], g[1], g[2]);
   bld.XOR(g[5], g[1], g[2]);
   bld.ELSE();

   bld.ROL(   x, g[1], g[2]);
   bld.XOR(g[6], g[1], g[2]);
   bld.XOR(g[7], g[1], g[2]);
   bld.XOR(g[8], g[1], g[2]);
   bld.XOR(g[9], g[1], g[2]);

   bld.ENDIF();
   bld.MUL(   x, g[1], g[2]);

   EXPECT_PROGRESS(brw_lower_scoreboard, bld);

   exp.XOR(   x, g[1], g[2]);
   exp.IF();

   exp.ADD(   x, g[1], g[2])->sched = SWSB("@2");
   exp.XOR(g[3], g[1], g[2]);
   exp.XOR(g[4], g[1], g[2]);
   exp.XOR(g[5], g[1], g[2]);
   exp.ELSE();

   exp.ROL(   x, g[1], g[2])->sched = SWSB("@2");
   exp.XOR(g[6], g[1], g[2]);
   exp.XOR(g[7], g[1], g[2]);
   exp.XOR(g[8], g[1], g[2]);
   exp.XOR(g[9], g[1], g[2]);

   exp.ENDIF();
   exp.MUL(   x, g[1], g[2])->sched = SWSB("@6");

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(scoreboard_test, conditional8)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg *g = vgrf_array(bld, exp, BRW_TYPE_D, 8);
   brw_reg  x = vgrf(bld, exp, BRW_TYPE_D);

   bld.XOR(   x, g[1], g[2]);
   bld.XOR(g[3], g[1], g[2]);
   bld.XOR(g[4], g[1], g[2]);
   bld.XOR(g[5], g[1], g[2]);
   bld.XOR(g[6], g[1], g[2]);
   bld.XOR(g[7], g[1], g[2]);
   bld.IF();

   bld.ADD(   x, g[1], g[2]);
   bld.ELSE();

   bld.ROL(   x, g[1], g[2]);

   bld.ENDIF();
   bld.MUL(   x, g[1], g[2]);

   EXPECT_PROGRESS(brw_lower_scoreboard, bld);

   exp.XOR(   x, g[1], g[2]);
   exp.XOR(g[3], g[1], g[2]);
   exp.XOR(g[4], g[1], g[2]);
   exp.XOR(g[5], g[1], g[2]);
   exp.XOR(g[6], g[1], g[2]);
   exp.XOR(g[7], g[1], g[2]);
   exp.IF();

   exp.ADD(   x, g[1], g[2])->sched = SWSB("@7");
   exp.ELSE();

   /* Note that the ROL will have RegDist 2 and not 7, illustrating the
    * physical CFG edge between the then-block and the else-block.
    */
   exp.ROL(   x, g[1], g[2])->sched = SWSB("@2");

   exp.ENDIF();
   exp.MUL(   x, g[1], g[2])->sched = SWSB("@2");

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(scoreboard_test, gfx125_RaR_over_different_pipes)
{
   set_gfx_verx10(125);

   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg a = vgrf(bld, exp, BRW_TYPE_D);
   brw_reg b = vgrf(bld, exp, BRW_TYPE_D);
   brw_reg f = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg x = vgrf(bld, exp, BRW_TYPE_D);

   bld.ADD(f, x, x);
   bld.ADD(a, x, x);
   bld.ADD(x, b, b);

   EXPECT_PROGRESS(brw_lower_scoreboard, bld);

   exp.ADD(f, x, x);
   exp.ADD(a, x, x);
   exp.ADD(x, b, b)->sched = SWSB("A@1");

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(scoreboard_test, gitlab_issue_from_mr_29723)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg a = brw_ud8_grf(29, 0);
   brw_reg b = brw_ud8_grf(2, 0);

   auto bld1 = bld.uniform();
   bld1.ADD(             a, stride(b, 0, 1, 0),    brw_imm_ud(256));
   bld1.CMP(brw_null_reg(), stride(a, 2, 1, 2), stride(b, 0, 1, 0), BRW_CONDITIONAL_L);

   EXPECT_PROGRESS(brw_lower_scoreboard, bld);

   auto exp1 = exp.uniform();
   exp1.ADD(             a, stride(b, 0, 1, 0),    brw_imm_ud(256));
   exp1.CMP(brw_null_reg(), stride(a, 2, 1, 2), stride(b, 0, 1, 0), BRW_CONDITIONAL_L)->sched = SWSB("@1");

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(scoreboard_test, combine_regdist_float_and_int_with_sbid_set)
{
   set_gfx_verx10(200);

   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg a = retype(brw_ud8_grf(10, 0), BRW_TYPE_F);
   brw_reg b = brw_ud8_grf(20, 0);
   brw_reg x = brw_ud8_grf(30, 0);

   bld.ADD(       a, a, a);
   bld.ADD(       b, b, b);
   emit_SEND(bld, x, a, b);

   EXPECT_PROGRESS(brw_lower_scoreboard, bld);

   exp.ADD(       a, a, a);
   exp.ADD(       b, b, b);
   emit_SEND(exp, x, a, b)->sched = SWSB("A@1 $0");

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(scoreboard_test, combine_regdist_float_with_sbid_set)
{
   set_gfx_verx10(200);

   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg a = retype(brw_ud8_grf(10, 0), BRW_TYPE_F);
   brw_reg b = retype(brw_ud8_grf(20, 0), BRW_TYPE_F);
   brw_reg x = brw_ud8_grf(30, 0);

   bld.ADD(       a, a, a);
   bld.ADD(       b, b, b);
   emit_SEND(bld, x, a, b);

   EXPECT_PROGRESS(brw_lower_scoreboard, bld);

   exp.ADD(       a, a, a);
   exp.ADD(       b, b, b);
   emit_SEND(exp, x, a, b)->sched = SWSB("F@1 $0");

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(scoreboard_test, combine_regdist_int_with_sbid_set)
{
   set_gfx_verx10(200);

   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg a = brw_ud8_grf(10, 0);
   brw_reg b = brw_ud8_grf(20, 0);
   brw_reg x = brw_ud8_grf(30, 0);

   bld.ADD(       a, a, a);
   bld.ADD(       b, b, b);
   emit_SEND(bld, x, a, b);

   EXPECT_PROGRESS(brw_lower_scoreboard, bld);

   exp.ADD(       a, a, a);
   exp.ADD(       b, b, b);
   emit_SEND(exp, x, a, b)->sched = SWSB("I@1 $0");

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(scoreboard_test, gitlab_issue_11069)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg a = brw_ud8_grf(76, 0);
   brw_reg b = brw_ud8_grf(2, 0);

   auto bld1 = bld.uniform();
   bld1.ADD(stride(a, 2, 1, 2), stride(b, 0, 1, 0),   brw_imm_ud(0x80));
   bld1.CMP(    brw_null_reg(), stride(a, 0, 1, 0), stride(b, 0, 1, 0), BRW_CONDITIONAL_L);

   EXPECT_PROGRESS(brw_lower_scoreboard, bld);

   auto exp1 = exp.uniform();
   exp1.ADD(stride(a, 2, 1, 2), stride(b, 0, 1, 0),   brw_imm_ud(0x80));
   exp1.CMP(    brw_null_reg(), stride(a, 0, 1, 0), stride(b, 0, 1, 0), BRW_CONDITIONAL_L)->sched = SWSB("@1");

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(scoreboard_test, gfx120_can_embed_outoforder_src_dependency_in_send_eot)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg a    = brw_ud8_grf(10, 0);
   brw_reg b    = brw_ud8_grf(20, 0);
   brw_reg x    = brw_ud8_grf(30, 0);
   brw_reg desc = brw_ud8_grf(40, 0);

   brw_inst *send;

          emit_SEND(bld, a, desc, x);
   send = emit_SEND(bld, b, desc, x);
   send->eot = true;

   EXPECT_PROGRESS(brw_lower_scoreboard, bld);

          emit_SEND(exp, a, desc, x)->sched = SWSB("$0");
   send = emit_SEND(exp, b, desc, x);
   send->eot   = true;
   send->sched = SWSB("$0.src");

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(scoreboard_test, gfx120_can_embed_outoforder_dst_dependency_in_send_eot)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg a    = brw_ud8_grf(10, 0);
   brw_reg b    = brw_ud8_grf(20, 0);
   brw_reg x    = brw_ud8_grf(30, 0);
   brw_reg desc = brw_ud8_grf(40, 0);

   brw_inst *send;

          emit_SEND(bld, x, desc, a);
   send = emit_SEND(bld, b, desc, x);
   send->eot = true;

   EXPECT_PROGRESS(brw_lower_scoreboard, bld);

          emit_SEND(exp, x, desc, a)->sched = SWSB("$0");
   send = emit_SEND(exp, b, desc, x);
   send->eot   = true;
   send->sched = SWSB("$0.dst");

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(scoreboard_test, gfx200_cannot_embed_outoforder_src_dependency_in_send_eot)
{
   set_gfx_verx10(200);

   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg a    = brw_ud8_grf(10, 0);
   brw_reg b    = brw_ud8_grf(20, 0);
   brw_reg x    = brw_ud8_grf(30, 0);
   brw_reg desc = brw_ud8_grf(40, 0);

   emit_SEND(bld, a, desc, x);
   emit_SEND(bld, b, desc, x)->eot = true;

   EXPECT_PROGRESS(brw_lower_scoreboard, bld);

   emit_SEND(exp, a, desc, x)->sched = SWSB("$0");
   SYNC_NOP (exp            )->sched = SWSB("$0.src");
   emit_SEND(exp, b, desc, x)->eot = true;

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(scoreboard_test, gfx200_cannot_embed_outoforder_dst_dependency_in_send_eot)
{
   set_gfx_verx10(200);

   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg a    = brw_ud8_grf(10, 0);
   brw_reg b    = brw_ud8_grf(20, 0);
   brw_reg x    = brw_ud8_grf(30, 0);
   brw_reg desc = brw_ud8_grf(40, 0);

   emit_SEND(bld, x, desc, a);
   emit_SEND(bld, b, desc, x)->eot = true;

   EXPECT_PROGRESS(brw_lower_scoreboard, bld);

   emit_SEND(exp, x, desc, a)->sched = SWSB("$0");
   SYNC_NOP (exp            )->sched = SWSB("$0.dst");
   emit_SEND(exp, b, desc, x)->eot = true;

   EXPECT_SHADERS_MATCH(bld, exp);
}

static brw_reg
brw_s0_with_region(enum brw_reg_type type, unsigned subnr, unsigned v, unsigned w, unsigned h)
{
   return brw_make_reg(ARF,
                       BRW_ARF_SCALAR,
                       subnr,
                       0,
                       0,
                       type,
                       cvt(v),
                       cvt(w)-1,
                       cvt(h),
                       BRW_SWIZZLE_XYZW,
                       WRITEMASK_XYZW);
}

TEST_F(scoreboard_test, scalar_register_mov_immediate_is_in_scalar_pipe)
{
   set_gfx_verx10(300);

   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg scalar = brw_s0_with_region(BRW_TYPE_UW, 0, 0, 1, 0);
   brw_reg imm    = brw_imm_uw(0x1415);
   brw_reg r20    = brw_uw8_grf(20, 0);

   bld.uniform().MOV(scalar, imm);
   bld          .MOV(r20, scalar);

   EXPECT_PROGRESS(brw_lower_scoreboard, bld);

   exp.uniform().MOV(scalar, imm);
                 SYNC_NOP(exp   )->sched = SWSB("S@1");
   exp          .MOV(r20, scalar);

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(scoreboard_test, scalar_register_mov_grf_is_not_in_scalar_pipe)
{
   set_gfx_verx10(300);

   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg scalar = brw_s0_with_region(BRW_TYPE_UW, 0, 0, 1, 0);
   brw_reg r10    = brw_uw8_grf(10, 0);
   brw_reg r20    = brw_uw8_grf(20, 0);

   bld.uniform().MOV(scalar, r10);
   bld          .MOV(r20, scalar);

   EXPECT_PROGRESS(brw_lower_scoreboard, bld);

   exp.uniform().MOV     (scalar, r10);
                 SYNC_NOP(exp       )->sched = SWSB("I@1");
   exp          .MOV     (r20, scalar);

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(scoreboard_test, baked_dependency_with_inferred_pipe_combination)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg *g = vgrf_array(bld, exp, BRW_TYPE_F, 8);
   brw_reg  x = vgrf(bld, exp, BRW_TYPE_F);

   bld.MOV(g[1], brw_imm_f(1.0f));
   bld.MOV(g[2], brw_imm_f(2.0f));
   bld.MOV(g[5], brw_imm_f(5.0f));

   bld.IF();
   emit_SEND(bld, x, g[1], g[2]);
   bld.ELSE();
   emit_SEND(bld, x, g[1], g[2]);
   bld.ENDIF();

   bld.MAD(g[4], g[5], g[1], x);

   EXPECT_PROGRESS(brw_lower_scoreboard, bld);

   exp.MOV(g[1], brw_imm_f(1.0f));
   exp.MOV(g[2], brw_imm_f(2.0f));
   exp.MOV(g[5], brw_imm_f(5.0f));

   exp.IF();
   emit_SEND(exp, x, g[1], g[2])->sched = SWSB("@3 $0");
   exp.ELSE();
   emit_SEND(exp, x, g[1], g[2])->sched = SWSB("@3 $0");
   exp.ENDIF();

   exp.MAD(g[4], g[5], g[1], x)->sched = SWSB("@3 $0.dst");

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(scoreboard_test, math_inv_with_mul_dependency)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg *g = vgrf_array(bld, exp, BRW_TYPE_F, 8);
   brw_reg  x = vgrf(bld, exp, BRW_TYPE_F);

   bld.MOV(g[1], brw_imm_f(1.0f));
   bld.MOV(g[2], brw_imm_f(2.0f));

   bld.emit(SHADER_OPCODE_RCP, x, g[1]);
   bld.MUL(g[1], g[2], x);

   EXPECT_PROGRESS(brw_lower_scoreboard, bld);

   exp.MOV(g[1], brw_imm_f(1.0f));
   exp.MOV(g[2], brw_imm_f(2.0f));

   exp.emit(SHADER_OPCODE_RCP, x, g[1])->sched = SWSB("@2 $0");
   exp.MUL(g[1], g[2], x)->sched = SWSB("@1 $0.dst");

   EXPECT_SHADERS_MATCH(bld, exp);
}
