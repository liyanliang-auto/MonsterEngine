#version 450
// Fullscreen fragment shader for presenting SplatPipeline output to swapchain.
// Samples the RGBA8 storage image output from the splat render pass.

layout(set = 0, binding = 0) uniform sampler2D splatOutput;

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

void main()
{
    // Vulkan: flip Y to match swapchain coordinate system
    vec2 uv = vec2(fragUV.x, 1.0 - fragUV.y);
    outColor = texture(splatOutput, uv);
}
