/*
 * Copyright (C) 2017 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include "spirv_info.h"

const char *
spirv_capability_to_string(SpvCapability v)
{
   switch (v) {
   case SpvCapabilityMatrix: return "SpvCapabilityMatrix";
   case SpvCapabilityShader: return "SpvCapabilityShader";
   case SpvCapabilityGeometry: return "SpvCapabilityGeometry";
   case SpvCapabilityTessellation: return "SpvCapabilityTessellation";
   case SpvCapabilityAddresses: return "SpvCapabilityAddresses";
   case SpvCapabilityLinkage: return "SpvCapabilityLinkage";
   case SpvCapabilityKernel: return "SpvCapabilityKernel";
   case SpvCapabilityVector16: return "SpvCapabilityVector16";
   case SpvCapabilityFloat16Buffer: return "SpvCapabilityFloat16Buffer";
   case SpvCapabilityFloat16: return "SpvCapabilityFloat16";
   case SpvCapabilityFloat64: return "SpvCapabilityFloat64";
   case SpvCapabilityInt64: return "SpvCapabilityInt64";
   case SpvCapabilityInt64Atomics: return "SpvCapabilityInt64Atomics";
   case SpvCapabilityImageBasic: return "SpvCapabilityImageBasic";
   case SpvCapabilityImageReadWrite: return "SpvCapabilityImageReadWrite";
   case SpvCapabilityImageMipmap: return "SpvCapabilityImageMipmap";
   case SpvCapabilityPipes: return "SpvCapabilityPipes";
   case SpvCapabilityGroups: return "SpvCapabilityGroups";
   case SpvCapabilityDeviceEnqueue: return "SpvCapabilityDeviceEnqueue";
   case SpvCapabilityLiteralSampler: return "SpvCapabilityLiteralSampler";
   case SpvCapabilityAtomicStorage: return "SpvCapabilityAtomicStorage";
   case SpvCapabilityInt16: return "SpvCapabilityInt16";
   case SpvCapabilityTessellationPointSize: return "SpvCapabilityTessellationPointSize";
   case SpvCapabilityGeometryPointSize: return "SpvCapabilityGeometryPointSize";
   case SpvCapabilityImageGatherExtended: return "SpvCapabilityImageGatherExtended";
   case SpvCapabilityStorageImageMultisample: return "SpvCapabilityStorageImageMultisample";
   case SpvCapabilityUniformBufferArrayDynamicIndexing: return "SpvCapabilityUniformBufferArrayDynamicIndexing";
   case SpvCapabilitySampledImageArrayDynamicIndexing: return "SpvCapabilitySampledImageArrayDynamicIndexing";
   case SpvCapabilityStorageBufferArrayDynamicIndexing: return "SpvCapabilityStorageBufferArrayDynamicIndexing";
   case SpvCapabilityStorageImageArrayDynamicIndexing: return "SpvCapabilityStorageImageArrayDynamicIndexing";
   case SpvCapabilityClipDistance: return "SpvCapabilityClipDistance";
   case SpvCapabilityCullDistance: return "SpvCapabilityCullDistance";
   case SpvCapabilityImageCubeArray: return "SpvCapabilityImageCubeArray";
   case SpvCapabilitySampleRateShading: return "SpvCapabilitySampleRateShading";
   case SpvCapabilityImageRect: return "SpvCapabilityImageRect";
   case SpvCapabilitySampledRect: return "SpvCapabilitySampledRect";
   case SpvCapabilityGenericPointer: return "SpvCapabilityGenericPointer";
   case SpvCapabilityInt8: return "SpvCapabilityInt8";
   case SpvCapabilityInputAttachment: return "SpvCapabilityInputAttachment";
   case SpvCapabilitySparseResidency: return "SpvCapabilitySparseResidency";
   case SpvCapabilityMinLod: return "SpvCapabilityMinLod";
   case SpvCapabilitySampled1D: return "SpvCapabilitySampled1D";
   case SpvCapabilityImage1D: return "SpvCapabilityImage1D";
   case SpvCapabilitySampledCubeArray: return "SpvCapabilitySampledCubeArray";
   case SpvCapabilitySampledBuffer: return "SpvCapabilitySampledBuffer";
   case SpvCapabilityImageBuffer: return "SpvCapabilityImageBuffer";
   case SpvCapabilityImageMSArray: return "SpvCapabilityImageMSArray";
   case SpvCapabilityStorageImageExtendedFormats: return "SpvCapabilityStorageImageExtendedFormats";
   case SpvCapabilityImageQuery: return "SpvCapabilityImageQuery";
   case SpvCapabilityDerivativeControl: return "SpvCapabilityDerivativeControl";
   case SpvCapabilityInterpolationFunction: return "SpvCapabilityInterpolationFunction";
   case SpvCapabilityTransformFeedback: return "SpvCapabilityTransformFeedback";
   case SpvCapabilityGeometryStreams: return "SpvCapabilityGeometryStreams";
   case SpvCapabilityStorageImageReadWithoutFormat: return "SpvCapabilityStorageImageReadWithoutFormat";
   case SpvCapabilityStorageImageWriteWithoutFormat: return "SpvCapabilityStorageImageWriteWithoutFormat";
   case SpvCapabilityMultiViewport: return "SpvCapabilityMultiViewport";
   case SpvCapabilitySubgroupDispatch: return "SpvCapabilitySubgroupDispatch";
   case SpvCapabilityNamedBarrier: return "SpvCapabilityNamedBarrier";
   case SpvCapabilityPipeStorage: return "SpvCapabilityPipeStorage";
   case SpvCapabilitySubgroupBallotKHR: return "SpvCapabilitySubgroupBallotKHR";
   case SpvCapabilityDrawParameters: return "SpvCapabilityDrawParameters";
   case SpvCapabilitySubgroupVoteKHR: return "SpvCapabilitySubgroupVoteKHR";
   case SpvCapabilityStorageBuffer16BitAccess: return "SpvCapabilityStorageBuffer16BitAccess";
   case SpvCapabilityUniformAndStorageBuffer16BitAccess: return "SpvCapabilityUniformAndStorageBuffer16BitAccess";
   case SpvCapabilityStoragePushConstant16: return "SpvCapabilityStoragePushConstant16";
   case SpvCapabilityStorageInputOutput16: return "SpvCapabilityStorageInputOutput16";
   case SpvCapabilityDeviceGroup: return "SpvCapabilityDeviceGroup";
   case SpvCapabilityMultiView: return "SpvCapabilityMultiView";
   case SpvCapabilityVariablePointersStorageBuffer: return "SpvCapabilityVariablePointersStorageBuffer";
   case SpvCapabilityVariablePointers: return "SpvCapabilityVariablePointers";
   case SpvCapabilityAtomicStorageOps: return "SpvCapabilityAtomicStorageOps";
   case SpvCapabilitySampleMaskPostDepthCoverage: return "SpvCapabilitySampleMaskPostDepthCoverage";
   case SpvCapabilityImageGatherBiasLodAMD: return "SpvCapabilityImageGatherBiasLodAMD";
   case SpvCapabilitySampleMaskOverrideCoverageNV: return "SpvCapabilitySampleMaskOverrideCoverageNV";
   case SpvCapabilityGeometryShaderPassthroughNV: return "SpvCapabilityGeometryShaderPassthroughNV";
   case SpvCapabilityShaderViewportIndexLayerNV: return "SpvCapabilityShaderViewportIndexLayerNV";
   case SpvCapabilityShaderViewportMaskNV: return "SpvCapabilityShaderViewportMaskNV";
   case SpvCapabilityShaderStereoViewNV: return "SpvCapabilityShaderStereoViewNV";
   case SpvCapabilityPerViewAttributesNV: return "SpvCapabilityPerViewAttributesNV";
   case SpvCapabilityMax: break; /* silence warnings about unhandled enums. */
   }

   return "unknown";
}

