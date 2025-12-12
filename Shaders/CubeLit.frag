#version 450

layout(binding = 0) uniform TransformUBO {
    mat4 model;
    mat4 view;
    mat4 projection;
    mat4 normalMatrix;
    vec4 cameraPosition;
    vec4 textureBlend;
} transform;

struct LightData {
    vec4 position;
    vec4 direction;
    vec4 color;
    vec4 params;
};

layout(binding = 3) uniform LightingUBO {
    LightData lights[8];
    vec4 ambientColor;
    int numLights;
    float padding1;
    float padding2;
    float padding3;
} lighting;

layout(binding = 1) uniform sampler2D texture1;
layout(binding = 2) uniform sampler2D texture2;

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec3 fragViewPos;

layout(location = 0) out vec4 outColor;

void main() {
    // Simple color based on normal direction for debugging
    vec3 normalColor = normalize(fragNormal) * 0.5 + 0.5;
    outColor = vec4(normalColor, 1.0);
}