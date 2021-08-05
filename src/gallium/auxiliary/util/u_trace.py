#
# Copyright (C) 2020 Google, Inc.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.
#

from mako.template import Template
import os

TRACEPOINTS = {}

class Tracepoint(object):
    """Class that represents all the information about a tracepoint
    """
    def __init__(self, name, args=[], tp_struct=None, tp_print=None, tp_perfetto=None):
        """Parameters:

        - name: the tracepoint name, a tracepoint function with the given
          name (prefixed by 'trace_') will be generated with the specied
          args (following a u_trace ptr).  Calling this tracepoint will
          emit a trace, if tracing is enabled.
        - args: the tracepoint func args, an array of [type, name] pairs
        - tp_struct: (optional) array of [type, name, expr] tuples to
          convert from tracepoint args to trace payload.  If not specified
          it will be generated from `args` (ie, [type, name, name])
        - tp_print: (optional) array of format string followed by expressions
        - tp_perfetto: (optional) driver provided callback which can generate
          perfetto events
        """
        assert isinstance(name, str)
        assert isinstance(args, list)
        assert name not in TRACEPOINTS

        self.name = name
        self.args = args
        if tp_struct is None:
            tp_struct = []
            for arg in args:
                tp_struct.append([arg[0], arg[1], arg[1]])
        self.tp_struct = tp_struct
        self.tp_print = tp_print
        self.tp_perfetto = tp_perfetto

        TRACEPOINTS[name] = self

HEADERS = []

class Header(object):
    """Class that represents a header file dependency of generated tracepoints
    """
    def __init__(self, hdr):
        """Parameters:

        - hdr: the required header path
        """
        assert isinstance(hdr, str)
        self.hdr = hdr

        HEADERS.append(self)

hdr_template = """\
/* Copyright (C) 2020 Google, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

<% guard_name = '_' + hdrname + '_H' %>
#ifndef ${guard_name}
#define ${guard_name}

% for header in HEADERS:
#include "${header.hdr}"
% endfor

#include "util/u_trace.h"

#ifdef __cplusplus
extern "C" {
#endif

% for trace_name, trace in TRACEPOINTS.items():
/*
 * ${trace_name}
 */
struct trace_${trace_name} {
%    for member in trace.tp_struct:
         ${member[0]} ${member[1]};
%    endfor
%    if len(trace.tp_struct) == 0:
#ifdef  __cplusplus
     /* avoid warnings about empty struct size mis-match in C vs C++..
      * the size mis-match is harmless because (a) nothing will deref
      * the empty struct, and (b) the code that cares about allocating
      * sizeof(struct trace_${trace_name}) (and wants this to be zero
      * if there is no payload) is C
      */
     uint8_t dummy;
#endif
%    endif
};
%    if trace.tp_perfetto is not None:
#ifdef HAVE_PERFETTO
void ${trace.tp_perfetto}(struct pipe_context *pctx, uint64_t ts_ns, const struct trace_${trace_name} *payload);
#endif
%    endif
void __trace_${trace_name}(struct u_trace *ut
%    for arg in trace.args:
     , ${arg[0]} ${arg[1]}
%    endfor
);
static inline void trace_${trace_name}(struct u_trace *ut
%    for arg in trace.args:
     , ${arg[0]} ${arg[1]}
%    endfor
) {
%    if trace.tp_perfetto is not None:
   if (!unlikely(ut->enabled || ut_perfetto_enabled))
%    else:
   if (!unlikely(ut->enabled))
%    endif
      return;
   __trace_${trace_name}(ut
%    for arg in trace.args:
        , ${arg[1]}
%    endfor
   );
}
% endfor

#ifdef __cplusplus
}
#endif

#endif /* ${guard_name} */
"""

src_template = """\
/* Copyright (C) 2020 Google, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

% for header in HEADERS:
#include "${header.hdr}"
% endfor

#include "${hdr}"

#define __NEEDS_TRACE_PRIV
#include "util/u_trace_priv.h"

% for trace_name, trace in TRACEPOINTS.items():
/*
 * ${trace_name}
 */
%    if trace.tp_print is not None:
static void __print_${trace_name}(FILE *out, const void *arg) {
   const struct trace_${trace_name} *__entry =
      (const struct trace_${trace_name} *)arg;
   fprintf(out, "${trace.tp_print[0]}\\n"
%       for arg in trace.tp_print[1:]:
           , ${arg}
%       endfor
   );
}
%    else:
#define __print_${trace_name} NULL
%    endif
static const struct u_tracepoint __tp_${trace_name} = {
    ALIGN_POT(sizeof(struct trace_${trace_name}), 8),   /* keep size 64b aligned */
    "${trace_name}",
    __print_${trace_name},
%    if trace.tp_perfetto is not None:
#ifdef HAVE_PERFETTO
    (void (*)(struct pipe_context *, uint64_t, const void *))${trace.tp_perfetto},
#endif
%    endif
};
void __trace_${trace_name}(struct u_trace *ut
%    for arg in trace.args:
     , ${arg[0]} ${arg[1]}
%    endfor
) {
   struct trace_${trace_name} *__entry =
      (struct trace_${trace_name} *)u_trace_append(ut, &__tp_${trace_name});
   (void)__entry;
%    for member in trace.tp_struct:
        __entry->${member[1]} = ${member[2]};
%    endfor
}

% endfor
"""

def utrace_generate(cpath, hpath):
    if cpath is not None:
        hdr = os.path.basename(cpath).rsplit('.', 1)[0] + '.h'
        with open(cpath, 'wb') as f:
            f.write(Template(src_template, output_encoding='utf-8').render(
                hdr=hdr,
                HEADERS=HEADERS,
                TRACEPOINTS=TRACEPOINTS))

    if hpath is not None:
        hdr = os.path.basename(hpath)
        with open(hpath, 'wb') as f:
            f.write(Template(hdr_template, output_encoding='utf-8').render(
                hdrname=hdr.rstrip('.h').upper(),
                HEADERS=HEADERS,
                TRACEPOINTS=TRACEPOINTS))
