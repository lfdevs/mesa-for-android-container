local data = {}
for i = 0, 15 do
  data[i] = i * 4
end

local buf = alloc(data)

execute [[
    @param autoswsb

    @id    r2
    @addr  r3 buf0 r2
    @load  r4 r3

    add (8) r4 r4 0x100

    @store r3 r4

    @eot
]]

dump(buf, 8)
