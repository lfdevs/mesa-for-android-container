# Copyright © 2026 Valve Corporation
# SPDX-License-Identifier: MIT
#
# Check for direct includes of *.xml.h files in freedreno. Only files in the
# allowed list may include *.xml.h directly. All other files must use wrapper
# headers (like fd_hw_common.h) instead.

BEGIN {
    # Allowlist: files permitted to include *.xml.h directly
    allowed["src/freedreno/common/freedreno_pm4.h"]
    allowed["src/freedreno/common/fd_hw_common.h"]
    allowed["src/freedreno/common/fd2_hw.h"]
    allowed["src/freedreno/common/fd3_hw.h"]
    allowed["src/freedreno/common/fd4_hw.h"]
    allowed["src/freedreno/common/fd5_hw.h"]
    allowed["src/freedreno/common/fd6_hw.h"]
    allowed["src/freedreno/common/fd6_pack.h"]
    allowed["src/freedreno/vulkan/tu_cs.h"]

    errors = 0
}

# Beginning of each file.
FNR == 1 {
    # We have to do this here since FILENAME isn't available in BEGIN.
    if (FILENAME == "-") {
        print "ERROR: stdin cannot be used with this script" > "/dev/stderr"
        errors++
        exit
    }

    relpath = FILENAME
    sub(/^.*src\/freedreno\//, "src/freedreno/", relpath)
    if (relpath in allowed) {
        nextfile
    }
}

# Match #include "something.xml.h"
/#include[[:space:]]+"[^"]*\.xml\.h"/ {
    print relpath ":" FNR ": Error: direct include of XML header not allowed"
    print "  " $0
    print "  Use fd6_hw.h or another approved wrapper header instead"
    errors++
}

END {
    if (errors > 0) {
        print "\n" errors " error(s) found"
        exit 1
    }
    print "No errors found"
    exit 0
}
