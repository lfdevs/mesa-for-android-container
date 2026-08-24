// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::encode_v9::*;
use crate::ir::*;
use crate::isa::ExecUnit;
use kraid_bindings::*;

pub use kraid_bindings::pan_model as PanModel;

pub struct SmallConstantTable(Vec<SmallConstant>);

impl<'a> IntoIterator for &'a SmallConstantTable {
    type Item = &'a SmallConstant;
    type IntoIter = std::slice::Iter<'a, SmallConstant>;

    fn into_iter(self) -> Self::IntoIter {
        self.0.iter()
    }
}

pub struct FAUModel {
    user_fau_page_words: u16,
    pub small_constants: SmallConstantTable,
    special_fn: Box<dyn Fn(SpecialFAU) -> Option<FAURef> + Send + Sync>,

    /// Instructions before v14 require all the FAU entries to have the
    /// same FAU RAM index, we can access two distinct 32-bit words but
    /// they need to be "aligned" to the same 64-bit address.
    /// This limit has been lifted from v14
    pub single_fau_ram_index: bool,
}

impl FAUModel {
    pub fn user_page_idx(&self, word_idx: u16) -> u8 {
        let page = word_idx / self.user_fau_page_words;
        assert!(page < 4);
        page as u8
    }

    pub fn special(&self, special: SpecialFAU) -> Option<FAURef> {
        (self.special_fn)(special)
    }
}

pub trait Model {
    fn arch(&self) -> u8;

    fn pan_model(&self) -> &PanModel;

    fn fau(&self) -> &FAUModel;

    fn encode_shader(&self, s: &Shader<'_>) -> Vec<u32>;

    fn op_is_supported(&self, op: &Op) -> bool;

    fn op_exec_unit(&self, op: &Op) -> Option<ExecUnit>;

    fn op_is_message(&self, op: &Op) -> bool;

    fn op_src_is_staging_reg(&self, op: &Op, src: &Src) -> bool;

    fn op_src_is_64bit(&self, op: &Op, src: &Src) -> bool;

    fn op_src_supports_imm32(&self, op: &Op, src: &Src, imm: u32) -> bool;

    fn op_src_supports_swizzle(
        &self,
        op: &Op,
        src: &Src,
        swizzle: Swizzle,
    ) -> bool;

    fn op_src_supports_mod(&self, op: &Op, src: &Src, src_mod: SrcMod) -> bool;

    fn op_dst_is_staging_reg(&self, op: &Op) -> bool;

    fn op_dst_supported_lanes(&self, op: &Op) -> DstLanesSet;

    fn op_dst_supports_lanes(&self, op: &Op, lanes: DstLanes) -> bool {
        self.op_dst_supported_lanes(op).contains(lanes)
    }

    fn preload_reg(&self, preload: PreloadReg) -> Option<RegRef>;

    fn subgroup_size(&self) -> u8 {
        unsafe { pan_subgroup_size(self.arch().into()).try_into().unwrap() }
    }

    fn max_threads(&self, registers_used: u8) -> u8;
}

struct ValhallModel {
    arch: u8,
    pan_model: std::ptr::NonNull<PanModel>,
    fau: FAUModel,
}

// SAFETY: The pan_model pointer points to static data
unsafe impl Send for ValhallModel {}
unsafe impl Sync for ValhallModel {}

impl ValhallModel {
    fn new(arch: u8, pan_model: std::ptr::NonNull<PanModel>) -> ValhallModel {
        use crate::isa::{SmallConstantTable, v9};
        let sc_table = SmallConstantTable(v9::SmallConstantT::collect(arch));
        let fau = FAUModel {
            user_fau_page_words: 64,
            small_constants: sc_table,
            special_fn: Box::new(move |special| {
                ValhallModel::special_fau(special, arch)
            }),
            single_fau_ram_index: arch < 14,
        };
        ValhallModel {
            arch,
            pan_model,
            fau,
        }
    }

    #[inline]
    fn special_fau(special: SpecialFAU, arch: u8) -> Option<FAURef> {
        use SpecialFAU::*;

        let (page, idx) = match special {
            WarpId => (0, 0b0010),
            FramebufferSize => (0, 0b0100),
            ATestDatum => (0, 0b0101),
            Sample => (0, 0b0110),
            BlendDescriptor0 => (0, 0b1000 | 0),
            BlendDescriptor1 => (0, 0b1000 | 1),
            BlendDescriptor2 => (0, 0b1000 | 2),
            BlendDescriptor3 => (0, 0b1000 | 3),
            BlendDescriptor4 => (0, 0b1000 | 4),
            BlendDescriptor5 => (0, 0b1000 | 5),
            BlendDescriptor6 => (0, 0b1000 | 6),
            BlendDescriptor7 => (0, 0b1000 | 7),
            ThreadLocalPointer => (1, 0b0001),
            WorkgroupLocalPointer => (1, 0b0011),
            ResourceTablePointer => (1, 0b0111),
            LaneId => (3, 0b0001),
            CoreId => (3, 0b0011),
            ShaderOutput => {
                if arch < 12 {
                    return None;
                }
                (3, 0b0100)
            }
            PrepassState => {
                if arch < 13 {
                    return None;
                }
                (3, 0b0101)
            }
            Pc => (3, 0b1111),
        };

        Some(FAURef {
            page: match page {
                0 => FAUPage::Special0,
                1 => FAUPage::Special1,
                3 => FAUPage::Special3,
                _ => panic!("Invalid FAU special page"),
            },
            idx: idx << 1, // FAURef::idx is in units of 32-bit words
            special: Some(special),
            load64: true,
        })
    }
}

impl Model for ValhallModel {
    fn arch(&self) -> u8 {
        self.arch
    }

