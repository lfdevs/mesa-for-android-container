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

    mov(1)   s0<1>UW     0x1111UW;

    mov(1)   r2<1>UW     0x2222UW;
    mov(1)   s0.1<1>UW   r2<0,1,0>UW  {I@1};

    mov(1)   s0.1<1>UD   0x33333333UD {I@1};

    mov(4)   r4<1>UW     0x4444UW;
    mov(1)   s0.1<1>UQ   r4<0,1,0>UQ {I@1};


    // Scalar content can be broadcasted back into GRFs.

    mov(16)  r20<1>UD    s0.0<0,1,0>UD;
    mov(16)  r32<1>UD    s0.1<0,1,0>UD;
    mov(16)  r96<1>UD    s0.2<0,1,0>UD;
    mov(16)  r64<1>UD    s0.3<0,1,0>UD;


    // Scalar can store a list of register numbers to be used as source for
    // SEND.  Note the scalar will contain the hex value for r11 and r20.
    // And the scalar register is indexed as bytes in r[...] syntax, unlike
    // in the MOV.

    mov(16)  r11<1>UD    0x30000000UD;
    mov(1)   s0.4<1>UD   0x0000140bUD;

    send(16) nullUD      r[s0.16]       0x04000504      0x00000000   ugm MsgDesc: () { I@1 $1 };


    // A larger SEND, note that registers for the payload don't need to be
    // contiguous, the hardware will gather them together.

    add(16)  r11<1>UD    r11<1,1,0>UD  0x4UD     {A@1, $1.src};
    mov(1)   s0.4<1>UD   0x4060200bUD;

    send(16) nullUD      r[s0.16]       0x08002504      0x00000000   ugm MsgDesc: ( ) { I@1 $1 };

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
