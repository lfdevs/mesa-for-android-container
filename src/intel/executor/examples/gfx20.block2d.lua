-- LSC 2D block load/store smoke test (Gfx20+).
--
-- Verify that we can load an 8x8 d32 block from a 16x16 source surface
-- and store it back to memory at a different region.

if devinfo.ver < 20 then
  error("block2d requires Gfx20+")
end

-- Source surface: 16x16 d32 values, value(row, col) = (row << 8) | col.
local data = {}
for i = 0, 511 do
  data[i] = 0
end
for row = 0, 15 do
  for col = 0, 15 do
    data[row * 16 + col] = (row << 8) | col
  end
end

local buf = alloc(data, { name = "buf", align = 4096 })

execute [[
    @param autoswsb

    // Surface size: 16 d32 elements per row, 16 rows.
    (W) mov (1) r10.0:ud 0x3f:ud         // width/pitch: 64 bytes - 1
    (W) mov (1) r10.1:ud 0xf:ud          // height: 16 rows - 1

    // r80: source surface.
    (W) mov (8) r80<1>:ud 0x0:ud
    @addr r82 buf
    (W) mov (1) r80.0:ud r82<0;1,0>:ud   // base address low
    (W) mov (1) r80.2:ud r10.0<0>:ud
    (W) mov (1) r80.3:ud r10.1<0>:ud
    (W) mov (1) r80.4:ud r10.0<0>:ud     // pitch: 64 bytes - 1
    (W) mov (1) r80.7:ud 0x0707:ud       // 8x8 block, array len 1

    // r81: destination surface, block start (4, 2) set in the payload.
    (W) mov (8) r81<1>:ud 0x0:ud
    @addr r83 buf 256
    (W) mov (1) r81.0:ud r83<0;1,0>:ud
    (W) mov (1) r81.2:ud r10.0<0>:ud
    (W) mov (1) r81.3:ud r10.1<0>:ud
    (W) mov (1) r81.4:ud r10.0<0>:ud     // pitch: 64 bytes - 1
    (W) mov (1) r81.5:ud 0x4:ud          // block start X = 4 elements
    (W) mov (1) r81.6:ud 0x2:ud          // block start Y = 2 rows
    (W) mov (1) r81.7:ud 0x0707:ud

    // Load an 8x8 block at source offset (4, 2), then store it to r81's block.
    (W) load_block2d.ugm.d32.a64 (1) r20:4 [r80:1 + (4, 2)]
    (W) sync.nop (1) null
    (W) store_block2d.ugm.d32.a64 (1) [r81:1] r20:4

    @eot
]]

local function dump_block(base, stride)
  for row = 0, 7 do
    local line = ""
    for col = 0, 7 do
      line = line .. string.format(" 0x%04x", buf[base + row * stride + col])
    end
    print(line)
  end
end

local dst = 256 + 2 * 16 + 4

print("block stored at (4, 2)")
dump_block(dst, 16)

for row = 0, 7 do
  for col = 0, 7 do
    if buf[dst + row * 16 + col] ~= ((row + 2) << 8 | (col + 4)) then
      print("FAIL")
      os.exit(1)
    end
  end
end

print("OK")
