#!/usr/bin/env python3
# Copyright (C) 2026 Valve Corporation
# SPDX-License-Identifier: MIT

# Generates the C header and source files for parsing a driver's list of
# supported DRI config options and their default values into a struct of values
# for the config options.  This generates the old DRI_CONF_SECTION /
# DRI_CONF_OPT_* macros internally, and supports validation that the driver
# config files don't refer to options that the driver doesn't use (e.g. due to
# typos).
#
# To use it, call <driver>_parse_dri_options() with the appropriate
# driConfigFileParseParams, and destroy it at teardown with
# driDestroyOptionCache() and driDestroyOptionInfo().

import argparse
import os
import re
import sys
import xml.etree.ElementTree as ET

from enum import Enum
from mako.template import Template

class DrircOptionType(Enum):
    BOOL         = 0,
    INT          = 1,
    UINT64       = 2,
    FLOAT        = 3,
    STRING       = 4,
    STRING_NODEF = 5,
    ENUM         = 6,

type_to_ctype: dict[DrircOptionType, str] = {
    DrircOptionType.BOOL:         "bool",
    DrircOptionType.INT:          "int",
    DrircOptionType.UINT64:       "uint64_t",
    DrircOptionType.FLOAT:        "float",
    DrircOptionType.STRING:       "char *",
    DrircOptionType.STRING_NODEF: "char *",
    DrircOptionType.ENUM:         "int",
}

type_to_macro: dict[DrircOptionType, str] = {
    DrircOptionType.BOOL:         "DRI_CONF_OPT_B",
    DrircOptionType.INT:          "DRI_CONF_OPT_I",
    DrircOptionType.UINT64:       "DRI_CONF_OPT_U64",
    DrircOptionType.FLOAT:        "DRI_CONF_OPT_F",
    DrircOptionType.STRING:       "DRI_CONF_OPT_S",
    DrircOptionType.STRING_NODEF: "DRI_CONF_OPT_S_NODEF",
    DrircOptionType.ENUM:         "DRI_CONF_OPT_E",
}

type_to_queryfn: dict[DrircOptionType, str] = {
    DrircOptionType.BOOL:         "driQueryOptionb",
    DrircOptionType.INT:          "driQueryOptioni",
    DrircOptionType.UINT64:       "driQueryOptionu64",
    DrircOptionType.FLOAT:        "driQueryOptionf",
    DrircOptionType.STRING:       "driQueryOptionstr",
    DrircOptionType.STRING_NODEF: "driQueryOptionstr",
    DrircOptionType.ENUM:         "driQueryOptioni",
}

type_to_documentation: dict[DrircOptionType, str] = {
    DrircOptionType.BOOL:         "bool",
    DrircOptionType.INT:          "int",
    DrircOptionType.UINT64:       "uint64",
    DrircOptionType.FLOAT:        "float",
    DrircOptionType.STRING:       "string",
    DrircOptionType.STRING_NODEF: "string",
    DrircOptionType.ENUM:         "enum",
}

class DrircOption(object):
    def __init__(self, dtype, name, description, c_name):
        self.dtype = dtype
        self.name = name
        self.description = description
        self.c_name = c_name
        self.c_args = []

class DrircBool(DrircOption):
    def __init__(self, name, value, description="", c_name=None):
        super().__init__(DrircOptionType.BOOL, name, description, c_name)
        self.value = value
        self.c_args = ["true" if value else "false", f"\"{self.description}\""]

class DrircInt(DrircOption):
    def __init__(self, name, value, min_value, max_value, description="", c_name=None):
        super().__init__(DrircOptionType.INT, name, description, c_name)
        self.value = value
        self.min_value = min_value
        self.max_value = max_value
        self.c_args = [f"{value}", f"{min_value}", f"{max_value}", f"\"{self.description}\""]

class DrircUint64(DrircOption):
    def __init__(self, name, value, min_value, max_value, description="", c_name=None):
        super().__init__(DrircOptionType.UINT64, name, description, c_name)
        self.value = value
        self.min_value = min_value
        self.max_value = max_value
        self.c_args = [f"{value}ULL", f"{min_value}ULL", f"{max_value}ULL", f"\"{self.description}\""]

class DrircFloat(DrircOption):
    def __init__(self, name, value, min_value, max_value, description="", c_name=None):
        super().__init__(DrircOptionType.FLOAT, name, description, c_name)
        self.value = value
        self.min_value = min_value
        self.max_value = max_value
        self.c_args = [f"{value}", f"{min_value}", f"{max_value}", f"\"{self.description}\""]

class DrircString(DrircOption):
    def __init__(self, name, value=None, description="", c_name=None):
        dtype = DrircOptionType.STRING if value is not None else DrircOptionType.STRING_NODEF
        super().__init__(dtype, name, description, c_name)
        self.value = value
        if value is not None:
            self.c_args = [f'"{value}"', f'"{self.description}"']
        else:
            self.c_args = [f'"{self.description}"']

class DrircEnumValue(object):
    def __init__(self, value, description=""):
        self.value = value
        self.description = description

