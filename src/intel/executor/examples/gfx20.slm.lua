-- Reserve SLM on Xe2+, synchronize several hardware threads, and use the
-- shared data to reverse one workgroup's lane IDs.

if devinfo.verx10 < 200 then
  error("Requires Xe2+")
end

local total_lanes = 4 * 16

local out = alloc(total_lanes, "out")
out:fill(0)

execute([[
  @param hw_threads 4
  @param simd       16
  @param slm_size   4096

  @id       r4
  shl (16)  r6  r4  0x2 {A@1}
  store.slm.d32.a32 (16) r6:1 r4:1 {A@1,$1}
  @syncnop

  @barrier

  // Read a different address
  mov (16) r8 63 {A@1}
  add (16) r8 r8 -r4 {A@1}
  shl (16) r8 r8 0x2 {A@1}
  load.slm.d32.a32 (16) r9:1 r8:1 {A@1,$1}
  @syncnop

  @addr   r10 out r4
  @store  r10 r9
  @eot
]])

local expected = {}
for i = 0, total_lanes - 1 do
  expected[i] = total_lanes - 1 - i
end

print("result")
dump(out, total_lanes)

print("expected")
dump(expected, total_lanes)

for i = 0, total_lanes - 1 do
  assert(out[i] == expected[i],
         string.format("out[%d]=0x%08x expected 0x%08x",
                       i, out[i], expected[i]))
end

print("OK")
