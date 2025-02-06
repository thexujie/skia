/*
 * Copyright 2022 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "tests/Test.h"

#include "include/core/SkBlendMode.h"
#include "src/base/SkArenaAlloc.h"
#include "src/gpu/Swizzle.h"
#include "src/gpu/graphite/ContextPriv.h"
#include "src/gpu/graphite/ContextUtils.h"
#include "src/gpu/graphite/PaintParamsKey.h"
#include "src/gpu/graphite/RendererProvider.h"
#include "src/gpu/graphite/ShaderCodeDictionary.h"
#include "src/gpu/graphite/ShaderInfo.h"

using namespace skgpu::graphite;

namespace {

void add_block(PaintParamsKeyBuilder* builder, int snippetID) {
    builder->beginBlock(snippetID);
    builder->endBlock();
}

PaintParamsKey create_key(const ShaderCodeDictionary* dict, int snippetID, SkArenaAlloc* arena) {
    PaintParamsKeyBuilder builder{dict};
    add_block(&builder, snippetID);

    AutoLockBuilderAsKey keyView{&builder};
    return keyView->clone(arena);
}

bool coeff_equal(SkBlendModeCoeff skCoeff, skgpu::BlendCoeff gpuCoeff) {
    switch(skCoeff) {
        case SkBlendModeCoeff::kZero: return skgpu::BlendCoeff::kZero == gpuCoeff;
        case SkBlendModeCoeff::kOne:  return skgpu::BlendCoeff::kOne == gpuCoeff;
        case SkBlendModeCoeff::kSC:   return skgpu::BlendCoeff::kSC == gpuCoeff;
        case SkBlendModeCoeff::kISC:  return skgpu::BlendCoeff::kISC == gpuCoeff;
        case SkBlendModeCoeff::kDC:   return skgpu::BlendCoeff::kDC == gpuCoeff;
        case SkBlendModeCoeff::kIDC:  return skgpu::BlendCoeff::kIDC == gpuCoeff;
        case SkBlendModeCoeff::kSA:   return skgpu::BlendCoeff::kSA == gpuCoeff;
        case SkBlendModeCoeff::kISA:  return skgpu::BlendCoeff::kISA == gpuCoeff;
        case SkBlendModeCoeff::kDA:   return skgpu::BlendCoeff::kDA == gpuCoeff;
        case SkBlendModeCoeff::kIDA:  return skgpu::BlendCoeff::kIDA == gpuCoeff;
        default:                      return false;
    }
}

} // anonymous namespace

// These are intended to be unit tests of the PaintParamsKeyBuilder and PaintParamsKey.
DEF_GRAPHITE_TEST_FOR_ALL_CONTEXTS(KeyWithInvalidCodeSnippetIDTest, reporter, context,
                                   CtsEnforcement::kApiLevel_202404) {
    SkArenaAlloc arena{256};
    ShaderCodeDictionary* dict = context->priv().shaderCodeDictionary();

    // A builder without any data is invalid. The Builder and the PaintParamKeys can include
    // invalid IDs without themselves becoming invalid. Normally adding an invalid ID triggers an
    // assert in debug builds, since the properly functioning key system should never encounter an
    // invalid ID.
    PaintParamsKeyBuilder builder(dict);
    AutoLockBuilderAsKey keyView{&builder};
    REPORTER_ASSERT(reporter, !keyView->isValid());
    REPORTER_ASSERT(reporter, !PaintParamsKey::Invalid().isValid());

    // However, if the program gets in a malformed state on release builds, the key
    // could contain an invalid ID. In that case the invalid snippet IDs are detected when
    // reconstructing the key into an effect tree for SkSL generation. To test this, we manually
    // construct an invalid span and test that it returns a null shader node tree when treated as
    // a PaintParamsKey.
    // NOTE: This is intentionally abusing memory to create a corrupt scenario and is dependent on
    // the structure of PaintParamsKey (just SkSpan<const int32_t>).
    int32_t invalidKeyData[3] = {(int32_t) BuiltInCodeSnippetID::kSolidColorShader,
                                 SkKnownRuntimeEffects::kSkiaBuiltInReservedCnt - 1,
                                 (int32_t) BuiltInCodeSnippetID::kFixedBlend_Src};
    SkSpan<const int32_t> invalidKeySpan{invalidKeyData, std::size(invalidKeyData)*sizeof(int32_t)};
    const PaintParamsKey* fakeKey = reinterpret_cast<const PaintParamsKey*>(&invalidKeySpan);
    REPORTER_ASSERT(reporter, fakeKey->getRootNodes(dict, &arena).empty());
}

DEF_GRAPHITE_TEST_FOR_ALL_CONTEXTS(KeyEqualityChecksSnippetID, reporter, context,
                                   CtsEnforcement::kApiLevel_202404) {
    SkArenaAlloc arena{256};
    ShaderCodeDictionary* dict = context->priv().shaderCodeDictionary();

    PaintParamsKey keyA = create_key(dict, (int) BuiltInCodeSnippetID::kSolidColorShader, &arena);
    PaintParamsKey keyB = create_key(dict, (int) BuiltInCodeSnippetID::kSolidColorShader, &arena);
    PaintParamsKey keyC = create_key(dict, (int) BuiltInCodeSnippetID::kRGBPaintColor, &arena);

    // Verify that keyA matches keyB, and that it does not match keyC.
    REPORTER_ASSERT(reporter, keyA == keyB);
    REPORTER_ASSERT(reporter, keyA != keyC);
    REPORTER_ASSERT(reporter, !(keyA == keyC));
    REPORTER_ASSERT(reporter, !(keyA != keyB));
}

DEF_GRAPHITE_TEST_FOR_ALL_CONTEXTS(ShaderInfoDetectsFixedFunctionBlend, reporter, context,
                                   CtsEnforcement::kApiLevel_202404) {
    ShaderCodeDictionary* dict = context->priv().shaderCodeDictionary();

    // For determining the DstReadStrategy, pass in reasonable render target texture properties.
    const Caps* caps = context->priv().caps();

    // Use reasonable render target texture information to query Caps for the DstReadStrategy to use
    // should a dst read be required by any of the paints encountered.
    // TODO(b/390458117): Once the TextureInfo argument is removed from getDstReadStrategy, this is
    // no longer necessary.
    DstReadStrategy dstReadStrategy = caps->getDstReadStrategy(
            caps->getDefaultSampledTextureInfo(SkColorType::kRGBA_8888_SkColorType,
                                               skgpu::Mipmapped::kNo,
                                               skgpu::Protected::kNo,
                                               skgpu::Renderable::kYes));

    for (int bm = 0; bm <= (int) SkBlendMode::kLastCoeffMode; ++bm) {
        PaintParamsKeyBuilder builder(dict);
        // Use a solid color as the 1st root node; the 2nd root node represents the final blend.
        add_block(&builder, (int) BuiltInCodeSnippetID::kSolidColorShader);
        add_block(&builder, bm + kFixedBlendIDOffset);
        UniquePaintParamsID paintID = dict->findOrCreate(&builder);

        const RenderStep* renderStep = &context->priv().rendererProvider()->nonAABounds()->step(0);
        bool dstReadRequired =
                IsDstReadRequired(caps, static_cast<SkBlendMode>(bm), renderStep->coverage());

        // ShaderInfo expects to receive a concrete determination of dstReadStrategy based upon
        // whether a dst read is needed. Therefore, we need to decide whether to pass in the
        // dstReadStrategy reported by caps OR DstReadStrategy::kNoneRequired.
        std::unique_ptr<ShaderInfo> shaderInfo =
                ShaderInfo::Make(caps,
                                 dict,
                                 /*rteDict=*/nullptr,
                                 renderStep,
                                 paintID,
                                 /*useStorageBuffers=*/false,
                                 skgpu::Swizzle::RGBA(),
                                 dstReadRequired ? dstReadStrategy
                                                 : DstReadStrategy::kNoneRequired);

        SkBlendMode expectedBM = static_cast<SkBlendMode>(bm);
        if (expectedBM == SkBlendMode::kPlus) {
            // The kPlus "coefficient" blend mode always triggers shader blending to add a clamping
            // step that was originally elided in Porter-Duff due to automatic saturation to 8-bit
            // color values. Shader-based blending always uses kSrc HW blending.
            expectedBM = SkBlendMode::kSrc;
        }
        SkBlendModeCoeff expectedSrc, expectedDst;
        REPORTER_ASSERT(reporter, SkBlendMode_AsCoeff(expectedBM, &expectedSrc, &expectedDst));
        REPORTER_ASSERT(reporter, coeff_equal(expectedSrc, shaderInfo->blendInfo().fSrcBlend));
        REPORTER_ASSERT(reporter, coeff_equal(expectedDst, shaderInfo->blendInfo().fDstBlend));

        REPORTER_ASSERT(reporter, shaderInfo->blendInfo().fEquation == skgpu::BlendEquation::kAdd);
        REPORTER_ASSERT(reporter,
                        shaderInfo->blendInfo().fBlendConstant == SK_PMColor4fTRANSPARENT);

        bool expectedWriteColor = BlendModifiesDst(skgpu::BlendEquation::kAdd,
                                                   shaderInfo->blendInfo().fSrcBlend,
                                                   shaderInfo->blendInfo().fDstBlend);
        REPORTER_ASSERT(reporter, shaderInfo->blendInfo().fWritesColor == expectedWriteColor);
    }
}

// TODO: Add unit tests for converting a complex key to a ShaderInfo
