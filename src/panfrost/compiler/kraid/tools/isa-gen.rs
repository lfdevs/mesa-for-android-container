// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

extern crate kraid_proc;

use std::env;

fn main() {
    let args: Vec<String> = env::args().collect();
    let xml_file = &args[1];

    let ts = kraid_proc::isa::encoder::gen_encoder(xml_file, 9..15).unwrap();
    println!("{ts}");
}
