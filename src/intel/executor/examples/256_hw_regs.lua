local data = {}
for i = 0, 15 do
  data[i] = i * 4
end

local buf = alloc(data)

execute [[
    @param autoswsb
    @param hw_regs 256

    @id    r2
    @addr  r3 buf0 r2
    @load  r240 r3

    add (8) r240 r240 0x100

    @store r3 r240

    @eot
]]

dump(buf, 8)
