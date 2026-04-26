// Copyright Monster Engine. All Rights Reserved.

#include <gtest/gtest.h>
#include "Engine/Deferred/DeferredUniformTypes.h"

using namespace MonsterEngine::Deferred;

/**
 * Test suite for TAA (Temporal Anti-Aliasing) UBO layout verification
 */

TEST(TAATests, TransformUBOLayout) {
    // Verify total size after TAA extension
    EXPECT_EQ(sizeof(FDeferredTransformUBO), 352);
    
    // Verify existing field offsets remain unchanged
    EXPECT_EQ(offsetof(FDeferredTransformUBO, Model), 0);
    EXPECT_EQ(offsetof(FDeferredTransformUBO, View), 64);
    EXPECT_EQ(offsetof(FDeferredTransformUBO, Proj), 128);
    EXPECT_EQ(offsetof(FDeferredTransformUBO, NormalMatrix), 192);
    EXPECT_EQ(offsetof(FDeferredTransformUBO, CameraPos), 256);
    
    // Verify new TAA field offset
    EXPECT_EQ(offsetof(FDeferredTransformUBO, PreviousModel), 272);
}

TEST(TAATests, SceneUBOLayout) {
    // Verify total size after TAA extension
    EXPECT_EQ(sizeof(FDeferredSceneUBO), 256);
    
    // Verify existing field offset
    EXPECT_EQ(offsetof(FDeferredSceneUBO, InvViewProj), 0);
    
    // Verify new TAA field offsets
    EXPECT_EQ(offsetof(FDeferredSceneUBO, PreviousViewProj), 160);
    EXPECT_EQ(offsetof(FDeferredSceneUBO, JitterOffset), 224);
    EXPECT_EQ(offsetof(FDeferredSceneUBO, TAAParams), 240);
}

/**
 * Test Halton sequence generation for jitter patterns
 */
TEST(TAATests, HaltonSequenceGeneration) {
    MonsterEngine::Deferred::FDeferredRenderer renderer;
    
    // Test Halton sequence with base 2
    EXPECT_FLOAT_EQ(renderer.Halton(1, 2), 0.5f);
    EXPECT_FLOAT_EQ(renderer.Halton(2, 2), 0.25f);
    EXPECT_FLOAT_EQ(renderer.Halton(3, 2), 0.75f);
    
    // Test Halton sequence with base 3
    EXPECT_NEAR(renderer.Halton(1, 3), 0.333333f, 0.0001f);
    EXPECT_NEAR(renderer.Halton(2, 3), 0.666666f, 0.0001f);
}

/**
 * Test 2D jitter generation using Halton sequence
 */
TEST(TAATests, JitterGeneration) {
    MonsterEngine::Deferred::FDeferredRenderer renderer;
    
    // Test jitter range for 8-sample pattern
    for (uint32 i = 0; i < 8; i++) {
        MonsterEngine::Math::FVector2f jitter = renderer.GenerateJitter(i);
        EXPECT_GE(jitter.x, -0.5f);
        EXPECT_LE(jitter.x, 0.5f);
        EXPECT_GE(jitter.y, -0.5f);
        EXPECT_LE(jitter.y, 0.5f);
    }
    
    // Test pattern repeats after 8 frames
    MonsterEngine::Math::FVector2f jitter0 = renderer.GenerateJitter(0);
    MonsterEngine::Math::FVector2f jitter8 = renderer.GenerateJitter(8);
    EXPECT_EQ(jitter0.x, jitter8.x);
    EXPECT_EQ(jitter0.y, jitter8.y);
}
