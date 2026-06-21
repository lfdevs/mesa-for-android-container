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

    fn op_src_supports_imm32(&self, op: &Op, src: &Src) -> bool;

    fn op_src_supports_swizzle(
        &self,
        op: &Op,
        src: &Src,
        swizzle: Swizzle,
    ) -> bool;

    fn op_dst_is_staging_reg(&self, op: &Op) -> bool;

    fn op_dst_supports_lanes(&self, op: &Op, lanes: DstLanes) -> bool;

    fn small_constants(&self) -> &[SmallConstant];
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

    fn op_src_supports_imm32(&self, op: &Op, src: &Src) -> bool {
        if let Some(vop) = op.as_virtual() {
            vop.src_supports_imm32(src)
        } else {
            v9_op_src_supports_imm32(op, src, self.arch)
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

    fn op_dst_is_staging_reg(&self, op: &Op) -> bool {
        if let Some(vop) = op.as_virtual() {
            vop.dst_is_staging_reg()
        } else {
            v9_op_dst_is_staging_reg(op, self.arch)
        }
    }

    fn op_dst_supports_lanes(&self, op: &Op, lanes: DstLanes) -> bool {
        if let Some(vop) = op.as_virtual() {
            vop.dst_supports_lanes(lanes)
        } else {
            v9_op_dst_supports_lanes(op, self.arch, lanes)
        }
    }

    fn small_constants(&self) -> &[SmallConstant] {
        &self.sc_table
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
