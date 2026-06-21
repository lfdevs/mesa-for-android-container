// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use std::collections::HashMap;
use std::ops::Range;
use xml::attribute::OwnedAttribute;
use xml::name::OwnedName;
use xml::reader::{EventReader, XmlEvent};

fn split_str_radix(s: &str) -> (&str, u32) {
    if s.starts_with("0x") || s.starts_with("0X") {
        (&s[2..], 16)
    } else if s.starts_with("0b") || s.starts_with("0B") {
        (&s[2..], 2)
    } else {
        (s, 10)
    }
}

/// A very simpl DOM
pub struct XmlElement {
    pub name: OwnedName,
    pub attrs: HashMap<String, String>,
    pub children: Vec<XmlElement>,
}

impl XmlElement {
    fn from_xml_event(
        name: OwnedName,
        attributes: Vec<OwnedAttribute>,
        events: &mut impl Iterator<Item = xml::reader::Result<XmlEvent>>,
    ) -> xml::reader::Result<XmlElement> {
        let mut elem = XmlElement {
            name,
            attrs: attributes
                .into_iter()
                .map(|a| (a.name.local_name, a.value))
                .collect(),
            children: Default::default(),
        };

        while let Some(event) = events.next() {
            match event? {
                XmlEvent::StartElement {
                    name, attributes, ..
                } => {
                    elem.children.push(XmlElement::from_xml_event(
                        name, attributes, events,
                    )?);
                }
                XmlEvent::EndElement { .. } => return Ok(elem),
                _ => (),
            }
        }

        Err(xml::reader::ErrorKind::UnexpectedEof.into())
    }

    pub fn from_xml_file(
        file: std::fs::File,
    ) -> xml::reader::Result<XmlElement> {
        // Buffering is important for performance
        let file = std::io::BufReader::new(file);
        let mut events = EventReader::new(file).into_iter();

        while let Some(event) = events.next() {
            match event? {
                XmlEvent::StartElement {
                    name, attributes, ..
                } => {
                    return XmlElement::from_xml_event(
                        name,
                        attributes,
                        &mut events,
                    );
                }
                _ => (),
            }
        }

        Err(xml::reader::ErrorKind::UnexpectedEof.into())
    }

    pub fn get_u8_attr(&self, name: &str) -> Option<u8> {
        let s = self.attrs.get(name)?;
        let (s, r) = split_str_radix(s);
        let u = u8::from_str_radix(s, r).unwrap();
        Some(u)
    }

    pub fn get_u32_attr(&self, name: &str) -> Option<u32> {
        let s = self.attrs.get(name)?;
        let (s, r) = split_str_radix(s);
        let u = u32::from_str_radix(s, r).unwrap();
        Some(u)
    }

    pub fn get_bool_attr(&self, name: &str) -> Option<bool> {
        self.get_u8_attr(name).map(|u| u != 0)
    }

    pub fn get_arch(&self, all_arch: Range<u8>) -> Range<u8> {
        let mut range = all_arch;
        if let Some(since) = self.get_u8_attr("since") {
            range.start = range.start.max(since);
        }
        if let Some(until) = self.get_u8_attr("until") {
            range.end = range.end.min(until + 1);
        }
        range
    }
}
