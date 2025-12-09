# Copyright © 2024 Imagination Technologies Ltd.
# SPDX-License-Identifier: MIT

from pco_pygen_common import *

OP_TYPE = enum_type('op_type', [
   'pseudo',
   'hw',
   'hw_direct',
])

MOD_TYPE = enum_type('mod_type', [
   'bool',
   'uint',
   'enum',
])

REF_TYPE = enum_type('ref_type', [
   ('null', '_'),
   ('ssa', '%'),
   ('reg', ''),
   ('idx_reg', ''),
   ('imm', ''),
   ('io', ''),
   ('pred', ''),
   ('drc', 'drc'),
])

FUNC_TYPE = enum_type('func_type', [
   'callable',
   'preamble',
   'entrypoint',
   'phase_change',
])

DTYPE = enum_type('dtype', [
   ('any', ''),
   ('unsigned', 'u'),
   ('signed', 'i'),
   ('float', 'f'),
])

BITS = enum_type('bits', [
   ('1', '1'),
   ('8', '8'),
   ('16', '16'),
   ('32', '32'),
   ('64', '64'),
])

REG_CLASS = enum_type('reg_class', [
   ('virt', '$'),
   ('temp', 'r'),
   ('vtxin', 'vi'),
   ('coeff', 'cf'),
   ('shared', 'sh'),
   ('index', 'idx'),
   ('spec', 'sr'),
   ('intern', 'i'),
   ('const', 'sc'),
   ('pixout', 'po'),
   ('global', 'g'),
   ('slot', 'sl'),
])

IO = enum_type('io', [
   ('s0', 's0'),
   ('s1', 's1'),
   ('s2', 's2'),

   ('s3', 's3'),
   ('s4', 's4'),
   ('s5', 's5'),

   ('w0', 'w0'),
   ('w1', 'w1'),

   ('is0', 'is0'),
   ('is1', 'is1'),
   ('is2', 'is2'),
   ('is3', 'is3'),
   ('is4', 'is4'),
   ('is5', 'is5'),

   ('ft0', 'ft0'),
   ('ft1', 'ft1'),
   ('ft2', 'ft2'),
   ('fte', 'fte'),

   ('ft1_invert', '~ft1'),
   ('ft3', 'ft3'),
   ('ft4', 'ft4'),
   ('ft5', 'ft5'),

   ('ftt', 'ftt'),

   ('cout', 'cout'),
])

PRED = enum_type('pred', [
   ('pe', 'pe'),
   ('p0', 'p0'),

   ('always', 'if(1)'),
   ('p0_true', 'if(p0)'),
   ('never', 'if(0)'),
   ('p0_false', 'if(!p0)'),
])

DRC = enum_type('drc', [
   ('0', '0'),
   ('1', '1'),
   ('pending', '?'),
])

MUTEX_ID = enum_type('mutex_id', [
   'atomic_emu',
   'barrier',
   'rsvd_2',
   'rsvd_3',
   'rsvd_4',
   'rsvd_5',
   'rsvd_6',
   'rsvd_7',
   'rsvd_8',
   'rsvd_9',
   'rsvd_a',
   'rsvd_b',
   'rsvd_c',
   'rsvd_d',
   'rsvd_e',
   'rsvd_f'
])

# Ref mods.
RM_ONEMINUS = ref_mod('oneminus', BaseType.bool)
RM_CLAMP = ref_mod('clamp', BaseType.bool)
RM_ABS = ref_mod('abs', BaseType.bool)
RM_NEG = ref_mod('neg', BaseType.bool)
RM_FLR = ref_mod('flr', BaseType.bool)

RM_ELEM = ref_mod_enum('elem', [
   'e0',
   'e1',
   'e2',
   'e3',
], is_bitset=True)

