// Copyright Monster Engine. All Rights Reserved.

#include "Core/Color.h"
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <sstream>
#include <iomanip>
#include <random>

namespace MonsterEngine
{

// ============================================================================
// FLinearColor Static Constants
// ============================================================================

const FLinearColor FLinearColor::White(1.0f, 1.0f, 1.0f, 1.0f);
const FLinearColor FLinearColor::Gray(0.5f, 0.5f, 0.5f, 1.0f);
const FLinearColor FLinearColor::Black(0.0f, 0.0f, 0.0f, 1.0f);
const FLinearColor FLinearColor::Transparent(0.0f, 0.0f, 0.0f, 0.0f);
const FLinearColor FLinearColor::Red(1.0f, 0.0f, 0.0f, 1.0f);
const FLinearColor FLinearColor::Green(0.0f, 1.0f, 0.0f, 1.0f);
const FLinearColor FLinearColor::Blue(0.0f, 0.0f, 1.0f, 1.0f);
const FLinearColor FLinearColor::Yellow(1.0f, 1.0f, 0.0f, 1.0f);
const FLinearColor FLinearColor::Cyan(0.0f, 1.0f, 1.0f, 1.0f);
const FLinearColor FLinearColor::Magenta(1.0f, 0.0f, 1.0f, 1.0f);
const FLinearColor FLinearColor::Orange(1.0f, 0.5f, 0.0f, 1.0f);

// ============================================================================
// FColor Static Constants
// ============================================================================

const FColor FColor::White(255, 255, 255, 255);
const FColor FColor::Black(0, 0, 0, 255);
const FColor FColor::Transparent(0, 0, 0, 0);
const FColor FColor::Red(255, 0, 0, 255);
const FColor FColor::Green(0, 255, 0, 255);
const FColor FColor::Blue(0, 0, 255, 255);
const FColor FColor::Yellow(255, 255, 0, 255);
const FColor FColor::Cyan(0, 255, 255, 255);
const FColor FColor::Magenta(255, 0, 255, 255);
const FColor FColor::Orange(255, 165, 0, 255);
const FColor FColor::Purple(128, 0, 128, 255);
const FColor FColor::Turquoise(64, 224, 208, 255);
const FColor FColor::Silver(192, 192, 192, 255);
const FColor FColor::Emerald(80, 200, 120, 255);

// ============================================================================
// sRGB Conversion Tables
// ============================================================================

// sRGB to linear conversion table
float FLinearColor::sRGBToLinearTable[256] = { 0 };

// Pow(2.2) conversion table
float FLinearColor::Pow22OneOver255Table[256] = { 0 };

namespace
{
    // Initialize conversion tables at startup
    struct FColorTableInitializer
    {
        FColorTableInitializer()
        {
            for (int32_t i = 0; i < 256; ++i)
            {
                float Value = static_cast<float>(i) / 255.0f;
                
                // sRGB to linear conversion
                if (Value <= 0.04045f)
                {
                    FLinearColor::sRGBToLinearTable[i] = Value / 12.92f;
                }
                else
                {
                    FLinearColor::sRGBToLinearTable[i] = std::pow((Value + 0.055f) / 1.055f, 2.4f);
                }
                
                // Pow(2.2) conversion
                FLinearColor::Pow22OneOver255Table[i] = std::pow(Value, 2.2f);
            }
        }
    };
    
