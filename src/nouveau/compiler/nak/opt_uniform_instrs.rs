// Copyright © 2024 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::ir::*;
use std::collections::HashMap;

fn should_lower_to_warp(
    sm: &dyn ShaderModel,
    instr: &Instr,
    r2ur: &HashMap<SSAValue, SSAValue>,
) -> bool {
    if !sm.op_can_be_uniform(&instr.op) {
        return true;
    }

    let mut num_non_uniform_srcs = 0;
    instr.for_each_ssa_use(|ssa| {
        if !ssa.is_uniform() || r2ur.contains_key(ssa) {
            num_non_uniform_srcs += 1;
        }
    });

    if num_non_uniform_srcs >= 2 {
        return true;
    }

    return false;
}

fn propagate_r2ur(
    instr: &mut Instr,
    r2ur: &HashMap<SSAValue, SSAValue>,
) -> bool {
    let mut progress = false;

    // We don't want Instr::for_each_ssa_use_mut() because it would treat
    // bindless cbuf sources as SSA sources.
    for src in instr.srcs_mut() {
        if let SrcRef::SSA(vec) = &mut src.src_ref {
            for ssa in &mut vec[..] {
                if let Some(r) = r2ur.get(ssa) {
                    progress = true;
                    *ssa = *r;
                }
            }
        }
    }

    progress
}

impl Shader<'_> {
    pub fn opt_uniform_instrs(&mut self) {
        let sm = self.sm;
        let mut r2ur = HashMap::new();
        let mut propagated_r2ur = false;
        self.map_instrs(|mut instr, alloc| {
            if matches!(
                &instr.op,
                Op::PhiDsts(_)
                    | Op::PhiSrcs(_)
                    | Op::Pin(_)
                    | Op::Unpin(_)
                    | Op::Vote(_)
            ) {
                MappedInstrs::One(instr)
            } else if instr.is_uniform() {
                let mut b = InstrBuilder::new(sm);
                if should_lower_to_warp(sm, &instr, &r2ur) {
                    propagated_r2ur |= propagate_r2ur(&mut instr, &r2ur);
                    instr.for_each_ssa_def_mut(|ssa| {
                        let w = alloc.alloc(ssa.file().to_warp());
                        r2ur.insert(*ssa, w);
                        b.push_op(OpR2UR {
                            dst: (*ssa).into(),
                            src: w.into(),
                        });
                        *ssa = w;
                    });
                    let mut v = b.as_vec();
                    v.insert(0, instr);
                    MappedInstrs::Many(v)
                } else {
                    // We may have non-uniform sources
                    instr.for_each_ssa_use_mut(|ssa| {
                        let file = ssa.file();
                        if !file.is_uniform() {
                            let u = alloc.alloc(file.to_uniform().unwrap());
                            b.push_op(OpR2UR {
                                dst: u.into(),
                                src: (*ssa).into(),
                            });
                            *ssa = u;
                        }
                    });
                    b.push_instr(instr);
                    b.as_mapped_instrs()
                }
            } else {
                propagated_r2ur |= propagate_r2ur(&mut instr, &r2ur);
                MappedInstrs::One(instr)
            }
        });

        if propagated_r2ur {
            self.opt_dce();
        }
    }
}
