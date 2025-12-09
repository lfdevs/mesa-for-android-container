// Copyright © 2022 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::ir::*;

pub trait Builder {
    fn push_instr(&mut self, instr: Instr) -> &mut Instr;

    fn sm(&self) -> u8;

    fn push_op(&mut self, op: impl Into<Op>) -> &mut Instr {
        self.push_instr(Instr::new(op))
    }

    fn predicate(&mut self, pred: Pred) -> PredicatedBuilder<'_, Self>
    where
        Self: Sized,
    {
        PredicatedBuilder {
            b: self,
            pred: pred,
        }
    }

    fn lop2_to(&mut self, dst: Dst, op: LogicOp2, mut x: Src, mut y: Src) {
        let is_predicate = match &dst {
            Dst::None => panic!("No LOP destination"),
            Dst::SSA(ssa) => ssa.is_predicate(),
            Dst::Reg(reg) => reg.is_predicate(),
        };
        assert!(x.is_predicate() == is_predicate);
        assert!(y.is_predicate() == is_predicate);

        if self.sm() >= 70 {
            let mut op = op.to_lut();
            if x.src_mod.is_bnot() {
                op = LogicOp3::new_lut(&|x, y, _| op.eval(!x, y, 0));
                x.src_mod = SrcMod::None;
            }
            if y.src_mod.is_bnot() {
                op = LogicOp3::new_lut(&|x, y, _| op.eval(x, !y, 0));
                y.src_mod = SrcMod::None;
            }
            if is_predicate {
                self.push_op(OpPLop3 {
                    dsts: [dst, Dst::None],
                    srcs: [x, y, true.into()],
                    ops: [op, LogicOp3::new_const(false)],
                });
            } else {
                self.push_op(OpLop3 {
                    dst: dst,
                    srcs: [x, y, 0.into()],
                    op: op,
                });
            }
        } else {
            if is_predicate {
                let mut x = x;
                let cmp_op = match op {
                    LogicOp2::And => PredSetOp::And,
                    LogicOp2::Or => PredSetOp::Or,
                    LogicOp2::Xor => PredSetOp::Xor,
                    LogicOp2::PassB => {
                        // Pass through B by AND with PT
                        x = true.into();
                        PredSetOp::And
                    }
                };
                self.push_op(OpPSetP {
                    dsts: [dst, Dst::None],
                    ops: [cmp_op, PredSetOp::And],
                    srcs: [x, y, true.into()],
                });
            } else {
                self.push_op(OpLop2 {
                    dst: dst,
                    srcs: [x, y],
                    op: op,
                });
            }
        }
    }

    fn prmt_to(&mut self, dst: Dst, x: Src, y: Src, sel: [u8; 4]) {
        if sel == [0, 1, 2, 3] {
            self.copy_to(dst, x);
        } else if sel == [4, 5, 6, 7] {
            self.copy_to(dst, y);
        } else {
            let mut sel_u32 = 0;
            for i in 0..4 {
                assert!(sel[i] < 16);
                sel_u32 |= u32::from(sel[i]) << (i * 4);
            }

            self.push_op(OpPrmt {
                dst: dst,
                srcs: [x, y],
                sel: sel_u32.into(),
                mode: PrmtMode::Index,
            });
        }
    }

    fn copy_to(&mut self, dst: Dst, src: Src) {
        self.push_op(OpCopy { dst: dst, src: src });
    }

    fn swap(&mut self, x: RegRef, y: RegRef) {
        assert!(x.file() == y.file());
        self.push_op(OpSwap {
            dsts: [x.into(), y.into()],
            srcs: [y.into(), x.into()],
        });
    }
}

pub trait SSABuilder: Builder {
    fn alloc_ssa(&mut self, file: RegFile) -> SSAValue;
    fn alloc_ssa_vec(&mut self, file: RegFile, comps: u8) -> SSARef;

