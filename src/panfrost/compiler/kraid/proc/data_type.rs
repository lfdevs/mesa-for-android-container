// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use proc_macro::TokenStream;
use proc_macro2::TokenStream as TokenStream2;
use syn::*;

fn str_to_num(s: &str) -> u8 {
    if s == "N" {
        0
    } else {
        let n = u8::from_str_radix(s, 10).unwrap();
        assert!(n > 0);
        n
    }
}

fn parse_type_name(name: &str) -> (u8, char, u8) {
    if name == "None" {
        (0, 'X', 0)
    } else {
        let mut chars = name.chars().peekable();
        let mut comps = 1;
        if chars.peek() == Some(&'V') {
            chars.next(); // Take the V
            comps = str_to_num(&chars.next().unwrap().to_string());
        }
        let num_type = chars.next().unwrap();
        let bits = str_to_num(&String::from_iter(chars));
        (comps, num_type, bits)
    }
}

fn abrev_to_numeric_type(t: char) -> TokenStream2 {
    match t {
        'A' => quote! { Some(NumericType::Auto) },
        'F' => quote! { Some(NumericType::Float) },
        'I' => quote! { Some(NumericType::Integer) },
        'U' => quote! { Some(NumericType::UnsignedInteger) },
        'S' => quote! { Some(NumericType::SignedInteger) },
        'X' => quote! { None },
        _ => panic!("Invalid numeric type abbreviation"),
    }
}

pub fn derive_data_type(input: TokenStream) -> TokenStream {
    let DeriveInput { ident, data, .. } = parse_macro_input!(input);
    let enum_type = ident;
    let Data::Enum(e) = data else {
        panic!("Not an enum type");
    };

    let mut to_cases = TokenStream2::new();
    let mut from_cases = TokenStream2::new();
    let mut fmt_cases = TokenStream2::new();

    for v in e.variants {
        let name = v.ident.to_string();
        if name == "SR" {
            to_cases.extend(quote! {
                #enum_type::SR => (0, Some(NumericType::Auto), 32),
            });
            // from_cases is intentionally omitted
            fmt_cases.extend(quote! {
                #enum_type::SR => write!(f, "sr"),
            });
            continue;
        }

        let (comps, num_type, bits) = parse_type_name(&name);

        // We need an actual ident for num_type
        let num_type = abrev_to_numeric_type(num_type);

        let variant = v.ident.clone();
        to_cases.extend(quote! {
            #enum_type::#variant => (#comps, #num_type, #bits),
        });

        let variant = v.ident.clone();
        from_cases.extend(quote! {
            (#comps, #num_type, #bits) => #enum_type::#variant,
        });

        let variant = v.ident.clone();
        let fmt_name = name.to_lowercase().to_string();
        fmt_cases.extend(quote! {
            #enum_type::#variant => write!(f, #fmt_name),
        });
    }

    let mut impls = TokenStream2::new();

    impls.extend(quote! {
        impl #enum_type {
            #[inline]
            const fn from_pieces(
                comps: u8,
                num_type: Option<NumericType>,
                bits: u8
            ) -> #enum_type {
                match (comps, num_type, bits) {
                    #from_cases
                    _ => panic!("Invalid DataType"),
                }
            }

            #[inline]
            const fn to_pieces(self) -> (u8, Option<NumericType>, u8) {
                match self {
                    #to_cases
                }
            }
        }

        impl std::fmt::Display for #enum_type {
            fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
                match self {
                    #fmt_cases
                }
            }
        }
    });

    impls.into()
}
