-- Illustrates executor parsing of structured control flow without having to
-- spell out JIP/UIP for IF/ELSE/ENDIF.  WHILE still needs an explicit JIP,
-- but the common structured branch labels are inferred automatically.

local buf = alloc(16, { fill = 0 })

execute [[
  @param autoswsb

  @id   r2
  @mov  r3 0
  @mov  r4 0
  @mov  r5 0

LOOP:
  // First conditional: 0 < 1, so this takes the then-path.
  cmp (1) (lt)f0.0 null:d r5<0>:d 1:d
  (f0.0) if(1)
    add (1) r3 r3<0> 1
  else(1)
    add (1) r3 r3<0> 2
  endif(1)

  // Second conditional: 0 < 0 is false, so this takes the else-path.
  cmp (1) (lt)f0.0 null:d r5<0>:d 0:d
  (f0.0) if(1)
    add (1) r3 r3<0> 4
  else(1)
    add (1) r3 r3<0> 8
  endif(1)

  add (1) r4 r4<0> 1
  cmp (1) (lt)f0.0 null:d r4<0>:d 2:d
  // WHILE still needs an explicit loop-start label for its JIP back-edge.
  (f0.0) while(1) jip:LOOP

  @addr r6 buf0 r2
  @store r6 r3
  @eot
]]

local expected = 18

print("result")
dump(buf, 1)

print("expected")
print(expected)

if buf[0] ~= expected then
  error(string.format("got %u expected %u", buf[0], expected))
end

print("OK")
