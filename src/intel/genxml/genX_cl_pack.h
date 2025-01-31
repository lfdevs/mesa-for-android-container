/* Copyright © 2023 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#ifndef GENX_CL_PACK_H
#define GENX_CL_PACK_H

#ifndef GFX_VERx10
#  error "The GFX_VERx10 macro must be defined"
#endif

#if (GFX_VERx10 == 40)
#  include "genxml/gen40_cl_pack.h"
#elif (GFX_VERx10 == 45)
#  include "genxml/gen45_cl_pack.h"
#elif (GFX_VERx10 == 50)
#  include "genxml/gen50_cl_pack.h"
#elif (GFX_VERx10 == 60)
#  include "genxml/gen60_cl_pack.h"
#elif (GFX_VERx10 == 70)
#  include "genxml/gen70_cl_pack.h"
#elif (GFX_VERx10 == 75)
#  include "genxml/gen75_cl_pack.h"
#elif (GFX_VERx10 == 80)
#  include "genxml/gen80_cl_pack.h"
#elif (GFX_VERx10 == 90)
#  include "genxml/gen90_cl_pack.h"
#elif (GFX_VERx10 == 110)
#  include "genxml/gen110_cl_pack.h"
#elif (GFX_VERx10 == 120)
#  include "genxml/gen120_cl_pack.h"
#elif (GFX_VERx10 == 125)
#  include "genxml/gen125_cl_pack.h"
#elif (GFX_VERx10 == 200)
#  include "genxml/gen200_cl_pack.h"
#elif (GFX_VERx10 == 300)
#  include "genxml/gen300_cl_pack.h"
#else
#  error "Need to add a pack header include for this gen"
#endif

#endif /* GENX_CL_PACK_H */
