// Copyright © 2023 Collabora, Ltd.
// SPDX-License-Identifier: MIT

mod api;
mod assign_regs;
mod builder;
mod calc_instr_deps;
mod const_tracker;
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
mod opt_instr_sched_common;
mod opt_instr_sched_postpass;
mod opt_jump_thread;
mod opt_lop;
mod opt_out;
mod opt_prmt;
mod opt_uniform_instrs;
mod qmd;
mod reg_tracker;
mod repair_ssa;
mod sm120_instr_latencies;
mod sm20;
mod sm30_instr_latencies;
mod sm32;
mod sm50;
mod sm70;
mod sm70_encode;
mod sm75_instr_latencies;
mod sm80_instr_latencies;
mod sph;
mod spill_values;
mod ssa_value;
mod to_cssa;
mod union_find;

#[cfg(test)]
mod hw_tests;

#[cfg(test)]
mod hw_runner;

#[cfg(test)]
mod nvdisasm_tests;
