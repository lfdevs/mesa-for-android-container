// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use proc_macro::TokenStream;
use proc_macro2::TokenStream as TokenStream2;
use quote::ToTokens;
use syn::parse::{Parse, ParseStream};
use syn::punctuated::Punctuated;
use syn::*;

struct VariantsDecoration {
    field: Ident,
    #[allow(dead_code)]
    in_token: token::In,
    #[allow(dead_code)]
    bracket_token: token::Bracket,
    variants: Punctuated<Ident, Token![,]>,
}

impl Parse for VariantsDecoration {
    fn parse(input: ParseStream) -> Result<Self> {
        let variants;
        Ok(VariantsDecoration {
            field: input.parse()?,
            in_token: input.parse()?,
            bracket_token: bracketed!(variants in input),
            variants: variants.parse_terminated(Ident::parse, Token![,])?,
        })
    }
}

pub fn variants(attr: TokenStream, item: TokenStream) -> TokenStream {
    // Clone item since we're not actually modifying it, just appending
    let mut out = item.clone();

    let dec = parse_macro_input!(attr as VariantsDecoration);
    let item = parse_macro_input!(item as ItemStruct);

    let ident = &item.ident;
    let field = &dec.field;

    if item
        .fields
        .iter()
        .find(|f| f.ident.as_ref() == Some(&dec.field))
        .is_none()
    {
        panic!("Struct {ident} has no member {field}");
    }

    let var_count = dec.variants.len();
    let mut variants = TokenStream2::new();
    for v in dec.variants.iter() {
        variants.extend(quote! { DataType::#v, });
    }

    let trait_impl = quote! {
        impl #ident {
            const VARIANTS: [DataType; #var_count] = [#variants];
        }

        impl HasVariants for #ident {
            const VARIANTS: &'static [DataType] = &#ident::VARIANTS;

            fn variant(&self) -> DataType {
                self.#field
            }

            fn set_variant(&mut self, data_type: DataType) {
                assert!(Self::VARIANTS.contains(&data_type));
                self.#field = data_type;
            }
        }
    };

    out.extend(TokenStream::from(trait_impl));
    out
}

fn variant_type(v: &Variant) -> &Type {
    let Fields::Unnamed(f) = &v.fields else {
        panic!("Op variant must have a single unnamed field");
    };

    if f.unnamed.len() != 1 {
        panic!("Op variant must have a single unnamed field");
    }

    &f.unnamed[0].ty
}

fn unbox_type(ty: &Type) -> TokenStream2 {
    let Type::Path(p) = ty else {
        panic!("Op variant needs to be either OpFoo or Box<OpFoo>");
    };

    let Some(seg) = p.path.segments.last() else {
        panic!("Path cannot be empty");
    };

    if seg.ident == "Box" {
        let PathArguments::AngleBracketed(a) = &seg.arguments else {
            panic!("Box<T> missing generic argument T");
        };

        a.args.to_token_stream()
    } else {
        p.to_token_stream()
    }
}

// Natural sort key: zero-pad digit runs so that embedded numbers compare by
// value, and lowercase so the order is case-insensitive (e.g. Clz < Copy < CSel)
fn name_sort_key(s: &str) -> String {
    let mut key = String::new();
    let mut num = String::new();
    for c in s.chars() {
        if c.is_ascii_digit() {
            num.push(c);
        } else {
            if !num.is_empty() {
                key += &format!("{num:0>5}");
                num.clear();
            }
            key.push(c.to_ascii_lowercase());
        }
    }
    key += &format!("{num:0>5}");
    key
}

pub fn derive_opcode(input: TokenStream) -> TokenStream {
    let DeriveInput {
        attrs, ident, data, ..
    } = parse_macro_input!(input);

    let mut has_variants = false;
    for attr in &attrs {
        if let Meta::List(ml) = &attr.meta {
            if ml.path.is_ident("variants") {
                has_variants = true;
                break;
            }
        }
    }

    match data {
        Data::Struct(_) => {
            if has_variants {
                quote! {
                    impl Opcode for #ident {
                        fn variant(&self) -> Option<DataType> {
                            Some(<Self as HasVariants>::variant(self))
                        }

                        fn set_variant(&mut self, variant: DataType) {
                            <Self as HasVariants>::set_variant(self, variant)
                        }

                        fn is_valid_variant(&self, variant: DataType) -> bool {
                            <Self as HasVariants>::is_valid_variant(variant)
                        }
                    }
                }
            } else {
                let set_variant_err = format!("{ident} does not have variants");
                quote! {
                    impl Opcode for #ident {
                        fn variant(&self) -> Option<DataType> {
                            None
                        }

                        fn set_variant(&mut self, _data_type: DataType) {
                            panic!(#set_variant_err);
                        }

                        fn is_valid_variant(&self, _variant: DataType) -> bool {
                            false
                        }
                    }
                }
            }
        }
        Data::Enum(e) => {
            let mut var_cases = TokenStream2::new();
            let mut set_cases = TokenStream2::new();
            let mut val_cases = TokenStream2::new();
            let mut fmt_cases = TokenStream2::new();

            let curr_order: Vec<_> =
                e.variants.iter().map(|e| e.ident.to_string()).collect();
            if !curr_order.is_sorted_by_key(|s| name_sort_key(s)) {
                let mut sorted = curr_order.clone();
                sorted.sort_by_key(|s| name_sort_key(s));
                panic!("Variants must always be sorted:\nsorted:  {sorted:?}\ncurrent: {curr_order:?}");
            }

            for v in e.variants {
                let case = &v.ident;
                let v_type = unbox_type(variant_type(&v));
                var_cases.extend(quote! {
                    #ident::#case(x) => {
                        use std::borrow::Borrow;
                        let b: &#v_type = x.borrow();
                        Opcode::variant(b)
                    }
                });
                set_cases.extend(quote! {
                    #ident::#case(x) => {
                        use std::borrow::BorrowMut;
                        let b: &mut #v_type = x.borrow_mut();
                        Opcode::set_variant(b, variant)
                    }
                });
                val_cases.extend(quote! {
                    #ident::#case(x) => {
                        use std::borrow::Borrow;
                        let b: &#v_type = x.borrow();
                        Opcode::is_valid_variant(b, variant)
                    }
                });
                fmt_cases.extend(quote! {
                    #ident::#case(x) => x.fmt(f),
                });
            }

            quote! {
                impl Opcode for #ident {
                    fn variant(&self) -> Option<DataType> {
                        match self {
                            #var_cases
                        }
                    }

                    fn set_variant(&mut self, variant: DataType) {
                        match self {
                            #set_cases
                        }
                    }

                    fn is_valid_variant(&self, variant: DataType) -> bool {
                        match self {
                            #val_cases
                        }
                    }
                }

                impl fmt::Display for #ident {
                    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                        match self {
                            #fmt_cases
                        }
                    }
                }
            }
        }
        _ => panic!("Not a struct or enum type"),
    }
    .into()
}
