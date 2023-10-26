# Copyright © 2023 Intel Corporation
# SPDX-License-Identifier: MIT

import sys

from builtin_types import BUILTIN_TYPES
from mako.template import Template

template = """\
/*
 * Copyright 2023 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

/* This is an automatically generated file. */

#ifdef BUILTIN_TYPES_CPP_DECLARATIONS
%for t in BUILTIN_TYPES:
static const glsl_type *const ${t["name"]}_type;
%endfor
#endif

#ifdef BUILTIN_TYPES_CPP_DEFINITIONS
%for t in BUILTIN_TYPES:
inline const glsl_type *const glsl_type::${t["name"]}_type = &glsl_type_builtin_${t["name"]};
%endfor
#endif
"""

if len(sys.argv) < 2:
    print('Missing output argument', file=sys.stderr)
    sys.exit(1)

output = sys.argv[1]

with open(output, 'w') as f:
    f.write(Template(template).render(BUILTIN_TYPES=BUILTIN_TYPES))
