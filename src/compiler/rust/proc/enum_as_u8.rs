// Copyright © 2023 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use proc_macro::TokenStream;
use proc_macro2::TokenStream as TokenStream2;
use syn::*;

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

    let mut variants = TokenStream2::new();
    for v in e.variants {
        let v_ident = v.ident;
        variants.extend(quote! {
            #ident::#v_ident as u8,
        });
    }

    let ident_s = ident.to_string();
    let try_from_err = format!("Invalid {ident_s} variant.");
    let imp = quote! {
        impl EnumAsU8 for #ident {
            const VARIANTS: compiler::bitset::ConstBitSet<8, u8> =
                compiler::bitset::ConstBitSet::<8, u8>::from_array([#variants]);

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
                if Self::VARIANTS.contains(u) {
                    Ok(unsafe { Self::from_u8_unchecked(u) })
                } else {
                    Err(#try_from_err)
                }
            }
        }
    };
    imp.into()
}
