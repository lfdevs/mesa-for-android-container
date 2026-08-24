local bufA = alloc(8, { name = "A", fill = 0 })
local bufB = alloc(8, { name = "B", fill = 0 })
local bufC = alloc(8, { name = "C", fill = 0 })

bufA:set({ [1] = 0x40 })
bufB:set({ [2] = 0x5 })

execute [[
  @param autoswsb

  @addr    r4      A 1
  @load    r5      r4

  @addr    r6      B 2
  @load    r7      r6

  @addr    r8      C 1
  @store   r8      r5

  @addr    r9      C 4
  @store   r9      r7
  @eot
]]

local out = bufC:read(8)
for i = 0, 8 - 1 do
  local expected = 0
  if i == 1 then
    expected = 0x40
  elseif i == 4 then
    expected = 0x5
  end
  assert(out[i] == expected, string.format("bufC[%d]=0x%x", i, out[i]))
end

bufC:dump(8)
