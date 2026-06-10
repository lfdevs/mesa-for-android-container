if not devinfo.has_bfloat16 then
  error("BF16 not supported in this platform.")
end

local r = execute {
  src=[[
    @id      r3

    mov (8)   r4:f      r3                         {A@1}
    mov (8)   r5:f      r4.1<0>:f                  {A@1}

    // Converting F to unpacked BF works, but as will be
    // illustrated, is not very useful.

    mov (8)   r10<2>:bf r4:f                       {A@1}

    // With exception of DPAS, instructions need to have at
    // least one non-BF operand and the operands must be packed.

    mov (8)   r11:uw    r10<2>:uw                  {A@1} // Pack it!
    add (8)   r12:bf    r11:bf      r4:f           {A@1}

    // Converting F to packed BF doesn't work, so add the value
    // to -0.0f instead.  This will preserve the NaN.  Note +0.0f
    // would not work since it doesn't preserve -0.0f!

    mov (8)   r20       0x80000000                 {A@1} // -0.0f.
    add (8)   r21:bf    r4:f        r20:f          {A@1} // F -> BF.

    // Converting BF to F doesn't work, so for a packed source,
    // shift-left the bits to expand it into an UD instead.

    shl (8)   r30       r21:uw       16:uw         {A@1} // BF -> F.

    mad (8)   r40:bf    r12:bf      r21:bf   r5:f  {A@1}
    add (8)   r41:bf    r40:bf      r30:f          {A@1}

    shl (8)   r42       r41:uw       16:uw         {A@1} // BF -> F.

    mov (8)   r43       r42:f                      {A@1}
    @write   r3        r43

    @eot
  ]]
}

expected = {[0] = 0, 4, 8, 12, 16, 20, 24, 28}

print("result")
dump(r, 8)

print("expected")
dump(expected, 8)

for i=0,7 do
  if r[i] ~= expected[i] then
    print("FAIL")
    return
  end
end

print("OK")
