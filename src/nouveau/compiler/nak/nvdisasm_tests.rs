// Copyright © 2025 Valve Corporation
// SPDX-License-Identifier: MIT

use crate::ir::*;
use crate::sm70::ShaderModel70;
use compiler::cfg::CFGBuilder;
use rustc_hash::FxBuildHasher;

use std::io::Write;
use std::process;
use std::process::Command;
use std::slice;
use std::sync::atomic::AtomicUsize;
use std::sync::OnceLock;

static FILE_NUM: AtomicUsize = AtomicUsize::new(0);

fn run_nvdisasm(s: &Shader) -> String {
    let code = s.sm.encode_shader(s);
    // println!("{code:x?}");

    let slice_u8: &[u8] = unsafe {
        slice::from_raw_parts(
            code.as_ptr() as *const u8,
            code.len() * size_of::<u32>(),
        )
    };

    let tmp_file = format!(
        "/tmp/nak_dis_{}_{}",
        process::id(),
        FILE_NUM.fetch_add(1, std::sync::atomic::Ordering::Relaxed)
    );
    std::fs::write(&tmp_file, slice_u8).expect("Failed to write file");

    let out = Command::new("nvdisasm")
        .arg("-b")
        .arg(format!("SM{}", s.sm.sm()))
        .arg("--print-raw")
        .arg(&tmp_file)
        .output()
        .expect("failed to execute process");

    std::io::stderr().write_all(&out.stderr).expect("IO error");
    assert!(out.status.success());
    let stdout = std::str::from_utf8(&out.stdout).unwrap();
    std::fs::remove_file(tmp_file).unwrap();
    stdout.into()
}

fn disassemble_instrs(instrs: Vec<Instr>, sm: u8) -> Vec<String> {
    let mut label_alloc = LabelAllocator::new();
    let block = BasicBlock {
        label: label_alloc.alloc(),
        uniform: true,
        instrs,
    };

    let mut cfg = CFGBuilder::<_, _, FxBuildHasher>::new();
    cfg.add_node(0, block);

    let f = Function {
        ssa_alloc: SSAValueAllocator::new(),
        phi_alloc: PhiAllocator::new(),
        blocks: cfg.as_cfg(),
    };

    let cs_info = ComputeShaderInfo {
        local_size: [32, 1, 1],
        smem_size: 0,
    };
    let info = ShaderInfo {
        max_warps_per_sm: 0,
        num_gprs: 0,
        num_control_barriers: 0,
        num_instrs: 0,
        num_static_cycles: 0,
        num_spills_to_mem: 0,
        num_fills_from_mem: 0,
        num_spills_to_reg: 0,
        num_fills_from_reg: 0,
        slm_size: 0,
        max_crs_depth: 0,
        uses_global_mem: true,
        writes_global_mem: true,
        uses_fp64: false,
        stage: ShaderStageInfo::Compute(cs_info),
        io: ShaderIoInfo::None,
    };

    let sm: Box<dyn ShaderModel> = Box::new(ShaderModel70::new(sm));
    let s = Shader {
        sm: &*sm,
        info: info,
        functions: vec![f],
    };
    let out = run_nvdisasm(&s);
    let out: Vec<String> = out
        .lines()
        .map(|line| {
            let mut line: String = line
                .trim_start_matches(|c| -> bool {
                    match c {
                        '/' | '*' => true,
                        'a'..='f' => true, // Actual instructions are uppercase
                        _ => c.is_numeric() || c.is_whitespace(),
                    }
                })
                .trim()
                .into();
            line.make_ascii_lowercase();
            line
        })
        .collect();

    out
}

struct DisasmCheck {
    instrs: Vec<Instr>,
    expected: Vec<String>,
}

impl DisasmCheck {
    fn new() -> Self {
        DisasmCheck {
            instrs: Vec::new(),
            expected: Vec::new(),
        }
    }

    fn push(&mut self, instr: impl Into<Instr>, expected: impl Into<String>) {
        self.instrs.push(instr.into());
        self.expected.push(expected.into());
    }

    fn check(mut self, sm: u8) {
        assert!(self.expected.len() > 0);
        let actual = disassemble_instrs(std::mem::take(&mut self.instrs), sm);
        assert_eq!(actual.len(), self.expected.len());

        let mut any_different = false;
        for (a, e) in actual
            .into_iter()
            .zip(std::mem::take(&mut self.expected).into_iter())
        {
            if a != e {
                if !any_different {
                    eprintln!("Error: Difference on SM{sm}");
                    any_different = true;
                }
                eprintln!("actual: {a}");
                eprintln!("expect: {e}\n");
            }
        }
        if any_different {
            panic!("Differences found");
        }
    }
}

