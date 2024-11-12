local data = {}
for i = 0, 8-1 do
  data[i] = i * 4
end

local r = execute {
  data = data,
  src = [[
    @id    g1
    @read  g3 g1

    add(8) g3<1>UD  g3<8,8,1>UD  0x100UD  { align1 1Q };

    @write g1 g3

    @eot
  ]],
}

dump(r, 8)
