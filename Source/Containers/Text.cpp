// Copyright Monster Engine. All Rights Reserved.

#include "Containers/Text.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <cctype>
#include <cwctype>
#include <cmath>

namespace MonsterEngine
{

// ============================================================================
// Static Members
// ============================================================================

const std::wstring FText::EmptyWString;
const std::string FText::EmptyString;
FText FText::EmptyText;

// ============================================================================
// FNumberFormattingOptions Implementation
// ============================================================================

const FNumberFormattingOptions& FNumberFormattingOptions::DefaultWithGrouping()
{
    static FNumberFormattingOptions Options;
    Options.UseGrouping = true;
    return Options;
}

const FNumberFormattingOptions& FNumberFormattingOptions::DefaultNoGrouping()
{
    static FNumberFormattingOptions Options;
    Options.UseGrouping = false;
    return Options;
}

// ============================================================================
// Internal Text Data Classes
// ============================================================================

namespace
{
    /** Simple text data - just stores a display string */
    class FSimpleTextData : public ITextData
    {
    public:
        FSimpleTextData(const std::wstring& InString, bool bInCultureInvariant)
            : DisplayString(InString)
            , bCultureInvariant(bInCultureInvariant)
        {
        }

        FSimpleTextData(std::wstring&& InString, bool bInCultureInvariant)
            : DisplayString(std::move(InString))
            , bCultureInvariant(bInCultureInvariant)
        {
        }

        const std::wstring& GetDisplayString() const override { return DisplayString; }
        const std::wstring& GetSourceString() const override { return DisplayString; }
        const std::string& GetNamespace() const override { return EmptyNamespace; }
        const std::string& GetKey() const override { return EmptyKey; }
        bool IsCultureInvariant() const override { return bCultureInvariant; }
        bool IsFromStringTable() const override { return false; }

    private:
        std::wstring DisplayString;
        bool bCultureInvariant;
        static const std::string EmptyNamespace;
        static const std::string EmptyKey;
    };

    const std::string FSimpleTextData::EmptyNamespace;
    const std::string FSimpleTextData::EmptyKey;

    /** Localized text data - stores namespace, key, and looks up translation */
    class FLocalizedTextData : public ITextData
    {
    public:
        FLocalizedTextData(
            const std::string& InNamespace,
            const std::string& InKey,
            const std::wstring& InDefaultString
        )
            : Namespace(InNamespace)
            , Key(InKey)
            , DefaultString(InDefaultString)
        {
            UpdateDisplayString();
        }

        const std::wstring& GetDisplayString() const override
        {
            return DisplayString;
        }

        const std::wstring& GetSourceString() const override
        {
            return DefaultString;
        }

        const std::string& GetNamespace() const override { return Namespace; }
        const std::string& GetKey() const override { return Key; }
        bool IsCultureInvariant() const override { return false; }
        bool IsFromStringTable() const override { return true; }

        void UpdateDisplayString()
        {
            // Try to find localized string
            if (!FTextLocalizationManager::Get().FindLocalizedString(
                Namespace, Key, DisplayString))
            {
                // Fall back to default string
                DisplayString = DefaultString;
            }
        }

    private:
        std::string Namespace;
        std::string Key;
        std::wstring DefaultString;
        std::wstring DisplayString;
    };

    /** Empty text data singleton */
    class FEmptyTextData : public ITextData
    {
    public:
        static FEmptyTextData& Get()
        {
            static FEmptyTextData Instance;
            return Instance;
        }

        const std::wstring& GetDisplayString() const override { return EmptyString; }
        const std::wstring& GetSourceString() const override { return EmptyString; }
        const std::string& GetNamespace() const override { return EmptyNamespaceStr; }
        const std::string& GetKey() const override { return EmptyKeyStr; }
        bool IsCultureInvariant() const override { return true; }
        bool IsFromStringTable() const override { return false; }

    private:
        FEmptyTextData() = default;
        static const std::wstring EmptyString;
        static const std::string EmptyNamespaceStr;
        static const std::string EmptyKeyStr;
    };

