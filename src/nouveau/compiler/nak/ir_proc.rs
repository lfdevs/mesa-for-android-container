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
    let input2 = input.clone();
    let DeriveInput { ident, data, .. } = parse_macro_input!(input2);

    if let Data::Enum(e) = data {
        let mut as_slice_cases = TokenStream2::new();
        let mut as_mut_slice_cases = TokenStream2::new();
        let mut types_cases = TokenStream2::new();
        for v in e.variants {
            let case = v.ident;
            as_slice_cases.extend(quote! {
                #ident::#case(x) => x.srcs_as_slice(),
            });
            as_mut_slice_cases.extend(quote! {
                #ident::#case(x) => x.srcs_as_mut_slice(),
            });
            types_cases.extend(quote! {
                #ident::#case(x) => x.src_types(),
            });
        }
        quote! {
            impl SrcsAsSlice for #ident {
                fn srcs_as_slice(&self) -> &[Src] {
                    match self {
                        #as_slice_cases
                    }
                }

                fn srcs_as_mut_slice(&mut self) -> &mut [Src] {
                    match self {
                        #as_mut_slice_cases
                    }
                }

                fn src_types(&self) -> SrcTypeList {
                    match self {
                        #types_cases
                    }
                }
            }
        }
        .into()
    } else {
        derive_as_slice(input, "Src", "src_type", "SrcType")
    }
}

#[proc_macro_derive(DstsAsSlice, attributes(dst_type))]
pub fn derive_dsts_as_slice(input: TokenStream) -> TokenStream {
    let input2 = input.clone();
    let DeriveInput { ident, data, .. } = parse_macro_input!(input2);

    if let Data::Enum(e) = data {
        let mut as_slice_cases = TokenStream2::new();
        let mut as_mut_slice_cases = TokenStream2::new();
        let mut types_cases = TokenStream2::new();
        for v in e.variants {
            let case = v.ident;
            as_slice_cases.extend(quote! {
                #ident::#case(x) => x.dsts_as_slice(),
            });
            as_mut_slice_cases.extend(quote! {
                #ident::#case(x) => x.dsts_as_mut_slice(),
            });
            types_cases.extend(quote! {
                #ident::#case(x) => x.dst_types(),
            });
        }
        quote! {
            impl DstsAsSlice for #ident {
                fn dsts_as_slice(&self) -> &[Dst] {
                    match self {
                        #as_slice_cases
                    }
                }

                fn dsts_as_mut_slice(&mut self) -> &mut [Dst] {
                    match self {
                        #as_mut_slice_cases
                    }
                }

                fn dst_types(&self) -> DstTypeList {
                    match self {
                        #types_cases
                    }
                }
            }
        }
        .into()
    } else {
        derive_as_slice(input, "Dst", "dst_type", "DstType")
    }
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

#[proc_macro_derive(FromVariants)]
pub fn derive_from_variants(input: TokenStream) -> TokenStream {
    compiler_proc::from_variants::derive_from_variants(input)
}

#[proc_macro_derive(EnumAsU8)]
pub fn derive_enum_as_u8(input: TokenStream) -> TokenStream {
    compiler_proc::enum_as_u8::derive_enum_as_u8(input)
}