    fn shl(&mut self, x: Src, shift: Src) -> SSAValue {
        let dst = self.alloc_ssa(RegFile::GPR);
        if self.sm() >= 70 {
            self.push_op(OpShf {
                dst: dst.into(),
                low: x,
                high: 0.into(),
                shift: shift,
                right: false,
                wrap: true,
                data_type: IntType::I32,
                dst_high: false,
            });
        } else {
            self.push_op(OpShl {
                dst: dst.into(),
                src: x,
                shift: shift,
                wrap: true,
            });
        }
        dst
    }

    fn shl64(&mut self, x: Src, shift: Src) -> SSARef {
        let x = x.as_ssa().unwrap();
        debug_assert!(shift.is_unmodified());

        let dst = self.alloc_ssa_vec(RegFile::GPR, 2);
        if self.sm() >= 70 {
            self.push_op(OpShf {
                dst: dst[0].into(),
                low: x[0].into(),
                high: 0.into(),
                shift: shift.clone(),
                right: false,
                wrap: true,
                data_type: IntType::U64,
                dst_high: false,
            });
        } else {
            // On Maxwell and earlier, shf.l doesn't work without .high so we
            // have to use only the high parts, hard-coding the lower parts
            // to rZ
            self.push_op(OpShf {
                dst: dst[0].into(),
                low: 0.into(),
                high: x[0].into(),
                shift: shift.clone(),
                right: false,
                wrap: true,
                data_type: IntType::U64,
                dst_high: true,
            });
        }
        self.push_op(OpShf {
            dst: dst[1].into(),
            low: x[0].into(),
            high: x[1].into(),
            shift,
            right: false,
            wrap: true,
            data_type: IntType::U64,
            dst_high: true,
        });
        dst
    }

    fn shr(&mut self, x: Src, shift: Src, signed: bool) -> SSAValue {
        let dst = self.alloc_ssa(RegFile::GPR);
        if self.sm() >= 70 {
            self.push_op(OpShf {
                dst: dst.into(),
                low: 0.into(),
                high: x,
                shift: shift,
                right: true,
                wrap: true,
                data_type: if signed { IntType::I32 } else { IntType::U32 },
                dst_high: true,
            });
        } else {
            self.push_op(OpShr {
                dst: dst.into(),
                src: x,
                shift: shift,
                wrap: true,
                signed,
            });
        }
        dst
    }

    fn shr64(&mut self, x: Src, shift: Src, signed: bool) -> SSARef {
        let x = x.as_ssa().unwrap();
        debug_assert!(shift.is_unmodified());

        let dst = self.alloc_ssa_vec(RegFile::GPR, 2);
        self.push_op(OpShf {
            dst: dst[0].into(),
            low: x[0].into(),
            high: x[1].into(),
            shift: shift.clone(),
            right: true,
            wrap: true,
            data_type: if signed { IntType::I64 } else { IntType::U64 },
            dst_high: false,
        });
        self.push_op(OpShf {
            dst: dst[1].into(),
            low: 0.into(),
            high: x[1].into(),
            shift,
            right: true,
            wrap: true,
            data_type: if signed { IntType::I64 } else { IntType::U64 },
            dst_high: true,
        });
        dst
    }

    fn fadd(&mut self, x: Src, y: Src) -> SSAValue {
        let dst = self.alloc_ssa(RegFile::GPR);
        self.push_op(OpFAdd {
            dst: dst.into(),
            srcs: [x, y],
            saturate: false,
            rnd_mode: FRndMode::NearestEven,
            ftz: false,
        });
        dst
    }

    fn fmul(&mut self, x: Src, y: Src) -> SSAValue {
        let dst = self.alloc_ssa(RegFile::GPR);
        self.push_op(OpFMul {
            dst: dst.into(),
            srcs: [x, y],
            saturate: false,
            rnd_mode: FRndMode::NearestEven,
            ftz: false,
            dnz: false,
        });
        dst
    }

    fn fset(&mut self, cmp_op: FloatCmpOp, x: Src, y: Src) -> SSAValue {
        let dst = self.alloc_ssa(RegFile::GPR);
        self.push_op(OpFSet {
            dst: dst.into(),
            cmp_op: cmp_op,
            srcs: [x, y],
            ftz: false,
        });
        dst
    }

