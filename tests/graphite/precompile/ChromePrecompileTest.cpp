/*
 * Copyright 2024 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "tests/Test.h"

#if defined(SK_GRAPHITE)

#include "include/gpu/graphite/Context.h"
#include "include/gpu/graphite/PrecompileContext.h"
#include "include/gpu/graphite/precompile/PaintOptions.h"
#include "include/gpu/graphite/precompile/Precompile.h"
#include "include/gpu/graphite/precompile/PrecompileColorFilter.h"
#include "include/gpu/graphite/precompile/PrecompileShader.h"
#include "src/gpu/graphite/ContextPriv.h"
#include "src/gpu/graphite/ContextUtils.h"
#include "src/gpu/graphite/GraphicsPipelineDesc.h"
#include "src/gpu/graphite/PrecompileContextPriv.h"
#include "src/gpu/graphite/RenderPassDesc.h"
#include "src/gpu/graphite/RendererProvider.h"
#include "tools/graphite/UniqueKeyUtils.h"

using namespace::skgpu::graphite;

namespace {

// "SolidColor SrcOver"
PaintOptions solid_srcover() {
    PaintOptions paintOptions;
    paintOptions.setBlendModes({ SkBlendMode::kSrcOver });
    return paintOptions;
}

// "SolidColor Src"
PaintOptions solid_src() {
    PaintOptions paintOptions;
    paintOptions.setBlendModes({ SkBlendMode::kSrc });
    return paintOptions;
}

// "LocalMatrix [ Compose [ HardwareImage(0) ColorSpaceTransform ] ] SrcOver"
PaintOptions image_srcover() {
    PaintOptions paintOptions;
    paintOptions.setShaders({ PrecompileShaders::Image() });
    paintOptions.setBlendModes({ SkBlendMode::kSrcOver });
    return paintOptions;
}

// "LocalMatrix [ Compose [ HardwareImage(0) ColorSpaceTransform ] ] Src"
PaintOptions image_src() {
    PaintOptions paintOptions;
    paintOptions.setShaders({ PrecompileShaders::Image() });
    paintOptions.setBlendModes({ SkBlendMode::kSrc });
    return paintOptions;
}

// "LocalMatrix [ Compose [ LinearGradient4 ColorSpaceTransform ] ] SrcOver"
PaintOptions lineargrad_srcover() {
    PaintOptions paintOptions;
    paintOptions.setShaders({ PrecompileShaders::LinearGradient() });
    paintOptions.setBlendModes({ SkBlendMode::kSrcOver });
    return paintOptions;
}

// "Compose [ LocalMatrix [ Compose [ LinearGradient4 ColorSpaceTransform ] ] Dither ] SrcOver"
PaintOptions lineargrad_srcover_dithered() {
    PaintOptions paintOptions;
    paintOptions.setShaders({ PrecompileShaders::LinearGradient() });
    paintOptions.setBlendModes({ SkBlendMode::kSrcOver });
    paintOptions.setDither(/* dither= */ true);
    return paintOptions;
}

// "Compose [ SolidColor Blend [ SolidColor Passthrough BlendModeBlender ] ] SrcOver"
[[maybe_unused]] PaintOptions blend_color_filter_srcover() {
    PaintOptions paintOptions;
    paintOptions.setBlendModes({ SkBlendMode::kSrcOver });
    paintOptions.setColorFilters({ PrecompileColorFilters::Blend() });
    return paintOptions;
}

// "RP(color: Dawn(f=R8,s=1), resolve: {}, ds: Dawn(f=D16,s=1), samples: 1, swizzle: a000)",
// Single sampled R w/ just depth
static const RenderPassProperties kR_1_Depth { DepthStencilFlags::kDepth,
                                               kAlpha_8_SkColorType,
                                               /* fDstCS= */ nullptr,
                                               /* fRequiresMSAA= */ false };

