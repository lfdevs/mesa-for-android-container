// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::ident;
use crate::isa::*;
use proc_macro2::TokenStream as TokenStream2;
use proc_macro2::{Ident, Span};
use quote::ToTokens;
use std::collections::{BTreeMap, HashMap, HashSet};
use std::rc::Rc;

// We assume FAU index and flow will be handled elsewhere
fn skip_field(field: &InstrField) -> bool {
    match field {
        InstrField::Physical(f) => match f.name.as_str() {
            "fau_page_index" | "flow" => true,
            _ => false,
        },
        _ => false,
    }
}

const SRC_SWIZZLE_ENUMS: &[&'static str] = &[
    "lane8_m",
    "lane16_m",
    "lanes_int_m",
    "shift_bitwise_i16_lanes_m",
    "shift_bitwise_i32_lanes_m",
    "swiz8_2_full_m",
    "swiz_int_m",
    "swiz_m",
    "widen_int_m",
    "widen_m",
];

const DST_LANES_ENUMS: &[&'static str] = &[
    "dest_width_m",
    "dest_width_narrow_m",
    "dest_width_replicate_m",
    "dest_width_single_lane_16_bit_m",
    "lane_all_m",
    "load_lane_16_m",
    "load_lane_32_m",
];

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum SrControl {
    None,
    Read,
    Write,
    ReadWrite,
}

impl SrControl {
    fn has_read(self) -> bool {
        matches!(self, SrControl::Read | SrControl::ReadWrite)
    }

    fn has_write(self) -> bool {
        matches!(self, SrControl::Write | SrControl::ReadWrite)
    }
}

fn get_instr_sr_control(instr: &Instr) -> SrControl {
    if instr.name.starts_with("TEX_") {
        // Texture instructions don't actually have a sr_control fields but
        // we knoa a priori that they're ReadWrite.
        return SrControl::ReadWrite;
    }

    for f in &instr.fields {
        let InstrField::Physical(f) = f else {
            continue;
        };

        if !f.type_.as_ref().is_some_and(|t| t.is_enum("sr_control_t")) {
            continue;
        }

        let lit = f.expr.as_ref().unwrap().as_enum().unwrap();
        return match lit.value_name.as_str() {
            "read" => SrControl::Read,
            "write" => SrControl::Write,
            "read_write" => SrControl::ReadWrite,
            _ => panic!("Unknown sr_control_t value"),
        };
    }

    SrControl::None
}

#[derive(Clone)]
pub enum SrcType {
    HWEnum(Rc<Enum>),
    MetaEnum(Rc<MetaEnum>),
    Dst,
    Src,
    SrRead,
    SrWrite,
    PcRelOffset,
    Int(u8),
    Uint(u8),
}

#[derive(Clone, Copy, Hash, Eq, PartialEq)]
pub enum SrcField {
    Direct,
    DstReg,
    DstLanes,
    EncodedSrc,
    SrcSwizzle,
    SrcModAbs,
    SrcModNeg,
    SrcModNot,
    SrIndex,
    SrCount,
}

impl SrcType {
    const DIRECT: SrcField = SrcField::Direct;
    const DST_FIELDS: &[SrcField] = &[SrcField::DstReg, SrcField::DstLanes];
    const SRC_FIELDS: &[SrcField] = &[
        SrcField::EncodedSrc,
        SrcField::SrcSwizzle,
        SrcField::SrcModAbs,
        SrcField::SrcModNeg,
        SrcField::SrcModNot,
    ];
    const SR_READ_FIELDS: &[SrcField] =
        &[SrcField::SrIndex, SrcField::SrCount, SrcField::SrcSwizzle];
    const SR_WRITE_FIELDS: &[SrcField] =
        &[SrcField::SrIndex, SrcField::SrCount, SrcField::DstLanes];

    fn from_field_type(field_type: &FieldType) -> SrcType {
        match field_type {
            FieldType::Enum(e) => {
                if let Some(me) = e.get_meta() {
                    SrcType::MetaEnum(me)
                } else {
                    SrcType::HWEnum(e.clone())
                }
            }
            FieldType::PcRelOffsetSigned | FieldType::PcRelOffsetUnsigned => {
                SrcType::PcRelOffset
            }
            FieldType::Source | FieldType::Source64 => SrcType::Src,
            FieldType::Int(bits) => SrcType::Int(*bits),
            FieldType::Uint(bits) => SrcType::Uint(*bits),
        }
    }

    fn fields(&self) -> &[SrcField] {
        match self {
            SrcType::HWEnum(_)
            | SrcType::MetaEnum(_)
            | SrcType::PcRelOffset
            | SrcType::Int(_)
            | SrcType::Uint(_) => std::slice::from_ref(&SrcType::DIRECT),
            SrcType::Dst => SrcType::DST_FIELDS,
            SrcType::Src => SrcType::SRC_FIELDS,
            SrcType::SrRead => SrcType::SR_READ_FIELDS,
            SrcType::SrWrite => SrcType::SR_WRITE_FIELDS,
        }
    }

