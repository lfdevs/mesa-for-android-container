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
    @id   r9

    @mov  r1  0x0102037F
    @mov  r2  50

    dp4a (8) r3      r2      r1      r1      {A@1}

    @write r9 r3
    @eot
  ]],
}

print("expected", DP4A({1,2,3,0x7F}, {1,2,3,0x7F}, 50))
print("calculated", r[0])
