if devinfo.ver < 20 then
  error("SRND instruction requires Gfx20+")
end

F32_VALUE        = 0x3F8016F0 -- 1.0007
-- F32_VALUE     = 0x3F8009D5 -- 1.0003
F16_ROUNDED_UP   = 0x3C01     -- 1.001
F16_ROUNDED_DOWN = 0x3C00     -- 1.000
N                = 16 * 4

math.randomseed(os.time())
local random_data = {}
for i = 0, N-1 do
  random_data[i] = math.random(1 << 32) - 1
end

src = [[
  @id        r10
  @mov       r20 ]] .. F32_VALUE .. [[

]]

for i = 0, (N/16)-1 do
  src = src .. [[
    @read      r30        r10
    @mov       r40        0
    (W) srnd (16) r40<2>:hf r20:f r30:f {A@1}

    // Change r30 to r40 to see the random values.
    @write     r10        r40
    add (16) r10 r10 0x10 {A@1}
  ]]
end

src = src .. [[
  @eot
]]


local r = execute {
  data = random_data,
  src = src,
}

up = 0
down = 0
invalid = 0

for i = 0, N-1 do
  if r[i] == F16_ROUNDED_UP then
    up = up + 1
  elseif r[i] == F16_ROUNDED_DOWN then
    down = down + 1
  else
    invalid = invalid + 1
  end
end

dump(r, N)

print()
print("rounded up    ", up/N, "%")
print("rounded down  ", down/N, "%")
print("invalid       ", invalid/N, "%")
