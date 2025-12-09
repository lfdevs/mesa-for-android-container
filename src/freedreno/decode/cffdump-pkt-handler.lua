-- SPDX-License-Identifier: MIT

-- `r` is predefined in the environment and is the equivalent of rnn.init(<gpu>)

function CP_REG_RMW(pkt, size)
	local dst_reg		= pkt[0].DST_REG
	local dst_scratch	= pkt[0].DST_SCRATCH
	local rotate		= pkt[0].ROTATE
	local src1_add		= pkt[0].SRC1_ADD
	local src1_is_reg	= pkt[0].SRC1_IS_REG
	local src0_is_reg	= pkt[0].SRC0_IS_REG
	local src0 		= pkt[1].SRC0
	local src1 		= pkt[2].SRC1

	local dst = regs.val(dst_reg)
	local dst_reg_str = string.format("%s", rnn.regname(r, dst_reg))
	if dst_scratch then
		dst_reg_str = string.format("CP_SCRATCH[%s]", dst_reg)
	end

	local src0_str = string.format("0x%08x", src0)
	if src0_is_reg then
		src0_str = string.format("%s", rnn.regname(r, src0))
		src0 = regs.val(src0)
	end

	local src1_str = string.format("0x%08x", src1)
	if src1_is_reg then
		src1_str = string.format("%s", rnn.regname(r, src1))
		src1 = regs.val(src1)
	end

	local result = dst & src0
	result = (result << rotate) | result >> (32 - rotate)

	local op_str
	if src1_add then
		op_str = '+'
		result = result + src1
	else
		op_str = '|'
		result = result | src1
	end

	result = (dst &~ 0xFFFFFFFF) | result & 0xFFFFFFFF

	if dst_scratch then
		io.stderr:write("WARNING: Write to CP_SCRATCH is not emulated.")
	else
		priv.reg_set(dst_reg, result)
	end

	return string.format("%s = ((%s & %s) rot_l %d) %s %s\n",
		             dst_reg_str, dst_reg_str, src0_str, rotate,
		             op_str, src1_str)
end
