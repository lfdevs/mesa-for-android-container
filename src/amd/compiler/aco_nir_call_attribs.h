/*
 * Copyright © 2024 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef ACO_NIR_CALL_ATTRIBS_H
#define ACO_NIR_CALL_ATTRIBS_H

enum aco_nir_call_abi {
   ACO_NIR_CALL_ABI_RT_RECURSIVE,
   ACO_NIR_CALL_ABI_TRAVERSAL,
   ACO_NIR_CALL_ABI_AHIT_ISEC,
};

enum aco_nir_function_attribs {
   ACO_NIR_FUNCTION_ATTRIB_ABI_MASK = 0x7F,
   /* Different lanes can have different values for the function pointer to call */
   ACO_NIR_FUNCTION_ATTRIB_DIVERGENT_CALL = 0x1 << 7,
   /* Function will never return */
   ACO_NIR_FUNCTION_ATTRIB_NORETURN = 0x2 << 7,
};

enum aco_nir_parameter_attribs {
   /* Parameter value is not used by any callee and does not need to be preserved */
   ACO_NIR_PARAM_ATTRIB_DISCARDABLE = 0x1,
};

#endif /* ACO_NIR_CALL_ATTRIBS_H */
