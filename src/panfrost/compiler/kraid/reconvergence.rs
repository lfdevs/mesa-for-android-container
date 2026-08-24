// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::ir::*;
use crate::ops::OpNop;
use compiler::cfg::CFG;

fn needs_reconverge(cfg: &CFG<BasicBlock>, bi: usize) -> bool {
    for &si in cfg.succ_indices(bi) {
        if cfg.pred_indices(si).len() > 1 {
            return true;
        }
    }
    false
}

impl Shader<'_> {
    pub fn mark_reconvergence(&mut self) {
        for bi in 0..self.blocks.len() {
            if needs_reconverge(&self.blocks, bi) {
                let block = &mut self.blocks[bi];
                if let Some(last) = block.instrs.last_mut() {
                    last.flow.set_reconverge();
                } else {
                    let mut nop = Instr::from(OpNop {});
                    nop.flow.set_reconverge();
                    block.instrs.push(nop);
                }
            }
        }
    }
}
