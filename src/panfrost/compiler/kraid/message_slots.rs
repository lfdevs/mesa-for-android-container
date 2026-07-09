// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::flow::FlowWaitBit;
use crate::ir::*;

impl Shader<'_> {
    pub fn assign_message_slots(&mut self) {
        for b in self.blocks.iter_mut() {
            for i in b.instrs.iter_mut() {
                if matches!(&i.op, Op::Barrier(_)) {
                    i.flow.set_wait_bit(FlowWaitBit::Barrier);
                } else if self.model.op_is_message(&i.op) {
                    i.flow.set_msg_slot_idx(0);
                    i.flow.set_wait_bit(FlowWaitBit::Slot0);
                }
            }
        }
    }
}