const char *
spirv_decoration_to_string(SpvDecoration v)
{
   switch (v) {
   case SpvDecorationRelaxedPrecision: return "SpvDecorationRelaxedPrecision";
   case SpvDecorationSpecId: return "SpvDecorationSpecId";
   case SpvDecorationBlock: return "SpvDecorationBlock";
   case SpvDecorationBufferBlock: return "SpvDecorationBufferBlock";
   case SpvDecorationRowMajor: return "SpvDecorationRowMajor";
   case SpvDecorationColMajor: return "SpvDecorationColMajor";
   case SpvDecorationArrayStride: return "SpvDecorationArrayStride";
   case SpvDecorationMatrixStride: return "SpvDecorationMatrixStride";
   case SpvDecorationGLSLShared: return "SpvDecorationGLSLShared";
   case SpvDecorationGLSLPacked: return "SpvDecorationGLSLPacked";
   case SpvDecorationCPacked: return "SpvDecorationCPacked";
   case SpvDecorationBuiltIn: return "SpvDecorationBuiltIn";
   case SpvDecorationNoPerspective: return "SpvDecorationNoPerspective";
   case SpvDecorationFlat: return "SpvDecorationFlat";
   case SpvDecorationPatch: return "SpvDecorationPatch";
   case SpvDecorationCentroid: return "SpvDecorationCentroid";
   case SpvDecorationSample: return "SpvDecorationSample";
   case SpvDecorationInvariant: return "SpvDecorationInvariant";
   case SpvDecorationRestrict: return "SpvDecorationRestrict";
   case SpvDecorationAliased: return "SpvDecorationAliased";
   case SpvDecorationVolatile: return "SpvDecorationVolatile";
   case SpvDecorationConstant: return "SpvDecorationConstant";
   case SpvDecorationCoherent: return "SpvDecorationCoherent";
   case SpvDecorationNonWritable: return "SpvDecorationNonWritable";
   case SpvDecorationNonReadable: return "SpvDecorationNonReadable";
   case SpvDecorationUniform: return "SpvDecorationUniform";
   case SpvDecorationSaturatedConversion: return "SpvDecorationSaturatedConversion";
   case SpvDecorationStream: return "SpvDecorationStream";
   case SpvDecorationLocation: return "SpvDecorationLocation";
   case SpvDecorationComponent: return "SpvDecorationComponent";
   case SpvDecorationIndex: return "SpvDecorationIndex";
   case SpvDecorationBinding: return "SpvDecorationBinding";
   case SpvDecorationDescriptorSet: return "SpvDecorationDescriptorSet";
   case SpvDecorationOffset: return "SpvDecorationOffset";
   case SpvDecorationXfbBuffer: return "SpvDecorationXfbBuffer";
   case SpvDecorationXfbStride: return "SpvDecorationXfbStride";
   case SpvDecorationFuncParamAttr: return "SpvDecorationFuncParamAttr";
   case SpvDecorationFPRoundingMode: return "SpvDecorationFPRoundingMode";
   case SpvDecorationFPFastMathMode: return "SpvDecorationFPFastMathMode";
   case SpvDecorationLinkageAttributes: return "SpvDecorationLinkageAttributes";
   case SpvDecorationNoContraction: return "SpvDecorationNoContraction";
   case SpvDecorationInputAttachmentIndex: return "SpvDecorationInputAttachmentIndex";
   case SpvDecorationAlignment: return "SpvDecorationAlignment";
   case SpvDecorationMaxByteOffset: return "SpvDecorationMaxByteOffset";
   case SpvDecorationAlignmentId: return "SpvDecorationAlignmentId";
   case SpvDecorationMaxByteOffsetId: return "SpvDecorationMaxByteOffsetId";
   case SpvDecorationExplicitInterpAMD: return "SpvDecorationExplicitInterpAMD";
   case SpvDecorationOverrideCoverageNV: return "SpvDecorationOverrideCoverageNV";
   case SpvDecorationPassthroughNV: return "SpvDecorationPassthroughNV";
   case SpvDecorationViewportRelativeNV: return "SpvDecorationViewportRelativeNV";
   case SpvDecorationSecondaryViewportRelativeNV: return "SpvDecorationSecondaryViewportRelativeNV";
   case SpvDecorationMax: break; /* silence warnings about unhandled enums. */
   }

   return "unknown";
}
