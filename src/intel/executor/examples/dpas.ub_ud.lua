if not devinfo.has_dpas then
  error("DPAS not supported in this platform.")
end

local gen = require("mod/gen")
local matrix = require("mod/matrix")

-- Similar to the constants used for VK_KHR_cooperative_matrix.
local M = 8
local N = devinfo.ver >= 20 and 16 or 8
local K = 32

local A = matrix.new(M, K, 1)
local B = matrix.new_diag(K, N, 1)
local C = matrix.new(M, N, 0)

-- Note: when tinkering with values, use set() method to set
-- values, e.g.
--
-- A:set(0, 2, 0x2)

-- Calculate A * B + C.  A and B are UB values, C and the result
-- are UD values.
local buf = execute {
  src =
    [[]]
    .. gen.mov_grf("UB", 10, A:to_row_major())

    -- For `src1`, the source representing the B matrix, DPAS expects
    -- the values to be in a layout that looks like an "interleaved"
    -- row major.  Elements from `packing_factor` rows are packed
    -- together.
    --
    -- See mod/matrix.lua for details on that format.
    --
    .. gen.mov_grf("UB", 20, B:to_interleaved_row_major(4))

    .. gen.mov_grf("UD", 30, C:to_row_major())

    .. (devinfo.ver >= 20 and [[

    dpas.8x8(16)  r40<1>UD  r30<1>UD  r20<1>UB  r10<1>UB  {A@1 $1};
    @syncnop

    ]] or [[

    dpas.8x8(8)  r40<1>UD  r30<1>UD  r20<1>UB  r10<1>UB  {A@1 $1};
    @syncnop

    ]])

    .. gen.write_grfs(40, 8)
    .. [[

    @eot

    ]],
}

r = matrix.from_row_major_buffer(M, N, buf)
r:print()