static SM_LIST_CELL: OnceLock<&'static [u8]> = OnceLock::new();

fn sm_list() -> &'static [u8] {
    SM_LIST_CELL.get_or_init(|| {
        let out = Command::new("nvdisasm")
            .arg("--version")
            .output()
            .expect("failed to execute process");

        std::io::stderr().write_all(&out.stderr).expect("IO error");
        assert!(out.status.success());
        let stdout = std::str::from_utf8(&out.stdout).unwrap();

        if stdout.find("cuda_12").is_some() {
            &[70, 75, 80, 86, 89, 90, 100, 120]
        } else if stdout.find("cuda_13").is_some() {
            &[75, 80, 86, 89, 90, 100, 120]
        } else {
            panic!("Unknown nvdisasm version. stdout: {stdout}");
        }
    })
}

#[test]
pub fn test_nop() {
    for &sm in sm_list() {
        let mut c = DisasmCheck::new();
        c.push(OpNop { label: None }, "nop;");
        c.check(sm);
    }
}

#[test]
pub fn test_ldc() {
    let reg_files = [RegFile::GPR, RegFile::UGPR];

    let ur2_4 = RegRef::new(RegFile::UGPR, 2, 2);
    let cbufs = [
        (CBuf::Binding(5), "c[0x5]"),
        (CBuf::BindlessUGPR(ur2_4), "cx[ur2]"),
    ];

    let mem_types = [
        (MemType::U8, ".u8"),
        (MemType::I8, ".s8"),
        (MemType::U16, ".u16"),
        (MemType::I16, ".s16"),
        (MemType::B32, ""),
        (MemType::B64, ".64"),
        (MemType::B128, ".128"),
    ];

    for &sm in sm_list() {
        let mut c = DisasmCheck::new();
        for reg_file in reg_files {
            if reg_file == RegFile::UGPR && sm < 73 {
                continue;
            }

            let ldc_op_str = match reg_file {
                RegFile::GPR => "ldc",
                RegFile::UGPR => {
                    if sm >= 100 {
                        "ldcu"
                    } else {
                        "uldc"
                    }
                }
                _ => panic!("Unsupported register file"),
            };

            for (cbuf, cbuf_str) in &cbufs {
                if matches!(cbuf, CBuf::BindlessUGPR(_)) && sm < 73 {
                    continue;
                }

                for (mem_type, mem_type_str) in mem_types {
                    if mem_type == MemType::B128
                        && (reg_file == RegFile::GPR || sm < 100)
                    {
                        continue;
                    }

                    let dst_regs = mem_type.bits().div_ceil(32);
                    let r4 = RegRef::new(reg_file, 4, dst_regs as u8);
                    let r4_str = format!("{}4", reg_file.fmt_prefix());

                    let cb = CBufRef {
                        buf: cbuf.clone(),
                        offset: 0x248,
                    };
                    let mut instr = OpLdc {
                        dst: r4.into(),
                        cb: cb.into(),
                        offset: 0.into(),
                        mode: LdcMode::Indexed,
                        mem_type,
                    };

                    c.push(
                        instr.clone(),
                        format!(
                            "{ldc_op_str}{mem_type_str} {r4_str}, {cbuf_str}[0x248];"
                        ),
                    );

                    if reg_file == RegFile::GPR
                        || (sm >= 100 && matches!(cbuf, CBuf::Binding(_)))
                        || sm >= 120
                    {
                        let r6 = RegRef::new(reg_file, 6, 1);
                        instr.offset = r6.into();

                        c.push(
                            instr.clone(),
                            format!(
                                "{ldc_op_str}{mem_type_str} {r4_str}, {cbuf_str}[{r6}+0x248];"
                            ),
                        );
                    }
                }
            }
        }
        c.check(sm);
    }
}

