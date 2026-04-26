// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file DeferredUniformTypes.h
 * @brief 延迟渲染 MVP 使用的 Uniform Buffer C++ 类型定义
 *
 * 本文件定义的 C++ struct 与 Shaders/Deferred/*.{vert,frag} 中的
 * GLSL uniform block 严格对应，遵循 std140 布局规则。
 *
 * 设计策略：
 *   为了消除 std140 中 vec3 后面必须补 padding 的陷阱，
 *   所有标量 / 小向量都打包到 FVector4f 的 4 个分量中：
 *     - vec3 + float  →  vec4 (xyz = 原向量, w = 标量)
 *     - 单个 float    →  vec4 (x = float, yzw 保留)
 *
 * 对应 GLSL：
 *   Shaders/Deferred/GeometryPass.vert  ← FDeferredTransformUBO
 *   Shaders/Deferred/LightingPass.frag  ← FDeferredSceneUBO
 */

#include "Math/MathFwd.h"
#include "Math/Matrix.h"
#include "Math/Vector4.h"

#include <cstddef>  // offsetof

namespace MonsterEngine
{
namespace Deferred
{

// ============================================================================
// FDeferredTransformUBO
//   Geometry Pass 顶点着色器使用，per-object 每帧更新。
//   对应 GLSL：layout(set = 0, binding = 0) uniform TransformUBO
// ============================================================================
struct alignas(16) FDeferredTransformUBO
{
    /** 模型矩阵（Local → World） */
    Math::FMatrix44f Model;                     // offset  0, size 64

    /** 视图矩阵（World → View） */
    Math::FMatrix44f View;                      // offset 64, size 64

    /** 投影矩阵（View → Clip） */
    Math::FMatrix44f Proj;                      // offset 128, size 64

    /**
     * 法线变换矩阵 = inverse-transpose(Model)
     * 在 row-vector 约定下，法线变换为：worldNormal = normal * mat3(NormalMatrix)
     */
    Math::FMatrix44f NormalMatrix;              // offset 192, size 64

    /** 相机世界坐标（xyz = pos, w = 1.0） */
    Math::FVector4f  CameraPos;                 // offset 256, size 16

    /** TAA: Previous frame model matrix for motion vector calculation */
    Math::FMatrix44f PreviousModel;             // offset 272, size 64

    /** Alignment padding to maintain std140 layout */
    Math::FVector4f  Padding;                   // offset 336, size 16
};

// ----- FDeferredTransformUBO 布局静态验证 -----
static_assert(sizeof(Math::FMatrix44f) == 64,
    "FMatrix44f must be exactly 64 bytes (4x4 float)!");
static_assert(sizeof(Math::FVector4f) == 16,
    "FVector4f must be exactly 16 bytes (4 floats)!");

static_assert(sizeof(FDeferredTransformUBO) == 352,
    "FDeferredTransformUBO size mismatch - must be 352 bytes for std140!");
static_assert(offsetof(FDeferredTransformUBO, Model)          == 0,
    "FDeferredTransformUBO::Model offset mismatch!");
static_assert(offsetof(FDeferredTransformUBO, View)           == 64,
    "FDeferredTransformUBO::View offset mismatch!");
static_assert(offsetof(FDeferredTransformUBO, Proj)           == 128,
    "FDeferredTransformUBO::Proj offset mismatch!");
static_assert(offsetof(FDeferredTransformUBO, NormalMatrix)   == 192,
    "FDeferredTransformUBO::NormalMatrix offset mismatch!");
static_assert(offsetof(FDeferredTransformUBO, CameraPos)      == 256,
    "FDeferredTransformUBO::CameraPos offset mismatch!");
static_assert(offsetof(FDeferredTransformUBO, PreviousModel)  == 272,
    "FDeferredTransformUBO::PreviousModel offset mismatch!");


// ============================================================================
// FDeferredSceneUBO
//   Lighting Pass 片段着色器使用，per-frame 每帧更新 1 次。
//   对应 GLSL：layout(set = 0, binding = 3) uniform SceneUBO
//
//   vec4 打包语义：
//     CameraPos                    : xyz = 相机位置,     w = 1.0
//     DirLightDirection            : xyz = 光线方向,     w = 0.0
//     DirLightColorIntensity       : xyz = 光源颜色,     w = 强度
//     PointLightPositionRadius     : xyz = 光源位置,     w = 作用半径
//     PointLightColorIntensity     : xyz = 光源颜色,     w = 强度
//     Ambient                      : x   = 环境光系数,   yzw 保留
// ============================================================================
struct alignas(16) FDeferredSceneUBO
{
    /**
     * (ViewMatrix * ProjMatrix) 的逆矩阵
     * row-vector 约定下：worldPos = clipPos * InvViewProj
     */
    Math::FMatrix44f InvViewProj;                   // offset   0, size 64

