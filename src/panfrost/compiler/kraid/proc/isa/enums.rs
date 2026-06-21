// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::ident;
use crate::isa::xml::XmlElement;
use crate::isa::*;
use proc_macro2::TokenStream as TokenStream2;
use proc_macro2::{Ident, Span};
use std::cell::OnceCell;
use std::collections::{BTreeMap, HashSet, btree_map};
use std::rc::{Rc, Weak};

pub struct EnumValue {
    pub name: String,
    pub ident: Ident,
    pub arch: ArchSet,
    pub canonical: bool,
    pub value: u8,
    pub bit_pattern: Option<u32>,
}

impl EnumValue {
    fn from_xml(xml: XmlElement, arch: Range<u8>) -> Result<EnumValue> {
        assert_eq!(xml.name.local_name, "value");

        let name = xml
            .attrs
            .get("name")
            .ok_or(err("Enum value has no name"))?
            .to_string();
        let camel_name = to_camel_case(&name);
        let ident = Ident::new(&camel_name, Span::call_site());

        Ok(EnumValue {
            name,
            ident,
            arch: xml.get_arch(arch).into(),
            canonical: xml.get_bool_attr("canonical").unwrap_or(true),
            value: xml
                .get_u8_attr("value")
                .ok_or(err("Enum value has no value"))?
                .into(),
            bit_pattern: xml.get_u32_attr("bit_pattern"),
        })
    }

    fn merge(&mut self, other: EnumValue) {
        assert_eq!(self.name, other.name);
        self.arch |= other.arch;
        assert_eq!(self.canonical, other.canonical);
        assert_eq!(self.value, other.value);
    }
}

pub struct Enum {
    pub name: String,
    pub ident: Ident,
    pub arch: ArchSet,
    pub has_none: bool,
    pub is_bool: bool,
    pub is_data_type: bool,
    pub values: BTreeMap<String, EnumValue>,
    meta: OnceCell<Weak<MetaEnum>>,
}

impl Enum {
    pub fn get_value(&self, name: &str) -> Option<&EnumValue> {
        self.values.get(name)
    }

    pub fn get_meta(&self) -> Option<Rc<MetaEnum>> {
        self.meta.get()?.upgrade()
    }

    fn from_xml(xml: xml::XmlElement, arch: Range<u8>) -> Result<Enum> {
        assert_eq!(xml.name.local_name, "enum");

        let name = xml
            .attrs
            .get("name")
            .ok_or(err("Enum has no name"))?
            .to_string();
        let camel_name = to_camel_case(&name);
        let ident = Ident::new(&camel_name, Span::call_site());

        // ls_multi_sr_count_m looks like a data type enum but isn't one.
        // Or at least, our data type enum doesn't have enough integer types
        // to satisfy it.
        let may_be_data_type =
            xml.children.len() > 0 && name != "ls_multi_sr_count_m";

        let mut e = Enum {
            name,
            ident,
            arch: xml.get_arch(arch.clone()).into(),
            has_none: false,
            is_bool: xml.children.len() == 2,
            is_data_type: may_be_data_type,
            values: Default::default(),
            meta: Default::default(),
        };

        for child in xml.children.into_iter() {
            let v = EnumValue::from_xml(child, arch.clone())?;

            if v.name == "none" {
                e.has_none = true;
            }

            // We only treat it as a bool if the two values are "none" and
            // something else, with none = 0.
            if !matches!((v.name.as_str(), &v.value), ("none", 0) | (_, 1)) {
                e.is_bool = false;
            }

            if !is_data_type_name(v.name.as_str()) {
                e.is_data_type = false;
            }

            if !v.arch.is_empty() {
                e.values.insert(v.name.clone(), v);
            }
        }

        Ok(e)
    }

    fn merge(&mut self, other: Enum) {
        assert_eq!(self.name, other.name);
        self.arch |= other.arch;
        self.has_none |= other.has_none;
        self.is_bool &= other.is_bool;
        self.is_data_type &= other.is_data_type;

        for (name, value) in other.values.into_iter() {
            use btree_map::Entry;
            match self.values.entry(name) {
                Entry::Vacant(entry) => {
                    entry.insert(value);
                }
                Entry::Occupied(mut entry) => {
                    entry.get_mut().merge(value);
                }
            }
        }
    }