# Op mods.
OM_EXEC_CND = op_mod_enum('exec_cnd', [
   ('e1_zx', ''),
   ('e1_z1', 'if(p0)'),
   ('ex_zx', '(ignorepe)'),
   ('e1_z0', 'if(!p0)'),
], print_early=True, unset=True)
OM_RPT = op_mod('rpt', BaseType.uint, print_early=True, nzdefault=1, unset=True)
OM_SAT = op_mod('sat', BaseType.bool)
OM_LP = op_mod('lp', BaseType.bool)
OM_SCALE = op_mod('scale', BaseType.bool)
OM_ROUNDZERO = op_mod('roundzero', BaseType.bool)
OM_S = op_mod('s', BaseType.bool)
OM_TST_TYPE_MAIN = op_mod_enum('tst_type_main', [
   'f32',
   'u16',
   's16',
   'u8',
   's8',
   'u32',
   's32',
])
OM_TST_OP_MAIN = op_mod_enum('tst_op_main', [
   ('zero', 'z'),
   ('gzero', 'gz'),
   ('gezero', 'gez'),
   ('carry', 'c'),
   ('equal', 'e'),
   ('greater', 'g'),
   ('gequal', 'ge'),
   ('notequal', 'ne'),
   ('less', 'l'),
   ('lequal', 'le'),
])
OM_TST_OP_BITWISE = op_mod_enum('tst_op_bitwise', [
   ('zero', 'z'),
   ('nonzero', 'nz'),
])
OM_SIGNPOS = op_mod_enum('signpos', [
   'twb',
   'pwb',
   'mtb',
   'ftb',
])
OM_DIM = op_mod_enum('dim', [
   '1d',
   '2d',
   '3d',
])
OM_PROJ = op_mod('proj', BaseType.bool)
OM_FCNORM = op_mod('fcnorm', BaseType.bool)
OM_NNCOORDS = op_mod('nncoords', BaseType.bool)
OM_LOD_MODE = op_mod_enum('lod_mode', [
   ('normal', ''),
   ('bias', 'bias'),
   ('replace', 'replace'),
   ('gradients', 'gradients'),
])
OM_PPLOD = op_mod('pplod', BaseType.bool)
OM_TAO = op_mod('tao', BaseType.bool)
OM_SOO = op_mod('soo', BaseType.bool)
OM_SNO = op_mod('sno', BaseType.bool)
OM_WRT = op_mod('wrt', BaseType.bool)
OM_SB_MODE = op_mod_enum('sb_mode', [
   ('none', ''),
   ('rawdata', 'rawdata'),
   ('coeffs', 'coeffs'),
   ('both', 'both'),
])
OM_ARRAY = op_mod('array', BaseType.bool)
OM_INTEGER = op_mod('integer', BaseType.bool)
OM_SCHEDSWAP = op_mod_enum('schedswap', [
   ('none', ''),
   ('swap', 'schedswap'),
])
OM_F16 = op_mod('f16', BaseType.bool)
OM_TILED = op_mod('tiled', BaseType.bool)
OM_FREEP = op_mod('freep', BaseType.bool)
OM_SM = op_mod('sm', BaseType.bool)
OM_SAVMSK_MODE = op_mod_enum('savmsk_mode', [
   'vm',
   'icm',
   'icmoc',
   'icmi',
   'caxy',
])
OM_ATOM_OP = op_mod_enum('atom_op', [
   'add',
   'sub',
   'xchg',
   'cmpxchg',
   'umin',
   'imin',
   'umax',
   'imax',
   'and',
   'or',
   'xor',
])
OM_MCU_CACHE_MODE_LD = op_mod_enum('mcu_cache_mode_ld', [
   ('normal', ''),
   ('bypass', 'bypass'),
   ('force_line_fill', 'forcelinefill'),
])
OM_MCU_CACHE_MODE_ST = op_mod_enum('mcu_cache_mode_st', [
   ('write_through', 'writethrough'),
   ('write_back', 'writeback'),
   ('lazy_write_back', 'lazywriteback'),
])
OM_BRANCH_CND = op_mod_enum('branch_cnd', [
   ('exec_cond', ''),
   ('allinst', 'allinst'),
   ('anyinst', 'anyinst'),
])
OM_LINK = op_mod('link', BaseType.bool)
OM_PCK_FMT = op_mod_enum('pck_fmt', [
   'u8888',
   's8888',
   'o8888',
   'u1616',
   's1616',
   'o1616',
   'u32',
   's32',
   'u1010102',
   's1010102',
   'u111110',
   's111110',
   'f111110',
   'f16f16',
   'f32',
   'cov',
   'u565u565',
   'd24s8',
   's8d24',
   'f32_mask',
   '2f10f10f10',
   's8888ogl',
   's1616ogl',
   'zero',
   'one',
])
OM_PHASE2END = op_mod('phase2end', BaseType.bool)
OM_ITR_MODE = op_mod_enum('itr_mode', [
   'pixel',
   'sample',
   'centroid',
])
OM_SCHED = op_mod_enum('sched', [
   ('none', ''),
   ('swap', 'schedswap'),
   ('wdf', 'schedwdf'),
])
OM_ATOM = op_mod('atom', BaseType.bool, unset=True)
OM_OLCHK = op_mod('olchk', BaseType.bool, unset=True)
OM_END = op_mod('end', BaseType.bool, unset=True)

