#include "RHI/ShaderPreprocessor.h"
#include "Core/Logging/Logging.h"
#include <cassert>

using namespace MonsterRender;
using namespace MonsterRender::RHI;

void TestPreprocessor_VulkanTarget()
{
    FString input = "layout(set = 0, binding = 0) uniform sampler2D tex;";
    FString output = FShaderPreprocessor::PreprocessShader(input, ERHIBackend::Vulkan);
    
    // Vulkan target should return original shader
    assert(input == output);
    printf("  ✓ TestPreprocessor_VulkanTarget: PASSED\n");
}

void TestPreprocessor_OpenGLTarget_SimpleCase()
{
    FString input = "layout(set = 0, binding = 0) uniform sampler2D tex;";
    FString expected = "layout(binding = 0) uniform sampler2D tex;";
    FString output = FShaderPreprocessor::PreprocessShader(input, ERHIBackend::OpenGL);
    
    assert(output == expected);
    printf("  ✓ TestPreprocessor_OpenGLTarget_SimpleCase: PASSED\n");
}

void TestPreprocessor_OpenGLTarget_WithQualifiers()
{
    FString input = "layout(set = 0, binding = 0, std140) uniform ViewUBO { mat4 view; };";
    FString expected = "layout(binding = 0, std140) uniform ViewUBO { mat4 view; };";
    FString output = FShaderPreprocessor::PreprocessShader(input, ERHIBackend::OpenGL);
    
    assert(output == expected);
    printf("  ✓ TestPreprocessor_OpenGLTarget_WithQualifiers: PASSED\n");
}

void TestPreprocessor_OpenGLTarget_LinearFlatten()
{
    // Test: (set=1, binding=1) -> globalBinding = 1*8+1 = 9
    FString input = "layout(set = 1, binding = 1) uniform sampler2D shadowMap;";
    FString expected = "layout(binding = 9) uniform sampler2D shadowMap;";
    FString output = FShaderPreprocessor::PreprocessShader(input, ERHIBackend::OpenGL);
    
    assert(output == expected);
    printf("  ✓ TestPreprocessor_OpenGLTarget_LinearFlatten: PASSED\n");
}

void TestPreprocessor_OpenGLTarget_MultipleLayouts()
{
    FString input = 
        "layout(set = 0, binding = 0) uniform sampler2D tex0;\n"
        "layout(set = 1, binding = 1) uniform sampler2D tex1;\n"
        "layout(set = 2, binding = 1) uniform sampler2D tex2;";
    
    FString expected = 
        "layout(binding = 0) uniform sampler2D tex0;\n"
        "layout(binding = 9) uniform sampler2D tex1;\n"
        "layout(binding = 17) uniform sampler2D tex2;";
    
    FString output = FShaderPreprocessor::PreprocessShader(input, ERHIBackend::OpenGL);
    
    assert(output == expected);
    printf("  ✓ TestPreprocessor_OpenGLTarget_MultipleLayouts: PASSED\n");
}

void TestGetGlobalBinding()
{
    // Test linear flatten formula
    assert(GetGlobalBinding(0, 0) == 0);
    assert(GetGlobalBinding(0, 1) == 1);
    assert(GetGlobalBinding(1, 0) == 8);
    assert(GetGlobalBinding(1, 1) == 9);
    assert(GetGlobalBinding(2, 1) == 17);
    assert(GetGlobalBinding(3, 0) == 24);
    
    printf("  ✓ TestGetGlobalBinding: PASSED\n");
}

void TestGetSetAndBinding()
{
    uint32 set, binding;
    
    GetSetAndBinding(0, set, binding);
    assert(set == 0 && binding == 0);
    
    GetSetAndBinding(9, set, binding);
    assert(set == 1 && binding == 1);
    
    GetSetAndBinding(17, set, binding);
    assert(set == 2 && binding == 1);
    
    GetSetAndBinding(24, set, binding);
    assert(set == 3 && binding == 0);
    
    printf("  ✓ TestGetSetAndBinding: PASSED\n");
}

void RunShaderPreprocessorTests()
{
    printf("=== Running ShaderPreprocessor Tests ===\n");
    
    TestPreprocessor_VulkanTarget();
    TestPreprocessor_OpenGLTarget_SimpleCase();
    TestPreprocessor_OpenGLTarget_WithQualifiers();
    TestPreprocessor_OpenGLTarget_LinearFlatten();
    TestPreprocessor_OpenGLTarget_MultipleLayouts();
    TestGetGlobalBinding();
    TestGetSetAndBinding();
    
    printf("=== All ShaderPreprocessor Tests PASSED ===\n");
}
