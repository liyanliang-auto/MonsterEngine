/**
 * @file MaterialRenderProxy.cpp
 * @brief Implementation of FMaterialRenderProxy class
 */

#include "Engine/Material/MaterialRenderProxy.h"
#include "Engine/Material/MaterialInterface.h"
#include "Engine/Material/Material.h"
#include "Core/Logging/LogMacros.h"
#include "RHI/IRHIDevice.h"
#include "RHI/IRHIResource.h"

namespace MonsterEngine
{

// Define log category
DEFINE_LOG_CATEGORY_STATIC(LogMaterialRenderProxy, Log, All);

// ============================================================================
// FMaterialRenderProxy Implementation
// ============================================================================

FMaterialRenderProxy::FMaterialRenderProxy()
    : m_material(nullptr)
    , m_uniformBufferDirty(true)
    , m_isDirty(true)
{
}

FMaterialRenderProxy::FMaterialRenderProxy(FMaterialInterface* InMaterial)
    : m_material(InMaterial)
    , m_uniformBufferDirty(true)
    , m_isDirty(true)
{
    MR_LOG_DEBUG("FMaterialRenderProxy: Created for material '%s'", 
                 InMaterial ? *InMaterial->GetDebugName() : TEXT("null"));
}

FMaterialRenderProxy::~FMaterialRenderProxy()
{
    MR_LOG_DEBUG("FMaterialRenderProxy: Destroying proxy");
    m_uniformBuffer.Reset();
}

// ============================================================================
// Material Access
// ============================================================================

FMaterial* FMaterialRenderProxy::GetMaterial() const
{
    if (m_material)
    {
        return m_material->GetMaterial();
    }
    return nullptr;
}

// ============================================================================
// Parameter Access
// ============================================================================

bool FMaterialRenderProxy::GetScalarValue(const FMaterialParameterInfo& ParameterInfo, float& OutValue) const
{
    // First check cached values
    for (const auto& Param : m_cachedScalars)
    {
        if (Param.ParameterInfo == ParameterInfo)
        {
            OutValue = Param.ParameterValue;
            return true;
        }
    }
    
    // Fall back to material interface
    if (m_material)
    {
        return m_material->GetScalarParameterValue(ParameterInfo, OutValue);
    }
    
    return false;
}

bool FMaterialRenderProxy::GetVectorValue(const FMaterialParameterInfo& ParameterInfo, FLinearColor& OutValue) const
{
    // First check cached values
    for (const auto& Param : m_cachedVectors)
    {
        if (Param.ParameterInfo == ParameterInfo)
        {
            OutValue = Param.ParameterValue;
            return true;
        }
    }
    
    // Fall back to material interface
    if (m_material)
    {
        return m_material->GetVectorParameterValue(ParameterInfo, OutValue);
    }
    
    return false;
}

bool FMaterialRenderProxy::GetTextureValue(const FMaterialParameterInfo& ParameterInfo, FTexture*& OutValue) const
{
    // First check cached values
    for (const auto& Param : m_cachedTextures)
    {
        if (Param.ParameterInfo == ParameterInfo)
        {
            OutValue = Param.ParameterValue;
            return true;
        }
    }
    
    // Fall back to material interface
    if (m_material)
    {
        return m_material->GetTextureParameterValue(ParameterInfo, OutValue);
    }
    
    return false;
}

// ============================================================================
// Cached Parameter Values
// ============================================================================

void FMaterialRenderProxy::SetCachedScalar(const FMaterialParameterInfo& ParameterInfo, float Value)
{
    // Check if already cached
    for (auto& Param : m_cachedScalars)
    {
        if (Param.ParameterInfo == ParameterInfo)
        {
            Param.ParameterValue = Value;
            m_uniformBufferDirty = true;
            return;
        }
    }
    
    // Add new cached value
    m_cachedScalars.Add(FScalarParameterValue(ParameterInfo, Value));
    m_uniformBufferDirty = true;
}

void FMaterialRenderProxy::SetCachedVector(const FMaterialParameterInfo& ParameterInfo, const FLinearColor& Value)
{
    // Check if already cached
    for (auto& Param : m_cachedVectors)
    {
        if (Param.ParameterInfo == ParameterInfo)
        {
            Param.ParameterValue = Value;
            m_uniformBufferDirty = true;
            return;
        }
    }
    
    // Add new cached value
    m_cachedVectors.Add(FVectorParameterValue(ParameterInfo, Value));
    m_uniformBufferDirty = true;
}

void FMaterialRenderProxy::SetCachedTexture(const FMaterialParameterInfo& ParameterInfo, FTexture* Value)
{
    // Check if already cached
    for (auto& Param : m_cachedTextures)
    {
        if (Param.ParameterInfo == ParameterInfo)
        {
            Param.ParameterValue = Value;
            return;
        }
    }
    
    // Add new cached value
    m_cachedTextures.Add(FTextureParameterValue(ParameterInfo, Value));
}

void FMaterialRenderProxy::ClearCachedValues()
{
    m_cachedScalars.Empty();
    m_cachedVectors.Empty();
    m_cachedTextures.Empty();
    m_uniformBufferDirty = true;
}

// ============================================================================
// Uniform Buffer
// ============================================================================

TSharedPtr<MonsterRender::RHI::IRHIBuffer> FMaterialRenderProxy::GetUniformBuffer(
    MonsterRender::RHI::IRHIDevice* Device)
{
    if (!m_uniformBuffer || m_uniformBufferDirty)
    {
        if (Device)
        {
            CreateUniformBuffer(Device);
        }
    }
    return m_uniformBuffer;
}

void FMaterialRenderProxy::UpdateUniformBuffer(MonsterRender::RHI::IRHIDevice* Device)
{
    if (!Device || !m_uniformBuffer)
    {
        return;
    }
    
    // Calculate buffer size and fill data
    uint32 BufferSize = CalculateUniformBufferSize();
    if (BufferSize == 0)
    {
        return;
    }
    
    // Map buffer and fill data
    void* MappedData = m_uniformBuffer->map();
    if (MappedData)
    {
        FillUniformBufferData(static_cast<uint8*>(MappedData), BufferSize);
        m_uniformBuffer->unmap();
        m_uniformBufferDirty = false;
    }
}

void FMaterialRenderProxy::InvalidateUniformBuffer()
{
    m_uniformBufferDirty = true;
}

bool FMaterialRenderProxy::CreateUniformBuffer(MonsterRender::RHI::IRHIDevice* Device)
{
    if (!Device)
    {
        return false;
    }
    
    uint32 BufferSize = CalculateUniformBufferSize();
    if (BufferSize == 0)
    {
        // No parameters, no buffer needed
        return true;
    }
    
    // Create buffer description
    MonsterRender::RHI::BufferDesc Desc;
    Desc.size = BufferSize;
    Desc.usage = MonsterRender::RHI::EResourceUsage::UniformBuffer;
    Desc.memoryUsage = MonsterRender::RHI::EMemoryUsage::Dynamic;
    Desc.cpuAccessible = true;
    Desc.debugName = "MaterialUniformBuffer";
    
    // Create buffer
    m_uniformBuffer = Device->createBuffer(Desc);
    if (!m_uniformBuffer)
    {
        MR_LOG_ERROR("FMaterialRenderProxy: Failed to create uniform buffer");
        return false;
    }
    
    // Fill initial data
    UpdateUniformBuffer(Device);
    
    MR_LOG_DEBUG("FMaterialRenderProxy: Created uniform buffer (size: %u)", BufferSize);
    return true;
}

uint32 FMaterialRenderProxy::CalculateUniformBufferSize() const
{
    uint32 Size = 0;
    
    // Scalars: 4 bytes each, aligned to 16 bytes
    Size += static_cast<uint32>(m_cachedScalars.Num()) * sizeof(float);
    Size = (Size + 15) & ~15; // Align to 16 bytes
    
    // Vectors: 16 bytes each (4 floats)
    Size += static_cast<uint32>(m_cachedVectors.Num()) * sizeof(float) * 4;
    
    return Size;
}

void FMaterialRenderProxy::FillUniformBufferData(uint8* Data, uint32 Size) const
{
    uint32 Offset = 0;
    
    // Write scalar parameters
    for (const auto& Param : m_cachedScalars)
    {
        if (Offset + sizeof(float) <= Size)
        {
            *reinterpret_cast<float*>(Data + Offset) = Param.ParameterValue;
            Offset += sizeof(float);
        }
    }
    
    // Align to 16 bytes
    Offset = (Offset + 15) & ~15;
    
    // Write vector parameters
    for (const auto& Param : m_cachedVectors)
    {
        if (Offset + sizeof(float) * 4 <= Size)
        {
            float* VecData = reinterpret_cast<float*>(Data + Offset);
            VecData[0] = Param.ParameterValue.R;
            VecData[1] = Param.ParameterValue.G;
            VecData[2] = Param.ParameterValue.B;
            VecData[3] = Param.ParameterValue.A;
            Offset += sizeof(float) * 4;
        }
    }
}

// ============================================================================
// Texture Bindings
// ============================================================================

void FMaterialRenderProxy::GetTextureBindings(TArray<TPair<int32, FTexture*>>& OutTextures) const
{
    OutTextures.Empty();
    
    int32 Slot = 0;
    for (const auto& Param : m_cachedTextures)
    {
        if (Param.ParameterValue)
        {
            OutTextures.Add(MakePair(Slot, Param.ParameterValue));
        }
        Slot++;
    }
}

FTexture* FMaterialRenderProxy::GetTextureAtSlot(int32 Slot) const
{
    if (Slot >= 0 && Slot < m_cachedTextures.Num())
    {
        return m_cachedTextures[Slot].ParameterValue;
    }
    return nullptr;
}

// ============================================================================
// Pipeline State
// ============================================================================

TSharedPtr<MonsterRender::RHI::IRHIPipelineState> FMaterialRenderProxy::GetPipelineState() const
{
    FMaterial* Material = GetMaterial();
    if (Material)
    {
        return Material->GetOrCreatePipelineState(nullptr);
    }
    return nullptr;
}

// ============================================================================
// Dirty State
// ============================================================================

void FMaterialRenderProxy::MarkDirty()
{
    m_isDirty = true;
    m_uniformBufferDirty = true;
}

// ============================================================================
// Debug
// ============================================================================

String FMaterialRenderProxy::GetDebugName() const
{
    if (m_material)
    {
        return m_material->GetDebugName();
    }
    return "UnknownMaterial";
}

} // namespace MonsterEngine