#[test]
pub fn test_ld_st_atom() {
    let r0 = RegRef::new(RegFile::GPR, 0, 1);
    let r1 = RegRef::new(RegFile::GPR, 1, 1);
    let r2 = RegRef::new(RegFile::GPR, 2, 1);
    let r3 = RegRef::new(RegFile::GPR, 3, 1);

    let order = MemOrder::Strong(MemScope::CTA);

    let atom_types = [
        (AtomType::F16x2, ".f16x2.rn"),
        (AtomType::U32, ""),
        (AtomType::I32, ".s32"),
        (AtomType::F32, ".f32.ftz.rn"),
        (AtomType::U64, ".64"),
        (AtomType::I64, ".s64"),
        (AtomType::F64, ".f64.rn"),
    ];

    let spaces = [
        MemSpace::Global(MemAddrType::A64),
        MemSpace::Shared,
        MemSpace::Local,
    ];

    for &sm in sm_list() {
        let mut c = DisasmCheck::new();
        for space in spaces {
            for (addr_offset, addr_offset_str) in [(0x12, "0x12"), (-1, "-0x1")]
            {
                let cta = if sm >= 80 { "sm" } else { "cta" };

                let pri = match space {
                    MemSpace::Global(_) => MemEvictionPriority::First,
                    MemSpace::Shared => MemEvictionPriority::Normal,
                    MemSpace::Local => MemEvictionPriority::Normal,
                };
                let access = MemAccess {
                    mem_type: MemType::B32,
                    space,
                    order: order,
                    eviction_priority: pri,
                };

                let instr = OpLd {
                    dst: Dst::Reg(r0),
                    addr: SrcRef::Reg(r1).into(),
                    offset: addr_offset,
                    access: access.clone(),
                };
                let expected = match space {
                    MemSpace::Global(_) => {
                        format!(
                            "ldg.e.ef.strong.{cta} r0, [r1+{addr_offset_str}];"
                        )
                    }
                    MemSpace::Shared => {
                        format!("lds r0, [r1+{addr_offset_str}];")
                    }
                    MemSpace::Local => {
                        format!("ldl r0, [r1+{addr_offset_str}];")
                    }
                };
                c.push(instr, expected);

                let instr = OpSt {
                    addr: SrcRef::Reg(r1).into(),
                    data: SrcRef::Reg(r2).into(),
                    offset: addr_offset,
                    access: access.clone(),
                };
                let expected = match space {
                    MemSpace::Global(_) => {
                        format!(
                            "stg.e.ef.strong.{cta} [r1+{addr_offset_str}], r2;"
                        )
                    }
                    MemSpace::Shared => {
                        format!("sts [r1+{addr_offset_str}], r2;")
                    }
                    MemSpace::Local => {
                        format!("stl [r1+{addr_offset_str}], r2;")
                    }
                };
                c.push(instr, expected);

                for (atom_type, atom_type_str) in atom_types {
                    for use_dst in [true, false] {
                        let instr = OpAtom {
                            dst: if use_dst { Dst::Reg(r0) } else { Dst::None },
                            addr: SrcRef::Reg(r1).into(),
                            data: SrcRef::Reg(r2).into(),
                            atom_op: AtomOp::Add,
                            cmpr: SrcRef::Reg(r3).into(),
                            atom_type,

                            addr_offset,

                            mem_space: space,
                            mem_order: order,
                            mem_eviction_priority: pri,
                        };

                        let expected = match space {
                            MemSpace::Global(_) => {
                                let op = if use_dst {
                                    "atomg"
                                } else if sm >= 90 {
                                    "redg"
                                } else {
                                    "red"
                                };
                                let dst = if use_dst { "pt, r0, " } else { "" };
                                format!("{op}.e.add.ef{atom_type_str}.strong.{cta} {dst}[r1+{addr_offset_str}], r2;")
                            }
                            MemSpace::Shared => {
                                if atom_type.is_float() {
                                    continue;
                                }
                                if atom_type.bits() == 64 {
                                    continue;
                                }
                                let dst = if use_dst { "r0" } else { "rz" };
                                format!("atoms.add{atom_type_str} {dst}, [r1+{addr_offset_str}], r2;")
                            }
                            MemSpace::Local => continue,
                        };

                        c.push(instr, expected);
                    }
                }
            }
        }
        c.check(sm);
    }
}

