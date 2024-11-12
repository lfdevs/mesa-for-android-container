// Copyright © 2023 Collabora, Ltd.
// SPDX-License-Identifier: MIT

mod api;
mod assign_regs;
mod builder;
mod calc_instr_deps;
mod from_nir;
mod ir;
mod legalize;
mod liveness;
mod lower_copy_swap;
mod lower_par_copies;
mod opt_bar_prop;
mod opt_copy_prop;
mod opt_crs;
mod opt_dce;
mod opt_jump_thread;
mod opt_lop;
mod opt_out;
mod opt_prmt;
mod opt_uniform_instrs;
mod qmd;
mod repair_ssa;
mod sm50;
mod sm70;
mod sph;
mod spill_values;
mod to_cssa;
mod union_find;

#[cfg(test)]
mod hw_tests;

#[cfg(test)]
mod hw_runner;
