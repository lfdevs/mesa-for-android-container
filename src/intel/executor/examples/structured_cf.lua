-- Illustrates executor parsing of structured control flow without having to
-- spell out JIP/UIP for IF/ELSE/ENDIF.  WHILE still needs an explicit JIP,
-- but the common structured branch labels are inferred automatically.

local src = [[
  @id   r1
  @mov  r2 0
  @mov  r3 0
  @mov  r4 0

LOOP:
  // First conditional: 0 < 1, so this takes the then-path.
  cmp (1) (lt)f0.0 null:d r4<0>:d 1:d {A@1}
  (f0.0) if(1)
    add (1) r2 r2<0> 1
  else(1)
    add (1) r2 r2<0> 2
  endif(1)

  // Second conditional: 0 < 0 is false, so this takes the else-path.
  cmp (1) (lt)f0.0 null:d r4<0>:d 0:d {A@1}
  (f0.0) if(1)
    add (1) r2 r2<0> 4
  else(1)
    add (1) r2 r2<0> 8
  endif(1)

  add (1) r3 r3<0> 1
  cmp (1) (lt)f0.0 null:d r3<0>:d 2:d {A@1}
  // WHILE still needs an explicit loop-start label for its JIP back-edge.
  (f0.0) while(1) jip:LOOP

  @write r1 r2
  @eot
]]

local r = execute { src = src }

local expected = {[0] = 18}

print("result")
dump(r, 1)

print("expected")
dump(expected, 1)

if r[0] ~= expected[0] then
  error(string.format("got %u expected %u", r[0], expected[0]))
end

print("OK")