    fn fsetp(&mut self, cmp_op: FloatCmpOp, x: Src, y: Src) -> SSAValue {
        let dst = self.alloc_ssa(RegFile::Pred);
        self.push_op(OpFSetP {
            dst: dst.into(),
            set_op: PredSetOp::And,
            cmp_op: cmp_op,
            srcs: [x, y],
            accum: SrcRef::True.into(),
            ftz: false,
        });
        dst
    }

    fn hadd2(&mut self, x: Src, y: Src) -> SSAValue {
        let dst = self.alloc_ssa(RegFile::GPR);
        self.push_op(OpHAdd2 {
            dst: dst.into(),
            srcs: [x, y],
            saturate: false,
            ftz: false,
            f32: false,
        });
        dst
    }

    fn hset2(&mut self, cmp_op: FloatCmpOp, x: Src, y: Src) -> SSARef {
        let dst = self.alloc_ssa(RegFile::GPR);
        self.push_op(OpHSet2 {
            dst: dst.into(),
            set_op: PredSetOp::And,
            cmp_op: cmp_op,
            srcs: [x, y],
            ftz: false,
            accum: SrcRef::True.into(),
        });
        dst.into()
    }

    fn dsetp(&mut self, cmp_op: FloatCmpOp, x: Src, y: Src) -> SSAValue {
        let dst = self.alloc_ssa(RegFile::Pred);
        self.push_op(OpDSetP {
            dst: dst.into(),
            set_op: PredSetOp::And,
            cmp_op: cmp_op,
            srcs: [x, y],
            accum: SrcRef::True.into(),
        });
        dst
    }

    fn iabs(&mut self, i: Src) -> SSAValue {
        let dst = self.alloc_ssa(RegFile::GPR);
        if self.sm() >= 70 {
            self.push_op(OpIAbs {
                dst: dst.into(),
                src: i,
            });
        } else {
            self.push_op(OpI2I {
                dst: dst.into(),
                src: i,
                src_type: IntType::I32,
                dst_type: IntType::I32,
                saturate: false,
                abs: true,
                neg: false,
            });
        }
        dst
    }

    fn iadd(&mut self, x: Src, y: Src, z: Src) -> SSAValue {
        let dst = self.alloc_ssa(RegFile::GPR);
        if self.sm() >= 70 {
            self.push_op(OpIAdd3 {
                dst: dst.into(),
                srcs: [x, y, z],
                overflow: [Dst::None, Dst::None],
            });
        } else {
            assert!(z.is_zero());
            self.push_op(OpIAdd2 {
                dst: dst.into(),
                srcs: [x, y],
                carry_out: Dst::None,
            });
        }
        dst
    }

    fn iadd64(&mut self, x: Src, y: Src, z: Src) -> SSARef {
        fn split_iadd64_src(src: Src) -> [Src; 2] {
            match src.src_ref {
                SrcRef::Zero => [0.into(), 0.into()],
                SrcRef::SSA(ssa) => {
                    if src.src_mod.is_ineg() {
                        [Src::from(ssa[0]).ineg(), Src::from(ssa[1]).bnot()]
                    } else {
                        [Src::from(ssa[0]), Src::from(ssa[1])]
                    }
                }
                _ => panic!("Unsupported iadd64 source"),
            }
        }

        let is_3src = !x.is_zero() && !y.is_zero() && !z.is_zero();

        let [x0, x1] = split_iadd64_src(x);
        let [y0, y1] = split_iadd64_src(y);
        let dst = self.alloc_ssa_vec(RegFile::GPR, 2);
        if self.sm() >= 70 {
            let carry1 = self.alloc_ssa(RegFile::Pred);
            let (carry2_dst, carry2_src) = if is_3src {
                let carry2 = self.alloc_ssa(RegFile::Pred);
                (carry2.into(), carry2.into())
            } else {
                // If one of the sources is known to be zero, we only need one
                // carry predicate.
                (Dst::None, false.into())
            };

            let [z0, z1] = split_iadd64_src(z);
            self.push_op(OpIAdd3 {
                dst: dst[0].into(),
                overflow: [carry1.into(), carry2_dst],
                srcs: [x0, y0, z0],
            });
            self.push_op(OpIAdd3X {
                dst: dst[1].into(),
                overflow: [Dst::None, Dst::None],
                srcs: [x1, y1, z1],
                carry: [carry1.into(), carry2_src],
            });
        } else {
            assert!(z.is_zero());
            let carry = self.alloc_ssa(RegFile::Carry);
            self.push_op(OpIAdd2 {
                dst: dst[0].into(),
                srcs: [x0, y0],
                carry_out: carry.into(),
            });
            self.push_op(OpIAdd2X {
                dst: dst[1].into(),
                srcs: [x1, y1],
                carry_out: Dst::None,
                carry_in: carry.into(),
            });
        }
        dst
    }

