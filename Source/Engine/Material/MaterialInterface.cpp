/**
 * @file MaterialInterface.cpp
 * @brief Implementation of FMaterialInterface base class
 */

#include "Engine/Material/MaterialInterface.h"
#include "Engine/Material/Material.h"
#include "Core/Logging/LogMacros.h"

namespace MonsterEngine
{

// Define log category
DEFINE_LOG_CATEGORY_STATIC(LogMaterialInterface, Log, All);

// ============================================================================
// FMaterialInterface Implementation
// ============================================================================

FMaterialInterface::FMaterialInterface()
    : m_propertiesCached(false)
{
}

FMaterialInterface::~FMaterialInterface()
{
}

EMaterialDomain FMaterialInterface::GetMaterialDomain() const
{
    return GetMaterialProperties().Domain;
}

EMaterialBlendMode FMaterialInterface::GetBlendMode() const
{
    return GetMaterialProperties().BlendMode;
}

EMaterialShadingModel FMaterialInterface::GetShadingModel() const
{
    return GetMaterialProperties().ShadingModel;
}

bool FMaterialInterface::IsTwoSided() const
{
    return GetMaterialProperties().bTwoSided;
}

bool FMaterialInterface::IsMasked() const
{
    return GetBlendMode() == EMaterialBlendMode::Masked;
}

bool FMaterialInterface::IsTranslucent() const
{
    EMaterialBlendMode Mode = GetBlendMode();
    return Mode == EMaterialBlendMode::Translucent ||
           Mode == EMaterialBlendMode::Additive ||
           Mode == EMaterialBlendMode::Modulate ||
           Mode == EMaterialBlendMode::AlphaComposite;
}

float FMaterialInterface::GetOpacityMaskClipValue() const
{
    return GetMaterialProperties().OpacityMaskClipValue;
}

bool FMaterialInterface::GetParameterValue(EMaterialParameterType Type,
                                           const FMaterialParameterInfo& ParameterInfo,
                                           FMaterialParameterMetadata& OutMetadata) const
{
    switch (Type)
    {
        case EMaterialParameterType::Scalar:
        {
            float Value;
            if (GetScalarParameterValue(ParameterInfo, Value))
            {
                OutMetadata.SetScalar(Value);
                return true;
            }
            break;
        }
        
        case EMaterialParameterType::Vector:
        {
            FLinearColor Value;
            if (GetVectorParameterValue(ParameterInfo, Value))
            {
                OutMetadata.SetVector(Value);
                return true;
            }
            break;
        }
        
        case EMaterialParameterType::Texture:
        {
            FTexture* Value = nullptr;
            if (GetTextureParameterValue(ParameterInfo, Value))
            {
                OutMetadata.SetTexture(Value);
                return true;
            }
            break;
        }
        
        default:
            break;
    }
    
    return false;
}

String FMaterialInterface::GetDebugName() const
{
    if (m_materialName.IsValid())
    {
        return m_materialName.ToString();
    }
    return TEXT("UnnamedMaterial");
}

} // namespace MonsterEngine
