#version 330 core

// ImGui Fragment Shader for OpenGL
// Samples texture and multiplies by vertex color

in vec2 fragTexCoord;
in vec4 fragColor;

out vec4 outColor;

uniform sampler2D fontTexture;

void main() {
    outColor = fragColor * texture(fontTexture, fragTexCoord);
}