    fn imnmx(&mut self, tp: IntCmpType, x: Src, y: Src, min: Src) -> SSAValue {
        let dst = self.alloc_ssa(RegFile::GPR);
        self.push_op(OpIMnMx {
            dst: dst.into(),
            cmp_type: tp,
            srcs: [x, y],
            min: min,
        });
        dst
    }

    fn imul(&mut self, x: Src, y: Src) -> SSAValue {
        let dst = self.alloc_ssa(RegFile::GPR);
        if self.sm() >= 70 {
            self.push_op(OpIMad {
                dst: dst.into(),
                srcs: [x, y, 0.into()],
                signed: false,
            });
        } else {
            self.push_op(OpIMul {
                dst: dst.into(),
                srcs: [x, y],
                signed: [false; 2],
                high: false,
            });
        }
        dst
    }

    fn imul_2x32_64(&mut self, x: Src, y: Src, signed: bool) -> SSARef {
        let dst = self.alloc_ssa_vec(RegFile::GPR, 2);
        if self.sm() >= 70 {
            self.push_op(OpIMad64 {
                dst: dst.clone().into(),
                srcs: [x, y, 0.into()],
                signed,
            });
        } else {
            self.push_op(OpIMul {
                dst: dst[0].into(),
                srcs: [x.clone(), y.clone()],
                signed: [signed; 2],
                high: false,
            });
            self.push_op(OpIMul {
                dst: dst[1].into(),
                srcs: [x, y],
                signed: [signed; 2],
                high: true,
            });
        }
        dst
    }

    fn ineg(&mut self, i: Src) -> SSAValue {
        let dst = self.alloc_ssa(RegFile::GPR);
        if self.sm() >= 70 {
            self.push_op(OpIAdd3 {
                dst: dst.into(),
                overflow: [Dst::None, Dst::None],
                srcs: [0.into(), i.ineg(), 0.into()],
            });
        } else {
            self.push_op(OpIAdd2 {
                dst: dst.into(),
                srcs: [0.into(), i.ineg()],
                carry_out: Dst::None,
            });
        }
        dst
    }

    fn ineg64(&mut self, x: Src) -> SSARef {
        self.iadd64(0.into(), x.ineg(), 0.into())
    }

    fn isetp(
        &mut self,
        cmp_type: IntCmpType,
        cmp_op: IntCmpOp,
        x: Src,
        y: Src,
    ) -> SSAValue {
        let dst = self.alloc_ssa(RegFile::Pred);
        self.push_op(OpISetP {
            dst: dst.into(),
            set_op: PredSetOp::And,
            cmp_op: cmp_op,
            cmp_type: cmp_type,
            ex: false,
            srcs: [x, y],
            accum: true.into(),
            low_cmp: true.into(),
        });
        dst
    }

