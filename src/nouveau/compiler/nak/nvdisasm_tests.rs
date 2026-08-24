// Copyright © 2025 Valve Corporation
// SPDX-License-Identifier: MIT

use crate::ir::*;
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
    let num_instrs = instrs.len();
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
        blocks: cfg.as_cfg(true),
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

    let sm = ShaderModelInfo::new(sm, 0);
    let s = Shader {
        sm: &sm,
        info: info,
        functions: vec![f],
    };
    let out = run_nvdisasm(&s);
    let mut out: Vec<String> = out
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

            // Remove possible scheduling infos present as seen on CUDA 12.8.xx
            if let Some(pos) = line.find('?') {
                line = line[0..pos].trim().into();
                line.push_str(" ;");
            }
            line
        })
        .collect();

    // Drop padding instructions from the output
    if out.len() > 0 {
        out.drain(num_instrs..out.len());
    }

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

        if stdout.contains("cuda_12") {
            &[50, 52, 53, 60, 61, 62, 70, 75, 80, 86, 89, 90, 100, 120]
        } else if stdout.contains("cuda_13") {
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
    let r4_64 = RegRef::new(RegFile::GPR, 4, 2);
    let r2 = RegRef::new(RegFile::GPR, 2, 1);
    let r3 = RegRef::new(RegFile::GPR, 3, 1);
    let p4 = RegRef::new(RegFile::Pred, 4, 1);
    let ur2_64 = RegRef::new(RegFile::UGPR, 2, 2);

    let order = MemOrder::Strong(MemScope::CTA);

    let atom_types = [
        (AtomType::F16v2, ".f16x2.rn"),
        (AtomType::U32, ""),
        (AtomType::I32, ".s32"),
        (AtomType::F32, ".f32.ftz.rn"),
        (AtomType::U64, ".64"),
        (AtomType::I64, ".s64"),
        (AtomType::F64, ".f64.rn"),
    ];

    let atom_ops = [
        AtomOp::Add,
        AtomOp::Min,
        AtomOp::Max,
        AtomOp::Inc,
        AtomOp::Dec,
        AtomOp::And,
        AtomOp::Or,
        AtomOp::Xor,
        AtomOp::Exch,
    ];

    let spaces = [
        MemSpace::Global(MemAddrType::A64),
        MemSpace::Shared,
        MemSpace::Local,
    ];

    for &sm in sm_list() {
        let mut c = DisasmCheck::new();
        for space in spaces {
            for (addr_offset, addr_offset_str) in
                [(0x12, "0x12"), (-1, "-0x1"), (0x4, "0x4")]
            {
                if addr_offset % 4 != 0 && sm < 70 {
                    continue;
                }

                for addr_stride in [OffsetStride::X1, OffsetStride::X8] {
                    let cta = if sm >= 80 { "sm" } else { "cta" };
                    let r4_64_str =
                        if sm >= 73 && matches!(space, MemSpace::Global(_)) {
                            "r4.64"
                        } else {
                            "r4"
                        };
                    let urz = if sm >= 73 {
                        SrcRef::Reg(ur2_64).into()
                    } else {
                        Src::ZERO
                    };
                    let uniform_addr = if sm >= 73 { "+ur2" } else { "" };

                    let pri = match space {
                        MemSpace::Global(_) => MemEvictionPriority::First,
                        MemSpace::Shared => MemEvictionPriority::Normal,
                        MemSpace::Local => MemEvictionPriority::Normal,
                    };
                    if (space != MemSpace::Shared || sm < 75)
                        && addr_stride != OffsetStride::X1
                    {
                        continue;
                    }
                    let access = MemAccess {
                        mem_type: MemType::B32,
                        space,
                        order: order,
                        eviction_priority: pri,
                    };

                    let instr = OpLd {
                        dst: Dst::Reg(r0),
                        addr: SrcRef::Reg(r4_64).into(),
                        uniform_addr: urz.clone(),
                        pred: if matches!(space, MemSpace::Global(_))
                            && sm >= 73
                        {
                            SrcRef::Reg(p4).into()
                        } else {
                            true.into()
                        },
                        offset: addr_offset,
                        access: access.clone(),
                        stride: addr_stride,
                    };
                    let expected = match space {
                        MemSpace::Global(_) if sm >= 73 => {
                            format!(
                                "ldg.e.ef.strong.{cta} r0, [{r4_64_str}{uniform_addr}+{addr_offset_str}], p4;"
                            )
                        }
                        MemSpace::Global(_) if sm >= 70 => {
                            format!(
                                "ldg.e.ef.strong.{cta} r0, [{r4_64_str}+{addr_offset_str}];"
                            )
                        }
                        MemSpace::Global(_) => {
                            format!(
                                "ldg.e r0, [{r4_64_str}+{addr_offset_str}];"
                            )
                        }
                        MemSpace::Shared => {
                            format!(
                                "lds r0, [{r4_64_str}{addr_stride}{uniform_addr}+{addr_offset_str}];"
                            )
                        }
                        MemSpace::Local => {
                            format!(
                                "ldl r0, [{r4_64_str}{uniform_addr}+{addr_offset_str}];"
                            )
                        }
                    };
                    c.push(instr, expected);

                    let instr = OpSt {
                        addr: SrcRef::Reg(r4_64).into(),
                        uniform_addr: urz.clone(),
                        data: SrcRef::Reg(r2).into(),
                        offset: addr_offset,
                        access: access.clone(),
                        stride: addr_stride,
                    };
                    let expected = match space {
                        MemSpace::Global(_) if sm >= 70 => {
                            format!(
                                "stg.e.ef.strong.{cta} [{r4_64_str}{uniform_addr}+{addr_offset_str}], r2;"
                            )
                        }
                        MemSpace::Global(_) => {
                            format!(
                                "stg.e [{r4_64_str}{uniform_addr}+{addr_offset_str}], r2;"
                            )
                        }
                        MemSpace::Shared => {
                            format!(
                                "sts [{r4_64_str}{addr_stride}{uniform_addr}+{addr_offset_str}], r2;"
                            )
                        }
                        MemSpace::Local => {
                            format!(
                                "stl [{r4_64_str}{uniform_addr}+{addr_offset_str}], r2;"
                            )
                        }
                    };
                    c.push(instr, expected);

                    for (atom_type, mut atom_type_str) in atom_types {
                        if sm < 70 {
                            if matches!(
                                atom_type,
                                AtomType::F16v2 | AtomType::F64
                            ) {
                                continue;
                            }

                            if matches!(atom_type, AtomType::U64) {
                                atom_type_str = ".u64";
                            }
                        }

                        let active_atom_ops = if atom_type.is_float() {
                            &atom_ops[0..3]
                        } else {
                            &atom_ops[..]
                        };

                        for atom_op in active_atom_ops {
                            for use_dst in [true, false] {
                                if !use_dst && *atom_op == AtomOp::Exch {
                                    continue;
                                }

                                let instr = OpAtom {
                                    dst: if use_dst {
                                        Dst::Reg(r0)
                                    } else {
                                        Dst::None
                                    },
                                    addr: SrcRef::Reg(r4_64).into(),
                                    uniform_address: urz.clone(),
                                    data: SrcRef::Reg(r2).into(),
                                    atom_op: *atom_op,
                                    cmpr: SrcRef::Reg(r3).into(),
                                    atom_type,

                                    addr_offset,
                                    addr_stride: addr_stride,

                                    mem_space: space,
                                    mem_order: order,
                                    mem_eviction_priority: pri,
                                };

                                let expected = match space {
                                    MemSpace::Global(_) => {
                                        let op = if use_dst && sm >= 70 {
                                            "atomg"
                                        } else if use_dst {
                                            "atom"
                                        } else if sm >= 90 {
                                            "redg"
                                        } else {
                                            "red"
                                        };
                                        let dst = if use_dst && sm >= 70 {
                                            "pt, r0, "
                                        } else if use_dst {
                                            "r0, "
                                        } else {
                                            ""
                                        };

                                        if sm >= 70 {
                                            format!("{op}.e{atom_op}.ef{atom_type_str}.strong.{cta} {dst}[{r4_64_str}{uniform_addr}+{addr_offset_str}], r2;")
                                        } else {
                                            format!("{op}.e{atom_op}{atom_type_str} {dst}[{r4_64_str}{uniform_addr}+{addr_offset_str}], r2;")
                                        }
                                    }
                                    MemSpace::Shared => {
                                        if atom_type.is_float() {
                                            continue;
                                        }
                                        if atom_type.bits() == 64 {
                                            continue;
                                        }
                                        let dst =
                                            if use_dst { "r0" } else { "rz" };
                                        format!("atoms{atom_op}{atom_type_str} {dst}, [{r4_64_str}{addr_stride}{uniform_addr}+{addr_offset_str}], r2;")
                                    }
                                    MemSpace::Local => continue,
                                };

                                c.push(instr, expected);
                            }
                        }
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
        // TODO: For pre-Volta we need to test without dst1 and adapt the text disassembly
        if sm < 70 {
            continue;
        }

        let mut c = DisasmCheck::new();
        for scalar in [false, true] {
            let scr = if scalar { ".scr" } else { "" };
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
                    scalar: scalar,
                };
                c.push(
                    instr,
                    format!(
                        "tex{scr}.b{lod_mode}.ef.nodep p0, r2, r0, r1, r3, 2d, 0x7;"
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
                        scalar: scalar,
                    };
                    c.push(
                        instr,
                        format!(
                            "tld{scr}.b{lod_mode}.ef.nodep p0, r2, r0, r1, r3, 2d, 0x7;"
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
                    scalar: scalar,
                };
                c.push(
                    instr,
                    format!("tld4{scr}.g.b{offset_mode_str}.ef.nodep p0, r2, r0, r1, r3, 2d, 0x7;"),
                );
            }
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
        c.push(
            instr,
            "tmml.b.lod.nodep r2, r0, r1, r3, 2d, 0x7;".to_string(),
        );

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
            "txd.b.ef.nodep p0, r2, r0, r1, r3, 2d, 0x7;".to_string(),
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
        // TODO: Maxwell and Pascal have LEA, support it
        if sm < 70 {
            continue;
        }

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
        // TODO: SM53+ have HFMA2, support it
        if sm < 70 {
            continue;
        }

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
        if sm < 70 {
            continue;
        }

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
        if sm < 70 {
            continue;
        }

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
        if sm < 70 {
            continue;
        }

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

#[test]
pub fn test_isberd() {
    let r1 = RegRef::new(RegFile::GPR, 1, 1);
    let r2 = RegRef::new(RegFile::GPR, 2, 1);

    let mem_types = [
        (MemType::U8, ""),
        (MemType::U16, ".u16"),
        (MemType::B32, ".32"),
    ];

    let output_type = [(false, ""), (true, ".o")];
    let skew_type = [(false, ""), (true, ".skew")];
    let access_type_list = [
        (IsbeAccessType::Map, ""),
        (IsbeAccessType::Patch, ".patch"),
        (IsbeAccessType::Primitive, ".prim"),
        (IsbeAccessType::Attribute, ".attr"),
    ];

    for &sm in sm_list() {
        if sm < 50 {
            continue;
        }

        let mut c = DisasmCheck::new();
        for (output, output_str) in output_type {
            if output && sm < 70 {
                continue;
            }

            for (access_type, access_type_str) in access_type_list {
                if access_type != IsbeAccessType::Map && sm < 75 {
                    continue;
                }

                for (skew, skew_str) in skew_type {
                    if skew && sm < 75 {
                        continue;
                    }

                    for (mem_type, mem_type_str) in mem_types {
                        if mem_type != MemType::U8 && sm < 75 {
                            continue;
                        }

                        for imm_offset in [0, 0x42] {
                            if imm_offset != 0 && sm < 86 {
                                continue;
                            }

                            let instr = OpIsberd {
                                dst: Dst::Reg(r1),
                                offset: SrcRef::Reg(r2).into(),
                                imm_offset,
                                mem_type,
                                access_type,
                                output,
                                skew,
                            };
                            let disasm = if imm_offset != 0 {
                                format!("isberd{output_str}{access_type_str}{skew_str}{mem_type_str} r1, [r2+0x{imm_offset:x}];")
                            } else {
                                format!("isberd{output_str}{access_type_str}{skew_str}{mem_type_str} r1, [r2];")
                            };
                            c.push(instr, disasm);
                        }
                    }
                }
            }
        }

        c.check(sm);
    }
}

#[test]
pub fn test_isbewr() {
    let r1 = RegRef::new(RegFile::GPR, 1, 1);
    let r2 = RegRef::new(RegFile::GPR, 2, 1);

    let mem_types = [
        (MemType::U8, ""),
        (MemType::U16, ".u16"),
        (MemType::B32, ".32"),
    ];

    let skew_type = [(false, ""), (true, ".skew")];
    let access_type_list = [
        (IsbeAccessType::Map, ""),
        (IsbeAccessType::Attribute, ".attr"),
    ];

    for &sm in sm_list() {
        if sm < 75 {
            continue;
        }

        let mut c = DisasmCheck::new();
        for (access_type, access_type_str) in access_type_list {
            for (skew, skew_str) in skew_type {
                for (mem_type, mem_type_str) in mem_types {
                    for imm_offset in [0, 0x42] {
                        if imm_offset != 0 && sm < 86 {
                            continue;
                        }

                        let instr = OpIsbewr {
                            offset: SrcRef::Reg(r2).into(),
                            data: SrcRef::Reg(r1).into(),
                            imm_offset,
                            mem_type,
                            access_type,
                            output: true,
                            skew,
                        };
                        let disasm = if imm_offset != 0 {
                            format!("isbewr.o{access_type_str}{skew_str}{mem_type_str} [r2+0x{imm_offset:x}], r1;")
                        } else {
                            format!("isbewr.o{access_type_str}{skew_str}{mem_type_str} [r2], r1;")
                        };
                        c.push(instr, disasm);
                    }
                }
            }
        }

        c.check(sm);
    }
}

#[test]
pub fn test_mufu() {
    let r2 = RegRef::new(RegFile::GPR, 2, 1);
    let r3 = RegRef::new(RegFile::GPR, 3, 1);

    use MuFuOp::*;
    let ops = [Cos, Sin, Exp2, Log2, Rcp, Rsq, Rcp64H, Rsq64H, Sqrt, Tanh];
    let op_types = [(FloatType::F32, ""), (FloatType::F16, ".f16")];

    for &sm in sm_list() {
        let mut c = DisasmCheck::new();

        for op in ops {
            for (op_type, op_type_str) in op_types {
                match (op, op_type) {
                    (Rcp64H | Rsq64H, FloatType::F16) => continue,
                    (Tanh, _) if sm < 75 => continue,
                    (Sqrt, _) if sm < 52 => continue,
                    (_, FloatType::F16) if sm < 75 => continue,
                    _ => (),
                }
                let instr = OpMuFu {
                    dst: Dst::Reg(r2),
                    src: SrcRef::Reg(r3).into(),
                    op,
                    op_type,
                };
                let op_str = match op {
                    Exp2 => ".ex2".into(),
                    Log2 => ".lg2".into(),
                    _ => format!(".{op}"),
                };
                let disasm = format!("mufu{op_str}{op_type_str} r2, r3;");
                c.push(instr, disasm);
            }
        }

        c.check(sm);
    }
}

#[test]
pub fn test_nanosleep() {
    let r3 = RegRef::new(RegFile::GPR, 3, 1);
    let ur2_4 = RegRef::new(RegFile::UGPR, 2, 2);

    for &sm in sm_list() {
        if sm < 70 {
            continue;
        }

        let mut c = DisasmCheck::new();

        let mut srcs = vec![
            (0.into(), "rz"),
            (0x87654321.into(), "0x87654321"),
            (SrcRef::Reg(r3).into(), "r3"),
        ];

        if sm < 100 {
            srcs.push((
                CBufRef {
                    buf: CBuf::Binding(5),
                    offset: 0x100,
                }
                .into(),
                "c[0x5][0x100]",
            ));
            if sm >= 75 {
                srcs.push((
                    CBufRef {
                        buf: CBuf::BindlessUGPR(ur2_4),
                        offset: 0x100,
                    }
                    .into(),
                    "cx[ur2][0x100]",
                ));
            }
        }

        for (src, src_str) in srcs {
            let mut instr: Instr = OpNanosleep { time: src }.into();

            // Delay can't be the default value otherwise nvdisasm is unhappy
            instr.deps.delay = 1;

            let disasm = format!("nanosleep {src_str} ;");
            c.push(instr, disasm);
        }

        c.check(sm);
    }
}

#[test]
pub fn test_uldc_global() {
    let ur2_4 = RegRef::new(RegFile::UGPR, 2, 2);
    let ur4_6 = RegRef::new(RegFile::UGPR, 4, 2);
    let up1 = RegRef::new(RegFile::UPred, 1, 1);

    for &sm in sm_list() {
        if sm < 75 {
            continue;
        }

        let mut c = DisasmCheck::new();

        let mut mem_types = vec![
            (MemType::U8, ".u8"),
            (MemType::I8, ".s8"),
            (MemType::U16, ".u16"),
            (MemType::I16, ".s16"),
            (MemType::B32, ""),
            (MemType::B64, ".64"),
        ];

        let uldc_str = if sm < 100 { "uldc" } else { "ldcu" };
        if sm >= 100 {
            mem_types.push((MemType::B128, ".128"));
        }

        for (mt, mt_str) in mem_types {
            let instr = OpLdcg {
                dst: ur2_4.into(),
                addr: ur4_6.into(),
                mem_type: mt,
                pred: up1.into(),
                offset: 0x100,
            };
            let disasm = format!("{uldc_str}{mt_str} ur2, [ur4+0x100], up1;");
            c.push(instr, disasm);
        }

        c.check(sm);
    }
}

#[test]
pub fn test_f2fp() {
    let r1 = RegRef::new(RegFile::GPR, 1, 1);
    let r2 = RegRef::new(RegFile::GPR, 2, 1);
    let r3 = RegRef::new(RegFile::GPR, 3, 1);
    let ur1 = RegRef::new(RegFile::UGPR, 1, 1);
    let ur2 = RegRef::new(RegFile::UGPR, 2, 1);
    let ur3 = RegRef::new(RegFile::UGPR, 3, 1);

    let rnd_modes = [(FRndMode::NearestEven, ""), (FRndMode::Zero, ".rz")];

    for &sm in sm_list() {
        if sm < 80 {
            continue;
        }

        let mut c = DisasmCheck::new();

        let conv_type_str = if sm >= 89 { ".f16.f32" } else { "" };
        let conv_type_uniform_str = if sm >= 120 { ".f16.f32" } else { "" };
        for (rnd_mode, rnd_mode_str) in rnd_modes {
            let instr = OpF2FP {
                dst: r1.into(),
                srcs: [r2.into(), r3.into()],
                rnd_mode,
            };
            let disasm = format!(
                "f2fp{conv_type_str}.pack_ab{rnd_mode_str} r1, r2, r3;"
            );
            c.push(instr, disasm);

            if sm >= 86 {
                let instr = OpF2FP {
                    dst: ur1.into(),
                    srcs: [ur2.into(), ur3.into()],
                    rnd_mode,
                };
                let disasm = format!(
                    "uf2fp{conv_type_uniform_str}.pack_ab{rnd_mode_str} ur1, ur2, ur3;"
                );
                c.push(instr, disasm);
            }
        }

        c.check(sm);
    }
}

#[test]
pub fn test_iabs() {
    let r1 = RegRef::new(RegFile::GPR, 1, 1);
    let r2 = RegRef::new(RegFile::GPR, 2, 1);
    let ur1 = RegRef::new(RegFile::UGPR, 1, 1);
    let ur2 = RegRef::new(RegFile::UGPR, 2, 1);

    for &sm in sm_list() {
        if sm < 70 {
            continue;
        }

        let mut c = DisasmCheck::new();
        let instr = OpIAbs {
            dst: r1.into(),
            src: r2.into(),
        };
        let disasm = format!("iabs r1, r2;");
        c.push(instr, disasm);

        if sm >= 120 {
            let instr = OpIAbs {
                dst: ur1.into(),
                src: ur2.into(),
            };
            let disasm = format!("uiabs ur1, ur2;");
            c.push(instr, disasm);
        }

        c.check(sm);
    }
}

#[test]
pub fn test_imnmx() {
    let r1 = RegRef::new(RegFile::GPR, 1, 1);
    let r2 = RegRef::new(RegFile::GPR, 2, 1);
    let r3 = RegRef::new(RegFile::GPR, 3, 1);
    let p4 = RegRef::new(RegFile::Pred, 4, 1);
    let ur1 = RegRef::new(RegFile::UGPR, 1, 1);
    let ur2 = RegRef::new(RegFile::UGPR, 2, 1);
    let ur3 = RegRef::new(RegFile::UGPR, 3, 1);
    let up4 = RegRef::new(RegFile::UPred, 4, 1);

    for &sm in sm_list() {
        // IMNMX does not exist on Volta.
        if sm == 70 {
            continue;
        }

        let mut c = DisasmCheck::new();

        for (cmp_type, cmp_type_str) in
            [(IntCmpType::U32, ".u32"), (IntCmpType::I32, "")]
        {
            let instr = OpIMnMx {
                dst: r1.into(),
                cmp_type,
                srcs: [r2.into(), r3.into()],
                min: p4.into(),
            };
            let disasm = if sm >= 120 {
                format!("imnmx{cmp_type_str} pt, pt, r1, r2, r3, p4, !pt;")
            } else {
                format!("imnmx{cmp_type_str} r1, r2, r3, p4;")
            };
            c.push(instr, disasm);

            if sm >= 120 {
                let instr = OpIMnMx {
                    dst: ur1.into(),
                    cmp_type,
                    srcs: [ur2.into(), ur3.into()],
                    min: up4.into(),
                };
                let disasm = format!(
                    "uimnmx{cmp_type_str} upt, upt, ur1, ur2, ur3, up4, !upt;"
                );
                c.push(instr, disasm);
            }
        }

        c.check(sm);
    }
}

#[test]
pub fn test_float_ops() {
    let r1 = RegRef::new(RegFile::GPR, 1, 1);
    let r2 = RegRef::new(RegFile::GPR, 2, 1);
    let r3 = RegRef::new(RegFile::GPR, 3, 1);
    let r4 = RegRef::new(RegFile::GPR, 4, 1);
    let p1 = RegRef::new(RegFile::Pred, 1, 1);
    let p2 = RegRef::new(RegFile::Pred, 2, 1);
    let p4 = RegRef::new(RegFile::Pred, 4, 1);
    let ur1 = RegRef::new(RegFile::UGPR, 1, 1);
    let ur2 = RegRef::new(RegFile::UGPR, 2, 1);
    let ur3 = RegRef::new(RegFile::UGPR, 3, 1);
    let ur4 = RegRef::new(RegFile::UGPR, 4, 1);
    let up1 = RegRef::new(RegFile::UPred, 1, 1);
    let up2 = RegRef::new(RegFile::UPred, 2, 1);
    let up4 = RegRef::new(RegFile::UPred, 4, 1);

    let test_two_src_cases = [
        (Dst::Reg(r1), ([Src::from(r2), Src::from(r3)], "r2, r3")),
        (
            Dst::Reg(r1),
            ([Src::from(r2), Src::from(2.0f32.to_bits())], "r2, 2"),
        ),
    ];

    let test_uniform_two_src_cases = [
        (
            Dst::Reg(ur1),
            ([Src::from(ur2), Src::from(ur3)], "ur2, ur3"),
        ),
        (
            Dst::Reg(ur1),
            ([Src::from(ur2), Src::from(2.0f32.to_bits())], "ur2, 2"),
        ),
    ];

    let test_three_src_cases = [
        (
            Dst::Reg(r1),
            ([Src::from(r2), Src::from(r3), Src::from(r4)], "r2, r3, r4"),
        ),
        (
            Dst::Reg(r1),
            (
                [Src::from(r2), Src::from(2.0f32.to_bits()), Src::from(r4)],
                "r2, 2, r4",
            ),
        ),
    ];

    let test_uniform_three_src_cases = [
        (
            Dst::Reg(ur1),
            (
                [Src::from(ur2), Src::from(ur3), Src::from(ur4)],
                "ur2, ur3, ur4",
            ),
        ),
        (
            Dst::Reg(ur1),
            (
                [Src::from(ur2), Src::from(2.0f32.to_bits()), Src::from(ur4)],
                "ur2, 2, ur4",
            ),
        ),
    ];

    let pred_set_ops = [PredSetOp::And, PredSetOp::Or, PredSetOp::Xor];

    let float_cmp_ops = [
        FloatCmpOp::OrdEq,
        FloatCmpOp::OrdNe,
        FloatCmpOp::OrdLt,
        FloatCmpOp::OrdLe,
        FloatCmpOp::OrdGt,
        FloatCmpOp::OrdGe,
        FloatCmpOp::UnordEq,
        FloatCmpOp::UnordNe,
        FloatCmpOp::UnordLt,
        FloatCmpOp::UnordLe,
        FloatCmpOp::UnordGt,
        FloatCmpOp::UnordGe,
        FloatCmpOp::IsNum,
        FloatCmpOp::IsNan,
    ];

    for &sm in sm_list() {
        let mut c = DisasmCheck::new();

        for (ftz, ftz_str) in [(false, ""), (true, ".ftz")] {
            for (dst, (srcs, srcs_str)) in &test_two_src_cases {
                let instr = OpFMnMx {
                    dst: dst.clone(),
                    srcs: srcs.clone(),
                    min: p4.into(),
                    ftz,
                };
                let disasm = format!("fmnmx{ftz_str} {dst}, {srcs_str}, p4;");
                c.push(instr, disasm);

                for cmp_op in &float_cmp_ops {
                    let instr = OpFSet {
                        dst: dst.clone(),
                        cmp_op: *cmp_op,
                        srcs: srcs.clone(),
                        ftz,
                    };
                    let disasm = format!(
                        "fset.bf{cmp_op}{ftz_str}.and {dst}, {srcs_str}, pt;"
                    );
                    c.push(instr, disasm);

                    for set_op in &pred_set_ops {
                        let instr = OpFSetP {
                            dst: p2.into(),
                            set_op: *set_op,
                            cmp_op: *cmp_op,
                            srcs: srcs.clone(),
                            accum: p1.into(),
                            ftz,
                        };
                        let disasm = format!(
                            "fsetp{cmp_op}{ftz_str}{set_op} p2, pt, {srcs_str}, p1;"
                        );
                        c.push(instr, disasm);
                    }
                }
            }

            if sm >= 120 {
                for (dst, (srcs, srcs_str)) in &test_uniform_two_src_cases {
                    let instr = OpFMnMx {
                        dst: dst.clone(),
                        srcs: srcs.clone(),
                        min: up4.into(),
                        ftz,
                    };
                    let disasm =
                        format!("ufmnmx{ftz_str} {dst}, {srcs_str}, up4;");
                    c.push(instr, disasm);

                    for cmp_op in &float_cmp_ops {
                        let instr = OpFSet {
                            dst: dst.clone(),
                            cmp_op: *cmp_op,
                            srcs: srcs.clone(),
                            ftz,
                        };
                        let disasm = format!("ufset.bf{cmp_op}{ftz_str}.and {dst}, {srcs_str}, upt;");
                        c.push(instr, disasm);

                        for set_op in &pred_set_ops {
                            let instr = OpFSetP {
                                dst: up2.into(),
                                set_op: *set_op,
                                cmp_op: *cmp_op,
                                srcs: srcs.clone(),
                                accum: up1.into(),
                                ftz,
                            };
                            let disasm = format!(
                                "ufsetp{cmp_op}{ftz_str}{set_op} up2, upt, {srcs_str}, up1;"
                            );
                            c.push(instr, disasm);
                        }
                    }
                }
            }

            for (rnd_mode, rnd_mode_str, int_rnd_mode_str) in [
                (FRndMode::NearestEven, "", ""),
                (FRndMode::Zero, ".rz", ".trunc"),
            ] {
                let instr = OpF2F {
                    dst: r1.into(),
                    src: r2.into(),
                    src_type: FloatType::F64,
                    dst_type: FloatType::F32,
                    rnd_mode,
                    ftz,
                    integer_rnd: false,
                };
                let disasm =
                    format!("f2f{ftz_str}.f32.f64{rnd_mode_str} r1, r2;");
                c.push(instr, disasm);

                let instr = OpF2F {
                    dst: r1.into(),
                    src: r2.into(),
                    src_type: FloatType::F16,
                    dst_type: FloatType::F32,
                    rnd_mode,
                    ftz,
                    integer_rnd: false,
                };
                let disasm =
                    format!("f2f{ftz_str}.f32.f16{rnd_mode_str} r1, r2;");
                c.push(instr, disasm);

                let instr = OpF2I {
                    dst: r1.into(),
                    src: r2.into(),
                    src_type: FloatType::F64,
                    dst_type: IntType::U32,
                    rnd_mode,
                    ftz,
                };
                let disasm =
                    format!("f2i{ftz_str}.u32.f64{int_rnd_mode_str} r1, r2;");
                c.push(instr, disasm);

                let instr = OpF2I {
                    dst: r1.into(),
                    src: r2.into(),
                    src_type: FloatType::F16,
                    dst_type: IntType::U32,
                    rnd_mode,
                    ftz,
                };
                let disasm =
                    format!("f2i{ftz_str}.u32.f16{int_rnd_mode_str} r1, r2;");
                c.push(instr, disasm);

                if sm >= 70 {
                    let instr = OpFRnd {
                        dst: r2.into(),
                        src: r2.into(),
                        src_type: FloatType::F64,
                        dst_type: FloatType::F64,
                        rnd_mode,
                        ftz,
                    };
                    let disasm =
                        format!("frnd{ftz_str}.f64{int_rnd_mode_str} r2, r2;");
                    c.push(instr, disasm);

                    let instr = OpFRnd {
                        dst: r1.into(),
                        src: r2.into(),
                        src_type: FloatType::F32,
                        dst_type: FloatType::F32,
                        rnd_mode,
                        ftz,
                    };
                    let disasm =
                        format!("frnd{ftz_str}{int_rnd_mode_str} r1, r2;");
                    c.push(instr, disasm);
                }

                let instr = OpI2F {
                    dst: r1.into(),
                    src: r2.into(),
                    src_type: IntType::U32,
                    dst_type: FloatType::F64,
                    rnd_mode,
                };
                let disasm = format!("i2f.f64.u32{rnd_mode_str} r1, r2;");
                c.push(instr, disasm);

                let instr = OpI2F {
                    dst: r1.into(),
                    src: r2.into(),
                    src_type: IntType::U32,
                    dst_type: FloatType::F16,
                    rnd_mode,
                };
                let disasm = format!("i2f.f16.u32{rnd_mode_str} r1, r2;");
                c.push(instr, disasm);

                if sm >= 120 {
                    let instr = OpF2F {
                        dst: ur1.into(),
                        src: ur2.into(),
                        src_type: FloatType::F16,
                        dst_type: FloatType::F32,
                        rnd_mode,
                        ftz,
                        integer_rnd: false,
                    };
                    let disasm = format!(
                        "uf2f{ftz_str}.f32.f16{rnd_mode_str} ur1, ur2;"
                    );
                    c.push(instr, disasm);

                    let instr = OpF2I {
                        dst: ur1.into(),
                        src: ur2.into(),
                        src_type: FloatType::F16,
                        dst_type: IntType::U32,
                        rnd_mode,
                        ftz,
                    };
                    let disasm = format!(
                        "uf2i{ftz_str}.u32.f16{int_rnd_mode_str} ur1, ur2;"
                    );
                    c.push(instr, disasm);

                    let instr = OpI2F {
                        dst: ur1.into(),
                        src: ur2.into(),
                        src_type: IntType::U32,
                        dst_type: FloatType::F16,
                        rnd_mode,
                    };
                    let disasm =
                        format!("ui2f.f16.u32{rnd_mode_str} ur1, ur2;");
                    c.push(instr, disasm);

                    let instr = OpFRnd {
                        dst: ur1.into(),
                        src: ur2.into(),
                        src_type: FloatType::F32,
                        dst_type: FloatType::F32,
                        rnd_mode,
                        ftz,
                    };
                    let disasm =
                        format!("ufrnd{ftz_str}{int_rnd_mode_str} ur1, ur2;");
                    c.push(instr, disasm);
                }

                for (saturate, saturate_str) in [(false, ""), (true, ".sat")] {
                    for (dst, (srcs, srcs_str)) in &test_two_src_cases {
                        let instr = OpFAdd {
                            dst: dst.clone(),
                            srcs: srcs.clone(),
                            saturate,
                            rnd_mode,
                            ftz,
                        };
                        let disasm = format!(
                            "fadd{ftz_str}{rnd_mode_str}{saturate_str} {dst}, {srcs_str};"
                        );
                        c.push(instr, disasm);

                        let instr = OpFMul {
                            dst: dst.clone(),
                            srcs: srcs.clone(),
                            saturate,
                            rnd_mode,
                            ftz,
                            dnz: false,
                        };
                        let disasm = format!(
                            "fmul{ftz_str}{rnd_mode_str}{saturate_str} {dst}, {srcs_str};"
                        );
                        c.push(instr, disasm);
                    }

                    for (dst, (srcs, srcs_str)) in &test_three_src_cases {
                        let instr = OpFFma {
                            dst: dst.clone(),
                            srcs: srcs.clone(),
                            saturate,
                            rnd_mode,
                            ftz,
                            dnz: false,
                        };
                        let disasm = format!(
                            "ffma{ftz_str}{rnd_mode_str}{saturate_str} {dst}, {srcs_str};"
                        );
                        c.push(instr, disasm);
                    }

                    if sm >= 120 {
                        for (dst, (srcs, srcs_str)) in
                            &test_uniform_two_src_cases
                        {
                            let instr = OpFAdd {
                                dst: dst.clone(),
                                srcs: srcs.clone(),
                                saturate,
                                rnd_mode,
                                ftz,
                            };
                            let disasm = format!(
                            "ufadd{ftz_str}{rnd_mode_str}{saturate_str} {dst}, {srcs_str};"
                            );
                            c.push(instr, disasm);

                            let instr = OpFMul {
                                dst: dst.clone(),
                                srcs: srcs.clone(),
                                saturate,
                                rnd_mode,
                                ftz,
                                dnz: false,
                            };
                            let disasm = format!(
                            "ufmul{ftz_str}{rnd_mode_str}{saturate_str} {dst}, {srcs_str};"
                            );
                            c.push(instr, disasm);
                        }

                        for (dst, (srcs, srcs_str)) in
                            &test_uniform_three_src_cases
                        {
                            let instr = OpFFma {
                                dst: dst.clone(),
                                srcs: srcs.clone(),
                                saturate,
                                rnd_mode,
                                ftz,
                                dnz: false,
                            };
                            let disasm = format!(
                            "uffma{ftz_str}{rnd_mode_str}{saturate_str} {dst}, {srcs_str};"
                            );
                            c.push(instr, disasm);
                        }
                    }
                }
            }
        }

        c.check(sm);
    }
}
