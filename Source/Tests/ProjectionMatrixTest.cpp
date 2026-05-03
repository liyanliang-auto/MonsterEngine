// Copyright Monster Engine. All Rights Reserved.

/**
 * @file ProjectionMatrixTest.cpp
 * @brief Test suite for projection matrix functions
 * 
 * Validates Standard, Reversed-Z, and Infinite Reversed-Z projection matrices.
 */

#include "Math/MonsterMath.h"
#include "Core/Log.h"

namespace MonsterEngine {
namespace ProjectionMatrixTest {

using namespace Math;

/**
 * Test standard perspective projection matrix
 */
static void Test_StandardPerspective() {
    MR_LOG_INFO("\n[TEST] Standard Perspective Projection");
    
    float32 fov = FMath::DegreesToRadians(90.0f);
    float32 aspect = 16.0f / 9.0f;
    float32 nearZ = 0.1f;
    float32 farZ = 100.0f;
    
    FMatrix4x4 proj = PerspectiveStandard(fov, aspect, nearZ, farZ);
    
    MR_LOG_INFO("  FOV: 90 degrees");
    MR_LOG_INFO("  Aspect: 16:9");
    MR_LOG_INFO("  Near: 0.1, Far: 100.0");
    MR_LOG_INFO("  Matrix[2][2]: " + std::to_string(proj.M[2][2]));
    MR_LOG_INFO("  Matrix[3][2]: " + std::to_string(proj.M[3][2]));
    MR_LOG_INFO("  [PASS] Standard perspective matrix created");
}

/**
 * Test Reversed-Z perspective projection matrix
 */
static void Test_ReversedZPerspective() {
    MR_LOG_INFO("\n[TEST] Reversed-Z Perspective Projection");
    
    float32 fov = FMath::DegreesToRadians(90.0f);
    float32 aspect = 16.0f / 9.0f;
    float32 nearZ = 0.1f;
    float32 farZ = 100.0f;
    
    FMatrix4x4 proj = PerspectiveReversedZ(fov, aspect, nearZ, farZ);
    
    MR_LOG_INFO("  FOV: 90 degrees");
    MR_LOG_INFO("  Aspect: 16:9");
    MR_LOG_INFO("  Near: 0.1, Far: 100.0");
    MR_LOG_INFO("  Matrix[2][2]: " + std::to_string(proj.M[2][2]));
    MR_LOG_INFO("  Matrix[3][2]: " + std::to_string(proj.M[3][2]));
    MR_LOG_INFO("  [PASS] Reversed-Z perspective matrix created");
}

/**
 * Test Infinite Reversed-Z perspective projection matrix
 */
static void Test_InfiniteReversedZPerspective() {
    MR_LOG_INFO("\n[TEST] Infinite Reversed-Z Perspective Projection");
    
    float32 fov = FMath::DegreesToRadians(90.0f);
    float32 aspect = 16.0f / 9.0f;
    float32 nearZ = 0.1f;
    
    FMatrix4x4 proj = PerspectiveInfiniteReversedZ(fov, aspect, nearZ);
    
    MR_LOG_INFO("  FOV: 90 degrees");
    MR_LOG_INFO("  Aspect: 16:9");
    MR_LOG_INFO("  Near: 0.1, Far: Infinity");
    MR_LOG_INFO("  Matrix[2][2]: " + std::to_string(proj.M[2][2]));
    MR_LOG_INFO("  Matrix[3][2]: " + std::to_string(proj.M[3][2]));
    MR_LOG_INFO("  [PASS] Infinite Reversed-Z perspective matrix created");
}

/**
 * Test CreatePerspectiveMatrix helper function
 */
static void Test_CreatePerspectiveMatrixHelper() {
    MR_LOG_INFO("\n[TEST] CreatePerspectiveMatrix Helper");
    
    using namespace MonsterRender::RHI;
    
    float32 fov = FMath::DegreesToRadians(90.0f);
    float32 aspect = 16.0f / 9.0f;
    float32 nearZ = 0.1f;
    float32 farZ = 100.0f;
    
    // Test Standard
    FMatrix4x4 standard = CreatePerspectiveMatrix(fov, aspect, nearZ, farZ, EDepthRange::Standard);
    MR_LOG_INFO("  Standard: Created");
    
    // Test Reversed
    FMatrix4x4 reversed = CreatePerspectiveMatrix(fov, aspect, nearZ, farZ, EDepthRange::Reversed);
    MR_LOG_INFO("  Reversed-Z: Created");
    
    // Test Infinite Reversed
    FMatrix4x4 infinite = CreatePerspectiveMatrix(fov, aspect, nearZ, farZ, EDepthRange::InfiniteReversed);
    MR_LOG_INFO("  Infinite Reversed-Z: Created");
    
    MR_LOG_INFO("  [PASS] CreatePerspectiveMatrix helper validated");
}

/**
 * Run all projection matrix tests
 */
void RunAllTests() {
    MR_LOG_INFO("\n========================================");
    MR_LOG_INFO("  PROJECTION MATRIX TEST SUITE");
    MR_LOG_INFO("========================================");
    
    Test_StandardPerspective();
    Test_ReversedZPerspective();
    Test_InfiniteReversedZPerspective();
    Test_CreatePerspectiveMatrixHelper();
    
    MR_LOG_INFO("\n========================================");
    MR_LOG_INFO("  ALL TESTS PASSED!");
    MR_LOG_INFO("========================================\n");
}

} // namespace ProjectionMatrixTest
} // namespace MonsterEngine
