// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::ir::*;
use crate::isa::ExecUnit;
use crate::ops::*;

use compiler::bitset::BitSet;
use kraid_bindings::*;

fn get_va_stats(s: &Shader) -> valhall_stats {
    let mut regs: BitSet<usize> = BitSet::new();
    let mut mark_reg = |reg: &RegRef| {
        let idx = usize::from(reg.idx);
        let n = match reg.range {
            RegRange::Regs(n) => n,
            _ => 1,
        };
        regs.set_range(idx..(idx + usize::from(n)));
    };

    let mut fau_used = 0_u16;
    let [mut instrs, mut loops] = [0_usize; 2];
    let [mut fma, mut cvt, mut sfu, mut v, mut t, mut ls] = [0.0_f32; 6];
    let [mut spills, mut fills, mut spill_cost] = [0_u32; 3];

    for (i, block) in s.blocks.iter().enumerate() {
        if s.blocks.is_loop_header(i) {
            loops += 1;
        }
        instrs += block.instrs.len();

        // The cost of a spill/fill is 10*depth for now.  This matches the old
        // Bifrost compiler
        let per_spill_cost =
            u32::try_from(10 * (s.blocks.loop_depth(i) + 1)).unwrap();

        for instr in &block.instrs {
            for src in instr.srcs() {
                match &src.src_ref {
                    SrcRef::FAU(fau) if fau.page == FAUPage::User => {
                        fau_used = fau_used.max(fau.idx / 2 + 1);
                    }
                    SrcRef::Reg(reg) => mark_reg(reg),
                    _ => (),
                }
            }

            let mut dst_bytes = 0;
            for dst in instr.dsts() {
                dst_bytes += dst.dst_ref.bytes_written();
                if let DstRef::Reg(reg) = &dst.dst_ref {
                    mark_reg(reg);
                }
            }

            match s.model.op_exec_unit(&instr.op).unwrap() {
                ExecUnit::Cvt => cvt += f32::from(dst_bytes.div_ceil(4)),
                ExecUnit::Fma => fma += f32::from(dst_bytes.div_ceil(4)),
                ExecUnit::Sfu => sfu += f32::from(dst_bytes.div_ceil(4)),
                ExecUnit::Msg => match &instr.op {
                    Op::ACmpXchg(_)
                    | Op::Atom(_)
                    | Op::Atom1(_)
                    | Op::LeaBuf(_)
                    | Op::LeaPka(_)
                    | Op::LeaTex(_)
                    | Op::LdAttr(_)
                    | Op::LdCvt(_)
                    | Op::LdGClk(_)
                    | Op::LdPka(_)
                    | Op::LdTex(_)
                    | Op::Load(_)
                    | Op::StCvt(_)
                    | Op::Store(_) => {
                        ls += 1.0;
                    }
                    Op::TexFetch(_)
                    | Op::TexGather(_)
                    | Op::TexGradient(_)
                    | Op::TexSingle(_) => {
                        t += 1.0;
                    }
                    Op::Barrier(_) => {
                        // These aren't counted
                    }
                    _ => panic!("Unknown message instruction"),
                },
            }

            match &instr.op {
                Op::Load(op) if op.is_tls => {
                    fills += 1;
                    spill_cost = spill_cost.saturating_add(per_spill_cost);
                }
                Op::Store(op) if op.is_tls => {
                    spills += 1;
                    spill_cost = spill_cost.saturating_add(per_spill_cost);
                }
                _ => (),
            }
        }
    }

    // v14 doesn't have rates anymore
    if s.model.arch() < 14 {
        let rates = &s.model.pan_model().rates;
        assert_ne!(rates.cvt, 0);
        assert_ne!(rates.fma, 0);
        assert_ne!(rates.sfu, 0);
        assert_ne!(rates.varying, 0);
        assert_ne!(rates.texel, 0);
        cvt /= rates.cvt as f32;
        fma /= rates.fma as f32;
        sfu /= rates.sfu as f32;
        v /= rates.varying as f32;
        t /= rates.texel as f32;
    }
    let alu =
        unsafe { pan_va_compute_alu_bound(s.model.arch(), fma, cvt, sfu) };

    valhall_stats {
        instrs: instrs.try_into().unwrap(),
        cycles: alu.max(v).max(t).max(ls),
        fma,
        cvt,
        sfu,
        alu,
        v,
        t,
        ls,
        code_size: (instrs * 8).try_into().unwrap(),
        threads: s.model.max_threads(s.info.registers_used).into(),
        loops: loops.try_into().unwrap(),
        spills,
        fills,
        spill_cost,
        registers_used: regs.len().try_into().unwrap(),
        uniforms_used: fau_used.into(),
    }
}

impl Shader<'_> {
    pub fn get_stats(&self) -> pan_stats {
        if self.model.arch() >= 9 {
            pan_stats {
                isa: PAN_STAT_VALHALL,
                __bindgen_anon_1: pan_stats__bindgen_ty_1 {
                    valhall: get_va_stats(self),
                },
            }
        } else {
            panic!("Kraid only supports Valhall (v9) and later GPUs");
        }
    }
}
