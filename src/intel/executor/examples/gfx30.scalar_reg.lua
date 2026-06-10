-- Xe3 adds a new ARF to store scalar values.  It supports only a limited
-- set of operations.

if devinfo.ver < 30 then
  error("Scalar register requires Gfx30+")
end

local r = execute {
  src=[[
    // The new scalar register s0 has 32 bytes.
    //
    // Only MOV can have scalar as destination.  Offsets in scalar
    // must be aligned to word boundary.  This non-overlapping MOVs
    // will fill the first 16 bytes.

    mov (1)  s0.0:uw   0x1111:uw

    mov (1)  r2:uw     0x2222:uw
    mov (1)  s0.2:uw   r2<0>:uw      {I@1}

    mov (1)  s0.4:ud   0x33333333:ud {I@1}

    mov (4)  r4:uw     0x4444:uw
    mov (1)  s0.8:uq   r4<0>:uq      {I@1}


    // Scalar content can be broadcasted back into GRFs.

    // Scalar subregister numbers are byte offsets in gen syntax.
    mov (16) r20:ud    s0.0<0>:ud
    mov (16) r32:ud    s0.4<0>:ud
    mov (16) r96:ud    s0.8<0>:ud
    mov (16) r64:ud    s0.12<0>:ud


    // Scalar can store a list of register numbers to be used as source for
    // SEND.  Note the scalar will contain the hex value for r11 and r20.
    // And the scalar register is indexed as bytes in the send source.

    mov (16) r11:ud    0x30000000:ud
    mov (1)  s0.16:ud  0x0000140b:ud {I@1}

    send.ugm (16) null r[s0.16] null:0 0x00000000 0x04000504 {I@1,$1}


    // A larger SEND, note that registers for the payload don't need to be
    // contiguous, the hardware will gather them together.

    add (16) r11:ud    r11:ud 0x4:ud {A@1,$1.src}
    mov (1)  s0.16:ud  0x4060200b:ud

    send.ugm (16) null r[s0.16] null:0 0x00000000 0x08002504 {I@1,$1}

    @eot
  ]]
}

expected = {[0] = 0x22221111, 0x33333333, 0x44444444, 0x44444444}

print("result")
dump(r, 4)

print("expected")
dump(expected, 4)

for i=0,3 do
  if r[i] ~= expected[i] then
    print("FAIL")
    return
  end
end

print("OK")
