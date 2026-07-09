# Copyright © 2026 Intel Corporation
# SPDX-License-Identifier: MIT

from dataclasses import dataclass
import enum


@enum.unique
class Props(enum.IntFlag):
    NONE = 0
    HAS_JIP = 1 << 0
    HAS_UIP = 1 << 1
    BRANCH_CTRL = 1 << 2
    NO_DST = 1 << 3


HwOpcodeValue = int | None
HwOpcodeInput = int | dict[str, HwOpcodeValue]
HwOpcode = dict[str, HwOpcodeValue]


@dataclass(frozen=True)
class Opcode:
    name: str
    enum_name: str
    format: str
    hw_opcode: HwOpcode
    num_srcs: int
    props: Props = Props.NONE


def normalize_hw_opcode(hw_opcode: HwOpcodeInput) -> HwOpcode:
    if isinstance(hw_opcode, int):
        return {"pre_xe": hw_opcode, "xe": hw_opcode, "xe2": hw_opcode,
                "xe3p": hw_opcode}

    assert hw_opcode.keys() <= {"pre_xe", "xe", "xe2", "xe3p"}
    assert all(value is None or isinstance(value, int)
               for value in hw_opcode.values())
    pre_xe = hw_opcode.get("pre_xe")
    xe = hw_opcode.get("xe", pre_xe)
    xe2 = hw_opcode.get("xe2", xe)
    xe3p = hw_opcode.get("xe3p", xe2)
    return {
        "pre_xe": pre_xe,
        "xe": xe,
        "xe2": xe2,
        "xe3p": xe3p,
    }


_opcodes: list[Opcode] = []


def op(name: str, format: str, hw_opcode: HwOpcodeInput,
       props: Props = Props.NONE, *, num_srcs: int) -> None:
    _opcodes.append(Opcode(
        name=name,
        enum_name=name.upper(),
        format=format,
        hw_opcode=normalize_hw_opcode(hw_opcode),
        props=props,
        num_srcs=num_srcs,
    ))


def basic1(name: str, hw_opcode: HwOpcodeInput,
           props: Props = Props.NONE) -> None:
    op(name, 'GEN_FORMAT_BASIC_ONE_SRC', hw_opcode, props, num_srcs=1)


def basic2(name: str, hw_opcode: HwOpcodeInput,
           props: Props = Props.NONE) -> None:
    op(name, 'GEN_FORMAT_BASIC_TWO_SRC', hw_opcode, props, num_srcs=2)


def basic3(name: str, hw_opcode: HwOpcodeInput,
           props: Props = Props.NONE) -> None:
    op(name, 'GEN_FORMAT_BASIC_THREE_SRC', hw_opcode, props, num_srcs=3)


def branch1(name: str, hw_opcode: HwOpcodeInput,
            props: Props = Props.NONE) -> None:
    branch_props = Props.NO_DST
    if name != 'join':
        branch_props |= Props.BRANCH_CTRL
    op(name, 'GEN_FORMAT_BRANCH_ONE_SRC', hw_opcode, branch_props | props,
       num_srcs=1)


def branch2(name: str, hw_opcode: HwOpcodeInput,
            props: Props = Props.NONE) -> None:
    op(name, 'GEN_FORMAT_BRANCH_TWO_SRC', hw_opcode,
       Props.BRANCH_CTRL | Props.NO_DST | props, num_srcs=2)


op('illegal', 'GEN_FORMAT_ILLEGAL', 0, Props.NO_DST, num_srcs=0)

basic1('bfrev', {"pre_xe": 23, "xe": 119})
basic1('cbit',  77)
basic1('fbh',   75)
basic1('fbl',   76)
basic1('frc',   67)
basic1('lzd',   74)
basic1('mov',   {"pre_xe": 1, "xe": 97})
basic1('not',   {"pre_xe": 4, "xe": 100})
basic1('rndd',  69)
basic1('rnde',  70)
basic1('rndu',  68)
basic1('rndz',  71)
basic1('sync',  {"xe": 1}, Props.NO_DST)
basic1('wait',  48)

