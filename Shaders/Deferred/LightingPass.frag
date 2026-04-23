#version 450 core

// ============================================================================
// Deferred Rendering - Lighting Pass Fragment Shader
// 从 GBuffer 采样几何信息，重建世界坐标，计算 Blinn-Phong 光照
//
// 输入：
//   binding 0 : gNormal   — 世界法线
//   binding 1 : gAlbedo   — 基础颜色
//   binding 2 : gDepth    — 深度缓冲（用于 Position 重建）
//   binding 3 : SceneUBO  — 相机 + 光源数据
//
// 输出：
//   location 0 : 最终颜色（RGBA8）
//
// 约定：UE5 row-vector（v * M）
// ============================================================================

// -----------------------------------------------------------------------------
// 从顶点着色器传入
// -----------------------------------------------------------------------------
layout(location = 0) in vec2 vScreenUV;

// -----------------------------------------------------------------------------
// GBuffer 纹理
// -----------------------------------------------------------------------------
layout(set = 0, binding = 0) uniform sampler2D gNormal;
layout(set = 0, binding = 1) uniform sampler2D gAlbedo;
layout(set = 0, binding = 2) uniform sampler2D gDepth;

// -----------------------------------------------------------------------------
// 场景 Uniform（对应 FDeferredSceneUBO，vec4 打包消除 std140 对齐陷阱）
// -----------------------------------------------------------------------------
layout(set = 0, binding = 3) uniform SceneUBO {
    mat4 invViewProj;                   // offset   0  (view * proj)^-1
    vec4 cameraPos;                     // offset  64  xyz=pos
    vec4 dirLightDirection;             // offset  80  xyz=dir
    vec4 dirLightColorIntensity;        // offset  96  xyz=color, w=intensity
    vec4 pointLightPositionRadius;      // offset 112  xyz=pos,   w=radius
    vec4 pointLightColorIntensity;      // offset 128  xyz=color, w=intensity
    vec4 ambient;                       // offset 144  x=ambientFactor
} scene;

// -----------------------------------------------------------------------------
// 输出：最终颜色
// -----------------------------------------------------------------------------
layout(location = 0) out vec4 outColor;

// -----------------------------------------------------------------------------
// 辅助常量
// -----------------------------------------------------------------------------
const float SHININESS = 32.0;

// ============================================================================
// 工具函数
// ============================================================================

// 从屏幕 UV + 深度重建世界坐标
// row-vector：worldPos = clipPos * invViewProj
vec3 reconstructWorldPos(vec2 uv, float depth) {
    // NDC 坐标
    //   xy: UV [0,1]  映射到 [-1, 1]
    //   z : Vulkan 的深度已经是 [0, 1]，不需要再 * 2 - 1（与 OpenGL 不同）
    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth, 1.0);

    // row-vector 下逆变换到世界空间
    vec4 worldPos4 = clipPos * scene.invViewProj;

    // 透视除法（关键步骤，不能省！）
    return worldPos4.xyz / worldPos4.w;
}

// Blinn-Phong 单光源贡献
//   lightVec : 从物体指向光源的方向（已归一化）
//   N : 世界法线（已归一化）
//   V : 视线方向，从物体指向相机（已归一化）
//   albedo : 基础颜色
//   lightColor : 光源颜色
//   intensity : 光源强度（含衰减后）
vec3 blinnPhong(
    vec3 lightVec, vec3 N, vec3 V,
    vec3 albedo, vec3 lightColor, float intensity)
{
    vec3 H = normalize(lightVec + V);

    float NdotL = max(dot(N, lightVec), 0.0);
    float NdotH = max(dot(N, H), 0.0);

    // Diffuse 乘 albedo（物体本身颜色）
    vec3 diffuse = NdotL * lightColor * albedo;

    // Specular 不乘 albedo（高光是光的反射，而非物体颜色）
    vec3 specular = pow(NdotH, SHININESS) * lightColor;

    return (diffuse + specular) * intensity;
}

// ============================================================================
// Main
// ============================================================================
void main() {
    // 1. 采样 GBuffer
    vec3 normal = texture(gNormal, vScreenUV).xyz;
    vec3 albedo = texture(gAlbedo, vScreenUV).rgb;
    float depth = texture(gDepth, vScreenUV).r;

    // 2. 早退：天空盒区域（深度为 1.0 表示没有几何）
    if (depth >= 1.0) {
        outColor = vec4(0.1, 0.1, 0.15, 1.0);    // 深灰蓝背景色
        return;
    }

    // 3. 重建世界坐标
    vec3 worldPos = reconstructWorldPos(vScreenUV, depth);

    // 4. 准备光照向量（全部归一化）
    vec3 N = normalize(normal);
    vec3 V = normalize(scene.cameraPos.xyz - worldPos);

    // 5. 环境光（循环外，只加一次；避免多光源时重复叠加环境项）
    vec3 ambientColor = albedo * scene.ambient.x;

    // 6. 平行光贡献
    //    注意：dirLightDirection 是“光线传播方向”，光源到物体的方向取反即为 L
    vec3 L_dir = -normalize(scene.dirLightDirection.xyz);
    vec3 dirLight = blinnPhong(
        L_dir, N, V,
        albedo,
        scene.dirLightColorIntensity.xyz,
        scene.dirLightColorIntensity.w
    );

    // 7. 点光源贡献（带距离衰减）
    vec3 toLight = scene.pointLightPositionRadius.xyz - worldPos;
    float distance = length(toLight);
    float radius = scene.pointLightPositionRadius.w;

    // 线性衰减后取平方（平滑的 falloff）
    float attenuation = clamp(1.0 - distance / radius, 0.0, 1.0);
    attenuation *= attenuation;

    vec3 L_point = (distance > 1e-4) ? (toLight / distance) : vec3(0.0, 1.0, 0.0);
    vec3 pointLight = blinnPhong(
        L_point, N, V,
        albedo,
        scene.pointLightColorIntensity.xyz,
        scene.pointLightColorIntensity.w * attenuation
    );

    // 8. 汇总输出
    vec3 finalColor = ambientColor + dirLight + pointLight;
    outColor = vec4(finalColor, 1.0);
}