    fn isetp64(
        &mut self,
        cmp_type: IntCmpType,
        cmp_op: IntCmpOp,
        x: Src,
        y: Src,
    ) -> SSARef {
        let x = x.as_ssa().unwrap();
        let y = y.as_ssa().unwrap();

        // Low bits are always an unsigned comparison
        let low = self.isetp(IntCmpType::U32, cmp_op, x[0].into(), y[0].into());

        let dst = self.alloc_ssa(RegFile::Pred);
        match cmp_op {
            IntCmpOp::False | IntCmpOp::True => {
                panic!("These don't make sense for the builder helper");
            }
            IntCmpOp::Eq | IntCmpOp::Ne => {
                self.push_op(OpISetP {
                    dst: dst.into(),
                    set_op: match cmp_op {
                        IntCmpOp::Eq => PredSetOp::And,
                        IntCmpOp::Ne => PredSetOp::Or,
                        _ => panic!("Not an integer equality"),
                    },
                    cmp_op: cmp_op,
                    cmp_type: IntCmpType::U32,
                    ex: false,
                    srcs: [x[1].into(), y[1].into()],
                    accum: low.into(),
                    low_cmp: true.into(),
                });
            }
            IntCmpOp::Ge | IntCmpOp::Gt | IntCmpOp::Le | IntCmpOp::Lt => {
                if self.sm() >= 70 {
                    self.push_op(OpISetP {
                        dst: dst.into(),
                        set_op: PredSetOp::And,
                        cmp_op,
                        cmp_type,
                        ex: true,
                        srcs: [x[1].into(), y[1].into()],
                        accum: true.into(),
                        low_cmp: low.into(),
                    });
                } else {
                    // On Maxwell, iset.ex doesn't do what we want so we need to
                    // do it with 3 comparisons.  Fortunately, we can chain them
                    // together and don't need the extra logic that the NIR
                    // lowering would emit.
                    let low_and_high_eq = self.alloc_ssa(RegFile::Pred);
                    self.push_op(OpISetP {
                        dst: low_and_high_eq.into(),
                        set_op: PredSetOp::And,
                        cmp_op: IntCmpOp::Eq,
                        cmp_type: IntCmpType::U32,
                        ex: false,
                        srcs: [x[1].into(), y[1].into()],
                        accum: low.into(),
                        low_cmp: true.into(),
                    });
                    self.push_op(OpISetP {
                        dst: dst.into(),
                        set_op: PredSetOp::Or,
                        // We always want a strict inequality for the high part
                        // so it's false when the two are equal and safe to OR
                        // with the low part.
                        cmp_op: match cmp_op {
                            IntCmpOp::Lt | IntCmpOp::Le => IntCmpOp::Lt,
                            IntCmpOp::Gt | IntCmpOp::Ge => IntCmpOp::Gt,
                            _ => panic!("Not an integer inequality"),
                        },
                        cmp_type,
                        ex: false,
                        srcs: [x[1].into(), y[1].into()],
                        accum: low_and_high_eq.into(),
                        low_cmp: true.into(),
                    });
                }
            }
        }
        dst.into()
    }

    fn lea(&mut self, a: Src, b: Src, shift: u8) -> SSAValue {
        let dst = self.alloc_ssa(RegFile::GPR);
        assert!(self.sm() >= 70);

        self.push_op(OpLea {
            dst: dst.into(),
            overflow: Dst::None,
            a: a,
            b: b,
            a_high: 0.into(),
            dst_high: false,
            shift: shift % 32,
            intermediate_mod: SrcMod::None,
        });

        dst
    }

    fn lea64(&mut self, a: Src, b: Src, shift: u8) -> SSARef {
        assert!(self.sm() >= 70);
        assert!(a.is_unmodified());
        assert!(b.is_unmodified());

        let a = a.as_ssa().unwrap();
        let b = b.as_ssa().unwrap();
        let dst = self.alloc_ssa_vec(RegFile::GPR, 2);
        let shift = shift % 64;
        if shift >= 32 {
            self.copy_to(dst[0].into(), b[0].into());
            self.push_op(OpLea {
                dst: dst[1].into(),
                overflow: Dst::None,
                a: a[0].into(),
                b: b[1].into(),
                a_high: 0.into(),
                dst_high: false,
                shift: shift - 32,
                intermediate_mod: SrcMod::None,
            });
        } else {
            let carry = self.alloc_ssa(RegFile::Pred);
            self.push_op(OpLea {
                dst: dst[0].into(),
                overflow: carry.into(),
                a: a[0].into(),
                b: b[0].into(),
                a_high: 0.into(),
                dst_high: false,
                shift: shift,
                intermediate_mod: SrcMod::None,
            });
            self.push_op(OpLeaX {
                dst: dst[1].into(),
                overflow: Dst::None,
                a: a[0].into(),
                b: b[1].into(),
                a_high: a[1].into(),
                carry: carry.into(),
                dst_high: true,
                shift: shift,
                intermediate_mod: SrcMod::None,
            });
        }
        dst
    }

