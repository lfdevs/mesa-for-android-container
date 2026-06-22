// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

extern crate kraid_proc;

use compiler_proc::as_slice::*;
use kraid_proc::*;
use proc_macro::TokenStream;
use std::ops::{Add, Range};
use std::path::PathBuf;
use std::str::FromStr;
use syn::parse::{Parse, ParseStream};
use syn::parse_macro_input;

#[proc_macro_attribute]
pub fn variants(attr: TokenStream, item: TokenStream) -> TokenStream {
    ir::variants(attr, item)
}

#[proc_macro_derive(Opcode, attributes(src_type, dst_type))]
pub fn derive_opcode(input: TokenStream) -> TokenStream {
    let mut s = TokenStream::new();
    s.extend(derive_as_slice(
        input.clone(),
        "Src",
        "src_type",
        "PartialDataType",
    ));
    s.extend(derive_as_slice(
        input.clone(),
        "Dst",
        "dst_type",
        "PartialDataType",
    ));
    s.extend(ir::derive_opcode(input));
    s
}

#[proc_macro_derive(DataType)]
pub fn derive_data_type(input: TokenStream) -> TokenStream {
    data_type::derive_data_type(input)
}

#[proc_macro_derive(AsmSwizzleWiden)]
pub fn derive_asm_swizzle_widen(input: TokenStream) -> TokenStream {
    swizzle::derive_asm_swizzle_widen(input)
}

#[proc_macro_derive(EnumAsU8)]
pub fn derive_enum_as_u8(input: TokenStream) -> TokenStream {
    compiler_proc::enum_as_u8::derive_enum_as_u8(input)
}

#[proc_macro_derive(FromVariants)]
pub fn derive_from_variants(input: TokenStream) -> TokenStream {
    compiler_proc::from_variants::derive_from_variants(input)
}

struct LitRange {
    start: syn::LitInt,
    limits: syn::RangeLimits,
    end: syn::LitInt,
}

impl LitRange {
    fn as_range<N: FromStr + Add<Output = N> + From<u8>>(self) -> Range<N>
    where
        <N as FromStr>::Err: std::fmt::Display,
    {
        let start = self.start.base10_parse().unwrap();
        let end = self.end.base10_parse().unwrap();
        match self.limits {
            syn::RangeLimits::HalfOpen(_) => start..end,
            syn::RangeLimits::Closed(_) => start..(end + N::from(1)),
        }
    }
}

impl Parse for LitRange {
    fn parse(input: ParseStream) -> syn::Result<Self> {
        Ok(LitRange {
            start: input.parse()?,
            limits: input.parse()?,
            end: input.parse()?,
        })
    }
}

struct GenIsaArgs {
    xml: String,
    arch: Range<u8>,
}

impl Parse for GenIsaArgs {
    fn parse(input: ParseStream) -> syn::Result<Self> {
        let xml: syn::LitStr = input.parse()?;
        let _: syn::token::Comma = input.parse()?;
        let arch: LitRange = input.parse()?;

        Ok(GenIsaArgs {
            xml: xml.value(),
            arch: arch.as_range(),
        })
    }
}

struct CfgArgs {
    args: std::env::Args,
}

impl Iterator for CfgArgs {
    type Item = (String, Option<String>);

    fn next(&mut self) -> Option<Self::Item> {
        while let Some(arg) = self.args.next() {
            if arg != "--cfg" {
                continue;
            }

            let arg = self.args.next()?;
            if let Some((key, value)) = arg.split_once("=") {
                let value = value
                    .strip_prefix("\"")
                    .expect("Invalid `--cfg` argument")
                    .strip_suffix("\"")
                    .expect("Invalid `--cfg` argument");
                return Some((key.to_string(), Some(value.to_string())));
            } else {
                return Some((arg.to_string(), None));
            }
        }
        None
    }
}

fn cfg_args() -> CfgArgs {
    let args = std::env::args();
    CfgArgs { args }
}

#[proc_macro]
pub fn gen_isa_encode(item: TokenStream) -> TokenStream {
    let args = parse_macro_input!(item as GenIsaArgs);

    let mut isa_xml_path = None;
    for (key, value) in cfg_args() {
        if key == "kraid_isa_xml_path" {
            isa_xml_path = value;
            break;
        }
    }
    let isa_xml_path = isa_xml_path.expect("kraid_isa_xml_path not specified");

    let mut xml_path = PathBuf::from(isa_xml_path);
    xml_path.push(args.xml);
    let xml_path = xml_path.to_str().unwrap();

    isa::encoder::gen_encoder(xml_path, args.arch)
        .unwrap()
        .into()
}
