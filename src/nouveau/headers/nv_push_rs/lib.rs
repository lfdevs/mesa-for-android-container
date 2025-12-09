// Copyright © 2024 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use nvidia_headers::ArrayMthd;
use nvidia_headers::Mthd;

pub const MAX_MTHD_LEN: u16 = 0x1fff;
pub const MAX_MTHD_ADDR: u16 = 0x7fff;

fn class_to_subc(class: u16) -> u8 {
    match class & 0xff {
        0x97 => 0,
        0xc0 => 1,
        0x39 => 2,
        0x2d => 3,
        0xb5 => 4,
        0xb0 => 4,
        _ => panic!("Invalid class: {class}"),
    }
}

#[repr(u8)]
enum MthdType {
    /// Each dword increments the address by one
    NInc = 1,
    /// The first dword increments the address by one
    OneInc = 3,
    /// Instead of a length, stores 13 bits of immediate data
    Immd = 4,
    /// The address is not incremented
    ZeroInc = 5,
}

impl TryFrom<u8> for MthdType {
    type Error = &'static str;

    fn try_from(value: u8) -> Result<Self, Self::Error> {
        match value {
            1 => Ok(MthdType::NInc),
            3 => Ok(MthdType::OneInc),
            4 => Ok(MthdType::Immd),
            5 => Ok(MthdType::ZeroInc),
            _ => Err("Invalid method type"),
        }
    }
}

/// A method header.
///
/// Methods start with a header that can encode the `IncType`, the subclass,
/// an address and the size. Optionally, the header can contain an address
/// and an immediate instead.
#[repr(transparent)]
struct MthdHeader(u32);

impl MthdHeader {
    fn from_bits_mut(bits: &mut u32) -> &mut Self {
        // This is always safe beause a reference is always safe to
        // derefence.
        unsafe { &mut *(bits as *mut u32 as *mut MthdHeader) }
    }

    fn into_bits(self) -> u32 {
        self.0
    }

    fn new(mthd_type: MthdType, subc: u8, addr: u16, data: u16) -> Self {
        debug_assert!(subc <= 0x7);
        debug_assert!(addr & 0x3 == 0 && addr <= MAX_MTHD_ADDR);
        debug_assert!(data <= MAX_MTHD_LEN);

        let mthd_type: u32 = (mthd_type as u8).into();
        let subc: u32 = subc.into();
        let addr: u32 = addr.into();
        let data: u32 = data.into();

        Self((mthd_type << 29) | (data << 16) | (subc << 13) | (addr >> 2))
    }

    fn new_immd(immd: u16, subc: u8, addr: u16) -> Self {
        Self::new(MthdType::Immd, subc, addr, immd)
    }

    fn mthd_type(&self) -> MthdType {
        ((self.0 >> 29) as u8).try_into().unwrap()
    }

    fn set_mthd_type(&mut self, mthd_type: MthdType) {
        self.0 &= 0x1fffffff;
        self.0 |= (mthd_type as u8 as u32) << 29;
    }

    fn subc(&self) -> u8 {
        ((self.0 >> 13) & 0x7) as u8
    }

    fn addr(&self) -> u16 {
        ((self.0 & 0x1fff) << 2) as u16
    }

    fn len(&self) -> u16 {
        debug_assert!(!matches!(self.mthd_type(), MthdType::Immd));
        ((self.0 >> 16) & 0x1fff) as u16
    }

    fn set_len(&mut self, len: u16) {
        debug_assert!(len <= MAX_MTHD_LEN);
        self.0 &= 0xe000ffff;
        self.0 |= u32::from(len) << 16;
    }

    fn add_len(&mut self, count: u16) {
        let new_len = self.len() + count;
        self.set_len(new_len);
    }
}

pub struct Push {
    /// The internal memory. Has to be uploaded to a BO through flush().
    mem: Vec<u32>,
    /// Last DW that is an incrementing type or usize::MAX
    last_inc: usize,
}

impl Push {
    /// Instantiates a new push buffer.
    pub fn new() -> Self {
        Self {
            mem: Vec::new(),
            last_inc: usize::MAX,
        }
    }

