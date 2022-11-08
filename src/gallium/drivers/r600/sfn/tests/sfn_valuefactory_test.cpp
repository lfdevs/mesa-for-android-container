/* -*- mesa-c++  -*-
 *
 * Copyright (c) 2021 Collabora LTD
 *
 * Author: Gert Wollny <gert.wollny@collabora.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "../sfn_alu_defines.h"
#include "../sfn_debug.h"
#include "../sfn_valuefactory.h"
#include "nir_builder.h"
#include "ralloc.h"

#include "gtest/gtest.h"

using namespace r600;

class ValuefactoryTest : public ::testing::Test {

public:
   ValuefactoryTest();

protected:
   void SetUp() override;
   void TearDown() override;

   ValueFactory *factory;
   nir_builder b;
   nir_shader_compiler_options options;
};

TEST_F(ValuefactoryTest, test_create_ssa)
{
   auto c1 = nir_imm_float(&b, 2.0);
   auto c2 = nir_imm_float(&b, 4.0);
   auto sum = nir_fadd(&b, c1, c2);
   auto alu = nir_instr_as_alu(sum->parent_instr);

   sfn_log << SfnLog::reg << "Search (test) " << &alu->dest << "\n";
   auto dest_value = factory->dest(alu->dest, 0, pin_none);
   EXPECT_EQ(dest_value->sel(), 1024);
   EXPECT_EQ(dest_value->chan(), 0);
   EXPECT_EQ(dest_value->pin(), pin_none);

   nir_src src = nir_src_for_ssa(sum);
   sfn_log << SfnLog::reg << "Search (test) " << &src << "\n";
   PVirtualValue value = factory->src(src, 0);
   EXPECT_EQ(value->sel(), 1024);
   EXPECT_EQ(value->chan(), 0);
   EXPECT_EQ(value->pin(), pin_none);
}

TEST_F(ValuefactoryTest, test_create_register_1)
{
   nir_src src1 = NIR_SRC_INIT;
   src1.reg.reg = nir_local_reg_create(b.impl);
   src1.reg.reg->num_components = 1;

   nir_src src2 = NIR_SRC_INIT;
   src2.reg.reg = nir_local_reg_create(b.impl);
   src2.reg.reg->num_components = 4;
   ASSERT_FALSE(src1.is_ssa);

   factory->allocate_registers(&b.impl->registers);

   auto value = factory->src(src1, 0);
   EXPECT_EQ(value->sel(), 1024);
   EXPECT_EQ(value->chan(), 0);

   for (int i = 0; i < 4; ++i) {
      PVirtualValue value = factory->src(src2, i);
      EXPECT_EQ(value->sel(), 1025);
      EXPECT_EQ(value->chan(), i);
      EXPECT_EQ(value->pin(), pin_none);
   }
}

TEST_F(ValuefactoryTest, test_create_register_array_direct_access)
{
   nir_dest dst = NIR_DEST_INIT;
   dst.reg.reg = nir_local_reg_create(b.impl);
   dst.reg.reg->num_components = 2;
   dst.reg.reg->num_array_elems = 10;

   factory->allocate_registers(&b.impl->registers);

   auto c1 = nir_imm_float(&b, 2.0);

   nir_alu_instr *mov = nir_alu_instr_create(b.shader, nir_op_mov);
   mov->src[0].src = nir_src_for_ssa(c1);
   mov->dest.write_mask = 3;
   mov->dest.dest.is_ssa = false;
   mov->dest.dest.reg.reg = dst.reg.reg;
   mov->dest.dest.reg.base_offset = 5;
   nir_builder_instr_insert(&b, &mov->instr);

   auto regx = factory->dest(mov->dest.dest, 0, pin_none);
   auto regy = factory->dest(mov->dest.dest, 1, pin_none);
   EXPECT_EQ(regx->sel(), 1024 + 5);
   EXPECT_EQ(regx->chan(), 0);
   EXPECT_EQ(regx->pin(), pin_array);

   EXPECT_EQ(regy->sel(), 1024 + 5);
   EXPECT_EQ(regy->chan(), 1);
   EXPECT_EQ(regy->pin(), pin_array);
}

TEST_F(ValuefactoryTest, test_create_register_array_indirect_access)
{
   nir_dest dst = NIR_DEST_INIT;
   dst.reg.reg = nir_local_reg_create(b.impl);
   dst.reg.reg->num_components = 3;
   dst.reg.reg->num_array_elems = 10;

   factory->allocate_registers(&b.impl->registers);

   auto c1 = nir_imm_vec2(&b, 2.0, 4.0);
   auto c2 = nir_imm_int(&b, 3);

   factory->dest(*c2, 0, pin_none);

   nir_alu_instr *mov = nir_alu_instr_create(b.shader, nir_op_mov);
   mov->src[0].src = nir_src_for_ssa(c1);
   mov->dest.write_mask = 3;
   mov->dest.dest.is_ssa = false;
   mov->dest.dest.reg.reg = dst.reg.reg;
   mov->dest.dest.reg.base_offset = 0;
   mov->dest.dest.reg.indirect = (nir_src *)calloc(1, sizeof(nir_src));
   nir_src addr = nir_src_for_ssa(c2);
   nir_src_copy(mov->dest.dest.reg.indirect, &addr, &mov->instr);
   nir_builder_instr_insert(&b, &mov->instr);

   auto addr_reg = factory->src(addr, 0);

   auto regx = factory->dest(mov->dest.dest, 0, pin_none);
   auto regy = factory->dest(mov->dest.dest, 1, pin_none);

   auto regx_addr = regx->get_addr();
   ASSERT_TRUE(regx_addr);

   EXPECT_EQ(regx->sel(), 1024);
   EXPECT_EQ(regx->chan(), 0);
   EXPECT_EQ(*regx_addr, *addr_reg);
   EXPECT_EQ(regx->pin(), pin_array);

   auto regy_addr = regy->get_addr();
   ASSERT_TRUE(regy_addr);

   EXPECT_EQ(regy->sel(), 1024);
   EXPECT_EQ(regy->chan(), 1);
   EXPECT_EQ(*regy_addr, *addr_reg);
   EXPECT_EQ(regy->pin(), pin_array);
}

TEST_F(ValuefactoryTest, test_create_ssa_pinned_chan)
{
   auto c1 = nir_imm_float(&b, 2.0);
   auto c2 = nir_imm_float(&b, 4.0);
   auto sum = nir_fadd(&b, c1, c2);
   auto alu = nir_instr_as_alu(sum->parent_instr);

   auto dest_value = factory->dest(alu->dest, 0, pin_chan);
   EXPECT_EQ(dest_value->sel(), 1024);
   EXPECT_EQ(dest_value->chan(), 0);
   EXPECT_EQ(dest_value->pin(), pin_chan);

   PVirtualValue value = factory->src(nir_src_for_ssa(sum), 0);
   EXPECT_EQ(value->sel(), 1024);
   EXPECT_EQ(value->chan(), 0);
   EXPECT_EQ(value->pin(), pin_chan);
}

TEST_F(ValuefactoryTest, test_create_ssa_pinned_chan_and_reg)
{
   auto c1 = nir_imm_float(&b, 2.0);
   auto c2 = nir_imm_float(&b, 4.0);
   auto sum = nir_fadd(&b, c1, c2);
   auto alu = nir_instr_as_alu(sum->parent_instr);

   auto dest_value = factory->dest(alu->dest, 1, pin_chan);
   EXPECT_EQ(dest_value->sel(), 1024);
   EXPECT_EQ(dest_value->chan(), 1);
   EXPECT_EQ(dest_value->pin(), pin_chan);

   PVirtualValue value = factory->src(nir_src_for_ssa(sum), 1);
   EXPECT_EQ(value->sel(), 1024);
   EXPECT_EQ(value->chan(), 1);
   EXPECT_EQ(value->pin(), pin_chan);
}

TEST_F(ValuefactoryTest, test_create_const)
{
   auto c1 = nir_imm_int(&b, 2);
   auto c2 = nir_imm_int(&b, 4);
   auto sum = nir_iadd(&b, c1, c2);

   auto ci1 = nir_instr_as_load_const(c1->parent_instr);
   factory->allocate_const(ci1);

   auto ci2 = nir_instr_as_load_const(c2->parent_instr);
   factory->allocate_const(ci2);

   auto alu = nir_instr_as_alu(sum->parent_instr);

   PVirtualValue value1 = factory->src(alu->src[0], 0);
   PVirtualValue value2 = factory->src(alu->src[1], 0);

   const auto& cvalue1 = dynamic_cast<const LiteralConstant&>(*value1);
   const auto& cvalue2 = dynamic_cast<const LiteralConstant&>(*value2);

   EXPECT_EQ(cvalue1.value(), 2);
   EXPECT_EQ(cvalue2.value(), 4);
}

TEST_F(ValuefactoryTest, test_create_sysvalue)
{
   auto ic = factory->inline_const(ALU_SRC_TIME_LO, 0);

   EXPECT_EQ(ic->sel(), ALU_SRC_TIME_LO);
   EXPECT_EQ(ic->chan(), 0);
}

class GetKCache : public ConstRegisterVisitor {
public:
   void visit(const VirtualValue& value) { (void)value; }
   void visit(const Register& value) { (void)value; };
   void visit(const LocalArray& value) { (void)value; }
   void visit(const LocalArrayValue& value) { (void)value; }
   void visit(const UniformValue& value)
   {
      (void)value;
      m_result = value.kcache_bank();
   }
   void visit(const LiteralConstant& value) { (void)value; }
   void visit(const InlineConstant& value) { (void)value; }

   GetKCache():
       m_result(0)
   {
   }

   int m_result;
};

ValuefactoryTest::ValuefactoryTest()
{
   memset(&options, 0, sizeof(options));
   init_pool();
}

void
ValuefactoryTest::SetUp()
{
   glsl_type_singleton_init_or_ref();
   b = nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT, &options, "test shader");
   factory = new ValueFactory();
}

void
ValuefactoryTest::TearDown()
{
   ralloc_free(b.shader);
   glsl_type_singleton_decref();
   release_pool();
}
