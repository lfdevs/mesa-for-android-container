use std::iter;

use compiler::float16::F16;
use compiler::smallvec::SmallVec;

use crate::bitview::{BitMutViewable, BitViewable};
use crate::ir::*;

pub trait FoldDataView {
    fn get_src(&self, src: &Src) -> u64;

    fn set_dst(&mut self, dst: &Dst, data: u64);

    fn get_op(&self) -> &impl Opcode;

    fn get_f32(&self, src: &Src) -> f32 {
        let data_type = self.get_op().src_type(src);
        let raw = self.get_src(src);
        assert!(data_type.is_float_type());
        match data_type.bits() {
            16 => F16::from_bits(raw as u16).into(),
            32 => f32::from_bits(raw as u32),
            _ => panic!("Invalid float"),
        }
    }

    fn set_f32(&mut self, dst: &Dst, raw: f32) {
        let data_type = self.get_op().dst_type(dst);
        assert!(data_type.is_float_type());
        match data_type.bits() {
            16 => self.set_dst(dst, F16::from_f32_rtne(raw).to_bits().into()),
            32 => self.set_dst(dst, raw.to_bits().into()),
            _ => panic!("Invalid float"),
        }
    }
}

pub struct FoldData<'a, O> {
    pub dsts: &'a mut [u64],
    pub srcs: &'a [u64],
    pub op: &'a O,
}

impl<'a, O: Opcode> FoldDataView for FoldData<'a, O> {
    fn get_src(&self, src: &Src) -> u64 {
        let idx = self.op.src_idx(src);
        let data = match &src.src_ref {
            SrcRef::Zero => 0u64,
            SrcRef::Imm32(i) => i.get() as u64,
            SrcRef::FAU(fauref) => {
                assert!(fauref.page == FAUPage::SmallConst);
                todo!();
            }
            SrcRef::SSA(_) => self.srcs[idx],
            SrcRef::Reg(_) | SrcRef::Mem(_) => todo!(),
        };
        let src_type = self.op.src_type(src);

        let data = match src_type.total_bits() {
            8 | 16 | 32 => src
                .swizzle
                .fold_u32(data as u32)
                .and_then(|x| src.src_mod.fold_u32(src_type, x))
                .map(|x| x.into()),
            64 => src
                .swizzle
                .fold_u64(data)
                .and_then(|x| src.src_mod.fold_u64(x)),
            _ => panic!("Invalid source width"),
        }
        .expect("Invalid swizzle or modifier");

        data
    }

    fn set_dst(&mut self, dst: &Dst, data: u64) {
        let dst_idx = self.op.dst_idx(dst);
        let dst_type = self.op.dst_type(dst);

        self.dsts[dst_idx] = match dst_type.total_bits() {
            8 | 16 | 32 => {
                let mask = dst.lanes.u32_mask().unwrap();
                ((data as u32) & mask) as u64
            }
            64 => {
                assert_eq!(dst.lanes, DstLanes::All);
                data
            }
            _ => unreachable!(),
        }
    }

    fn get_op(&self) -> &impl Opcode {
        self.op
    }
}

pub trait Foldable: Opcode {
    // Currently only used by test code
    #[allow(dead_code)]
    fn fold(&self, model: &dyn Model, f: &mut impl FoldDataView);
}

struct PerCompFoldData<'a, 'b, D, O> {
    backend: &'b mut D,
    dst: &'a mut [u64],
    op: &'a O,
    comp_index: u8,
}

impl<D: FoldDataView, O: Opcode> FoldDataView
    for PerCompFoldData<'_, '_, D, O>
{
    fn get_src(&self, src: &Src) -> u64 {
        let data = self.backend.get_src(src);
        let bits = self.op.src_type(src).bits() as usize;

        let start = self.comp_index as usize * bits;
        data.get_bit_range_u64(start..(start + bits))
    }

    fn set_dst(&mut self, dst: &Dst, data: u64) {
        let dst_idx = self.op.dst_idx(dst);

        let bits = self.op.dst_type(dst).bits() as usize;
        let start = self.comp_index as usize * bits;

        let data = data.get_bit_range_u64(0..bits);
        self.dst[dst_idx].set_bit_range_u64(start..(start + bits), data);
    }

    fn get_op(&self) -> &impl Opcode {
        self.op
    }
}

// Like Foldable, but abstracts the DataType handling and calls the folding method
// one time for each component.
pub trait PerCompFoldable: Opcode + HasVariants {
    fn fold_comp(&self, model: &dyn Model, f: &mut impl FoldDataView);
}

impl<T: PerCompFoldable> Foldable for T {
    fn fold(&self, model: &dyn Model, f: &mut impl FoldDataView) {
        let variant = HasVariants::variant(self);
        let mut dst_vec: SmallVec<_> =
            iter::repeat_n(0_u64, self.dsts().len()).collect();

        for i in 0..variant.comps() {
            let mut data = PerCompFoldData {
                backend: f,
                dst: &mut dst_vec,
                op: self,
                comp_index: i,
            };

            self.fold_comp(model, &mut data);
        }

        for (val, dst) in dst_vec.iter().zip(self.dsts().iter()) {
            f.set_dst(dst, *val);
        }
    }
}