    pub fn declare(&self, ts: &mut TokenStream2, data_type_casts: bool) {
        let mut unique_values = true;
        let mut all_same_arch = true;
        let mut max_value = 0_u8;
        let mut values = HashSet::new();
        for (_, v) in &self.values {
            if v.arch != self.arch {
                all_same_arch = false;
            }
            if !values.insert(v.value) {
                unique_values = false;
            }
            max_value = max_value.max(v.value);
        }

        let e_ident = &self.ident;

        // Declare the enum

        if unique_values {
            let mut values_ts = TokenStream2::new();
            for EnumValue { ident, value, .. } in self.values.values() {
                values_ts.extend(quote! {
                    #ident = #value,
                });
            }
            ts.extend(quote! {
                #[repr(u8)]
                #[derive(Clone, Copy, Hash, PartialEq)]
                pub enum #e_ident {
                    #values_ts
                }
            });
        } else {
            let mut values_ts = TokenStream2::new();
            for EnumValue { ident, .. } in self.values.values() {
                values_ts.extend(quote! {
                    #ident,
                });
            }
            ts.extend(quote! {
                #[derive(Clone, Copy, Hash, PartialEq)]
                pub enum #e_ident {
                    #values_ts
                }
            });
        }

        // Implement Display

        let mut fmt_cases_ts = TokenStream2::new();
        for EnumValue { ident, name, .. } in self.values.values() {
            fmt_cases_ts.extend(quote! {
                #e_ident::#ident => write!(f, #name),
            });
        }
        ts.extend(quote! {
            impl std::fmt::Display for #e_ident {
                fn fmt(
                    &self,
                    f: &mut std::fmt::Formatter<'_>
                ) -> std::fmt::Result {
                    match self {
                        #fmt_cases_ts
                    }
                }
            }
        });

        if self.has_none {
            ts.extend(quote! {
                impl #e_ident {
                    fn is_none(&self) -> bool {
                        matches!(self, #e_ident::None)
                    }
                }
            });
        }

        if self.is_bool {
            debug_assert!(unique_values);
            ts.extend(quote! {
                impl From<bool> for #e_ident {
                    fn from(b: bool) -> #e_ident {
                        unsafe { std::mem::transmute(b as u8) }
                    }
                }

                impl From<#e_ident> for bool {
                    fn from(e: #e_ident) -> bool {
                        (e as u8) != 0
                    }
                }
            });
        }

        let err = format!("Unsupported {e_ident}");
        if self.is_data_type && data_type_casts {
            let mut cases_ts = TokenStream2::new();
            for EnumValue { ident, .. } in self.values.values() {
                cases_ts.extend(quote! {
                    DataType::#ident => Ok(#e_ident::#ident),
                });
            }
            ts.extend(quote! {
                impl TryFrom<DataType> for #e_ident {
                    type Error = &'static str;

                    fn try_from(
                        dt: DataType
                    ) -> Result<#e_ident, &'static str> {
                        match dt {
                            #cases_ts
                            _ => Err(#err),
                        }
                    }
                }
            });
        }

        if self.name == "ls_multi_sr_count_m" {
            let mut sr_cases_ts = TokenStream2::new();
            for EnumValue { ident, name, .. } in self.values.values() {
                let bits = u16::from_str_radix(&name[1..], 10).unwrap();
                assert!(bits % 32 == 0);
                let regs = u8::try_from(bits / 32).unwrap();
                sr_cases_ts.extend(quote! {
                    #regs => Ok(#e_ident::#ident),
                });
            }
            ts.extend(quote! {
                impl #e_ident {
                    fn from_sr_count(
                        regs: u8
                    ) -> Result<#e_ident, &'static str> {
                        match regs {
                            #sr_cases_ts
                            _ => Err("Invalid ls_multi_sr_count_m"),
                        }
                    }
                }
            });
        }

        if self.name == "small_constant_t" {
            let mut name_cases_ts = TokenStream2::new();
            let mut bit_pattern_cases_ts = TokenStream2::new();
            for v in self.values.values() {
                let v_name = &v.name;
                let v_ident = &v.ident;
                let bit_pattern = v.bit_pattern.unwrap();
                name_cases_ts.extend(quote! {
                    #e_ident::#v_ident => #v_name,
                });
                bit_pattern_cases_ts.extend(quote! {
                    #e_ident::#v_ident => #bit_pattern,
                });
            }
            let table_len = max_value + 1;
            ts.extend(quote! {
                impl SmallConstantTable for #e_ident {
                    const TABLE_LEN: u8 = #table_len;

                    fn name(&self) -> &'static str {
                        match self {
                            #name_cases_ts
                        }
                    }

                    fn bit_pattern(&self) -> u32 {
                        match self {
                            #bit_pattern_cases_ts
                        }
                    }
                }
            });
        }

        // Add a per-value arch array which we'll use for TryEncode/Decode

        if unique_values {
            if all_same_arch {
                let mut values_ts = TokenStream2::new();
                for EnumValue { ident, .. } in self.values.values() {
                    values_ts.extend(quote! {
                        #e_ident::#ident as u8,
                    });
                }

                let set_size = usize::from(max_value + 1).div_ceil(32);
                ts.extend(quote! {
                    impl #e_ident {
                        const VALUES: ConstBitSet<#set_size, u8> =
                            ConstBitSet::<#set_size, u8>::from_array([
                            #values_ts
                        ]);
                    }
                });
            } else {
                let mut arch_arr = Vec::new();
                for EnumValue { value, arch, .. } in self.values.values() {
                    let min_len = usize::from(value + 1);
                    if arch_arr.len() < min_len {
                        arch_arr.resize(min_len, ArchSet::new());
                    }
                    arch_arr[usize::from(*value)] = *arch;
                }
                let arch_arr_len = arch_arr.len();

                let mut arch_arr_ts = TokenStream2::new();
                for arch in arch_arr {
                    arch_arr_ts.extend(quote! { #arch, });
                }

                ts.extend(quote! {
                    impl #e_ident {
                        const ARCH_PER_VALUE: [ArchSet; #arch_arr_len] = [
                            #arch_arr_ts
                        ];
                    }
                });
            }
        }

        // Implement [Try]Encode

        if all_same_arch {
            let encode_impl = if unique_values {
                quote! {
                    self as u8
                }
            } else {
                let mut cases_ts = TokenStream2::new();
                for EnumValue { ident, value, .. } in self.values.values() {
                    cases_ts.extend(quote! {
                        #e_ident::#ident => #value,
                    });
                }
                quote! {
                    match self {
                        #cases_ts
                    }
                }
            };

            ts.extend(quote! {
                impl Encode for #e_ident {
                    type Encoded = u8;

                    fn encode(self) -> u8 {
                        #encode_impl
                    }
                }
            });
        } else {
            let try_encode_impl = if unique_values {
                quote! {
                    let value = self as u8;
                    if #e_ident::ARCH_PER_VALUE[usize::from(value)]
                        .contains(arch)
                    {
                        Ok(value)
                    } else {
                        Err(#err.into())
                    }
                }
            } else {
                let mut cases_ts = TokenStream2::new();
                for EnumValue {
                    ident, value, arch, ..
                } in self.values.values()
                {
                    cases_ts.extend(quote! {
                        #e_ident::#ident => {
                            if #arch.contains(arch) {
                                Ok(#value)
                            } else {
                                Err(#err.into())
                            }
                        }
                    });
                }
                quote! {
                    match self {
                        #cases_ts
                    }
                }
            };

            ts.extend(quote! {
                impl TryEncode for #e_ident {
                    type Encoded = u8;
                    type Error = EncodeError;

                    fn try_encode(
                        self,
                        arch: u8,
                    ) -> std::result::Result<u8, EncodeError> {
                        #try_encode_impl
                    }
                }
            });
        }

        // Implement TryDecode

        let try_decode_impl = if unique_values {
            if all_same_arch {
                quote! {
                    if #e_ident::VALUES.contains(value) {
                        Ok(unsafe { std::mem::transmute(value) })
                    } else {
                        Err(#err.into())
                    }
                }
            } else {
                quote! {
                    if #e_ident::ARCH_PER_VALUE
                        .as_slice()
                        .get(usize::from(value))
                        .is_some_and(|a| a.contains(arch))
                    {
                        Ok(unsafe { std::mem::transmute(value) })
                    } else {
                        Err(#err.into())
                    }
                }
            }
        } else {
            let mut values: BTreeMap<_, Vec<_>> = BTreeMap::new();
            for ev in self.values.values() {
                values.entry(ev.value).or_default().push(ev);
            }

            let mut cases_ts = TokenStream2::new();
            for (value, mut ev_vec) in values {
                // Place cannonical values first, then it doesn't really
                // matter. Sort by arch and then name to keep things stable
                ev_vec.sort_by(|a, b| {
                    a.canonical
                        .cmp(&b.canonical)
                        .reverse()
                        .then_with(|| a.arch.first().cmp(&b.arch.first()))
                        .then_with(|| a.name.cmp(&b.name))
                });

                let mut per_arch_cases_ts = TokenStream2::new();
                let mut arch_covered = ArchSet::new();
                for EnumValue { ident, arch, .. } in ev_vec {
                    per_arch_cases_ts.extend(quote! {
                        if ArchSet::contains(&#arch, arch) {
                            Ok(#e_ident::#ident)
                        } else
                    });

                    arch_covered |= *arch;
                    if arch_covered == self.arch {
                        break;
                    }
                }

                cases_ts.extend(quote! {
                    #value => {
                        #per_arch_cases_ts {
                            Err(#err.into())
                        }
                    }
                });
            }

            quote! {
                match value {
                    #cases_ts
                    _ => Err(#err.into()),
                }
            }
        };

        ts.extend(quote! {
            impl TryDecode<u8> for #e_ident {
                type Error = EncodeError;

                fn try_decode(
                    value: u8,
                    arch: u8,
                ) -> std::result::Result<#e_ident, EncodeError> {
                    #try_decode_impl
                }
            }
        });
    }
}

