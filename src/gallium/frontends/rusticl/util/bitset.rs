// Copyright 2022 Red Hat.
// SPDX-License-Identifier: MIT

pub fn test_bit(bitset: &[u32], bit: u32) -> bool {
    let idx = bit / 32;
    let test = bit % 32;

    bitset[idx as usize] & (1 << test) != 0
}

#[test]
fn test_test_bit() {
    let data = [0x3254424d, 0xffffffff, 0x00000001];

    assert!(test_bit(&data, 0));
    assert!(!test_bit(&data, 5));
    assert!(test_bit(&data, 64));
    assert!(!test_bit(&data, 65));
}
