// Copyright © 2022 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::{
    api::{GetDebugFlags, DEBUG},
    ir::*,
};

use std::cmp::max;

struct LowerCopySwap {
    slm_start: u32,
    slm_size: u32,
}

impl LowerCopySwap {
    fn new(slm_size: u32) -> Self {
        Self {
            slm_start: slm_size,
            slm_size: slm_size,
        }
    }

    fn lower_copy(&mut self, b: &mut impl Builder, copy: OpCopy) {
        let dst_reg = copy.dst.as_reg().unwrap();
        assert!(dst_reg.comps() == 1);
        assert!(copy.src.is_unmodified());
        assert!(copy.src.is_uniform() || !dst_reg.is_uniform());

        match dst_reg.file() {
            RegFile::GPR | RegFile::UGPR => match copy.src.src_ref {
                SrcRef::Zero | SrcRef::Imm32(_) => {
                    b.push_op(OpMov {
                        dst: copy.dst,
                        src: copy.src,
                        quad_lanes: 0xf,
                    });
                }
                SrcRef::CBuf(_) => match dst_reg.file() {
                    RegFile::GPR => {
                        if b.sm() >= 100 {
                            b.push_op(OpLdc {
                                dst: copy.dst,
                                cb: copy.src,
                                offset: 0.into(),
                                mode: LdcMode::Indexed,
                                mem_type: MemType::B32,
                            });
                        } else {
                            b.push_op(OpMov {
                                dst: copy.dst,
                                src: copy.src,
                                quad_lanes: 0xf,
                            });
                        }
                    }
                    RegFile::UGPR => {
                        b.push_op(OpLdc {
                            dst: copy.dst,
                            cb: copy.src,
                            offset: 0.into(),
                            mode: LdcMode::Indexed,
                            mem_type: MemType::B32,
                        });
                    }
                    _ => panic!("Invalid cbuf destination"),
                },
                SrcRef::True | SrcRef::False => {
                    panic!("Cannot copy to GPR");
                }
                SrcRef::Reg(src_reg) => match src_reg.file() {
                    RegFile::GPR | RegFile::UGPR => {
                        b.push_op(OpMov {
                            dst: copy.dst,
                            src: copy.src,
                            quad_lanes: 0xf,
                        });
                    }
                    RegFile::Bar => {
                        b.push_op(OpBMov {
                            dst: copy.dst,
                            src: copy.src,
                            clear: false,
                        });
                    }
                    RegFile::Mem => {
                        let access = MemAccess {
                            mem_type: MemType::B32,
                            space: MemSpace::Local,
                            order: MemOrder::Strong(MemScope::CTA),
                            eviction_priority: MemEvictionPriority::Normal,
                        };
                        let addr = self.slm_start + src_reg.base_idx() * 4;
                        self.slm_size = max(self.slm_size, addr + 4);
                        b.push_op(OpLd {
                            dst: copy.dst,
                            addr: Src::ZERO,
                            offset: addr.try_into().unwrap(),
                            access: access,
                        });
                    }
                    _ => panic!("Cannot copy to GPR"),
                },
                SrcRef::SSA(_) => panic!("Should be run after RA"),
            },
            RegFile::Pred | RegFile::UPred => match copy.src.src_ref {
                SrcRef::Zero | SrcRef::Imm32(_) | SrcRef::CBuf(_) => {
                    panic!("Cannot copy to Pred");
                }
                SrcRef::True => {
                    b.lop2_to(
                        copy.dst,
                        LogicOp2::PassB,
                        Src::new_imm_bool(true),
                        Src::new_imm_bool(true),
                    );
                }
                SrcRef::False => {
                    b.lop2_to(
                        copy.dst,
                        LogicOp2::PassB,
                        Src::new_imm_bool(true),
                        Src::new_imm_bool(false),
                    );
                }
                SrcRef::Reg(src_reg) => match src_reg.file() {
                    RegFile::Pred => {
                        b.lop2_to(
                            copy.dst,
                            LogicOp2::PassB,
                            Src::new_imm_bool(true),
                            copy.src,
                        );
                    }
                    RegFile::UPred => {
                        // PLOP3 supports a UPred in src[2]
                        b.push_op(OpPLop3 {
                            dsts: [copy.dst, Dst::None],
                            srcs: [true.into(), true.into(), copy.src],
                            ops: [
                                LogicOp3::new_lut(&|_, _, z| z),
                                LogicOp3::new_const(false),
                            ],
                        });
                    }
                    _ => panic!("Cannot copy to Pred"),
                },
                SrcRef::SSA(_) => panic!("Should be run after RA"),
            },
            RegFile::Bar => match copy.src.src_ref {
                SrcRef::Reg(src_reg) => match src_reg.file() {
                    RegFile::GPR | RegFile::UGPR => {
                        b.push_op(OpBMov {
                            dst: copy.dst,
                            src: copy.src,
                            clear: false,
                        });
                    }
                    _ => panic!("Cannot copy to Bar"),
                },
                _ => panic!("Cannot copy to Bar"),
            },
            RegFile::Mem => match copy.src.src_ref {
                SrcRef::Reg(src_reg) => match src_reg.file() {
                    RegFile::GPR => {
                        let access = MemAccess {
                            mem_type: MemType::B32,
                            space: MemSpace::Local,
                            order: MemOrder::Strong(MemScope::CTA),
                            eviction_priority: MemEvictionPriority::Normal,
                        };
                        let addr = self.slm_start + dst_reg.base_idx() * 4;
                        self.slm_size = max(self.slm_size, addr + 4);
                        b.push_op(OpSt {
                            addr: Src::ZERO,
                            data: copy.src,
                            offset: addr.try_into().unwrap(),
                            access: access,
                        });
                    }
                    _ => panic!("Cannot copy to Mem"),
                },
                _ => panic!("Cannot copy to Mem"),
            },
            _ => panic!("Unhandled register file"),
        }
    }

