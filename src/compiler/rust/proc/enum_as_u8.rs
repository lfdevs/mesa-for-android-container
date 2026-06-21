// Copyright © 2023 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use proc_macro::TokenStream;
use proc_macro2::TokenStream as TokenStream2;
use syn::*;

fn try_get_discriminant(v: &Variant) -> Option<u8> {
    let Expr::Lit(l) = &v.discriminant.as_ref()?.1 else {
        return None;
    };

    let Lit::Int(i) = &l.lit else {
        return None;
    };

    i.base10_parse::<u8>().ok()
}

pub fn derive_enum_as_u8(input: TokenStream) -> TokenStream {
    let DeriveInput {
        attrs, ident, data, ..
    } = parse_macro_input!(input);

    let Data::Enum(e) = data else {
        panic!("EnumAsU8 can only be derived for enum types");
    };

    let mut has_repr_u8 = false;
    for attr in attrs {
        if let Meta::List(ml) = attr.meta {
            if ml.path.is_ident("repr") && format!("{}", ml.tokens) == "u8" {
                has_repr_u8 = true;
                break;
            }
        }
    }

    if !has_repr_u8 {
        panic!("EnumAsU8 can only be derived for enum which are #[repr(u8)]");
    };

    let mut max_desc = 0_u8;
    let mut max_desc_ts = TokenStream2::new();
    let mut variants_ts = TokenStream2::new();
    for v in &e.variants {
        let v_ident = &v.ident;
        if let Some(d) = try_get_discriminant(v) {
            max_desc = max_desc.max(d);
        } else {
            max_desc_ts.extend(quote! {
                if (#ident::#v_ident as u8) > max_desc {
                    max_desc = #ident::#v_ident as u8;
                }
            });
        }
        variants_ts.extend(quote! {
            #ident::#v_ident as u8,
        });
    }

    let var_set_u32s = if max_desc_ts.is_empty() {
        (usize::from(max_desc) + 1).div_ceil(32)
    } else {
        // Worst case
        256 / 32
    };

    let ident_s = ident.to_string();
    let try_from_err = format!("Invalid {ident_s} variant.");
    let imp = quote! {
        impl EnumAsU8 for #ident {
            type VariantSet = compiler::enum_as_u8::U8EnumSet<#ident, #var_set_u32s>;
            const VARIANTS: compiler::enum_as_u8::U8EnumSet<#ident, #var_set_u32s> = {
                unsafe {
                    compiler::enum_as_u8::U8EnumSet::from_u8_array([#variants_ts])
                }
            };
            const MAX_DISCRIMINANT: u8 = {
                let mut max_desc = #max_desc;
                #max_desc_ts
                max_desc
            };

            fn as_u8(self) -> u8 {
                self as u8
            }

            unsafe fn from_u8_unchecked(u: u8) -> Self {
                unsafe { std::mem::transmute(u) }
            }
        }

        impl From<#ident> for u8 {
            fn from(e: #ident) -> Self {
                e.as_u8()
            }
        }

        impl TryFrom<u8> for #ident {
            type Error = &'static str;

            fn try_from(u: u8) -> Result<Self, &'static str> {
                if Self::VARIANTS.contains_u8(u) {
                    Ok(unsafe { Self::from_u8_unchecked(u) })
                } else {
                    Err(#try_from_err)
                }
            }
        }
    };
    imp.into()
}