    fn lop2(&mut self, op: LogicOp2, x: Src, y: Src) -> SSAValue {
        let dst = if x.is_predicate() {
            self.alloc_ssa(RegFile::Pred)
        } else {
            self.alloc_ssa(RegFile::GPR)
        };
        self.lop2_to(dst.into(), op, x, y);
        dst
    }

    fn brev(&mut self, x: Src) -> SSAValue {
        let dst = self.alloc_ssa(RegFile::GPR);
        if self.sm() >= 70 {
            self.push_op(OpBRev {
                dst: dst.into(),
                src: x,
            });
        } else {
            // No BREV in Maxwell
            self.push_op(OpBfe {
                dst: dst.into(),
                base: x,
                signed: false,
                range: Src::new_imm_u32(0x2000),
                reverse: true,
            });
        }
        dst
    }

    fn mufu(&mut self, op: MuFuOp, src: Src) -> SSAValue {
        let dst = self.alloc_ssa(RegFile::GPR);
        self.push_op(OpMuFu {
            dst: dst.into(),
            op: op,
            src: src,
        });
        dst
    }

    fn fsin(&mut self, src: Src) -> SSAValue {
        let tmp = if self.sm() >= 70 {
            let frac_1_2pi = 1.0 / (2.0 * std::f32::consts::PI);
            self.fmul(src, frac_1_2pi.into())
        } else {
            let tmp = self.alloc_ssa(RegFile::GPR);
            self.push_op(OpRro {
                dst: tmp.into(),
                op: RroOp::SinCos,
                src,
            });
            tmp.into()
        };
        self.mufu(MuFuOp::Sin, tmp.into())
    }

    fn fcos(&mut self, src: Src) -> SSAValue {
        let tmp = if self.sm() >= 70 {
            let frac_1_2pi = 1.0 / (2.0 * std::f32::consts::PI);
            self.fmul(src, frac_1_2pi.into())
        } else {
            let tmp = self.alloc_ssa(RegFile::GPR);
            self.push_op(OpRro {
                dst: tmp.into(),
                op: RroOp::SinCos,
                src,
            });
            tmp.into()
        };
        self.mufu(MuFuOp::Cos, tmp.into())
    }

    fn fexp2(&mut self, src: Src) -> SSAValue {
        let tmp = if self.sm() >= 70 {
            src
        } else {
            let tmp = self.alloc_ssa(RegFile::GPR);
            self.push_op(OpRro {
                dst: tmp.into(),
                op: RroOp::Exp2,
                src,
            });
            tmp.into()
        };
        self.mufu(MuFuOp::Exp2, tmp)
    }

    fn prmt(&mut self, x: Src, y: Src, sel: [u8; 4]) -> SSAValue {
        let dst = self.alloc_ssa(RegFile::GPR);
        self.prmt_to(dst.into(), x, y, sel);
        dst
    }