    const std::wstring FEmptyTextData::EmptyString;
    const std::string FEmptyTextData::EmptyNamespaceStr;
    const std::string FEmptyTextData::EmptyKeyStr;
}

// ============================================================================
// FTextLocalizationManager Implementation
// ============================================================================

FTextLocalizationManager& FTextLocalizationManager::Get()
{
    static FTextLocalizationManager Instance;
    return Instance;
}

FTextLocalizationManager::FTextLocalizationManager()
    : CurrentCulture("en-US")
{
}

void FTextLocalizationManager::SetCurrentCulture(const std::string& CultureCode)
{
    std::lock_guard<std::mutex> Lock(Mutex);
    CurrentCulture = CultureCode;
}

const std::string& FTextLocalizationManager::GetCurrentCulture() const
{
    std::lock_guard<std::mutex> Lock(Mutex);
    return CurrentCulture;
}

void FTextLocalizationManager::RegisterLocalizedString(
    const std::string& Namespace,
    const std::string& Key,
    const std::string& Culture,
    const std::wstring& LocalizedString)
{
    std::lock_guard<std::mutex> Lock(Mutex);
    LocalizationData[Culture][Namespace][Key] = LocalizedString;
}

bool FTextLocalizationManager::FindLocalizedString(
    const std::string& Namespace,
    const std::string& Key,
    std::wstring& OutLocalizedString) const
{
    std::lock_guard<std::mutex> Lock(Mutex);

    // Try current culture
    auto CultureIt = LocalizationData.find(CurrentCulture);
    if (CultureIt != LocalizationData.end())
    {
        auto NamespaceIt = CultureIt->second.find(Namespace);
        if (NamespaceIt != CultureIt->second.end())
        {
            auto KeyIt = NamespaceIt->second.find(Key);
            if (KeyIt != NamespaceIt->second.end())
            {
                OutLocalizedString = KeyIt->second;
                return true;
            }
        }
    }

    // Try base language (e.g., "en" from "en-US")
    size_t DashPos = CurrentCulture.find('-');
    if (DashPos != std::string::npos)
    {
        std::string BaseLanguage = CurrentCulture.substr(0, DashPos);
        CultureIt = LocalizationData.find(BaseLanguage);
        if (CultureIt != LocalizationData.end())
        {
            auto NamespaceIt = CultureIt->second.find(Namespace);
            if (NamespaceIt != CultureIt->second.end())
            {
                auto KeyIt = NamespaceIt->second.find(Key);
                if (KeyIt != NamespaceIt->second.end())
                {
                    OutLocalizedString = KeyIt->second;
                    return true;
                }
            }
        }
    }

    return false;
}

bool FTextLocalizationManager::LoadLocalizationFile(const std::string& FilePath)
{
    // Simple CSV-like format: Culture,Namespace,Key,LocalizedString
    std::ifstream File(FilePath);
    if (!File.is_open())
    {
        return false;
    }

    std::string Line;
    while (std::getline(File, Line))
    {
        if (Line.empty() || Line[0] == '#')
        {
            continue;  // Skip empty lines and comments
        }

        // Parse CSV line (simplified - doesn't handle quoted strings)
        std::vector<std::string> Parts;
        std::stringstream SS(Line);
        std::string Part;
        while (std::getline(SS, Part, ','))
        {
            Parts.push_back(Part);
        }

        if (Parts.size() >= 4)
        {
            // Convert string to wstring (simplified UTF-8 to UTF-16)
            std::wstring LocalizedString(Parts[3].begin(), Parts[3].end());
            RegisterLocalizedString(Parts[1], Parts[2], Parts[0], LocalizedString);
        }
    }

    return true;
}

std::vector<std::string> FTextLocalizationManager::GetAvailableCultures() const
{
    std::lock_guard<std::mutex> Lock(Mutex);
    std::vector<std::string> Cultures;
    for (const auto& Pair : LocalizationData)
    {
        Cultures.push_back(Pair.first);
    }
    return Cultures;
}

// ============================================================================
// FText Implementation
// ============================================================================

FText::FText()
    : TextData(nullptr)
    , Flags(0)
{
}

FText::FText(const FText& Other)
    : TextData(Other.TextData)
    , Flags(Other.Flags)
{
}

FText::FText(FText&& Other) noexcept
    : TextData(std::move(Other.TextData))
    , Flags(Other.Flags)
{
    Other.Flags = 0;
}

FText::~FText() = default;

FText& FText::operator=(const FText& Other)
{
    if (this != &Other)
    {
        TextData = Other.TextData;
        Flags = Other.Flags;
    }
    return *this;
}

FText& FText::operator=(FText&& Other) noexcept
{
    if (this != &Other)
    {
        TextData = std::move(Other.TextData);
        Flags = Other.Flags;
        Other.Flags = 0;
    }
    return *this;
}

FText::FText(std::shared_ptr<ITextData> InData, uint32_t InFlags)
    : TextData(std::move(InData))
    , Flags(InFlags)
{
}

FText FText::GetEmpty()
{
    return FText();
}

FText FText::AsCultureInvariant(const std::wstring& String)
{
    return FText(
        std::make_shared<FSimpleTextData>(String, true),
        ETextFlag::CultureInvariant
    );
}

FText FText::AsCultureInvariant(std::wstring&& String)
{
    return FText(
        std::make_shared<FSimpleTextData>(std::move(String), true),
        ETextFlag::CultureInvariant
    );
}

FText FText::FromString(const std::wstring& String)
{
    return FText(
        std::make_shared<FSimpleTextData>(String, false),
        ETextFlag::InitializedFromString
    );
}

FText FText::FromString(std::wstring&& String)
{
    return FText(
        std::make_shared<FSimpleTextData>(std::move(String), false),
        ETextFlag::InitializedFromString
    );
}

FText FText::FromString(const FString& String)
{
    return FromString(String.ToWString());
}

FText FText::FromStringTable(
    const std::string& Namespace,
    const std::string& Key,
    const std::wstring& DefaultString)
{
    return FText(
        std::make_shared<FLocalizedTextData>(Namespace, Key, DefaultString),
        0
    );
}

FText FText::Format(
    const FText& Pattern,
    const std::unordered_map<std::string, FText>& Arguments)
{
    std::wstring Result = Pattern.ToString();

    for (const auto& Arg : Arguments)
    {
        std::wstring Placeholder = L"{";
        Placeholder += std::wstring(Arg.first.begin(), Arg.first.end());
        Placeholder += L"}";

        size_t Pos = 0;
        while ((Pos = Result.find(Placeholder, Pos)) != std::wstring::npos)
        {
            Result.replace(Pos, Placeholder.length(), Arg.second.ToString());
            Pos += Arg.second.ToString().length();
        }
    }

    return FText::FromString(std::move(Result));
}

FText FText::FormatOrdered(
    const FText& Pattern,
    const std::vector<FText>& Arguments)
{
    std::wstring Result = Pattern.ToString();

    for (size_t i = 0; i < Arguments.size(); ++i)
    {
        std::wstring Placeholder = L"{" + std::to_wstring(i) + L"}";

        size_t Pos = 0;
        while ((Pos = Result.find(Placeholder, Pos)) != std::wstring::npos)
        {
            Result.replace(Pos, Placeholder.length(), Arguments[i].ToString());
            Pos += Arguments[i].ToString().length();
        }
    }

    return FText::FromString(std::move(Result));
}

FText FText::AsNumber(int32_t Value, const FNumberFormattingOptions* Options)
{
    return AsNumber(static_cast<int64_t>(Value), Options);
}

FText FText::AsNumber(int64_t Value, const FNumberFormattingOptions* Options)
{
    const FNumberFormattingOptions& Opts = Options ? *Options : FNumberFormattingOptions::DefaultWithGrouping();

    std::wstringstream SS;

    if (Opts.AlwaysSign && Value >= 0)
    {
        SS << L"+";
    }

    if (Opts.UseGrouping)
    {
        // Simple grouping implementation
        std::wstring NumStr = std::to_wstring(std::abs(Value));
        std::wstring Grouped;
        int Count = 0;
        for (auto It = NumStr.rbegin(); It != NumStr.rend(); ++It)
        {
            if (Count > 0 && Count % 3 == 0)
            {
                Grouped = L"," + Grouped;
            }
            Grouped = *It + Grouped;
            ++Count;
        }
        if (Value < 0)
        {
            Grouped = L"-" + Grouped;
        }
        SS << Grouped;
    }
    else
    {
        SS << Value;
    }

    return FText::FromString(SS.str());
}

FText FText::AsNumber(float Value, const FNumberFormattingOptions* Options)
{
    return AsNumber(static_cast<double>(Value), Options);
}

FText FText::AsNumber(double Value, const FNumberFormattingOptions* Options)
{
    const FNumberFormattingOptions& Opts = Options ? *Options : FNumberFormattingOptions::DefaultWithGrouping();

    std::wstringstream SS;
    SS << std::fixed << std::setprecision(Opts.MaximumFractionalDigits);

    if (Opts.AlwaysSign && Value >= 0)
    {
        SS << L"+";
    }

    SS << Value;

    return FText::FromString(SS.str());
}

FText FText::AsPercent(float Value, const FNumberFormattingOptions* Options)
{
    return AsPercent(static_cast<double>(Value), Options);
}

FText FText::AsPercent(double Value, const FNumberFormattingOptions* Options)
{
    FNumberFormattingOptions PercentOptions = Options ? *Options : FNumberFormattingOptions::DefaultNoGrouping();
    PercentOptions.MaximumFractionalDigits = std::min(PercentOptions.MaximumFractionalDigits, 2);

    std::wstringstream SS;
    SS << std::fixed << std::setprecision(PercentOptions.MaximumFractionalDigits);
    SS << (Value * 100.0) << L"%";

    return FText::FromString(SS.str());
}

FText FText::AsCurrency(
    double Value,
    const std::string& CurrencyCode,
    const FNumberFormattingOptions* Options)
{
    FText NumberText = AsNumber(Value, Options);

    std::wstring CurrencySymbol;
    if (CurrencyCode == "USD")
    {
        CurrencySymbol = L"$";
    }
    else if (CurrencyCode == "EUR")
    {
        CurrencySymbol = L"€";
    }
    else if (CurrencyCode == "GBP")
    {
        CurrencySymbol = L"£";
    }
    else if (CurrencyCode == "JPY")
    {
        CurrencySymbol = L"¥";
    }
    else if (CurrencyCode == "CNY")
    {
        CurrencySymbol = L"¥";
    }
    else
    {
        CurrencySymbol = std::wstring(CurrencyCode.begin(), CurrencyCode.end()) + L" ";
    }

    return FText::FromString(CurrencySymbol + NumberText.ToString());
}

FText FText::AsMemory(uint64_t Bytes, bool bUseIEC)
{
    const double Base = bUseIEC ? 1024.0 : 1000.0;
    const wchar_t* Suffixes[] = {
        bUseIEC ? L" B" : L" B",
        bUseIEC ? L" KiB" : L" KB",
        bUseIEC ? L" MiB" : L" MB",
        bUseIEC ? L" GiB" : L" GB",
        bUseIEC ? L" TiB" : L" TB",
        bUseIEC ? L" PiB" : L" PB"
    };

    double Value = static_cast<double>(Bytes);
    int SuffixIndex = 0;

    while (Value >= Base && SuffixIndex < 5)
    {
        Value /= Base;
        ++SuffixIndex;
    }

    std::wstringstream SS;
    SS << std::fixed << std::setprecision(SuffixIndex > 0 ? 2 : 0) << Value << Suffixes[SuffixIndex];

    return FText::FromString(SS.str());
}

FText FText::ToUpper() const
{
    if (!TextData)
    {
        return FText();
    }

    std::wstring Upper = ToString();
    std::transform(Upper.begin(), Upper.end(), Upper.begin(),
        [](wchar_t c) { return std::towupper(c); });

    return FText::FromString(std::move(Upper));
}

FText FText::ToLower() const
{
    if (!TextData)
    {
        return FText();
    }

    std::wstring Lower = ToString();
    std::transform(Lower.begin(), Lower.end(), Lower.begin(),
        [](wchar_t c) { return std::towlower(c); });

    return FText::FromString(std::move(Lower));
}

bool FText::IsEmpty() const
{
    return !TextData || TextData->GetDisplayString().empty();
}

bool FText::IsEmptyOrWhitespace() const
{
    if (!TextData)
    {
        return true;
    }

    const std::wstring& Str = TextData->GetDisplayString();
    return std::all_of(Str.begin(), Str.end(),
        [](wchar_t c) { return std::iswspace(c); });
}

bool FText::IsCultureInvariant() const
{
    return (Flags & ETextFlag::CultureInvariant) != 0 ||
           (TextData && TextData->IsCultureInvariant());
}

bool FText::IsFromStringTable() const
{
    return TextData && TextData->IsFromStringTable();
}

bool FText::IsTransient() const
{
    return (Flags & ETextFlag::Transient) != 0;
}

bool FText::EqualTo(const FText& Other) const
{
    return ToString() == Other.ToString();
}

bool FText::EqualToCaseIgnored(const FText& Other) const
{
    return ToLower().ToString() == Other.ToLower().ToString();
}

int32_t FText::CompareTo(const FText& Other) const
{
    return ToString().compare(Other.ToString());
}

int32_t FText::CompareToCaseIgnored(const FText& Other) const
{
    return ToLower().ToString().compare(Other.ToLower().ToString());
}

const std::wstring& FText::ToString() const
{
    if (!TextData)
    {
        return EmptyWString;
    }
    return TextData->GetDisplayString();
}

FString FText::ToFString() const
{
    return FString(ToString());
}

const std::string& FText::GetNamespace() const
{
    if (!TextData)
    {
        return EmptyString;
    }
    return TextData->GetNamespace();
}

const std::string& FText::GetKey() const
{
    if (!TextData)
    {
        return EmptyString;
    }
    return TextData->GetKey();
}

bool FText::ShouldRebuildOnCultureChange() const
{
    return IsFromStringTable();
}

} // namespace MonsterEngine
