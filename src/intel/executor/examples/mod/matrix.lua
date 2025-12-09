-- Copyright © 2025 Intel Corporation
-- SPDX-License-Identifier: MIT

local M = {}

local Matrix = {}
Matrix.__index = Matrix

Matrix.get = function(self, i, j)
  if i < 0 or i >= self.rows or j < 0 or j >= self.cols then
    error(string.format("Index out of bounds in get(): i=%d j=%d (size %dx%d)",
                        i, j, self.rows, self.cols))
  end
  return self.data[i][j]
end

Matrix.set = function(self, i, j, value)
  if i < 0 or i >= self.rows or j < 0 or j >= self.cols then
    error(string.format("Index out of bounds in set(): i=%d j=%d (size %dx%d)",
                        i, j, self.rows, self.cols))
  end
  self.data[i][j] = value
end

Matrix.print_submatrix = function(self, rows, cols, fmt)
  local fmt = fmt or "%4u"
  print(string.format("# %dx%d matrix", rows, cols))
  io.write("[\n")
  for i = 0, rows - 1 do
    for j = 0, cols - 1 do
      io.write(string.format(fmt, self.data[i][j]))
      if j < cols - 1 then io.write(" ") end
    end
    io.write("\n")
  end
  io.write("]\n")
end

Matrix.print = function(self, fmt)
  self:print_submatrix(self.rows, self.cols, fmt)
end

-- "Interleaved" row major is like row major except that
-- elements from `packing_factor` rows are packed together.
--
-- So, for a 4x4 matrix with packing factor 2
--
--   [ 1 2 3 4
--     5 6 7 8
--     9 a b c
--     d e f 0 ]
--
-- in regular row major the sequence
--
--   [ 1 2 3 4 5 6 7 8 9 a b c d e f 0 ]
--
-- while in "interleaved" row major
--
--   [ 1 5 2 6 3 7 4 8 9 d a e b f c 0 ]
--
-- This is used by `src1` of DPAS instruction.
--
Matrix.to_interleaved_row_major = function(self, packing_factor)
  local result = {}
  for row = 0, self.rows-1 do
    for col = 0, self.cols-1 do
      local v = self.data[row][col]
      local idx = (row // packing_factor) * (self.cols * packing_factor) +
                  col * packing_factor +
                  row % packing_factor
      -- The +1 is for 1-based arrays in Lua.
      result[idx+1] = v
    end
  end
  return result
end

Matrix.to_row_major = function(self)
  local result = {}
  for i = 0, self.rows - 1 do
    for j = 0, self.cols - 1 do
      local idx = i * self.cols + j
      -- The +1 is for 1-based arrays in Lua.
      result[idx+1] = self.data[i][j]
    end
  end
  return result
end

M.new = function(rows, cols, default)
  local self = setmetatable({}, Matrix)
  self.rows = rows
  self.cols = cols
  self.data = {}

  for i = 0, rows - 1 do
    self.data[i] = {}
    for j = 0, cols - 1 do
      self.data[i][j] = default or 0
    end
  end

  return self
end

M.new_diag = function(rows, cols, value)
  cols = cols or rows  -- If cols not provided, use rows (square matrix)
  local self = M.new(rows, cols, 0)
  local min_dim = math.min(rows, cols)
  for i = 0, min_dim - 1 do
      self.data[i][i] = value
  end
  return self
end

M.from_row_major = function(rows, cols, data)
  local self = M.new(rows, cols)

  for i = 0, rows - 1 do
    for j = 0, cols - 1 do
      local idx = i * cols + j + 1
      self.data[i][j] = data[idx] or 0
    end
  end

  return self
end

-- Like above but assume data is 0-indexed like a memory buffer.
M.from_row_major_buffer = function(rows, cols, data)
  local self = M.new(rows, cols)

  for i = 0, rows - 1 do
    for j = 0, cols - 1 do
      local idx = i * cols + j
      self.data[i][j] = data[idx] or 0
    end
  end

  return self
end

Matrix.apply = function(self, func)
  for i = 0, self.rows - 1 do
    for j = 0, self.cols - 1 do
      self.data[i][j] = func(self.data[i][j])
    end
  end
end

return M