pub struct MetaEnum {
    pub name: String,
    pub ident: Ident,
    pub has_none: bool,
    enums: Vec<Rc<Enum>>,
    none_values: HashSet<String>,
}

impl MetaEnum {
    fn new(
        name: &str,
        enums: Vec<Rc<Enum>>,
        none_values: HashSet<String>,
    ) -> Rc<MetaEnum> {
        let camel_name = to_camel_case(&name);
        let ident = Ident::new(&camel_name, Span::call_site());
        let has_none = enums.iter().find(|e| e.has_none).is_some();
        assert!(has_none || none_values.is_empty());
        let me = MetaEnum {
            name: name.to_string(),
            ident,
            has_none,
            enums,
            none_values,
        };
        Rc::new_cyclic(|weak| {
            for e in &me.enums {
                // We should have checked this before we created the MetaEnum
                e.meta.set(weak.clone()).unwrap();
            }
            me
        })
    }

    pub fn declare(&self, ts: &mut TokenStream2) {
        let mut values = BTreeMap::new();
        for e in &self.enums {
            for v in e.values.values() {
                values.insert(&v.name, &v.ident);
            }
        }

        let me_ident = &self.ident;
        let mut values_ts = TokenStream2::new();
        let mut fmt_cases_ts = TokenStream2::new();
        for (v_name, v_ident) in &values {
            if self.none_values.contains(*v_name) {
                continue;
            }
            values_ts.extend(quote! {
                #v_ident,
            });
            fmt_cases_ts.extend(quote! {
                #me_ident::#v_ident => write!(f, #v_name),
            });
        }

        if self.name == "src_swizzle" || self.name == "dst_lanes" {
            ts.extend(quote! {
                #[repr(u8)]
                #[derive(Clone, Copy, EnumAsU8, Hash, PartialEq)]
                pub enum #me_ident {
                    #values_ts
                }
            });
        } else {
            ts.extend(quote! {
                #[derive(Clone, Copy, Hash, PartialEq)]
                pub enum #me_ident {
                    #values_ts
                }
            });
        }

        ts.extend(quote! {
            impl std::fmt::Display for #me_ident {
                fn fmt(
                    &self,
                    f: &mut std::fmt::Formatter<'_>
                ) -> std::fmt::Result {
                    match self {
                        #fmt_cases_ts
                    }
                }
            }
        });

        if self.has_none {
            ts.extend(quote! {
                impl #me_ident {
                    fn is_none(&self) -> bool {
                        matches!(self, #me_ident::None)
                    }
                }
            });
        }

        for e in &self.enums {
            let e_ident = &e.ident;
            let mut from_cases_ts = TokenStream2::new();
            let mut into_cases_ts = TokenStream2::new();
            for EnumValue { name, ident, .. } in e.values.values() {
                if self.none_values.contains(name) {
                    from_cases_ts.extend(quote! {
                        #e_ident::#ident => #me_ident::None,
                    });
                    into_cases_ts.extend(quote! {
                        #me_ident::None => Ok(#e_ident::#ident),
                    });
                } else {
                    from_cases_ts.extend(quote! {
                        #e_ident::#ident => #me_ident::#ident,
                    });
                    into_cases_ts.extend(quote! {
                        #me_ident::#ident => Ok(#e_ident::#ident),
                    });
                }
            }

            let err = format!("Unsupported {e_ident}");
            ts.extend(quote! {
                impl From<#e_ident> for #me_ident {
                    fn from(e: #e_ident) -> #me_ident {
                        match e {
                            #from_cases_ts
                        }
                    }
                }

                impl TryFrom<#me_ident> for #e_ident {
                    type Error = EncodeError;

                    fn try_from(
                        e: #me_ident
                    ) -> Result<#e_ident, EncodeError> {
                        match e {
                            #into_cases_ts
                            _ => Err(#err.into()),
                        }
                    }
                }
            });
        }
    }
}

