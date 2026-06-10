// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use proc_macro::TokenStream;
use proc_macro2::Span;
use proc_macro2::TokenStream as TokenStream2;
use syn::*;

fn is_valid_data_type(comps: u8, num_type: char, bits: u8) -> bool {
    if bits == 64 {
        return comps == 1 && ['I', 'S', 'U'].contains(&num_type);
    }

    if bits == 8 && num_type == 'F' {
        return false;
    }

    if comps * bits > 32 {
        return false;
    }

    true
}

fn data_type_str(comps: u8, num_type: char, bits: u8) -> String {
    if comps == 1 {
        format!("{num_type}{bits}")
    } else {
        format!("V{comps}{num_type}{bits}")
    }
}

fn data_type_ident(comps: u8, num_type: char, bits: u8) -> Ident {
    Ident::new(&data_type_str(comps, num_type, bits), Span::call_site())
}

fn widen_call_for_asm_swizzle(
    asm_swizzle: &Ident,
    data_type: TokenStream2,
) -> TokenStream2 {
    let swz_name = asm_swizzle.to_string();
    let mut iter = swz_name.chars();
    let widen_type = iter.next().unwrap();

    let mut widen_ident = format!("widen_{widen_type}");
    let mut widen_args = data_type;
    for c in iter {
        // Everything after the first character should be a
        // number
        let i = c.to_digit(10).unwrap() as u8;
        widen_ident += "x";
        widen_args.extend(quote! { , #i });
    }

    let widen_ident = widen_ident.to_lowercase();
    let widen_ident = Ident::new(&widen_ident, Span::call_site());

    quote! { Swizzle::#widen_ident(#widen_args) }
}

pub fn derive_asm_swizzle_widen(input: TokenStream) -> TokenStream {
    let DeriveInput { ident, data, .. } = parse_macro_input!(input);
    let enum_type = ident;
    let Data::Enum(e) = data else {
        panic!("Not an enum type");
    };

    const NUM_COMPS: [u8; 3] = [1, 2, 4];
    const NUM_TYPES: [char; 4] = ['F', 'I', 'S', 'U'];
    const BIT_SIZES: [u8; 4] = [8, 16, 32, 64];

    let mut from_swizzle_dt_cases = TokenStream2::new();
    let mut swizzle_consts = TokenStream2::new();

    for &dt_comps in &NUM_COMPS {
        for &num_type in &NUM_TYPES {
            for &dt_bits in &BIT_SIZES {
                if !is_valid_data_type(dt_comps, num_type, dt_bits) {
                    continue;
                }

                let (dt_ident, dt_case) = if dt_bits == 8 {
                    // 8-bit types don't do any format conversion so we can
                    // handle them together.  This is good because there are
                    // a LOT of 8-bit types.
                    if num_type != 'I' {
                        continue;
                    }

                    let i = data_type_ident(dt_comps, 'I', dt_bits);
                    let s = data_type_ident(dt_comps, 'S', dt_bits);
                    let u = data_type_ident(dt_comps, 'U', dt_bits);
                    let case =
                        quote! { DataType::#i | DataType::#s | DataType::#u };
                    (i, case)
                } else {
                    let t = data_type_ident(dt_comps, num_type, dt_bits);
                    let case = quote! { DataType::#t };
                    (t, case)
                };

                let mut from_swizzle_cases = quote! {
                    Swizzle::NONE => Some(#enum_type::None),
                };
                for v in &e.variants {
                    if v.ident == "None" {
                        continue;
                    }

                    let v_name = v.ident.to_string();
                    let mut v_iter = v_name.chars();
                    let widen_type = v_iter.next().unwrap();
                    let widen_comps = v_iter.count() as u8;

                    if widen_comps != dt_comps {
                        continue;
                    }

                    let widen_bits = match widen_type {
                        'B' => 8,
                        'H' => 16,
                        'W' => 32,
                        _ => panic!("Invalid widen: {}", v.ident),
                    };

                    // Bytes can't be widened to floats
                    if widen_bits == 8 && num_type == 'F' {
                        continue;
                    }

                    // This is widen, not narrow
                    if widen_bits > dt_bits {
                        continue;
                    }

                    // I types can't be widened
                    if num_type == 'I' && dt_bits != widen_bits {
                        continue;
                    }

                    // 32 and 64-bit can't swizzle unless we're widening
                    if dt_bits >= 32 && dt_bits == widen_bits {
                        continue;
                    }

                    let widen = widen_call_for_asm_swizzle(
                        &v.ident,
                        quote! { DataType::#dt_ident },
                    );

                    let v_ident = &v.ident;
                    let const_ident = Ident::new(
                        &format!("SWIZZLE_{dt_ident}_{v_ident}"),
                        Span::call_site(),
                    );

                    swizzle_consts.extend(quote! {
                        const #const_ident: Swizzle = #widen;
                    });
                    from_swizzle_cases.extend(quote! {
                        Self::#const_ident => Some(#enum_type::#v_ident),
                    });
                }

                from_swizzle_dt_cases.extend(quote! {
                    #dt_case => match swizzle {
                        #from_swizzle_cases
                        _ => None,
                    }
                });
            }
        }
    }

    let mut to_swizzle_cases = TokenStream2::new();
    let mut display_cases = TokenStream2::new();
    for v in &e.variants {
        if v.ident == "None" {
            to_swizzle_cases.extend(quote! {
                #enum_type::None => Swizzle::NONE,
            });
            display_cases.extend(quote! {
                #enum_type::None => Ok(()),
            });
        } else {
            let widen =
                widen_call_for_asm_swizzle(&v.ident, quote! { data_type });

            let v_ident = &v.ident;
            to_swizzle_cases.extend(quote! {
                #enum_type::#v_ident => #widen,
            });

            let v_disp = format!(".{v_ident}").to_lowercase();
            display_cases.extend(quote! {
                #enum_type::#v_ident => write!(f, #v_disp),
            });
        }
    }

    let imp = quote! {
        impl #enum_type {
            #swizzle_consts

            pub fn from_swizzle(
                src_type: DataType,
                swizzle: Swizzle,
            ) -> Option<AsmSwizzleWiden> {
                if swizzle == Swizzle::NONE {
                    Some(#enum_type::None)
                } else {
                    match src_type {
                        #from_swizzle_dt_cases
                        _ => None,
                    }
                }
            }

            pub fn to_swizzle(self, data_type: DataType) -> Swizzle {
                match self {
                    #to_swizzle_cases
                }
            }
        }

        impl std::fmt::Display for #enum_type {
            fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
                match self {
                    #display_cases
                }
            }
        }
    };
    imp.into()
}
