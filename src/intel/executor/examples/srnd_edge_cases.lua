if devinfo.ver < 20 then
  error("SRND instruction requires Gfx20+")
end

local r = execute {
  src = [[
    @id      r1

    // Prepare F32 input data in r2
    mov (8) r2.0 0x00000000 {A@1}   // 0.0f
    mov (8) r2.1 0x80000000 {A@1}   // -0.0f
    mov (8) r2.2 0x7f7fffff {A@1}   // FLT_MAX
    mov (8) r2.3 0xff7fffff {A@1}   // -FLT_MAX
    mov (8) r2.4 0x00800000 {A@1}   // smallest normal
    mov (8) r2.5 0x7fc00000 {A@1}   // NaN
    mov (8) r2.6 0x7f800000 {A@1}   // +inf
    mov (8) r2.7 0xff800000 {A@1}   // -inf

    mov (8) r3<2>:uw 42:uw {A@1}

    // Stochastic rounding: F32 -> HF16 using r3 as random, packed
    (W) srnd (8) r4<2>:hf r2:f r3:f {A@1}

    // Convert back to F32 for checking, using supported regioning
    mov (8) r5:f r4<2>:hf {A@1}

    @write   r1        r5

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