#[derive(Clone)]
pub enum EnumType {
    Enum(Rc<Enum>),
    Meta(Rc<MetaEnum>),
}

impl EnumType {
    fn ident(&self) -> &Ident {
        match self {
            EnumType::Enum(e) => &e.ident,
            EnumType::Meta(m) => &m.ident,
        }
    }
}

#[derive(Clone)]
pub struct EnumLiteral {
    pub enum_type: EnumType,
    pub value_name: String,
    pub value_ident: Ident,
}

impl EnumLiteral {
    pub fn new(e: &Rc<Enum>, v: &EnumValue) -> EnumLiteral {
        EnumLiteral {
            enum_type: EnumType::Enum(e.clone()),
            value_name: v.name.clone(),
            value_ident: v.ident.clone(),
        }
    }

    pub fn to_meta(&self) -> Option<EnumLiteral> {
        let e = match &self.enum_type {
            EnumType::Enum(e) => e,
            EnumType::Meta(_) => return Some(self.clone()),
        };
        let m = e.get_meta()?;

        let (v_name, v_ident) = if m.none_values.contains(&self.value_name) {
            ("none".to_string(), ident!("None"))
        } else {
            (self.value_name.clone(), self.value_ident.clone())
        };

        Some(EnumLiteral {
            enum_type: EnumType::Meta(m),
            value_name: v_name,
            value_ident: v_ident,
        })
    }
}

