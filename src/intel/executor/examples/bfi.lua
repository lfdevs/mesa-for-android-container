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
      @id   g9
      @mov  g11  0
      @mov  g12  1
      @mov  g13  2
      @mov  g14  3

      @read g1 g11
      @read g2 g12
      @read g3 g13
      @read g4 g14

      bfi1(8)  g5<1>UD  g1<8,8,1>UD  g2<8,8,1>UD               { align1 @1 1Q };
      bfi2(8)  g6<1>UD  g5<8,8,1>UD  g3<8,8,1>UD  g4<8,8,1>UD  { align1 @1 1Q };

      @write g9 g6
      @eot
    ]],
  }
  return r[0]
end

function Hex(v) return string.format("0x%08x", v) end

local a, b, c, d = 12, 12, 0xAAAAAAAA, 0xBBBBBBBB

print("calculated", Hex(BFI(a, b, c, d)))
print("expected",   Hex(BFI_simulation(a, b, c, d)))
