#version 450
// Fullscreen fragment shader for presenting SplatPipeline output to swapchain.
// Samples the RGBA8 storage image output from the splat render pass.

layout(set = 0, binding = 0) uniform sampler2D splatOutput;

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

void main()
{
    // VulkanPendingState already uses a negative-height viewport. A second
    // fragment UV flip cancels it and displays the compute image upside down
    // for the source-camera convention used by the Bicycle scene.
    outColor = texture(splatOutput, fragUV);
}
