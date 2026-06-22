// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::isa::xml::XmlElement;
use crate::isa::*;
use proc_macro2::{Ident, Span};
use std::rc::Rc;

impl XmlElement {
    fn get_bit_range(&self) -> Result<Range<u8>> {
        let pos = self
            .get_u8_attr("pos")
            .ok_or(err("Instruction field has no pos attribute"))?;
        let width = self
            .get_u8_attr("width")
            .ok_or(err("Instruction field has no width attribute"))?;
        Ok(pos..(pos + width))
    }
}

#[derive(Clone)]
pub enum FieldType {
    Enum(Rc<Enum>),
    PcRelOffsetSigned,
    PcRelOffsetUnsigned,
    Source,
    Source64,
    Int(u8),
    Uint(u8),
}

impl FieldType {
    fn from_name(type_: &str, bits: u8, enums: &EnumSet) -> Result<FieldType> {
        if let Some(e) = enums.get_enum(type_) {
            return Ok(FieldType::Enum(e.clone()));
        }

        match type_ {
            "pc_rel_label_signed" => Ok(FieldType::PcRelOffsetSigned),
            "pc_rel_label_unsigned" => Ok(FieldType::PcRelOffsetUnsigned),
            "SourceEncoding" => Ok(FieldType::Source),
            "SourceEncoding64" => Ok(FieldType::Source64),
            "float" | "V2F16" | "V2I16" | "V4I8" => {
                if bits != 32 {
                    return Err(err(
                        "Vector and float immediates should be 32-bit",
                    ));
                }
                Ok(FieldType::Uint(32))
            }
            "int" => Ok(FieldType::Int(bits)),
            "uint" => Ok(FieldType::Uint(bits)),
            _ => Err(err("Unknown field type")),
        }
    }

    pub fn is_enum(&self, name: &str) -> bool {
        if let FieldType::Enum(e) = self {
            e.name == name
        } else {
            false
        }
    }
}

pub struct FieldRestrict {
    pub values: Vec<EnumLiteral>,
}

impl FieldRestrict {
    fn from_xml_attr(
        attr: &str,
        type_: Option<&FieldType>,
    ) -> Result<Rc<FieldRestrict>> {
        let Some(FieldType::Enum(e)) = type_ else {
            return Err(err("restrict= requires an enum type"));
        };

        let mut values = Vec::new();
        for v_name in attr.split(' ') {
            if v_name.trim().is_empty() {
                continue;
            }

            let Some(v) = e.get_value(v_name) else {
                return Err(err("Invalid enum value in restrict"));
            };
            values.push(EnumLiteral::new(e, v));
        }
        Ok(Rc::new(FieldRestrict { values }))
    }
}

