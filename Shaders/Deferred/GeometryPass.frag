#version 450 core

// ============================================================================
// Deferred Rendering - Geometry Pass Fragment Shader
// 将几何信息写入 GBuffer（2 个 Color RT + 自动写入的 Depth）
//
// GBuffer 布局（MVP 版本）：
//   RT0 gNormal : RGBA32F  — xyz = 世界法线, w = 保留
//   RT1 gAlbedo : RGBA8    — rgb = 基础颜色, a = 未使用
//   Depth       : D24S8    — 光栅化自动写入
//
// 备注：
//   - Normal 使用 RGBA32F 是因为当前 EPixelFormat 枚举缺少 RGBA16F
//     后续扩展 EPixelFormat 后可切换为 R16G16B16A16_FLOAT 节省内存
//   - MVP 不输出 Metallic/Roughness（Blinn-Phong 暂不需要）
// ============================================================================

// -----------------------------------------------------------------------------
// 从顶点着色器传入
// -----------------------------------------------------------------------------
layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vWorldNormal;
layout(location = 2) in vec2 vTexCoord;

// -----------------------------------------------------------------------------
// 纹理采样器（与 CubeLit 兼容：binding = 1 是 Albedo）
// -----------------------------------------------------------------------------
layout(set = 0, binding = 1) uniform sampler2D albedoMap;

// -----------------------------------------------------------------------------
// GBuffer 输出（MRT）
// -----------------------------------------------------------------------------
layout(location = 0) out vec4 outNormal;
layout(location = 1) out vec4 outAlbedo;

void main() {
    // 1. 输出归一化后的世界法线（.w 预留作为 Shading Model ID 等用途）
    outNormal = vec4(normalize(vWorldNormal), 0.0);

    // 2. 采样 Albedo 贴图；若需要支持 SRGB 请在纹理创建时设置
    vec4 albedo = texture(albedoMap, vTexCoord);
    outAlbedo = vec4(albedo.rgb, 1.0);

    // 3. Depth 由光栅化硬件自动写入 Depth Buffer，无需显式输出
}
