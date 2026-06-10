// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

extern crate proc_macro;
extern crate proc_macro2;
#[macro_use]
extern crate quote;
extern crate syn;

pub mod data_type;
pub mod ir;
pub mod isa;
pub mod swizzle;