// "RP(color: Dawn(f=R8,s=4), resolve: Dawn(f=R8,s=1), ds: Dawn(f=D24_S8,s=4), samples: 4, swizzle: a000)",
// MSAA R w/ depth and stencil
static const RenderPassProperties kR_4_DepthStencil { DepthStencilFlags::kDepthStencil,
                                                      kAlpha_8_SkColorType,
                                                      /* fDstCS= */ nullptr,
                                                      /* fRequiresMSAA= */ true };

// "RP(color: Dawn(f=BGRA8,s=1), resolve: {}, ds: Dawn(f=D16,s=1), samples: 1, swizzle: rgba)"
// Single sampled BGRA w/ just depth
static const RenderPassProperties kBGRA_1_Depth { DepthStencilFlags::kDepth,
                                                  kBGRA_8888_SkColorType,
                                                  /* fDstCS= */ nullptr,
                                                  /* fRequiresMSAA= */ false };

// "RP(color: Dawn(f=BGRA8,s=4), resolve: Dawn(f=BGRA8,s=1), ds: Dawn(f=D16,s=4), samples: 4, swizzle: rgba)"
// MSAA BGRA w/ just depth
static const RenderPassProperties kBGRA_4_Depth { DepthStencilFlags::kDepth,
                                                  kBGRA_8888_SkColorType,
                                                  /* fDstCS= */ nullptr,
                                                  /* fRequiresMSAA= */ true };

// "RP(color: Dawn(f=BGRA8,s=4), resolve: Dawn(f=BGRA8,s=1), ds: Dawn(f=D24_S8,s=4), samples: 4, swizzle: rgba)"
// MSAA BGRA w/ depth and stencil
static const RenderPassProperties kBGRA_4_DepthStencil { DepthStencilFlags::kDepthStencil,
                                                         kBGRA_8888_SkColorType,
                                                         /* fDstCS= */ nullptr,
                                                         /* fRequiresMSAA= */ true };


// This helper maps from the RenderPass string in the Pipeline label to the
// RenderPassProperties needed by the Precompile system
// TODO(robertphillips): converting this to a more piecemeal approach might better illuminate
// the mapping between the string and the RenderPassProperties
RenderPassProperties get_render_pass_properties(const char* str) {
    static const struct {
        const char* fStr;
        RenderPassProperties fRenderPassProperties;
    } kRenderPassPropertiesMapping[] = {
        { "RP(color: Dawn(f=R8,s=1), resolve: {}, ds: Dawn(f=D16,s=1), samples: 1, swizzle: a000)",
          kR_1_Depth },
        { "RP(color: Dawn(f=R8,s=4), resolve: Dawn(f=R8,s=1), ds: Dawn(f=D24_S8,s=4), samples: 4, swizzle: a000)",
          kR_4_DepthStencil},
        { "RP(color: Dawn(f=BGRA8,s=1), resolve: {}, ds: Dawn(f=D16,s=1), samples: 1, swizzle: rgba)",
          kBGRA_1_Depth },
        { "RP(color: Dawn(f=BGRA8,s=4), resolve: Dawn(f=BGRA8,s=1), ds: Dawn(f=D16,s=4), samples: 4, swizzle: rgba)",
          kBGRA_4_Depth },
        { "RP(color: Dawn(f=BGRA8,s=4), resolve: Dawn(f=BGRA8,s=1), ds: Dawn(f=D24_S8,s=4), samples: 4, swizzle: rgba)",
           kBGRA_4_DepthStencil },
    };

    for (const auto& rppm : kRenderPassPropertiesMapping) {
        if (strstr(str, rppm.fStr)) {
            return rppm.fRenderPassProperties;
        }
    }

    SkAssertResult(0);
    return {};
}

