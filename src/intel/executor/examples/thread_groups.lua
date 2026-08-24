-- Dispatch several 1D thread groups and write both the global invocation id
-- and the thread-group id for every lane.

local thread_groups = 4
local hw_threads = 2
local simd = 16
local group_size = hw_threads * simd
local total_lanes = thread_groups * group_size

local ids = alloc(total_lanes, "ids")
local tgs = alloc(total_lanes, "tgs")
ids:fill(0xffffffff)
tgs:fill(0xffffffff)

execute{
  thread_groups = thread_groups,
  src = [[
    @param autoswsb
    @param hw_threads 2
    @param simd       16

    @globalid r4
    @tg       r6

    @addr  r8  ids r4
    @store r8  r4

    @addr  r10 tgs r4
    @store r10 r6

    @eot
  ]],
}

for i = 0, total_lanes - 1 do
  assert(ids[i] == i,
         string.format("ids[%d]=0x%08x expected 0x%08x", i, ids[i], i))
  local expected_tg = math.floor(i / group_size)
  assert(tgs[i] == expected_tg,
         string.format("tgs[%d]=0x%08x expected 0x%08x", i, tgs[i], expected_tg))
end

print("OK")
