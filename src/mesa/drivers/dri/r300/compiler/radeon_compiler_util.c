/*
 * Copyright 2010 Tom Stellard <tstellar@gmail.com>
 *
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

/**
 * \file
 */

#include "radeon_compiler_util.h"

#include "radeon_compiler.h"
#include "radeon_dataflow.h"
/**
 */
unsigned int rc_swizzle_to_writemask(unsigned int swz)
{
	unsigned int mask = 0;
	unsigned int i;

	for(i = 0; i < 4; i++) {
		mask |= 1 << GET_SWZ(swz, i);
	}
	mask &= RC_MASK_XYZW;

	return mask;
}

rc_swizzle get_swz(unsigned int swz, rc_swizzle idx)
{
	if (idx & 0x4)
		return idx;
	return GET_SWZ(swz, idx);
}

unsigned int combine_swizzles4(unsigned int src,
		rc_swizzle swz_x, rc_swizzle swz_y, rc_swizzle swz_z, rc_swizzle swz_w)
{
	unsigned int ret = 0;

	ret |= get_swz(src, swz_x);
	ret |= get_swz(src, swz_y) << 3;
	ret |= get_swz(src, swz_z) << 6;
	ret |= get_swz(src, swz_w) << 9;

	return ret;
}

unsigned int combine_swizzles(unsigned int src, unsigned int swz)
{
	unsigned int ret = 0;

	ret |= get_swz(src, GET_SWZ(swz, RC_SWIZZLE_X));
	ret |= get_swz(src, GET_SWZ(swz, RC_SWIZZLE_Y)) << 3;
	ret |= get_swz(src, GET_SWZ(swz, RC_SWIZZLE_Z)) << 6;
	ret |= get_swz(src, GET_SWZ(swz, RC_SWIZZLE_W)) << 9;

	return ret;
}

/**
 * @param mask Must be either RC_MASK_X, RC_MASK_Y, RC_MASK_Z, or RC_MASK_W
 */
rc_swizzle rc_mask_to_swizzle(unsigned int mask)
{
	switch (mask) {
	case RC_MASK_X: return RC_SWIZZLE_X;
	case RC_MASK_Y: return RC_SWIZZLE_Y;
	case RC_MASK_Z: return RC_SWIZZLE_Z;
	case RC_MASK_W: return RC_SWIZZLE_W;
	}
	return RC_SWIZZLE_UNUSED;
}

/* Reorder mask bits according to swizzle. */
unsigned swizzle_mask(unsigned swizzle, unsigned mask)
{
	unsigned ret = 0;
	for (unsigned chan = 0; chan < 4; ++chan) {
		unsigned swz = GET_SWZ(swizzle, chan);
		if (swz < 4)
			ret |= GET_BIT(mask, swz) << chan;
	}
	return ret;
}

/**
 * Left multiplication of a register with a swizzle
 */
struct rc_src_register lmul_swizzle(unsigned int swizzle, struct rc_src_register srcreg)
{
	struct rc_src_register tmp = srcreg;
	int i;
	tmp.Swizzle = 0;
	tmp.Negate = 0;
	for(i = 0; i < 4; ++i) {
		rc_swizzle swz = GET_SWZ(swizzle, i);
		if (swz < 4) {
			tmp.Swizzle |= GET_SWZ(srcreg.Swizzle, swz) << (i*3);
			tmp.Negate |= GET_BIT(srcreg.Negate, swz) << i;
		} else {
			tmp.Swizzle |= swz << (i*3);
		}
	}
	return tmp;
}

void reset_srcreg(struct rc_src_register* reg)
{
	memset(reg, 0, sizeof(struct rc_src_register));
	reg->Swizzle = RC_SWIZZLE_XYZW;
}

unsigned int rc_src_reads_dst_mask(
		rc_register_file src_file,
		unsigned int src_idx,
		unsigned int src_swz,
		rc_register_file dst_file,
		unsigned int dst_idx,
		unsigned int dst_mask)
{
	if (src_file != dst_file || src_idx != dst_idx) {
		return RC_MASK_NONE;
	}
	return dst_mask & rc_swizzle_to_writemask(src_swz);
}

unsigned int rc_source_type_swz(unsigned int swizzle, unsigned int channels)
{
	unsigned int chan;
	unsigned int swz = RC_SWIZZLE_UNUSED;
	unsigned int ret = RC_SOURCE_NONE;

	for(chan = 0; chan < channels; chan++) {
		swz = GET_SWZ(swizzle, chan);
		if (swz == RC_SWIZZLE_W) {
			ret |= RC_SOURCE_ALPHA;
		} else if (swz == RC_SWIZZLE_X || swz == RC_SWIZZLE_Y
						|| swz == RC_SWIZZLE_Z) {
			ret |= RC_SOURCE_RGB;
		}
	}
	return ret;
}

