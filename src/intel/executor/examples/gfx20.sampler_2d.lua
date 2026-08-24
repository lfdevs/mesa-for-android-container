if devinfo.verx10 ~= 200 then
  error("This example is for BMG / Gfx20")
end

local src = alloc(16, { name = "src", align = 4096 })
for i = 0, 15 do
  src[i] = (i + 1) * 10
end

local out = alloc(16, "out")
out:fill(0)

local surf = surface_2d(src, {
  format = "r32uint",
  width = 16,
  height = 1,
})
local samp = sampler()
local desc = sampler_desc{
  op = "sample_l",
  surface = surf,
  sampler = samp,
  simd = 16,
  mlen = 3,
  rlen = 1,
}

execute([[
  @param autoswsb
  @param simd 16

  @id r4

  // sample_l payload for 2D: LOD, U, V.
  mov (16) r6:f 0x00000000:f       // LOD = 0.
  mov (16) r7:f r4:ud              // U texel index = lane ID.
  add (16) r7:f r7:f 0x3f000000:f  // U texel center = lane ID + 0.5.
  mul (16) r7:f r7:f 0x3d800000:f  // Normalized U = (lane ID + 0.5) / 16.
  mov (16) r8:f 0x3f000000:f       // Normalized V = (row 0 + 0.5) / 1.
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