OM_LOGIOP = op_mod_enum('logiop', [
   'or',
   'and',
   'xor',
   'nor',
   'nand',
   'xnor',
])

OM_SHIFTOP = op_mod_enum('shiftop', [
   'lsl',
   'shr',
   'rol',
   'cps',
   'asr_twb',
   'asr_pwb',
   'asr_mtb',
   'asr_ftb',
])

OM_CND = op_mod_enum('cnd', [
   ('always', 'if(1)'),
   ('p0_true', 'if(p0)'),
   ('never', 'if(0)'),
   ('p0_false', 'if(!p0)'),
])

OM_FRED_TYPE = op_mod_enum('fred_type', [
   'sin',
   'cos',
])

OM_FRED_PART = op_mod_enum('fred_part', [
   'a',
   'b',
])

OM_MUTEX_OP = op_mod_enum('mutex_op', [
   ('release', 'release'),
   ('release_sleep', 'release.sleep'),
   ('release_wakeup', 'release.wakeup'),
   ('lock', 'lock'),
])

OM_OFFSET_SD = op_mod_enum('offset_sd', [
   'src',
   'dest',
])

OM_IDF = op_mod('idf', BaseType.bool)

# Ops.

OM_ALU = [OM_OLCHK, OM_EXEC_CND, OM_END, OM_ATOM, OM_RPT]
OM_ALU_RPT1 = [OM_OLCHK, OM_EXEC_CND, OM_END, OM_ATOM]
OM_ALU_ATOMEXT = [OM_OLCHK, OM_EXEC_CND, OM_END, OM_RPT]

## Main.
O_FADD = hw_op('fadd', OM_ALU + [OM_SAT], 1, 2, [], [[RM_ABS, RM_NEG, RM_FLR], [RM_ABS]])
O_FMUL = hw_op('fmul', OM_ALU + [OM_SAT], 1, 2, [], [[RM_ABS, RM_NEG, RM_FLR], [RM_ABS]])
O_FMAD = hw_op('fmad', OM_ALU + [OM_SAT, OM_LP], 1, 3, [], [[RM_ABS, RM_NEG], [RM_ABS, RM_NEG], [RM_ABS, RM_NEG, RM_FLR]])
O_FRCP = hw_op('frcp', OM_ALU, 1, 1, [], [[RM_ABS, RM_NEG]])
O_FRSQ = hw_op('frsq', OM_ALU, 1, 1, [], [[RM_ABS, RM_NEG]])
O_FLOG = hw_op('flog', OM_ALU, 1, 1, [], [[RM_ABS, RM_NEG]])
O_FLOGCN = hw_op('flogcn', OM_ALU, 1, 1, [], [[RM_ABS, RM_NEG]])
O_FEXP = hw_op('fexp', OM_ALU, 1, 1, [], [[RM_ABS, RM_NEG]])
O_FRED = hw_op('fred', OM_ALU + [OM_FRED_TYPE, OM_FRED_PART], 3, 3, [], [[RM_ABS, RM_NEG]])
O_FSINC = hw_op('fsinc', OM_ALU, 2, 1)
O_MBYP = hw_op('mbyp', OM_ALU, 1, 1, [], [[RM_ABS, RM_NEG]])
O_FDSX = hw_op('fdsx', OM_ALU, 1, 1, [], [[RM_ABS, RM_NEG]])
O_FDSXF = hw_op('fdsxf', OM_ALU, 1, 1, [], [[RM_ABS, RM_NEG]])
O_FDSY = hw_op('fdsy', OM_ALU, 1, 1, [], [[RM_ABS, RM_NEG]])
O_FDSYF = hw_op('fdsyf', OM_ALU, 1, 1, [], [[RM_ABS, RM_NEG]])
O_PCK = hw_op('pck', OM_ALU + [OM_PCK_FMT, OM_ROUNDZERO, OM_SCALE], 1, 1)
O_PCK_PROG = hw_op('pck.prog', OM_ALU + [OM_ROUNDZERO, OM_SCALE], 1, 2)
O_ADD64_32 = hw_op('add64_32', OM_ALU + [OM_S], 2, 4, [], [[RM_ABS, RM_NEG], [], [RM_ABS, RM_NEG]])
O_IMADD64 = hw_op('imadd64', OM_ALU + [OM_S], 2, 5, [], [[RM_ABS, RM_NEG], [RM_ABS, RM_NEG], [RM_ABS, RM_NEG]])
O_IMADD32 = hw_op('imadd32', OM_ALU + [OM_S], 1, 4, [], [[RM_ABS, RM_NEG], [RM_ABS, RM_NEG], [RM_ABS, RM_NEG]])
O_UNPCK = hw_op('unpck', OM_ALU + [OM_PCK_FMT, OM_ROUNDZERO, OM_SCALE], 1, 1, [], [[RM_ELEM]])