unsigned int rc_source_type_mask(unsigned int mask)
{
	unsigned int ret = RC_SOURCE_NONE;

	if (mask & RC_MASK_XYZ)
		ret |= RC_SOURCE_RGB;

	if (mask & RC_MASK_W)
		ret |= RC_SOURCE_ALPHA;

	return ret;
}

struct src_select {
	rc_register_file File;
	int Index;
	unsigned int SrcType;
};

struct can_use_presub_data {
	struct src_select Selects[5];
	unsigned int SelectCount;
	const struct rc_src_register * ReplaceReg;
	unsigned int ReplaceRemoved;
};

static void can_use_presub_data_add_select(
	struct can_use_presub_data * data,
	rc_register_file file,
	unsigned int index,
	unsigned int src_type)
{
	struct src_select * select;

	select = &data->Selects[data->SelectCount++];
	select->File = file;
	select->Index = index;
	select->SrcType = src_type;
}

/**
 * This callback function counts the number of sources in inst that are
 * different from the sources in can_use_presub_data->RemoveSrcs.
 */
static void can_use_presub_read_cb(
	void * userdata,
	struct rc_instruction * inst,
	struct rc_src_register * src)
{
	struct can_use_presub_data * d = userdata;

	if (!d->ReplaceRemoved && src == d->ReplaceReg) {
		d->ReplaceRemoved = 1;
		return;
	}

	if (src->File == RC_FILE_NONE)
		return;

	can_use_presub_data_add_select(d, src->File, src->Index,
					rc_source_type_swz(src->Swizzle, 4));
}

unsigned int rc_inst_can_use_presub(
	struct rc_instruction * inst,
	rc_presubtract_op presub_op,
	unsigned int presub_writemask,
	const struct rc_src_register * replace_reg,
	const struct rc_src_register * presub_src0,
	const struct rc_src_register * presub_src1)
{
	struct can_use_presub_data d;
	unsigned int num_presub_srcs;
	unsigned int i;
	const struct rc_opcode_info * info =
					rc_get_opcode_info(inst->U.I.Opcode);
	int rgb_count = 0, alpha_count = 0;
	unsigned int src_type0, src_type1;

	if (presub_op == RC_PRESUB_NONE) {
		return 1;
	}

	if (info->HasTexture) {
		return 0;
	}

	/* We can't use more than one presubtract value in an
	 * instruction, unless the two prsubtract operations
	 * are the same and read from the same registers.
	 * XXX For now we will limit instructions to only one presubtract
	 * value.*/
	if (inst->U.I.PreSub.Opcode != RC_PRESUB_NONE) {
		return 0;
	}

	memset(&d, 0, sizeof(d));
	d.ReplaceReg = replace_reg;

	rc_for_all_reads_src(inst, can_use_presub_read_cb, &d);

	num_presub_srcs = rc_presubtract_src_reg_count(presub_op);

	src_type0 = rc_source_type_swz(presub_src0->Swizzle, 4);
	can_use_presub_data_add_select(&d,
		presub_src0->File,
		presub_src0->Index,
		src_type0);

	if (num_presub_srcs > 1) {
		src_type1 = rc_source_type_swz(presub_src1->Swizzle, 4);
		can_use_presub_data_add_select(&d,
			presub_src1->File,
			presub_src1->Index,
			src_type1);

		/* Even if both of the presub sources read from the same
		 * register, we still need to use 2 different source selects
		 * for them, so we need to increment the count to compensate.
		 */
		if (presub_src0->File == presub_src1->File
		    && presub_src0->Index == presub_src1->Index) {
			if (src_type0 & src_type1 & RC_SOURCE_RGB) {
				rgb_count++;
			}
			if (src_type0 & src_type1 & RC_SOURCE_ALPHA) {
				alpha_count++;
			}
		}
	}

	/* Count the number of source selects for Alpha and RGB.  If we
	 * encounter two of the same source selects then we can ignore the
	 * first one. */
	for (i = 0; i < d.SelectCount; i++) {
		unsigned int j;
		unsigned int src_type = d.Selects[i].SrcType;
		for (j = i + 1; j < d.SelectCount; j++) {
			if (d.Selects[i].File == d.Selects[j].File
			    && d.Selects[i].Index == d.Selects[j].Index) {
				src_type &= ~d.Selects[j].SrcType;
			}
		}
		if (src_type & RC_SOURCE_RGB) {
			rgb_count++;
		}

		if (src_type & RC_SOURCE_ALPHA) {
			alpha_count++;
		}
	}

	if (rgb_count > 3 || alpha_count > 3) {
		return 0;
	}

	return 1;
}

