// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file Text.h
 * @brief FText - Localized text type for internationalization
 * 
 * This file defines FText, a text type that supports localization and
 * culture-aware formatting. Unlike FString, FText maintains information
 * about its source for localization purposes.
 */

#include "Core/CoreTypes.h"
#include "Containers/String.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <mutex>
#include <functional>
#include <vector>
#include <locale>

namespace MonsterEngine
{

// Forward declarations
class FText;
class FTextLocalizationManager;

// ============================================================================
// Text Flags
// ============================================================================

namespace ETextFlag
{
    enum Type : uint32_t
    {
        /** Text is transient and should not be saved */
        Transient = (1 << 0),
        /** Text is culture invariant */
        CultureInvariant = (1 << 1),
        /** Text was converted from a property */
        ConvertedProperty = (1 << 2),
        /** Text is immutable */
        Immutable = (1 << 3),
        /** Text was initialized from a string */
        InitializedFromString = (1 << 4),
    };
}

// ============================================================================
// Text Gender (for gendered languages)
// ============================================================================

enum class ETextGender : uint8_t
{
    Masculine,
    Feminine,
    Neuter
};

// ============================================================================
// Text Plural Form
// ============================================================================

enum class ETextPluralForm : uint8_t
{
    Zero = 0,
    One,        // Singular
    Two,        // Dual
    Few,        // Paucal
    Many,       // Also used for fractions
    Other,      // General plural form
    Count
};

// ============================================================================
// Date/Time Style
// ============================================================================

namespace EDateTimeStyle
{
    enum Type
    {
        Default,
        Short,
        Medium,
        Long,
        Full
    };
}

// ============================================================================
// Number Formatting Options
// ============================================================================

enum class ERoundingMode : int32_t
{
    /** Rounds to nearest, ties go to even */
    HalfToEven,
    /** Rounds to nearest, ties go away from zero */
    HalfFromZero,
    /** Rounds to nearest, ties go toward zero */
    HalfToZero,
    /** Always round away from zero */
    FromZero,
    /** Always round toward zero */
    ToZero,
    /** Always round toward negative infinity */
    ToNegativeInfinity,
    /** Always round toward positive infinity */
    ToPositiveInfinity
};

struct FNumberFormattingOptions
{
    bool AlwaysSign = false;
    bool UseGrouping = true;
    ERoundingMode RoundingMode = ERoundingMode::HalfToEven;
    int32_t MinimumIntegralDigits = 1;
    int32_t MaximumIntegralDigits = 324;
    int32_t MinimumFractionalDigits = 0;
    int32_t MaximumFractionalDigits = 3;

    FNumberFormattingOptions& SetAlwaysSign(bool InValue) { AlwaysSign = InValue; return *this; }
    FNumberFormattingOptions& SetUseGrouping(bool InValue) { UseGrouping = InValue; return *this; }
    FNumberFormattingOptions& SetRoundingMode(ERoundingMode InValue) { RoundingMode = InValue; return *this; }
    FNumberFormattingOptions& SetMinimumIntegralDigits(int32_t InValue) { MinimumIntegralDigits = InValue; return *this; }
    FNumberFormattingOptions& SetMaximumIntegralDigits(int32_t InValue) { MaximumIntegralDigits = InValue; return *this; }
    FNumberFormattingOptions& SetMinimumFractionalDigits(int32_t InValue) { MinimumFractionalDigits = InValue; return *this; }
    FNumberFormattingOptions& SetMaximumFractionalDigits(int32_t InValue) { MaximumFractionalDigits = InValue; return *this; }

    static const FNumberFormattingOptions& DefaultWithGrouping();
    static const FNumberFormattingOptions& DefaultNoGrouping();
};

// ============================================================================
// ITextData - Internal text data interface
// ============================================================================

class ITextData
{
public:
    virtual ~ITextData() = default;
    
    /** Get the display string */
    virtual const std::wstring& GetDisplayString() const = 0;
    
    /** Get the source string (for localization) */
    virtual const std::wstring& GetSourceString() const = 0;
    
    /** Get the namespace */
    virtual const std::string& GetNamespace() const = 0;
    