// This helper maps from the RenderStep's name in the Pipeline label to the DrawTypeFlag that
// resulted in its use.
DrawTypeFlags get_draw_type_flags(const char* str) {
    static const struct {
        const char* fStr;
        DrawTypeFlags fFlags;
    } kDrawTypeFlagsMapping[] = {
        { "BitmapTextRenderStep[Mask]",                  DrawTypeFlags::kBitmapText_Mask },
        { "BitmapTextRenderStep[LCD]",                   DrawTypeFlags::kBitmapText_LCD },
        { "BitmapTextRenderStep[Color]",                 DrawTypeFlags::kBitmapText_Color },

        { "SDFTextRenderStep",                           DrawTypeFlags::kSDFText },
        { "SDFTextLCDRenderStep",                        DrawTypeFlags::kSDFText_LCD },

        { "VerticesRenderStep[Tris]",                    DrawTypeFlags::kDrawVertices },
        { "VerticesRenderStep[TrisTexCoords]",           DrawTypeFlags::kDrawVertices },
        { "VerticesRenderStep[TrisColor]",               DrawTypeFlags::kDrawVertices },
        { "VerticesRenderStep[TrisColorTexCoords]",      DrawTypeFlags::kDrawVertices },
        { "VerticesRenderStep[Tristrips]",               DrawTypeFlags::kDrawVertices },
        { "VerticesRenderStep[TristripsTexCoords]",      DrawTypeFlags::kDrawVertices },
        { "VerticesRenderStep[TristripsColor]",          DrawTypeFlags::kDrawVertices },
        { "VerticesRenderStep[TristripsColorTexCoords]", DrawTypeFlags::kDrawVertices },

        // TODO: AnalyticBlurRenderStep and CircularArcRenderStep should be split out into their
        // own DrawTypeFlags (e.g., kAnalyticBlur and kCircularArc)
        { "AnalyticBlurRenderStep",                      DrawTypeFlags::kSimpleShape },
        { "AnalyticRRectRenderStep",                     DrawTypeFlags::kSimpleShape },
        { "CircularArcRenderStep",                       DrawTypeFlags::kSimpleShape },
        { "CoverBoundsRenderStep[NonAAFill]",            DrawTypeFlags::kSimpleShape },
        { "PerEdgeAAQuadRenderStep",                     DrawTypeFlags::kSimpleShape },

        { "CoverageMaskRenderStep",                      DrawTypeFlags::kNonSimpleShape },
        { "CoverBoundsRenderStep[RegularCover]",         DrawTypeFlags::kNonSimpleShape },
        { "CoverBoundsRenderStep[InverseCover]",         DrawTypeFlags::kNonSimpleShape },
        { "MiddleOutFanRenderStep[EvenOdd]",             DrawTypeFlags::kNonSimpleShape },
        { "MiddleOutFanRenderStep[Winding]",             DrawTypeFlags::kNonSimpleShape },
        { "TessellateCurvesRenderStep[EvenOdd]",         DrawTypeFlags::kNonSimpleShape },
        { "TessellateCurvesRenderStep[Winding]",         DrawTypeFlags::kNonSimpleShape },
        { "TessellateStrokesRenderStep",                 DrawTypeFlags::kNonSimpleShape },
        { "TessellateWedgesRenderStep[Convex]",          DrawTypeFlags::kNonSimpleShape },
        { "TessellateWedgesRenderStep[EvenOdd]",         DrawTypeFlags::kNonSimpleShape },
        { "TessellateWedgesRenderStep[Winding]",         DrawTypeFlags::kNonSimpleShape },
    };

    for (const auto& dtfm : kDrawTypeFlagsMapping) {
        if (strstr(str, dtfm.fStr)) {
            SkAssertResult(dtfm.fFlags != DrawTypeFlags::kNone);
            return dtfm.fFlags;
        }
    }

    SkAssertResult(0);
    return DrawTypeFlags::kNone;
}