    fn pan_model(&self) -> &PanModel {
        unsafe { self.pan_model.as_ref() }
    }

    fn fau(&self) -> &FAUModel {
        &self.fau
    }

    fn encode_shader(&self, s: &Shader<'_>) -> Vec<u32> {
        encode_v9(s, self.arch)
    }

    fn op_is_supported(&self, op: &Op) -> bool {
        op.as_virtual().is_some() || v9_op_is_supported(op, self.arch)
    }

    fn op_exec_unit(&self, op: &Op) -> Option<ExecUnit> {
        v9_op_exec_unit(op, self.arch)
    }

    fn op_is_message(&self, op: &Op) -> bool {
        if let Some(vop) = op.as_virtual() {
            vop.is_message()
        } else {
            v9_op_is_message(op, self.arch)
        }
    }

    fn op_src_is_staging_reg(&self, op: &Op, src: &Src) -> bool {
        if let Some(vop) = op.as_virtual() {
            vop.src_is_staging_reg(src)
        } else {
            v9_op_src_is_staging_reg(op, src, self.arch)
        }
    }

    fn op_src_is_64bit(&self, op: &Op, src: &Src) -> bool {
        if let Some(vop) = op.as_virtual() {
            vop.src_is_64bit(src)
        } else {
            v9_op_src_is_64bit(op, src, self.arch)
        }
    }

    fn op_src_supports_imm32(&self, op: &Op, src: &Src, imm: u32) -> bool {
        if let Some(vop) = op.as_virtual() {
            vop.src_supports_imm32(src, imm)
        } else {
            v9_op_src_supports_imm32(op, src, self.arch, imm)
        }
    }

    fn op_src_supports_swizzle(
        &self,
        op: &Op,
        src: &Src,
        swizzle: Swizzle,
    ) -> bool {
        if let Some(vop) = op.as_virtual() {
            vop.src_supports_swizzle(src, swizzle)
        } else {
            v9_op_src_supports_swizzle(op, src, self.arch, swizzle)
        }
    }

    fn op_src_supports_mod(&self, op: &Op, src: &Src, src_mod: SrcMod) -> bool {
        if let Some(vop) = op.as_virtual() {
            vop.src_supports_mod(src, src_mod)
        } else {
            v9_op_src_supports_mod(op, src, self.arch, src_mod)
        }
    }

    fn op_dst_is_staging_reg(&self, op: &Op) -> bool {
        if let Some(vop) = op.as_virtual() {
            vop.dst_is_staging_reg()
        } else {
            v9_op_dst_is_staging_reg(op, self.arch)
        }
    }

    fn op_dst_supported_lanes(&self, op: &Op) -> DstLanesSet {
        if let Some(vop) = op.as_virtual() {
            vop.dst_supported_lanes()
        } else {
            v9_op_dst_supported_lanes(op, self.arch)
        }
    }

    fn preload_reg(&self, preload: PreloadReg) -> Option<RegRef> {
        use PreloadReg::*;

        let idx = match preload {
            LocalId01 => 55,
            LocalId2 => 56,
            WorkgroupId0 => 57,
            WorkgroupId1 => 58,
            WorkgroupId2 => 59,
            GlobalId0 => 60,
            GlobalId1 => 61,
            GlobalId2 => 62,
            InternalId => 59,
            VertexId => 60,
            InstanceId => 61,
            DrawId => 62,
            ViewId => 63,
            PrimitiveId => 57,
            PrimitiveFlags => 58,
            PositionXY => 59,
            CumulativeCoverage => 60,
            RasterizerSampleCentroid => 61,
            FrameArgLow => 62,
            FrameArgHigh => 63,
        };

        Some(RegRef {
            idx,
            range: RegRange::Regs(1),
            preload: Some(preload),
        })
    }

    fn max_threads(&self, registers_used: u8) -> u8 {
        64 / registers_used.max(32)
    }
}

pub fn model_for_gpu_id(
    gpu_id: u64,
    gpu_variant: u32,
) -> Result<Box<dyn Model + Sync + Send>, &'static str> {
    // SAFETY: pan_arch() just translates one integer to another
    let arch = u8::try_from(unsafe { pan_arch(gpu_id) }).unwrap();
    let pan_model = unsafe {
        let model = pan_get_model(gpu_id, gpu_variant) as *mut _;
        std::ptr::NonNull::new(model).ok_or("Invalid GPU ID or variant")?
    };

    if arch >= 15 {
        Err("Kraid does not yet support this GPU")
    } else if arch >= 9 {
        Ok(Box::new(ValhallModel::new(arch, pan_model)))
    } else {
        Err("Kraid only supports Valhall (v9) and later GPUs")
    }
}
