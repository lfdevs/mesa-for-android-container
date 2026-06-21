// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::isa::xml::XmlElement;
use crate::isa::*;
use proc_macro2::Ident;
use proc_macro2::TokenStream as TokenStream2;

pub struct FieldIdent {
    pub name: String,
    pub ident: Ident,
}

impl ToTokens for FieldIdent {
    fn to_tokens(&self, ts: &mut TokenStream2) {
        self.ident.to_tokens(ts)
    }
}

pub enum Expr {
    Ident(FieldIdent),
    Enum(EnumLiteral),
    Uint(u32),
    If(Vec<Expr>),
    BitwiseAnd([Box<Expr>; 2]),
    BitwiseOr([Box<Expr>; 2]),
    BitwiseXor([Box<Expr>; 2]),
    Equal([Box<Expr>; 2]),
    LessThan([Box<Expr>; 2]),
    LessEqual([Box<Expr>; 2]),
    NotEqual([Box<Expr>; 2]),
    LogicalAnd([Box<Expr>; 2]),
    Inside(Vec<Expr>),
    Add([Box<Expr>; 2]),
    Sub([Box<Expr>; 2]),
    RShift([Box<Expr>; 2]),
    LShift([Box<Expr>; 2]),
    CountOnes(Box<Expr>),
    Mask(Box<Expr>),
    Slice([Box<Expr>; 3]),
}

impl Expr {
    pub(super) fn literal(
        type_: Option<&str>,
        value: &str,
        enums: &EnumSet,
    ) -> Result<Expr> {
        if let Some(e) = type_.and_then(|t| enums.get_enum(t)) {
            if let Some(v) = e.get_value(value) {
                return Ok(Expr::Enum(EnumLiteral::new(e, v)));
            }
        }

        if let Ok(v) = u32::from_str_radix(value, 10) {
            Ok(Expr::Uint(v))
        } else {
            Err(err("Unknown expression literal"))
        }
    }

    pub(super) fn from_xml(xml: XmlElement, enums: &EnumSet) -> Result<Expr> {
        assert_eq!(xml.name.local_name, "expression");

        if let Some(op) = xml.attrs.get("operator") {
            let mut operands = Vec::new();
            for c in xml.children {
                operands.push(Expr::from_xml(c, enums)?);
            }

            // Check the number of arguments now so we can assume it later
            let op_err = "Invalid number of expression children";
            match op.as_str() {
                "if" => {
                    if operands.len() < 3 || operands.len() % 2 != 1 {
                        return Err(op_err.into());
                    }
                    Ok(Expr::If(operands))
                }
                "bitwise_and" | "bitwise_or" | "add" => {
                    let y = Box::new(operands.pop().ok_or(op_err)?);
                    let x = Box::new(operands.pop().ok_or(op_err)?);
                    let mut y = Box::new(match op.as_str() {
                        "bitwise_and" => Expr::BitwiseAnd([x, y]),
                        "bitwise_or" => Expr::BitwiseOr([x, y]),
                        "add" => Expr::Add([x, y]),
                        _ => panic!("Unknown expression operator: {op}"),
                    });
                    while let Some(x) = operands.pop() {
                        let x = Box::new(x);
                        y = Box::new(match op.as_str() {
                            "bitwise_and" => Expr::BitwiseAnd([x, y]),
                            "bitwise_or" => Expr::BitwiseOr([x, y]),
                            "add" => Expr::Add([x, y]),
                            _ => panic!("Unknown expression operator: {op}"),
                        });
                    }
                    Ok(*y)
                }
                "bitwise_xor" | "equal" | "not_equal" | "less_equal"
                | "less_than" | "greater_than" | "greater_equal"
                | "logical_and" | "sub" | "rshift" | "lshift" => {
                    let mut iter = operands.into_iter();
                    let x = Box::new(iter.next().ok_or(op_err)?);
                    let y = Box::new(iter.next().ok_or(op_err)?);
                    if iter.next().is_some() {
                        return Err(op_err.into());
                    }
                    match op.as_str() {
                        "bitwise_xor" => Ok(Expr::BitwiseXor([x, y])),
                        "equal" => Ok(Expr::Equal([x, y])),
                        "greater_equal" => Ok(Expr::LessEqual([y, x])),
                        "greater_than" => Ok(Expr::LessThan([y, x])),
                        "less_equal" => Ok(Expr::LessEqual([x, y])),
                        "less_than" => Ok(Expr::LessThan([x, y])),
                        "not_equal" => Ok(Expr::NotEqual([x, y])),
                        "logical_and" => Ok(Expr::LogicalAnd([x, y])),
                        "sub" => Ok(Expr::Sub([x, y])),
                        "rshift" => Ok(Expr::RShift([x, y])),
                        "lshift" => Ok(Expr::LShift([x, y])),
                        _ => panic!("Unknown expression operator: {op}"),
                    }
                }
                "inside" => {
                    if operands.len() < 2 {
                        return Err(op_err.into());
                    }
                    for i in 1..operands.len() {
                        if !matches!(operands[i], Expr::Enum(_) | Expr::Uint(_),)
                        {
                            return Err(
                                "inside expression operands should be literals"
                                    .into(),
                            );
                        }
                    }
                    Ok(Expr::Inside(operands))
                }
                "countones" | "mask" => {
                    let mut iter = operands.into_iter();
                    let x = Box::new(iter.next().ok_or(op_err)?);
                    if iter.next().is_some() {
                        return Err(op_err.into());
                    }
                    match op.as_str() {
                        "countones" => Ok(Expr::CountOnes(x)),
                        "mask" => Ok(Expr::Mask(x)),
                        _ => panic!("Unknown expression operator: {op}"),
                    }
                }
                "slice" => {
                    let mut iter = operands.into_iter();
                    let x = Box::new(iter.next().ok_or(op_err)?);
                    let y = Box::new(iter.next().ok_or(op_err)?);
                    let z = Box::new(iter.next().ok_or(op_err)?);
                    if iter.next().is_some() {
                        return Err(op_err.into());
                    }
                    Ok(Expr::Slice([x, y, z]))
                }
                _ => Err("Unknown expression".into()),
            }
        } else if let Some(name) = xml.attrs.get("identifier") {
            Ok(Expr::Ident(FieldIdent {
                name: name.to_string(),
                ident: instr_field_ident(name),
            }))
        } else if let Some(literal) = xml.attrs.get("literal") {
            let type_ = xml
                .attrs
                .get("type")
                .or_else(|| xml.attrs.get("enum"))
                .ok_or(err("A literal must have a type or an enum"))?;

            Expr::literal(Some(type_), literal, enums)
        } else {
            Err(err("Unknown expression type"))
        }
    }

