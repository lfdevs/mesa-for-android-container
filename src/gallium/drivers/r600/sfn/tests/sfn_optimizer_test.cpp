
#include "../sfn_optimizer.h"
#include "../sfn_ra.h"
#include "../sfn_scheduler.h"
#include "../sfn_shader.h"
#include "sfn_test_shaders.h"

using namespace r600;
using std::ostringstream;

class TestShaderFromNir : public TestShader {

protected:
   void check(Shader *s, const char *expect_str);
   void ra_check(Shader *s, const char *expect_str);
};

TEST_F(TestShaderFromNir, SimpleDCE)
{
   auto sh = from_string(red_triangle_fs_expect_from_nir);
   dead_code_elimination(*sh);

   check(sh, red_triangle_fs_expect_from_nir_dce);
}

TEST_F(TestShaderFromNir, CopyPropagationForwardBackward)
{
   auto sh = from_string(add_add_1_expect_from_nir);
   copy_propagation_fwd(*sh);
   check(sh, add_add_1_expect_from_nir_copy_prop_fwd);
}

TEST_F(TestShaderFromNir, CopyPropagationForwardDCE)
{
   auto sh = from_string(add_add_1_expect_from_nir);
   copy_propagation_fwd(*sh);
   dead_code_elimination(*sh);
   check(sh, add_add_1_expect_from_nir_copy_prop_fwd_dce);
}

TEST_F(TestShaderFromNir, CopyPropagationBackwardDCE)
{
   auto sh = from_string(add_add_1_expect_from_nir_copy_prop_fwd_dce);
   copy_propagation_backward(*sh);
   dead_code_elimination(*sh);
   check(sh, add_add_1_expect_from_nir_copy_prop_fwd_dce_bwd);
}

TEST_F(TestShaderFromNir, FullOPtimize)
{
   auto sh = from_string(basic_interpolation_orig);

   bool progress;

   do {
      progress = false;
      progress |= copy_propagation_fwd(*sh);
      progress |= dead_code_elimination(*sh);
      progress |= copy_propagation_backward(*sh);
      progress |= dead_code_elimination(*sh);
   } while (progress);

   check(sh, basic_interpolation_expect_from_nir_opt);
}

TEST_F(TestShaderFromNir, CombinePinFlags)
{
   auto sh = from_string(shader_group_chan_pin_to_combine);

   bool progress;

   do {
      progress = false;
      progress |= copy_propagation_fwd(*sh);
      progress |= dead_code_elimination(*sh);
      progress |= copy_propagation_backward(*sh);
      progress |= dead_code_elimination(*sh);
   } while (progress);

   check(sh, shader_group_chan_pin_combined);
}

TEST_F(TestShaderFromNir, FullOPtimizeLoop)
{
   auto sh = from_string(vs_nexted_loop_from_nir_expect);

   optimize(*sh);

   check(sh, vs_nexted_loop_from_nir_expect_opt);
}
TEST_F(TestShaderFromNir, OptimizeWithDestArrayValue)
{
   auto sh = from_string(shader_with_dest_array);

   optimize(*sh);

   check(sh, shader_with_dest_array_opt_expect);
}

TEST_F(TestShaderFromNir, ScheduleOPtimizedWithDestArrayValue)
{
   auto sh = from_string(shader_with_dest_array_opt_expect);
   check(schedule(sh), shader_with_dest_array_opt_scheduled);
}

TEST_F(TestShaderFromNir, ScheduleWithArrayWriteAndRead)
{
   auto sh = from_string(shader_with_dest_array2);
   check(schedule(sh), shader_with_dest_array2_scheduled);
}

TEST_F(TestShaderFromNir, RA_with_dest_array)
{
   auto sh = from_string(shader_with_dest_array2_scheduled);

   auto lrm = r600::LiveRangeEvaluator().run(*sh);
   EXPECT_TRUE(r600::register_allocation(lrm));

   ra_check(sh, shader_with_dest_array2_scheduled_ra);
}

TEST_F(TestShaderFromNir, RA_with_chan_group)
{
   auto sh = from_string(shader_group_chan_pin_combined_sheduled);

   auto lrm = r600::LiveRangeEvaluator().run(*sh);
   EXPECT_TRUE(r600::register_allocation(lrm));
   ra_check(sh, shader_group_chan_pin_combined_sheduled_ra);
}

TEST_F(TestShaderFromNir, TES_opt)
{
   auto sh = from_string(tes_pre_op);

   optimize(*sh);

   check(sh, tes_optimized);
}

TEST_F(TestShaderFromNir, TES_scheduled)
{
   auto sh = from_string(tes_optimized_pre_sched);

   check(schedule(sh), tes_optimized_sched);
}