    static FColorTableInitializer GColorTableInitializer;
}

// ============================================================================
// FLinearColor Implementation
// ============================================================================

FLinearColor::FLinearColor(const FColor& Color)
    : R(sRGBToLinearTable[Color.R])
    , G(sRGBToLinearTable[Color.G])
    , B(sRGBToLinearTable[Color.B])
    , A(Color.A / 255.0f)
{
}

FColor FLinearColor::ToFColor(bool bSRGB) const
{
    // Clamp to [0,1] range
    float ClampedR = std::clamp(R, 0.0f, 1.0f);
    float ClampedG = std::clamp(G, 0.0f, 1.0f);
    float ClampedB = std::clamp(B, 0.0f, 1.0f);
    float ClampedA = std::clamp(A, 0.0f, 1.0f);
    
    if (bSRGB)
    {
        // Linear to sRGB conversion
        auto LinearToSRGB = [](float Linear) -> float
        {
            if (Linear <= 0.0031308f)
            {
                return Linear * 12.92f;
            }
            else
            {
                return 1.055f * std::pow(Linear, 1.0f / 2.4f) - 0.055f;
            }
        };
        
        ClampedR = LinearToSRGB(ClampedR);
        ClampedG = LinearToSRGB(ClampedG);
        ClampedB = LinearToSRGB(ClampedB);
    }
    
    return FColor(
        static_cast<uint8_t>(ClampedR * 255.0f + 0.5f),
        static_cast<uint8_t>(ClampedG * 255.0f + 0.5f),
        static_cast<uint8_t>(ClampedB * 255.0f + 0.5f),
        static_cast<uint8_t>(ClampedA * 255.0f + 0.5f)
    );
}

FColor FLinearColor::QuantizeRound() const
{
    // Clamp and quantize with rounding
    auto Quantize = [](float Value) -> uint8_t
    {
        // Handle NaN by mapping to 0
        float Clamped = (Value > 0.0f) ? Value : 0.0f;
        Clamped = (Clamped < 1.0f) ? Clamped : 1.0f;
        return static_cast<uint8_t>(Clamped * 255.0f + 0.5f);
    };
    
    return FColor(Quantize(R), Quantize(G), Quantize(B), Quantize(A));
}

FColor FLinearColor::QuantizeFloor() const
{
    auto Quantize = [](float Value) -> uint8_t
    {
        float Clamped = std::clamp(Value, 0.0f, 1.0f);
        return static_cast<uint8_t>(Clamped * 255.0f);
    };
    
    return FColor(Quantize(R), Quantize(G), Quantize(B), Quantize(A));
}

FLinearColor FLinearColor::FromSRGBColor(const FColor& Color)
{
    return FLinearColor(Color);
}

FLinearColor FLinearColor::FromPow22Color(const FColor& Color)
{
    return FLinearColor(
        Pow22OneOver255Table[Color.R],
        Pow22OneOver255Table[Color.G],
        Pow22OneOver255Table[Color.B],
        Color.A / 255.0f
    );
}

FLinearColor FLinearColor::LinearRGBToHSV() const
{
    float RGBMin = std::min({R, G, B});
    float RGBMax = std::max({R, G, B});
    float Delta = RGBMax - RGBMin;
    
    float H = 0.0f;
    float S = 0.0f;
    float V = RGBMax;
    
    if (Delta > 0.0f)
    {
        S = Delta / RGBMax;
        
        if (R == RGBMax)
        {
            H = (G - B) / Delta;
        }
        else if (G == RGBMax)
        {
            H = 2.0f + (B - R) / Delta;
        }
        else
        {
            H = 4.0f + (R - G) / Delta;
        }
        
        H *= 60.0f;
        if (H < 0.0f)
        {
            H += 360.0f;
        }
    }
    
    return FLinearColor(H, S, V, A);
}

FLinearColor FLinearColor::HSVToLinearRGB() const
{
    // R = H, G = S, B = V
    float H = R;
    float S = G;
    float V = B;
    
    if (S <= 0.0f)
    {
        return FLinearColor(V, V, V, A);
    }
    
    H = std::fmod(H, 360.0f);
    if (H < 0.0f) H += 360.0f;
    H /= 60.0f;
    
    int32_t I = static_cast<int32_t>(H);
    float F = H - static_cast<float>(I);
    float P = V * (1.0f - S);
    float Q = V * (1.0f - S * F);
    float T = V * (1.0f - S * (1.0f - F));
    
    float OutR, OutG, OutB;
    
    switch (I)
    {
        case 0:  OutR = V; OutG = T; OutB = P; break;
        case 1:  OutR = Q; OutG = V; OutB = P; break;
        case 2:  OutR = P; OutG = V; OutB = T; break;
        case 3:  OutR = P; OutG = Q; OutB = V; break;
        case 4:  OutR = T; OutG = P; OutB = V; break;
        default: OutR = V; OutG = P; OutB = Q; break;
    }
    
    return FLinearColor(OutR, OutG, OutB, A);
}

FLinearColor FLinearColor::LerpUsingHSV(const FLinearColor& From, const FLinearColor& To, float Progress)
{
    FLinearColor FromHSV = From.LinearRGBToHSV();
    FLinearColor ToHSV = To.LinearRGBToHSV();
    
    // Find shortest path for hue
    float DeltaH = ToHSV.R - FromHSV.R;
    if (DeltaH > 180.0f)
    {
        DeltaH -= 360.0f;
    }
    else if (DeltaH < -180.0f)
    {
        DeltaH += 360.0f;
    }
    
    float H = FromHSV.R + Progress * DeltaH;
    if (H < 0.0f) H += 360.0f;
    if (H >= 360.0f) H -= 360.0f;
    
    float S = FromHSV.G + Progress * (ToHSV.G - FromHSV.G);
    float V = FromHSV.B + Progress * (ToHSV.B - FromHSV.B);
    float A = From.A + Progress * (To.A - From.A);
    
    return FLinearColor(H, S, V, A).HSVToLinearRGB();
}

FLinearColor FLinearColor::MakeFromHSV(float H, float S, float V)
{
    return FLinearColor(H, S, V, 1.0f).HSVToLinearRGB();
}

FLinearColor FLinearColor::MakeFromHSV8(uint8_t H, uint8_t S, uint8_t V)
{
    return MakeFromHSV(
        static_cast<float>(H) * (360.0f / 255.0f),
        static_cast<float>(S) / 255.0f,
        static_cast<float>(V) / 255.0f
    );
}

FLinearColor FLinearColor::MakeRandomColor()
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_real_distribution<float> dis(0.0f, 1.0f);
    
