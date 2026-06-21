// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

pub mod encoder;
pub mod enums;
pub mod expr;
pub mod instr;
mod xml;

use proc_macro2::TokenStream as TokenStream2;
use quote::ToTokens;
use std::ops::Range;
use xml::XmlElement;

pub use enums::*;
pub use expr::*;
pub use instr::*;

#[macro_export]
macro_rules! ident {
    ($($args:expr),*) => {{
        let names = format!($($args),*);
        Ident::new(&names, Span::call_site())
    }};
}

mod result {
    #[allow(dead_code)]
    #[derive(Debug)]
    enum ErrorKind {
        XmlError(::xml::reader::Error),
        IsaError(&'static str),
    }

    #[allow(dead_code)]
    #[derive(Debug)]
    pub struct Error {
        kind: ErrorKind,
    }

    impl From<::xml::reader::Error> for Error {
        fn from(err: ::xml::reader::Error) -> Error {
            Error {
                kind: ErrorKind::XmlError(err),
            }
        }
    }

    impl From<std::io::Error> for Error {
        fn from(err: std::io::Error) -> Error {
            let kind = ::xml::reader::ErrorKind::Io(err);
            ::xml::reader::Error::from(kind).into()
        }
    }

    impl From<&'static str> for Error {
        fn from(s: &'static str) -> Error {
            Error {
                kind: ErrorKind::IsaError(s),
            }
        }
    }

    pub fn err(s: &'static str) -> Error {
        s.into()
    }

    pub type Result<T> = std::result::Result<T, Error>;
}

pub use result::{Error, Result, err};

pub fn is_data_type_name(s: &str) -> bool {
    let mut s = s.as_bytes();
    if s.len() < 2 {
        return false;
    }
    if s[0].to_ascii_lowercase() == b'v' {
        if s[1] != b'2' && s[1] != b'4' {
            return false;
        }
        s = &s[2..];
    }

    // We've taken care of any V2 or V4, check for a float or integer type
    if s.len() < 2 {
        return false;
    }
    if !b"fisu".contains(&s[0].to_ascii_lowercase()) {
        return false;
    }
    let mut bits = 0_u32;
    for b in &s[1..] {
        if !b.is_ascii_digit() {
            return false;
        }
        bits = bits * 10 + u32::from(b - b'0');
    }
    bits >= 8
        && (bits.is_power_of_two()
            || (bits % 3 == 0 && (bits / 3).is_power_of_two()))
}

pub(self) fn to_camel_case(s: &str) -> String {
    if is_data_type_name(s) {
        return s.to_uppercase();
    }

    let mut out = String::new();
    let mut next_upper = true;
    for c in s.chars() {
        if c == '_' {
            next_upper = true;
        } else if next_upper {
            out.extend(c.to_uppercase());
            next_upper = false;
        } else {
            out.extend(c.to_lowercase());
        }
    }
    out
}

pub(self) fn to_snake_case(s: &str) -> String {
    if is_data_type_name(s) {
        return s.to_lowercase();
    }

    let mut out = String::new();
    for c in s.chars() {
        if c.is_uppercase() {
            out.push('_');
            out.extend(c.to_lowercase());
        } else {
            out.push(c);
        }
    }
    out
}

pub type ArchRange = std::ops::Range<u8>;

#[derive(Clone, Copy, Default, PartialEq)]
pub struct ArchSet {
    bits: u32,
}

impl ArchSet {
    pub fn new() -> ArchSet {
        Default::default()
    }

    pub fn is_empty(&self) -> bool {
        self.bits == 0
    }

    pub fn contains(&self, arch: u8) -> bool {
        assert!(arch < 32);
        ((self.bits >> arch) & 1) != 0
    }

    pub fn contains_range(&self, arch: Range<u8>) -> bool {
        self.contains_set(arch.into())
    }

    pub fn contains_set(&self, other: ArchSet) -> bool {
        other.bits & !self.bits == 0
    }

    pub fn first(&self) -> Option<u8> {
        let first = self.bits.trailing_zeros();
        if first < 32 { Some(first as u8) } else { None }
    }

    pub fn insert(&mut self, arch: u8) -> bool {
        let exists = self.contains(arch);
        self.bits |= 1 << arch;
        exists
    }

    pub fn insert_range(&mut self, arch: Range<u8>) {
        assert!(arch.end <= 32);
        if !arch.is_empty() {
            let mask = (!0_u32) >> (32 - arch.len());
            self.bits |= mask << arch.start;
        }
    }
}

impl ToTokens for ArchSet {
    fn to_tokens(&self, tokens: &mut TokenStream2) {
        let bits = self.bits;
        tokens.extend(quote! {
            ArchSet { bits: #bits }
        });
    }
}

impl From<Range<u8>> for ArchSet {
    fn from(range: Range<u8>) -> Self {
        let mut set = ArchSet::new();
        set.insert_range(range);
        set
    }
}

impl std::ops::BitOrAssign<ArchSet> for ArchSet {
    fn bitor_assign(&mut self, rhs: ArchSet) {
        self.bits |= rhs.bits;
    }
}

pub struct ISA {
    pub arch: Range<u8>,
    pub enums: EnumSet,
    pub instrs: Vec<Instr>,
}

impl ISA {
    fn from_xml(xml: XmlElement, arch: Range<u8>) -> Result<ISA> {
        assert_eq!(xml.name.local_name, "mali-isa");

        // Process enums first since we need those for everything else
        let mut enums = EnumSet::new();
        let mut children = Vec::new();
        for child in xml.children.into_iter() {
            if child.name.local_name == "enum" {
                enums.add_xml_enum(child, arch.clone())?;
            } else {
                children.push(child);
            }
        }

        let mut instrs = Vec::new();
        for child in children {
            match child.name.local_name.as_str() {
                "instruction" => {
                    let i = Instr::from_xml(child, arch.clone(), &enums)?;
                    if !i.arch.is_empty() {
                        instrs.push(i);
                    }
                }
                _ => (),
            }
        }

        Ok(ISA {
            arch,
            enums,
            instrs,
        })
    }

    pub fn from_xml_file(file: std::fs::File, arch: Range<u8>) -> Result<ISA> {
        ISA::from_xml(xml::XmlElement::from_xml_file(file)?, arch)
    }
}