basic2('add',   64)
basic2('addc',  78)
basic2('and',   {"pre_xe": 5, "xe": 101})
basic2('asr',   {"pre_xe": 12, "xe": 108})
basic2('avg',   66)
basic2('bfi1',  {"pre_xe": 25, "xe": 121})
basic2('cmp',   {"pre_xe": 16, "xe": 112})
basic2('cmpn',  {"pre_xe": 17, "xe": 113})
basic2('dp2',   {"pre_xe": 87, "xe": None})
basic2('dp3',   {"pre_xe": 86, "xe": None})
basic2('dp4',   {"pre_xe": 84, "xe": None})
basic2('dph',   {"pre_xe": 85, "xe": None})
basic2('line',  {"pre_xe": 89, "xe": None})
basic2('mac',   {"pre_xe": 72, "xe3p": None})
basic2('mach',  {"pre_xe": 73, "xe3p": None})
basic2('macl',  {"xe2": 83, "xe3p": None})
basic2('math',  56)
basic2('movi',  {"pre_xe": 3, "xe": 99})
basic2('mul',   65)
basic2('mullh', {"xe3p": 95})
basic2('or',    {"pre_xe": 6, "xe": 102})
basic2('pln',   {"pre_xe": 90, "xe": None})
basic2('rol',   {"pre_xe": 15, "xe": 111})
basic2('ror',   {"pre_xe": 14, "xe": 110})
basic2('sel',   {"pre_xe": 2, "xe": 98})
basic2('shl',   {"pre_xe": 9, "xe": 105})
basic2('shr',   {"pre_xe": 8, "xe": 104})
basic2('smov',  {"pre_xe": 10, "xe": 106}, Props.NO_DST)
basic2('srnd',  {"xe2": 84})
basic2('subb',  79)
basic2('xor',   {"pre_xe": 7, "xe": 103})

basic3('add3', {"xe": 82})
basic3('bfe',  {"pre_xe": 24, "xe": 120})
basic3('bfi2', {"pre_xe": 26, "xe": 122})
basic3('bfn',  {"xe": 107})
basic3('csel', {"pre_xe": 18, "xe": 114})
basic3('dp4a', {"xe": 88})
basic3('lrp',  {"pre_xe": 92, "xe": None})
basic3('mad',  91)
basic3('madm', 93)

op('dpas', 'GEN_FORMAT_DPAS_THREE_SRC', {"xe": 89}, num_srcs=3)

op('send',   'GEN_FORMAT_SEND', 49, num_srcs=1)
op('sendc',  'GEN_FORMAT_SEND', 50, num_srcs=1)
op('sends',  'GEN_FORMAT_SEND', {"pre_xe": 51, "xe": None}, num_srcs=1)
op('sendsc', 'GEN_FORMAT_SEND', {"pre_xe": 52, "xe": None}, num_srcs=1)

branch1('brd',   33)
branch1('call',  44)
branch1('calla', 43)
branch1('endif', 37, Props.HAS_JIP)
branch1('jmpi',  32)
branch1('join',  47, Props.HAS_JIP)
branch1('ret',   45)
branch1('while', 39, Props.HAS_JIP)

branch2('brc',      35)
branch2('break',    40, Props.HAS_JIP | Props.HAS_UIP)
branch2('continue', 41, Props.HAS_JIP | Props.HAS_UIP)
branch2('else',     36, Props.HAS_JIP | Props.HAS_UIP)
branch2('goto',     46, Props.HAS_JIP | Props.HAS_UIP)
branch2('halt',     42, Props.HAS_JIP | Props.HAS_UIP)
branch2('if',       34, Props.HAS_JIP | Props.HAS_UIP)

op('nop', 'GEN_FORMAT_NOP', {"pre_xe": 126, "xe": 96}, Props.NO_DST, num_srcs=0)

OPCODES = tuple(_opcodes)
OPCODES_BY_ENUM_NAME = {op.enum_name: op for op in OPCODES}


del op
del _opcodes