    fn lower_r2ur(&mut self, b: &mut impl Builder, r2ur: OpR2UR) {
        assert!(r2ur.src.is_unmodified());
        if r2ur.src.is_uniform() {
            let copy = OpCopy {
                dst: r2ur.dst,
                src: r2ur.src,
            };
            self.lower_copy(b, copy);
        } else {
            let src_file = r2ur.src.src_ref.as_reg().unwrap().file();
            let dst_file = r2ur.dst.as_reg().unwrap().file();
            match src_file {
                RegFile::GPR => {
                    assert!(dst_file == RegFile::UGPR);
                    b.push_op(r2ur);
                }
                RegFile::Pred => {
                    assert!(dst_file == RegFile::UPred);
                    // It doesn't matter what channel we take
                    b.push_op(OpVote {
                        op: VoteOp::Any,
                        ballot: Dst::None,
                        vote: r2ur.dst,
                        pred: r2ur.src,
                    });
                }
                _ => panic!("No matching uniform register file"),
            }
        }
    }

    fn lower_swap(&mut self, b: &mut impl Builder, swap: OpSwap) {
        let x = *swap.dsts[0].as_reg().unwrap();
        let y = *swap.dsts[1].as_reg().unwrap();

        assert!(x.file() == y.file());
        assert!(x.file() != RegFile::Mem);
        assert!(x.comps() == 1 && y.comps() == 1);
        assert!(swap.srcs[0].is_unmodified());
        assert!(*swap.srcs[0].src_ref.as_reg().unwrap() == y);
        assert!(swap.srcs[1].is_unmodified());
        assert!(*swap.srcs[1].src_ref.as_reg().unwrap() == x);

        if x == y {
            // Nothing to do
        } else if x.is_predicate() && b.sm() >= 70 {
            b.push_op(OpPLop3 {
                dsts: [x.into(), y.into()],
                srcs: [x.into(), y.into(), Src::new_imm_bool(true)],
                ops: [
                    LogicOp3::new_lut(&|_, y, _| y),
                    LogicOp3::new_lut(&|x, _, _| x),
                ],
            });
        } else {
            b.lop2_to(x.into(), LogicOp2::Xor, x.into(), y.into());
            b.lop2_to(y.into(), LogicOp2::Xor, x.into(), y.into());
            b.lop2_to(x.into(), LogicOp2::Xor, x.into(), y.into());
        }
    }

    fn run(&mut self, s: &mut Shader) {
        let sm = s.sm;
        s.map_instrs(|instr: Instr, _| -> MappedInstrs {
            match instr.op {
                Op::R2UR(r2ur) => {
                    debug_assert!(instr.pred.is_true());
                    let mut b = InstrBuilder::new(sm);
                    if DEBUG.annotate() {
                        b.push_instr(Instr::new(OpAnnotate {
                            annotation: "r2ur lowered by lower_copy_swap"
                                .into(),
                        }));
                    }
                    self.lower_r2ur(&mut b, *r2ur);
                    b.into_mapped_instrs()
                }
                Op::Copy(copy) => {
                    debug_assert!(instr.pred.is_true());
                    let mut b = InstrBuilder::new(sm);
                    if DEBUG.annotate() {
                        b.push_instr(Instr::new(OpAnnotate {
                            annotation: "copy lowered by lower_copy_swap"
                                .into(),
                        }));
                    }
                    self.lower_copy(&mut b, *copy);
                    b.into_mapped_instrs()
                }
                Op::Swap(swap) => {
                    debug_assert!(instr.pred.is_true());
                    let mut b = InstrBuilder::new(sm);
                    if DEBUG.annotate() {
                        b.push_instr(Instr::new(OpAnnotate {
                            annotation: "swap lowered by lower_copy_swap"
                                .into(),
                        }));
                    }
                    self.lower_swap(&mut b, *swap);
                    b.into_mapped_instrs()
                }
                _ => MappedInstrs::One(instr),
            }
        });
    }
}

impl Shader<'_> {
    pub fn lower_copy_swap(&mut self) {
        let mut pass = LowerCopySwap::new(self.info.slm_size);
        pass.run(self);
        self.info.slm_size = pass.slm_size;
    }
}
