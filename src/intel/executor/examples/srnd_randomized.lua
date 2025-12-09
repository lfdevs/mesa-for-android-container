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
  @id        g10
  @mov       g20 ]] .. F32_VALUE .. [[

]]

for i = 0, (N/16)-1 do
  src = src .. [[
    @read      g30        g10
    @mov       g40        0
    srnd(16)   g40<2>HF   g20<1,1,0>F   g30<1,1,0>F   {nomask};

    // Change g30 to g40 to see the random values.
    @write     g10        g40
    add(16)    g10<1>UD   g10<1,1,0>UD  0x10UD        {A@1};
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
