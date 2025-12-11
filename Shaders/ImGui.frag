#version 450

// ImGui Fragment Shader for Vulkan
// Samples texture and multiplies by vertex color

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 1) uniform sampler2D fontTexture;

void main() {
    outColor = inColor * texture(fontTexture, inTexCoord);
}
