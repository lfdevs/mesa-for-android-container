-- BFI seems available on Gfx9, need to fix the emission code for that.
if devinfo.verx10 < 110 then
  error("BFI instruction requires Gfx11+")
end

function BFI_simulation(a, b, c, d)
  local width  = a & 0x1F
  local offset = b & 0x1F
  local mask   = ((1 << width) - 1) << offset
  return ((c << offset) & mask) | (d & ~mask)
end

local buf = alloc(16, { fill = 0 })

function BFI(a, b, c, d)
  buf:set({ [0] = a, b, c, d })

  execute [[
      @param autoswsb

      @id   r9
      @addr r9  buf0 r9

      @addr r11 buf0 0
      @addr r12 buf0 1
      @addr r13 buf0 2
      @addr r14 buf0 3

      @load r2 r11
      @load r3 r12
      @load r4 r13
      @load r5 r14

      bfi1 (8) r6      r2      r3
      bfi2 (8) r7      r6      r4      r5

      @store r9 r7
      @eot
  ]]
  return buf[0]
end

function Hex(v) return string.format("0x%08x", v) end

local a, b, c, d = 12, 12, 0xAAAAAAAA, 0xBBBBBBBB

print("calculated", Hex(BFI(a, b, c, d)))
print("expected",   Hex(BFI_simulation(a, b, c, d)))
