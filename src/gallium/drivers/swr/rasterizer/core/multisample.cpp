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
const uint32_t MultisampleTraits<SWR_MULTISAMPLE_1X>::samplePosXi {0x80};
const uint32_t MultisampleTraits<SWR_MULTISAMPLE_1X>::samplePosYi {0x80};
const uint32_t MultisampleTraits<SWR_MULTISAMPLE_2X>::samplePosXi[2] {0xC0, 0x40};
const uint32_t MultisampleTraits<SWR_MULTISAMPLE_2X>::samplePosYi[2] {0xC0, 0x40};
const uint32_t MultisampleTraits<SWR_MULTISAMPLE_4X>::samplePosXi[4] {0x60, 0xE0, 0x20, 0xA0};
const uint32_t MultisampleTraits<SWR_MULTISAMPLE_4X>::samplePosYi[4] {0x20, 0x60, 0xA0, 0xE0};
const uint32_t MultisampleTraits<SWR_MULTISAMPLE_8X>::samplePosXi[8] {0x90, 0x70, 0xD0, 0x50, 0x30, 0x10, 0xB0, 0xF0};
const uint32_t MultisampleTraits<SWR_MULTISAMPLE_8X>::samplePosYi[8] {0x50, 0xB0, 0x90, 0x30, 0xD0, 0x70, 0xF0, 0x10};
const uint32_t MultisampleTraits<SWR_MULTISAMPLE_16X>::samplePosXi[16] 
{0x90, 0x70, 0x50, 0xC0, 0x30, 0xA0, 0xD0, 0xB0, 0x60, 0x80, 0x40, 0x20, 0x00, 0xF0, 0xE0, 0x10};
const uint32_t MultisampleTraits<SWR_MULTISAMPLE_16X>::samplePosYi[16]
{0x90, 0x50, 0xA0, 0x70, 0x60, 0xD0, 0xB0, 0x30, 0xE0, 0x10, 0x20, 0xC0, 0x80, 0x40, 0xF0, 0x00};

const float MultisampleTraits<SWR_MULTISAMPLE_1X>::samplePosX{0.5f};
const float MultisampleTraits<SWR_MULTISAMPLE_1X>::samplePosY{0.5f};
const float MultisampleTraits<SWR_MULTISAMPLE_2X>::samplePosX[2]{0.75f, 0.25f};
const float MultisampleTraits<SWR_MULTISAMPLE_2X>::samplePosY[2]{0.75f, 0.25f};
const float MultisampleTraits<SWR_MULTISAMPLE_4X>::samplePosX[4]{0.375f, 0.875, 0.125, 0.625};
const float MultisampleTraits<SWR_MULTISAMPLE_4X>::samplePosY[4]{0.125, 0.375, 0.625, 0.875};
const float MultisampleTraits<SWR_MULTISAMPLE_8X>::samplePosX[8]{0.5625, 0.4375, 0.8125, 0.3125, 0.1875, 0.0625, 0.6875, 0.9375};
const float MultisampleTraits<SWR_MULTISAMPLE_8X>::samplePosY[8]{0.3125, 0.6875, 0.5625, 0.1875, 0.8125, 0.4375, 0.9375, 0.0625};
const float MultisampleTraits<SWR_MULTISAMPLE_16X>::samplePosX[16]
{0.5625, 0.4375, 0.3125, 0.7500, 0.1875, 0.6250, 0.8125, 0.6875, 0.3750, 0.5000, 0.2500, 0.1250, 0.0000, 0.9375, 0.8750, 0.0625};
const float MultisampleTraits<SWR_MULTISAMPLE_16X>::samplePosY[16]
{0.5625, 0.3125, 0.6250, 0.4375, 0.3750, 0.8125, 0.6875, 0.1875, 0.8750, 0.0625, 0.1250, 0.7500, 0.5000, 0.2500, 0.9375, 0.0000};

const float MultisampleTraits<SWR_MULTISAMPLE_1X, SWR_MSAA_CENTER_PATTERN>::samplePosX{ 0.5f };
const float MultisampleTraits<SWR_MULTISAMPLE_1X, SWR_MSAA_CENTER_PATTERN>::samplePosY{ 0.5f };
const float MultisampleTraits<SWR_MULTISAMPLE_2X, SWR_MSAA_CENTER_PATTERN>::samplePosX[2]{ 0.5f, 0.5f};
const float MultisampleTraits<SWR_MULTISAMPLE_2X, SWR_MSAA_CENTER_PATTERN>::samplePosY[2]{ 0.5f, 0.5f};
const float MultisampleTraits<SWR_MULTISAMPLE_4X, SWR_MSAA_CENTER_PATTERN>::samplePosX[4]{ 0.5f, 0.5f, 0.5f, 0.5f};
const float MultisampleTraits<SWR_MULTISAMPLE_4X, SWR_MSAA_CENTER_PATTERN>::samplePosY[4]{ 0.5f, 0.5f, 0.5f, 0.5f };
const float MultisampleTraits<SWR_MULTISAMPLE_8X, SWR_MSAA_CENTER_PATTERN>::samplePosX[8]{ 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
const float MultisampleTraits<SWR_MULTISAMPLE_8X, SWR_MSAA_CENTER_PATTERN>::samplePosY[8]{ 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
const float MultisampleTraits<SWR_MULTISAMPLE_16X, SWR_MSAA_CENTER_PATTERN>::samplePosX[16]
{ 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };
const float MultisampleTraits<SWR_MULTISAMPLE_16X, SWR_MSAA_CENTER_PATTERN>::samplePosY[16]
{ 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };
