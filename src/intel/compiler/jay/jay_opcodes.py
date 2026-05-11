# Copyright 2026 Intel Corporation
# SPDX-License-Identifier: MIT

from typing import TYPE_CHECKING
from dataclasses import dataclass
import enum

if TYPE_CHECKING:
    from collections.abc import Mapping


@dataclass
class Opcode:
    name: str
    has_dest: bool
    num_srcs: int
    types: list[str]
    negate: int
    sat: bool
    cmod: bool
    side_effects: bool
    _2src_commutative: bool
    extra_struct: list[tuple[str, str]]


@enum.unique
class Props(enum.IntEnum):
    NEGATE0 = 1 << 0
    NEGATE1 = 1 << 1
    NEGATE2 = 1 << 2
    NEGATE3 = 1 << 3
    SAT = 1 << 4
    CMOD = 1 << 5
    SIDE_EFFECTS = 1 << 6
    COMMUTATIVE = 1 << 7
    NO_DEST_ = 1 << 8
    NEGATE = NEGATE0 | NEGATE1 | NEGATE2 | NEGATE3
    NO_DEST = SIDE_EFFECTS | NO_DEST_


_opcodes: dict[str, Opcode] = {}


def op(name: str, num_srcs: int, types: str | None = None,
       props: int = 0, extra_struct: str | list[str] | None = None) -> None:
    types_ = types.split(' ') if types else ['untyped']

    # We can always negate the predicate.
    negate_mask = (props & Props.NEGATE) | (1 << num_srcs)

    if extra_struct is not None:
        extra_struct_ = [(' '.join(x.split(' ')[0:-1]), x.split(' ')[-1])
                         for x in extra_struct]
    else:
        extra_struct_ = []

    _opcodes[name] = Opcode(name, not bool(props & Props.NO_DEST_),
                            num_srcs, types_, negate_mask,
                            bool(props & Props.SAT), bool(props & Props.CMOD),
                            bool(props & Props.SIDE_EFFECTS),
                            bool(props & Props.COMMUTATIVE),
                            extra_struct_)


op('and', 2, 'u1 u16 u32', Props.NEGATE | Props.CMOD | Props.COMMUTATIVE)
op('or',  2, 'u1 u16 u32', Props.NEGATE | Props.CMOD | Props.COMMUTATIVE)
op('xor', 2, 'u1 u16 u32', Props.NEGATE | Props.CMOD | Props.COMMUTATIVE)

op('add',   2, 'u32 s32 u64 s64 f32 f64 f16 bf16 u16 s16',
   Props.SAT | Props.CMOD | Props.COMMUTATIVE | Props.NEGATE)
op('add3',  3, 'u32 s32 u64 s64 u16 s16', Props.SAT |
   Props.CMOD | Props.COMMUTATIVE | Props.NEGATE)
op('asr',   2, 's32 s64 s16', Props.CMOD | Props.NEGATE0)
op('avg',   2, 's16 s32 u16 u32', Props.NEGATE | Props.CMOD)
op('bfe',   3, 'u32 s32', Props.NEGATE0)
op('bfi1',  2, 'u32')
op('bfi2',  3, 'u32')
op('bfn',   3, 'u32', Props.CMOD, ['uint8_t ctrl'])
op('bfrev', 1, 'u32', Props.NEGATE)
op('cbit',  1, 'u32')
op('cmp',   2, 'u32', Props.NEGATE | Props.CMOD)


# With an 8/16-bit type, `index` specifies the element index of the source
# within the 32-bit word. For example, if src_type == U16 and index == 1, this
# converts the upper 16-bits of the input.
op('cvt', 1, 'u8 s8 u16 s16 u32 s32 u64 s64 f32 f64 f16 bf16',
   Props.NEGATE | Props.SAT | Props.CMOD, [
    'enum jay_type src_type',
    'enum jay_rounding_mode rounding_mode',
    'uint8_t index',
    'uint8_t pad'
])

