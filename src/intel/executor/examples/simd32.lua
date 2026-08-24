local buf = alloc(32)
buf:fill(0)

execute [[
  @param autoswsb
  @param simd 32

  @id      r4
  @addr    r8 buf0 r4
  @store   r8 r4
  @eot
]]

local expected = {}
for i = 0, 31 do
  expected[i] = i
end

dump(buf, 32)

for i = 0, 31 do
  assert(buf[i] == expected[i],
         string.format("buf[%d]=0x%08x expected 0x%08x",
                       i, buf[i], expected[i]))
end

print("OK")
