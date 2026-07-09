#!/usr/bin/python3
#
# Copyright 2026 Intel Corporation
# SPDX-License-Identifier: MIT

import argparse
import os
import sys
import subprocess

ALIASES = {
    "amd": "gfx1201",
    "asahi": "m1",
    "freedreno": "a750",
    "intel": "lnl",
    "lima": "lima-mali450",
    "nouveau": "turing",
    "v3d": "rpi5",
    "vc4": "rpi3",

    "a3xx": "a306",
    "a5xx": "a530",
    "a6xx": "a660",
    "a7xx": "a750",

    "lima-mali450": "mali450",
    "panfrost-t860": "t860",
    "panfrost-g52": "g52",
    "panfrost-g57": "g57",
    "panfrost-g72": "g72",
    "panfrost-g610": "g610",
    "panfrost-g925": "g925",
}

TARGETS = {
    "stoney": ["amd", "STONEY"],
    "raven": ["amd", "RAVEN2"],
    "cezanne": ["amd", "RENOIR"],
    "mendocino": ["amd", "RAPHAEL_MENDOCINO"],
    "vangogh": ["amd", "VANGOGH"],
    "navi21": ["amd", "NAVI21"],
    "navi31": ["amd", "NAVI31"],
    "gfx1201": ["amd", "GFX1201"],

    "m1": ["asahi", None],

    "a306": ["freedreno", "307"],
    "a530": ["freedreno", "530"],
    "a618": ["freedreno", "618"],
    "a660": ["freedreno", "660"],
    "a750": ["freedreno", "750"],

    "skl": ["intel", "skl"],
    "apl": ["intel", "apl"],
    "glk": ["intel", "glk"],
    "kbl": ["intel", "kbl"],
    "jsl": ["intel", "jsl"],
    "tgl": ["intel", "tgl"],
    "adl": ["intel", "adl"],
    "rpl": ["intel", "rpl"],
    "lnl": ["intel", "lnl"],
    "ptl": ["intel", "ptl"],

    "mali450": ["lima", None],

    "turing": ["nouveau", "160"],

    "t720": ["panfrost", "720"],
    "t860": ["panfrost", "860"],
    "g52": ["panfrost", "7212"],
    "g57": ["panfrost", "9093"],
    "g72": ["panfrost", "6221"],
    "g610": ["panfrost", "a867"],
    "g310v1": ["panfrost", "ac04:0"],
    "g310v2": ["panfrost", "ac04:1"],
    "g310v3": ["panfrost", "ac04:2"],
    "g310v4": ["panfrost", "ac04:3"],
    "g310v5": ["panfrost", "ac04:4"],
    "g720": ["panfrost", "c800:4"],
    "g725": ["panfrost", "d800:4"],
    "g925": ["panfrost", "d830:4"],

    "rpi4": ["v3d", "42"],
    "rpi5": ["v3d", "71"],

    "rpi3": ["vc4", None],

}

LD_PRELOAD = {
    "amd": "src/amd/drm-shim/libamdgpu_noop_drm_shim.so",
    "asahi": "src/asahi/drm-shim/libasahi_noop_drm_shim.so",
    "freedreno": "src/freedreno/drm-shim/libfreedreno_noop_drm_shim.so",
    "intel": "src/intel/tools/libintel_noop_drm_shim.so",
    "nouveau": "src/nouveau/drm-shim/libnouveau_noop_drm_shim.so",
    "panfrost": "src/panfrost/drm-shim/libpanfrost_noop_drm_shim.so",
    "v3d": "src/broadcom/drm-shim/libv3d_noop_drm_shim.so",
    "vc4": "src/broadcom/drm-shim/libvc4_noop_drm_shim.so",
}

SHIM_VAR = {
    "amd": "AMDGPU_GPU_ID",
    "freedreno": "FD_GPU_ID",
    "intel": "INTEL_STUB_GPU_PLATFORM",
    "nouveau": "NOUVEAU_CHIPSET",
    "panfrost": "PAN_GPU_ID",
    "v3d": "V3D_GPU_ID",
}

DISASM = {
    "amd": [("AMD_DEBUG", "vs,ps,gs,tcs,tes,cs,ts,ms,nir,aco,asm"), ("RADV_DEBUG", "shaders")],
    "asahi": [("AGX_MESA_DEBUG", "shaders")],
    "freedreno": [("IR3_SHADER_DEBUG", "disasm")],
    "intel": [("INTEL_DEBUG", "vs,tcs,tes,gs,fs,cs,mesh,task,rt")],
    "nouveau": [("NAK_DEBUG", "print")],
    "panfrost": [
        ("MIDGARD_MESA_DEBUG", "shaders"),
        ("BIFROST_MESA_DEBUG", "shaders")
    ],
    "vc4": [("VC4_DEBUG", "nir,vir,qpu")],
    "v3d": [("V3D_DEBUG", "nir,vir,qpu")],
}

if __name__ == "__main__":
    epilog = "Known GPUs: " + ' '.join([f'{target}' for target in TARGETS])
    parser = argparse.ArgumentParser(
        prog='drm-shim',
        description='Run a program under drm-shim',
        epilog=epilog)
    parser.add_argument('gpu', help="Driver name, CI job or GPU model")
    parser.add_argument('-d', '--disasm', action='store_true',
                        help="Disassemble shaders")
    parser.add_argument('-z', '--zink', action='store_true',
                        help="Use Zink for OpenGL instead of the native driver")
    args, rest = parser.parse_known_args()
    command = ' '.join(rest)

    build = os.environ.get('DRM_SHIM_PATH')
    if build is None:
        print("Must run inside a Mesa meson devenv")
        sys.exit(1)

    # Force zink for zink CI jobs
    if args.gpu.startswith('zink-'):
        args.zink = True

    # Drop common CI job prefixes/suffixes
    prefixes = ["zink-", "anv-", "radv-", "tu-", "radeonsi-"]
    suffixes = ["-valve", "-gl", "-gles", "-gles2",
                "-vk", "-piglit", "-glcts", "-vkcts"]

    for prefix in prefixes:
        args.gpu = args.gpu.removeprefix(prefix)

    for suffix in suffixes:
        args.gpu = args.gpu.removesuffix(suffix)

    if args.gpu in ALIASES:
        args.gpu = ALIASES[args.gpu]

    if args.gpu not in TARGETS:
        print(f"Unknown target {args.gpu}")
        sys.exit(1)

    driver, model = TARGETS[args.gpu]
    env = os.environ.copy()
    env['LD_PRELOAD'] = f'{build}/{LD_PRELOAD[driver]}'

    if model is not None:
        env[SHIM_VAR[driver]] = model

    if args.zink:
        env['MESA_LOADER_DRIVER_OVERRIDE'] = 'zink'

    if args.disasm:
        for (key, val) in DISASM[driver]:
            env[key] = val

    sys.exit(subprocess.call(command, env=env, shell=True))
