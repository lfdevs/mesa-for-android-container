// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::encode_v9::*;
use crate::ir::*;
use kraid_bindings::*;

pub trait Model {
    fn arch(&self) -> u8;

    fn encode_shader(&self, s: &Shader<'_>) -> Vec<u32>;

    fn op_is_supported(&self, op: &Op) -> bool;

    fn op_is_message(&self, op: &Op) -> bool;

    fn op_src_is_staging_reg(&self, op: &Op, src: &Src) -> bool;

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

    fn small_constants(&self) -> &[SmallConstant];

    fn special_fau(&self, preload: SpecialFAU) -> Option<FAURef>;

    fn preload_reg(&self, preload: PreloadReg) -> Option<RegRef>;

    fn subgroup_size(&self) -> u8 {
        unsafe { pan_subgroup_size(self.arch().into()).try_into().unwrap() }
    }
}

struct ValhallModel {
    arch: u8,
    sc_table: Vec<SmallConstant>,
}

impl ValhallModel {
    fn new(arch: u8) -> ValhallModel {
        use crate::isa::{SmallConstantTable, v9};
        let sc_table = v9::SmallConstantT::collect(arch);
        ValhallModel { arch, sc_table }
    }
}

impl Model for ValhallModel {
    fn arch(&self) -> u8 {
        self.arch
    }

    fn encode_shader(&self, s: &Shader<'_>) -> Vec<u32> {
        encode_v9(s, self.arch)
    }

    fn op_is_supported(&self, op: &Op) -> bool {
        op.as_virtual().is_some() || v9_op_is_supported(op, self.arch)
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

    fn small_constants(&self) -> &[SmallConstant] {
        &self.sc_table
    }

    fn special_fau(&self, special: SpecialFAU) -> Option<FAURef> {
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
                if self.arch < 12 {
                    return None;
                }
                (3, 0b0100)
            }
            PrepassState => {
                if self.arch < 13 {
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
}

pub fn model_for_gpu_id(
    gpu_id: u64,
) -> Result<Box<dyn Model + Sync + Send>, &'static str> {
    // SAFETY: pan_arch() just translates one integer to another
    let arch = u8::try_from(unsafe { pan_arch(gpu_id) }).unwrap();

    if arch >= 15 {
        Err("Kraid does not yet support this GPU")
    } else if arch >= 9 {
        Ok(Box::new(ValhallModel::new(arch)))
    } else {
        Err("Kraid only supports Valhall (v9) and later GPUs")
    }
}
