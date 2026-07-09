// Copyright © 2023 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use proc_macro::TokenStream;
use proc_macro2::TokenStream as TokenStream2;
use quote::ToTokens;
use syn::*;

fn unbox_type(ty: &Type) -> Option<TokenStream2> {
    let Type::Path(p) = ty else {
        return None;
    };

    let Some(seg) = p.path.segments.last() else {
        panic!("Path cannot be empty");
    };

    if seg.ident == "Box" {
        let PathArguments::AngleBracketed(a) = &seg.arguments else {
            panic!("Box<T> missing generic argument T");
        };

        Some(a.args.to_token_stream())
    } else {
        None
    }
}

pub fn derive_from_variants(input: TokenStream) -> TokenStream {
    let DeriveInput { ident, data, .. } = parse_macro_input!(input);
    let enum_type = ident;

    let mut impls = TokenStream2::new();

    let Data::Enum(e) = data else {
        panic!("FromVariants can only be used on an enum type");
    };

    for v in e.variants {
        let var_ident = v.ident;
        let from_type = match v.fields {
            Fields::Named(_) => {
                panic!("FromVariants does not support named fields")
            }
            Fields::Unnamed(FieldsUnnamed { unnamed, .. }) => unnamed,
            Fields::Unit => continue,
        };

        assert!(
            from_type.len() == 1,
            "FromVariants does not support multiple unnamed fields"
        );
        let from_type = &from_type.first().unwrap().ty;

        impls.extend(quote! {
            impl From<#from_type> for #enum_type {
                fn from (v: #from_type) -> #enum_type {
                    #enum_type::#var_ident(v)
                }
            }
        });

        if let Some(unboxed) = unbox_type(from_type) {
            impls.extend(quote! {
                impl From<#unboxed> for #enum_type {
                    fn from (v: #unboxed) -> #enum_type {
                        #enum_type::#var_ident(v.into())
                    }
                }
            });
        }
    }

    impls.into()
}