    /** Get the key */
    virtual const std::string& GetKey() const = 0;
    
    /** Check if this is culture invariant */
    virtual bool IsCultureInvariant() const = 0;
    
    /** Check if this is from a string table */
    virtual bool IsFromStringTable() const = 0;
};

// ============================================================================
// FTextLocalizationManager - Manages localized text
// ============================================================================

class FTextLocalizationManager
{
public:
    /** Get the singleton instance */
    static FTextLocalizationManager& Get();

    /** Set the current culture */
    void SetCurrentCulture(const std::string& CultureCode);

    /** Get the current culture */
    const std::string& GetCurrentCulture() const;

    /** Register a localized string */
    void RegisterLocalizedString(
        const std::string& Namespace,
        const std::string& Key,
        const std::string& Culture,
        const std::wstring& LocalizedString
    );

    /** Find a localized string */
    bool FindLocalizedString(
        const std::string& Namespace,
        const std::string& Key,
        std::wstring& OutLocalizedString
    ) const;

    /** Load localization data from file */
    bool LoadLocalizationFile(const std::string& FilePath);

    /** Get available cultures */
    std::vector<std::string> GetAvailableCultures() const;

private:
    FTextLocalizationManager();
    ~FTextLocalizationManager() = default;

    /** Current culture code (e.g., "en-US", "zh-CN") */
    std::string CurrentCulture;

    /** Localization data: Culture -> Namespace -> Key -> LocalizedString */
    std::unordered_map<std::string, 
        std::unordered_map<std::string, 
            std::unordered_map<std::string, std::wstring>>> LocalizationData;

    /** Mutex for thread safety */
    mutable std::mutex Mutex;
};

// ============================================================================
// FText - Localized Text
// ============================================================================

/**
 * @brief Localized text type for internationalization
 * 
 * FText is the primary type for user-facing text that may need to be
 * translated. It maintains source information for localization and
 * supports culture-aware formatting.
 * 
 * Key differences from FString:
 * - Maintains localization context (namespace, key)
 * - Supports automatic translation lookup
 * - Culture-aware number/date/time formatting
 * - Immutable display string (regenerated on culture change)
 */
class FText
{
public:
    // ========================================================================
    // Constructors
    // ========================================================================

    /** Default constructor - creates empty text */
    FText();

    /** Copy constructor */
    FText(const FText& Other);

    /** Move constructor */
    FText(FText&& Other) noexcept;

    /** Destructor */
    ~FText();

    /** Copy assignment */
    FText& operator=(const FText& Other);

    /** Move assignment */
    FText& operator=(FText&& Other) noexcept;

public:
    // ========================================================================
    // Factory Functions
    // ========================================================================

    /** Create empty text */
    static FText GetEmpty();

    /**
     * Create from a literal string (culture invariant)
     * Use for text that should NOT be localized
     */
    static FText AsCultureInvariant(const std::wstring& String);
    static FText AsCultureInvariant(std::wstring&& String);

    /**
     * Create from a string (for display only, not localizable)
     * Use when you need to display a dynamic string
     */
    static FText FromString(const std::wstring& String);
    static FText FromString(std::wstring&& String);
    static FText FromString(const FString& String);

    /**
     * Create localized text with namespace and key
     * This is the primary way to create localizable text
     */
    static FText FromStringTable(
        const std::string& Namespace,
        const std::string& Key,
        const std::wstring& DefaultString
    );

public:
    // ========================================================================
    // Formatting Functions
    // ========================================================================

    /**
     * Format text with named arguments
     * Example: FText::Format(L"Hello {Name}!", {{"Name", FText::FromString(L"World")}})
     */
    static FText Format(
        const FText& Pattern,
        const std::unordered_map<std::string, FText>& Arguments
    );

    /**
     * Format text with ordered arguments
     * Example: FText::FormatOrdered(L"Hello {0}!", {FText::FromString(L"World")})
     */
    static FText FormatOrdered(
        const FText& Pattern,
        const std::vector<FText>& Arguments
    );

