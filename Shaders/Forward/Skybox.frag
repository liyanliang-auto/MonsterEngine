// Copyright Monster Engine. All Rights Reserved.
// Skybox Fragment Shader
// 
// Samples a cubemap texture for skybox rendering.
// Supports tint color and intensity adjustment.
//
// Reference: UE5 SkyPassPixelShader.usf

#version 450

// ============================================================================
// Fragment Input
// ============================================================================

layout(location = 0) in vec3 inTexCoord;    // Direction for cubemap sampling

// ============================================================================
// Fragment Output
// ============================================================================

layout(location = 0) out vec4 outColor;

// ============================================================================
// Uniforms
// ============================================================================

layout(set = 0, binding = 2) uniform SkyboxData
{
    vec4 SkyboxTint;        // xyz = tint color, w = intensity
    vec4 SunDirection;      // xyz = sun direction, w = sun disk size
    vec4 SunColor;          // xyz = sun color, w = sun intensity
    float Exposure;         // Exposure adjustment
    float Rotation;         // Y-axis rotation in radians
    float Padding0;
    float Padding1;
} Skybox;

// ============================================================================
// Textures
// ============================================================================

layout(set = 1, binding = 0) uniform samplerCube SkyboxCubemap;

// ============================================================================
// Helper Functions
// ============================================================================

// Rotate direction around Y axis
vec3 RotateY(vec3 dir, float angle)
{
    float s = sin(angle);
    float c = cos(angle);
    return vec3(
        c * dir.x + s * dir.z,
        dir.y,
        -s * dir.x + c * dir.z
    );
}

// Simple sun disk
float SunDisk(vec3 viewDir, vec3 sunDir, float sunSize)
{
    float cosAngle = dot(viewDir, sunDir);
    float sunDisk = smoothstep(1.0 - sunSize * 0.01, 1.0, cosAngle);
    return sunDisk;
}

// ============================================================================
// Main
// ============================================================================

void main()
{
    // Apply rotation to sampling direction
    vec3 sampleDir = RotateY(normalize(inTexCoord), Skybox.Rotation);
    
    // Sample cubemap
    vec3 skyColor = texture(SkyboxCubemap, sampleDir).rgb;
    
    // Apply tint and intensity
    skyColor *= Skybox.SkyboxTint.rgb * Skybox.SkyboxTint.w;
    
    // Add sun disk (optional)
    if (Skybox.SunColor.w > 0.0)
    {
        float sun = SunDisk(sampleDir, Skybox.SunDirection.xyz, Skybox.SunDirection.w);
        skyColor += Skybox.SunColor.rgb * Skybox.SunColor.w * sun;
    }
    
    // Apply exposure
    skyColor *= Skybox.Exposure;
    
    // Tone mapping (simple Reinhard)
    skyColor = skyColor / (skyColor + vec3(1.0));
    
    // Gamma correction
    skyColor = pow(skyColor, vec3(1.0 / 2.2));
    
    outColor = vec4(skyColor, 1.0);
}