op('fbh',        1, 'u32 s32')
op('fbl',        1, 'u32')
op('lzd',        1, 'u32')
op('frc',        1, 'f32 f64', Props.NEGATE | Props.CMOD)
op('mad',        3, 'u32 s32 u16 s16 f32 f64 f16 bf16',
   Props.NEGATE | Props.SAT | Props.CMOD | Props.COMMUTATIVE)
op('mac',        3, 'f32', Props.NEGATE | Props.SAT | Props.CMOD |
                           Props.COMMUTATIVE)
op('max',        2, 'u32 s32 u64 s64 u16 s16 f32 f64 f16 bf16',
   Props.NEGATE | Props.SAT | Props.COMMUTATIVE)
op('min',        2, 'u32 s32 u64 s64 u16 s16 f32 f64 f16 bf16',
   Props.NEGATE | Props.SAT | Props.COMMUTATIVE)
op('mov',        1, 'u1 u16 u32 u64', Props.NEGATE0 | Props.CMOD)
op('modifier',   1, 'f32 f64 f16 s16 s32 s64 u16 u32 u64 s8',
   Props.NEGATE | Props.SAT | Props.CMOD)
op('mul',        2, 'u16 s16 f32 f64 f16 bf16',
   Props.NEGATE | Props.SAT | Props.CMOD | Props.COMMUTATIVE)
op('mul_high',   2, 'u32 s32', Props.COMMUTATIVE)
op('mul_32x16',  2, 'u32 s32')
op('mul_32',     2, 'u32 s32', Props.COMMUTATIVE, ['bool high'])
op('sel',        3, 'u32 f32 s32 u1 s16 u16 f16', Props.NEGATE)
op('csel',       3, 'u32 s32 f32', Props.NEGATE)
op('dp4a_uu',    3, 'u32', Props.SAT)
op('dp4a_ss',    3, 's32', Props.SAT)
op('dp4a_su',    3, 's32', Props.SAT)
op('rndd',       1, 'f16 f32 f64', Props.NEGATE | Props.SAT)
op('rndz',       1, 'f16 f32 f64', Props.NEGATE | Props.SAT)
op('rnde',       1, 'f16 f32 f64', Props.NEGATE | Props.SAT)
op('math', 1, 'f16 f32',     Props.NEGATE | Props.SAT, ['enum jay_math op'])

op('rol', 2, 'u32 u64 u16 s16 s32 s64', Props.CMOD)
op('ror', 2, 'u32 u64 u16 s16 s32 s64', Props.CMOD)
op('shl', 2, 'u32 u64 u16 s16 s32 s64', Props.CMOD | Props.NEGATE0)
op('shr', 2, 'u32 u64 u16 s16 s32 s64', Props.CMOD | Props.NEGATE0)

op('quad_swizzle', 1, 'u1 u32', 0, ['enum jay_quad_swizzle swizzle'])
op('sync', 1, 'u32', Props.NO_DEST, ['enum tgl_sync_function op'])
op('schedule_barrier', 0, None, Props.NO_DEST)

for n in ['brd', 'illegal', 'goto', 'join', 'if', 'else',
          'endif', 'while', 'break', 'cont', 'call', 'calla', 'jmpi', 'ret',
          'loop_once']:
    op(n, 0, None, Props.NO_DEST)

op('send', 4, None, Props.SIDE_EFFECTS, [
    'enum brw_sfid sfid',
    'uint8_t sbid',
    'bool eot',
    'bool check_tdr',
    'bool uniform',
    'bool bindless',
    'enum jay_type type_0',
    'enum jay_type type_1',
    'uint8_t ex_mlen',
    'uint32_t ex_desc_imm',
])

op('reloc',   0, 'u32 u64', 0, ['unsigned param', 'unsigned base'])
op('preload', 0, 'u32',     0, ['unsigned reg'])
op('deswizzle', 0, 'u32', Props.NO_DEST, ['unsigned size'])
op('deswizzle_odd', 2, 'f32', 0, ['bool src2_hi'])
op('deswizzle_even', 1, 'f32', 0, ['bool src_hi'])

# Calculating the lane ID requires multiple power-of-two steps each involving
# complex architectural features not modelled in the IR.
op('lane_id_8', 0, 'u16')
op('lane_id_expand', 1, 'u16', 0, ['unsigned width'])

