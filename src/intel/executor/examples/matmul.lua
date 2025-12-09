-- Copyright © 2025 Intel Corporation
-- SPDX-License-Identifier: MIT

local HELP_MESSAGE = [[
Matrix Multiplication using DPAS

Usage: executor matmul.lua FORMAT A_FILE B_FILE [C_FILE]

Perform matrix multiplication D = A * B + C using DPAS instruction.
If C is not provided, will be equivalent to all zeros.

Input files have values separated by spaces, with the rows
separated by newlines.  Values are expected to be valid for
the format, e.g. if a matrix should contain UB (unsigned byte),
values should be between 0-255.  Float-pointing data is
expected to be in "raw" form as hexadecimal values.

Matrices smaller than the maximum dimensions will be automatically
zero-padded to the required size for DPAS computation.

Maximum dimensions are limited by data format and hardware version.

   Gfx20+
   - HF/F:  A max 8x16, B max 16x16, C/D max 8x16
   - BF/F:  A max 8x16, B max 16x16, C/D max 8x16
   - UB/UD: A max 8x32, B max 32x16, C/D max 8x16

   Gfx125
   - HF/F:  A max 8x16, B max 16x8, C/D max 8x8
   - BF/F:  A max 8x16, B max 16x8, C/D max 8x8
   - UB/UD: A max 8x32, B max 32x8, C/D max 8x8

If `octave` is installed, it will be used to verify the results.
]]

-- TODO: Change this program to load matrix data from memory
-- instead of setting data as immediates in the shader code.

if not devinfo.has_dpas then
  print("DPAS not supported on this platform.")
  os.exit(1)
end

local verify_results = false
do
  local handle = io.popen("which octave 2>/dev/null")
  local octave_path = handle:read("*a")
  handle:close()
  verify_results = octave_path and #octave_path > 0
end

local gen = require("mod/gen")
local matrix = require("mod/matrix")
local fp = require("mod/fp")

local format_ab, format_cd, a_file, b_file, c_file

for i = 1, #arg do
  if arg[i] == "--help" or arg[i] == "-h" then
    print(HELP_MESSAGE)
    os.exit(0)
  elseif not format_ab then
    local format = arg[i]:upper()
    format_ab = format:sub(1, format:find("/") - 1)
    format_cd = format:sub(format:find("/") + 1)
  elseif not a_file then a_file = arg[i]
  elseif not b_file then b_file = arg[i]
  elseif not c_file then c_file = arg[i]
  end
end

if not format_ab or not format_cd or not a_file or not b_file then
  print("Usage: executor matmul.lua FORMAT A_FILE B_FILE [C_FILE]")
  print("Use --help for more information")
  os.exit(1)
end

if not ((format_ab == "HF" and format_cd == "F") or
        (format_ab == "BF" and format_cd == "F") or
        (format_ab == "UB" and format_cd == "UD")) then
  print("Error: format must be 'HF/F', 'BF/F', or 'UB/UD', got '" .. format_ab .. "/" .. format_cd .. "'")
  print("Use --help for more information")
  os.exit(1)
end

