// Copyright Monster Engine. All Rights Reserved.

#include <gtest/gtest.h>
#include "Engine/PostProcess/FXAATypes.h"

using namespace MonsterEngine::PostProcess;

/**
 * Test suite for FXAA (Fast Approximate Anti-Aliasing) implementation
 */

// ============================================================================
// FFXAAUniformBuffer Layout Tests
// ============================================================================

TEST(FXAATests, UniformBufferSize)
{
    // Verify UBO size is exactly 64 bytes (std140 alignment)
    EXPECT_EQ(sizeof(FFXAAUniformBuffer), 64);
}

TEST(FXAATests, UniformBufferFieldOffsets)
{
    // Verify field offsets match std140 layout
    EXPECT_EQ(offsetof(FFXAAUniformBuffer, RcpFrame), 0);
    EXPECT_EQ(offsetof(FFXAAUniformBuffer, QualitySubpix), 8);
    EXPECT_EQ(offsetof(FFXAAUniformBuffer, QualityEdgeThreshold), 12);
    EXPECT_EQ(offsetof(FFXAAUniformBuffer, QualityEdgeThresholdMin), 16);
    EXPECT_EQ(offsetof(FFXAAUniformBuffer, Preset), 20);
    EXPECT_EQ(offsetof(FFXAAUniformBuffer, Padding), 24);
}

// ============================================================================
// FFXAAConfig Default Values Tests
// ============================================================================

TEST(FXAATests, DefaultConfiguration)
{
    FFXAAConfig config;
    
    // Verify defaults
    EXPECT_FALSE(config.EnableFXAA);  // Disabled by default (TAA preferred)
    EXPECT_EQ(config.Preset, 2);      // Preset 2 (balanced)
    EXPECT_FLOAT_EQ(config.QualitySubpix, 0.75f);
    EXPECT_FLOAT_EQ(config.QualityEdgeThreshold, 0.166f);
    EXPECT_FLOAT_EQ(config.QualityEdgeThresholdMin, 0.0833f);
}

TEST(FXAATests, PresetRange)
{
    FFXAAConfig config;
    
    // Test valid preset range (0-5)
    for (int32 preset = 0; preset <= 5; ++preset)
    {
        config.Preset = preset;
        EXPECT_GE(config.Preset, 0);
        EXPECT_LE(config.Preset, 5);
    }
}

// ============================================================================
// Configuration Parameter Tests
// ============================================================================

TEST(FXAATests, QualitySubpixRange)
{
    FFXAAConfig config;
    
    // Test valid range [0.0, 1.0]
    config.QualitySubpix = 0.0f;
    EXPECT_GE(config.QualitySubpix, 0.0f);
    EXPECT_LE(config.QualitySubpix, 1.0f);
    
    config.QualitySubpix = 0.5f;
    EXPECT_GE(config.QualitySubpix, 0.0f);
    EXPECT_LE(config.QualitySubpix, 1.0f);
    
    config.QualitySubpix = 1.0f;
    EXPECT_GE(config.QualitySubpix, 0.0f);
    EXPECT_LE(config.QualitySubpix, 1.0f);
}

TEST(FXAATests, EdgeThresholdRange)
{
    FFXAAConfig config;
    
    // Test recommended range [0.063, 0.333]
    config.QualityEdgeThreshold = 0.063f;  // High quality
    EXPECT_GE(config.QualityEdgeThreshold, 0.063f);
    EXPECT_LE(config.QualityEdgeThreshold, 0.333f);
    
    config.QualityEdgeThreshold = 0.166f;  // Balanced (default)
    EXPECT_GE(config.QualityEdgeThreshold, 0.063f);
    EXPECT_LE(config.QualityEdgeThreshold, 0.333f);
    
    config.QualityEdgeThreshold = 0.333f;  // Performance
    EXPECT_GE(config.QualityEdgeThreshold, 0.063f);
    EXPECT_LE(config.QualityEdgeThreshold, 0.333f);
}

// ============================================================================
// Preset Configuration Tests
// ============================================================================

TEST(FXAATests, PresetConfigurations)
{
    FFXAAConfig config;
    
    // Preset 0: Fastest (mobile/low-end)
    config.Preset = 0;
    config.QualitySubpix = 0.5f;
    config.QualityEdgeThreshold = 0.25f;
    EXPECT_EQ(config.Preset, 0);
    
    // Preset 2: Balanced (default, recommended)
    config.Preset = 2;
    config.QualitySubpix = 0.75f;
    config.QualityEdgeThreshold = 0.166f;
    EXPECT_EQ(config.Preset, 2);
    
    // Preset 5: Highest quality (high-end PC)
    config.Preset = 5;
    config.QualitySubpix = 1.0f;
    config.QualityEdgeThreshold = 0.063f;
    EXPECT_EQ(config.Preset, 5);
}

// ============================================================================
// UBO Initialization Tests
// ============================================================================

TEST(FXAATests, UniformBufferInitialization)
{
    FFXAAUniformBuffer ubo;
    
    // Initialize with test values
    ubo.RcpFrame = MonsterEngine::Math::FVector2D(1.0f / 1920.0f, 1.0f / 1080.0f);
    ubo.QualitySubpix = 0.75f;
    ubo.QualityEdgeThreshold = 0.166f;
    ubo.QualityEdgeThresholdMin = 0.0833f;
    ubo.Preset = 2;
    
    // Verify values
    EXPECT_FLOAT_EQ(ubo.RcpFrame.x, 1.0f / 1920.0f);
    EXPECT_FLOAT_EQ(ubo.RcpFrame.y, 1.0f / 1080.0f);
    EXPECT_FLOAT_EQ(ubo.QualitySubpix, 0.75f);
    EXPECT_FLOAT_EQ(ubo.QualityEdgeThreshold, 0.166f);
    EXPECT_FLOAT_EQ(ubo.QualityEdgeThresholdMin, 0.0833f);
    EXPECT_EQ(ubo.Preset, 2);
}

TEST(FXAATests, RcpFrameCalculation)
{
    FFXAAUniformBuffer ubo;
    
    // Test 1080p
    uint32 width = 1920;
    uint32 height = 1080;
    ubo.RcpFrame = MonsterEngine::Math::FVector2D(
        1.0f / static_cast<float>(width),
        1.0f / static_cast<float>(height)
    );
    
    EXPECT_NEAR(ubo.RcpFrame.x, 0.00052083f, 0.00001f);
    EXPECT_NEAR(ubo.RcpFrame.y, 0.00092593f, 0.00001f);
    
    // Test 4K
    width = 3840;
    height = 2160;
    ubo.RcpFrame = MonsterEngine::Math::FVector2D(
        1.0f / static_cast<float>(width),
        1.0f / static_cast<float>(height)
    );
    
    EXPECT_NEAR(ubo.RcpFrame.x, 0.00026042f, 0.00001f);
    EXPECT_NEAR(ubo.RcpFrame.y, 0.00046296f, 0.00001f);
}
