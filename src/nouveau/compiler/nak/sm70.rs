// Copyright © 2022 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::ir::*;
use crate::legalize::LegalizeBuilder;
use crate::sm120_instr_latencies::SM120Latency;
use crate::sm70_encode::*;
use crate::sm75_instr_latencies::SM75Latency;
use crate::sm80_instr_latencies::SM80Latency;
pub struct ShaderModel70 {
    sm: u8,
}

impl ShaderModel70 {
    pub fn new(sm: u8) -> Self {
        assert!(sm >= 70);
        Self { sm }
    }

    fn has_uniform_alu(&self) -> bool {
        self.sm >= 73
    }

    fn instr_latency(&self, op: &Op, dst_idx: usize) -> u32 {
        let file = match &op.dsts_as_slice()[dst_idx] {
            Dst::None => return 0,
            Dst::SSA(vec) => vec.file(),
            Dst::Reg(reg) => reg.file(),
        };

        let (gpr_latency, pred_latency) = if self.sm < 80 {
            match op {
                // Double-precision float ALU
                Op::DAdd(_)
                | Op::DFma(_)
                | Op::DMnMx(_)
                | Op::DMul(_)
                | Op::DSetP(_)
                // Half-precision float ALU
                | Op::HAdd2(_)
                | Op::HFma2(_)
                | Op::HMul2(_)
                | Op::HSet2(_)
                | Op::HSetP2(_)
                | Op::HMnMx2(_) => if self.is_volta() {
                    // Volta is even slower
                    (13, 15)
                } else {
                    (13, 14)
                }
                _ => (6, 13)
            }
        } else {
            (6, 13)
        };

        // This is BS and we know it
        match file {
            RegFile::GPR => gpr_latency,
            RegFile::UGPR => 12,
            RegFile::Pred => pred_latency,
            RegFile::UPred => 11,
            RegFile::Bar => 0, // Barriers have a HW scoreboard
            RegFile::Carry => 6,
            RegFile::Mem => panic!("Not a register"),
        }
    }
}

impl ShaderModel for ShaderModel70 {
    fn sm(&self) -> u8 {
        self.sm
    }

    fn num_regs(&self, file: RegFile) -> u32 {
        match file {
            RegFile::GPR => 255 - self.hw_reserved_gprs(),
            RegFile::UGPR => {
                if !self.has_uniform_alu() {
                    0
                } else if self.sm >= 100 {
                    80
                } else {
                    63
                }
            }
            RegFile::Pred => 7,
            RegFile::UPred => {
                if self.has_uniform_alu() {
                    7
                } else {
                    0
                }
            }
            RegFile::Carry => 0,
            RegFile::Bar => 16,
            RegFile::Mem => RegRef::MAX_IDX + 1,
        }
    }

    fn hw_reserved_gprs(&self) -> u32 {
        // On Volta+, 2 GPRs get burned for the program counter - see the
        // footnote on table 2 of the volta whitepaper
        // https://images.nvidia.com/content/volta-architecture/pdf/volta-architecture-whitepaper.pdf
        2
    }

    fn crs_size(&self, max_crs_depth: u32) -> u32 {
        assert!(max_crs_depth == 0);
        0
    }

    fn op_can_be_uniform(&self, op: &Op) -> bool {
        if !self.has_uniform_alu() {
            return false;
        }

        match op {
            Op::R2UR(_)
            | Op::Redux(_)
            | Op::S2R(_)
            | Op::BMsk(_)
            | Op::BRev(_)
            | Op::Flo(_)
            | Op::IAdd3(_)
            | Op::IAdd3X(_)
            | Op::IMad(_)
            | Op::IMad64(_)
            | Op::ISetP(_)
            | Op::Lea(_)
            | Op::LeaX(_)
            | Op::Lop3(_)
            | Op::Mov(_)
            | Op::PLop3(_)
            | Op::PopC(_)
            | Op::Prmt(_)
            | Op::PSetP(_)
            | Op::Sel(_)
            | Op::Sgxt(_)
            | Op::Shf(_)
            | Op::Shl(_)
            | Op::Shr(_)
            | Op::Vote(_)
            | Op::Copy(_)
            | Op::Pin(_)
            | Op::Unpin(_) => true,
            Op::Ldc(op) => op.offset.is_zero(),
            // UCLEA  USHL  USHR
            _ => false,
        }
    }

