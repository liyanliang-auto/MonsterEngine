/**
 * @file MaterialInstanceDynamic.cpp
 * @brief Implementation of FMaterialInstanceDynamic class
 */

#include "Engine/Material/MaterialInstanceDynamic.h"
#include "Engine/Material/Material.h"
#include "Engine/Material/MaterialRenderProxy.h"
#include "Core/Logging/LogMacros.h"

namespace MonsterEngine
{

// Define log category
DEFINE_LOG_CATEGORY_STATIC(LogMaterialInstanceDynamic, Log, All);

// ============================================================================
// FMaterialInstanceDynamic Implementation
// ============================================================================

FMaterialInstanceDynamic::FMaterialInstanceDynamic()
    : FMaterialInstance()
    , m_batchUpdateDepth(0)
{
    MR_LOG_DEBUG("FMaterialInstanceDynamic: Created unnamed dynamic instance");
}

FMaterialInstanceDynamic::FMaterialInstanceDynamic(FMaterialInterface* InParent)
    : FMaterialInstance(InParent)
    , m_batchUpdateDepth(0)
{
    MR_LOG_DEBUG("FMaterialInstanceDynamic: Created dynamic instance with parent");
}

FMaterialInstanceDynamic::~FMaterialInstanceDynamic()
{
    MR_LOG_DEBUG("FMaterialInstanceDynamic: Destroying dynamic instance '%s'", *GetDebugName());
}

// ============================================================================
// Static Creation
// ============================================================================

TSharedPtr<FMaterialInstanceDynamic> FMaterialInstanceDynamic::Create(FMaterialInterface* ParentMaterial)
{
    if (!ParentMaterial)
    {
        MR_LOG_ERROR("FMaterialInstanceDynamic::Create: Parent material is null");
        return nullptr;
    }
    
    auto Instance = MakeShared<FMaterialInstanceDynamic>(ParentMaterial);
    return Instance;
}

TSharedPtr<FMaterialInstanceDynamic> FMaterialInstanceDynamic::Create(FMaterialInterface* ParentMaterial, const FName& Name)
{
    auto Instance = Create(ParentMaterial);
    if (Instance)
    {
        Instance->SetMaterialName(Name);
    }
    return Instance;
}

// ============================================================================
// Scalar Parameters
// ============================================================================

void FMaterialInstanceDynamic::SetScalarParameterValue(const FName& ParameterName, float Value)
{
    FMaterialInstance::SetScalarParameterValue(ParameterName, Value);
    OnParameterChanged();
}

void FMaterialInstanceDynamic::SetScalarParameterValueByInfo(const FMaterialParameterInfo& ParameterInfo, float Value)
{
    FMaterialInstance::SetScalarParameterValue(ParameterInfo, Value);
    OnParameterChanged();
}

float FMaterialInstanceDynamic::GetScalarParameterValue(const FName& ParameterName) const
{
    float Value = 0.0f;
    FMaterialInstance::GetScalarParameterValue(FMaterialParameterInfo(ParameterName), Value);
    return Value;
}

bool FMaterialInstanceDynamic::InitializeScalarParameterAndGetIndex(const FName& ParameterName, float Value, int32& OutParameterIndex)
{
    // Set the value first
    SetScalarParameterValue(ParameterName, Value);
    
    // Find the index
    OutParameterIndex = FindScalarOverrideIndex(FMaterialParameterInfo(ParameterName));
    return OutParameterIndex != INDEX_NONE;
}

bool FMaterialInstanceDynamic::SetScalarParameterByIndex(int32 ParameterIndex, float Value)
{
    if (ParameterIndex < 0 || ParameterIndex >= m_scalarOverrides.Num())
    {
        return false;
    }
    
    m_scalarOverrides[ParameterIndex].ParameterValue = Value;
    OnParameterChanged();
    return true;
}

// ============================================================================
// Vector Parameters
// ============================================================================

void FMaterialInstanceDynamic::SetVectorParameterValue(const FName& ParameterName, const FLinearColor& Value)
{
    FMaterialInstance::SetVectorParameterValue(ParameterName, Value);
    OnParameterChanged();
}

void FMaterialInstanceDynamic::SetVectorParameterValueByInfo(const FMaterialParameterInfo& ParameterInfo, const FLinearColor& Value)
{
    FMaterialInstance::SetVectorParameterValue(ParameterInfo, Value);
    OnParameterChanged();
}

FLinearColor FMaterialInstanceDynamic::GetVectorParameterValue(const FName& ParameterName) const
{
    FLinearColor Value(0.0f, 0.0f, 0.0f, 1.0f);
    FMaterialInstance::GetVectorParameterValue(FMaterialParameterInfo(ParameterName), Value);
    return Value;
}

bool FMaterialInstanceDynamic::InitializeVectorParameterAndGetIndex(const FName& ParameterName, const FLinearColor& Value, int32& OutParameterIndex)
{
    // Set the value first
    SetVectorParameterValue(ParameterName, Value);
    
    // Find the index
    OutParameterIndex = FindVectorOverrideIndex(FMaterialParameterInfo(ParameterName));
    return OutParameterIndex != INDEX_NONE;
}

bool FMaterialInstanceDynamic::SetVectorParameterByIndex(int32 ParameterIndex, const FLinearColor& Value)
{
    if (ParameterIndex < 0 || ParameterIndex >= m_vectorOverrides.Num())
    {
        return false;
    }
    
    m_vectorOverrides[ParameterIndex].ParameterValue = Value;
    OnParameterChanged();
    return true;
}

// ============================================================================
// Texture Parameters
// ============================================================================

void FMaterialInstanceDynamic::SetTextureParameterValue(const FName& ParameterName, FTexture* Value)
{
    FMaterialInstance::SetTextureParameterValue(ParameterName, Value);
    OnParameterChanged();
}

void FMaterialInstanceDynamic::SetTextureParameterValueByInfo(const FMaterialParameterInfo& ParameterInfo, FTexture* Value)
{
    FMaterialInstance::SetTextureParameterValue(ParameterInfo, Value);
    OnParameterChanged();
}

FTexture* FMaterialInstanceDynamic::GetTextureParameterValue(const FName& ParameterName) const
{
    FTexture* Value = nullptr;
    FMaterialInstance::GetTextureParameterValue(FMaterialParameterInfo(ParameterName), Value);
    return Value;
}

// ============================================================================
// Parameter Interpolation
// ============================================================================

void FMaterialInstanceDynamic::InterpolateParameters(FMaterialInstance* SourceA, FMaterialInstance* SourceB, float Alpha)
{
    if (!SourceA && !SourceB)
    {
        return;
    }
    
    // Clamp alpha
    Alpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
    
    BeginBatchUpdate();
    
    // Interpolate scalar parameters
    if (SourceA)
    {
        for (const auto& ParamA : SourceA->GetScalarParameterOverrides())
        {
            float ValueA = ParamA.ParameterValue;
            float ValueB = ValueA;
            
            // Try to get corresponding value from SourceB
            if (SourceB)
            {
                float TempValue;
                if (SourceB->GetScalarParameterValue(ParamA.ParameterInfo, TempValue))
                {
                    ValueB = TempValue;
                }
            }
            
            // Interpolate and set
            float InterpolatedValue = FMath::Lerp(ValueA, ValueB, Alpha);
            SetScalarParameterValue(ParamA.ParameterInfo, InterpolatedValue);
        }
    }
    
    // Interpolate vector parameters
    if (SourceA)
    {
        for (const auto& ParamA : SourceA->GetVectorParameterOverrides())
        {
            FLinearColor ValueA = ParamA.ParameterValue;
            FLinearColor ValueB = ValueA;
            
            // Try to get corresponding value from SourceB
            if (SourceB)
            {
                FLinearColor TempValue;
                if (SourceB->GetVectorParameterValue(ParamA.ParameterInfo, TempValue))
                {
                    ValueB = TempValue;
                }
            }
            
            // Interpolate and set
            FLinearColor InterpolatedValue = FLinearColor::LerpUsingHSV(ValueA, ValueB, Alpha);
            SetVectorParameterValue(ParamA.ParameterInfo, InterpolatedValue);
        }
    }
    
    // Texture parameters are not interpolated - use SourceA if Alpha < 0.5, else SourceB
    FMaterialInstance* TextureSource = (Alpha < 0.5f) ? SourceA : SourceB;
    if (TextureSource)
    {
        for (const auto& Param : TextureSource->GetTextureParameterOverrides())
        {
            SetTextureParameterValue(Param.ParameterInfo, Param.ParameterValue);
        }
    }
    
    EndBatchUpdate();
}

// ============================================================================
// Copy Operations
// ============================================================================

void FMaterialInstanceDynamic::CopyParameterOverrides(FMaterialInstance* Source)
{
    if (!Source)
    {
        return;
    }
    
    BeginBatchUpdate();
    
    // Copy scalar overrides
    for (const auto& Param : Source->GetScalarParameterOverrides())
    {
        SetScalarParameterValue(Param.ParameterInfo, Param.ParameterValue);
    }
    
    // Copy vector overrides
    for (const auto& Param : Source->GetVectorParameterOverrides())
    {
        SetVectorParameterValue(Param.ParameterInfo, Param.ParameterValue);
    }
    
    // Copy texture overrides
    for (const auto& Param : Source->GetTextureParameterOverrides())
    {
        SetTextureParameterValue(Param.ParameterInfo, Param.ParameterValue);
    }
    
    EndBatchUpdate();
}

void FMaterialInstanceDynamic::CopyScalarAndVectorParameters(FMaterialInterface* Source)
{
    if (!Source)
    {
        return;
    }
    
    BeginBatchUpdate();
    
    // Get base material to find all parameters
    const FMaterial* BaseMaterial = Source->GetMaterial();
    if (BaseMaterial)
    {
        // Copy scalar parameters
        for (const auto& Param : BaseMaterial->GetScalarParameterDefaults())
        {
            float Value;
            if (Source->GetScalarParameterValue(Param.ParameterInfo, Value))
            {
                SetScalarParameterValue(Param.ParameterInfo, Value);
            }
        }
        
        // Copy vector parameters
        for (const auto& Param : BaseMaterial->GetVectorParameterDefaults())
        {
            FLinearColor Value;
            if (Source->GetVectorParameterValue(Param.ParameterInfo, Value))
            {
                SetVectorParameterValue(Param.ParameterInfo, Value);
            }
        }
    }
    
    EndBatchUpdate();
}

void FMaterialInstanceDynamic::ClearParameterValues()
{
    FMaterialInstance::ClearAllParameterValues();
    OnParameterChanged();
}

// ============================================================================
// Update Management
// ============================================================================

void FMaterialInstanceDynamic::UpdateRenderProxy()
{
    FMaterialInstance::UpdateRenderProxy();
}

void FMaterialInstanceDynamic::BeginBatchUpdate()
{
    m_batchUpdateDepth++;
}

void FMaterialInstanceDynamic::EndBatchUpdate()
{
    if (m_batchUpdateDepth > 0)
    {
        m_batchUpdateDepth--;
        
        if (m_batchUpdateDepth == 0 && m_isDirty)
        {
            UpdateRenderProxy();
        }
    }
}

void FMaterialInstanceDynamic::OnParameterChanged()
{
    if (!IsInBatchUpdate())
    {
        UpdateRenderProxy();
    }
}

} // namespace MonsterEngine
