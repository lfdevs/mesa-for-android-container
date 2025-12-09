#! /usr/bin/env python3

import argparse
from collections import defaultdict
from mako.template import Template
import util

TEMPLATE_H = Template(
    """\
#pragma once

%for (engine_id, classes) in classes_per_eng.items():
%for cl in classes:
#include "nv_push_cl${cl_for_filename(cl)}.h"
%endfor
%endfor


static inline const char*
P_PARSE_NV_MTHD(uint16_t class_id, uint16_t idx)
{
    uint16_t cls_hi = (class_id & 0xff00) >> 8;
    uint16_t cls_lo = class_id & 0xff;

    if (idx < 0x100) {
%for cl_idx in range(0, len(classes_per_eng[0x6F])):
        if (cls_hi >= ${hex(class_id_to_arch_id(classes_per_eng[0x6F][cl_idx]))})
            return P_PARSE_${cl_for_function_name(classes_per_eng[0x6F][cl_idx])}_MTHD(idx);
        else
%endfor
        {
            assert(false && "unknown class id");
        }

    } else {
        switch (cls_lo) {
%for (engine_id, classes) in classes_per_eng.items():
            case ${hex(engine_id)}:
%if len(classes) == 1:
                return P_PARSE_${cl_for_function_name(classes[-1])}_MTHD(idx);
%else:
%for cl_idx in range(0, len(classes)):
                if (cls_hi >= ${hex(class_id_to_arch_id(classes[cl_idx]))})
                    return P_PARSE_${cl_for_function_name(classes[cl_idx])}_MTHD(idx);
                else
%endfor
                {
                    assert(false && "unknown class id");
                }
                break;
%endif
%endfor
            default:
                break;
        }
    }

    return "unknown method";
}

static inline void
P_DUMP_NV_MTHD_DATA(FILE *fp, uint16_t class_id, uint16_t idx, uint32_t data,
                    const char *prefix)
{
    uint16_t cls_hi = (class_id & 0xff00) >> 8;
    uint16_t cls_lo = class_id & 0xff;

    if (idx < 0x100) {
%for cl_idx in range(0, len(classes_per_eng[0x6F])):
        if (cls_hi >= ${hex(class_id_to_arch_id(classes_per_eng[0x6F][cl_idx]))})
            P_DUMP_${cl_for_function_name(classes_per_eng[0x6F][cl_idx])}_MTHD_DATA(fp, idx, data, prefix);
        else
%endfor
        {
            assert(false && "unknown class id");
        }
    } else {
        switch (cls_lo) {
%for (engine_id, classes) in classes_per_eng.items():
            case ${hex(engine_id)}:
%if len(classes) == 1:
                P_DUMP_${cl_for_function_name(classes[-1])}_MTHD_DATA(fp, idx, data, prefix);
%else:
%for cl_idx in range(0, len(classes)):
                if (cls_hi >= ${hex(class_id_to_arch_id(classes[cl_idx]))})
                    P_DUMP_${cl_for_function_name(classes[cl_idx])}_MTHD_DATA(fp, idx, data, prefix);
                else
%endfor
                {
                    assert(false && "unknown class id");
                }
%endif
                break;
%endfor
            default:
                fprintf(fp, "%s.VALUE = 0x%x\\n", prefix, data);
                break;
        }
    }
}
"""
)


def cl_for_filename(class_id):
    return f"{class_id:04x}"


def cl_for_function_name(class_id):
    return f"NV{class_id:04X}"


def class_id_to_engine_id(class_id):
    return class_id & 0xFF


def class_id_to_arch_id(class_id):
    return (class_id & 0xFF00) >> 8


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out-h", required=False, help="Output C header.")
    parser.add_argument("--classes", help="Input class header file.", required=True)
    args = parser.parse_args()

    classes = args.classes.strip().split(" ")

    classes_per_eng = defaultdict(list)

    for cl in classes:
        class_id = int(cl.removeprefix("cl"), 16)
        engine_id = class_id_to_engine_id(class_id)

        classes_per_eng[engine_id].append(class_id)

    # Ensure everything is sorted in reverse order
    for engine_id in classes_per_eng:
        classes_per_eng[engine_id].sort(reverse=True)

    environment = {
        "classes_per_eng": classes_per_eng,
        "cl_for_filename": cl_for_filename,
        "cl_for_function_name": cl_for_function_name,
        "class_id_to_arch_id": class_id_to_arch_id,
    }

    if args.out_h is not None:
        util.write_template(args.out_h, TEMPLATE_H, environment)


if __name__ == "__main__":
    main()
