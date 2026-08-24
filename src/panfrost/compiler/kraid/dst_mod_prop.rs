// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::ir::*;
use crate::ops::*;
use crate::ssa_value::AllocSSA;
use rustc_hash::FxHashMap;

#[derive(Clone, Copy, Default, PartialEq)]
enum Uses<T: Clone + Copy + PartialEq> {
    #[default]
    None,
    One(T),
    Unknown,
}

impl<T: Clone + Copy + PartialEq> Uses<T> {
    fn add_use(&mut self, u: Uses<T>) {
        match u {
            Uses::None => panic!("None is only an initializer"),
            Uses::One(uv) => match *self {
                Uses::None => *self = u,
                Uses::One(sv) => {
                    if uv != sv {
                        *self = Uses::Unknown;
                    }
                }
                Uses::Unknown => (),
            },
            Uses::Unknown => *self = Uses::Unknown,
        }
    }
}

#[derive(Clone, Copy, Default)]
enum ClampUses {
    #[default]
    None,
    F16(FClamp),
    F32(FClamp),
    Unknown,
}

impl ClampUses {
    fn add_use(&mut self, use_clamp: ClampUses) {
        match (*self, use_clamp) {
            (_, ClampUses::None) => (),
            (ClampUses::None, _) => *self = use_clamp,
            (ClampUses::F16(s), ClampUses::F16(u)) => {
                *self = ClampUses::F16(s.max(u))
            }
            (ClampUses::F32(s), ClampUses::F32(u)) => {
                *self = ClampUses::F32(s.max(u))
            }
            _ => *self = ClampUses::Unknown,
        }
    }

    fn get_clamp(&self, dst_type: DataType) -> Option<FClamp> {
        match (self, dst_type) {
            (ClampUses::None, _) => Some(FClamp::None),
            (ClampUses::F16(clamp), DataType::F16 | DataType::V2F16) => {
                Some(*clamp)
            }
            (ClampUses::F32(clamp), DataType::F32) => Some(*clamp),
            _ => None,
        }
    }
}

#[derive(Default)]
struct UseMod {
    clamp: ClampUses,
    to_f16: Uses<FRound>,
}

impl UseMod {
    const INVALID: UseMod = UseMod {
        clamp: ClampUses::Unknown,
        to_f16: Uses::Unknown,
    };

    fn add_use(&mut self, use_mod: UseMod) {
        self.clamp.add_use(use_mod.clamp);
        self.to_f16.add_use(use_mod.to_f16);
    }
}

#[derive(Clone, Copy, Default)]
enum SrcType {
    #[default]
    None,
    F16,
    F32,
}

impl SrcType {
    fn from_src(src: &Src, src_type: DataType) -> SrcType {
        match src_type {
            DataType::F16 | DataType::V2F16 => {
                debug_assert!(matches!(
                    src.swizzle,
                    Swizzle::H00 | Swizzle::H01 | Swizzle::H10 | Swizzle::H11
                ));
                SrcType::F16
            }
            DataType::F32 => {
                if src.swizzle.is_f16_widen() {
                    SrcType::F16
                } else if src.swizzle.is_none() {
                    SrcType::F32
                } else {
                    SrcType::None
                }
            }
            _ => SrcType::None,
        }
    }

    fn is_none(&self) -> bool {
        matches!(self, SrcType::None)
    }
}

struct DstModProp<'a, A> {
    ssa_alloc: &'a mut A,
    use_mods: FxHashMap<SSAValue, UseMod>,
    def_clamp: FxHashMap<SSAValue, ClampUses>,
    def_f16: FxHashMap<SSAValue, SSAValue>,
}

