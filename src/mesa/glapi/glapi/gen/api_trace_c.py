# Copyright © 2026 Igalia S.L.
# SPDX-License-Identifier: MIT
#
# Generates api_trace.c: per-entrypoint wrappers that log GL calls to stderr
# and forward to the real dispatch. Installed at context create when
# MESA_VERBOSE=api is set.

import apiexec
import gl_XML
import license

from collections import defaultdict

TRACE_ARRAY_BUFSZ = 512
TRACE_BITFIELD_BUFSZ = 512


header = """
#include <inttypes.h>
#include <stdio.h>

#include "glapi/glapi/glapi.h"
#include "main/api_trace_helpers.h"
#include "main/glthread_marshal.h"
#include "main/context.h"
#include "main/enums.h"
#include "main/errors.h"
#include "dispatch.h"
"""


TYPE_FORMAT = {
    'GLenum':      ('%s',          '_mesa_enum_to_string({p})'),
    'GLboolean':   ('%s',          '{p} ? "GL_TRUE" : "GL_FALSE"'),
    'GLbitfield':  ('0x%x',        '{p}'),
    'GLbyte':      ('%d',          '{p}'),
    'GLubyte':     ('%u',          '{p}'),
    'GLshort':     ('%d',          '{p}'),
    'GLushort':    ('%u',          '{p}'),
    'GLint':       ('%d',          '{p}'),
    'GLuint':      ('%u',          '{p}'),
    'GLint64':     ('%" PRId64 "', '(int64_t){p}'),
    'GLuint64':    ('%" PRIu64 "', '(uint64_t){p}'),
    'GLsizei':     ('%d',          '{p}'),
    'GLintptr':    ('%" PRIdPTR "','(intptr_t){p}'),
    'GLsizeiptr':  ('%" PRIdPTR "','(intptr_t){p}'),
    'GLfloat':     ('%f',          '{p}'),
    'GLclampf':    ('%f',          '{p}'),
    'GLdouble':    ('%f',          '{p}'),
    'GLclampd':    ('%f',          '{p}'),
    'GLfixed':     ('%d',          '{p}'),
    'GLclampx':    ('%d',          '{p}'),
    'GLhalfNV':    ('0x%x',        '{p}'),
    'GLvdpauSurfaceNV': ('%" PRIdPTR "', '(intptr_t){p}'),
    'GLsync':      ('%p',          '(void *){p}'),
    'GLhandleARB': ('%u',          '(unsigned int){p}'),
    'GLDEBUGPROC':    ('%p',       '(void *){p}'),
    'GLDEBUGPROCARB': ('%p',       '(void *){p}'),
    'GLDEBUGPROCAMD': ('%p',       '(void *){p}'),
    'GLDEBUGPROCKHR': ('%p',       '(void *){p}'),
    'GLVULKANPROCNV': ('%p',       '(void *){p}'),
}

TYPE_ALIASES = {
    'GLint64EXT':    'GLint64',
    'GLuint64EXT':   'GLuint64',
    'GLintptrARB':   'GLintptr',
    'GLsizeiptrARB': 'GLsizeiptr',
    'float':         'GLfloat',
    'int':           'GLint',
}


# Element types not listed here fall back to ``%p`` at the call site.
ARRAY_ELEM_KIND = {
    'GLfloat':     'MESA_TRACE_ELEM_FLOAT',
    'GLclampf':    'MESA_TRACE_ELEM_FLOAT',
    'GLdouble':    'MESA_TRACE_ELEM_DOUBLE',
    'GLclampd':    'MESA_TRACE_ELEM_DOUBLE',
    'GLbyte':      'MESA_TRACE_ELEM_BYTE',
    'GLshort':     'MESA_TRACE_ELEM_SHORT',
    'GLint':       'MESA_TRACE_ELEM_INT',
    'GLsizei':     'MESA_TRACE_ELEM_INT',
    'GLfixed':     'MESA_TRACE_ELEM_INT',
    'GLclampx':    'MESA_TRACE_ELEM_INT',
    'GLubyte':     'MESA_TRACE_ELEM_UBYTE',
    'GLushort':    'MESA_TRACE_ELEM_USHORT',
    'GLuint':      'MESA_TRACE_ELEM_UINT',
    'GLhalfNV':    'MESA_TRACE_ELEM_HALF',
    'GLint64':     'MESA_TRACE_ELEM_INT64',
    'GLuint64':    'MESA_TRACE_ELEM_UINT64',
    'GLintptr':    'MESA_TRACE_ELEM_INTPTR',
    'GLsizeiptr':  'MESA_TRACE_ELEM_INTPTR',
}


def array_count_expr(p):
    if p.counter:
        scale = p.count_scale if p.count_scale else 1
        if scale == 1:
            return '(size_t){0}'.format(p.counter)
        return '(size_t){0} * {1}'.format(p.counter, scale)
    if p.count and p.count >= 2:
        return str(p.count)

    if p.marshal_count:
        return '(size_t)({0})'.format(p.marshal_count)

    return None