/*
TEST_F(TestShaderFromNir, ShaderClone)
{
   auto sh = from_string(red_triangle_fs_expect_from_nir);

   auto sh_cloned = sh->clone();

   MemoryPool::instance().push();
   dead_code_elimination(*sh);

   check(sh, red_triangle_fs_expect_from_nir_dce);

   check(sh_cloned, red_triangle_fs_expect_from_nir);

   MemoryPool::instance().pop();
   check(sh, red_triangle_fs_expect_from_nir_dce);
}
*/

TEST_F(TestShaderFromNir, ShaderSchedule)
{
   auto sh = from_string(basic_interpolation_orig);

   check(schedule(sh), basic_interpolation_expect_from_nir_sched);
}

TEST_F(TestShaderFromNir, ShaderScheduleCayman)
{
   auto sh = from_string(basic_interpolation_orig_cayman);

   check(schedule(sh), basic_interpolation_expect_from_nir_sched_cayman);
}

TEST_F(TestShaderFromNir, ShaderScheduleOptimizedCayman)
{
   auto sh = from_string(basic_interpolation_orig_cayman);

   optimize(*sh);

   check(schedule(sh), basic_interpolation_expect_opt_sched_cayman);
}

TEST_F(TestShaderFromNir, CopyPropLegalConst)
{
   auto sh = from_string(dot4_pre);

   copy_propagation_fwd(*sh);
   dead_code_elimination(*sh);

   check(sh, dot4_copy_prop_dce);
}

TEST_F(TestShaderFromNir, FullOPtimize_glxgears_vs2)
{

   auto sh = from_string(glxgears_vs2_from_nir_expect);
   optimize(*sh);
   check(sh, glxgears_vs2_from_nir_expect_optimized);
}

TEST_F(TestShaderFromNir, test_schedule_group)
{

   auto sh = from_string(test_schedule_group);
   optimize(*sh);
   check(schedule(sh), test_schedule_group_expect);
}

TEST_F(TestShaderFromNir, test_dont_kill_dual_use)
{
   auto sh = from_string(shader_copy_prop_dont_kill_double_use);
   optimize(*sh);
   check(schedule(sh), shader_copy_prop_dont_kill_double_use_expect);
}

TEST_F(TestShaderFromNir, test_schedule_with_bany)
{

   auto sh = from_string(shader_with_bany_expect_eg);
   optimize(*sh);
   check(schedule(sh), shader_with_bany_expect_opt_sched_eg);
}

TEST_F(TestShaderFromNir, GroupAndChanCombine)
{
   auto sh = from_string(shader_group_chan_pin_to_combine_2);
   optimize(*sh);
   check(sh, shader_group_chan_pin_to_combine_2_opt);
}

TEST_F(TestShaderFromNir, RemoveUseAfterSplitgroup)
{
   auto sh = from_string(fs_with_loop_multislot_reuse);
   check(schedule(sh), fs_with_loop_multislot_reuse_scheduled);
}

TEST_F(TestShaderFromNir, OptimizeVSforTCS)
{
   auto sh = from_string(vtx_for_tcs_inp);
   optimize(*sh);
   check(sh, vtx_for_tcs_opt);
}

TEST_F(TestShaderFromNir, ScheduleVSforTCS)
{
   auto sh = from_string(vtx_for_tcs_pre_sched);
   check(schedule(sh), vtx_for_tcs_sched);
}

TEST_F(TestShaderFromNir, fs_opt_tex_coord)
{
   auto sh = from_string(fs_opt_tex_coord_init);

   optimize(*sh);

   check(sh, fs_opt_tex_coord_expect);
}

TEST_F(TestShaderFromNir, fs_shed_tex_coord)
{
   auto sh = from_string(fs_sched_tex_coord_init);
   check(schedule(sh), fs_sched_tex_coord_expect);
}

void
TestShaderFromNir::check(Shader *s, const char *expect_orig)
{
   ostringstream test_str;
   s->print(test_str);

   auto expect = from_string(expect_orig);

   ostringstream expect_str;
   expect->print(expect_str);

   EXPECT_EQ(test_str.str(), expect_str.str());
}

void
TestShaderFromNir::ra_check(Shader *s, const char *expect_orig)
{
   s->value_factory().clear_pins();
   ostringstream test_str;
   s->print(test_str);

   auto expect = from_string(expect_orig);
   expect->value_factory().clear_pins();

   ostringstream expect_str;
   expect->print(expect_str);

   EXPECT_EQ(test_str.str(), expect_str.str());
}