#[test]
pub fn test_texture() {
    let r0 = RegRef::new(RegFile::GPR, 0, 1);
    let r1 = RegRef::new(RegFile::GPR, 1, 1);
    let r2 = RegRef::new(RegFile::GPR, 2, 1);
    let r3 = RegRef::new(RegFile::GPR, 3, 1);
    let p0 = RegRef::new(RegFile::Pred, 0, 1);

    let lod_modes = [
        TexLodMode::Auto,
        TexLodMode::Zero,
        TexLodMode::Lod,
        TexLodMode::Bias,
        TexLodMode::Clamp,
        TexLodMode::BiasClamp,
    ];

    let tld4_offset_modes = [
        TexOffsetMode::None,
        TexOffsetMode::AddOffI,
        TexOffsetMode::PerPx,
    ];

    let tex_queries = [
        TexQuery::Dimension,
        TexQuery::TextureType,
        TexQuery::SamplerPos,
    ];

    for &sm in sm_list() {
        let mut c = DisasmCheck::new();
        for lod_mode in lod_modes {
            if lod_mode == TexLodMode::BiasClamp && sm >= 100 {
                continue;
            }

            let instr = OpTex {
                dsts: [Dst::Reg(r0), Dst::Reg(r2)],
                fault: Dst::Reg(p0),

                tex: TexRef::Bindless,

                srcs: [SrcRef::Reg(r1).into(), SrcRef::Reg(r3).into()],

                dim: TexDim::_2D,
                lod_mode,
                deriv_mode: TexDerivMode::Auto,
                z_cmpr: false,
                offset_mode: TexOffsetMode::None,
                mem_eviction_priority: MemEvictionPriority::First,
                nodep: true,
                channel_mask: ChannelMask::for_comps(3),
            };
            c.push(
                instr,
                format!(
                    "tex.b{lod_mode}.ef.nodep p0, r2, r0, r1, r3, 2d, 0x7;"
                ),
            );

            if lod_mode.is_explicit_lod() {
                let instr = OpTld {
                    dsts: [Dst::Reg(r0), Dst::Reg(r2)],
                    fault: Dst::Reg(p0),

                    tex: TexRef::Bindless,

                    srcs: [SrcRef::Reg(r1).into(), SrcRef::Reg(r3).into()],

                    dim: TexDim::_2D,
                    is_ms: false,
                    lod_mode,
                    offset_mode: TexOffsetMode::None,
                    mem_eviction_priority: MemEvictionPriority::First,
                    nodep: true,
                    channel_mask: ChannelMask::for_comps(3),
                };
                c.push(
                    instr,
                    format!(
                        "tld.b{lod_mode}.ef.nodep p0, r2, r0, r1, r3, 2d, 0x7;"
                    ),
                );
            }
        }

        for offset_mode in tld4_offset_modes {
            let offset_mode_str = if offset_mode == TexOffsetMode::None {
                String::new()
            } else {
                format!("{offset_mode}")
            };

            let instr = OpTld4 {
                dsts: [Dst::Reg(r0), Dst::Reg(r2)],
                fault: Dst::Reg(p0),

                tex: TexRef::Bindless,

                srcs: [SrcRef::Reg(r1).into(), SrcRef::Reg(r3).into()],

                dim: TexDim::_2D,
                comp: 1,
                offset_mode,
                z_cmpr: false,
                mem_eviction_priority: MemEvictionPriority::First,
                nodep: true,
                channel_mask: ChannelMask::for_comps(3),
            };
            c.push(
                instr,
                format!("tld4.g.b{offset_mode_str}.ef.nodep p0, r2, r0, r1, r3, 2d, 0x7;"),
            );
        }

        let instr = OpTmml {
            dsts: [Dst::Reg(r0), Dst::Reg(r2)],

            tex: TexRef::Bindless,

            srcs: [SrcRef::Reg(r1).into(), SrcRef::Reg(r3).into()],

            dim: TexDim::_2D,
            deriv_mode: TexDerivMode::Auto,
            nodep: true,
            channel_mask: ChannelMask::for_comps(3),
        };
        c.push(instr, format!("tmml.b.lod.nodep r2, r0, r1, r3, 2d, 0x7;"));

        let instr = OpTxd {
            dsts: [Dst::Reg(r0), Dst::Reg(r2)],
            fault: Dst::Reg(p0),

            tex: TexRef::Bindless,

            srcs: [SrcRef::Reg(r1).into(), SrcRef::Reg(r3).into()],

            dim: TexDim::_2D,
            offset_mode: TexOffsetMode::None,
            mem_eviction_priority: MemEvictionPriority::First,
            nodep: true,
            channel_mask: ChannelMask::for_comps(3),
        };
        c.push(
            instr,
            format!("txd.b.ef.nodep p0, r2, r0, r1, r3, 2d, 0x7;"),
        );

        for tex_query in tex_queries {
            let instr = OpTxq {
                dsts: [Dst::Reg(r0), Dst::Reg(r2)],

                tex: TexRef::Bindless,

                src: SrcRef::Reg(r1).into(),

                query: tex_query,
                nodep: true,
                channel_mask: ChannelMask::for_comps(3),
            };
            c.push(
                instr,
                format!("txq.b.nodep r2, r0, r1, tex_header_{tex_query}, 0x7;"),
            );
        }

        c.check(sm);
    }
}

