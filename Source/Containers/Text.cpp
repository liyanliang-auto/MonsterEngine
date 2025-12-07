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

FText::FText(TSharedPtr<ITextData> InData, uint32_t InFlags)
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
        MakeShared<FSimpleTextData>(String, true),
        ETextFlag::CultureInvariant
    );
}

FText FText::AsCultureInvariant(std::wstring&& String)
{
    return FText(
        MakeShared<FSimpleTextData>(std::move(String), true),
        ETextFlag::CultureInvariant
    );
}

FText FText::FromString(const std::wstring& String)
{
    return FText(
        MakeShared<FSimpleTextData>(String, false),
        ETextFlag::InitializedFromString
    );
}

FText FText::FromString(std::wstring&& String)
{
    return FText(
        MakeShared<FSimpleTextData>(std::move(String), false),
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
        MakeShared<FLocalizedTextData>(Namespace, Key, DefaultString),
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
        CurrencySymbol = L"\u20AC";  // Euro sign
    }
    else if (CurrencyCode == "GBP")
    {
        CurrencySymbol = L"\u00A3";  // Pound sign
    }
    else if (CurrencyCode == "JPY")
    {
        CurrencySymbol = L"\u00A5";  // Yen sign
    }
    else if (CurrencyCode == "CNY")
    {
        CurrencySymbol = L"\u00A5";  // Yuan sign (same as Yen)
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

FText FText::AsDate(int32_t Year, int32_t Month, int32_t Day, EDateTimeStyle::Type Style)
{
    std::wstringstream SS;
    
    // Month names for Long/Full styles
    static const wchar_t* MonthNames[] = {
        L"January", L"February", L"March", L"April", L"May", L"June",
        L"July", L"August", L"September", L"October", L"November", L"December"
    };
    
    static const wchar_t* MonthNamesShort[] = {
        L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun",
        L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec"
    };
    
    switch (Style)
    {
        case EDateTimeStyle::Short:
            // MM/DD/YY
            SS << std::setfill(L'0') << std::setw(2) << Month << L"/"
               << std::setw(2) << Day << L"/"
               << std::setw(2) << (Year % 100);
            break;
            
        case EDateTimeStyle::Medium:
            // MMM DD, YYYY
            if (Month >= 1 && Month <= 12)
            {
                SS << MonthNamesShort[Month - 1] << L" " << Day << L", " << Year;
            }
            break;
            
        case EDateTimeStyle::Long:
            // MMMM DD, YYYY
            if (Month >= 1 && Month <= 12)
            {
                SS << MonthNames[Month - 1] << L" " << Day << L", " << Year;
            }
            break;
            
        case EDateTimeStyle::Full:
            // DayOfWeek, MMMM DD, YYYY (simplified - no day of week calculation)
            if (Month >= 1 && Month <= 12)
            {
                SS << MonthNames[Month - 1] << L" " << Day << L", " << Year;
            }
            break;
            
        case EDateTimeStyle::Default:
        default:
            // YYYY-MM-DD (ISO 8601)
            SS << Year << L"-"
               << std::setfill(L'0') << std::setw(2) << Month << L"-"
               << std::setw(2) << Day;
            break;
    }
    
    return FText::FromString(SS.str());
}

FText FText::AsTime(int32_t Hour, int32_t Minute, int32_t Second, EDateTimeStyle::Type Style)
{
    std::wstringstream SS;
    
    switch (Style)
    {
        case EDateTimeStyle::Short:
            // HH:MM AM/PM
            {
                int Hour12 = Hour % 12;
                if (Hour12 == 0) Hour12 = 12;
                SS << Hour12 << L":"
                   << std::setfill(L'0') << std::setw(2) << Minute
                   << (Hour < 12 ? L" AM" : L" PM");
            }
            break;
            
        case EDateTimeStyle::Medium:
            // HH:MM:SS AM/PM
            {
                int Hour12 = Hour % 12;
                if (Hour12 == 0) Hour12 = 12;
                SS << Hour12 << L":"
                   << std::setfill(L'0') << std::setw(2) << Minute << L":"
                   << std::setw(2) << Second
                   << (Hour < 12 ? L" AM" : L" PM");
            }
            break;
            
        case EDateTimeStyle::Long:
        case EDateTimeStyle::Full:
            // HH:MM:SS AM/PM timezone (simplified - no timezone)
            {
                int Hour12 = Hour % 12;
                if (Hour12 == 0) Hour12 = 12;
                SS << Hour12 << L":"
                   << std::setfill(L'0') << std::setw(2) << Minute << L":"
                   << std::setw(2) << Second
                   << (Hour < 12 ? L" AM" : L" PM");
            }
            break;
            
        case EDateTimeStyle::Default:
        default:
            // HH:MM:SS (24-hour)
            SS << std::setfill(L'0') << std::setw(2) << Hour << L":"
               << std::setw(2) << Minute << L":"
               << std::setw(2) << Second;
            break;
    }
    
    return FText::FromString(SS.str());
}

FText FText::AsDateTime(
    int32_t Year, int32_t Month, int32_t Day,
    int32_t Hour, int32_t Minute, int32_t Second,
    EDateTimeStyle::Type DateStyle, EDateTimeStyle::Type TimeStyle)
{
    FText DateText = AsDate(Year, Month, Day, DateStyle);
    FText TimeText = AsTime(Hour, Minute, Second, TimeStyle);
    
    return FText::FromString(DateText.ToString() + L" " + TimeText.ToString());
}

FText FText::AsTimespan(int64_t TotalSeconds)
{
    std::wstringstream SS;
    
    bool bNegative = TotalSeconds < 0;
    if (bNegative)
    {
        TotalSeconds = -TotalSeconds;
        SS << L"-";
    }
    
    int64_t Days = TotalSeconds / 86400;
    int64_t Hours = (TotalSeconds % 86400) / 3600;
    int64_t Minutes = (TotalSeconds % 3600) / 60;
    int64_t Seconds = TotalSeconds % 60;
    
    if (Days > 0)
    {
        SS << Days << (Days == 1 ? L" day, " : L" days, ");
    }
    
    SS << std::setfill(L'0') << std::setw(2) << Hours << L":"
       << std::setw(2) << Minutes << L":"
       << std::setw(2) << Seconds;
    
    return FText::FromString(SS.str());
}

FText FText::FormatPlural(int64_t Count, const FText& Singular, const FText& Plural)
{
    // Simple English plural rule: 1 = singular, else plural
    if (Count == 1 || Count == -1)
    {
        return Singular;
    }
    return Plural;
}

ETextPluralForm FText::GetPluralForm(int64_t Count, ETextPluralType Type)
{
    // Get current culture
    const std::string& Culture = FTextLocalizationManager::Get().GetCurrentCulture();
    
    // Simplified plural rules based on common language families
    // Full implementation would use CLDR plural rules
    
    int64_t AbsCount = Count < 0 ? -Count : Count;
    
    // English-like languages (en, de, nl, etc.)
    if (Culture.find("en") == 0 || Culture.find("de") == 0 || Culture.find("nl") == 0)
    {
        if (AbsCount == 1) return ETextPluralForm::One;
        return ETextPluralForm::Other;
    }
    
    // French-like languages (fr, pt-BR)
    if (Culture.find("fr") == 0 || Culture == "pt-BR")
    {
        if (AbsCount == 0 || AbsCount == 1) return ETextPluralForm::One;
        return ETextPluralForm::Other;
    }
    
    // Russian/Slavic languages
    if (Culture.find("ru") == 0 || Culture.find("uk") == 0 || Culture.find("pl") == 0)
    {
        int64_t Mod10 = AbsCount % 10;
        int64_t Mod100 = AbsCount % 100;
        
        if (Mod10 == 1 && Mod100 != 11)
        {
            return ETextPluralForm::One;
        }
        if (Mod10 >= 2 && Mod10 <= 4 && (Mod100 < 12 || Mod100 > 14))
        {
            return ETextPluralForm::Few;
        }
        return ETextPluralForm::Many;
    }
    
    // Arabic
    if (Culture.find("ar") == 0)
    {
        if (AbsCount == 0) return ETextPluralForm::Zero;
        if (AbsCount == 1) return ETextPluralForm::One;
        if (AbsCount == 2) return ETextPluralForm::Two;
        
        int64_t Mod100 = AbsCount % 100;
        if (Mod100 >= 3 && Mod100 <= 10) return ETextPluralForm::Few;
        if (Mod100 >= 11 && Mod100 <= 99) return ETextPluralForm::Many;
        return ETextPluralForm::Other;
    }
    
    // Chinese, Japanese, Korean (no plural forms)
    if (Culture.find("zh") == 0 || Culture.find("ja") == 0 || Culture.find("ko") == 0)
    {
        return ETextPluralForm::Other;
    }
    
    // Default: English-like
    if (AbsCount == 1) return ETextPluralForm::One;
    return ETextPluralForm::Other;
}

FText FText::FormatPluralFull(
    int64_t Count,
    const std::unordered_map<ETextPluralForm, FText>& Forms)
{
    ETextPluralForm Form = GetPluralForm(Count);
    
    // Try to find the exact form
    auto It = Forms.find(Form);
    if (It != Forms.end())
    {
        return It->second;
    }
    
    // Fall back to Other
    It = Forms.find(ETextPluralForm::Other);
    if (It != Forms.end())
    {
        return It->second;
    }
    
    // Fall back to One
    It = Forms.find(ETextPluralForm::One);
    if (It != Forms.end())
    {
        return It->second;
    }
    
    // Return empty if nothing found
    return FText::GetEmpty();
}

FText FText::FormatGender(
    ETextGender Gender,
    const FText& Masculine,
    const FText& Feminine,
    const FText& Neuter)
{
    switch (Gender)
    {
        case ETextGender::Masculine:
            return Masculine;
        case ETextGender::Feminine:
            return Feminine;
        case ETextGender::Neuter:
            return Neuter.IsEmpty() ? Masculine : Neuter;
        default:
            return Masculine;
    }
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
