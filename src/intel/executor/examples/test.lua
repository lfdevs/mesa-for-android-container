local data = {}
for i = 0, 8-1 do
  data[i] = i * 4
end

local r = execute {
  data = data,
  src = [[
    @id    r1
    @read  r3 r1

    add (8) r3 r3 0x100

    @write r1 r3

    @eot
  ]],
}

dump(r, 8)
