// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::ir::*;
use crate::ops::*;
use crate::ssa_value::SSAValueAllocator;

pub trait Builder {
    fn arch(&self) -> u8;

    fn model(&self) -> &dyn Model;

    fn push_instr(&mut self, instr: Instr) -> &mut Instr;

    fn push_op(&mut self, op: impl Into<Op>) -> &mut Instr {
        self.push_instr(Instr::from(op))
    }

    fn copy_to(&mut self, dst: Dst, dst_type: DataType, src: Src) {
        self.push_op(OpCopy { dst, dst_type, src });
    }

    fn copy_i8_to(&mut self, dst: Dst, src: Src) {
        self.copy_to(dst, DataType::I8, src);
    }

    fn copy_i16_to(&mut self, dst: Dst, src: Src) {
        self.copy_to(dst, DataType::I16, src);
    }

    fn copy_i32_to(&mut self, dst: Dst, src: Src) {
        self.copy_to(dst, DataType::I32, src);
    }
}

pub trait SSABuilder: Builder {
    fn alloc_ssa(&mut self, bits: u8) -> SSAValue;

    fn alloc_vec(&mut self, comps: u8) -> SSARef;

    fn copy_i8(&mut self, src: Src) -> SSAValue {
        let def = self.alloc_ssa(8);
        self.copy_i8_to(def.into(), src);
        def
    }

    fn copy_i16(&mut self, src: Src) -> SSAValue {
        let def = self.alloc_ssa(16);
        self.copy_i16_to(def.into(), src);
        def
    }

    fn copy_i32(&mut self, src: Src) -> SSAValue {
        let def = self.alloc_ssa(32);
        self.copy_i32_to(def.into(), src);
        def
    }

    fn mkvec_v2i8(&mut self, x: Src, y: Src) -> SSAValue {
        let def = self.alloc_ssa(16);
        self.push_op(OpMkVecV2I8 {
            dst: def.into(),
            srcs: [x, y],
        });
        def
    }

    fn mkvec_v2i16(&mut self, x: Src, y: Src) -> SSAValue {
        self.mkvec_v4i8(
            x.clone().byte(0),
            x.clone().byte(1),
            y.clone().byte(0),
            y.clone().byte(1),
        )
    }

    fn mkvec_v4i8(&mut self, x: Src, y: Src, z: Src, w: Src) -> SSAValue {
        let def = self.alloc_ssa(32);
        self.push_op(OpMkVecV4I8 {
            dst: def.into(),
            srcs: [x, y, z, w],
        });
        def
    }
}

pub struct InstrBuilder<'a> {
    arch: u8,
    model: &'a dyn Model,
    instrs: MappedInstrs,
}

impl<'a> InstrBuilder<'a> {
    pub fn new(model: &'a dyn Model) -> Self {
        InstrBuilder {
            arch: model.arch(),
            model: model,
            instrs: Default::default(),
        }
    }

    pub fn into_mapped(self) -> MappedInstrs {
        self.instrs
    }

    pub fn into_vec(self) -> Vec<Instr> {
        self.instrs.into()
    }
}

impl<'a> Builder for InstrBuilder<'a> {
    fn arch(&self) -> u8 {
        self.arch
    }

    fn model(&self) -> &'a dyn Model {
        self.model
    }

    fn push_instr(&mut self, instr: Instr) -> &mut Instr {
        self.instrs.push(instr);
        self.instrs.last_mut().unwrap()
    }
}

pub struct SSAInstrBuilder<'a> {
    b: InstrBuilder<'a>,
    alloc: &'a mut SSAValueAllocator,
}

impl<'a> SSAInstrBuilder<'a> {
    pub fn new(
        model: &'a dyn Model,
        alloc: &'a mut SSAValueAllocator,
    ) -> SSAInstrBuilder<'a> {
        SSAInstrBuilder {
            b: InstrBuilder::new(model),
            alloc,
        }
    }

    pub fn into_mapped(self) -> MappedInstrs {
        self.b.into_mapped()
    }

    pub fn into_vec(self) -> Vec<Instr> {
        self.b.into_vec()
    }
}

impl<'a> Builder for SSAInstrBuilder<'a> {
    fn arch(&self) -> u8 {
        self.b.arch()
    }

    fn model(&self) -> &'a dyn Model {
        self.b.model
    }

    fn push_instr(&mut self, instr: Instr) -> &mut Instr {
        self.b.push_instr(instr)
    }
}

impl SSABuilder for SSAInstrBuilder<'_> {
    fn alloc_ssa(&mut self, bits: u8) -> SSAValue {
        self.alloc.alloc(bits)
    }

    fn alloc_vec(&mut self, comps: u8) -> SSARef {
        self.alloc.alloc_vec(comps)
    }
}
