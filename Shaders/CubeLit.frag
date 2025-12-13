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
layout(location = 4) flat in int vertexID;

layout(location = 0) out vec4 outColor;

void main() {
    // DEBUG: Color based on world position to verify vertex data is correct
    // This should show a gradient across the cube
    vec3 posColor = (fragWorldPos + vec3(0.5)) * 0.8 + vec3(0.2);
    outColor = vec4(posColor, 1.0);
}
