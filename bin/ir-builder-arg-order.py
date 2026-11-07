#!/usr/bin/env python3
#
# Copyright 2026 Valve Corporation
# SPDX-License-Identifier: MIT

import argparse
import dataclasses
import os
import re
import subprocess
import sys
import tempfile


COCCI = r"""
@definition@
identifier f;
parameter list ps;
position fp;
position tp;
type T;
@@
T@tp f@fp(ps) { ... }

@script:python depends on definition@
f << definition.f;
fp << definition.fp;
tp << definition.tp;
ps << definition.ps;
@@
print("definition\t%s\t%s\t%s\t%s\t%s" %
      (fp[0].file, tp[0].line, fp[0].line, f, ps))

@prototype@
identifier f;
parameter list ps;
position fp;
position tp;
type T;
@@
T@tp f@fp(ps);

@script:python depends on prototype@
f << prototype.f;
fp << prototype.fp;
tp << prototype.tp;
ps << prototype.ps;
@@
print("prototype\t%s\t%s\t%s\t%s\t%s" %
      (fp[0].file, tp[0].line, fp[0].line, f, ps))
"""


@dataclasses.dataclass(frozen=True)
class Profile:
    default_paths: tuple[str, ...]
    wrapper_macro: str
    builder_pattern: object
    operand_patterns: tuple[str, ...]
    excluded_directories: tuple[str, ...] = ()


PROFILES = {
    "nir": Profile(
        default_paths=("src/compiler/nir",),
        wrapper_macro="MESA_CALL_WITH_LTR_ARGS",
        builder_pattern=r"\b(?:struct\s+)?nir_builder\s*\*",
        operand_patterns=(
            r"\bnir_def\b",
            r"\bnir_deref_instr\b",
            r"\bnir_variable\b",
        ),
        excluded_directories=("tests",),
    ),
    "tu-nir": Profile(
        default_paths=("src/freedreno/vulkan",),
        wrapper_macro="MESA_CALL_WITH_LTR_ARGS",
        builder_pattern=r"\b(?:struct\s+)?nir_builder\s*\*",
        operand_patterns=(
            r"\bnir_def\b",
            r"\bnir_deref_instr\b",
            r"\bnir_variable\b",
        ),
    ),
    "ir3": Profile(
        default_paths=("src/freedreno/ir3",),
        wrapper_macro="MESA_CALL_WITH_LTR_ARGS",
        builder_pattern=None,
        operand_patterns=(r"\bstruct\s+ir3_instruction\b",),
    ),
    "spirv": Profile(
        default_paths=("src/compiler/spirv",),
        wrapper_macro="MESA_CALL_WITH_LTR_ARGS",
        builder_pattern=r"\b(?:struct\s+)?vtn_builder\s*\*",
        operand_patterns=(
            r"\bstruct\s+vtn_ssa_value\b",
            r"\bnir_def\b",
            r"\bnir_deref_instr\b",
        ),
    ),
}


@dataclasses.dataclass(frozen=True)
class Candidate:
    file: str
    decl_line: int
    name_line: int
    name: str
    args: tuple[str, ...]


C_SUFFIXES = (".c", ".h")
CXX_SUFFIXES = (".cc", ".cpp")


def split_spatch_paths(paths, excluded_directories=()):
    c_paths = []
    cxx_paths = []

    for path in paths:
        for root, directories, names in os.walk(path):
            if os.path.basename(root) in excluded_directories:
                directories.clear()
                continue

            directories[:] = [
                directory for directory in directories
                if directory not in excluded_directories
            ]
            for name in names:
                file_name = os.path.join(root, name)
                if file_name.endswith(C_SUFFIXES):
                    c_paths.append(file_name)
                elif file_name.endswith(CXX_SUFFIXES):
                    cxx_paths.append(file_name)

    return c_paths, cxx_paths


def run_spatch_once(paths, spatch, cocci_file, cxx=False):
    if not paths:
        return []

    cmd = [
        spatch,
        "--sp-file",
        cocci_file,
        "--very-quiet",
        "--include-headers",
    ]
    if cxx:
        cmd.append("--c++=17")
    for path in paths:
        cmd.append(path)

    proc = subprocess.run(cmd, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE)
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr)
        raise SystemExit(proc.returncode)

    candidates = []
    for line in proc.stdout.splitlines():
        fields = line.split("\t", 5)
        if len(fields) != 6:
            continue

        kind, file_name, decl_line, name_line, name, args = fields
        if kind not in ("definition", "prototype"):
            continue

        candidates.append(Candidate(
            file=file_name,
            decl_line=int(decl_line),
            name_line=int(name_line),
            name=name,
            args=split_parameters(args),
        ))

    return candidates