# Sample ID calculation
op('extract_byte_per_8lanes', 2, 'u32')
op('shr_odd_subspans_by_4', 1, 'u16')
op('and_u32_u16', 2, 'u32')

# Pixel coord calculations. expand_quad replicates out the per-2x2 values from
# its source g0.[10...13] and - in the case of SIMD32 - g1.[10...13] into a
# per-lane value. Then offset_packed_pixel_coords adds the appropriate packed
# 2x16-bit offset within each quad, giving 2x16-bit per-lane coordinates.
op('expand_quad', 2, 'u32')
op('offset_packed_pixel_coords', 1, 'u32')
op('extract_layer', 2, 'u32')

# Phi function representations
#
# Unlike in NIR, we represent Phi functions as a pair of opcodes, purely
# for convenience since it makes many things easier to work with.
#
# Phis locially exist along control flow edges between blocks.  PHI_DST
# lives where 𝜙 would traditionally be written, at the point where the new
# value is defined.  A PHI_DST will have a corresponding PHI_SRC in each of
# its predecessor block, representing value coming in along that edge.  This
# ensures that source modifiers, scalar to vector promotion, or other source
# evaluation happens in the predecessor block.
#
# The PHI_SRC refers to the SSA index of the PHI_DST. For example, 'if (..) r3 =
# r1 else r3 = r2 endif' might look
#
#            (following block)  | (then block) | (else block)
#            START B3 <B1 <B2   |  ...         |  ...
#              r3 = 𝜙           | 𝜙3 = r1      | 𝜙3 = r2
#              ...              | END B1       | END B2
#
# Here, PHI_DST defines a new SSA value r3. The PHI_SRC in blocks B1 and B2 each
# indicate that the r3 phi's value is r1 when coming from B1, and r2 when coming
# from B2. This would traditionally be written r3 = 𝜙(r1, r2).
#
# Phis operate on whole 32-bit lane values. Phis are not allowed to mix files.
op('phi_src', 1, 'u1 u32', Props.NO_DEST, ['uint32_t index'])
op('phi_dst', 0, 'u1 u32')

# Output from a unit test to prevent dead code elimination.
op('unit_test', 1, 'u32', Props.NO_DEST)

# Produces a stable indeterminate value. Freeze(Poison) in LLVM parlance.
op('indeterminate', 0, 'u1 u32')

op('not', 1, 'u1 u32', Props.CMOD)
op('cast_canonical_to_flag', 1, 'u1')

op('mov_imm64', 0, 'u64', 0, ['uint64_t imm'])
op('zero_flag', 0, 'u1', Props.NO_DEST, ['unsigned reg'])

# Cross-lane shuffle. src0=data, src1=offset in bytes. Clobbers an address reg.
op('shuffle', 2, 'u1 u32')

# Shuffle with a constant lane index.
op('broadcast_imm', 1, 'u1 u32', 0, ['unsigned lane'])

OPCODES = _opcodes

ENUMS: 'Mapping[str, tuple[str, list[str]]]' = {
    'jay_quad_swizzle': ('JAY_QUAD_SWIZZLE', ['xxxx', 'yyyy', 'zzzz', 'wwww',
                                              'xyxy', 'zwzw', 'xxzz', 'yyww']),
    'jay_rounding_mode': ('JAY', ['round', 'rne', 'ru', 'rd', 'rtz']),
    'jay_math': ('JAY_MATH', ['_', 'inv', 'log', 'exp', 'sqrt', 'rsq', 'sin', 'cos']),
    'brw_sfid': ('BRW_SFID', ['null', 'sampler', 'message_gateway',
                              'render_cache', 'urb', 'bindless_thread_dispatch',
                              'ray_trace_accelerator', 'hdc0',
                              'pixel_interpolator', 'tgm', 'slm', 'ugm']),
    'tgl_sync_function': ('TGL_SYNC', ['nop', 'allrd', 'allwr', 'fence', 'bar', 'host']),
}

# Clean up namespace
del op
del _opcodes
