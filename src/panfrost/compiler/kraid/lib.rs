// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

mod bitview;
mod builder;
mod compile;
mod data_type;
mod encode_v9;
mod flow;
mod foldable;
#[cfg(test)]
mod hw_tests;
mod ir;
mod isa;
mod legalize_imm;
mod legalize_src_swizzles;
mod legalize_vec_srcs;
mod liveness;
mod lower_copy;
mod lower_mkvec_swz;
mod message_slots;
mod model;
mod nir;
mod ops;
mod opt_copy_prop;
mod opt_dce;
mod parallel_copy;
mod ra;
mod remat_constants;
mod small_constants;
mod ssa_value;
mod swizzle;
mod validate;
mod widen_alu_ops;

mod debug {
    bitflags::bitflags! {
        pub struct DebugFlags: u32 {
            const PRINT = 1 << 0;
        }
    }

    fn get_debug_flags() -> DebugFlags {
        let debug_var = "KRAID_DEBUG";
        let Ok(debug_str) = std::env::var(debug_var) else {
            return DebugFlags::empty();
        };

        let mut flags = DebugFlags::empty();
        for flag in debug_str.split(',') {
            match flag.trim() {
                "print" => flags |= DebugFlags::PRINT,
                unk => eprintln!("Unknown {debug_var} flag \"{}\"", unk),
            }
        }
        flags
    }

    pub struct Debug {
        flags: std::sync::OnceLock<DebugFlags>,
    }

    impl std::ops::Deref for Debug {
        type Target = DebugFlags;

        fn deref(&self) -> &DebugFlags {
            self.flags.get_or_init(get_debug_flags)
        }
    }

    pub static DEBUG: Debug = Debug {
        flags: std::sync::OnceLock::new(),
    };
}
