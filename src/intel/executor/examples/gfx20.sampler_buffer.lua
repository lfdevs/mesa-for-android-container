if devinfo.verx10 ~= 200 then
  error("This example is for BMG / Gfx20")
end

local src = alloc(16, "src")
for i = 0, 15 do
  src[i] = (i + 1) * 10
end

local out = alloc(16, "out")
out:fill(0)

local surf = surface_buffer(src, { format = "r32uint" })
local samp = sampler()
local desc = sampler_desc{
  op = "ld",
  surface = surf,
  sampler = samp,
  simd = 16,
  mlen = 2,
  rlen = 1,
}

execute([[
  @param autoswsb
  @param simd 16

  @id r4

  // ld payload: U coordinate and LOD.
  mov (16) r6:ud r4:ud
  mov (16) r7:ud 0x0:ud
  send.smpl (16) r20 r6 null:0 0x00000000 ]] .. desc .. [[

  @addr  r30 out r4
  @store r30 r20
  @eot
]])

print("result")
dump(out, 16)

print("expected")
dump(src, 16)

for i = 0, 15 do
  if out[i] ~= src[i] then
    print("FAIL")
    return
  end
end

print("OK")
