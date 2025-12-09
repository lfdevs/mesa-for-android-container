--[[

Execute the example from the Dot Product 4 Accumulate
instruction as seen in the PRM.

    mov (1) r1.0:d 0x0102037F:d
    // (char4)(0x1,0x2,0x3,0x7F)
    mov (1) r2.0:d 50:d
    dp4a (1) r3.0:d r2:d r1:d r1:d
    // r3.0 = 50 + (0x1*0x1 + 0x2*0x2 + 0x3*0x3 + 0x7F*0x7F)
    // = 50 + (1 + 4 + 9 + 16129)
    // = 16193

--]]

if devinfo.ver < 12 then
  error("DP4A instruction requires Gfx12+")
end

function DP4A(a, b, c)
  local r = c
  for i = 1, 4 do
    r = r + a[i] * b[i]
  end
  return r
end

local r = execute {
  src = [[
    @id   g9

    @mov  g1  0x0102037F
    @mov  g2  50

    dp4a(8)  g3<1>UD  g2<8,8,1>UD  g1<8,8,1>UD  g1<8,8,1>UD  { align1 @1 1Q };

    @write g9 g3
    @eot
  ]],
}

print("expected", DP4A({1,2,3,0x7F}, {1,2,3,0x7F}, 50))
print("calculated", r[0])
