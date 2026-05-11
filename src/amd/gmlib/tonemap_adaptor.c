/* Copyright 2025 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 *
 * Authors: AMD
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "ToneMapGenerator.h"
#include "AGMGenerator.h"
#include "tonemap_adaptor.h"

static void VPEFree3DLut(void* memToFree, void* pDevice)
{
   free(memToFree);
}

static void* VPEAlloc3DLut(unsigned int allocSize, void* pDevice)
{
    return calloc(1, allocSize);
}

void* tm_create(void)
{
    struct ToneMapGenerator* p_tmGenerator = (struct ToneMapGenerator*)calloc(1, sizeof(struct ToneMapGenerator));
    if (!p_tmGenerator)
        return NULL;

    p_tmGenerator->tmAlgo = TMG_A_AGM;
    p_tmGenerator->memAllocSet = false;
    p_tmGenerator->agmGenerator.initalized = false;

    return (void*)p_tmGenerator;
}

void tm_destroy(void** pp_tmGenerator)
{
    struct ToneMapGenerator* p_tmGenerator;

    if (!pp_tmGenerator || ((*pp_tmGenerator) == NULL))
        return;

    p_tmGenerator = *pp_tmGenerator;
    AGMGenerator_Exit(&p_tmGenerator->agmGenerator);

    free(p_tmGenerator);
    *pp_tmGenerator = NULL;
}

int tm_generate3DLut(struct tonemap_param* pInparam, void* pLutData)
{
    enum TMGReturnCode               result;
    struct ToneMappingParameters     tmParams;

    tmParams.lutData = (uint16_t *)pLutData;

    ToneMapGenerator_SetInternalAllocators(
                    (struct ToneMapGenerator*)pInparam->tm_handle,
                    (TMGAlloc)(VPEAlloc3DLut),
                    (TMGFree)(VPEFree3DLut),
                    (void*)(NULL));

    result = ToneMapGenerator_GenerateToneMappingParameters(
                    (struct ToneMapGenerator*)pInparam->tm_handle,
                    &pInparam->streamMetaData,
                    &pInparam->dstMetaData,
                    pInparam->inputContainerGamma,
                    pInparam->outputContainerGamma,
                    pInparam->outputContainerPrimaries,
                    pInparam->lutDim,
                    &tmParams
    );

    return (int)result;
}

int tm_generate_formatted_3DLut(
         uint16_t* pCpuLutData,
         int cpuLutDim,
         int gpuLutContainerDim,
         const float bitDepthCpu,
         const uint32_t bitDepthGpu,
         void* pGpuLutData)
{
   int result = 1;
   /* HardCode: use 256bits alignment */
   int AlignedLutContainerDim = ((gpuLutContainerDim + 32) >> 5) * 32;

   if ((NULL != pCpuLutData) && (NULL != pGpuLutData)) {
      uint16_t* pGpuData     = (uint16_t *)pGpuLutData;
      int       cpuSurfIndex = 0;
      int       gpuSurfIndex = 0;
      int       widthSize    = AlignedLutContainerDim * 4;
      int       sliceSize    = widthSize * gpuLutContainerDim;
      int       i, j, k;

      for (i = 0; i < cpuLutDim; i++) {
         for (j = 0; j < cpuLutDim; j++) {
            for (k = 0; k < cpuLutDim; k++) {
               cpuSurfIndex = i * cpuLutDim * cpuLutDim * 3 + j * cpuLutDim * 3 + k * 3;
               gpuSurfIndex = i * sliceSize + j * widthSize + k * 4;
               *(pGpuData + gpuSurfIndex + 0) = (uint16_t)(((float)(pCpuLutData[cpuSurfIndex + 0]) / (float)bitDepthCpu) * bitDepthGpu);
               *(pGpuData + gpuSurfIndex + 1) = (uint16_t)(((float)(pCpuLutData[cpuSurfIndex + 1]) / (float)bitDepthCpu) * bitDepthGpu);
               *(pGpuData + gpuSurfIndex + 2) = (uint16_t)(((float)(pCpuLutData[cpuSurfIndex + 2]) / (float)bitDepthCpu) * bitDepthGpu);
            }
         }
      }
      result = 0;
   }
   return result;
}