class DrircEnum(DrircOption):
    def __init__(self, name, value, min_value, max_value, values, description="", c_name=None):
        super().__init__(DrircOptionType.ENUM, name, description, c_name)
        self.values = values
        self.value = value
        self.min_value = min_value
        self.max_value = max_value
        self.c_args = [f"{value}", f"{min_value}", f"{min_value}", f"\"{self.description}\""]
        vals = []
        for v in values:
            vals.append(f"DRI_CONF_ENUM({v.value}, \"{v.description}\")")
        self.c_args.append(''.join(vals))

class DrircSection(object):
    """Class that represents a DRIRC section
    """
    def __init__(self, name, options=[], c_name=None):
        """Parameters:

        - options: list of DrircOption
        """
        self.name = name
        self.options = options
        self.c_name = c_name if c_name is not None else name.lower()


TEMPLATE_H = """\
/* This file is autogenerated.  Do not edit. */

#ifndef ${driver_prefix.upper()}_DRIRC_H
#define ${driver_prefix.upper()}_DRIRC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "util/driconf.h"

struct ${driver_prefix}_drirc {
   struct driOptionCache options;
   struct driOptionCache available_options;

% for section in sections:
%   if any(o.c_name is not None for o in section.options):
   struct {
%     for option in section.options:
%       if option.c_name is not None:
      ${type_to_ctype(option.dtype)} ${option.c_name};
%       endif
%     endfor
   } ${section.c_name};
%   endif
% endfor
};

void ${driver_prefix}_parse_dri_options(struct ${driver_prefix}_drirc *drirc,
                                        const driConfigFileParseParams *params);

#ifdef __cplusplus
}
#endif

#endif /* ${driver_prefix.upper()}_DRIRC_H */
"""

TEMPLATE_C = """\
/* This file is autogenerated.  Do not edit. */

#include "${include_file}"

static const driOptionDescription dri_options[] = {
% for section in sections:
DRI_CONF_SECTION("${section.name}")
%   for option in section.options:
   ${type_to_macro(option.dtype)}(${option.name}
%      for arg in option.c_args:
      , ${arg}
%      endfor
      )
%   endfor
DRI_CONF_SECTION_END
% endfor
};

void
${driver_prefix}_parse_dri_options(struct ${driver_prefix}_drirc *drirc,
                                   const driConfigFileParseParams *params)
{
   driParseOptionInfo(&drirc->available_options, dri_options, ARRAY_SIZE(dri_options));
   driParseConfigFiles(&drirc->options, &drirc->available_options, params);

% for section in sections:
%   for option in section.options:
%     if option.c_name is not None:
   drirc->${section.c_name}.${option.c_name} = ${type_to_queryfn(option.dtype)}(&drirc->options, "${option.name}");
%     endif
%   endfor
% endfor
}
"""

def drirc_validate(conf_paths, sections):
    declared = {opt.name for section in sections for opt in section.options}
    conf_names = set()
    for conf_path in conf_paths:
        tree = ET.parse(conf_path)
        for option in tree.iter('option'):
            name = option.get('name')
            if name:
                conf_names.add(name)
    missing = conf_names - declared
    if missing:
        print('ERROR: options used in conf but not declared:', file=sys.stderr)
        for name in sorted(missing):
            print(f'  {name}', file=sys.stderr)
        sys.exit(1)

def drirc_generate(cpath, hpath, driver_prefix, sections):
    environment = {
        'driver_prefix': driver_prefix,
        'sections': sections,
        'type_to_ctype': lambda dtype: type_to_ctype[dtype],
        'type_to_macro': lambda dtype: type_to_macro[dtype],
        'type_to_queryfn': lambda dtype: type_to_queryfn[dtype],
        'include_file': os.path.basename(hpath),
    }

    with open(cpath, 'w', encoding='utf-8') as f:
        try:
            result = f.write(Template(TEMPLATE_C).render(**environment))
        except Exception:
            from mako import exceptions
            print(exceptions.text_error_template().render(), file=sys.stderr)
            sys.exit(1)
    with open(hpath, 'w', encoding='utf-8') as f:
        try:
            result = f.write(Template(TEMPLATE_H).render(**environment))
        except Exception:
            from mako import exceptions
            print(exceptions.text_error_template().render(), file=sys.stderr)
            sys.exit(1)

TEMPLATE_RST = """\
.. This file is autogenerated by drirc_gen.py — do not edit.
% for section in sections:

${section.name}
${'~' * len(section.name)}

.. list-table::
   :header-rows: 1
   :widths: auto

   * - Option
     - Type
     - Default
     - Range / Values
     - Description
%   for opt in section.options:
   * - .. _drirc-${opt.name}:

       ``${opt.name}``
     - ${type_to_documentation(opt.dtype)}
     - ${opt_default(opt)}
     - ${opt_range_or_values(opt)}
     - ${opt.description}
%   endfor
% endfor
"""

