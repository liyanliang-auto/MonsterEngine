#version 330 core

// Fragment shader for lit cube rendering (OpenGL version)
// Supports multiple lights with Blinn-Phong shading

// Light structure
struct LightData {
    vec4 position;    // xyz = position/direction, w = type (0=directional, 1=point/spot)
    vec4 color;       // rgb = color, a = intensity
    vec4 params;      // x = radius, y = source radius, z/w = reserved
};

// Texture uniforms (not in UBO)
uniform sampler2D texture1;
uniform sampler2D texture2;
uniform float textureBlend;

// Light uniform block (matches binding point 3)
layout(std140) uniform LightUB
{
    LightData lights[8];
    vec3 ambientColor;
    int numLights;
};

// Inputs from vertex shader
in vec3 fragWorldPos;
in vec3 fragNormal;
in vec2 fragTexCoord;
in vec3 fragViewPos;

// Output color
out vec4 outColor;

// Calculate lighting contribution from a single light
vec3 calculateLight(int lightIndex, vec3 normal, vec3 viewDir, vec3 baseColor) {
    LightData light = lights[lightIndex];
    
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
    // DEBUG: Output normal as color to visualize if geometry is correct
    // outColor = vec4(normalize(fragNormal) * 0.5 + 0.5, 1.0); return;
    
    // Normalize interpolated normal
    vec3 normal = normalize(fragNormal);
    
    // View direction (from fragment to camera)
    vec3 viewDir = normalize(fragViewPos - fragWorldPos);
    
    // Sample textures
    vec4 texColor1 = texture(texture1, fragTexCoord);
    vec4 texColor2 = texture(texture2, fragTexCoord);
    
    // Blend textures based on blend factor
    vec3 baseColor = mix(texColor1.rgb, texColor2.rgb, textureBlend);
    
    // If no texture, use a default color
    if (texColor1.a < 0.01 && texColor2.a < 0.01) {
        baseColor = vec3(0.8, 0.6, 0.4);  // Default brownish color
    }
    
    // Ambient lighting
    vec3 ambient = ambientColor * baseColor;
    
    // Accumulate lighting from all lights
    vec3 totalLighting = vec3(0.0);
    int lightCount = min(numLights, 8);
    
    for (int i = 0; i < lightCount; i++) {
        totalLighting += calculateLight(i, normal, viewDir, baseColor);
    }
    
    // Final color = ambient + diffuse + specular
    vec3 result = ambient + totalLighting * baseColor;
    
    // Tone mapping (simple Reinhard)
    result = result / (result + vec3(1.0));
    
    outColor = vec4(result, 1.0);
}