O_TST = hw_direct_op('tst', [OM_TST_OP_MAIN, OM_PHASE2END, OM_TST_TYPE_MAIN], 2, 2, [], [[RM_ELEM], [RM_ELEM]])
O_MOVC = hw_direct_op('movc', [OM_PHASE2END], 2, 5, [[RM_ELEM]])

O_MOVWM = hw_op('movwm', OM_ALU + [OM_PHASE2END], 1, 2, [[RM_ELEM]])
O_MOVS1 = hw_op('movs1', OM_ALU, 1, 1)

# TODO
# O_PCK_ELEM = pseudo_op('pck.elem', OM_ALU_RPT1 + [OM_PCK_FMT, OM_ROUNDZERO, OM_SCALE], 1, 2)

## Backend.
O_UVSW_WRITE = hw_op('uvsw.write', [OM_EXEC_CND, OM_RPT], 0, 2)
O_UVSW_EMIT = hw_op('uvsw.emit', [OM_EXEC_CND])
O_UVSW_ENDTASK = hw_op('uvsw.endtask', [OM_END])
O_UVSW_EMIT_ENDTASK = hw_op('uvsw.emit.endtask', [OM_END])
O_UVSW_WRITE_EMIT_ENDTASK = hw_op('uvsw.write.emit.endtask', [OM_END], 0, 2)

O_FITR = hw_op('fitr', [OM_EXEC_CND, OM_END, OM_ITR_MODE, OM_SAT], 1, 3)
O_FITRP = hw_op('fitrp', [OM_EXEC_CND, OM_END, OM_ITR_MODE, OM_SAT], 1, 4)

O_LD = hw_op('ld', OM_ALU_RPT1 + [OM_MCU_CACHE_MODE_LD], 1, 3)
O_LD_REGBL = hw_op('ld.regbl', OM_ALU_RPT1 + [OM_MCU_CACHE_MODE_LD], 1, 3)

O_ST = hw_direct_op('st', [OM_MCU_CACHE_MODE_ST, OM_IDF], 0, 6)
O_ST_REGBL = hw_direct_op('st.regbl', [OM_MCU_CACHE_MODE_ST, OM_IDF], 0, 6)

# { data, data size, drc, chans, addr, cov_msk}
O_ST_TILED = hw_op('st.tiled', OM_ALU_RPT1 + [OM_MCU_CACHE_MODE_ST], 0, 6)

O_IDF = hw_op('idf', OM_ALU_RPT1, 0, 2)

O_ATOMIC = hw_op('atomic', [OM_OLCHK, OM_EXEC_CND, OM_END, OM_ATOM_OP], 1, 2)

O_SMP = hw_op('smp', OM_ALU_RPT1 + [OM_DIM, OM_PROJ, OM_FCNORM, OM_NNCOORDS,
                                    OM_LOD_MODE, OM_PPLOD, OM_TAO, OM_SOO,
                                    OM_SNO, OM_WRT, OM_SB_MODE, OM_MCU_CACHE_MODE_LD,
                                    OM_ARRAY, OM_INTEGER, OM_SCHEDSWAP, OM_F16], 1, 6)

O_ALPHATST = hw_op('alphatst', OM_ALU_RPT1, 1, 4)

O_ALPHAF = hw_op('alphaf', OM_ALU_RPT1, 1, 4)
O_DEPTHF = hw_op('depthf', OM_ALU_RPT1, 0, 2)

O_SAVMSK = hw_op('savmsk', OM_ALU_RPT1 + [OM_SAVMSK_MODE], 2)

O_EMITPIX = hw_op('emitpix', OM_ALU_RPT1 + [OM_FREEP], 0, 2)

