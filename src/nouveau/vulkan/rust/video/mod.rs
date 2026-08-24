// Copyright © 2024 Collabora, Ltd.
// SPDX-License-Identifier: MIT
//
// Thanks to Averne for the work on the Nvdec tracer. None of the decode work
// would have been possible otherwise.

pub mod decode;

fn align_u32(value: u32, alignment: u32) -> u32 {
    debug_assert!(alignment.is_power_of_two());
    (value + alignment - 1) & !(alignment - 1)
}
