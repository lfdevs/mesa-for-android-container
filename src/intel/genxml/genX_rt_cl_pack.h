/* Copyright © 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#ifndef GENX_RT_CL_PACK_H
#define GENX_RT_CL_PACK_H

#ifndef GFX_VERx10
#  error "The GFX_VERx10 macro must be defined"
#endif

#if (GFX_VERx10 < 125)
/* No RT support for those gfx ver */
#elif (GFX_VERx10 == 125)
#  include "genxml/gen125_rt_cl_pack.h"
#elif (GFX_VERx10 == 200)
#  include "genxml/xe2_rt_cl_pack.h"
#elif (GFX_VERx10 == 300)
#  include "genxml/xe3_rt_cl_pack.h"
#elif (GFX_VERx10 == 350)
#  include "genxml/xe3p_rt_cl_pack.h"
#else
#  error "Need to add a pack header include for this gen"
#endif

#endif /* GENX_RT_CL_PACK_H */
