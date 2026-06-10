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

function BFI(a, b, c, d)
  local r = execute {
    data = { [0] = a, b, c, d },
    src = [[
      @id   r9
      @mov  r11  0
      @mov  r12  1
      @mov  r13  2
      @mov  r14  3

      @read r1 r11
      @read r2 r12
      @read r3 r13
      @read r4 r14

      bfi1 (8) r5      r1      r2              {A@1}
      bfi2 (8) r6      r5      r3      r4      {A@1}

      @write r9 r6
      @eot
    ]],
  }
  return r[0]
end

function Hex(v) return string.format("0x%08x", v) end

local a, b, c, d = 12, 12, 0xAAAAAAAA, 0xBBBBBBBB

print("calculated", Hex(BFI(a, b, c, d)))
print("expected",   Hex(BFI_simulation(a, b, c, d)))
