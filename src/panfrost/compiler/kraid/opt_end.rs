// Copyright © 2026 Arm Ltd.
// SPDX-License-Identifier: MIT

use crate::ir::*;

fn has_nop_end(block: &BasicBlock) -> bool {
    block
        .instrs
        .last()
        .is_some_and(|i| matches!(&i.op, Op::Nop(_)) && i.flow.get_end_shader())
}

impl Shader<'_> {
    pub fn opt_end(&mut self) {
        // This pass will remove NOP.end instructions if the .end modifier can
        // be encoded on the instruction before. Barriers at the end of shaders
        // are also safe to remove.
        for bi in 0..self.blocks.len() {
            let block = &mut self.blocks[bi];
            if !has_nop_end(block) || block.instrs.len() == 0 {
                continue;
            }

            let nop_end = block.instrs.pop().unwrap();

            while block
                .instrs
                .last()
                .is_some_and(|i| matches!(i.op, Op::Barrier(_)))
            {
                block.instrs.pop();
            }

            if let Some(last) = block.instrs.last_mut() {
                let slot = last.flow.get_msg_slot_idx();
                last.flow = FlowCtrl::NONE;
                if let Some(slot) = slot {
                    last.flow.set_msg_slot_idx(slot);
                }
                last.flow.set_end_shader();
            } else {
                block.instrs.push(nop_end);
            }
        }
    }
}
