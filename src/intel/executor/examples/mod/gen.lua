-- Copyright © 2025 Intel Corporation
-- SPDX-License-Identifier: MIT

local M = {}

local GRF_SLOTS = devinfo.ver >= 20 and 16 or 8

local mov_ub = function(grf, values)
  local s = ""
  for i = 1, #values, 4 do
    local b0 = values[i] or 0
    local b1 = values[i+1] or 0
    local b2 = values[i+2] or 0
    local b3 = values[i+3] or 0
    local packed = b0 + (b1 * 2^8) + (b2 * 2^16) + (b3 * 2^24)
    local dword_idx = (i-1) // 4
    local nr = dword_idx // GRF_SLOTS
    local off = dword_idx % GRF_SLOTS
    s = s .. string.format("mov (1) r%d.%d 0x%x {A@1}\n", grf + nr, off, packed)
  end
  return s
end

M.mov_grf = function(fmt, grf, values)
  if fmt == "ub" then
    return mov_ub(grf, values)
  end

  local packing = 0
  local dst_type = fmt
  local imm_type = fmt
  if     fmt == "hf" then packing = 2
  elseif fmt == "bf" then packing = 2; dst_type = "uw"; imm_type = "uw"
  elseif fmt == "f"  then packing = 1
  elseif fmt == "ud" then packing = 1
  else
    error("unsupported format")
  end

  local s = ""

  for i, v in ipairs(values) do
    nr = (i-1) // (GRF_SLOTS * packing)
    off = (i-1) % (GRF_SLOTS * packing)
    s = s .. string.format("mov (1) r%d.%d:%s 0x%x:%s {A@1}\n",
                           grf + nr, off, dst_type, v, imm_type)
  end

  return s
end

M.write_grfs = function(grf, count)
  local s = "@id  r126\n"
  local width = devinfo.ver >= 20 and 16 or 8
  for i = 1, count do
    s = s .. string.format([[
    @write   r126       r%d
    add (%d) r126 r126 %d {A@1}
    ]], grf + i - 1, width, GRF_SLOTS)
  end
  return s
end

return M
