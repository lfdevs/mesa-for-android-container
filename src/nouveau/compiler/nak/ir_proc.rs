// Copyright © 2023 Collabora, Ltd.
// SPDX-License-Identifier: MIT

extern crate proc_macro;
extern crate proc_macro2;
#[macro_use]
extern crate quote;
extern crate syn;

use compiler_proc::as_slice::*;
use proc_macro::TokenStream;
use proc_macro2::TokenStream as TokenStream2;
use syn::*;

#[proc_macro_derive(SrcsAsSlice, attributes(src_type))]
pub fn derive_srcs_as_slice(input: TokenStream) -> TokenStream {
    derive_as_slice(input, "Src", "src_type", "SrcType")
}

#[proc_macro_derive(DstsAsSlice, attributes(dst_type))]
pub fn derive_dsts_as_slice(input: TokenStream) -> TokenStream {
    derive_as_slice(input, "Dst", "dst_type", "DstType")
}

#[proc_macro_derive(DisplayOp)]
pub fn enum_derive_display_op(input: TokenStream) -> TokenStream {
    let DeriveInput { ident, data, .. } = parse_macro_input!(input);

    if let Data::Enum(e) = data {
        let mut fmt_dsts_cases = TokenStream2::new();
        let mut fmt_op_cases = TokenStream2::new();
        for v in e.variants {
            let case = v.ident;
            fmt_dsts_cases.extend(quote! {
                #ident::#case(x) => x.fmt_dsts(f),
            });
            fmt_op_cases.extend(quote! {
                #ident::#case(x) => x.fmt_op(f),
            });
        }
        quote! {
            impl DisplayOp for #ident {
                fn fmt_dsts(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                    match self {
                        #fmt_dsts_cases
                    }
                }

                fn fmt_op(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                    match self {
                        #fmt_op_cases
                    }
                }
            }
        }
        .into()
    } else {
        panic!("Not an enum type");
    }
}

fn into_box_inner_type<'a>(from_type: &'a syn::Type) -> Option<&'a syn::Type> {
    let last = match from_type {
        Type::Path(TypePath { path, .. }) => path.segments.last()?,
        _ => return None,
    };

    if last.ident != "Box" {
        return None;
    }

    let PathArguments::AngleBracketed(AngleBracketedGenericArguments {
        args,
        ..
    }) = &last.arguments
    else {
        panic!("Expected Box<T> (with angle brackets)");
    };

    for arg in args {
        if let GenericArgument::Type(inner_type) = arg {
            return Some(inner_type);
        }
    }
    panic!("Expected Box to use a type argument");
}

#[proc_macro_derive(FromVariants)]
pub fn derive_from_variants(input: TokenStream) -> TokenStream {
    let DeriveInput { ident, data, .. } = parse_macro_input!(input);
    let enum_type = ident;

    let mut impls = TokenStream2::new();

    if let Data::Enum(e) = data {
        for v in e.variants {
            let var_ident = v.ident;
            let from_type = match v.fields {
                Fields::Unnamed(FieldsUnnamed { unnamed, .. }) => unnamed,
                _ => panic!("Expected Op(OpFoo)"),
            };

            assert!(from_type.len() == 1, "Expected Op(OpFoo)");
            let from_type = &from_type.first().unwrap().ty;

            let quote = quote! {
                impl From<#from_type> for #enum_type {
                    fn from (op: #from_type) -> #enum_type {
                        #enum_type::#var_ident(op)
                    }
                }
            };

            impls.extend(quote);

            if let Some(inner_type) = into_box_inner_type(from_type) {
                let quote = quote! {
                    impl From<#inner_type> for #enum_type {
                        fn from(value: #inner_type) -> Self {
                            From::from(Box::new(value))
                        }
                    }
                };

                impls.extend(quote);
            }
        }
    }

    impls.into()
}
