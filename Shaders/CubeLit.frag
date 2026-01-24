#version 450

// Fragment shader for lit cube rendering (Vulkan version)
// Supports multiple lights with Blinn-Phong shading

layout(binding = 0) uniform TransformUBO {
    mat4 model;
    mat4 view;
    mat4 projection;
    mat4 normalMatrix;
    vec4 cameraPosition;
    vec4 textureBlend;
} transform;

struct LightData {
    vec4 position;    // xyz = position/direction, w = type (0=directional, 1=point/spot)
    vec4 color;       // rgb = color, a = intensity
    vec4 params;      // x = radius, y = source radius, z/w = reserved
};

layout(binding = 3) uniform LightingUBO {
    LightData lights[8];
    vec4 ambientColor;
    int numLights;
    float padding1;
    float padding2;
    float padding3;
} lighting;

// Material uniform buffer (Set 1, Binding 0 in descriptor set layout)
// For now using binding 4 in the same set for compatibility
layout(binding = 4) uniform MaterialUBO {
    vec4 baseColor;     // Base color (RGBA)
    float metallic;     // Metallic value [0,1]
    float roughness;    // Roughness value [0,1]
    float specular;     // Specular intensity [0,1]
    float padding;      // Padding for alignment
} material;

layout(binding = 1) uniform sampler2D texture1;
layout(binding = 2) uniform sampler2D texture2;

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec3 fragViewPos;
layout(location = 4) flat in int vertexID;

layout(location = 0) out vec4 outColor;

// Calculate lighting contribution from a single light
vec3 calculateLight(int lightIndex, vec3 normal, vec3 viewDir, vec3 baseColor) {
    LightData light = lighting.lights[lightIndex];
    
    vec3 lightDir;
    float attenuation = 1.0;
    
    // Determine light direction and attenuation based on light type
    if (light.position.w < 0.5) {
        // Directional light - position is actually direction
        lightDir = normalize(-light.position.xyz);
    } else {
        // Point/Spot light
        vec3 lightVec = light.position.xyz - fragWorldPos;
        float distance = length(lightVec);
        lightDir = normalize(lightVec);
        
        // Distance attenuation
        float radius = light.params.x;
        if (radius > 0.0) {
            attenuation = clamp(1.0 - (distance / radius), 0.0, 1.0);
            attenuation *= attenuation;  // Quadratic falloff
        }
    }
    
    vec3 lightColor = light.color.rgb;
    float intensity = light.color.a;
    
    // Diffuse (Lambertian)
    float NdotL = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = NdotL * lightColor * intensity;
    
    // Specular (Blinn-Phong)
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float NdotH = max(dot(normal, halfwayDir), 0.0);
    float specPower = 32.0;  // Shininess
    float spec = pow(NdotH, specPower);
    vec3 specular = spec * lightColor * intensity * 0.5;
    
    return (diffuse + specular) * attenuation;
}

void main() {
    // Normalize interpolated normal
    vec3 normal = normalize(fragNormal);
    
    // View direction (from fragment to camera)
    vec3 viewDir = normalize(fragViewPos - fragWorldPos);
    
    // Get base color from material UBO
    // If material base color is valid (not default white), use it
    // Otherwise fall back to texture sampling
    vec3 baseColor = material.baseColor.rgb;
    
    // If material base color is default (1,1,1), try to use textures
    if (abs(material.baseColor.r - 1.0) < 0.01 && 
        abs(material.baseColor.g - 1.0) < 0.01 && 
        abs(material.baseColor.b - 1.0) < 0.01) {
        // Sample textures
        vec4 texColor1 = texture(texture1, fragTexCoord);
        vec4 texColor2 = texture(texture2, fragTexCoord);
        
        // Blend textures based on blend factor from UBO
        float blendFactor = transform.textureBlend.x;
        baseColor = mix(texColor1.rgb, texColor2.rgb, blendFactor);
        
        // If no texture, use material base color
        if (texColor1.a < 0.01 && texColor2.a < 0.01) {
            baseColor = material.baseColor.rgb;
        }
    }
    
    // Ambient lighting
    vec3 ambient = lighting.ambientColor.rgb * baseColor;
    
    // Accumulate lighting from all lights
    vec3 totalLighting = vec3(0.0);
    int lightCount = min(lighting.numLights, 8);
    
    for (int i = 0; i < lightCount; i++) {
        totalLighting += calculateLight(i, normal, viewDir, baseColor);
    }
    
    // Final color = ambient + diffuse + specular
    // Apply material specular intensity
    vec3 result = ambient + totalLighting * baseColor * material.specular;
    
    // Tone mapping (simple Reinhard)
    result = result / (result + vec3(1.0));
    
    outColor = vec4(result, material.baseColor.a);
}
