-- Example from the help message.

local r = execute {
  data={ [42] = 0x100 },
  src=[[
    @mov     g1      42
    @read    g2      g1

    @id      g3

    add(8)   g4<1>UD  g2<8,8,1>UD  g3<8,8,1>UD  { align1 @1 1Q };

    @write   g3       g4
    @eot
  ]]
}

dump(r, 4)
