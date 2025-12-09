if devinfo.ver < 20 then
  error("SRND instruction requires Gfx20+")
end

local r = execute {
  src = [[
    @id      g1

    // Prepare F32 input data in g2
    mov(8)   g2<1>UD    0x00000000UD    {A@1};   // 0.0f
    mov(8)   g2.1<1>UD  0x80000000UD    {A@1};   // -0.0f
    mov(8)   g2.2<1>UD  0x7f7fffffUD    {A@1};   // FLT_MAX
    mov(8)   g2.3<1>UD  0xff7fffffUD    {A@1};   // -FLT_MAX
    mov(8)   g2.4<1>UD  0x00800000UD    {A@1};   // smallest normal
    mov(8)   g2.5<1>UD  0x7fc00000UD    {A@1};   // NaN
    mov(8)   g2.6<1>UD  0x7f800000UD    {A@1};   // +inf
    mov(8)   g2.7<1>UD  0xff800000UD    {A@1};   // -inf

    mov(8)   g3<2>UW   42UW                      {A@1};

    // Stochastic rounding: F32 -> HF16 using g3 as random, packed
    srnd(8)  g4<2>HF   g2<1,1,0>F   g3<1,1,0>F   {nomask};

    // Convert back to F32 for checking, using supported regioning
    mov(8)   g5<1>F    g4<2,1,0>HF               {A@1};

    @write   g1        g5

    @eot
  ]],
}

print("result")
dump(r, 8)

print("expected")
expected = {
  [0] = 0x00000000,
        0x80000000,
        0x7f800000, -- FLT_MAX rounds to +inf in HF16
        0xff800000, -- -FLT_MAX rounds to -inf in HF16
        0x00000000, -- smallest F32 normal rounds to zero in HF16
        0x7fc00000,
        0x7f800000,
        0xff800000
}

dump(expected, 8)

for i=0,7 do
  if r[i] ~= expected[i] then
    print("FAIL at index", i, string.format("got 0x%08x expected 0x%08x", r[i], expected[i]))
    return
  end
end

print("OK")
