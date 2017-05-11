/****************************************************************************
* Copyright (C) 2014-2015 Intel Corporation.   All Rights Reserved.
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
*
* @file multisample.cpp
*
******************************************************************************/

#include "multisample.h"
constexpr uint32_t MultisampleTraits<SWR_MULTISAMPLE_1X>::samplePosXi[1];
constexpr uint32_t MultisampleTraits<SWR_MULTISAMPLE_1X>::samplePosYi[1];
constexpr uint32_t MultisampleTraits<SWR_MULTISAMPLE_2X>::samplePosXi[2];
constexpr uint32_t MultisampleTraits<SWR_MULTISAMPLE_2X>::samplePosYi[2];
constexpr uint32_t MultisampleTraits<SWR_MULTISAMPLE_4X>::samplePosXi[4];
constexpr uint32_t MultisampleTraits<SWR_MULTISAMPLE_4X>::samplePosYi[4];
constexpr uint32_t MultisampleTraits<SWR_MULTISAMPLE_8X>::samplePosXi[8];
constexpr uint32_t MultisampleTraits<SWR_MULTISAMPLE_8X>::samplePosYi[8];
constexpr uint32_t MultisampleTraits<SWR_MULTISAMPLE_16X>::samplePosXi[16];
constexpr uint32_t MultisampleTraits<SWR_MULTISAMPLE_16X>::samplePosYi[16];

constexpr float MultisampleTraits<SWR_MULTISAMPLE_1X>::samplePosX[1];
constexpr float MultisampleTraits<SWR_MULTISAMPLE_1X>::samplePosY[1];
constexpr float MultisampleTraits<SWR_MULTISAMPLE_2X>::samplePosX[2];
constexpr float MultisampleTraits<SWR_MULTISAMPLE_2X>::samplePosY[2];
constexpr float MultisampleTraits<SWR_MULTISAMPLE_4X>::samplePosX[4];
constexpr float MultisampleTraits<SWR_MULTISAMPLE_4X>::samplePosY[4];
constexpr float MultisampleTraits<SWR_MULTISAMPLE_8X>::samplePosX[8];
constexpr float MultisampleTraits<SWR_MULTISAMPLE_8X>::samplePosY[8];
constexpr float MultisampleTraits<SWR_MULTISAMPLE_16X>::samplePosX[16];
constexpr float MultisampleTraits<SWR_MULTISAMPLE_16X>::samplePosY[16];
