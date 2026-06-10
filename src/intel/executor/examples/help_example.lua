-- Example from the help message.

local r = execute {
  data={ [42] = 0x100 },
  src=[[
    @mov     r1      42
    @read    r2      r1

    @id      r3

    add (8)  r4      r2      r3      {A@1}

    @write   r3       r4
    @eot
  ]]
}

dump(r, 4)