#[test]
pub fn test_lea() {
    let r0 = RegRef::new(RegFile::GPR, 0, 1);
    let r1 = RegRef::new(RegFile::GPR, 1, 1);
    let r2 = RegRef::new(RegFile::GPR, 2, 1);
    let r3 = RegRef::new(RegFile::GPR, 3, 1);
    let p0 = RegRef::new(RegFile::Pred, 0, 1);

    let src_mods = [
        (SrcMod::None, SrcMod::None),
        (SrcMod::INeg, SrcMod::None),
        (SrcMod::None, SrcMod::INeg),
    ];

    for &sm in sm_list() {
        let mut c = DisasmCheck::new();

        for (intermediate_mod, b_mod) in src_mods {
            for shift in 0..32 {
                let intermediate_mod_str = match intermediate_mod {
                    SrcMod::None => "",
                    SrcMod::INeg => "-",
                    _ => unreachable!(),
                };

                let mut instr = OpLea {
                    dst: Dst::Reg(r0),
                    overflow: Dst::Reg(p0),

                    a: SrcRef::Reg(r1).into(),
                    b: SrcRef::Reg(r2).into(),

                    a_high: 0.into(),

                    shift,
                    dst_high: false,
                    intermediate_mod,
                };
                instr.b.src_mod = b_mod;
                let disasm = format!(
                    "lea r0, p0, {0}r1, {1}, 0x{2:x};",
                    intermediate_mod_str, instr.b, shift
                );
                c.push(instr, disasm);

                let mut instr = OpLea {
                    dst: Dst::Reg(r0),
                    overflow: Dst::Reg(p0),

                    a: SrcRef::Reg(r1).into(),
                    b: SrcRef::Reg(r2).into(),

                    a_high: SrcRef::Reg(r3).into(),

                    shift,
                    dst_high: true,
                    intermediate_mod,
                };
                instr.b.src_mod = b_mod;
                let disasm = format!(
                    "lea.hi r0, p0, {0}r1, {1}, r3, 0x{2:x};",
                    intermediate_mod_str, instr.b, shift
                );
                c.push(instr, disasm);
            }
        }

        c.check(sm);
    }
}

#[test]
pub fn test_hfma2() {
    let r0 = RegRef::new(RegFile::GPR, 0, 1);
    let r1 = RegRef::new(RegFile::GPR, 1, 1);
    let r2 = RegRef::new(RegFile::GPR, 2, 1);
    let r3 = RegRef::new(RegFile::GPR, 3, 1);

    let src_mods = [SrcMod::None, SrcMod::FAbs, SrcMod::FNeg, SrcMod::FNegAbs];

    for &sm in sm_list() {
        let mut c = DisasmCheck::new();

        for a_mod in src_mods {
            for b_mod in src_mods {
                for c_mod in src_mods {
                    let mut instr = OpHFma2 {
                        dst: Dst::Reg(r0),

                        srcs: [
                            SrcRef::Reg(r1).into(),
                            SrcRef::Reg(r2).into(),
                            SrcRef::Reg(r3).into(),
                        ],

                        saturate: false,
                        ftz: false,
                        dnz: false,
                        f32: false,
                    };
                    instr.srcs[0].src_mod = a_mod;
                    instr.srcs[1].src_mod = b_mod;
                    instr.srcs[2].src_mod = c_mod;
                    let disasm = format!(
                        "hfma2 r0, {}, {}, {};",
                        instr.srcs[0], instr.srcs[1], instr.srcs[2],
                    );
                    c.push(instr, disasm);
                }
            }
        }

        c.check(sm);
    }
}