    fn op_needs_scoreboard(&self, op: &Op) -> bool {
        if op.no_scoreboard() {
            return false;
        }

        if self.is_blackwell() {
            SM120Latency::needs_scoreboards(op)
        } else if self.is_ampere() || self.is_ada() {
            SM80Latency::needs_scoreboards(op)
        } else if self.is_turing() {
            SM75Latency::needs_scoreboards(op)
        } else {
            !op.has_fixed_latency(self.sm())
        }
    }

    fn exec_latency(&self, op: &Op) -> u32 {
        match op {
            Op::Bar(_) | Op::MemBar(_) => {
                if self.sm >= 80 {
                    6
                } else {
                    5
                }
            }
            Op::CCtl(_op) => {
                // CCTL.C needs 8, CCTL.I needs 11
                11
            }
            // Op::DepBar(_) => 4,
            _ => 1, // TODO: co-issue
        }
    }

    fn raw_latency(
        &self,
        write: &Op,
        dst_idx: usize,
        read: &Op,
        src_idx: usize,
    ) -> u32 {
        if self.is_blackwell() {
            SM120Latency::raw(write, dst_idx, Some(read), src_idx)
        } else if self.is_ampere() || self.is_ada() {
            SM80Latency::raw(write, dst_idx, Some(read), src_idx)
        } else if self.is_turing() {
            SM75Latency::raw(write, dst_idx, Some(read), src_idx)
        } else {
            self.instr_latency(write, dst_idx)
        }
    }

    fn war_latency(
        &self,
        read: &Op,
        src_idx: usize,
        write: &Op,
        dst_idx: usize,
    ) -> u32 {
        if self.is_blackwell() {
            SM120Latency::war(read, src_idx, write, dst_idx)
        } else if self.is_ampere() || self.is_ada() {
            SM80Latency::war(read, src_idx, write, dst_idx)
        } else if self.is_turing() {
            SM75Latency::war(read, src_idx, write, dst_idx)
        } else {
            // We assume the source gets read in the first 4 cycles.  We don't
            // know how quickly the write will happen.  This is all a guess.
            4
        }
    }

    fn waw_latency(
        &self,
        a: &Op,
        a_dst_idx: usize,
        a_has_pred: bool,
        b: &Op,
        b_dst_idx: usize,
    ) -> u32 {
        if self.is_blackwell() {
            SM120Latency::waw(a, a_dst_idx, b, b_dst_idx, a_has_pred)
        } else if self.is_ampere() || self.is_ada() {
            SM80Latency::waw(a, a_dst_idx, b, b_dst_idx, a_has_pred)
        } else if self.is_turing() {
            SM75Latency::waw(a, a_dst_idx, b, b_dst_idx, a_has_pred)
        } else {
            // We know our latencies are wrong so assume the wrote could happen
            // anywhere between 0 and instr_latency(a) cycles
            self.instr_latency(a, a_dst_idx)
        }
    }

    fn paw_latency(&self, write: &Op, dst_idx: usize) -> u32 {
        if self.is_blackwell() {
            SM120Latency::raw(write, dst_idx, None, 0)
        } else if self.is_ampere() || self.is_ada() {
            SM80Latency::raw(write, dst_idx, None, 0)
        } else if self.is_turing() {
            SM75Latency::raw(write, dst_idx, None, 0)
        } else if self.is_volta() {
            match write {
                Op::DSetP(_) | Op::HSetP2(_) => 15,
                _ => 13,
            }
        } else {
            13
        }
    }

    fn latency_upper_bound(&self) -> u32 {
        if self.is_blackwell() {
            30
        } else if self.is_ampere() || self.is_ada() {
            30
        } else if self.is_turing() {
            25
        } else {
            15
        }
    }

    fn worst_latency(&self, write: &Op, dst_idx: usize) -> u32 {
        if self.is_blackwell() {
            SM120Latency::raw(write, dst_idx, None, 0)
        } else if self.is_ampere() || self.is_ada() {
            SM80Latency::raw(write, dst_idx, None, 0)
        } else if self.is_turing() {
            SM75Latency::raw(write, dst_idx, None, 0)
        } else {
            self.instr_latency(write, dst_idx)
        }
    }

    fn max_instr_delay(&self) -> u8 {
        15
    }

    fn legalize_op(&self, b: &mut LegalizeBuilder, op: &mut Op) {
        legalize_sm70_op(self, b, op);
    }

    fn encode_shader(&self, s: &Shader<'_>) -> Vec<u32> {
        encode_sm70_shader(self, s)
    }
}
