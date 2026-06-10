// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::bitview::*;

#[repr(u8)]
#[derive(Clone, Copy, Eq, Hash, PartialEq)]
pub enum FlowWaitBit {
    Slot0 = 0,
    Slot1 = 1,
    Slot2 = 2,
    Resource = 5,
    ZS = 6,
    Barrier = 7,
}

#[repr(u8)]
#[derive(Clone, Copy, Eq, Hash, PartialEq)]
enum FlowCtrlBit {
    MsgSlotBit0 = 0,
    MsgSlotBit1 = 1,
    Reconverge,
    Discard,
    EndShader,
}

#[derive(Clone, Copy, Default, Eq, Hash, PartialEq)]
pub struct FlowCtrl {
    pub wait: u8,
    ctrl: u8,
}

impl FlowCtrl {
    pub const NONE: FlowCtrl = FlowCtrl { wait: 0, ctrl: 0 };

    fn get_ctrl_bit(&self, bit: FlowCtrlBit) -> bool {
        BitView::new(&self.ctrl).get_bit(bit as usize)
    }

    fn set_ctrl_bit(&mut self, bit: FlowCtrlBit) {
        BitMutView::new(&mut self.ctrl).set_bit(bit as usize, true);
    }

    fn take_ctrl_bit(&mut self, bit: FlowCtrlBit) -> bool {
        let mut bv = BitMutView::new(&mut self.ctrl);
        let val = bv.get_bit(bit as usize);
        bv.set_bit(bit as usize, false);
        val
    }

    pub fn get_msg_slot_idx(&self) -> Option<u8> {
        let slot = BitView::new(&self.ctrl).get_bit_range_u64(0..2) as u8;
        if slot > 0 {
            Some((slot - 1) as u8)
        } else {
            None
        }
    }

    pub fn set_msg_slot_idx(&mut self, slot_idx: u8) {
        assert!(slot_idx < 3);
        let slot = slot_idx + 1;
        BitMutView::new(&mut self.ctrl).set_field(0..2, slot);
    }

    pub fn take_msg_slot_idx(&mut self) -> Option<u8> {
        let val = self.get_msg_slot_idx();
        BitMutView::new(&mut self.ctrl).set_field(0..2, 0_u8);
        val
    }

    pub fn get_reconverge(&self) -> bool {
        self.get_ctrl_bit(FlowCtrlBit::Reconverge)
    }

    pub fn set_reconverge(&mut self) {
        self.set_ctrl_bit(FlowCtrlBit::Reconverge)
    }

    pub fn take_reconverge(&mut self) -> bool {
        self.take_ctrl_bit(FlowCtrlBit::Reconverge)
    }

    pub fn get_discard(&self) -> bool {
        self.get_ctrl_bit(FlowCtrlBit::Discard)
    }

    pub fn set_discard(&mut self) {
        self.set_ctrl_bit(FlowCtrlBit::Discard)
    }

    pub fn take_discard(&mut self) -> bool {
        self.take_ctrl_bit(FlowCtrlBit::Discard)
    }

    pub fn get_end_shader(&self) -> bool {
        self.get_ctrl_bit(FlowCtrlBit::EndShader)
    }

    pub fn set_end_shader(&mut self) {
        self.set_ctrl_bit(FlowCtrlBit::EndShader)
    }

    pub fn take_end_shader(&mut self) -> bool {
        self.take_ctrl_bit(FlowCtrlBit::EndShader)
    }

    pub fn get_wait_bit(&self, bit: FlowWaitBit) -> bool {
        BitView::new(&self.wait).get_bit(bit as usize)
    }

    pub fn set_wait_bit(&mut self, bit: FlowWaitBit) {
        BitMutView::new(&mut self.wait).set_bit(bit as usize, true);
    }

    pub fn take_wait_bit(&mut self, bit: FlowWaitBit) -> bool {
        let mut bv = BitMutView::new(&mut self.wait);
        let val = bv.get_bit(bit as usize);
        bv.set_bit(bit as usize, false);
        val
    }
}
