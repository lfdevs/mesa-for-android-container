# Copyright © 2026 Valve Corporation
# SPDX-License-Identifier: MIT

import subprocess
import glob
import sys
import os
import shutil

os.chdir(os.path.dirname(__file__) + "/../../..")

# Meson and other build systems use 77 to skip tests
SKIP_RETURN_CODE = 77
if not shutil.which("awk"):
    print('Skipping test. "awk" not found')
    sys.exit(SKIP_RETURN_CODE)

files = (
    glob.glob("src/freedreno/**/*.[ch]", recursive=True)
    + glob.glob("src/freedreno/**/*.cc", recursive=True)
    + glob.glob("src/gallium/drivers/freedreno/**/*.[ch]", recursive=True)
    + glob.glob("src/gallium/drivers/freedreno/**/*.cc", recursive=True)
)

result = subprocess.run(
    ["awk", "-f", "src/freedreno/tests/check_xml_includes.awk"] + sorted(files)
)
sys.exit(result.returncode)
