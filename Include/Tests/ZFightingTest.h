// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file ZFightingTest.h
 * @brief Z-Fighting test suite for lane-level navigation depth conflict scenarios
 * 
 * Tests various solutions for resolving Z-fighting between coplanar geometry
 * (e.g., road surface and lane arrows in navigation applications).
 */

#include "Core/CoreMinimal.h"
#include "RHI/RHIDefinitions.h"

namespace MonsterEngine {
namespace ZFightingTest {

/**
 * Test configuration structure
 */
struct FZFightingTestConfig {
    bool bEnablePolygonOffset = false;
    float32 depthBiasConstant = 0.0f;
    float32 depthBiasSlope = 0.0f;
    float32 cameraDistance = 10.0f;
    bool bUseDecalTechnique = false;
    MonsterRender::RHI::EDepthRange depthRange = MonsterRender::RHI::EDepthRange::Standard;
};

/**
 * Run all Z-Fighting tests
 */
void RunAllTests();

/**
 * Individual test cases
 */
void Test_CoplanarGeometry();
void Test_PolygonOffsetSolution();
void Test_DepthBiasSolution();
void Test_DecalRenderingSolution();
void Test_DistanceBasedOffset();
void Test_StencilBufferSolution();
void Test_DepthPrecisionAnalysis();
void Test_ReversedZDepthBuffer();
void Test_ZFightingComparison();
void Test_PracticalUsageExample();

} // namespace ZFightingTest
} // namespace MonsterEngine