    fn push_mthd_bits(&mut self, subc: u8, addr: u16, bits: u32) {
        let current_len = self.mem.len();
        if let Some(last) = self.mem.get_mut(self.last_inc) {
            let last = MthdHeader::from_bits_mut(last);
            debug_assert!(last.len() >= 1);
            debug_assert!(
                self.last_inc + usize::from(last.len()) + 1 == current_len
            );
            if subc == last.subc() {
                match last.mthd_type() {
                    MthdType::NInc => {
                        if addr == last.addr() + last.len() * 4 {
                            last.add_len(1);
                            self.mem.push(bits);
                            return;
                        } else if last.len() == 1 && addr == last.addr() {
                            last.set_mthd_type(MthdType::ZeroInc);
                            last.add_len(1);
                            self.mem.push(bits);
                            return;
                        } else if last.len() == 2 && addr == last.addr() + 4 {
                            last.set_mthd_type(MthdType::OneInc);
                            last.add_len(1);
                            self.mem.push(bits);
                            return;
                        }
                    }
                    MthdType::ZeroInc => {
                        if addr == last.addr() {
                            last.add_len(1);
                            self.mem.push(bits);
                            return;
                        }
                    }
                    MthdType::OneInc => {
                        if addr == last.addr() + 4 {
                            last.add_len(1);
                            self.mem.push(bits);
                            return;
                        }
                    }
                    _ => (),
                }
            }
        }

        // Otherwise, we need a new method header.
        //
        // Methods that use 13bits or lower can be encoded as immediates
        // directly.
        if bits <= 0x1fff {
            self.last_inc = usize::MAX;
            let header = MthdHeader::new_immd(bits as u16, subc, addr);
            self.mem.push(header.into_bits());
        } else {
            self.last_inc = self.mem.len();
            let header = MthdHeader::new(MthdType::NInc, subc, addr, 1);
            self.mem.push(header.into_bits());
            self.mem.push(bits);
        }
    }

    pub fn push_method<M: Mthd>(&mut self, mthd: M) {
        self.push_mthd_bits(class_to_subc(M::CLASS), M::ADDR, mthd.to_bits());
    }

    pub fn push_mthd_0inc<M: Mthd>(&mut self, mthd: M) {
        assert!(mthd.to_bits() == 0);

        self.last_inc = self.mem.len();
        let header = MthdHeader::new(
            MthdType::ZeroInc,
            class_to_subc(M::CLASS),
            M::ADDR,
            0,
        );
        self.mem.push(header.into_bits());
    }

    pub fn push_mthd_1inc<M: Mthd>(&mut self, mthd: M) {
        self.last_inc = self.mem.len();
        let header = MthdHeader::new(
            MthdType::OneInc,
            class_to_subc(M::CLASS),
            M::ADDR,
            0,
        );
        self.mem.push(header.into_bits());
        self.mem.push(mthd.to_bits());
    }

    pub fn push_array_method<M: ArrayMthd>(&mut self, i: usize, mthd: M) {
        self.push_mthd_bits(
            class_to_subc(M::CLASS),
            M::addr(i),
            mthd.to_bits(),
        );
    }

    pub fn push_array_mthd_0inc<M: ArrayMthd>(&mut self, i: usize, mthd: M) {
        assert!(mthd.to_bits() == 0);

        self.last_inc = self.mem.len();
        let header = MthdHeader::new(
            MthdType::ZeroInc,
            class_to_subc(M::CLASS),
            M::addr(i),
            0,
        );
        self.mem.push(header.into_bits());
    }

    pub fn push_array_mthd_1inc<M: ArrayMthd>(&mut self, i: usize, mthd: M) {
        self.last_inc = self.mem.len();
        let header = MthdHeader::new(
            MthdType::OneInc,
            class_to_subc(M::CLASS),
            M::addr(i),
            1,
        );
        self.mem.push(header.into_bits());
        self.mem.push(mthd.to_bits());
    }

    /// Push an array of dwords into the push buffer
    pub fn push_inline_data(&mut self, data: &[u32]) {
        if let Some(last) = self.mem.get_mut(self.last_inc) {
            let last = MthdHeader::from_bits_mut(last);
            last.add_len(data.len().try_into().unwrap());
        } else {
            panic!("Inline data must only be placed after a method header");
        }
        self.mem.extend_from_slice(data);
    }
}

impl Default for Push {
    fn default() -> Self {
        Self::new()
    }
}

impl std::ops::Deref for Push {
    type Target = [u32];

    fn deref(&self) -> &[u32] {
        &self.mem
    }
}
