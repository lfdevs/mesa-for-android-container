-- SPDX-License-Identifier: MIT
--
-- Parse cmdstream dump and produce json for A2D copies/blits.

--local posix = require "posix"

function printf(fmt, ...)
	return io.stderr:write(string.format(fmt, ...))
end

function dbg(fmt, ...)
	--printf(fmt, ...)
end

local r = rnn.init("a750")

local draws = 0

function bool_str(cnd)
	if cnd then
		return "true"
	else
		return "false"
	end
end

function print_resource(name, format, tile_mode, flag, color_swap, mutable)
	printf("\t\"%s\": {\n", name)
	printf("\t\t\"format\": \"%s\",\n", format)
	printf("\t\t\"tile_mode\": \"%s\",\n", tile_mode)
	printf("\t\t\"ubwc\": %s,\n", bool_str(flag))
	printf("\t\t\"mutable\": %s,\n", bool_str(mutable))
	printf("\t\t\"swap\": \"%s\"\n", color_swap)
	printf("\t}\n")
end

function finish()
	printf("]\n")
end

local mode = ""
function CP_SET_MARKER(pkt, size)
	mode = pkt[0].MODE
end

local last_tex_const = {}
function A6XX_TEX_CONST(pkt, size)
	last_tex_const.format = pkt[0].FMT
	last_tex_const.tile_mode = pkt[0].TILE_MODE
	last_tex_const.swap = pkt[0].SWAP
	last_tex_const.mutable = pkt[1].MUTABLEEN
	last_tex_const.samples = pkt[0].SAMPLES
	last_tex_const.flag = pkt[3].FLAGS
	last_tex_const.base = pkt[4].BASE_LO | (pkt[5].BASE_HI << 32)
	-- TODO: Track Flag buffer?
	last_tex_const.flag_base = pkt[7].FLAG_LO | (pkt[8].FLAG_HI << 32)

	assert(tostring(pkt[0].SWIZ_X)=="A6XX_TEX_X", "SWIZ_X not X")
	assert(tostring(pkt[0].SWIZ_Y)=="A6XX_TEX_Y", "SWIZ_X not Y")
	assert(tostring(pkt[0].SWIZ_Z)=="A6XX_TEX_Z", "SWIZ_X not Z")
	assert(tostring(pkt[0].SWIZ_W)=="A6XX_TEX_W", "SWIZ_X not W")
end

function draw(primtype, nindx)
	if (primtype ~= "BLIT_OP_SCALE" and tostring(mode) == "RM6_BLIT2DSCALE") then
		draws = draws + 1
		-- 3D Blit
		return
	elseif primtype ~= "BLIT_OP_SCALE" then
		draws = draws + 1
		return
	end

	if (draws == 0) then
		printf("[\n{\n")
	else
		printf(",{\n")
	end
	draws = draws + 1

	-- blob sometimes uses CP_BLIT for resolves, so filter those out:
	-- TODO it would be nice to not hard-code GMEM addr:
	-- TODO I guess the src can be an offset from GMEM addr..
	if r.TPL1_A2D_SRC_TEXTURE_BASE == 0x100000 and not r.RB_A2D_BLT_CNTL.SOLID_COLOR then
		return
	end

	if r.RB_A2D_BLT_CNTL.SOLID_COLOR then
		return
	end

	printf("\t\"raw_copy\": %s,\n", bool_str(r.TPL1_A2D_BLT_CNTL.RAW_COPY))
	printf("\t\"copy_format\": \"%s\",\n", r.SP_A2D_OUTPUT_INFO.COLOR_FORMAT)
	printf("\t\"half_precision\": %s,\n", bool_str(r.SP_A2D_OUTPUT_INFO.HALF_PRECISION))
	printf("\t\"mask\": %d,\n", r.SP_A2D_OUTPUT_INFO.MASK)
	printf("\t\"blt_cntl_type\": \"%s\",\n", r.TPL1_A2D_BLT_CNTL.TYPE)

	if r.TPL1_A2D_BLT_CNTL.RAW_COPY then
		assert(r.GRAS_A2D_BLT_CNTL.COPY, "Expected GRAS_A2D_BLT_CNTL.COPY to be set")
		assert(r.RB_A2D_BLT_CNTL.COPY, "Expected GRAS_A2D_BLT_CNTL.COPY to be set")
	else
	end

	print_resource("src",
		       r.TPL1_A2D_SRC_TEXTURE_INFO.COLOR_FORMAT,
		       r.TPL1_A2D_SRC_TEXTURE_INFO.TILE_MODE,
		       r.TPL1_A2D_SRC_TEXTURE_INFO.FLAGS,
		       r.TPL1_A2D_SRC_TEXTURE_INFO.COLOR_SWAP,
		       r.TPL1_A2D_SRC_TEXTURE_INFO.MUTABLEEN)
	assert(tostring(r.TPL1_A2D_SRC_TEXTURE_INFO.SAMPLES)=="MSAA_ONE",
	       "A2D only supports MSAA_ONE")
	printf(",")

	print_resource("dst",
		       r.RB_A2D_DEST_BUFFER_INFO.COLOR_FORMAT,
		       r.RB_A2D_DEST_BUFFER_INFO.TILE_MODE,
		       r.RB_A2D_DEST_BUFFER_INFO.FLAGS,
		       r.RB_A2D_DEST_BUFFER_INFO.COLOR_SWAP,
		       r.RB_A2D_DEST_BUFFER_INFO.MUTABLEEN)
	assert(tostring(r.RB_A2D_DEST_BUFFER_INFO.SAMPLES)=="MSAA_ONE",
	       "A2D only supports MSAA_ONE")

	printf("}\n")
end
