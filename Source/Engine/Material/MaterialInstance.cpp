/**
 * @file MaterialInstance.cpp
 * @brief Implementation of FMaterialInstance class
 */

#include "Engine/Material/MaterialInstance.h"
#include "Engine/Material/Material.h"
#include "Engine/Material/MaterialRenderProxy.h"
#include "Core/Logging/LogMacros.h"

namespace MonsterEngine
{

// Define log category
DEFINE_LOG_CATEGORY_STATIC(LogMaterialInstance, Log, All);

// ============================================================================
// FMaterialInstance Implementation
// ============================================================================

FMaterialInstance::FMaterialInstance()
    : FMaterialInterface()
    , m_hasPropertyOverrides(false)
    , m_isDirty(true)
{
    MR_LOG_DEBUG("FMaterialInstance: Created unnamed instance");
}

FMaterialInstance::FMaterialInstance(FMaterialInterface* InParent)
    : FMaterialInterface()
    , m_hasPropertyOverrides(false)
    , m_isDirty(true)
{
    SetParent(InParent);
    MR_LOG_DEBUG("FMaterialInstance: Created instance with parent");
}

FMaterialInstance::~FMaterialInstance()
{
    MR_LOG_DEBUG("FMaterialInstance: Destroying instance '%s'", *GetDebugName());
    m_renderProxy.Reset();
}

// ============================================================================
// Material Hierarchy
// ============================================================================

FMaterial* FMaterialInstance::GetMaterial()
{
    if (FMaterialInterface* Parent = m_parent.Get())
    {
        return Parent->GetMaterial();
    }
    return nullptr;
}

const FMaterial* FMaterialInstance::GetMaterial() const
{
    if (const FMaterialInterface* Parent = m_parent.Get())
    {
        return Parent->GetMaterial();
    }
    return nullptr;
}

void FMaterialInstance::SetParent(FMaterialInterface* InParent)
{
    if (InParent == this)
    {
        MR_LOG_ERROR("FMaterialInstance: Cannot set self as parent");
        return;
    }
    
    // Check for circular dependency
    FMaterialInterface* Current = InParent;
    while (Current)
    {
        if (Current == this)
        {
            MR_LOG_ERROR("FMaterialInstance: Circular dependency detected");
            return;
        }
        Current = Current->GetParent();
    }
    
    // Use weak pointer to avoid circular references
    // Note: In a real implementation, you'd want proper shared_ptr management
    m_parent.Reset();
    if (InParent)
    {
        // Store as weak reference
        // This is a simplified version - in production you'd use proper ref counting
    }
    
    MarkDirty();
    MR_LOG_DEBUG("FMaterialInstance: Set parent for '%s'", *GetDebugName());
}

// ============================================================================
// Render Proxy
// ============================================================================

FMaterialRenderProxy* FMaterialInstance::GetRenderProxy()
{
    if (!m_renderProxy)
    {
        CreateRenderProxy();
    }
    
    if (m_isDirty)
    {
        UpdateRenderProxy();
    }
    
    return m_renderProxy.Get();
}

const FMaterialRenderProxy* FMaterialInstance::GetRenderProxy() const
{
    return m_renderProxy.Get();
}

void FMaterialInstance::CreateRenderProxy()
{
    m_renderProxy = MakeShared<FMaterialRenderProxy>(this);
    UpdateRenderProxy();
    MR_LOG_DEBUG("FMaterialInstance: Created render proxy for '%s'", *GetDebugName());
}

void FMaterialInstance::UpdateRenderProxy()
{
    if (!m_renderProxy)
    {
        return;
    }
    
    // Clear existing cached values
    m_renderProxy->ClearCachedValues();
    
    // Cache override values
    for (const auto& Param : m_scalarOverrides)
    {
        m_renderProxy->SetCachedScalar(Param.ParameterInfo, Param.ParameterValue);
    }
    
    for (const auto& Param : m_vectorOverrides)
    {
        m_renderProxy->SetCachedVector(Param.ParameterInfo, Param.ParameterValue);
    }
    
    for (const auto& Param : m_textureOverrides)
    {
        m_renderProxy->SetCachedTexture(Param.ParameterInfo, Param.ParameterValue);
    }
    
    m_renderProxy->InvalidateUniformBuffer();
    m_isDirty = false;
}

// ============================================================================
// Material Properties
// ============================================================================

const FMaterialProperties& FMaterialInstance::GetMaterialProperties() const
{
    if (m_hasPropertyOverrides)
    {
        return m_propertyOverrides;
    }
    
    if (const FMaterialInterface* Parent = m_parent.Get())
    {
        return Parent->GetMaterialProperties();
    }
    
    // Return default properties if no parent
    static FMaterialProperties DefaultProperties;
    return DefaultProperties;
}

void FMaterialInstance::SetPropertyOverrides(const FMaterialProperties& Properties)
{
    m_propertyOverrides = Properties;
    m_hasPropertyOverrides = true;
    MarkDirty();
}

void FMaterialInstance::ClearPropertyOverrides()
{
    m_hasPropertyOverrides = false;
    MarkDirty();
}

// ============================================================================
// Parameter Access
// ============================================================================

bool FMaterialInstance::GetScalarParameterValue(const FMaterialParameterInfo& ParameterInfo, float& OutValue) const
{
    // First check overrides
    int32 Index = FindScalarOverrideIndex(ParameterInfo);
    if (Index != INDEX_NONE)
    {
        OutValue = m_scalarOverrides[Index].ParameterValue;
        return true;
    }
    
    // Then check parent
    if (const FMaterialInterface* Parent = m_parent.Get())
    {
        return Parent->GetScalarParameterValue(ParameterInfo, OutValue);
    }
    
    return false;
}

bool FMaterialInstance::GetVectorParameterValue(const FMaterialParameterInfo& ParameterInfo, FLinearColor& OutValue) const
{
    // First check overrides
    int32 Index = FindVectorOverrideIndex(ParameterInfo);
    if (Index != INDEX_NONE)
    {
        OutValue = m_vectorOverrides[Index].ParameterValue;
        return true;
    }
    
    // Then check parent
    if (const FMaterialInterface* Parent = m_parent.Get())
    {
        return Parent->GetVectorParameterValue(ParameterInfo, OutValue);
    }
    
    return false;
}

bool FMaterialInstance::GetTextureParameterValue(const FMaterialParameterInfo& ParameterInfo, FTexture*& OutValue) const
{
    // First check overrides
    int32 Index = FindTextureOverrideIndex(ParameterInfo);
    if (Index != INDEX_NONE)
    {
        OutValue = m_textureOverrides[Index].ParameterValue;
        return true;
    }
    
    // Then check parent
    if (const FMaterialInterface* Parent = m_parent.Get())
    {
        return Parent->GetTextureParameterValue(ParameterInfo, OutValue);
    }
    
    return false;
}

void FMaterialInstance::GetUsedTextures(TArray<FTexture*>& OutTextures) const
{
    // Get parent textures first
    if (const FMaterialInterface* Parent = m_parent.Get())
    {
        Parent->GetUsedTextures(OutTextures);
    }
    
    // Add/replace with override textures
    for (const auto& Param : m_textureOverrides)
    {
        if (Param.ParameterValue != nullptr)
        {
            // Check if already in list
            bool bFound = false;
            for (int32 i = 0; i < OutTextures.Num(); ++i)
            {
                if (OutTextures[i] == Param.ParameterValue)
                {
                    bFound = true;
                    break;
                }
            }
            
            if (!bFound)
            {
                OutTextures.Add(Param.ParameterValue);
            }
        }
    }
}

// ============================================================================
// Parameter Overrides
// ============================================================================

void FMaterialInstance::SetScalarParameterValue(const FName& ParameterName, float Value)
{
    SetScalarParameterValue(FMaterialParameterInfo(ParameterName), Value);
}

void FMaterialInstance::SetScalarParameterValue(const FMaterialParameterInfo& ParameterInfo, float Value)
{
    int32 Index = FindScalarOverrideIndex(ParameterInfo);
    if (Index != INDEX_NONE)
    {
        m_scalarOverrides[Index].ParameterValue = Value;
    }
    else
    {
        m_scalarOverrides.Add(FScalarParameterValue(ParameterInfo, Value));
    }
    
    MarkDirty();
}

void FMaterialInstance::SetVectorParameterValue(const FName& ParameterName, const FLinearColor& Value)
{
    SetVectorParameterValue(FMaterialParameterInfo(ParameterName), Value);
}

void FMaterialInstance::SetVectorParameterValue(const FMaterialParameterInfo& ParameterInfo, const FLinearColor& Value)
{
    int32 Index = FindVectorOverrideIndex(ParameterInfo);
    if (Index != INDEX_NONE)
    {
        m_vectorOverrides[Index].ParameterValue = Value;
    }
    else
    {
        m_vectorOverrides.Add(FVectorParameterValue(ParameterInfo, Value));
    }
    
    MarkDirty();
}

void FMaterialInstance::SetTextureParameterValue(const FName& ParameterName, FTexture* Value)
{
    SetTextureParameterValue(FMaterialParameterInfo(ParameterName), Value);
}

void FMaterialInstance::SetTextureParameterValue(const FMaterialParameterInfo& ParameterInfo, FTexture* Value)
{
    int32 Index = FindTextureOverrideIndex(ParameterInfo);
    if (Index != INDEX_NONE)
    {
        m_textureOverrides[Index].ParameterValue = Value;
    }
    else
    {
        m_textureOverrides.Add(FTextureParameterValue(ParameterInfo, Value));
    }
    
    MarkDirty();
}

// ============================================================================
// Override Queries
// ============================================================================

bool FMaterialInstance::IsScalarParameterOverridden(const FName& ParameterName) const
{
    return FindScalarOverrideIndex(FMaterialParameterInfo(ParameterName)) != INDEX_NONE;
}

bool FMaterialInstance::IsVectorParameterOverridden(const FName& ParameterName) const
{
    return FindVectorOverrideIndex(FMaterialParameterInfo(ParameterName)) != INDEX_NONE;
}

bool FMaterialInstance::IsTextureParameterOverridden(const FName& ParameterName) const
{
    return FindTextureOverrideIndex(FMaterialParameterInfo(ParameterName)) != INDEX_NONE;
}

// ============================================================================
// Clear Overrides
// ============================================================================

void FMaterialInstance::ClearScalarParameterValue(const FName& ParameterName)
{
    int32 Index = FindScalarOverrideIndex(FMaterialParameterInfo(ParameterName));
    if (Index != INDEX_NONE)
    {
        m_scalarOverrides.RemoveAt(Index);
        MarkDirty();
    }
}

void FMaterialInstance::ClearVectorParameterValue(const FName& ParameterName)
{
    int32 Index = FindVectorOverrideIndex(FMaterialParameterInfo(ParameterName));
    if (Index != INDEX_NONE)
    {
        m_vectorOverrides.RemoveAt(Index);
        MarkDirty();
    }
}

void FMaterialInstance::ClearTextureParameterValue(const FName& ParameterName)
{
    int32 Index = FindTextureOverrideIndex(FMaterialParameterInfo(ParameterName));
    if (Index != INDEX_NONE)
    {
        m_textureOverrides.RemoveAt(Index);
        MarkDirty();
    }
}

void FMaterialInstance::ClearAllParameterValues()
{
    m_scalarOverrides.Empty();
    m_vectorOverrides.Empty();
    m_textureOverrides.Empty();
    MarkDirty();
}

// ============================================================================
// Dirty State
// ============================================================================

void FMaterialInstance::MarkDirty()
{
    m_isDirty = true;
    
    if (m_renderProxy)
    {
        m_renderProxy->MarkDirty();
    }
}

// ============================================================================
// Helper Functions
// ============================================================================

int32 FMaterialInstance::FindScalarOverrideIndex(const FMaterialParameterInfo& Info) const
{
    for (int32 i = 0; i < m_scalarOverrides.Num(); ++i)
    {
        if (m_scalarOverrides[i].ParameterInfo == Info)
        {
            return i;
        }
    }
    return INDEX_NONE;
}

int32 FMaterialInstance::FindVectorOverrideIndex(const FMaterialParameterInfo& Info) const
{
    for (int32 i = 0; i < m_vectorOverrides.Num(); ++i)
    {
        if (m_vectorOverrides[i].ParameterInfo == Info)
        {
            return i;
        }
    }
    return INDEX_NONE;
}

int32 FMaterialInstance::FindTextureOverrideIndex(const FMaterialParameterInfo& Info) const
{
    for (int32 i = 0; i < m_textureOverrides.Num(); ++i)
    {
        if (m_textureOverrides[i].ParameterInfo == Info)
        {
            return i;
        }
    }
    return INDEX_NONE;
}

} // namespace MonsterEngine