def run_spatch(paths, spatch, excluded_directories=()):
    c_paths, cxx_paths = split_spatch_paths(paths, excluded_directories)

    with tempfile.NamedTemporaryFile("w", suffix=".cocci") as cocci:
        cocci.write(COCCI)
        cocci.flush()

        candidates = run_spatch_once(c_paths, spatch, cocci.name)
        candidates += run_spatch_once(cxx_paths, spatch, cocci.name, cxx=True)

    return candidates


def split_parameters(text):
    args = []
    current = []
    depth = 0

    for ch in text:
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1

        if ch == "," and depth == 0:
            arg = "".join(current).strip()
            if arg:
                args.append(arg)
            current = []
        else:
            current.append(ch)

    arg = "".join(current).strip()
    if arg and arg != "void":
        args.append(arg)

    return tuple(args)


def compact_arg(arg):
    return re.sub(r"\s+", " ", arg.replace("\n", " ")).strip()


def has_builder_arg(args, profile):
    if profile.builder_pattern is None:
        return True
    return any(re.search(profile.builder_pattern, arg) for arg in args)


def count_operands(args, profile):
    operands = 0

    for arg in args:
        compact = compact_arg(arg)
        if re.search(r"\(\s*\*", compact):
            continue

        for pattern in profile.operand_patterns:
            match = re.search(pattern, compact)
            if not match:
                continue

            after = compact[match.end():]
            stars = after.count("*")
            if stars == 1 and "[" not in after:
                operands += 1
            break

    return operands


def is_header(file_name):
    return os.path.splitext(file_name)[1] == ".h"


def wrapper_text(name, real_name, profile):
    return (f"#define {name}(...) {profile.wrapper_macro}"
            f"({real_name}, __VA_ARGS__)\n")


def collect(paths, spatch, profile):
    candidates = []

    for candidate in run_spatch(paths, spatch,
                                profile.excluded_directories):
        if candidate.name.startswith("_"):
            continue
        if not has_builder_arg(candidate.args, profile):
            continue
        if count_operands(candidate.args, profile) < 2:
            continue

        candidates.append(candidate)

    return candidates


def rename_function_line(line, name, real_name):
    pattern = re.compile(r"\b" + re.escape(name) + r"\s*\(")
    return pattern.sub(real_name + "(", line, count=1)


def function_insert_index(lines, decl_line):
    idx = decl_line - 1
    while idx < len(lines) and not lines[idx].strip():
        idx += 1
    return idx


def add_wrapper_insert(inserts, lines, idx, text):
    entries = inserts.setdefault(idx, [])

    if not entries and idx > 0 and lines[idx - 1].strip():
        entries.append("\n")

    entries.append(text)


def write_file(file_name, candidates, header_wrapped_names, profile):
    with open(file_name, "r", encoding="utf-8") as f:
        lines = f.readlines()

    inserts = {}
    renamed_lines = {}
    macros_added = set()

    for candidate in sorted(candidates, key=lambda c: c.decl_line):
        real_name = "_" + candidate.name

        line_idx = candidate.name_line - 1
        if line_idx < 0 or line_idx >= len(lines):
            raise SystemExit(
                f"{file_name}:{candidate.name_line}: function line is out of range")

        renamed_lines[line_idx] = rename_function_line(
            renamed_lines.get(line_idx, lines[line_idx]),
            candidate.name, real_name)

        if (is_header(file_name) or candidate.name not in header_wrapped_names):
            if candidate.name not in macros_added:
                add_wrapper_insert(
                    inserts, lines, function_insert_index(
                        lines, candidate.decl_line),
                    wrapper_text(candidate.name, real_name, profile))
                macros_added.add(candidate.name)

    out = []
    for idx, line in enumerate(lines):
        out.extend(inserts.get(idx, ()))
        out.append(renamed_lines.get(idx, line))
    out.extend(inserts.get(len(lines), ()))

    with open(file_name, "w", encoding="utf-8") as f:
        f.writelines(out)

    print(f"updated {file_name}")


def write_changes(candidates, profile):
    by_file = {}
    for candidate in candidates:
        by_file.setdefault(candidate.file, []).append(candidate)

    header_wrapped_names = {
        candidate.name
        for candidate in candidates
        if is_header(candidate.file)
    }

    for file_name, file_candidates in sorted(by_file.items()):
        write_file(file_name, file_candidates, header_wrapped_names, profile)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--profile",
        choices=sorted(PROFILES),
        help="profile to process (default: all profiles)",
    )
    parser.add_argument("paths", nargs="*")
    parser.add_argument("--spatch", default="spatch")
    args = parser.parse_args()

    profile_names = (args.profile,) if args.profile else PROFILES
    for profile_name in profile_names:
        profile = PROFILES[profile_name]
        paths = args.paths or list(profile.default_paths)
        candidates = collect(paths, args.spatch, profile)

        write_changes(candidates, profile)


if __name__ == "__main__":
    main()