// Precompile with the provided paintOptions, drawType, and RenderPassSettings then verify that
// the expected string is in the generated set.
// Additionally, verify that overgeneration is within expected tolerances.
// If you add an additional RenderStep you may need to increase the tolerance values.
void run_test(PrecompileContext* precompileContext,
              skiatest::Reporter* reporter,
              SkSpan<const char*> cases,
              size_t caseID,
              const PaintOptions& paintOptions,
              DrawTypeFlags drawType,
              const RenderPassProperties& renderPassSettings,
              unsigned int allowedOvergeneration) {
    const char* expectedString = cases[caseID];

    precompileContext->priv().globalCache()->resetGraphicsPipelines();

    Precompile(precompileContext, paintOptions, drawType, { &renderPassSettings, 1 });

    std::vector<std::string> generated;

    {
        const RendererProvider* rendererProvider = precompileContext->priv().rendererProvider();
        const ShaderCodeDictionary* dict = precompileContext->priv().shaderCodeDictionary();

        std::vector<skgpu::UniqueKey> generatedKeys;

        UniqueKeyUtils::FetchUniqueKeys(precompileContext, &generatedKeys);

        for (const skgpu::UniqueKey& key : generatedKeys) {
            GraphicsPipelineDesc pipelineDesc;
            RenderPassDesc renderPassDesc;
            UniqueKeyUtils::ExtractKeyDescs(precompileContext, key, &pipelineDesc, &renderPassDesc);

            const RenderStep* renderStep = rendererProvider->lookup(pipelineDesc.renderStepID());
            generated.push_back(GetPipelineLabel(dict, renderPassDesc, renderStep,
                                                 pipelineDesc.paintParamsID()));
        }
    }

    bool correctGenerationAmt = generated.size() == allowedOvergeneration;
    REPORTER_ASSERT(reporter, correctGenerationAmt,
                    "case %zu overgenerated - %zu > %d\n",
                    caseID, generated.size(), allowedOvergeneration);

    const size_t len = strlen(expectedString);

    bool foundIt = false;
    for (size_t i = 0; i < generated.size(); ++i) {
        // The generated strings have trailing whitespace
        if (!strncmp(expectedString, generated[i].c_str(), len)) {
            foundIt = true;
            break;
        }
    }

    REPORTER_ASSERT(reporter, foundIt);

#ifdef SK_DEBUG
    if (foundIt && correctGenerationAmt) {
        return;
    }

    SkDebugf("Expected string:\n%s\n%s in %zu strings:\n",
             expectedString,
             foundIt ? "found" : "NOT found",
             generated.size());

    for (size_t i = 0; i < generated.size(); ++i) {
        SkDebugf("%zu: %s\n", i, generated[i].c_str());
    }
#endif
}

// The pipeline strings were created using the Dawn Metal backend so that is the only viable
// comparison
bool is_dawn_metal_context_type(skgpu::ContextType type) {
    return type == skgpu::ContextType::kDawn_Metal;
}

} // anonymous namespace


