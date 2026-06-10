// Copyright © 2024 Igalia S.L.
// SPDX-License-Identifier: MIT

use crate::util::EtnaAsmResultExt;

use compiler::float16::F16;
use etnaviv_isa_proc::IsaParser;
use isa_bindings::*;
use pest::iterators::Pair;
use pest::Parser;
use std::fs;
use std::str::FromStr;

#[derive(IsaParser)]
#[isa = "etnaviv.xml"]
#[static_rules_file = "static_rules.pest"]
struct Isa;

fn get_child_rule(item: Pair<Rule>) -> Rule {
    item.into_inner().next().unwrap().as_rule()
}

fn parse_pair<T: FromStr>(item: Pair<Rule>) -> T
where
    T::Err: std::fmt::Debug,
{
    item.as_str().parse::<T>().unwrap()
}

fn parse_numeric<T: FromStr>(item: Pair<Rule>) -> T
where
    T::Err: std::fmt::Debug,
{
    let cleaned = item.as_str();
    // strip suffixes like :s20, :u20, :f16, :f20
    let cleaned = if let Some((number, _type)) = cleaned.split_once(':') {
        number
    } else {
        cleaned
    };
    cleaned.parse::<T>().unwrap()
}

fn fill_swizzle(item: Pair<Rule>) -> u32 {
    assert!(item.as_rule() == Rule::SrcSwizzle);

    item.into_inner()
        .map(|comp| match comp.as_rule() {
            Rule::Swiz => match comp.as_str() {
                "x" => 0,
                "y" => 1,
                "z" => 2,
                "w" => 3,
                _ => panic!("Unexpected swizzle {:?}", comp.as_str()),
            },
            _ => panic!("Unexpected rule {:?}", comp.as_rule()),
        })
        .enumerate()
        .fold(0, |acc, (index, swiz_index)| {
            acc | swiz_index << (2 * index)
        })
}

fn fill_destination(pair: Pair<Rule>, dest: &mut etna_inst_dst) {
    dest.set_use(1);

    for item in pair.into_inner() {
        match item.as_rule() {
            Rule::RegAddressingMode => {
                let rule = get_child_rule(item);
                dest.set_amode(isa_reg_addressing_mode::from_rule(rule));
            }
            Rule::Register => {
                dest.set_reg(parse_pair(item));
            }
            Rule::Wrmask => {
                let rule = get_child_rule(item);
                dest.set_write_mask(isa_wrmask::from_rule(rule));
            }
            _ => panic!("Unexpected rule {:?}", item.as_rule()),
        }
    }
}

fn fill_mem_destination(pair: Pair<Rule>, dest: &mut etna_inst_dst) {
    dest.set_use(1);

    for item in pair.into_inner() {
        match item.as_rule() {
            Rule::Wrmask => {
                let rule = get_child_rule(item);
                dest.set_write_mask(isa_wrmask::from_rule(rule));
            }
            _ => panic!("Unexpected rule {:?}", item.as_rule()),
        }
    }
}

fn fill_tex(pair: Pair<Rule>, tex: &mut etna_inst_tex) {
    for item in pair.into_inner() {
        match item.as_rule() {
            Rule::Register => {
                let r = parse_pair(item);
                tex.set_id(r)
            }
            Rule::SrcSwizzle => {
                let bytes = fill_swizzle(item);
                tex.set_swiz(bytes);
            }
            _ => panic!("Unexpected rule {:?}", item.as_rule()),
        }
    }
}