def classify_param(p):
    """Return one of:
       ('scalar', spec, expr)        - printed inline
       ('array', kind, count_expr, name)
       ('opaque', spec, expr)        - printed as %p / hex fallback
    """
    opaque = ('opaque', '%p', '(void *){0}'.format(p.name))

    if not p.is_pointer():
        if p.type_string().strip() == 'GLbitfield' and p.group:
            return ('bitfield', '%s', p.group, p.name)

        ts = TYPE_ALIASES.get(p.type_string().strip(), p.type_string().strip())
        if ts in TYPE_FORMAT:
            spec, expr = TYPE_FORMAT[ts]
            return ('scalar', spec, expr.format(p=p.name))
        return ('scalar', '0x%x', p.name)

    base = p.get_base_type_string()

    if base in ('GLchar', 'GLcharARB'):
        ts = p.type_string().lstrip()
        if ts.startswith('const'):
            return ('scalar', '%s',
                    '{p} ? (const char *){p} : "(null)"'.format(p=p.name))
        return opaque

    if p.is_output:
        return opaque

    elem = TYPE_ALIASES.get(base, base)
    count_expr = array_count_expr(p)
    if elem in ARRAY_ELEM_KIND and count_expr is not None:
        return ('array', ARRAY_ELEM_KIND[elem], count_expr, p.name)

    return opaque


class PrintCode(gl_XML.gl_print_base):
    def __init__(self):
        super(PrintCode, self).__init__()
        self.name = 'api_trace_c.py'
        self.license = license.bsd_license_template % (
            'Copyright (C) 2026 Christian Gmeiner', 'Christian Gmeiner')

    def printRealHeader(self):
        print(header)

    def printRealFooter(self):
        pass

    def print_bitfield_helpers(self, api):

        # prepare list of bitfield-groups
        groups = defaultdict(list)
        for e in api.enums_by_name.values():
            for g in e.group:
                groups[g].append((e.value, e.name))

        for group_name, enums in groups.items():
            # sort from largest bitmask value to smallest to prefer
            # composite enums (e.g GL_ALL_ATTRIB_BITS)
            enums.sort(key=lambda x: x[0], reverse=True)

            print('static void')
            print('_mesa_trace_format_bitfield_group_{0}(char *buf, size_t buflen, GLbitfield mask)'.format(group_name))
            print('{')
            print('   static const char *enum_names[] = {')
            print('      {0}'.format(', '.join('"{0}"'.format(enum[1]) for enum in enums)))
            print('   };')
            print('   static const unsigned enum_values[] = {')
            print('      {0}'.format(', '.join('{0}u'.format(enum[0]) for enum in enums)))
            print('   };')
            print('   _mesa_trace_format_bitfield_group(buf, buflen, enum_names, enum_values, {0}, mask);'.format(len(enums)))
            print('}')
            print('')


    def print_wrapper(self, f):
        params = [p for p in f.parameters if not p.is_padding]
        classified = [classify_param(p) for p in params]

        specs = []
        args = []
        prelude = []
        for p, c in zip(params, classified):
            if c[0] == 'bitfield':
                _, _, group_name, name = c
                buf_name = '{0}_buf'.format(name)
                prelude.append('char {buf}[{sz}];'.format(buf=buf_name, sz=TRACE_BITFIELD_BUFSZ))
                prelude.append(
                    '_mesa_trace_format_bitfield_group_{group}({buf}, sizeof({buf}), {name});'
                    .format(group=group_name, buf=buf_name, name=name)
                )
                specs.append('%s')
                args.append(buf_name)
            elif c[0] == 'array':
                _, kind, count_expr, name = c
                buf_name = '{0}_buf'.format(name)
                prelude.append('char {buf}[{sz}];'
                               .format(buf=buf_name, sz=TRACE_ARRAY_BUFSZ))
                prelude.append(
                    '_mesa_trace_format_array({buf}, sizeof({buf}),'
                    ' {name}, {count}, {kind});'
                    .format(buf=buf_name, name=name,
                            count=count_expr, kind=kind))
                specs.append('%s')
                args.append(buf_name)
            else:
                _, spec, expr = c
                specs.append(spec)
                args.append(expr)

        ret = f.return_type
        param_string = f.get_parameter_string()
        called = f.get_called_parameter_string()

        print('static {rt} GLAPIENTRY'.format(rt=ret))
        print('_mesa_trace_{name}({ps})'.format(name=f.name, ps=param_string))
        print('{')
        print('   GET_CURRENT_CONTEXT(ctx);')
        for stmt in prelude:
            print('   {0}'.format(stmt))
        if args:
            fmt_body = 'gl{0}({1})\\n'.format(f.name, ', '.join(specs))
            print('   _mesa_debug(ctx, "{fmt}", {args});'
                  .format(fmt=fmt_body, args=', '.join(args)))
        else:
            print('   _mesa_debug(ctx, "gl{0}()\\n");'.format(f.name))
        call = 'CALL_{name}(ctx->Dispatch.RealPublished, ({ca}));'.format(
            name=f.name, ca=called if called else '')
        if ret != 'void':
            print('   return {call}'.format(call=call))
        else:
            print('   {call}'.format(call=call))
        print('}')
        print('')

    def print_install(self, functions):
        print('bool')
        print('_mesa_init_dispatch_trace(struct gl_context *ctx)')
        print('{')
        print('   struct _glapi_table *table = _mesa_alloc_dispatch_table(false);')
        print('   if (!table)')
        print('      return false;')
        print('')
        for f in functions:
            print('   SET_{name}(table, _mesa_trace_{name});'.format(name=f.name))
        print('')
        print('   ctx->Dispatch.Trace = table;')
        print('   return true;')
        print('}')

    def printBody(self, api):
        self.print_bitfield_helpers(api)
        functions = list(api.functionIterateByOffset())
        for f in functions:
            self.print_wrapper(f)
        self.print_install(functions)


if __name__ == '__main__':
    apiexec.print_glapi_file(PrintCode())
