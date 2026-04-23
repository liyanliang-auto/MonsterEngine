#version 450 core

// ============================================================================
// Deferred Rendering - Geometry Pass Vertex Shader
// 将顶点变换到裁剪空间，并把世界坐标、世界法线、UV 传递给片段着色器
// 约定：UE5 row-vector（v * M），与 CubeLit.vert 保持一致
// ============================================================================

// -----------------------------------------------------------------------------
// Uniform 缓冲（std140 布局，对应 FDeferredTransformUBO）
// -----------------------------------------------------------------------------
layout(set = 0, binding = 0) uniform TransformUBO {
    mat4 model;                 // offset 0
    mat4 view;                  // offset 64
    mat4 proj;                  // offset 128
    mat4 normalMatrix;          // offset 192  inverse-transpose(model)
    vec4 cameraPos;             // offset 256  xyz=pos, w=1.0
} ubo;

// -----------------------------------------------------------------------------
// 顶点输入（布局需与 FCubeLitVertex 匹配：pos(vec3) + normal(vec3) + uv(vec2)）
// -----------------------------------------------------------------------------
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

// -----------------------------------------------------------------------------
// 输出到片段着色器
// -----------------------------------------------------------------------------
layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vWorldNormal;
layout(location = 2) out vec2 vTexCoord;

void main() {
    // UE5 row-vector convention: v * M（向量在左，矩阵在右）
    vec4 worldPos = vec4(inPosition, 1.0) * ubo.model;
    vec4 viewPos  = worldPos * ubo.view;
    vec4 clipPos  = viewPos * ubo.proj;

    // 世界坐标（用于 Lighting Pass 的 Position 重建对比验证）
    vWorldPos = worldPos.xyz;

    // 世界法线（row-vector 下使用 inverse-transpose(model)）
    vWorldNormal = normalize(inNormal * mat3(ubo.normalMatrix));

    // UV 直接透传
    vTexCoord = inTexCoord;

    gl_Position = clipPos;
}