## Bitwise.
O_MOVI32 = hw_op('movi32', OM_ALU, 1, 1)

O_CBS = hw_op('cbs', OM_ALU, 1, 1)
O_FTB = hw_op('ftb', OM_ALU, 1, 1)
O_REV = hw_op('rev', OM_ALU, 1, 1)
O_SHUFFLE = hw_op('shuffle', OM_ALU, 1, 2)
O_LOGICAL = hw_op('logical', OM_ALU + [OM_LOGIOP], 1, 4)
O_SHIFT = hw_op('shift', OM_ALU + [OM_SHIFTOP], 1, 3)

O_COPYSIGN = hw_op('copysign', OM_ALU, 1, 2)

O_IBFE = hw_op('ibfe', OM_ALU, 1, 3)
O_UBFE = hw_op('ubfe', OM_ALU, 1, 3)
O_BFI = hw_op('bfi', OM_ALU, 1, 4)

O_BBYP0BM = hw_direct_op('bbyp0bm', [], 2, 2)
O_BBYP0BM_IMM32 = hw_direct_op('bbyp0bm_imm32', [], 2, 2)
O_BBYP0S1 = hw_direct_op('bbyp0s1', [], 2, 1)
O_MSK_BBYP0S1 = hw_direct_op('msk_bbyp0s1', [], 3, 3)
O_MSK_LSL = hw_direct_op('msk_lsl', [], 3, 3)

## Control.
O_WOP = hw_op('wop')
O_WDF = hw_op('wdf', [], 0, 1)
O_NOP = hw_op('nop', [OM_EXEC_CND, OM_END])

# TODO NEXT: gate usage of OM_F16!
O_DITR = hw_op('ditr', [OM_EXEC_CND, OM_ITR_MODE, OM_SAT, OM_SCHED, OM_F16], 1, 3)
O_DITRP = hw_op('ditrp', [OM_EXEC_CND, OM_ITR_MODE, OM_SAT, OM_SCHED, OM_F16], 1, 4)
O_DITRP_WRITE = hw_op('ditrp.write', [OM_EXEC_CND, OM_ITR_MODE, OM_SAT, OM_SCHED, OM_F16], 1, 4)
O_DITRP_READ = hw_op('ditrp.read', [OM_EXEC_CND, OM_ITR_MODE, OM_SAT, OM_SCHED, OM_F16], 1, 3)

O_CNDST = hw_op('cndst', [OM_EXEC_CND, OM_CND], 2, 2)
O_CNDEF = hw_op('cndef', [OM_EXEC_CND, OM_CND], 2, 2)
O_CNDSM = hw_op('cndsm', [OM_EXEC_CND, OM_CND], 2, 2)
O_CNDLT = hw_op('cndlt', [OM_EXEC_CND, OM_CND], 3, 2)
O_CNDEND = hw_op('cndend', [OM_EXEC_CND], 2, 2)

O_BR = hw_op('br', [OM_EXEC_CND, OM_BRANCH_CND, OM_LINK], has_target_cf_node=True)

O_BR_NEXT = hw_op('br.next', [OM_EXEC_CND])

O_MUTEX = hw_op('mutex', [OM_MUTEX_OP], 0, 1)

# Combination (> 1 instructions per group).
O_SCMP = hw_op('scmp', OM_ALU + [OM_TST_OP_MAIN], 1, 2, [], [[RM_ABS, RM_NEG], [RM_ABS, RM_NEG]])
O_BCMP = hw_op('bcmp', OM_ALU + [OM_TST_OP_MAIN, OM_TST_TYPE_MAIN], 1, 2, [], [[RM_ABS, RM_NEG], [RM_ABS, RM_NEG]])
O_BCSEL = hw_op('bcsel', OM_ALU, 1, 3, [], [[], [RM_ABS, RM_NEG], [RM_ABS, RM_NEG]])
O_CSEL = hw_op('csel', OM_ALU + [OM_TST_OP_MAIN, OM_TST_TYPE_MAIN], 1, 3, [], [[], [RM_ABS, RM_NEG], [RM_ABS, RM_NEG]])
O_PSEL = hw_op('psel', OM_ALU, 1, 3)
O_PSEL_TRIG = hw_op('psel_trig', OM_ALU, 1, 3)
O_FSIGN = hw_op('fsign', OM_ALU, 1, 1)
O_ISIGN = hw_op('isign', OM_ALU, 1, 1)
O_FCEIL = hw_op('fceil', OM_ALU, 1, 1)
O_MIN = hw_op('min', OM_ALU + [OM_TST_TYPE_MAIN], 1, 2, [], [[RM_ABS, RM_NEG], [RM_ABS, RM_NEG]])
O_MAX = hw_op('max', OM_ALU + [OM_TST_TYPE_MAIN], 1, 2, [], [[RM_ABS, RM_NEG], [RM_ABS, RM_NEG]])
O_IADD32 = hw_op('iadd32', OM_ALU + [OM_S], 1, 3, [], [[RM_ABS, RM_NEG], [RM_ABS, RM_NEG]])
O_IMUL32 = hw_op('imul32', OM_ALU + [OM_S], 1, 3, [], [[RM_ABS, RM_NEG], [RM_ABS, RM_NEG]])