    fn prmt4(&mut self, src: [Src; 4], sel: [u8; 4]) -> SSAValue {
        let max_sel = *sel.iter().max().unwrap();
        let [src0, src1, src2, src3] = src;
        if max_sel < 8 {
            self.prmt(src0, src1, sel)
        } else if max_sel < 12 {
            let mut sel_a = [0_u8; 4];
            let mut sel_b = [0_u8; 4];
            for i in 0..4_u8 {
                if sel[usize::from(i)] < 8 {
                    sel_a[usize::from(i)] = sel[usize::from(i)];
                    sel_b[usize::from(i)] = i;
                } else {
                    sel_b[usize::from(i)] = (sel[usize::from(i)] - 8) + 4;
                }
            }
            let a = self.prmt(src0, src1, sel_a);
            self.prmt(a.into(), src2, sel_b)
        } else if max_sel < 16 {
            let mut sel_a = [0_u8; 4];
            let mut sel_b = [0_u8; 4];
            let mut sel_c = [0_u8; 4];
            for i in 0..4_u8 {
                if sel[usize::from(i)] < 8 {
                    sel_a[usize::from(i)] = sel[usize::from(i)];
                    sel_c[usize::from(i)] = i;
                } else {
                    sel_b[usize::from(i)] = sel[usize::from(i)] - 8;
                    sel_c[usize::from(i)] = 4 + i;
                }
            }
            let a = self.prmt(src0, src1, sel_a);
            let b = self.prmt(src2, src3, sel_b);
            self.prmt(a.into(), b.into(), sel_c)
        } else {
            panic!("Invalid permute value: {max_sel}");
        }
    }

    fn sel(&mut self, cond: Src, x: Src, y: Src) -> SSAValue {
        assert!(cond.src_ref.is_predicate());
        assert!(x.is_predicate() == y.is_predicate());
        if x.is_predicate() {
            let dst = self.alloc_ssa(RegFile::Pred);
            if self.sm() >= 70 {
                self.push_op(OpPLop3 {
                    dsts: [dst.into(), Dst::None],
                    srcs: [cond, x, y],
                    ops: [
                        LogicOp3::new_lut(&|c, x, y| (c & x) | (!c & y)),
                        LogicOp3::new_const(false),
                    ],
                });
            } else {
                let tmp = self.alloc_ssa(RegFile::Pred);
                self.push_op(OpPSetP {
                    dsts: [tmp.into(), Dst::None],
                    ops: [PredSetOp::And, PredSetOp::And],
                    srcs: [cond.clone(), x, true.into()],
                });
                self.push_op(OpPSetP {
                    dsts: [dst.into(), Dst::None],
                    ops: [PredSetOp::And, PredSetOp::Or],
                    srcs: [cond.bnot(), y, tmp.into()],
                });
            }
            dst
        } else {
            let dst = self.alloc_ssa(RegFile::GPR);
            self.push_op(OpSel {
                dst: dst.into(),
                cond: cond,
                srcs: [x, y],
            });
            dst
        }
    }

    fn undef(&mut self) -> SSAValue {
        let dst = self.alloc_ssa(RegFile::GPR);
        self.push_op(OpUndef { dst: dst.into() });
        dst
    }

    fn copy(&mut self, src: Src) -> SSAValue {
        let dst = if src.is_predicate() {
            self.alloc_ssa(RegFile::Pred)
        } else {
            self.alloc_ssa(RegFile::GPR)
        };
        self.copy_to(dst.into(), src);
        dst.into()
    }

    fn bmov_to_bar(&mut self, src: Src) -> SSAValue {
        assert!(src.src_ref.as_ssa().unwrap().file() == RegFile::GPR);
        let dst = self.alloc_ssa(RegFile::Bar);
        self.push_op(OpBMov {
            dst: dst.into(),
            src: src,
            clear: false,
        });
        dst
    }

    fn bmov_to_gpr(&mut self, src: Src) -> SSAValue {
        assert!(src.src_ref.as_ssa().unwrap().file() == RegFile::Bar);
        let dst = self.alloc_ssa(RegFile::GPR);
        self.push_op(OpBMov {
            dst: dst.into(),
            src: src,
            clear: false,
        });
        dst
    }
}

pub struct InstrBuilder<'a> {
    instrs: MappedInstrs,
    sm: &'a dyn ShaderModel,
}

impl<'a> InstrBuilder<'a> {
    pub fn new(sm: &'a dyn ShaderModel) -> Self {
        Self {
            instrs: MappedInstrs::None,
            sm,
        }
    }
}

