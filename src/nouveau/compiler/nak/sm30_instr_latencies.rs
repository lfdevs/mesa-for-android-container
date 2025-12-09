use bitview::{BitMutViewable, BitViewable, SetField};
use rustc_hash::FxHashMap;

use crate::ir::{Instr, InstrDeps, Label, Op, OpNop, Shader};

pub fn instr_latency(_sm: u8, op: &Op, _dst_idx: usize) -> u32 {
    if op.is_fp64() {
        return 20;
    }

    match op {
        Op::Ipa(_) => 15,
        Op::Ld(_) => 24,
        Op::ALd(_) => 24,
        Op::IMul(_) => 15, // This does not apply to imad, right? right???
        Op::ISetP(_) => 13,
        Op::PSetP(_) => 13,
        Op::IAdd2(o) if !o.carry_out.is_none() => 13,
        Op::Tex(_)
        | Op::Tld(_)
        | Op::Tld4(_)
        | Op::Tmml(_)
        | Op::Txd(_)
        | Op::Txq(_) => 17,
        _ => 9,
    }
}

pub fn latency_upper_bound() -> u32 {
    24
}

pub fn instr_exec_latency(sm: u8, op: &Op) -> u32 {
    let is_kepler_a = sm == 30;
    match op {
        Op::Tex(_)
        | Op::Tld(_)
        | Op::Tld4(_)
        | Op::Tmml(_)
        | Op::Txd(_)
        | Op::Txq(_) => 17,
        Op::MemBar(_) => 16,
        Op::Cont(_) | Op::Brk(_) if is_kepler_a => 5,
        Op::Exit(_) => 15,
        _ => 1,
    }
}

fn calc_instr_sched(prev_op: Option<&Op>, op: &Op, deps: &InstrDeps) -> u8 {
    // Kepler is the first generation to lift scoreboarding from the
    // hardware into the compiler. For each instruction we encode
    // the delay but not all the other information necessary for newer
    // architectures.
    // The hardware still checks for data-hazard and, if present, it
    // will delay the instruction by 32 cycles.
    match op {
        Op::TexDepBar(_) => 0xc2,
        Op::Sync(_) => 0x00, // Wait 16 cycles
        _ => {
            // TODO: when we support dual-issue this should check for
            // both previous ops
            let base = match prev_op {
                Some(Op::ASt(_)) => 0x40,
                _ => 0x20,
            };

            let delay = deps.delay;
            debug_assert!(delay >= 1 && delay <= 32);
            base | (delay - 1)
        }
    }

    // 0x00: wait for 16 cycles
    // 0x04: dual-issue with next instruction
    // 0xc2: if TEXBAR
    // 0x20 | 0x40: suspend for N+1 cycles (N = bitmask 0x1f)
    //              0x40 only if prev_op is attribute store
    // Unsure:
    // 0x80: global memory bit
    //
    // TODO:
    // - Dual issue (0x04)
    // - Functional Unit tracking
}

pub trait KeplerInstructionEncoder {
    /// Encode the instruction and push it into the "encoded" vec
    fn encode_instr(
        &self,
        instr: &Instr,
        labels: &FxHashMap<Label, usize>,
        encoded: &mut Vec<u32>,
    );

    /// Prepare the scheduling instruction opcode-field and return a
    /// subset where the actual scheduling information will be written
    fn prepare_sched_instr<'a>(
        &self,
        sched_instr: &'a mut [u32; 2],
    ) -> impl BitMutViewable + 'a;
}

/// Helper function that encodes shaders for both KeplerA and KeplerB.
/// Difference in the encoders are handled by KeplerInstructionEncoder.
pub fn encode_kepler_shader<E>(encoder: &E, s: &Shader<'_>) -> Vec<u32>
where
    E: KeplerInstructionEncoder,
{
    const INSTR_LEN_BYTES: usize = 8;
    assert!(s.functions.len() == 1);
    let func = &s.functions[0];

    // --- Compute label addresses ---
    // We need a schedule instruction every 7 instructions, these don't
    // define jump boundaries so we can have multible blocks in the same
    // 7-instr group.
    let mut ip = 0_usize;
    let mut labels = FxHashMap::default();
    for b in &func.blocks {
        let num_sched = (ip / 7) + 1;
        labels.insert(b.label, (ip + num_sched) * INSTR_LEN_BYTES);
        ip += b.instrs.len();
    }

    // --- Real encoding ---
    // Create an instruction iterator and iterate it in chunks of 7.
    // fill the last chunk with a nop (it should never be executed).
    let mut instr_iter = func
        .blocks
        .iter()
        .flat_map(|b| b.instrs.iter().map(|x| &*x))
        .peekable();
    let mut filling_instr = Instr {
        pred: true.into(),
        op: Op::Nop(OpNop { label: None }),
        deps: InstrDeps::new(),
    };
    filling_instr.deps.set_delay(1);
    let mut sched_chunk_gen = || {
        if instr_iter.peek().is_none() {
            return None;
        }
        Some([0; 7].map(|_| instr_iter.next().unwrap_or(&filling_instr)))
    };

    let mut encoded = Vec::new();
    let mut prev_op = None;
    while let Some(sched_chunk) = sched_chunk_gen() {
        let sched_i = encoded.len();

        let mut sched_instr = [0u32; 2];
        encoded.extend(&sched_instr[..]); // Push now, will edit later
        let mut bv = encoder.prepare_sched_instr(&mut sched_instr);
        // There should be 8 bits for each instr in a scheduling block
        debug_assert!(bv.bits() == 8 * 7);

        for (i, instr) in sched_chunk.iter().enumerate() {
            encoder.encode_instr(&instr, &labels, &mut encoded);

            let sched = calc_instr_sched(prev_op, &instr.op, &instr.deps);
            bv.set_field(i * 8..(i + 1) * 8, sched);
            prev_op = Some(&instr.op);
        }

        drop(bv);
        encoded[sched_i] = sched_instr[0];
        encoded[sched_i + 1] = sched_instr[1];
    }

    encoded
}
