/* Copyright (C) 2015 Broadcom
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

#ifndef _NIR_BUILDER_OPCODES_
#define _NIR_BUILDER_OPCODES_



static inline nir_ssa_def *
nir_b2f(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_b2f, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_b2i(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_b2i, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_ball_fequal2(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_ball_fequal2, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_ball_fequal3(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_ball_fequal3, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_ball_fequal4(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_ball_fequal4, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_ball_iequal2(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_ball_iequal2, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_ball_iequal3(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_ball_iequal3, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_ball_iequal4(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_ball_iequal4, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_bany_fnequal2(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_bany_fnequal2, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_bany_fnequal3(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_bany_fnequal3, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_bany_fnequal4(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_bany_fnequal4, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_bany_inequal2(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_bany_inequal2, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_bany_inequal3(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_bany_inequal3, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_bany_inequal4(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_bany_inequal4, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_bcsel(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1, nir_ssa_def *src2)
{
   return nir_build_alu(build, nir_op_bcsel, src0, src1, src2, NULL);
}
static inline nir_ssa_def *
nir_bfi(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1, nir_ssa_def *src2)
{
   return nir_build_alu(build, nir_op_bfi, src0, src1, src2, NULL);
}
static inline nir_ssa_def *
nir_bfm(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_bfm, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_bit_count(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_bit_count, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_bitfield_insert(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1, nir_ssa_def *src2, nir_ssa_def *src3)
{
   return nir_build_alu(build, nir_op_bitfield_insert, src0, src1, src2, src3);
}
static inline nir_ssa_def *
nir_bitfield_reverse(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_bitfield_reverse, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_d2b(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_d2b, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_d2f(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_d2f, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_d2i(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_d2i, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_d2u(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_d2u, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_extract_i16(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_extract_i16, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_extract_i8(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_extract_i8, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_extract_u16(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_extract_u16, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_extract_u8(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_extract_u8, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_f2b(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_f2b, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_f2d(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_f2d, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_f2i(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_f2i, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_f2u(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_f2u, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_fabs(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_fabs, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_fadd(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_fadd, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_fall_equal2(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_fall_equal2, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_fall_equal3(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_fall_equal3, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_fall_equal4(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_fall_equal4, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_fand(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_fand, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_fany_nequal2(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_fany_nequal2, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_fany_nequal3(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_fany_nequal3, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_fany_nequal4(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_fany_nequal4, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_fceil(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_fceil, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_fcos(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_fcos, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_fcsel(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1, nir_ssa_def *src2)
{
   return nir_build_alu(build, nir_op_fcsel, src0, src1, src2, NULL);
}
static inline nir_ssa_def *
nir_fddx(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_fddx, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_fddx_coarse(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_fddx_coarse, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_fddx_fine(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_fddx_fine, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_fddy(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_fddy, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_fddy_coarse(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_fddy_coarse, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_fddy_fine(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_fddy_fine, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_fdiv(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_fdiv, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_fdot2(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_fdot2, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_fdot3(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_fdot3, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_fdot4(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_fdot4, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_fdot_replicated2(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_fdot_replicated2, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_fdot_replicated3(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_fdot_replicated3, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_fdot_replicated4(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_fdot_replicated4, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_fdph(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_fdph, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_fdph_replicated(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_fdph_replicated, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_feq(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_feq, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_fexp2(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_fexp2, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_ffloor(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_ffloor, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_ffma(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1, nir_ssa_def *src2)
{
   return nir_build_alu(build, nir_op_ffma, src0, src1, src2, NULL);
}
static inline nir_ssa_def *
nir_ffract(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_ffract, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_fge(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_fge, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_find_lsb(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_find_lsb, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_flog2(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_flog2, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_flrp(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1, nir_ssa_def *src2)
{
   return nir_build_alu(build, nir_op_flrp, src0, src1, src2, NULL);
}
static inline nir_ssa_def *
nir_flt(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_flt, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_fmax(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_fmax, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_fmin(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_fmin, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_fmod(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_fmod, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_fmov(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_fmov, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_fmul(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_fmul, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_fne(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_fne, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_fneg(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_fneg, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_fnoise1_1(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_fnoise1_1, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_fnoise1_2(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_fnoise1_2, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_fnoise1_3(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_fnoise1_3, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_fnoise1_4(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_fnoise1_4, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_fnoise2_1(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_fnoise2_1, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_fnoise2_2(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_fnoise2_2, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_fnoise2_3(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_fnoise2_3, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_fnoise2_4(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_fnoise2_4, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_fnoise3_1(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_fnoise3_1, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_fnoise3_2(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_fnoise3_2, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_fnoise3_3(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_fnoise3_3, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_fnoise3_4(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_fnoise3_4, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_fnoise4_1(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_fnoise4_1, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_fnoise4_2(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_fnoise4_2, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_fnoise4_3(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_fnoise4_3, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_fnoise4_4(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_fnoise4_4, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_fnot(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_fnot, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_for(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_for, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_fpow(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_fpow, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_fquantize2f16(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_fquantize2f16, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_frcp(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_frcp, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_frem(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_frem, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_fround_even(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_fround_even, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_frsq(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_frsq, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_fsat(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_fsat, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_fsign(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_fsign, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_fsin(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_fsin, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_fsqrt(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_fsqrt, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_fsub(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_fsub, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_ftrunc(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_ftrunc, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_fxor(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_fxor, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_i2b(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_i2b, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_i2d(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_i2d, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_i2f(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_i2f, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_iabs(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_iabs, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_iadd(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_iadd, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_iand(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_iand, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_ibfe(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1, nir_ssa_def *src2)
{
   return nir_build_alu(build, nir_op_ibfe, src0, src1, src2, NULL);
}
static inline nir_ssa_def *
nir_ibitfield_extract(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1, nir_ssa_def *src2)
{
   return nir_build_alu(build, nir_op_ibitfield_extract, src0, src1, src2, NULL);
}
static inline nir_ssa_def *
nir_idiv(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_idiv, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_ieq(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_ieq, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_ifind_msb(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_ifind_msb, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_ige(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_ige, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_ilt(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_ilt, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_imax(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_imax, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_imin(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_imin, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_imod(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_imod, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_imov(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_imov, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_imul(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_imul, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_imul_high(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_imul_high, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_ine(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_ine, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_ineg(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_ineg, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_inot(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_inot, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_ior(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_ior, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_irem(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_irem, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_ishl(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_ishl, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_ishr(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_ishr, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_isign(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_isign, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_isub(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_isub, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_ixor(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_ixor, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_ldexp(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_ldexp, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_pack_double_2x32(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_pack_double_2x32, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_pack_double_2x32_split(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_pack_double_2x32_split, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_pack_half_2x16(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_pack_half_2x16, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_pack_half_2x16_split(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_pack_half_2x16_split, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_pack_snorm_2x16(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_pack_snorm_2x16, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_pack_snorm_4x8(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_pack_snorm_4x8, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_pack_unorm_2x16(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_pack_unorm_2x16, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_pack_unorm_4x8(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_pack_unorm_4x8, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_pack_uvec2_to_uint(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_pack_uvec2_to_uint, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_pack_uvec4_to_uint(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_pack_uvec4_to_uint, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_seq(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_seq, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_sge(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_sge, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_slt(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_slt, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_sne(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_sne, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_u2d(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_u2d, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_u2f(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_u2f, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_uadd_carry(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_uadd_carry, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_ubfe(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1, nir_ssa_def *src2)
{
   return nir_build_alu(build, nir_op_ubfe, src0, src1, src2, NULL);
}
static inline nir_ssa_def *
nir_ubitfield_extract(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1, nir_ssa_def *src2)
{
   return nir_build_alu(build, nir_op_ubitfield_extract, src0, src1, src2, NULL);
}
static inline nir_ssa_def *
nir_udiv(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_udiv, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_ufind_msb(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_ufind_msb, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_uge(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_uge, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_ult(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_ult, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_umax(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_umax, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_umax_4x8(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_umax_4x8, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_umin(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_umin, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_umin_4x8(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_umin_4x8, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_umod(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_umod, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_umul_high(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_umul_high, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_umul_unorm_4x8(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_umul_unorm_4x8, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_unpack_double_2x32(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_unpack_double_2x32, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_unpack_double_2x32_split_x(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_unpack_double_2x32_split_x, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_unpack_double_2x32_split_y(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_unpack_double_2x32_split_y, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_unpack_half_2x16(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_unpack_half_2x16, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_unpack_half_2x16_split_x(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_unpack_half_2x16_split_x, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_unpack_half_2x16_split_y(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_unpack_half_2x16_split_y, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_unpack_snorm_2x16(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_unpack_snorm_2x16, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_unpack_snorm_4x8(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_unpack_snorm_4x8, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_unpack_unorm_2x16(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_unpack_unorm_2x16, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_unpack_unorm_4x8(nir_builder *build, nir_ssa_def *src0)
{
   return nir_build_alu(build, nir_op_unpack_unorm_4x8, src0, NULL, NULL, NULL);
}
static inline nir_ssa_def *
nir_usadd_4x8(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_usadd_4x8, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_ushr(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_ushr, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_ussub_4x8(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_ussub_4x8, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_usub_borrow(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_usub_borrow, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_vec2(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   return nir_build_alu(build, nir_op_vec2, src0, src1, NULL, NULL);
}
static inline nir_ssa_def *
nir_vec3(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1, nir_ssa_def *src2)
{
   return nir_build_alu(build, nir_op_vec3, src0, src1, src2, NULL);
}
static inline nir_ssa_def *
nir_vec4(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1, nir_ssa_def *src2, nir_ssa_def *src3)
{
   return nir_build_alu(build, nir_op_vec4, src0, src1, src2, src3);
}

#endif /* _NIR_BUILDER_OPCODES_ */