    // Generate random hue with full saturation and value
    float H = dis(gen) * 360.0f;
    return MakeFromHSV(H, 1.0f, 1.0f);
}

FLinearColor FLinearColor::MakeRandomSeededColor(int32_t Seed)
{
    std::mt19937 gen(static_cast<uint32_t>(Seed));
    std::uniform_real_distribution<float> dis(0.0f, 1.0f);
    
    float H = dis(gen) * 360.0f;
    return MakeFromHSV(H, 1.0f, 1.0f);
}

FLinearColor FLinearColor::MakeFromColorTemperature(float Temp)
{
    // Attempt to approximate black body radiation
    // Based on approximation from http://www.tannerhelland.com/4435/convert-temperature-rgb-algorithm-code/
    
    Temp = std::clamp(Temp, 1000.0f, 15000.0f);
    Temp /= 100.0f;
    
    float R, G, B;
    
    // Red
    if (Temp <= 66.0f)
    {
        R = 1.0f;
    }
    else
    {
        R = 329.698727446f * std::pow(Temp - 60.0f, -0.1332047592f);
        R = std::clamp(R / 255.0f, 0.0f, 1.0f);
    }
    
    // Green
    if (Temp <= 66.0f)
    {
        G = 99.4708025861f * std::log(Temp) - 161.1195681661f;
        G = std::clamp(G / 255.0f, 0.0f, 1.0f);
    }
    else
    {
        G = 288.1221695283f * std::pow(Temp - 60.0f, -0.0755148492f);
        G = std::clamp(G / 255.0f, 0.0f, 1.0f);
    }
    
    // Blue
    if (Temp >= 66.0f)
    {
        B = 1.0f;
    }
    else if (Temp <= 19.0f)
    {
        B = 0.0f;
    }
    else
    {
        B = 138.5177312231f * std::log(Temp - 10.0f) - 305.0447927307f;
        B = std::clamp(B / 255.0f, 0.0f, 1.0f);
    }
    
    return FLinearColor(R, G, B, 1.0f);
}

std::string FLinearColor::ToString() const
{
    char Buffer[128];
    snprintf(Buffer, sizeof(Buffer), "(R=%f,G=%f,B=%f,A=%f)", R, G, B, A);
    return std::string(Buffer);
}

bool FLinearColor::InitFromString(const std::string& InSourceString)
{
    R = G = B = 0.0f;
    A = 1.0f;
    
    // Simple parsing for R=, G=, B=, A= format
    auto ParseValue = [&InSourceString](const char* Key, float& OutValue) -> bool
    {
        size_t Pos = InSourceString.find(Key);
        if (Pos != std::string::npos)
        {
            OutValue = std::stof(InSourceString.substr(Pos + strlen(Key)));
            return true;
        }
        return false;
    };
    
    bool bSuccess = ParseValue("R=", R) && ParseValue("G=", G) && ParseValue("B=", B);
    ParseValue("A=", A);  // Alpha is optional
    
    return bSuccess;
}