DEF_GRAPHITE_TEST_FOR_CONTEXTS(ChromePrecompileTest, is_dawn_metal_context_type,
                               reporter, context, /* testContext */, CtsEnforcement::kNever) {

    std::unique_ptr<PrecompileContext> precompileContext = context->makePrecompileContext();
    const skgpu::graphite::Caps* caps = precompileContext->priv().caps();

    TextureInfo textureInfo = caps->getDefaultSampledTextureInfo(kBGRA_8888_SkColorType,
                                                                 skgpu::Mipmapped::kNo,
                                                                 skgpu::Protected::kNo,
                                                                 skgpu::Renderable::kYes);

    TextureInfo msaaTex = caps->getDefaultMSAATextureInfo(textureInfo, Discardable::kYes);

    if (msaaTex.numSamples() <= 1) {
        // The following pipelines rely on having MSAA
        return;
    }

#ifdef SK_ENABLE_VELLO_SHADERS
    if (caps->computeSupport()) {
        // The following pipelines rely on not utilizing Vello
        return;
    }
#endif

    const char* kCases[] = {
        // Wikipedia 2018 - these are reordered from the spreadsheet
        /*  0 */ "RP(color: Dawn(f=BGRA8,s=4), resolve: Dawn(f=BGRA8,s=1), ds: Dawn(f=D24_S8,s=4), samples: 4, swizzle: rgba) + "
                 "TessellateWedgesRenderStep[Winding] + "
                 "(empty)",
        /*  1 */ "RP(color: Dawn(f=BGRA8,s=4), resolve: Dawn(f=BGRA8,s=1), ds: Dawn(f=D24_S8,s=4), samples: 4, swizzle: rgba) + "
                 "TessellateWedgesRenderStep[EvenOdd] + "
                 "(empty)",
        /*  2 */ "RP(color: Dawn(f=BGRA8,s=4), resolve: Dawn(f=BGRA8,s=1), ds: Dawn(f=D24_S8,s=4), samples: 4, swizzle: rgba) + "
                 "CoverBoundsRenderStep[NonAAFill] + "
                 "SolidColor SrcOver",
        /*  3 */ "RP(color: Dawn(f=BGRA8,s=4), resolve: Dawn(f=BGRA8,s=1), ds: Dawn(f=D24_S8,s=4), samples: 4, swizzle: rgba) + "
                 "CoverBoundsRenderStep[NonAAFill] + "
                 "SolidColor Src",
        /*  4 */ "RP(color: Dawn(f=BGRA8,s=4), resolve: Dawn(f=BGRA8,s=1), ds: Dawn(f=D24_S8,s=4), samples: 4, swizzle: rgba) + "
                 "PerEdgeAAQuadRenderStep + "
                 "LocalMatrix [ Compose [ Image(0) ColorSpaceTransform ] ] SrcOver",
        /*  5 */ "RP(color: Dawn(f=BGRA8,s=4), resolve: Dawn(f=BGRA8,s=1), ds: Dawn(f=D24_S8,s=4), samples: 4, swizzle: rgba) + "
                 "PerEdgeAAQuadRenderStep + "
                 "LocalMatrix [ Compose [ HardwareImage(0) ColorSpaceTransform ] ] SrcOver",
        /*  6 */ "RP(color: Dawn(f=BGRA8,s=4), resolve: Dawn(f=BGRA8,s=1), ds: Dawn(f=D24_S8,s=4), samples: 4, swizzle: rgba) + "
                 "CoverBoundsRenderStep[NonAAFill] + "
                 "LocalMatrix [ Compose [ HardwareImage(0) ColorSpaceTransform ] ] SrcOver",
        /*  7 */ "RP(color: Dawn(f=BGRA8,s=4), resolve: Dawn(f=BGRA8,s=1), ds: Dawn(f=D24_S8,s=4), samples: 4, swizzle: rgba) + "
                 "AnalyticRRectRenderStep + "
                 "Compose [ LocalMatrix [ Compose [ LinearGradient4 ColorSpaceTransformPremul ] ] Dither ] SrcOver",
        /*  8 */ "RP(color: Dawn(f=BGRA8,s=4), resolve: Dawn(f=BGRA8,s=1), ds: Dawn(f=D24_S8,s=4), samples: 4, swizzle: rgba) + "
                 "CoverBoundsRenderStep[NonAAFill] + "
                 "Compose [ LocalMatrix [ Compose [ LinearGradient4 ColorSpaceTransformPremul ] ] Dither ] SrcOver",
        /*  9 */ "RP(color: Dawn(f=BGRA8,s=4), resolve: Dawn(f=BGRA8,s=1), ds: Dawn(f=D24_S8,s=4), samples: 4, swizzle: rgba) + "
                 "BitmapTextRenderStep[Mask] + "
                 "LocalMatrix [ Compose [ LinearGradient4 ColorSpaceTransformPremul ] ] SrcOver",
        /* 10 */ "RP(color: Dawn(f=BGRA8,s=4), resolve: Dawn(f=BGRA8,s=1), ds: Dawn(f=D24_S8,s=4), samples: 4, swizzle: rgba) + "
                 "BitmapTextRenderStep[Mask] + "
                 "SolidColor SrcOver",
        /* 11 */ "RP(color: Dawn(f=BGRA8,s=1), resolve: {}, ds: Dawn(f=D16,s=1), samples: 1, swizzle: rgba) + "
                 "AnalyticRRectRenderStep + "
                 "SolidColor SrcOver",
        /* 12 */ "RP(color: Dawn(f=BGRA8,s=1), resolve: {}, ds: Dawn(f=D16,s=1), samples: 1, swizzle: rgba) + "
                 "CoverBoundsRenderStep[NonAAFill] + "
                 "SolidColor SrcOver",
        /* 13 */ "RP(color: Dawn(f=BGRA8,s=1), resolve: {}, ds: Dawn(f=D16,s=1), samples: 1, swizzle: rgba) + "
                 "PerEdgeAAQuadRenderStep + "
                 "LocalMatrix [ Compose [ HardwareImage(0) ColorSpaceTransform ] ] Src",
        /* 14 */ "RP(color: Dawn(f=BGRA8,s=1), resolve: {}, ds: Dawn(f=D16,s=1), samples: 1, swizzle: rgba) + "
                 "CoverBoundsRenderStep[NonAAFill] + "
                 "LocalMatrix [ Compose [ HardwareImage(0) ColorSpaceTransform ] ] SrcOver",
        /* 15 */ "RP(color: Dawn(f=BGRA8,s=4), resolve: Dawn(f=BGRA8,s=1), ds: Dawn(f=D16,s=4), samples: 4, swizzle: rgba) + "
                 "TessellateWedgesRenderStep[Convex] + "
                 "SolidColor SrcOver",
        /* 16 */ "RP(color: Dawn(f=BGRA8,s=4), resolve: Dawn(f=BGRA8,s=1), ds: Dawn(f=D16,s=4), samples: 4, swizzle: rgba) + "
                 "TessellateStrokesRenderStep + "
                 "SolidColor SrcOver",
        /* 17 */ "RP(color: Dawn(f=BGRA8,s=1), resolve: {}, ds: Dawn(f=D16,s=1), samples: 1, swizzle: rgba) + "
                 "AnalyticBlurRenderStep + "
                 "Compose [ SolidColor Blend [ SolidColor Passthrough BlendModeBlender ] ] SrcOver",
        /* 18 */ "RP(color: Dawn(f=BGRA8,s=1), resolve: {}, ds: Dawn(f=D16,s=1), samples: 1, swizzle: rgba) + "
                 "CoverBoundsRenderStep[NonAAFill] + "
                 "SolidColor Src",
        /* 19 */ "RP(color: Dawn(f=BGRA8,s=1), resolve: {}, ds: Dawn(f=D16,s=1), samples: 1, swizzle: rgba) + "
                 "CoverBoundsRenderStep[NonAAFill] + "
                 "Compose [ LocalMatrix [ Compose [ LinearGradient4 ColorSpaceTransformPremul ] ] Dither ] SrcOver",
    };

    for (size_t i = 0; i < std::size(kCases); ++i) {
        PaintOptions paintOptions;
        RenderPassProperties renderPassSettings;
        DrawTypeFlags drawTypeFlags = DrawTypeFlags::kNone;

        // TODO(robertphillips): splitting kCases[i] into substrings (based on a " + " separator)
        //  before passing to the helpers would make this prettier
        RenderPassProperties expectedRenderPassSettings = get_render_pass_properties(kCases[i]);
        DrawTypeFlags expectedDrawTypeFlags = get_draw_type_flags(kCases[i]);
        unsigned int expectedNumPipelines = 0;

        switch (i) {
            case 0:            [[fallthrough]];
            case 1:
                paintOptions = solid_srcover();
                drawTypeFlags = DrawTypeFlags::kNonSimpleShape;
                renderPassSettings = kBGRA_4_DepthStencil;
                expectedNumPipelines = 11;
                break;
            case 2:
                paintOptions = solid_srcover();
                drawTypeFlags = DrawTypeFlags::kSimpleShape;
                renderPassSettings = kBGRA_4_DepthStencil;
                expectedNumPipelines = 5;
                break;
            case 3: // only differs from 18 by MSAA and depth vs depth-stencil
                paintOptions = solid_src();
                drawTypeFlags = DrawTypeFlags::kSimpleShape;
                renderPassSettings = kBGRA_4_DepthStencil;
                expectedNumPipelines = 5; // a lot for a rectangle clear - all RenderSteps
                break;
            case 4: // 4 is part of an AA image rect draw that can't use HW tiling
            case 5: // 5 & 6 together make up an AA image rect draw w/ a filled center
            case 6:
                paintOptions = image_srcover();
                drawTypeFlags = DrawTypeFlags::kSimpleShape;
                renderPassSettings = kBGRA_4_DepthStencil;
                expectedNumPipelines = 80;
                break;
            case 7: // 7 & 8 are combined pair
            case 8:
                paintOptions = lineargrad_srcover_dithered();
                drawTypeFlags = DrawTypeFlags::kSimpleShape;
                renderPassSettings = kBGRA_4_DepthStencil;
                expectedNumPipelines = 15; // 3x from gradient, 12x from RenderSteps
                break;
            case 9:
                paintOptions = lineargrad_srcover();
                drawTypeFlags = DrawTypeFlags::kBitmapText_Mask;
                renderPassSettings = kBGRA_4_DepthStencil;
                expectedNumPipelines = 3; // from the 3 internal gradient alternatives
                break;
            case 10:
                paintOptions = solid_srcover();
                drawTypeFlags = DrawTypeFlags::kBitmapText_Mask;
                renderPassSettings = kBGRA_4_DepthStencil;
                expectedNumPipelines = 1;
                break;
            case 11: // 11 & 12 are a pair - an RRect draw w/ a non-aa-fill center
            case 12:
                paintOptions = solid_srcover();
                drawTypeFlags = DrawTypeFlags::kSimpleShape;
                renderPassSettings = kBGRA_1_Depth;
                expectedNumPipelines = 5;  // all from RenderSteps
                break;
            case 13:
                paintOptions = image_src();
                drawTypeFlags = DrawTypeFlags::kSimpleShape;
                renderPassSettings = kBGRA_1_Depth;
                // This is a lot for a kSrc image draw:
                expectedNumPipelines = 80; // 8x of this are the paint combos,
                                           // the rest are the RenderSteps!!
                break;
            case 14:
                paintOptions = image_srcover();
                drawTypeFlags = DrawTypeFlags::kSimpleShape;
                renderPassSettings = kBGRA_1_Depth;
                expectedNumPipelines = 80; // !!!! - a lot for just a non-aa image rect draw
                break;
            case 15:
            case 16:
                paintOptions = solid_srcover();
                drawTypeFlags = DrawTypeFlags::kNonSimpleShape;
                renderPassSettings = kBGRA_4_Depth;
                expectedNumPipelines = 11;
                break;
            case 17:
                // After https://skia-review.googlesource.com/c/skia/+/887476 ([graphite] Split up
                // universal blend shader snippet) this case no longer exists/is reproducible.
                //
                //  paintOptions = blend_color_filter_srcover();
                //  drawTypeFlags = DrawTypeFlags::kSimpleShape;
                //  renderPassSettings = bgra_1_depth();
                //  allowedOvergeneration = 4;
                continue;
            case 18: // only differs from 3 by MSAA and depth vs depth-stencil
                paintOptions = solid_src();
                drawTypeFlags = DrawTypeFlags::kSimpleShape;
                renderPassSettings = kBGRA_1_Depth;
                expectedNumPipelines = 5; // a lot for a rectangle clear - all RenderSteps
                break;
            case 19:
                paintOptions = lineargrad_srcover_dithered();
                drawTypeFlags = DrawTypeFlags::kSimpleShape;
                renderPassSettings = kBGRA_1_Depth;
                expectedNumPipelines = 15; // 3x from gradient, rest from RenderSteps
                break;
            default:
                continue;
        }

        SkAssertResult(renderPassSettings == expectedRenderPassSettings);
        SkAssertResult(drawTypeFlags == expectedDrawTypeFlags);

        if (renderPassSettings.fRequiresMSAA && caps->loadOpAffectsMSAAPipelines()) {
            expectedNumPipelines *= 2; // due to wgpu::LoadOp::ExpandResolveTexture
        }

        run_test(precompileContext.get(), reporter,
                 { kCases, std::size(kCases) }, i,
                 paintOptions, drawTypeFlags, renderPassSettings, expectedNumPipelines);
    }
}

#endif // SK_GRAPHITE