#[test]
pub fn test_redux() {
    let ur0 = RegRef::new(RegFile::UGPR, 0, 1);
    let r1 = RegRef::new(RegFile::GPR, 1, 1);

    for &sm in sm_list() {
        if sm < 80 {
            continue;
        }

        let mut c = DisasmCheck::new();
        for (op, op_str) in [
            (ReduxOp::And, ""),
            (ReduxOp::Or, ".or"),
            (ReduxOp::Xor, ".xor"),
            (ReduxOp::Sum, ".sum"),
            (ReduxOp::Min(IntCmpType::U32), ".min"),
            (ReduxOp::Max(IntCmpType::U32), ".max"),
            (ReduxOp::Min(IntCmpType::I32), ".min.s32"),
            (ReduxOp::Max(IntCmpType::I32), ".max.s32"),
        ] {
            let instr = OpRedux {
                dst: Dst::Reg(ur0),
                src: SrcRef::Reg(r1).into(),
                op,
            };
            let disasm = format!("redux{op_str} ur0, r1;");
            c.push(instr, disasm);
        }
        c.check(sm);
    }
}

#[test]
pub fn test_match() {
    let r3 = RegRef::new(RegFile::GPR, 3, 1);
    let p1 = RegRef::new(RegFile::Pred, 1, 1);

    for &sm in sm_list() {
        let mut c = DisasmCheck::new();

        for (op, pred, pred_str) in [
            (MatchOp::All, Dst::Reg(p1), "p1, "),
            (MatchOp::Any, Dst::None, ""),
        ] {
            for (src_comps, u64_str) in [(1, ""), (2, ".u64")] {
                let src = RegRef::new(RegFile::GPR, 4, src_comps);
                let instr = OpMatch {
                    pred: pred.clone(),
                    mask: Dst::Reg(r3),

                    src: SrcRef::Reg(src).into(),
                    op,
                    u64: src_comps == 2,
                };
                let disasm = format!("match{op}{u64_str} {pred_str}r3, r4;");
                c.push(instr, disasm);
            }
        }

        c.check(sm);
    }
}

#[test]
pub fn test_sgxt() {
    let r0 = RegRef::new(RegFile::GPR, 0, 1);
    let r1 = RegRef::new(RegFile::GPR, 1, 1);
    let r2 = RegRef::new(RegFile::GPR, 2, 1);

    for &sm in sm_list() {
        let mut c = DisasmCheck::new();
        for signed in [false, true] {
            let instr = OpSgxt {
                dst: Dst::Reg(r0),
                a: SrcRef::Reg(r1).into(),
                bits: SrcRef::Reg(r2).into(),
                signed,
            };

            let disasm = if signed {
                "sgxt r0, r1, r2;"
            } else {
                "sgxt.u32 r0, r1, r2;"
            };
            c.push(instr, disasm);
        }
        c.check(sm);
    }
}

#[test]
pub fn test_plop3() {
    let p0 = RegRef::new(RegFile::Pred, 0, 1);
    let p1 = RegRef::new(RegFile::Pred, 1, 1);
    let p2 = RegRef::new(RegFile::Pred, 2, 1);
    let p3 = RegRef::new(RegFile::Pred, 3, 1);
    let p4 = RegRef::new(RegFile::Pred, 4, 1);

    let src_mods = [SrcMod::None, SrcMod::BNot];

    for &sm in sm_list() {
        let mut c = DisasmCheck::new();
        for a_mod in src_mods {
            for b_mod in src_mods {
                for c_mod in src_mods {
                    for lut_bit in 0..16 {
                        let lut = 1 << lut_bit;
                        let lut0 = (lut >> 0) as u8;
                        let lut1 = (lut >> 8) as u8;

                        let mut instr = OpPLop3 {
                            dsts: [p0.into(), p1.into()],
                            ops: [
                                LogicOp3 { lut: lut0 },
                                LogicOp3 { lut: lut1 },
                            ],
                            srcs: [p2.into(), p3.into(), p4.into()],
                        };
                        instr.srcs[0].src_mod = a_mod;
                        instr.srcs[1].src_mod = b_mod;
                        instr.srcs[2].src_mod = c_mod;

                        let disasm = format!(
                            "plop3.lut p0, p1, {}, {}, {}, {:#x}, {:#x};",
                            instr.srcs[0],
                            instr.srcs[1],
                            instr.srcs[2],
                            lut0,
                            lut1,
                        );
                        c.push(instr, disasm);
                    }
                }
            }
        }
        c.check(sm);
    }
}