fn fill_source(pair: Pair<Rule>, src: &mut etna_inst_src) {
    src.set_use(1);

    for item in pair.into_inner() {
        match item.as_rule() {
            Rule::Absolute => unsafe {
                src.__bindgen_anon_1.__bindgen_anon_1.set_abs(1);
            },
            Rule::Negate => unsafe {
                src.__bindgen_anon_1.__bindgen_anon_1.set_neg(1);
            },
            Rule::RegGroup => {
                let rule = get_child_rule(item);
                src.set_rgroup(isa_reg_group::from_rule(rule));
            }
            Rule::RegAddressingMode => {
                let rule = get_child_rule(item);
                unsafe {
                    src.__bindgen_anon_1
                        .__bindgen_anon_1
                        .set_amode(isa_reg_addressing_mode::from_rule(rule));
                }
            }
            Rule::Register => {
                let r = parse_pair(item);
                unsafe {
                    src.__bindgen_anon_1.__bindgen_anon_1.set_reg(r);
                }
            }
            Rule::Immediate_inf_float => {
                src.set_rgroup(isa_reg_group::ISA_REG_GROUP_IMMED);

                let imm_struct = unsafe { &mut src.__bindgen_anon_1.__bindgen_anon_2 };

                imm_struct.set_imm_type(0);
                imm_struct.set_imm_val(0x7f800);
            }
            Rule::Immediate_neg_inf_float => {
                src.set_rgroup(isa_reg_group::ISA_REG_GROUP_IMMED);

                let imm_struct = unsafe { &mut src.__bindgen_anon_1.__bindgen_anon_2 };

                imm_struct.set_imm_type(0);
                imm_struct.set_imm_val(0xff800);
            }
            Rule::Immediate_inf_half_float => {
                src.set_rgroup(isa_reg_group::ISA_REG_GROUP_IMMED);

                let imm_struct = unsafe { &mut src.__bindgen_anon_1.__bindgen_anon_2 };

                imm_struct.set_imm_type(3);
                imm_struct.set_imm_val(0x7c00);
            }
            Rule::Immediate_neg_inf_half_float => {
                src.set_rgroup(isa_reg_group::ISA_REG_GROUP_IMMED);

                let imm_struct = unsafe { &mut src.__bindgen_anon_1.__bindgen_anon_2 };

                imm_struct.set_imm_type(3);
                imm_struct.set_imm_val(0xfc00);
            }
            Rule::Immediate_nan_float => {
                src.set_rgroup(isa_reg_group::ISA_REG_GROUP_IMMED);

                let imm_struct = unsafe { &mut src.__bindgen_anon_1.__bindgen_anon_2 };

                imm_struct.set_imm_type(0);
                imm_struct.set_imm_val(0x7fc00);
            }
            Rule::Immediate_neg_nan_float => {
                src.set_rgroup(isa_reg_group::ISA_REG_GROUP_IMMED);

                let imm_struct = unsafe { &mut src.__bindgen_anon_1.__bindgen_anon_2 };

                imm_struct.set_imm_type(0);
                imm_struct.set_imm_val(0xffc00);
            }
            Rule::Immediate_nan_half_float => {
                src.set_rgroup(isa_reg_group::ISA_REG_GROUP_IMMED);

                let imm_struct = unsafe { &mut src.__bindgen_anon_1.__bindgen_anon_2 };

                imm_struct.set_imm_type(3);
                imm_struct.set_imm_val(0x7fff);
            }
            Rule::Immediate_neg_nan_half_float => {
                src.set_rgroup(isa_reg_group::ISA_REG_GROUP_IMMED);

                let imm_struct = unsafe { &mut src.__bindgen_anon_1.__bindgen_anon_2 };

                imm_struct.set_imm_type(3);
                imm_struct.set_imm_val(0xffff);
            }
            Rule::Immediate_float => {
                let value: f32 = parse_numeric(item);
                let bits = value.to_bits();

                assert!((bits & 0xfff) == 0); /* 12 lsb cut off */
                src.set_rgroup(isa_reg_group::ISA_REG_GROUP_IMMED);

                let imm_struct = unsafe { &mut src.__bindgen_anon_1.__bindgen_anon_2 };

                imm_struct.set_imm_type(0);
                imm_struct.set_imm_val(bits >> 12);
            }
            Rule::Immediate_half_float => {
                let value: f32 = parse_numeric(item);
                let bits = F16::from_f32_rtne(value).to_bits();

                src.set_rgroup(isa_reg_group::ISA_REG_GROUP_IMMED);

                let imm_struct = unsafe { &mut src.__bindgen_anon_1.__bindgen_anon_2 };

                imm_struct.set_imm_type(3);
                imm_struct.set_imm_val(bits as u32);
            }
            Rule::Immediate_int => {
                let value: i32 = parse_numeric(item);
                assert!(
                    (-0x80000..=0x7ffff).contains(&value),
                    "Immediate_int out of 20-bit signed range: {value}"
                );

                src.set_rgroup(isa_reg_group::ISA_REG_GROUP_IMMED);

                let imm_struct = unsafe { &mut src.__bindgen_anon_1.__bindgen_anon_2 };

                // 20-bit two's-complement
                let imm_val = (value as u32) & 0xfffff;

                imm_struct.set_imm_type(1);
                imm_struct.set_imm_val(imm_val);
            }
            Rule::Immediate_uint => {
                let value: u32 = parse_numeric(item);
                assert!(value <= 0xfffff, "Immediate_uint out of range: {value}");

                src.set_rgroup(isa_reg_group::ISA_REG_GROUP_IMMED);

                let imm_struct = unsafe { &mut src.__bindgen_anon_1.__bindgen_anon_2 };

                imm_struct.set_imm_type(2);
                imm_struct.set_imm_val(value);
            }
            Rule::SrcSwizzle => {
                let bytes = fill_swizzle(item);
                unsafe {
                    src.__bindgen_anon_1.__bindgen_anon_1.set_swiz(bytes);
                }
            }
            _ => panic!("Unexpected rule {:?}", item.as_rule()),
        }
    }
}