O_TSTZ = hw_op('tstz', OM_ALU + [OM_TST_TYPE_MAIN], 2, 1, [], [[RM_ELEM]])
O_ST32 = hw_op('st32', OM_ALU_RPT1 + [OM_MCU_CACHE_MODE_ST, OM_IDF], 0, 5)
O_ST32_REGBL = hw_op('st32.regbl', OM_ALU_RPT1 + [OM_MCU_CACHE_MODE_ST, OM_IDF], 0, 5)

# [vec3 for store], [data, offset, base_addr_lo, base_addr_hi]
O_SPILL = hw_op('spill', OM_ALU_RPT1, 1, 4)

# [result], [offset, base_addr_lo, base_addr_hi]
O_UNSPILL = hw_op('unspill', OM_ALU_RPT1, 1, 3)

O_IADD32_ATOMIC = hw_op('iadd32.atomic', OM_ALU_ATOMEXT + [OM_S], 2, 3, [], [[RM_ABS, RM_NEG], [RM_ABS, RM_NEG]])
O_XCHG_ATOMIC = hw_op('xchg.atomic', OM_ALU_ATOMEXT, 2, 2, [], [[RM_ABS, RM_NEG], [RM_ABS, RM_NEG]])
O_CMPXCHG_ATOMIC = hw_op('cmpxchg.atomic', OM_ALU_ATOMEXT + [OM_TST_TYPE_MAIN], 2, 3, [], [[RM_ABS, RM_NEG], [RM_ABS, RM_NEG]])
O_MIN_ATOMIC = hw_op('min.atomic', OM_ALU_ATOMEXT + [OM_TST_TYPE_MAIN], 2, 2, [], [[RM_ABS, RM_NEG], [RM_ABS, RM_NEG]])
O_MAX_ATOMIC = hw_op('max.atomic', OM_ALU_ATOMEXT + [OM_TST_TYPE_MAIN], 2, 2, [], [[RM_ABS, RM_NEG], [RM_ABS, RM_NEG]])
O_LOGICAL_ATOMIC = hw_op('logical.atomic', OM_ALU_ATOMEXT + [OM_LOGIOP], 2, 2, [], [[RM_ABS, RM_NEG], [RM_ABS, RM_NEG]])

O_FLUSH_P0 = hw_op('flush.p0', [OM_EXEC_CND, OM_END])

O_MBYP2 = hw_op('mbyp2', OM_ALU, 2, 2, [], [[RM_ABS, RM_NEG], [RM_ABS, RM_NEG]])

# Pseudo-ops (unmapped).
O_FNEG = pseudo_op('fneg', OM_ALU, 1, 1)
O_FABS = pseudo_op('fabs', OM_ALU, 1, 1)
O_FFLR = pseudo_op('fflr', OM_ALU, 1, 1)
O_MOV = pseudo_op('mov', OM_ALU, 1, 1)
O_MOV_OFFSET = pseudo_op('mov.offset', OM_ALU + [OM_OFFSET_SD], 1, 2)
O_VEC = pseudo_op('vec', [OM_EXEC_CND], 1, VARIABLE)
O_COMP = pseudo_op('comp', [], 1, 2)

O_OP_ATOMIC_OFFSET = pseudo_op('op.atomic.offset', OM_ALU_ATOMEXT + [OM_ATOM_OP], 2, 4, [], [[RM_ABS, RM_NEG], [RM_ABS, RM_NEG]])

O_BREAK = pseudo_op('break', [OM_EXEC_CND])
O_CONTINUE = pseudo_op('continue', [OM_EXEC_CND])