// ============================================================================
// FColor Implementation
// ============================================================================

FLinearColor FColor::ToLinearColor() const
{
    return FLinearColor(*this);
}

FColor FColor::FromHex(const std::string& HexString)
{
    std::string Hex = HexString;
    
    // Remove # prefix if present
    if (!Hex.empty() && Hex[0] == '#')
    {
        Hex = Hex.substr(1);
    }
    
    uint8_t R = 0, G = 0, B = 0, A = 255;
    
    if (Hex.length() == 3)
    {
        // RGB format (expand to RRGGBB)
        R = static_cast<uint8_t>(std::stoul(Hex.substr(0, 1), nullptr, 16) * 17);
        G = static_cast<uint8_t>(std::stoul(Hex.substr(1, 1), nullptr, 16) * 17);
        B = static_cast<uint8_t>(std::stoul(Hex.substr(2, 1), nullptr, 16) * 17);
    }
    else if (Hex.length() == 6)
    {
        // RRGGBB format
        R = static_cast<uint8_t>(std::stoul(Hex.substr(0, 2), nullptr, 16));
        G = static_cast<uint8_t>(std::stoul(Hex.substr(2, 2), nullptr, 16));
        B = static_cast<uint8_t>(std::stoul(Hex.substr(4, 2), nullptr, 16));
    }
    else if (Hex.length() == 8)
    {
        // RRGGBBAA format
        R = static_cast<uint8_t>(std::stoul(Hex.substr(0, 2), nullptr, 16));
        G = static_cast<uint8_t>(std::stoul(Hex.substr(2, 2), nullptr, 16));
        B = static_cast<uint8_t>(std::stoul(Hex.substr(4, 2), nullptr, 16));
        A = static_cast<uint8_t>(std::stoul(Hex.substr(6, 2), nullptr, 16));
    }
    
    return FColor(R, G, B, A);
}

FColor FColor::MakeRandomColor()
{
    return FLinearColor::MakeRandomColor().ToFColor(true);
}

FColor FColor::MakeRandomSeededColor(int32_t Seed)
{
    return FLinearColor::MakeRandomSeededColor(Seed).ToFColor(true);
}

FColor FColor::MakeRedToGreenColorFromScalar(float Scalar)
{
    Scalar = std::clamp(Scalar, 0.0f, 1.0f);
    
    // Red at 0, Yellow at 0.5, Green at 1
    float R = (Scalar < 0.5f) ? 1.0f : 2.0f * (1.0f - Scalar);
    float G = (Scalar > 0.5f) ? 1.0f : 2.0f * Scalar;
    
    return FLinearColor(R, G, 0.0f, 1.0f).ToFColor(true);
}

FColor FColor::MakeFromColorTemperature(float Temp)
{
    return FLinearColor::MakeFromColorTemperature(Temp).ToFColor(true);
}

std::string FColor::ToHex() const
{
    char Buffer[16];
    snprintf(Buffer, sizeof(Buffer), "%02X%02X%02X%02X", R, G, B, A);
    return std::string(Buffer);
}

std::string FColor::ToString() const
{
    char Buffer[64];
    snprintf(Buffer, sizeof(Buffer), "(R=%d,G=%d,B=%d,A=%d)", R, G, B, A);
    return std::string(Buffer);
}

bool FColor::InitFromString(const std::string& InSourceString)
{
    R = G = B = 0;
    A = 255;
    
    auto ParseValue = [&InSourceString](const char* Key, uint8_t& OutValue) -> bool
    {
        size_t Pos = InSourceString.find(Key);
        if (Pos != std::string::npos)
        {
            OutValue = static_cast<uint8_t>(std::stoi(InSourceString.substr(Pos + strlen(Key))));
            return true;
        }
        return false;
    };
    
    bool bSuccess = ParseValue("R=", R) && ParseValue("G=", G) && ParseValue("B=", B);
    ParseValue("A=", A);  // Alpha is optional
    
    return bSuccess;
}

} // namespace MonsterEngine