impl<A: AllocSSA> DstModProp<'_, A> {
    fn new<'a>(ssa_alloc: &'a mut A) -> DstModProp<'a, A> {
        DstModProp {
            ssa_alloc,
            use_mods: Default::default(),
            def_clamp: Default::default(),
            def_f16: Default::default(),
        }
    }

    fn add_use(&mut self, ssa: SSAValue, use_mod: UseMod) {
        self.use_mods.entry(ssa).or_default().add_use(use_mod);
    }

    fn set_def_clamp(&mut self, ssa: SSAValue, clamp: ClampUses) {
        let old = self.def_clamp.insert(ssa, clamp);
        debug_assert!(old.is_none());
    }

    fn set_def_f16(&mut self, ssa: SSAValue, f16: SSAValue) {
        let old = self.def_f16.insert(ssa, f16);
        debug_assert!(old.is_none());
    }

    fn add_f32_to_f16_uses(&mut self, op: &OpF32ToF16) {
        let SrcRef::SSA(vec) = &op.src.src_ref else {
            return;
        };
        debug_assert!(vec.comps() == 1);
        let ssa = vec[0];

        debug_assert!(ssa.bits() == 32);
        if !op.src.src_mod.is_none() {
            self.add_use(ssa, UseMod::INVALID);
            return;
        }

        debug_assert!(op.src.swizzle.is_none());
        let use_mod = UseMod {
            clamp: ClampUses::F32(op.clamp),
            to_f16: Uses::One(op.round),
        };
        self.add_use(ssa, use_mod);
    }

    fn add_fadd_uses(&mut self, op: &OpFAdd) {
        let src = if op.srcs[0].is_fneg_zero(op.dst_type) {
            &op.srcs[1]
        } else if op.srcs[1].is_fneg_zero(op.dst_type) {
            &op.srcs[0]
        } else {
            for ssa in op.iter_ssa_uses() {
                self.add_use(*ssa, UseMod::INVALID);
            }
            return;
        };

        let SrcRef::SSA(vec) = &src.src_ref else {
            return;
        };
        debug_assert!(vec.comps() == 1);
        let ssa = vec[0];

        if !src.src_mod.is_none() {
            self.add_use(ssa, UseMod::INVALID);
            return;
        }

        let use_mod = match SrcType::from_src(src, op.dst_type) {
            SrcType::None => UseMod::INVALID,
            SrcType::F16 => UseMod {
                clamp: ClampUses::F16(op.clamp),
                to_f16: Uses::Unknown,
            },
            SrcType::F32 => UseMod {
                clamp: ClampUses::F32(op.clamp),
                to_f16: if op.dst.lanes.is_f16_narrow() {
                    Uses::One(op.round)
                } else {
                    Uses::Unknown
                },
            },
        };
        self.add_use(ssa, use_mod);
    }

    fn add_instr_uses(&mut self, instr: &Instr) {
        match &instr.op {
            Op::F32ToF16(op) => self.add_f32_to_f16_uses(op.as_ref()),
            Op::FAdd(op) => self.add_fadd_uses(op.as_ref()),
            _ => {
                for ssa in instr.iter_ssa_uses() {
                    self.add_use(*ssa, UseMod::INVALID);
                }
            }
        }
    }

    fn try_remove_clamp(
        &self,
        clamp: &mut FClamp,
        src_ssa: SSAValue,
        src_type: DataType,
    ) -> bool {
        if clamp.is_none() {
            return false;
        }

        let def_clamp = match self.def_clamp.get(&src_ssa) {
            None => return false,
            Some(ClampUses::F16(def_clamp)) => {
                debug_assert!(matches!(
                    src_type,
                    DataType::F16 | DataType::V2F16
                ));
                *def_clamp
            }
            Some(ClampUses::F32(def_clamp)) => {
                debug_assert!(src_type == DataType::F32);
                *def_clamp
            }
            Some(_) => unreachable!(),
        };

        if def_clamp == def_clamp.min(*clamp) {
            *clamp = FClamp::None;
            true
        } else {
            false
        }
    }

    fn try_fold_instr_src(&mut self, instr: &mut Instr) -> bool {
        // This must match the same cases as add_instr_uses()
        match &mut instr.op {
            Op::F32ToF16(op) => {
                let SrcRef::SSA(vec) = &op.src.src_ref else {
                    return false;
                };
                debug_assert!(vec.comps() == 1);
                let ssa = vec[0];

                let clamp_changed =
                    self.try_remove_clamp(&mut op.clamp, ssa, DataType::F32);

                if let Some(ssa_f16) = self.def_f16.get(&ssa) {
                    // If our source is now an f16, we need to convert to a
                    // (possibly no-op) FADD.f16
                    instr.op = Op::from(OpFAdd {
                        dst: op.dst.clone(),
                        dst_type: DataType::F16,
                        round: op.round,
                        clamp: op.clamp,
                        srcs: [(*ssa_f16).into(), Src::fneg_zero(16)],
                    });
                    true
                } else {
                    clamp_changed
                }
            }
            Op::FAdd(op) => {
                let src = if op.srcs[0].is_fneg_zero(op.dst_type) {
                    &mut op.srcs[1]
                } else if op.srcs[1].is_fneg_zero(op.dst_type) {
                    &mut op.srcs[0]
                } else {
                    return false;
                };

                let SrcRef::SSA(vec) = &src.src_ref else {
                    return false;
                };
                debug_assert!(vec.comps() == 1);
                let ssa = vec[0];

                let clamp_changed =
                    self.try_remove_clamp(&mut op.clamp, ssa, op.dst_type);

                if let Some(ssa_f16) = self.def_f16.get(&ssa) {
                    debug_assert!(op.dst_type == DataType::F32);
                    op.dst.lanes = match op.dst.lanes {
                        DstLanes::AnyHF => DstLanes::AnyH,
                        DstLanes::HF0 => DstLanes::H0,
                        DstLanes::HF1 => DstLanes::H1,
                        _ => panic!("Must be a f16 narrow"),
                    };
                    op.dst_type = DataType::F16;
                    src.src_ref = (*ssa_f16).into();
                    src.swizzle = Swizzle::H00;
                    true
                } else {
                    clamp_changed
                }
            }
            _ => false,
        }
    }

    fn fold_clamp(
        &mut self,
        dst: &Dst,
        dst_type: DataType,
        clamp: &mut FClamp,
    ) -> bool {
        let DstRef::SSA(vec) = &dst.dst_ref else {
            return false;
        };
        debug_assert!(vec.comps() == 1);
        let ssa = vec[0];

        let mut changed = false;
        if let Some(use_mod) = self.use_mods.get(&ssa) {
            if let Some(use_clamp) = use_mod.clamp.get_clamp(dst_type) {
                let new_clamp = clamp.min(use_clamp);
                changed = new_clamp != *clamp;
                *clamp = new_clamp;
            }
        }

        match dst_type {
            DataType::F16 | DataType::V2F16 => {
                self.set_def_clamp(ssa, ClampUses::F16(*clamp));
            }
            DataType::F32 => {
                if dst.lanes.is_f16_narrow() {
                    self.set_def_clamp(ssa, ClampUses::F16(*clamp));
                } else {
                    self.set_def_clamp(ssa, ClampUses::F32(*clamp));
                }
            }
            _ => panic!("Invlaid dst_type"),
        }

        changed
    }

    fn fold_to_f32(
        &mut self,
        dst: &mut Dst,
        dst_type: DataType,
        round: FRound,
    ) -> bool {
        if dst_type != DataType::F32 {
            return false;
        }

        let DstRef::SSA(vec) = &mut dst.dst_ref else {
            return false;
        };
        debug_assert!(vec.comps() == 1);
        let ssa = vec[0];

        let Some(use_mod) = self.use_mods.get(&ssa) else {
            return false;
        };

        if use_mod.to_f16 != Uses::One(round) {
            return false;
        }

        debug_assert!(ssa.bits() == 32);
        let ssa_f16 = self.ssa_alloc.alloc_ssa(16);
        self.set_def_f16(ssa, ssa_f16);
        dst.dst_ref = ssa_f16.into();
        dst.lanes = DstLanes::AnyHF;
        true
    }

    fn try_fold_instr(&mut self, instr: &mut Instr) -> bool {
        let src_folded = self.try_fold_instr_src(instr);
        let dst_folded = match &mut instr.op {
            Op::F32ToF16(op) => {
                self.fold_clamp(&op.dst, DataType::F32, &mut op.clamp)
            }
            Op::FAdd(op) => {
                self.fold_clamp(&op.dst, op.dst_type, &mut op.clamp)
                    | self.fold_to_f32(&mut op.dst, op.dst_type, op.round)
            }
            Op::Fma(op) => {
                self.fold_clamp(&op.dst, op.dst_type, &mut op.clamp)
                    | self.fold_to_f32(&mut op.dst, op.dst_type, op.round)
            }
            Op::FMax(op) => {
                self.fold_clamp(&op.dst, op.dst_type, &mut op.clamp)
            }
            Op::FMin(op) => {
                self.fold_clamp(&op.dst, op.dst_type, &mut op.clamp)
            }
            _ => false,
        };
        src_folded | dst_folded
    }
}

impl Shader<'_> {
    pub fn opt_dst_mod_prop(&mut self) -> bool {
        let mut mod_prop = DstModProp::new(&mut self.ssa_alloc);

        for block in self.blocks.iter() {
            for instr in block.instrs.iter() {
                mod_prop.add_instr_uses(instr);
            }
        }

        let mut progress = false;
        for block in self.blocks.iter_mut() {
            for instr in block.instrs.iter_mut() {
                progress |= mod_prop.try_fold_instr(instr);
            }
        }
        progress
    }
}
