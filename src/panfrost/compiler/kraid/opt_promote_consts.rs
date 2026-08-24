// Copyright © 2026 Arm Ltd.
// SPDX-License-Identifier: MIT

use crate::ir::*;
use kraid_bindings::*;
use rustc_hash::FxHashMap;

#[derive(Debug)]
struct FauConst {
    weight: u32,
    fau_idx: Option<u16>,
}

fn fau_available(fau: &pan_fau_layout) -> usize {
    unsafe { pan_fau_available(fau) as usize }
}

fn fau_emit_const(fau: &mut pan_fau_layout, imm: u32) -> u16 {
    unsafe { pan_fau_emit_const(fau, imm) }.try_into().unwrap()
}

fn promote_consts(s: &mut Shader, fau: &mut pan_fau_layout) {
    let mut fau_consts = FxHashMap::default();

    // TODO: Take into account control flow and try to use source mods to unify
    // constants together.
    for block in s.blocks.iter() {
        for instr in block.instrs.iter() {
            for src in instr.srcs() {
                let SrcRef::Imm32(v) = &src.src_ref else {
                    continue;
                };

                if s.model.op_src_supports_imm32(&instr.op, src, (*v).into()) {
                    continue;
                }

                fau_consts
                    .entry(*v)
                    .and_modify(|fc: &mut FauConst| fc.weight += 1)
                    .or_insert(FauConst {
                        weight: 1,
                        fau_idx: None,
                    });
            }
        }
    }

    let mut weighted: Vec<_> = fau_consts.iter_mut().collect();
    weighted.sort_by(|a, b| a.1.weight.cmp(&b.1.weight).reverse());

    for (imm, fc) in weighted {
        if fau_available(fau) == 0 {
            break;
        }

        fc.fau_idx = Some(fau_emit_const(fau, (*imm).into()));
    }

    for block in s.blocks.iter_mut() {
        for instr in block.instrs.iter_mut() {
            for src in instr.srcs_mut() {
                let SrcRef::Imm32(v) = &src.src_ref else {
                    continue;
                };

                if let Some(idx) = fau_consts.get(v).and_then(|e| e.fau_idx) {
                    src.src_ref = SrcRef::FAU(FAURef::user_i32(idx));
                }
            }
        }
    }
}

impl Shader<'_> {
    pub fn opt_promote_consts(&mut self, fau: &mut pan_fau_layout) {
        promote_consts(self, fau);
    }
}
