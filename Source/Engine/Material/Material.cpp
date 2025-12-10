/**
 * @file Material.cpp
 * @brief Implementation of FMaterial class
 */

#include "Engine/Material/Material.h"
#include "Engine/Material/MaterialRenderProxy.h"
#include "Engine/Shader/ShaderManager.h"
#include "Core/Logging/LogMacros.h"

namespace MonsterEngine
{

// Define log category
DEFINE_LOG_CATEGORY_STATIC(LogMaterial, Log, All);

// ============================================================================
// FMaterial Implementation
// ============================================================================

FMaterial::FMaterial()
    : FMaterialInterface()
    , m_isCompiled(false)
    , m_isDirty(true)
{
    MR_LOG_DEBUG("FMaterial: Created unnamed material");
}

FMaterial::FMaterial(const FName& InName)
    : FMaterialInterface()
    , m_isCompiled(false)
    , m_isDirty(true)
{
    m_materialName = InName;
    MR_LOG_DEBUG("FMaterial: Created material '%s'", *InName.ToString());
}

FMaterial::~FMaterial()
{
    MR_LOG_DEBUG("FMaterial: Destroying material '%s'", *GetDebugName());
    
    // Release render proxy
    m_renderProxy.Reset();
    
    // Release pipeline state
    m_pipelineState.Reset();
}

// ============================================================================
// Render Proxy
// ============================================================================

FMaterialRenderProxy* FMaterial::GetRenderProxy()
{
    if (!m_renderProxy)
    {
        CreateRenderProxy();
    }
    return m_renderProxy.Get();
}

const FMaterialRenderProxy* FMaterial::GetRenderProxy() const
{
    return m_renderProxy.Get();
}

void FMaterial::CreateRenderProxy()
{
    m_renderProxy = MakeShared<FMaterialRenderProxy>(this);
    
    // Cache default parameter values in the proxy
    for (const auto& Param : m_scalarParameters)
    {
        m_renderProxy->SetCachedScalar(Param.ParameterInfo, Param.ParameterValue);
    }
    
    for (const auto& Param : m_vectorParameters)
    {
        m_renderProxy->SetCachedVector(Param.ParameterInfo, Param.ParameterValue);
    }
    
    for (const auto& Param : m_textureParameters)
    {
        m_renderProxy->SetCachedTexture(Param.ParameterInfo, Param.ParameterValue);
    }
    
    MR_LOG_DEBUG("FMaterial: Created render proxy for '%s'", *GetDebugName());
}

// ============================================================================
// Parameter Access
// ============================================================================

bool FMaterial::GetScalarParameterValue(const FMaterialParameterInfo& ParameterInfo, float& OutValue) const
{
    int32 Index = FindScalarParameterIndex(ParameterInfo.Name);
    if (Index != INDEX_NONE)
    {
        OutValue = m_scalarParameters[Index].ParameterValue;
        return true;
    }
    return false;
}

bool FMaterial::GetVectorParameterValue(const FMaterialParameterInfo& ParameterInfo, FLinearColor& OutValue) const
{
    int32 Index = FindVectorParameterIndex(ParameterInfo.Name);
    if (Index != INDEX_NONE)
    {
        OutValue = m_vectorParameters[Index].ParameterValue;
        return true;
    }
    return false;
}

bool FMaterial::GetTextureParameterValue(const FMaterialParameterInfo& ParameterInfo, FTexture*& OutValue) const
{
    int32 Index = FindTextureParameterIndex(ParameterInfo.Name);
    if (Index != INDEX_NONE)
    {
        OutValue = m_textureParameters[Index].ParameterValue;
        return true;
    }
    return false;
}

void FMaterial::GetUsedTextures(TArray<FTexture*>& OutTextures) const
{
    OutTextures.Empty();
    for (const auto& Param : m_textureParameters)
    {
        if (Param.ParameterValue != nullptr)
        {
            OutTextures.Add(Param.ParameterValue);
        }
    }
}

// ============================================================================
// Material Properties
// ============================================================================

void FMaterial::SetMaterialProperties(const FMaterialProperties& Properties)
{
    m_properties = Properties;
    MarkDirty();
}

void FMaterial::SetBlendMode(EMaterialBlendMode Mode)
{
    if (m_properties.BlendMode != Mode)
    {
        m_properties.BlendMode = Mode;
        MarkDirty();
    }
}

void FMaterial::SetShadingModel(EMaterialShadingModel Model)
{
    if (m_properties.ShadingModel != Model)
    {
        m_properties.ShadingModel = Model;
        MarkDirty();
    }
}

void FMaterial::SetTwoSided(bool bTwoSided)
{
    if (m_properties.bTwoSided != bTwoSided)
    {
        m_properties.bTwoSided = bTwoSided;
        MarkDirty();
    }
}

void FMaterial::SetOpacityMaskClipValue(float Value)
{
    if (m_properties.OpacityMaskClipValue != Value)
    {
        m_properties.OpacityMaskClipValue = Value;
        MarkDirty();
    }
}

// ============================================================================
// Default Parameter Values
// ============================================================================

void FMaterial::SetDefaultScalarParameter(const FName& ParameterName, float Value)
{
    int32 Index = FindScalarParameterIndex(ParameterName);
    if (Index != INDEX_NONE)
    {
        m_scalarParameters[Index].ParameterValue = Value;
    }
    else
    {
        m_scalarParameters.Add(FScalarParameterValue(ParameterName, Value));
    }
    
    // Update render proxy if exists
    if (m_renderProxy)
    {
        m_renderProxy->SetCachedScalar(FMaterialParameterInfo(ParameterName), Value);
    }
    
    MR_LOG_DEBUG("FMaterial: Set scalar parameter '%s' = %f", *ParameterName.ToString(), Value);
}

void FMaterial::SetDefaultVectorParameter(const FName& ParameterName, const FLinearColor& Value)
{
    int32 Index = FindVectorParameterIndex(ParameterName);
    if (Index != INDEX_NONE)
    {
        m_vectorParameters[Index].ParameterValue = Value;
    }
    else
    {
        m_vectorParameters.Add(FVectorParameterValue(ParameterName, Value));
    }
    
    // Update render proxy if exists
    if (m_renderProxy)
    {
        m_renderProxy->SetCachedVector(FMaterialParameterInfo(ParameterName), Value);
    }
    
    MR_LOG_DEBUG("FMaterial: Set vector parameter '%s' = (%f, %f, %f, %f)", 
                 *ParameterName.ToString(), Value.R, Value.G, Value.B, Value.A);
}

void FMaterial::SetDefaultTextureParameter(const FName& ParameterName, FTexture* Value)
{
    int32 Index = FindTextureParameterIndex(ParameterName);
    if (Index != INDEX_NONE)
    {
        m_textureParameters[Index].ParameterValue = Value;
    }
    else
    {
        m_textureParameters.Add(FTextureParameterValue(ParameterName, Value));
    }
    
    // Update render proxy if exists
    if (m_renderProxy)
    {
        m_renderProxy->SetCachedTexture(FMaterialParameterInfo(ParameterName), Value);
    }
    
    MR_LOG_DEBUG("FMaterial: Set texture parameter '%s'", *ParameterName.ToString());
}

// ============================================================================
// Shader Management
// ============================================================================

void FMaterial::SetVertexShader(TSharedPtr<FShader> Shader)
{
    m_vertexShader = Shader;
    InvalidatePipelineState();
}

void FMaterial::SetPixelShader(TSharedPtr<FShader> Shader)
{
    m_pixelShader = Shader;
    InvalidatePipelineState();
}

bool FMaterial::HasValidShaders() const
{
    return m_vertexShader.IsValid() && m_vertexShader->IsValid() &&
           m_pixelShader.IsValid() && m_pixelShader->IsValid();
}

bool FMaterial::LoadShadersFromFiles(const String& VertexShaderPath, const String& PixelShaderPath)
{
    MR_LOG_DEBUG("FMaterial: Loading shaders for '%s'", *GetDebugName());
    MR_LOG_DEBUG("  Vertex: %s", *VertexShaderPath);
    MR_LOG_DEBUG("  Pixel: %s", *PixelShaderPath);
    
    // Store paths
    m_vertexShaderPath = VertexShaderPath;
    m_pixelShaderPath = PixelShaderPath;
    
    // Get shader manager
    FShaderManager& ShaderManager = FShaderManager::Get();
    
    if (!ShaderManager.IsInitialized())
    {
        MR_LOG_ERROR("FMaterial: ShaderManager not initialized");
        return false;
    }
    
    // Compile vertex shader
    auto VertexShader = ShaderManager.CompileVertexShader(VertexShaderPath);
    if (!VertexShader)
    {
        MR_LOG_ERROR("FMaterial: Failed to compile vertex shader '%s'", *VertexShaderPath);
        return false;
    }
    
    // Compile pixel shader
    auto PixelShader = ShaderManager.CompilePixelShader(PixelShaderPath);
    if (!PixelShader)
    {
        MR_LOG_ERROR("FMaterial: Failed to compile pixel shader '%s'", *PixelShaderPath);
        return false;
    }
    
    // Set shaders
    m_vertexShader = VertexShader;
    m_pixelShader = PixelShader;
    
    // Invalidate pipeline state
    InvalidatePipelineState();
    
    MR_LOG_DEBUG("FMaterial: Successfully loaded shaders for '%s'", *GetDebugName());
    return true;
}

// ============================================================================
// Pipeline State
// ============================================================================

TSharedPtr<MonsterRender::RHI::IRHIPipelineState> FMaterial::GetOrCreatePipelineState(
    MonsterRender::RHI::IRHIDevice* Device)
{
    if (!m_pipelineState && Device)
    {
        // TODO: Create pipeline state from shaders and material properties
        // This will be implemented when the shader system is complete
        MR_LOG_WARNING("FMaterial: Pipeline state creation not yet implemented");
    }
    return m_pipelineState;
}

void FMaterial::InvalidatePipelineState()
{
    m_pipelineState.Reset();
    m_isCompiled = false;
    m_isDirty = true;
}

// ============================================================================
// Compilation
// ============================================================================

bool FMaterial::Compile(MonsterRender::RHI::IRHIDevice* Device)
{
    if (!Device)
    {
        MR_LOG_ERROR("FMaterial: Cannot compile without RHI device");
        return false;
    }
    
    if (!HasValidShaders())
    {
        MR_LOG_ERROR("FMaterial: Cannot compile material '%s' - missing shaders", *GetDebugName());
        return false;
    }
    
    // Create pipeline state
    m_pipelineState = GetOrCreatePipelineState(Device);
    
    m_isCompiled = m_pipelineState.IsValid();
    m_isDirty = !m_isCompiled;
    
    if (m_isCompiled)
    {
        MR_LOG_DEBUG("FMaterial: Compiled material '%s'", *GetDebugName());
    }
    else
    {
        MR_LOG_ERROR("FMaterial: Failed to compile material '%s'", *GetDebugName());
    }
    
    return m_isCompiled;
}

// ============================================================================
// Dirty State
// ============================================================================

void FMaterial::MarkDirty()
{
    m_isDirty = true;
    m_isCompiled = false;
    
    if (m_renderProxy)
    {
        m_renderProxy->MarkDirty();
    }
}

// ============================================================================
// Helper Functions
// ============================================================================

int32 FMaterial::FindScalarParameterIndex(const FName& Name) const
{
    for (int32 i = 0; i < m_scalarParameters.Num(); ++i)
    {
        if (m_scalarParameters[i].ParameterInfo.Name == Name)
        {
            return i;
        }
    }
    return INDEX_NONE;
}

int32 FMaterial::FindVectorParameterIndex(const FName& Name) const
{
    for (int32 i = 0; i < m_vectorParameters.Num(); ++i)
    {
        if (m_vectorParameters[i].ParameterInfo.Name == Name)
        {
            return i;
        }
    }
    return INDEX_NONE;
}

int32 FMaterial::FindTextureParameterIndex(const FName& Name) const
{
    for (int32 i = 0; i < m_textureParameters.Num(); ++i)
    {
        if (m_textureParameters[i].ParameterInfo.Name == Name)
        {
            return i;
        }
    }
    return INDEX_NONE;
}

} // namespace MonsterEngine