impl ToTokens for FieldRestrict {
    fn to_tokens(&self, ts: &mut TokenStream2) {
        let mut values_ts = TokenStream2::new();
        for i in &self.values {
            values_ts.extend(quote! { #i, });
        }
        ts.extend(quote! { [#values_ts] });
    }
}

pub struct VirtualField {
    pub name: String,
    pub ident: Ident,
    pub type_: FieldType,
    pub restrict: Option<Rc<FieldRestrict>>,
    pub expr: Box<Expr>,
}

impl VirtualField {
    fn from_xml(
        xml: XmlElement,
        _arch: Range<u8>,
        enums: &EnumSet,
    ) -> Result<VirtualField> {
        assert_eq!(xml.name.local_name, "virtual");

        let name = xml
            .attrs
            .get("name")
            .ok_or(err("Instruction virtual has no name"))?
            .to_string();
        let snake_name = to_snake_case(&name);
        let ident = Ident::new(&snake_name, Span::call_site());

        let type_name = xml
            .attrs
            .get("type")
            .ok_or(err("Instruction virtual has no type"))?;
        let type_ = FieldType::from_name(type_name, 32, enums)?;

        let restrict = if let Some(res_attr) = xml.attrs.get("restrict") {
            Some(FieldRestrict::from_xml_attr(res_attr, Some(&type_))?)
        } else {
            None
        };

        let expr = if let Some(exact) = xml.attrs.get("exact") {
            Box::new(Expr::literal(Some(type_name), exact, enums)?)
        } else {
            let mut children = xml.children;
            if children.len() != 1 {
                return Err(err("Virtual fields must have a single expr"));
            }
            let child = children.pop().unwrap();
            Box::new(Expr::from_xml(child, enums)?)
        };

        Ok(VirtualField {
            name,
            ident,
            type_,
            restrict,
            expr,
        })
    }
}

pub enum FieldMod {
    None,
    Align(u8),
    Shr(u8),
    Minus(u8),
}

impl FieldMod {
    fn args_as_u8(a: &str) -> Result<u8> {
        let a = a.strip_prefix("(").ok_or(err("Invalid modifier"))?;
        let a = a.strip_suffix(")").ok_or(err("Invalid modifier"))?;
        u8::from_str_radix(a, 10).map_err(|_| err("Invalid modifier"))
    }

    fn from_attr(attr: &str) -> Result<FieldMod> {
        if let Some(a) = attr.strip_prefix("align") {
            Ok(FieldMod::Align(FieldMod::args_as_u8(a)?))
        } else if let Some(a) = attr.strip_prefix("minus") {
            Ok(FieldMod::Minus(FieldMod::args_as_u8(a)?))
        } else if let Some(a) = attr.strip_prefix("shr") {
            Ok(FieldMod::Shr(FieldMod::args_as_u8(a)?))
        } else {
            Err(err("Invalid modifier"))
        }
    }

    pub fn extra_bits(&self) -> u8 {
        match self {
            FieldMod::None | FieldMod::Align(_) => 0,
            FieldMod::Minus(_) => 1,
            FieldMod::Shr(n) => *n,
        }
    }
}

pub struct PhysicalField {
    pub name: String,
    pub ident: Ident,
    pub bits: Range<u8>,
    pub mod_: FieldMod,
    pub type_: Option<FieldType>,
    pub restrict: Option<Rc<FieldRestrict>>,
    pub expr: Option<Box<Expr>>,
}

impl PhysicalField {
    fn from_xml(
        xml: XmlElement,
        _arch: Range<u8>,
        enums: &EnumSet,
    ) -> Result<PhysicalField> {
        assert_eq!(xml.name.local_name, "field");

        let name = xml
            .attrs
            .get("name")
            .ok_or(err("Instruction virtual has no name"))?
            .to_string();
        let snake_name = to_snake_case(&name);
        let ident = Ident::new(&snake_name, Span::call_site());
        let bits = xml.get_bit_range()?;

        let mod_ = if let Some(attr) = xml.attrs.get("modifier") {
            FieldMod::from_attr(attr)?
        } else {
            FieldMod::None
        };

        let type_name = xml.attrs.get("type");
        let type_ = match type_name {
            Some(name) => Some(FieldType::from_name(
                name,
                u8::try_from(bits.len()).unwrap() + mod_.extra_bits(),
                enums,
            )?),
            None => None,
        };

        let restrict = if let Some(res_attr) = xml.attrs.get("restrict") {
            Some(FieldRestrict::from_xml_attr(res_attr, type_.as_ref())?)
        } else {
            None
        };

        let mut expr = if let Some(exact) = xml.attrs.get("exact") {
            Some(Box::new(Expr::literal(
                type_name.map(String::as_str),
                exact,
                enums,
            )?))
        } else {
            None
        };

        for child in xml.children {
            match child.name.local_name.as_str() {
                "expression" => {
                    let e = Box::new(Expr::from_xml(child, enums)?);
                    if expr.replace(e).is_some() {
                        return Err(err(
                            "Instruction field has multiple expressions",
                        ));
                    }
                }
                _ => (),
            }
        }

        if type_.is_none() && expr.is_none() {
            return Err(err(
                "Field must have at least one of type, exact, or expression",
            ));
        }

        Ok(PhysicalField {
            name,
            ident,
            bits,
            mod_,
            type_,
            restrict,
            expr,
        })
    }
}

pub struct ReservedField {
    pub bits: Range<u8>,
    pub zero: bool,
}

impl ReservedField {
    fn from_xml(xml: XmlElement, _arch: Range<u8>) -> Result<ReservedField> {
        assert_eq!(xml.name.local_name, "reserved");

        let type_ = xml
            .attrs
            .get("type")
            .ok_or(err("Instruction virtual has no type"))?;
        let zero = match type_.as_str() {
            "zero" => true,
            "ignore" => false,
            _ => return Err(err("Unknown reserved bits type")),
        };

        Ok(ReservedField {
            bits: xml.get_bit_range()?,
            zero,
        })
    }
}

pub enum InstrField {
    Virtual(VirtualField),
    Physical(PhysicalField),
    Reserved(ReservedField),
}

pub struct Instr {
    pub name: String,
    pub arch: Range<u8>,
    pub variant: Option<String>,
    pub fields: Vec<InstrField>,
    pub total_bits: u8,
}

pub fn instr_field_ident(name: &str) -> Ident {
    let snake_name = to_snake_case(name);
    Ident::new(&snake_name, Span::call_site())
}

impl Instr {
    pub(super) fn from_xml(
        xml: XmlElement,
        arch: Range<u8>,
        enums: &EnumSet,
    ) -> Result<Instr> {
        assert_eq!(xml.name.local_name, "instruction");

        let name = xml
            .attrs
            .get("name")
            .ok_or(err("Enum has no name"))?
            .to_string();

        let mut i = Instr {
            name,
            arch: xml.get_arch(arch.clone()),
            variant: xml.attrs.get("variant").cloned(),
            fields: Default::default(),
            total_bits: 0,
        };

        for child in xml.children.into_iter() {
            let arch = i.arch.clone();
            match child.name.local_name.as_str() {
                "field" => {
                    let f = PhysicalField::from_xml(child, arch, enums)?;
                    i.total_bits = i.total_bits.max(f.bits.end);
                    i.fields.push(InstrField::Physical(f));
                }
                "virtual" => {
                    let f = VirtualField::from_xml(child, arch, enums)?;
                    i.fields.push(InstrField::Virtual(f));
                }
                "reserved" => {
                    let f = ReservedField::from_xml(child, arch)?;
                    i.total_bits = i.total_bits.max(f.bits.end);
                    i.fields.push(InstrField::Reserved(f));
                }
                _ => (),
            }
        }

        Ok(i)
    }

    pub fn get_named_field(&self, name: &str) -> Option<&InstrField> {
        for f in &self.fields {
            let f_name = match f {
                InstrField::Virtual(f) => &f.name,
                InstrField::Physical(f) => &f.name,
                InstrField::Reserved(_) => continue,
            };
            if f_name == name {
                return Some(f);
            }
        }
        None
    }

    pub fn has_field_named(&self, name: &str) -> bool {
        self.get_named_field(name).is_some()
    }
}