impl InstrBuilder<'_> {
    pub fn into_vec(self) -> Vec<Instr> {
        match self.instrs {
            MappedInstrs::None => Vec::new(),
            MappedInstrs::One(i) => vec![i],
            MappedInstrs::Many(v) => v,
        }
    }

    pub fn into_mapped_instrs(self) -> MappedInstrs {
        self.instrs
    }
}

impl Builder for InstrBuilder<'_> {
    fn push_instr(&mut self, instr: Instr) -> &mut Instr {
        self.instrs.push(instr);
        self.instrs.last_mut().unwrap()
    }

    fn sm(&self) -> u8 {
        self.sm.sm()
    }
}

pub struct SSAInstrBuilder<'a> {
    b: InstrBuilder<'a>,
    alloc: &'a mut SSAValueAllocator,
}

impl<'a> SSAInstrBuilder<'a> {
    pub fn new(
        sm: &'a dyn ShaderModel,
        alloc: &'a mut SSAValueAllocator,
    ) -> Self {
        Self {
            b: InstrBuilder::new(sm),
            alloc: alloc,
        }
    }
}

impl SSAInstrBuilder<'_> {
    pub fn into_vec(self) -> Vec<Instr> {
        self.b.into_vec()
    }

    #[allow(dead_code)]
    pub fn into_mapped_instrs(self) -> MappedInstrs {
        self.b.into_mapped_instrs()
    }
}

impl Builder for SSAInstrBuilder<'_> {
    fn push_instr(&mut self, instr: Instr) -> &mut Instr {
        self.b.push_instr(instr)
    }

    fn sm(&self) -> u8 {
        self.b.sm()
    }
}

impl SSABuilder for SSAInstrBuilder<'_> {
    fn alloc_ssa(&mut self, file: RegFile) -> SSAValue {
        self.alloc.alloc(file)
    }

    fn alloc_ssa_vec(&mut self, file: RegFile, comps: u8) -> SSARef {
        self.alloc.alloc_vec(file, comps)
    }
}

pub struct PredicatedBuilder<'a, T: Builder> {
    b: &'a mut T,
    pred: Pred,
}

impl<T: Builder> Builder for PredicatedBuilder<'_, T> {
    fn push_instr(&mut self, instr: Instr) -> &mut Instr {
        let mut instr = instr;
        assert!(instr.pred.is_true());
        instr.pred = self.pred;
        self.b.push_instr(instr)
    }

    fn sm(&self) -> u8 {
        self.b.sm()
    }
}

impl<T: SSABuilder> SSABuilder for PredicatedBuilder<'_, T> {
    fn alloc_ssa(&mut self, file: RegFile) -> SSAValue {
        self.b.alloc_ssa(file)
    }

    fn alloc_ssa_vec(&mut self, file: RegFile, comps: u8) -> SSARef {
        self.b.alloc_ssa_vec(file, comps)
    }
}

pub struct UniformBuilder<'a, T: Builder> {
    b: &'a mut T,
    uniform: bool,
}

impl<'a, T: Builder> UniformBuilder<'a, T> {
    pub fn new(b: &'a mut T, uniform: bool) -> Self {
        Self { b, uniform }
    }
}

impl<T: Builder> Builder for UniformBuilder<'_, T> {
    fn push_instr(&mut self, instr: Instr) -> &mut Instr {
        self.b.push_instr(instr)
    }

    fn sm(&self) -> u8 {
        self.b.sm()
    }
}

impl<T: SSABuilder> SSABuilder for UniformBuilder<'_, T> {
    fn alloc_ssa(&mut self, file: RegFile) -> SSAValue {
        let file = if self.uniform {
            file.to_uniform().unwrap()
        } else {
            file
        };
        self.b.alloc_ssa(file)
    }

    fn alloc_ssa_vec(&mut self, file: RegFile, comps: u8) -> SSARef {
        let file = if self.uniform {
            file.to_uniform().unwrap()
        } else {
            file
        };
        self.b.alloc_ssa_vec(file, comps)
    }
}