impl ToTokens for EnumLiteral {
    fn to_tokens(&self, ts: &mut TokenStream2) {
        let e_ident = self.enum_type.ident();
        let v_ident = &self.value_ident;
        ts.extend(quote! {
            #e_ident::#v_ident
        });
    }
}

#[derive(Default)]
pub struct EnumSet {
    enums: BTreeMap<String, Rc<Enum>>,
    meta_enums: BTreeMap<String, Rc<MetaEnum>>,
}

impl EnumSet {
    pub fn new() -> EnumSet {
        Default::default()
    }

    pub fn get_enum(&self, name: &str) -> Option<&Rc<Enum>> {
        self.enums.get(name)
    }

    pub fn get_meta_enum(&self, name: &str) -> Option<&Rc<MetaEnum>> {
        self.meta_enums.get(name)
    }

    pub fn get_ident(&self, name: &str) -> Option<&Ident> {
        if let Some(me) = self.meta_enums.get(name) {
            Some(&me.ident)
        } else if let Some(e) = self.enums.get(name) {
            Some(&e.ident)
        } else {
            None
        }
    }

    pub(crate) fn add_xml_enum(
        &mut self,
        xml: XmlElement,
        arch: Range<u8>,
    ) -> Result<()> {
        let e = Enum::from_xml(xml, arch)?;
        if !e.arch.is_empty() {
            use std::collections::btree_map::Entry;
            match self.enums.entry(e.name.clone()) {
                Entry::Vacant(entry) => {
                    entry.insert(Rc::new(e));
                }
                Entry::Occupied(mut entry) => {
                    Rc::get_mut(entry.get_mut())
                        .ok_or("Meta enums must be added last")?
                        .merge(e);
                }
            }
        }
        Ok(())
    }

    pub fn add_meta_enum<'a>(
        &mut self,
        name: &str,
        enums: impl IntoIterator<Item = &'a str>,
        none_values: impl IntoIterator<Item = &'a str>,
    ) -> Result<()> {
        if self.enums.contains_key(name) {
            return Err(err("Enum and meta enum cannot have the same name"));
        }

        if self.meta_enums.contains_key(name) {
            return Err(err("Duplicate meta enum name"));
        }

        let mut enum_vec = Vec::new();
        for e_name in enums {
            let e = self.enums.get(e_name).ok_or(err("Unknown enum name"))?;
            if e.meta.get().is_some() {
                return Err(err("Cannot add an enum to two metas"));
            }
            enum_vec.push(e.clone());
        }

        let none_values = none_values.into_iter().map(str::to_string).collect();

        let me = MetaEnum::new(name, enum_vec, none_values);
        self.meta_enums.insert(name.to_string(), me);
        Ok(())
    }

    pub fn declare(&self, ts: &mut TokenStream2, data_type_casts: bool) {
        for e in self.enums.values() {
            e.declare(ts, data_type_casts);
        }

        for me in self.meta_enums.values() {
            me.declare(ts);
        }
    }
}
