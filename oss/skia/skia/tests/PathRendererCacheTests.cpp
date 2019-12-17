/*
 * Copyright 2017 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "Test.h"

#include "SkPath.h"

#if SK_SUPPORT_GPU
#include "GrClip.h"
#include "GrContext.h"
#include "GrContextPriv.h"
#include "GrResourceCache.h"
#include "GrSoftwarePathRenderer.h"
#include "effects/GrPorterDuffXferProcessor.h"
#include "ops/GrTessellatingPathRenderer.h"

static SkPath create_concave_path() {
    SkPath path;
    path.moveTo(100, 0);
    path.lineTo(200, 200);
    path.lineTo(100, 150);
    path.lineTo(0, 200);
    path.close();
    return path;
}

static void draw_path(GrContext* ctx,
                      GrRenderTargetContext* renderTargetContext,
                      const SkPath& path,
                      GrPathRenderer* pr,
                      GrAAType aaType,
                      const GrStyle& style) {
    GrPaint paint;
    paint.setXPFactory(GrPorterDuffXPFactory::Get(SkBlendMode::kSrc));

    GrNoClip noClip;
    SkIRect clipConservativeBounds = SkIRect::MakeWH(renderTargetContext->width(),
                                                     renderTargetContext->height());
    GrShape shape(path, style);
    if (shape.style().applies()) {
        shape = shape.applyStyle(GrStyle::Apply::kPathEffectAndStrokeRec, 1.0f);
    }
    SkMatrix matrix = SkMatrix::I();
    GrPathRenderer::DrawPathArgs args{ctx,
                                      std::move(paint),
                                      &GrUserStencilSettings::kUnused,
                                      renderTargetContext,
                                      &noClip,
                                      &clipConservativeBounds,
                                      &matrix,
                                      &shape,
                                      aaType,
                                      false};
    pr->drawPath(args);
}

static bool cache_non_scratch_resources_equals(GrResourceCache* cache, int expected) {
#if GR_CACHE_STATS
    GrResourceCache::Stats stats;
    cache->getStats(&stats);
    return (stats.fTotal - stats.fScratch) == expected;
#else
    return true;
#endif
}

static void test_path(skiatest::Reporter* reporter,
                      std::function<SkPath(void)> createPath,
                      std::function<GrPathRenderer*(GrContext*)> createPathRenderer,
                      GrAAType aaType = GrAAType::kNone,
                      GrStyle style = GrStyle(SkStrokeRec::kFill_InitStyle)) {
    sk_sp<GrContext> ctx = GrContext::MakeMock(nullptr);
    ctx->setResourceCacheLimits(100, 100000);
    GrResourceCache* cache = ctx->getResourceCache();

    sk_sp<GrRenderTargetContext> rtc(ctx->makeDeferredRenderTargetContext(
            SkBackingFit::kApprox, 800, 800, kRGBA_8888_GrPixelConfig, nullptr, 0,
            kTopLeft_GrSurfaceOrigin));
    if (!rtc) {
        return;
    }

    sk_sp<GrPathRenderer> pathRenderer(createPathRenderer(ctx.get()));
    SkPath path = createPath();

    // Initially, cache only has the render target context
    REPORTER_ASSERT(reporter, cache_non_scratch_resources_equals(cache, 0));

    // Draw the path, check that new resource count matches expectations
    draw_path(ctx.get(), rtc.get(), path, pathRenderer.get(), aaType, style);
    ctx->flush();
    REPORTER_ASSERT(reporter, cache_non_scratch_resources_equals(cache, 1));

    // Nothing should be purgeable yet
    cache->purgeAsNeeded();
    REPORTER_ASSERT(reporter, cache_non_scratch_resources_equals(cache, 1));

    // Reset the path to change the GenID, which should invalidate any resources in the cache
    path.reset();
    cache->purgeAsNeeded();
    REPORTER_ASSERT(reporter, cache_non_scratch_resources_equals(cache, 0));
}

// Test that deleting the original path invalidates the VBs cached by the tessellating path renderer
DEF_GPUTEST(TessellatingPathRendererCacheTest, reporter, factory) {
    auto createPR = [](GrContext*) {
        return new GrTessellatingPathRenderer();
    };

    // Tessellating path renderer stores vertex buffers in the cache (for non-AA paths)
    test_path(reporter, create_concave_path, createPR);

    // Test with a style that alters the path geometry. This needs to attach the invalidation logic
    // to the original path, not the modified path produced by the style.
    SkPaint paint;
    paint.setStyle(SkPaint::kStroke_Style);
    paint.setStrokeWidth(1);
    GrStyle style(paint);
    test_path(reporter, create_concave_path, createPR, GrAAType::kNone, style);
}

// Test that deleting the original path invalidates the textures cached by the SW path renderer
DEF_GPUTEST(SoftwarePathRendererCacheTest, reporter, factory) {
    auto createPR = [](GrContext* ctx) {
        return new GrSoftwarePathRenderer(ctx->resourceProvider(), true);
    };

    // Tessellating path renderer stores vertex buffers in the cache (for non-AA paths)
    test_path(reporter, create_concave_path, createPR, GrAAType::kCoverage);

    // Test with a style that alters the path geometry. This needs to attach the invalidation logic
    // to the original path, not the modified path produced by the style.
    SkPaint paint;
    paint.setStyle(SkPaint::kStroke_Style);
    paint.setStrokeWidth(1);
    GrStyle style(paint);
    test_path(reporter, create_concave_path, createPR, GrAAType::kCoverage, style);
}

#endif
