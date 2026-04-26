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
