// Copyright © 2024 Collabora, Ltd.
// SPDX-License-Identifier: MIT

extern crate proc_macro;
extern crate proc_macro2;
#[macro_use]
extern crate quote;
extern crate syn;

pub mod as_slice;
pub mod enum_as_u8;
pub mod from_variants;