def _opt_default_rst(opt) -> str:
    if isinstance(opt, DrircBool):
        return "``true``" if opt.value else "``false``"
    if isinstance(opt, (DrircInt, DrircUint64, DrircFloat)):
        return f"``{opt.value}``"
    if isinstance(opt, DrircString):
        return f"``{opt.value}``" if opt.value is not None else "*(none)*"
    if isinstance(opt, DrircEnum):
        return f"``{opt.value}``"
    return ""

def _opt_range_or_values_rst(opt) -> str:
    if isinstance(opt, (DrircInt, DrircUint64, DrircFloat)):
        return f"``{opt.min_value}`` – ``{opt.max_value}``"
    if isinstance(opt, DrircEnum):
        return "; ".join(f"``{v.value}`` — {v.description}" for v in opt.values)
    return ""

def drirc_generate_rst(rst_path: str, sections: list) -> None:
    environment = {
        'sections':            sections,
        'type_to_documentation': lambda dtype: type_to_documentation[dtype],
        'opt_default':         _opt_default_rst,
        'opt_range_or_values': _opt_range_or_values_rst,
    }
    with open(rst_path, 'w', encoding='utf-8') as f:
        try:
            f.write(Template(TEMPLATE_RST).render(**environment))
        except Exception:
            from mako import exceptions
            print(exceptions.text_error_template().render(), file=sys.stderr)
            sys.exit(1)

def add_common_vk_options(debug_options, features_options, misc_options, valid_options, defaults=None):
    B = DrircBool
    I = DrircInt
    F = DrircFloat
    S = DrircString

    if defaults is None:
        defaults = {}

    debug_options.extend([opt for opt in [
        I("force_vk_vendor", defaults.get("force_vk_vendor", 0), -1, 2147483647,
          "Override GPU vendor id",
          c_name="force_vk_vendor"),
        S("force_vk_devicename", defaults.get("force_vk_devicename", None),
          "Override Vulkan deviceName.",
          c_name="force_vk_devicename"),
        B("vk_lower_terminate_to_discard", defaults.get("vk_lower_terminate_to_discard", False),
          "Lower terminate to discard (which is implicitly demote)",
          c_name="lower_terminate_to_discard"),
        B("vk_zero_vram", defaults.get("vk_zero_vram", False),
          "Initialize to zero all VRAM allocations",
          c_name="zero_vram"),
    ] if opt.name in valid_options])

    features_options.extend([opt for opt in [
        B("vk_require_etc2", defaults.get("vk_require_etc2", False),
          "Implement emulated ETC2 on HW that does not support it",
          c_name="require_etc2"),
        B("vk_require_astc", defaults.get("vk_require_astc", False),
          "Implement emulated ASTC on HW that does not support it",
          c_name="require_astc"),
    ] if opt.name in valid_options])

    misc_options.extend([opt for opt in [
        F("heap_memory_percent", defaults.get("heap_memory_percent", 0.0), 0.0, 1.0,
          "Percentage of total system memory to report as gpu heap memory (0 = driver default)",
          c_name="heap_memory_percent"),
    ] if opt.name in valid_options])

def add_common_vk_wsi_options(debug_options, performance_options, defaults=None):
    B = DrircBool
    I = DrircInt

    if defaults is None:
        defaults = {}

    performance_options.extend([
        B("adaptive_sync", defaults.get("adaptive_sync", True),
          "Adapt the monitor sync to the application performance (when possible)"),
        I("vk_x11_override_min_image_count", defaults.get("vk_x11_override_min_image_count", 0), 0, 999,
          "Override the VkSurfaceCapabilitiesKHR::minImageCount (0 = no override)"),
        B("vk_x11_strict_image_count", defaults.get("vk_x11_strict_image_count", False),
          "Force the X11 WSI to create exactly the number of image specified by the application in VkSwapchainCreateInfoKHR::minImageCount"),
        B("vk_x11_ensure_min_image_count", defaults.get("vk_x11_ensure_min_image_count", False),
          "Force the X11 WSI to create at least the number of image specified by the driver in VkSurfaceCapabilitiesKHR::minImageCount"),
        B("vk_google_display_timing", defaults.get("vk_google_display_timing", False),
          "Expose VK_GOOGLE_display_timing even if fully conformant support can't be guaranteed on all enabled surface types, e.g., (X)Wayland"),
    ])

    debug_options.extend([
        B("vk_wsi_force_bgra8_unorm_first", defaults.get("vk_wsi_force_bgra8_unorm_first", False),
          "Force vkGetPhysicalDeviceSurfaceFormatsKHR to return VK_FORMAT_B8G8R8A8_UNORM as the first format"),
        B("vk_wsi_force_swapchain_to_current_extent", defaults.get("vk_wsi_force_swapchain_to_current_extent", False),
          "Force VkSwapchainCreateInfoKHR::imageExtent to be VkSurfaceCapabilities2KHR::currentExtent"),
        B("vk_wsi_disable_unordered_submits", defaults.get("vk_wsi_disable_unordered_submits", False),
          "Disable unordered WSI submits to workaround application synchronization bugs"),
        B("vk_x11_ignore_suboptimal", defaults.get("vk_x11_ignore_suboptimal", False),
          "Force the X11 WSI to never report VK_SUBOPTIMAL_KHR"),
    ])