    /** 相机位置（xyz = pos, w = 1.0） */
    Math::FVector4f  CameraPos;                     // offset  64, size 16

    /** 平行光方向 —— 光线传播方向，非指向光源（xyz = dir, w = 0） */
    Math::FVector4f  DirLightDirection;             // offset  80, size 16

    /** 平行光颜色 + 强度（xyz = color, w = intensity） */
    Math::FVector4f  DirLightColorIntensity;        // offset  96, size 16

    /** 点光源位置 + 作用半径（xyz = pos, w = radius） */
    Math::FVector4f  PointLightPositionRadius;      // offset 112, size 16

    /** 点光源颜色 + 强度（xyz = color, w = intensity） */
    Math::FVector4f  PointLightColorIntensity;      // offset 128, size 16

    /** 环境光（x = ambientFactor, yzw 保留） */
    Math::FVector4f  Ambient;                       // offset 144, size 16

    /** TAA: Previous frame View-Projection matrix for motion vector calculation */
    Math::FMatrix44f PreviousViewProj;              // offset 160, size 64

    /** TAA: Jitter offset (xy = current jitter, zw = previous jitter) */
    Math::FVector4f  JitterOffset;                  // offset 224, size 16

    /** TAA: Parameters (x = blendFactor, y = sharpness, z = enableSharpening, w = reserved) */
    Math::FVector4f  TAAParams;                     // offset 240, size 16
};

// ----- FDeferredSceneUBO 布局静态验证 -----
static_assert(sizeof(FDeferredSceneUBO) == 256,
    "FDeferredSceneUBO size mismatch - must be 256 bytes for std140!");
static_assert(offsetof(FDeferredSceneUBO, InvViewProj)               == 0,
    "FDeferredSceneUBO::InvViewProj offset mismatch!");
static_assert(offsetof(FDeferredSceneUBO, CameraPos)                 == 64,
    "FDeferredSceneUBO::CameraPos offset mismatch!");
static_assert(offsetof(FDeferredSceneUBO, DirLightDirection)         == 80,
    "FDeferredSceneUBO::DirLightDirection offset mismatch!");
static_assert(offsetof(FDeferredSceneUBO, DirLightColorIntensity)    == 96,
    "FDeferredSceneUBO::DirLightColorIntensity offset mismatch!");
static_assert(offsetof(FDeferredSceneUBO, PointLightPositionRadius)  == 112,
    "FDeferredSceneUBO::PointLightPositionRadius offset mismatch!");
static_assert(offsetof(FDeferredSceneUBO, PointLightColorIntensity)  == 128,
    "FDeferredSceneUBO::PointLightColorIntensity offset mismatch!");
static_assert(offsetof(FDeferredSceneUBO, Ambient)                   == 144,
    "FDeferredSceneUBO::Ambient offset mismatch!");
static_assert(offsetof(FDeferredSceneUBO, PreviousViewProj)          == 160,
    "FDeferredSceneUBO::PreviousViewProj offset mismatch!");
static_assert(offsetof(FDeferredSceneUBO, JitterOffset)              == 224,
    "FDeferredSceneUBO::JitterOffset offset mismatch!");
static_assert(offsetof(FDeferredSceneUBO, TAAParams)                 == 240,
    "FDeferredSceneUBO::TAAParams offset mismatch!");

} // namespace Deferred
} // namespace MonsterEngine