local function read_matrix(m, filename, max_rows, max_cols)
  local file = io.open(filename, "r")
  if not file then
    error("Failed to open file: " .. filename)
  end

  -- Read entire file content first
  local raw_content = file:read("*all")
  file:close()

  local rows_data = {}
  local cols = nil

  -- Parse from the raw content string
  for line in raw_content:gmatch("[^\r\n]+") do
    local row = {}
    for val in line:gmatch("%S+") do
      local num = tonumber(val)
      if not num then
        error(string.format(
          "Error reading matrix from '%s': invalid number in row %d: " .. val,
          filename, #rows_data + 1))
      end

      table.insert(row, num)
    end

    if #row > 0 then
      if cols == nil then
        cols = #row
      elseif #row ~= cols then
        error(string.format(
          "Error reading matrix from %s: inconsistent number of columns (%d) in row %d",
          filename, #row, #rows_data + 1))
      end
      table.insert(rows_data, row)
    end
  end

  local rows = #rows_data

  if rows > max_rows then
    error(string.format(
      "Error reading matrix from %s: too many rows (%d), maximum is %d",
      filename, rows, max_rows))
  end

  if cols > max_cols then
    error(string.format(
      "Error reading matrix from %s: too many columns (%d), maximum is %d",
      filename, cols, max_cols))
  end

  for i = 1, rows do
    for j = 1, cols do
      -- Matrix indices are zero indexed.
      m:set(i-1, j-1, rows_data[i][j])
    end
  end

  return rows, cols, raw_content
end

local exec_size = devinfo.ver >= 20 and 16 or 8
local packing_factor

if format_ab == "HF" or format_ab == "BF" then
  packing_factor = 2
elseif format_ab == "UB" then
  packing_factor = 4
end

local max_a = { rows = 8, cols = packing_factor * 8 }
local max_b = { rows = packing_factor * 8, cols = exec_size }
local max_c = { rows = 8, cols = exec_size }

local a = matrix.new(max_a.rows, max_a.cols, 0)
local b = matrix.new(max_b.rows, max_b.cols, 0)
local c = matrix.new(max_c.rows, max_c.cols, 0)

local actual_a_rows, actual_a_cols, a_raw_content = read_matrix(a, a_file, max_a.rows, max_a.cols)
local actual_b_rows, actual_b_cols, b_raw_content = read_matrix(b, b_file, max_b.rows, max_b.cols)

if actual_a_cols ~= actual_b_rows then
  error(string.format(
    "Matrix dimension mismatch: A is %dx%d, B is %dx%d. A columns (%d) must equal B rows (%d)",
    actual_a_rows, actual_a_cols, actual_b_rows, actual_b_cols, actual_a_cols, actual_b_rows))
end

local actual_c_rows, actual_c_cols, c_raw_content
if c_file then
  actual_c_rows, actual_c_cols, c_raw_content = read_matrix(c, c_file, max_c.rows, max_c.cols)
  if actual_a_rows ~= actual_c_rows or actual_b_cols ~= actual_c_cols then
    error(string.format(
      "Matrix dimension mismatch: A*B would be %dx%d, but C is %dx%d",
      actual_a_rows, actual_b_cols, actual_c_rows, actual_c_cols))
  end
else
  -- C defaults to zeros with dimensions matching A*B result
  actual_c_rows, actual_c_cols = actual_a_rows, actual_b_cols
  c_raw_content = nil
end

local exec_size = c.cols

local encode = function(m, fmt)
  local f = nil
  if     fmt == "HF" then f = fp.encode_f16
  elseif fmt == "BF" then f = fp.encode_bf16
  elseif fmt == "F"  then f = fp.encode_f32
  end

  if f then
    m:apply(f)
  end
end

encode(a, format_ab)
encode(b, format_ab)
encode(c, format_cd)

local buf = execute {
  src =
    [[]]
    .. gen.mov_grf(format_ab, 10, a:to_row_major())
    .. gen.mov_grf(format_ab, 20, b:to_interleaved_row_major(packing_factor))
    .. gen.mov_grf(format_cd, 30, c:to_row_major())
    .. string.format([[

    dpas.8x8(%d)  r40<1>%s  r30<1>%s  r20<1>%s  r10<1>%s  {A@1 $1};
    @syncnop

    ]], exec_size, format_cd, format_cd, format_ab, format_ab)
    .. gen.write_grfs(40, 8)
    .. [[
    @eot
    ]],
}

local d = matrix.from_row_major_buffer(8, exec_size, buf)

local d_print_fmt = nil
if string.find(format_cd, "F") then
  d_print_fmt = "%.6f"

  local f = nil
  if     format_cd == "HF" then f = fp.decode_f16
  elseif format_cd == "BF" then f = fp.decode_bf16
  elseif format_cd == "F"  then f = fp.decode_f32
  else
    error("unsupported float format")
  end

  d:apply(f)
end

-- Just consider the actual rows, same as C matrix.
d:print_submatrix(actual_c_rows, actual_c_cols, d_print_fmt)

--
-- VERIFICATION USING OCTAVE.
--

if verify_results then
  local function save_matrix_to_temp(m, rows, cols)
    local filename = os.tmpname()
    local file = io.open(filename, "w")
    for i = 0, rows - 1 do
      local row = {}
      for j = 0, cols - 1 do
        table.insert(row, tostring(m:get(i, j)))
      end
      file:write(table.concat(row, " ") .. "\n")
    end
    file:close()
    return filename
  end

  local function save_raw_content_to_temp(raw_content)
    local filename = os.tmpname()
    local file = io.open(filename, "w")
    file:write(raw_content)
    file:close()
    return filename
  end

  -- Save A and B raw contents to temp files for Octave
  local a_for_octave = save_raw_content_to_temp(a_raw_content)
  local b_for_octave = save_raw_content_to_temp(b_raw_content)
  local d_for_octave = save_matrix_to_temp(d, actual_c_rows, actual_c_cols)

  local c_load, c_for_octave
  if c_raw_content then
    c_for_octave = save_raw_content_to_temp(c_raw_content)
    c_load = string.format("C = single(dlmread('%s'));", c_for_octave)
  else
    c_load = string.format("C = single(zeros(%d, %d));", actual_c_rows, actual_c_cols)
  end

  local tolerance = (format_cd == "F") and "1e-3" or "1e-6"

  -- TODO: Currently values are rounded to what fits in F (32-bit), but for
  -- better results we should have a way to round them to precision based on
  -- format, and handle HF and BF16. Octave doesn't support those types
  -- natively.  See https://github.com/higham/chop for Matlab version of this.

  local octave_script = string.format([[
A = single(dlmread('%s'));
B = single(dlmread('%s'));
%s
D = single(dlmread('%s'));

D_expected = A * B + C;

%% Use relative tolerance for better comparison across different magnitudes.
max_val = max(abs(D_expected(:)));
tol = max_val * %s;

if all(all(abs(D_expected - D) < tol))
  exit(0);
else
  disp('MISMATCH!');
  disp('Octave result:');
  disp(D_expected);
  disp('DPAS result:');
  disp(D);
  exit(1);
endif
]], a_for_octave, b_for_octave, c_load, d_for_octave, tolerance)

  local exit_code = os.execute(
    [[octave --quiet --no-gui --eval "]]
    .. octave_script ..
    [[" 2>&1]])

  -- Clean up temporary files
  os.remove(a_for_octave)
  os.remove(b_for_octave)
  if c_for_octave then
    os.remove(c_for_octave)
  end
  os.remove(d_for_octave)

  if exit_code then
    print("\nMatches Octave.")
  else
    print("\nMISMATCH with Octave!")
    os.exit(1)
  end
else
  print("\nNOTE: Install `octave` to verify the results.")
end