    pub fn as_enum(&self) -> Option<&EnumLiteral> {
        match self {
            Expr::Enum(lit) => Some(lit),
            _ => None,
        }
    }
}

pub fn declare_expr_helpers(ts: &mut TokenStream2) {
    ts.extend(quote! {
        fn expr_mask_u32(bits: u32) -> u32 {
            if bits < 32 {
                (1_u32 << bits) - 1
            } else {
                !0_u32
            }
        }

        fn expr_slice_u32(x: u32, high_bit: u32, low_bit: u32) -> u32 {
            (x >> low_bit) & expr_mask_u32(high_bit + 1 - low_bit)
        }
    });
}

impl ToTokens for Expr {
    fn to_tokens(&self, ts: &mut TokenStream2) {
        ts.extend(match self {
            Expr::Ident(ident) => {
                quote! { u32::from(#ident) }
            }
            Expr::Enum(lit) => {
                quote! {
                    u32::from(#lit.try_encode(arch)?)
                }
            }
            Expr::Uint(u) => quote! { #u },
            Expr::If(args) => {
                let mut iter = args.iter();
                let c = iter.next().unwrap();
                let v = iter.next().unwrap();
                let mut val_ts = quote! { if #c { #v } };
                loop {
                    let c = iter.next().unwrap();
                    if let Some(v) = iter.next() {
                        val_ts.extend(quote! { else if #c { #v } });
                    } else {
                        val_ts.extend(quote! { else { #c } });
                        break;
                    }
                }
                val_ts
            }
            Expr::BitwiseAnd([x, y]) => quote! { (#x) & (#y) },
            Expr::BitwiseOr([x, y]) => quote! { (#x) | (#y) },
            Expr::BitwiseXor([x, y]) => quote! { (#x) ^ (#y) },
            Expr::Equal([x, y]) => quote! { (#x) == (#y) },
            Expr::LessThan([x, y]) => quote! { (#x) < (#y) },
            Expr::LessEqual([x, y]) => quote! { (#x) <= (#y) },
            Expr::NotEqual([x, y]) => quote! { (#x) != (#y) },
            Expr::LogicalAnd([x, y]) => quote! { (#x) && (#y) },
            Expr::Add([x, y]) => quote! { (#x) + (#y) },
            Expr::Sub([x, y]) => quote! { (#x) - (#y) },
            Expr::RShift([x, y]) => quote! { (#x) >> (#y) },
            Expr::LShift([x, y]) => quote! { (#x) << (#y) },
            Expr::Inside(args) => {
                let mut iter = args.iter();
                let x = iter.next().unwrap();
                let v = iter.next().unwrap();
                let mut vals_ts = quote! { #v };
                for v in iter {
                    vals_ts.extend(quote! { , #v });
                }
                quote! { [#vals_ts].contains(&(#x)) }
            }
            Expr::CountOnes(x) => quote! { (#x).count_ones() },
            Expr::Mask(x) => quote! { expr_mask_u32(#x) },
            Expr::Slice([v, h, l]) => quote! { expr_slice_u32(#v, #h, #l) },
        });
    }
}
