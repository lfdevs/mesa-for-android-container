#include "sfn_test_shaders.h"

#include "../sfn_optimizer.h"
#include "../sfn_ra.h"
#include "../sfn_scheduler.h"
#include "../sfn_shader.h"
#include "../sfn_split_address_loads.h"

using namespace r600;
using std::ostringstream;

TEST_F(TestShaderFromNir, CombineRegisterToTexSrc)
{
   const char *shader_input =
      R"(VS
CHIPCLASS EVERGREEN
INPUT LOC:0
INPUT LOC:1
OUTPUT LOC:0 VARYING_SLOT:0 MASK:15
OUTPUT LOC:1 VARYING_SLOT:32 MASK:3
REGISTERS R1.xyzw R2.xyzw R6.y R7.x R8.y R9.x R10.y
ARRAYS A3[2].zw
SHADER
BLOCK_START
ALU MOV R10.y@free : R2.x@fully{sb} {W}
ALU MOV R9.x@free : R2.y@fully{sb} {W}
ALU MOV R8.y@free : R2.z@fully{sb} {W}
ALU MOV R7.x@free : R2.w@fully{sb} {W}
ALU MOV R6.y@free : I[0] {W}
LOOP_BEGIN
BLOCK_END
BLOCK_START
IF (( ALU PRED_SETGE_INT __.x : R6.y@free KC0[0].x {LEP} PUSH_BEFORE ))
BLOCK_END
BLOCK_START
BREAK
BLOCK_END
BLOCK_START
ENDIF
BLOCK_END
BLOCK_START
ALU INT_TO_FLT CLAMP S22.y@free{s} : R6.y@free {W}
ALU TRUNC S24.w@free{s} : S22.y@free{s} {W}
ALU FLT_TO_INT S25.x@free{s} : S24.w@free{s} {W}
ALU MOV A3[S25.x@free].z : R10.y@free {W}
ALU MOV A3[S25.x@free].w : R9.x@free {W}
ALU MUL_IEEE R5.x@free : R9.x@free I[0.5] {W}
ALU MUL_IEEE R9.x@free : R8.y@free I[0.5] {W}
ALU MUL_IEEE R8.y@free : R7.x@free I[0.5] {W}
ALU MUL_IEEE R7.x@free : R10.y@free I[0.5] {W}
ALU ADD_INT R6.y@free : R6.y@free I[1] {W}
ALU MOV R10.y@free : R5.x@free {W}
LOOP_END
BLOCK_END
BLOCK_START
ALU ADD S47.z@group{s} : A3[0].z A3[1].z {W}
ALU ADD S47.x@group{s} : A3[0].w A3[1].w {W}
EXPORT_DONE PARAM 0 S47.zx__
EXPORT_DONE POS 0 R1.xyzw
BLOCK_END)";

   auto sh = from_string(shader_input);
   split_address_loads(*sh);
   schedule(sh);
}
