-- Copyright © 2025 Intel Corporation
-- SPDX-License-Identifier: MIT
--
-- Encode and decode floating point types.
--
-- Just enough to get basic usage of the examples.  If this get serious might
-- be a good idea to implement it using the existing Mesa routines and exposing
-- as part of executor API.

local M = {}

M.decode_float = function(bits, mantissa_bits, exponent_bits, bias)
  local total_bits = 1 + exponent_bits + mantissa_bits

  local sign_mask = 1 << (total_bits - 1)
  local exponent_mask = ((1 << exponent_bits) - 1) << mantissa_bits
  local mantissa_mask = (1 << mantissa_bits) - 1

  local sign = (bits & sign_mask) ~= 0
  local exponent = (bits & exponent_mask) >> mantissa_bits
  local mantissa = bits & mantissa_mask

  if exponent == 0 then
    if mantissa == 0 then
      return sign and "-0.0" or "0.0"
    else
      -- Subnormal number.  They don't have implicit leading 1, so
      -- the number corresponds to "0.mantissa * 2^(1-bias)".
      local value = mantissa / (1 << mantissa_bits)
      value = value * (2 ^ (1 - bias))
      if sign then value = -value end
      return string.format("%.17g", value)
    end
  elseif exponent == (1 << exponent_bits) - 1 then
    if mantissa == 0 then
      return sign and "-inf" or "inf"
    else
      return "nan"
    end
  else
    -- Normal numbers have implicit leading 1, so the
    -- number corresponds to "1.mantissa * 2^(exponent-bias)".
    local value = 1.0 + (mantissa / (1 << mantissa_bits))
    value = value * (2 ^ (exponent - bias))
    if sign then value = -value end
    return string.format("%.17g", value)
  end
end

M.decode_f32 = function(bits)
  return M.decode_float(bits, 23, 8, 127)
end

M.decode_f16 = function(bits)
  return M.decode_float(bits, 10, 5, 15)
end

M.decode_bf16 = function(bits)
  return M.decode_float(bits, 7, 8, 127)
end

M.encode_float = function(value_str, mantissa_bits, exponent_bits, bias)
  local value = tonumber(value_str)
  if not value then
    return nil
  end

  local total_bits = 1 + exponent_bits + mantissa_bits
  local max_exponent = (1 << exponent_bits) - 1

  local sign_bit = value < 0 and (1 << (total_bits - 1)) or 0
  local signed_inf = sign_bit | max_exponent << mantissa_bits

  -- Handle various special cases first: signed zero, NaN, Inf/-Inf.
  if value == 0.0 then
    return sign_bit
  elseif value ~= value then
    return (1 << (total_bits - 1)) | (max_exponent << mantissa_bits) | 1
  elseif value == math.huge or value == -math.huge then
    return signed_inf
  end

  -- Do math with the absolute value from now on.
  if sign_bit ~= 0 then value = -value end

  local exponent = math.floor(math.log(value) / math.log(2))

  local mantissa_value = value / (2 ^ exponent) - 1.0

  exponent = exponent + bias

  if exponent <= 0 then
    -- Subnormal: no implicit leading 1, use minimum exponent
    mantissa_value = value / (2 ^ (1 - bias))
    exponent = 0
  elseif exponent >= max_exponent then
    -- Value too large to represent.
    return signed_inf
  end

  local mantissa = math.floor(mantissa_value * (1 << mantissa_bits) + 0.5)

  if mantissa >= (1 << mantissa_bits) then
    -- Rounding caused mantissa to overflow, increment exponent.
    mantissa = 0
    exponent = exponent + 1
    if exponent >= max_exponent then
      -- Value too large to represent.
      return signed_inf
    end
  end

  return sign_bit | (exponent << mantissa_bits) | mantissa
end

M.encode_f32 = function(value_str)
  return M.encode_float(value_str, 23, 8, 127)
end

M.encode_f16 = function(value_str)
  return M.encode_float(value_str, 10, 5, 15)
end

M.encode_bf16 = function(value_str)
  return M.encode_float(value_str, 7, 8, 127)
end

return M