    /**
     * Format a number as text
     */
    static FText AsNumber(int32_t Value, const FNumberFormattingOptions* Options = nullptr);
    static FText AsNumber(int64_t Value, const FNumberFormattingOptions* Options = nullptr);
    static FText AsNumber(float Value, const FNumberFormattingOptions* Options = nullptr);
    static FText AsNumber(double Value, const FNumberFormattingOptions* Options = nullptr);

    /**
     * Format a percentage
     */
    static FText AsPercent(float Value, const FNumberFormattingOptions* Options = nullptr);
    static FText AsPercent(double Value, const FNumberFormattingOptions* Options = nullptr);

    /**
     * Format currency
     */
    static FText AsCurrency(
        double Value,
        const std::string& CurrencyCode,
        const FNumberFormattingOptions* Options = nullptr
    );

    /**
     * Format memory size (bytes to KB/MB/GB)
     */
    static FText AsMemory(uint64_t Bytes, bool bUseIEC = true);

public:
    // ========================================================================
    // Case Conversion
    // ========================================================================

    /** Convert to uppercase */
    FText ToUpper() const;

    /** Convert to lowercase */
    FText ToLower() const;

public:
    // ========================================================================
    // Comparison
    // ========================================================================

    /** Check if text is empty */
    bool IsEmpty() const;

    /** Check if text is empty or whitespace only */
    bool IsEmptyOrWhitespace() const;

    /** Check if this is culture invariant */
    bool IsCultureInvariant() const;

    /** Check if this is from a string table */
    bool IsFromStringTable() const;

    /** Check if this is transient */
    bool IsTransient() const;

    /** Compare two texts for equality */
    bool EqualTo(const FText& Other) const;

    /** Compare two texts (case-insensitive) */
    bool EqualToCaseIgnored(const FText& Other) const;

    /** Lexicographic comparison */
    int32_t CompareTo(const FText& Other) const;

    /** Lexicographic comparison (case-insensitive) */
    int32_t CompareToCaseIgnored(const FText& Other) const;

public:
    // ========================================================================
    // Accessors
    // ========================================================================

    /** Get the display string */
    const std::wstring& ToString() const;

    /** Convert to FString */
    FString ToFString() const;

    /** Get the namespace (empty if not from string table) */
    const std::string& GetNamespace() const;

    /** Get the key (empty if not from string table) */
    const std::string& GetKey() const;

    /** Check if text should be rebuilt on culture change */
    bool ShouldRebuildOnCultureChange() const;

public:
    // ========================================================================
    // Operators
    // ========================================================================

    bool operator==(const FText& Other) const { return EqualTo(Other); }
    bool operator!=(const FText& Other) const { return !EqualTo(Other); }
    bool operator<(const FText& Other) const { return CompareTo(Other) < 0; }
    bool operator>(const FText& Other) const { return CompareTo(Other) > 0; }
    bool operator<=(const FText& Other) const { return CompareTo(Other) <= 0; }
    bool operator>=(const FText& Other) const { return CompareTo(Other) >= 0; }

private:
    /** Internal constructor */
    FText(std::shared_ptr<ITextData> InData, uint32_t InFlags);

private:
    /** The text data */
    std::shared_ptr<ITextData> TextData;

    /** Text flags */
    uint32_t Flags;

    /** Empty text singleton */
    static FText EmptyText;

    /** Empty string for returning references */
    static const std::wstring EmptyWString;
    static const std::string EmptyString;
};

// ============================================================================
// Localization Macros
// ============================================================================

/**
 * LOCTEXT - Create localized text
 * Usage: LOCTEXT("Namespace", "Key", L"Default text")
 */
#define LOCTEXT(Namespace, Key, Default) \
    MonsterEngine::FText::FromStringTable(Namespace, Key, Default)

/**
 * NSLOCTEXT - Create localized text with explicit namespace
 * Usage: NSLOCTEXT("MyNamespace", "MyKey", L"Default text")
 */
#define NSLOCTEXT(Namespace, Key, Default) \
    MonsterEngine::FText::FromStringTable(Namespace, Key, Default)

/**
 * INVTEXT - Create culture-invariant text
 * Usage: INVTEXT(L"This text will not be translated")
 */
#define INVTEXT(String) \
    MonsterEngine::FText::AsCultureInvariant(String)

} // namespace MonsterEngine