    fn merge(&mut self, other: &SrcType) {
        use SrcType::*;
        match self {
            HWEnum(e) => match other {
                HWEnum(o) => assert_eq!(e.ident, o.ident),
                _ => panic!("Field types don't match"),
            },
            MetaEnum(e) => match other {
                MetaEnum(o) => assert_eq!(e.ident, o.ident),
                _ => panic!("Field types don't match"),
            },
            PcRelOffset => assert!(matches!(other, PcRelOffset)),
            Dst => assert!(matches!(other, Dst)),
            Src => assert!(matches!(other, Src)),
            SrRead => assert!(matches!(other, SrRead)),
            SrWrite => assert!(matches!(other, SrWrite)),
            Int(s_bits) => match other {
                Int(o_bits) => *s_bits = (*s_bits).max(*o_bits),
                _ => panic!("Field types don't match"),
            },
            Uint(s_bits) => match other {
                Uint(o_bits) => *s_bits = (*s_bits).max(*o_bits),
                _ => panic!("Field types don't match"),
            },
        }
    }

    fn has_enum_none(&self) -> bool {
        match self {
            SrcType::HWEnum(e) => e.has_none,
            SrcType::MetaEnum(e) => e.has_none,
            _ => false,
        }
    }
}

fn field_type_to_tokens(field_type: &FieldType) -> TokenStream2 {
    match field_type {
        FieldType::Enum(e) => {
            let ident = &e.ident;
            quote! { #ident }
        }
        FieldType::PcRelOffsetSigned => quote! { i64 },
        FieldType::PcRelOffsetUnsigned => quote! { u64 },
        FieldType::Source => quote! { u16 },
        FieldType::Source64 => quote! { u16 },
        FieldType::Int(bits) => {
            let itype = ident!("i{}", bits.max(&8).next_power_of_two());
            quote! { #itype }
        }
        FieldType::Uint(bits) => {
            let utype = ident!("u{}", bits.max(&8).next_power_of_two());
            quote! { #utype }
        }
    }
}

impl ToTokens for SrcType {
    fn to_tokens(&self, ts: &mut TokenStream2) {
        ts.extend(match self {
            SrcType::HWEnum(e) => e.ident.to_token_stream(),
            SrcType::MetaEnum(e) => e.ident.to_token_stream(),
            SrcType::PcRelOffset => quote! { i64 },
            SrcType::Dst => quote! { EncodedDst },
            SrcType::Src => quote! { EncodedSrc },
            SrcType::SrRead => quote! { SrRead },
            SrcType::SrWrite => quote! { SrWrite },
            SrcType::Int(bits) => {
                let itype = ident!("i{}", bits.max(&8).next_power_of_two());
                quote! { #itype }
            }
            SrcType::Uint(bits) => {
                let utype = ident!("u{}", bits.max(&8).next_power_of_two());
                quote! { #utype }
            }
        });
    }
}

const LOAD_INSTRS: &[&str] =
    &["LD_CHECKED", "LD_CHECKED_IMM", "LD_PKA", "LOAD"];
const STORE_INSTRS: &[&str] = &["STORE", "ST_CHECKED", "ST_CHECKED_IMM"];

fn map_field_src(
    instr_name: &str,
    field_name: &str,
    field_type: &FieldType,
    sr_control: SrControl,
) -> (SrcType, SrcField) {
    if matches!(field_type, FieldType::Source | FieldType::Source64) {
        (SrcType::Src, SrcField::EncodedSrc)
    } else if field_name == "dest_register_index" {
        (SrcType::Dst, SrcField::DstReg)
    } else if field_name == "staging_register_index" {
        if sr_control.has_read() {
            (SrcType::SrRead, SrcField::SrIndex)
        } else {
            (SrcType::SrWrite, SrcField::SrIndex)
        }
    } else if field_name == "staging_register_write_index" {
        assert_eq!(sr_control, SrControl::ReadWrite);
        (SrcType::SrWrite, SrcField::SrIndex)
    } else if field_name == "lane" && LOAD_INSTRS.contains(&&instr_name) {
        (SrcType::SrWrite, SrcField::DstLanes)
    } else if field_name == "lane" && STORE_INSTRS.contains(&&instr_name) {
        (SrcType::SrRead, SrcField::SrcSwizzle)
    } else if SRC_SWIZZLE_ENUMS.iter().any(|e| field_type.is_enum(e)) {
        (SrcType::Src, SrcField::SrcSwizzle)
    } else if DST_LANES_ENUMS.iter().any(|e| field_type.is_enum(e)) {
        if !sr_control.has_write() {
            assert!(
                [
                    "lane",
                    "dest_width",
                    "dest_width_narrow",
                    "dest_width_replicate"
                ]
                .contains(&field_name)
            );
            (SrcType::Dst, SrcField::DstLanes)
        } else {
            (SrcType::from_field_type(field_type), SrcField::Direct)
        }
    } else if field_type.is_enum("abs_m") && !field_name.ends_with("result") {
        (SrcType::Src, SrcField::SrcModAbs)
    } else if field_type.is_enum("neg_m") && !field_name.ends_with("result") {
        (SrcType::Src, SrcField::SrcModNeg)
    } else if field_type.is_enum("not_m") && !field_name.ends_with("result") {
        (SrcType::Src, SrcField::SrcModNot)
    } else if field_name == "sr_count" {
        if sr_control.has_read() {
            (SrcType::SrRead, SrcField::SrCount)
        } else {
            (SrcType::SrWrite, SrcField::SrCount)
        }
    } else if field_name == "sr_write_count" {
        assert_eq!(sr_control, SrControl::ReadWrite);
        (SrcType::SrWrite, SrcField::SrCount)
    } else {
        (SrcType::from_field_type(field_type), SrcField::Direct)
    }
}

#[derive(Clone)]
pub struct InstrEncSrc {
    name: String,
    ident: Ident,
    type_: SrcType,
    optional: bool,
}

const SRC_NAME_PREFIXES: &[&'static str] = &[
    "src",
    "staging_register_index",
    "sr_count",
    "lane",
    "lanes",
    "swz",
    "widen",
    "not",
    "abs",
    "neg",
];

fn src_number(field_name: &str) -> u8 {
    let pos = field_name
        .find(char::is_numeric)
        .expect("src fields should end in a number");
    assert!(SRC_NAME_PREFIXES.contains(&&field_name[0..pos]));
    u8::from_str_radix(&field_name[pos..], 10).unwrap()
}

impl InstrEncSrc {
    fn src_name(field_name: &str, src_type: &SrcType) -> String {
        match src_type {
            SrcType::Src => format!("src{}", src_number(field_name)),
            SrcType::Dst => "dst".to_string(),
            SrcType::SrRead => "sr_src".to_string(),
            SrcType::SrWrite => "sr_dst".to_string(),
            _ => to_snake_case(field_name),
        }
    }

    fn new(name: &str, src_type: SrcType) -> InstrEncSrc {
        let ident = Ident::new(name, Span::call_site());
        InstrEncSrc {
            name: name.to_string(),
            ident,
            type_: src_type,
            optional: false,
        }
    }

    fn merge(&mut self, other: &InstrEncSrc) {
        assert_eq!(self.name, other.name);
        self.type_.merge(&other.type_);
    }

    fn declare(&self, ts: &mut TokenStream2) {
        let InstrEncSrc { ident, type_, .. } = self;
        if self.optional && !type_.has_enum_none() {
            ts.extend(quote! { pub #ident: Option<#type_>, })
        } else {
            ts.extend(quote! { pub #ident: #type_, })
        }
    }

    fn load(&self, field: &SrcField) -> TokenStream2 {
        let InstrEncSrc { ident, type_, .. } = self;

        let src = if self.optional && !type_.has_enum_none() {
            quote! { self.#ident.unwrap() }
        } else {
            quote! { self.#ident }
        };

        match field {
            SrcField::Direct => src,
            SrcField::DstReg => quote! { #src.reg },
            SrcField::DstLanes => quote! { #src.lanes },
            SrcField::EncodedSrc => quote! { #src.encoded },
            SrcField::SrcSwizzle => quote! { #src.swizzle },
            SrcField::SrcModAbs => quote! { #src.abs },
            SrcField::SrcModNeg => quote! { #src.neg },
            SrcField::SrcModNot => quote! { #src.not },
            SrcField::SrIndex => quote! { #src.index },
            SrcField::SrCount => quote! { #src.count },
        }
    }

    fn assert_unused(&self, field: &SrcField) -> TokenStream2 {
        let ident = &self.ident;
        match field {
            SrcField::Direct => {
                // For direct, we do't want to do a load because that might
                // unwrap!
                quote! { debug_assert!(self.#ident.is_none()); }
            }
            SrcField::DstReg | SrcField::EncodedSrc | SrcField::SrIndex => {
                panic!("If we use a src/dst, we have to use the reg");
            }
            SrcField::DstLanes | SrcField::SrcSwizzle => {
                let src = self.load(field);
                quote! { debug_assert!(#src.is_none()); }
            }
            SrcField::SrcModAbs | SrcField::SrcModNeg | SrcField::SrcModNot => {
                let src = self.load(field);
                quote! { debug_assert_eq!(#src, false); }
            }
            SrcField::SrCount => {
                // This one is okay to ignore because a bunch of
                // instructions compute it based on the variant.
                Default::default()
            }
        }
    }
}

struct InstrEncFieldSrc {
    ident: Ident,
    field_type: FieldType,
    field_restrict: Option<Rc<FieldRestrict>>,
    src_name: String,
    src_field: SrcField,
}

impl InstrEncFieldSrc {
    fn new(
        field_name: &str,
        field_type: FieldType,
        field_restrict: Option<Rc<FieldRestrict>>,
        src_name: &str,
        src_field: SrcField,
    ) -> InstrEncFieldSrc {
        let ident = Ident::new(field_name, Span::call_site());
        let src_name = src_name.to_string();
        InstrEncFieldSrc {
            ident,
            field_type,
            field_restrict,
            src_name,
            src_field,
        }
    }

    fn to_tokens(&self, ts: &mut TokenStream2, srcs: &InstrEncSources) {
        let src = srcs.srcs.get(self.src_name.as_str()).unwrap();
        let src = src.load(&self.src_field);

        let f_ident = &self.ident;
        let f_type = field_type_to_tokens(&self.field_type);
        if matches!(self.src_field, SrcField::SrCount)
            && self.field_type.is_enum("ls_multi_sr_count_m")
        {
            ts.extend(quote! {
                let #f_ident = #f_type::from_sr_count(#src)?;
            });
        } else {
            ts.extend(quote! {
                let #f_ident = #f_type::try_from(#src)?;
            });
        }

        if let Some(restrict) = &self.field_restrict {
            let err = format!("Unsupported {f_ident} value");
            ts.extend(quote! {
                if !#restrict.contains(&#f_ident) {
                    return Err(#err.into());
                }
            });
        }

        if matches!(&self.field_type, FieldType::Enum(_)) {
            ts.extend(quote! {
                let #f_ident = #f_ident.try_encode(arch)?;
            });
        }
    }
}

struct InstrEncSources {
    ident: Ident,
    variants: Option<Ident>,
    srcs: BTreeMap<String, InstrEncSrc>,
    new_srcs_optional: bool,
}

impl InstrEncSources {
    fn new(name: &str) -> InstrEncSources {
        let camel_name = to_camel_case(name);
        let ident = Ident::new(&camel_name, Span::call_site());
        InstrEncSources {
            ident,
            variants: None,
            srcs: Default::default(),
            new_srcs_optional: false,
        }
    }

    fn enable_variants(&mut self) {
        if self.variants.is_none() {
            self.variants = Some(ident!("{}Variant", self.ident));
        }
    }

    fn add_src(&mut self, src: InstrEncSrc) -> &InstrEncSrc {
        let new_srcs_optional = self.new_srcs_optional;
        self.srcs
            .entry(src.name.to_string())
            .and_modify(|s| s.merge(&src))
            .or_insert_with(|| {
                let mut src = src;
                src.optional = new_srcs_optional;
                src
            })
    }

    fn add_src_for_src_dst_field(
        &mut self,
        instr_name: &str,
        field_name: &str,
        field_type: &FieldType,
        field_restrict: &Option<Rc<FieldRestrict>>,
        sr_control: SrControl,
    ) -> Option<InstrEncFieldSrc> {
        let (src_type, src_field) =
            map_field_src(instr_name, field_name, field_type, sr_control);

        if !matches!(
            src_field,
            SrcField::EncodedSrc | SrcField::DstReg | SrcField::SrIndex,
        ) {
            return None;
        }

        let src_name = InstrEncSrc::src_name(field_name, &src_type);
        let src = self.add_src(InstrEncSrc::new(&src_name, src_type));

        Some(InstrEncFieldSrc::new(
            field_name,
            field_type.clone(),
            field_restrict.clone(),
            &src.name,
            src_field,
        ))
    }

    fn add_src_for_other_field(
        &mut self,
        instr_name: &str,
        field_name: &str,
        field_type: &FieldType,
        field_restrict: &Option<Rc<FieldRestrict>>,
        sr_control: SrControl,
    ) -> InstrEncFieldSrc {
        let (src_type, src_field) =
            map_field_src(instr_name, field_name, field_type, sr_control);

        if matches!(
            src_type,
            SrcType::Dst | SrcType::Src | SrcType::SrRead | SrcType::SrWrite
        ) {
            // If it comes from a src or a dst, try to look up the existing
            // source in the list and use that
            let name = InstrEncSrc::src_name(field_name, &src_type);
            if let Some(src) = self.srcs.get(&name) {
                return InstrEncFieldSrc::new(
                    field_name,
                    field_type.clone(),
                    field_restrict.clone(),
                    &src.name,
                    src_field,
                );
            }
        }

        // Otherwise, we have to assume direct.  Also, don't be clever with the
        // name.  Just snakify the name we got from the XML.
        let src_name = to_snake_case(field_name);
        let src_type = SrcType::from_field_type(field_type);
        let src_field = SrcField::Direct;

        let src = self.add_src(InstrEncSrc::new(&src_name, src_type));

        InstrEncFieldSrc::new(
            field_name,
            field_type.clone(),
            field_restrict.clone(),
            &src.name,
            src_field,
        )
    }

    fn add_instr_src_set<'a>(&mut self, src_names: &HashSet<&'a str>) {
        for (name, src) in self.srcs.iter_mut() {
            if !src_names.contains(name.as_str()) {
                src.optional = true;
            }
        }
        self.new_srcs_optional = true;
    }
}

fn valid_field_values(
    arch: Range<u8>,
    field_type: &FieldType,
    field_restrict: &Option<Rc<FieldRestrict>>,
) -> Vec<EnumLiteral> {
    if let Some(restrict) = field_restrict {
        restrict
            .values
            .iter()
            .map(|v| v.to_meta().unwrap())
            .collect()
    } else {
        let FieldType::Enum(e) = field_type else {
            panic!("Swizzle field must have an enum type");
        };
        let map_fn = |v: &EnumValue| {
            if v.arch.contains(arch.start) {
                assert!(v.arch.contains_range(arch.clone()));
                Some(EnumLiteral::new(e, v).to_meta().unwrap())
            } else {
                None
            }
        };
        e.values.values().filter_map(map_fn).collect()
    }
}

#[derive(Default)]
struct InstrVariantSrcInfo {
    exists: bool,
    allowed_swizzles: Vec<EnumLiteral>,
    has_abs: bool,
    has_neg: bool,
    has_not: bool,
}

impl InstrVariantSrcInfo {
    fn add_field(
        &mut self,
        arch: Range<u8>,
        src_field: SrcField,
        field_type: &FieldType,
        field_restrict: &Option<Rc<FieldRestrict>>,
    ) {
        match src_field {
            SrcField::EncodedSrc | SrcField::SrIndex => {
                assert!(!self.exists);
                self.exists = true;
            }
            SrcField::SrcSwizzle => {
                assert!(self.allowed_swizzles.is_empty());
                self.allowed_swizzles =
                    valid_field_values(arch, field_type, field_restrict);
            }
            SrcField::SrcModAbs => {
                self.has_abs = true;
            }
            SrcField::SrcModNeg => {
                self.has_neg = true;
            }
            SrcField::SrcModNot => {
                self.has_not = true;
            }
            SrcField::SrCount => (),
            _ => panic!("Invalid Src field"),
        }
    }
}

impl ToTokens for InstrVariantSrcInfo {
    fn to_tokens(&self, ts: &mut TokenStream2) {
        let InstrVariantSrcInfo {
            has_abs,
            has_neg,
            has_not,
            ..
        } = self;

        let mut swizzles_ts = TokenStream2::new();
        if !self.exists {
            // Leave the swizzle set empty to indicate a non-existant source
        } else if self.allowed_swizzles.is_empty() {
            swizzles_ts.extend(quote! { SrcSwizzle::None as u8 });
        } else {
            for s in &self.allowed_swizzles {
                swizzles_ts.extend(quote! { #s as u8, });
            }
        }

        ts.extend(quote! {
            InstructionSrcInfo {
                allowed_swizzles: unsafe {
                    U8EnumSet::from_u8_array([#swizzles_ts])
                },
                has_abs: #has_abs,
                has_neg: #has_neg,
                has_not: #has_not,
            }
        });
    }
}

#[derive(Default)]
struct InstrVariantDstInfo {
    exists: bool,
    is_sr: bool,
    allowed_lanes: Vec<EnumLiteral>,
}

impl InstrVariantDstInfo {
    fn add_field(
        &mut self,
        arch: Range<u8>,
        src_field: SrcField,
        field_type: &FieldType,
        field_restrict: &Option<Rc<FieldRestrict>>,
    ) {
        match src_field {
            SrcField::DstReg | SrcField::SrIndex => {
                assert!(!self.exists);
                self.exists = true;
                self.is_sr = matches!(src_field, SrcField::SrIndex);
            }
            SrcField::DstLanes => {
                assert!(self.allowed_lanes.is_empty());
                self.allowed_lanes =
                    valid_field_values(arch, field_type, field_restrict);
            }
            SrcField::SrCount => (),
            _ => panic!("Invalid Dst field"),
        }
    }
}

impl ToTokens for InstrVariantDstInfo {
    fn to_tokens(&self, ts: &mut TokenStream2) {
        let InstrVariantDstInfo { is_sr, .. } = self;

        assert!(self.exists);
        let mut lanes_ts = TokenStream2::new();
        if self.allowed_lanes.is_empty() {
            lanes_ts.extend(quote! { DstLanes::None as u8 });
        } else {
            for s in &self.allowed_lanes {
                lanes_ts.extend(quote! { #s as u8, });
            }
        }

        ts.extend(quote! {
            InstructionDstInfo {
                is_sr: #is_sr,
                allowed_lanes: unsafe {
                    U8EnumSet::from_u8_array([#lanes_ts])
                },
            }
        });
    }
}

struct InstrVariantInfo {
    ident: Ident,
    arch: Range<u8>,
    is_message: bool,
    srcs: Vec<InstrVariantSrcInfo>,
    sr_src: Option<InstrVariantSrcInfo>,
    dst: Option<InstrVariantDstInfo>,
}

impl InstrVariantInfo {
    fn new(instr: &Instr) -> InstrVariantInfo {
        let mut name = format!("instruction_info_v{}", instr.arch.start);
        if let Some(variant) = &instr.variant {
            name += "_";
            name += &to_snake_case(variant);
        }
        let name = name.to_uppercase();
        let ident = Ident::new(&name, Span::call_site());

        InstrVariantInfo {
            ident,
            arch: instr.arch.clone(),
            is_message: false,
            srcs: Default::default(),
            sr_src: Default::default(),
            dst: Default::default(),
        }
    }

    fn add_field(
        &mut self,
        instr_name: &str,
        field_name: &str,
        field_type: &FieldType,
        field_restrict: &Option<Rc<FieldRestrict>>,
        sr_control: SrControl,
    ) {
        if field_name == "message_slot_index" {
            self.is_message = true;
            return;
        }

        let (src_type, src_field) =
            map_field_src(instr_name, field_name, field_type, sr_control);
        match src_type {
            SrcType::Src => {
                let n = usize::from(src_number(field_name));
                if n >= self.srcs.len() {
                    self.srcs.resize_with(n + 1, Default::default);
                }
                self.srcs[n].add_field(
                    self.arch.clone(),
                    src_field,
                    field_type,
                    field_restrict,
                );
            }
            SrcType::SrRead => {
                self.sr_src.get_or_insert_default().add_field(
                    self.arch.clone(),
                    src_field,
                    field_type,
                    field_restrict,
                );
            }
            SrcType::Dst | SrcType::SrWrite => {
                self.dst.get_or_insert_default().add_field(
                    self.arch.clone(),
                    src_field,
                    field_type,
                    field_restrict,
                );
            }
            _ => (),
        }
    }
}

impl ToTokens for InstrVariantInfo {
    fn to_tokens(&self, ts: &mut TokenStream2) {
        let InstrVariantInfo {
            ident, is_message, ..
        } = self;

        let mut src_infos_ts = TokenStream2::new();
        for src in &self.srcs {
            src_infos_ts.extend(quote! { #src, });
        }
        let srcs_ts = quote! { &[#src_infos_ts] };

        let sr_src_ts = if let Some(src) = &self.sr_src {
            quote! { Some(#src) }
        } else {
            quote! { None }
        };

        let dst_ts = if let Some(dst) = &self.dst {
            quote! { Some(#dst) }
        } else {
            quote! { None }
        };

        ts.extend(quote! {
            const #ident: InstructionInfo = InstructionInfo {
                is_message: #is_message,
                srcs: #srcs_ts,
                sr_src: #sr_src_ts,
                dst: #dst_ts,
            };
        });
    }
}

struct InstrEncVariant {
    instr: Instr,
    field_srcs: Vec<Option<InstrEncFieldSrc>>,
    info: InstrVariantInfo,
}

impl InstrEncVariant {
    fn new(instr: Instr, srcs: &mut InstrEncSources) -> InstrEncVariant {
        let mut info = InstrVariantInfo::new(&instr);
        let sr_control = get_instr_sr_control(&instr);

        let mut field_srcs = Vec::new();
        field_srcs.resize_with(instr.fields.len(), Default::default);

        // Iteration here is a bit annoying.  Put it in a vector
        let mut fields = Vec::new();
        for (i, field) in instr.fields.iter().enumerate() {
            if skip_field(field) {
                continue;
            }

            let (field_name, field_type, restrict) = match field {
                InstrField::Virtual(f) => {
                    // Virtual fields are always sources since they're used to
                    // calculate computed physical fields.
                    let mut restrict = f.restrict.clone();
                    if let Some(lit) = f.expr.as_enum() {
                        restrict = Some(Rc::new(FieldRestrict {
                            values: vec![lit.clone()],
                        }));
                    }
                    (&f.name, &f.type_, restrict)
                }
                InstrField::Physical(f) => {
                    // Physical fields only show up as sources if we can't
                    // automatically calculate them.
                    if f.expr.is_some() {
                        continue;
                    }
                    let restrict = f.restrict.clone();
                    (&f.name, f.type_.as_ref().unwrap(), restrict)
                }
                InstrField::Reserved(_) => continue,
            };

            info.add_field(
                &instr.name,
                field_name,
                &field_type,
                &restrict,
                sr_control,
            );

            fields.push((i, field_name, field_type, restrict));
        }

        // Add the sources and destinations first
        for (i, field_name, field_type, restrict) in fields.iter().cloned() {
            field_srcs[i] = srcs.add_src_for_src_dst_field(
                &instr.name,
                field_name,
                &field_type,
                &restrict,
                sr_control,
            );
        }

        // Add all the other sources
        for (i, field_name, field_type, restrict) in fields.iter().cloned() {
            if field_srcs[i].is_some() {
                continue;
            }

            field_srcs[i] = Some(srcs.add_src_for_other_field(
                &instr.name,
                field_name,
                &field_type,
                &restrict,
                sr_control,
            ));
        }

        assert_eq!(instr.fields.len(), field_srcs.len());

        let src_names: HashSet<&str> = field_srcs
            .iter()
            .filter_map(|s| s.as_ref().map(|s| s.src_name.as_str()))
            .collect();
        srcs.add_instr_src_set(&src_names);

        InstrEncVariant {
            instr,
            field_srcs,
            info,
        }
    }

    fn to_tokens(&self, ts: &mut TokenStream2, srcs: &InstrEncSources) {
        // First, assert that any unused sources are none
        let mut used_srcs: HashMap<_, HashSet<_>> = Default::default();
        for fs in &self.field_srcs {
            if let Some(fs) = fs {
                used_srcs
                    .entry(&fs.src_name)
                    .or_default()
                    .insert(&fs.src_field);
            }
        }
        for src in srcs.srcs.values() {
            if let Some(used_fields) = used_srcs.get(&src.name) {
                for src_field in src.type_.fields() {
                    if !used_fields.contains(src_field) {
                        ts.extend(src.assert_unused(src_field));
                    }
                }
            } else {
                ts.extend(src.assert_unused(&SrcField::Direct));
            }
        }

        // Now collect all the fields that directly come from sources
        for fs in &self.field_srcs {
            if let Some(field_src) = fs {
                field_src.to_tokens(ts, srcs);
            }
        }

        // Now handle expressions
        for field in &self.instr.fields {
            let InstrField::Physical(field) = field else {
                continue;
            };

            let f_ident = instr_field_ident(&field.name);
            if let Some(expr) = field.expr.as_ref() {
                ts.extend(quote! {
                    let #f_ident: u32 = #expr;
                });
            }
        }

        // ... and that should be everything

        for field in self.instr.fields.iter() {
            if skip_field(field) {
                continue;
            }

            let InstrField::Physical(field) = field else {
                continue;
            };

            let f_ident = instr_field_ident(&field.name);
            match field.mod_ {
                FieldMod::None => (),
                FieldMod::Align(n) => {
                    let n = proc_macro2::Literal::u8_unsuffixed(n);
                    ts.extend(quote! {
                        assert!(#f_ident % #n == 0);
                    });
                }
                FieldMod::Minus(n) => {
                    let n = proc_macro2::Literal::u8_unsuffixed(n);
                    ts.extend(quote! {
                        assert!(#f_ident >= #n);
                        let #f_ident = #f_ident - #n;
                    });
                }
                FieldMod::Shr(n) => {
                    let n = proc_macro2::Literal::u8_unsuffixed(n);
                    ts.extend(quote! {
                        assert!(#f_ident % (1 << #n) == 0);
                        let #f_ident = #f_ident >> #n;
                    });
                }
            }

            let start_bit = usize::from(field.bits.start);
            let end_bit = usize::from(field.bits.end);
            ts.extend(quote! {
                b.set_field(#start_bit..#end_bit, #f_ident);
            });
        }
    }
}

struct InstrEnc {
    name: String,
    srcs: InstrEncSources,
    variants: BTreeMap<Option<String>, Vec<InstrEncVariant>>,
}

impl InstrEnc {
    fn new(name: &str) -> InstrEnc {
        InstrEnc {
            name: name.to_string(),
            srcs: InstrEncSources::new(name),
            variants: Default::default(),
        }
    }

    fn add_variant(&mut self, instr: Instr) {
        let variant_name = instr.variant.clone();
        if variant_name.is_some() {
            self.srcs.enable_variants();
        }

        let enc_instr = InstrEncVariant::new(instr, &mut self.srcs);
        self.variants
            .entry(variant_name)
            .or_default()
            .push(enc_instr);
    }
}

impl ToTokens for InstrEnc {
    fn to_tokens(&self, ts: &mut TokenStream2) {
        let mut v_idents = Vec::new();
        if let Some(ve_ident) = &self.srcs.variants {
            let mut vars_ts = TokenStream2::new();
            let mut is_data_type = true;
            let mut dt_cases_ts = TokenStream2::new();
            for v_name in self.variants.keys() {
                let v_camel_name = to_camel_case(v_name.as_ref().unwrap());
                let v_ident = Ident::new(&v_camel_name, Span::call_site());
                vars_ts.extend(quote! {
                    #v_ident,
                });

                if v_name.as_ref().is_some_and(|s| is_data_type_name(s)) {
                    dt_cases_ts.extend(quote! {
                        DataType::#v_ident => Ok(#ve_ident::#v_ident),
                    });
                } else {
                    is_data_type = false;
                }
                v_idents.push(v_ident);
            }

            ts.extend(quote! {
                #[derive(Clone, Copy, Hash, PartialEq)]
                pub enum #ve_ident {
                    #vars_ts
                }
            });
            if is_data_type {
                let err = format!("Unsupported {} variant", self.name);
                ts.extend(quote! {
                    impl TryFrom<DataType> for #ve_ident {
                        type Error = &'static str;

                        fn try_from(
                            dt: DataType
                        ) -> Result<#ve_ident, &'static str> {
                            match dt {
                                #dt_cases_ts
                                _ => Err(#err),
                            }
                        }
                    }
                });
            }
            assert_eq!(v_idents.len(), self.variants.len());
        } else {
            assert_eq!(self.variants.len(), 1);
        }
        let v_idents = v_idents;

        // Declare the sources struct

        let s_ident = &self.srcs.ident;
        let mut srcs_ts = TokenStream2::new();
        if let Some(v_ident) = &self.srcs.variants {
            srcs_ts.extend(quote! { pub variant: #v_ident, });
        }
        for src in self.srcs.srcs.values() {
            src.declare(&mut srcs_ts);
        }
        ts.extend(quote! {
            pub struct #s_ident {
                #srcs_ts
            }
        });

        let mut total_bits = 0_u8;
        let mut info_const_decls_ts = TokenStream2::new();
        let mut per_variant_info_ts = TokenStream2::new();
        let mut per_variant_enc_ts = TokenStream2::new();
        for (i, (v_name, per_arch)) in self.variants.iter().enumerate() {
            let mut per_arch_enc_ts = TokenStream2::new();
            let mut per_arch_info_ts = TokenStream2::new();
            for enc in per_arch.iter() {
                if total_bits == 0 {
                    total_bits = enc.instr.total_bits;
                } else {
                    assert_eq!(total_bits, enc.instr.total_bits);
                }

                enc.info.to_tokens(&mut info_const_decls_ts);

                let info_ident = &enc.info.ident;
                let arch_start = enc.instr.arch.start;
                let arch_end = enc.instr.arch.end;
                per_arch_info_ts.extend(quote! {
                    if (#arch_start..#arch_end).contains(&arch) {
                        Some(&#s_ident::#info_ident)
                    } else
                });

                let mut enc_ts = TokenStream2::new();
                enc.to_tokens(&mut enc_ts, &self.srcs);
                per_arch_enc_ts.extend(quote! {
                    if (#arch_start..#arch_end).contains(&arch) {
                        #enc_ts
                    } else
                });
            }

            let mut instr_name = self.name.to_string();
            if let Some(v_name) = v_name {
                instr_name += ".";
                instr_name += v_name;
            }
            let err = format!("{} not supported on this arch", instr_name);

            let per_arch_info_ts = quote! {
                #per_arch_info_ts {
                    None
                }
            };
            let per_arch_enc_ts = quote! {
                #per_arch_enc_ts {
                    return Err(#err.into());
                }
            };

            if let Some(v_ident) = v_idents.get(i) {
                let ve_ident = self.srcs.variants.as_ref().unwrap();
                per_variant_info_ts.extend(quote! {
                    #ve_ident::#v_ident => {
                        #per_arch_info_ts
                    }
                });
                per_variant_enc_ts.extend(quote! {
                    #ve_ident::#v_ident => {
                        #per_arch_enc_ts
                    }
                });
            } else {
                assert!(per_variant_info_ts.is_empty());
                assert!(per_variant_enc_ts.is_empty());
                per_variant_info_ts = per_arch_info_ts;
                per_variant_enc_ts = per_arch_enc_ts;
            }
        }

        let variant_type_ts = if let Some(v) = &self.srcs.variants {
            quote! { #v }
        } else {
            quote! { () }
        };

        let get_info_impl_ts = if self.srcs.variants.is_some() {
            quote! {
                match variant {
                    #per_variant_info_ts
                }
            }
        } else {
            per_variant_info_ts
        };

        let try_encode_impl_ts = if self.srcs.variants.is_some() {
            quote! {
                match self.variant {
                    #per_variant_enc_ts
                }
            }
        } else {
            per_variant_enc_ts
        };

        assert!(total_bits % 32 == 0);
        let total_words = usize::from(total_bits / 32);

        ts.extend(quote! {
            impl #s_ident {
                #info_const_decls_ts
            }

            impl Instruction<SrcSwizzle, DstLanes> for #s_ident {
                type Variant = #variant_type_ts;

                fn get_info_for_variant(
                    variant: Self::Variant,
                    arch: u8,
                ) -> Option<&'static InstructionInfo> {
                    #get_info_impl_ts
                }
            }

            impl TryEncode for #s_ident {
                type Encoded = [u32; #total_words];
                type Error = EncodeError;

                fn try_encode(
                    self,
                    arch: u8,
                ) -> Result<[u32; #total_words], EncodeError> {
                    let mut encoded_words = [0_u32; #total_words];
                    let mut b = BitMutView::new(&mut encoded_words);
                    #try_encode_impl_ts
                    Ok(encoded_words)
                }
            }
        });
    }
}

pub fn gen_encoder(
    xml_file: &str,
    arch: std::ops::Range<u8>,
) -> Result<TokenStream2> {
    let mut isa = ISA::from_xml_file(std::fs::File::open(xml_file)?, arch)?;

    let mut ts = quote! {
        use crate::isa::*;
        use crate::bitview::*;
        use crate::data_type::DataType;
        use compiler::bitset::ConstBitSet;
        use compiler::enum_as_u8::EnumAsU8;

        pub type InstructionInfo = super::InstructionInfo<SrcSwizzle, DstLanes>;
        pub type InstructionSrcInfo = super::InstructionSrcInfo<SrcSwizzle>;
        pub type InstructionDstInfo = super::InstructionDstInfo<DstLanes>;
        pub type EncodedSrc = super::EncodedSrc<SrcSwizzle>;
        pub type EncodedDst = super::EncodedDst<DstLanes>;
        pub type SrRead = super::SrRead<SrcSwizzle>;
        pub type SrWrite = super::SrWrite<DstLanes>;
    };
    declare_expr_helpers(&mut ts);

    isa.enums
        .add_meta_enum(
            "src_swizzle",
            SRC_SWIZZLE_ENUMS.iter().cloned(),
            ["h01", "b0123"],
        )
        .expect("Failed to create src_swizzle meta-enum");
    isa.enums
        .add_meta_enum("dst_lanes", DST_LANES_ENUMS.iter().cloned(), [])
        .expect("Failed to create dst_lanes meta-enum");
    isa.enums
        .add_meta_enum(
            "round",
            [
                "round_m",
                "round_extended_m",
                "ldexp_round_m",
                "hadd_round_m",
                "fma_rscale_round_m",
            ],
            [],
        )
        .expect("Failed to create round meta-enum");

    isa.enums
        .add_meta_enum(
            "update_mode",
            [
                "update_mode_m",
                "update_mode_none_m",
                "update_mode_special_m",
            ],
            [],
        )
        .expect("Failed to create update_mode meta-enum");

    isa.enums
        .add_meta_enum(
            "sample_position",
            ["sample_position_m", "sample_position_none_m"],
            [],
        )
        .expect("Failed to create sample_position meta-enum");

    isa.enums.declare(&mut ts, true);

    let mut instrs: BTreeMap<_, InstrEnc> = Default::default();
    for i in isa.instrs {
        instrs
            .entry(i.name.to_string())
            .or_insert_with(|| InstrEnc::new(&i.name))
            .add_variant(i)
    }

    for (_, i) in instrs {
        i.to_tokens(&mut ts);
    }

    Ok(ts)
}