fn process(input: Pair<Rule>) -> Option<etna_inst> {
    // The assembler and disassembler are both using the
    // 'full' form of the ISA which contains void's and
    // use the HW ordering of instruction src arguments.

    if input.as_rule() == Rule::EOI {
        return None;
    }

    // Create instruction with sane defaults.
    let mut instr = etna_inst::default();
    instr.dst.set_write_mask(isa_wrmask::ISA_WRMASK_XYZW);

    instr.opcode = isa_opc::from_rule(input.as_rule());
    let mut src_index = 0;

    for p in input.into_inner() {
        match p.as_rule() {
            Rule::Dst_full => {
                instr.set_dst_full(1);
            }
            Rule::Sat => {
                instr.set_sat(1);
            }
            Rule::Cond => {
                let rule = get_child_rule(p);
                instr.set_cond(isa_cond::from_rule(rule));
            }
            Rule::Skphp => {
                instr.set_skphp(1);
            }
            Rule::Pmode => {
                instr.set_pmode(1);
            }
            Rule::Denorm => {
                instr.set_denorm(1);
            }
            Rule::Local => {
                instr.set_local(1);
            }
            Rule::Unk => {
                instr.set_unk(1);
            }
            Rule::Left_shift => {
                let item = p.into_inner().next().unwrap();
                let amount = parse_pair(item);
                instr.set_left_shift(amount);
            }
            Rule::Type => {
                let rule = get_child_rule(p);
                instr.type_ = isa_type::from_rule(rule);
            }
            Rule::Thread => {
                let rule = get_child_rule(p);
                instr.set_thread(isa_thread::from_rule(rule));
            }
            Rule::Rounding => {
                let rule = get_child_rule(p);
                instr.rounding = isa_rounding::from_rule(rule);
            }
            Rule::DestVoid => {
                // Nothing to do
            }
            Rule::DstRegister => {
                fill_destination(p, &mut instr.dst);
            }
            Rule::DstMemAddr => {
                fill_mem_destination(p, &mut instr.dst);
            }
            Rule::SrcVoid => {
                // Nothing to do
            }
            Rule::SrcRegister => {
                fill_source(p, &mut instr.src[src_index]);
                src_index += 1;
            }
            Rule::TexSrc => {
                fill_tex(p, &mut instr.tex);
            }
            Rule::Target => {
                let target = parse_pair(p);
                instr.imm = target;
            }
            _ => panic!("Unexpected rule {:?}", p.as_rule()),
        }
    }

    Some(instr)
}

fn parse(rule: Rule, content: &str, asm_result: &mut etna_asm_result) {
    let result = Isa::parse(rule, content);

    match result {
        Ok(program) => {
            for line in program {
                if let Some(result) = process(line) {
                    asm_result.append_instruction(result);
                }
            }

            asm_result.success = true;
        }
        Err(e) => {
            asm_result.set_error(&format!("{}", e));
            asm_result.success = false;
        }
    }
}

pub fn asm_process_str(string: &str, asm_result: &mut etna_asm_result) {
    parse(Rule::instruction, string, asm_result)
}

pub fn asm_process_file(file: &str, asm_result: &mut etna_asm_result) {
    let content = fs::read_to_string(file).expect("cannot read file");

    parse(Rule::instructions, &content, asm_result)
}
