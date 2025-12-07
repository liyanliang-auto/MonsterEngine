// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file String.h
 * @brief Dynamic string class following UE5 FString patterns
 * 
 * FString is a dynamically sized string with the following features:
 * - Based on TArray<TCHAR> for consistent memory management
 * - Rich string manipulation API
 * - Conversion between character encodings
 * - Printf-style formatting
 */

#include "Core/CoreTypes.h"
#include "Core/Templates/TypeTraits.h"
#include "Core/Templates/TypeHash.h"
#include "ContainerAllocationPolicies.h"
#include "Array.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <string_view>

namespace MonsterEngine
{

// ============================================================================
// Character Type Definitions
// ============================================================================

/** Wide character type (platform-dependent) */
using TCHAR = wchar_t;

/** ANSI character type */
using ANSICHAR = char;

/** Wide character type */
using WIDECHAR = wchar_t;

/** UTF-8 character type */
using UTF8CHAR = char;

// ============================================================================
// Character Conversion Helpers
// ============================================================================

namespace StringConv
{
    /**
     * Converts ANSI string to wide string
     */
    inline std::wstring AnsiToWide(const char* Str)
    {
        if (!Str || !*Str)
        {
            return std::wstring();
        }
        
        size_t Len = std::strlen(Str);
        std::wstring Result(Len, L'\0');
        
        for (size_t i = 0; i < Len; ++i)
        {
            Result[i] = static_cast<wchar_t>(static_cast<unsigned char>(Str[i]));
        }
        
        return Result;
    }
    
    /**
     * Converts wide string to ANSI string
     */
    inline std::string WideToAnsi(const wchar_t* Str)
    {
        if (!Str || !*Str)
        {
            return std::string();
        }
        
        size_t Len = std::wcslen(Str);
        std::string Result(Len, '\0');
        
        for (size_t i = 0; i < Len; ++i)
        {
            Result[i] = static_cast<char>(Str[i] & 0xFF);
        }
        
        return Result;
    }
}

// ============================================================================
// FString
// ============================================================================

/**
 * A dynamically sizeable string
 * 
 * Stores characters as TCHAR (wide characters) using TArray for memory management.
 * Provides a rich API for string manipulation, searching, and formatting.
 */
class FString
{
public:
    using AllocatorType = FDefaultAllocator;
    using ElementType = TCHAR;
    using SizeType = int32;
    
private:
    using DataType = TArray<TCHAR, AllocatorType>;
    DataType Data;
    
public:
    // ========================================================================
    // Constructors and Destructor
    // ========================================================================
    
    /**
     * Default constructor - creates empty string
     */
    FString() = default;
    
    /**
     * Copy constructor
     */
    FString(const FString& Other) = default;
    
    /**
     * Move constructor
     */
    FString(FString&& Other) noexcept = default;
    
    /**
     * Constructor from null-terminated ANSI string
     */
    FString(const ANSICHAR* Str)
    {
        if (Str && *Str)
        {
            std::wstring Wide = StringConv::AnsiToWide(Str);
            Data.Reserve(static_cast<SizeType>(Wide.length()) + 1);
            for (wchar_t C : Wide)
            {
                Data.Add(C);
            }
            Data.Add(L'\0');
        }
    }
    
    /**
     * Constructor from null-terminated wide string
     */
    FString(const WIDECHAR* Str)
    {
        if (Str && *Str)
        {
            size_t Len = std::wcslen(Str);
            Data.Reserve(static_cast<SizeType>(Len) + 1);
            for (size_t i = 0; i < Len; ++i)
            {
                Data.Add(Str[i]);
            }
            Data.Add(L'\0');
        }
    }
    
    /**
     * Constructor from substring
     */
    FString(SizeType Count, const TCHAR* Str)
    {
        if (Str && Count > 0)
        {
            Data.Reserve(Count + 1);
            for (SizeType i = 0; i < Count; ++i)
            {
                Data.Add(Str[i]);
            }
            Data.Add(L'\0');
        }
    }
    
    /**
     * Constructor from std::string
     */
    FString(const std::string& Str)
        : FString(Str.c_str())
    {
    }
    
    /**
     * Constructor from std::wstring
     */
    FString(const std::wstring& Str)
        : FString(Str.c_str())
    {
    }
    
    /**
     * Constructor from std::string_view
     */
    FString(std::string_view Str)
    {
        if (!Str.empty())
        {
            Data.Reserve(static_cast<SizeType>(Str.length()) + 1);
            for (char C : Str)
            {
                Data.Add(static_cast<TCHAR>(static_cast<unsigned char>(C)));
            }
            Data.Add(L'\0');
        }
    }
    
    /**
     * Constructor from std::wstring_view
     */
    FString(std::wstring_view Str)
    {
        if (!Str.empty())
        {
            Data.Reserve(static_cast<SizeType>(Str.length()) + 1);
            for (wchar_t C : Str)
            {
                Data.Add(C);
            }
            Data.Add(L'\0');
        }
    }
    
    /**
     * Destructor
     */
    ~FString() = default;
    
    // ========================================================================
    // Assignment Operators
    // ========================================================================
    
    FString& operator=(const FString& Other) = default;
    FString& operator=(FString&& Other) noexcept = default;
    
    FString& operator=(const ANSICHAR* Str)
    {
        *this = FString(Str);
        return *this;
    }
    
    FString& operator=(const WIDECHAR* Str)
    {
        *this = FString(Str);
        return *this;
    }
    
    FString& operator=(const std::string& Str)
    {
        *this = FString(Str);
        return *this;
    }
    
    FString& operator=(const std::wstring& Str)
    {
        *this = FString(Str);
        return *this;
    }
    
    // ========================================================================
    // Element Access
    // ========================================================================
    
    /**
     * Returns character at index
     */
    FORCEINLINE TCHAR& operator[](SizeType Index)
    {
        return Data[Index];
    }
    
    /**
     * Returns character at index (const)
     */
    FORCEINLINE const TCHAR& operator[](SizeType Index) const
    {
        return Data[Index];
    }
    
    /**
     * Returns pointer to character data
     */
    FORCEINLINE TCHAR* GetCharArray()
    {
        return Data.GetData();
    }
    
    /**
     * Returns const pointer to character data
     */
    FORCEINLINE const TCHAR* GetCharArray() const
    {
        return Data.GetData();
    }
    
    /**
     * Returns null-terminated C string
     */
    FORCEINLINE const TCHAR* operator*() const
    {
        return IsEmpty() ? L"" : Data.GetData();
    }
    
    // ========================================================================
    // Size and Capacity
    // ========================================================================
    
    /**
     * Returns length of string (not including null terminator)
     */
    FORCEINLINE SizeType Len() const
    {
        return Data.Num() > 0 ? Data.Num() - 1 : 0;
    }
    
    /**
     * Returns true if string is empty
     */
    FORCEINLINE bool IsEmpty() const
    {
        return Data.Num() <= 1;
    }
    
    /**
     * Empties the string
     */
    void Empty(SizeType ExpectedNumChars = 0)
    {
        Data.Empty(ExpectedNumChars > 0 ? ExpectedNumChars + 1 : 0);
    }
    
    /**
     * Resets the string without deallocating
     */
    void Reset(SizeType NewReservedSize = 0)
    {
        Data.Reset(NewReservedSize > 0 ? NewReservedSize + 1 : 0);
    }
    
    /**
     * Reserves capacity for characters
     */
    void Reserve(SizeType NumChars)
    {
        Data.Reserve(NumChars + 1);
    }
    
    /**
     * Shrinks the string to fit its contents
     */
    void Shrink()
    {
        Data.Shrink();
    }
    
    // ========================================================================
    // String Concatenation
    // ========================================================================
    
    /**
     * Appends another string
     */
    FString& operator+=(const FString& Other)
    {
        Append(Other);
        return *this;
    }
    
    /**
     * Appends a C string
     */
    FString& operator+=(const TCHAR* Str)
    {
        Append(Str);
        return *this;
    }
    
    /**
     * Appends a character
     */
    FString& operator+=(TCHAR Char)
    {
        AppendChar(Char);
        return *this;
    }
    
    /**
     * Concatenation operator
     */
    FString operator+(const FString& Other) const
    {
        FString Result(*this);
        Result += Other;
        return Result;
    }
    
    /**
     * Concatenation operator with C string
     */
    FString operator+(const TCHAR* Str) const
    {
        FString Result(*this);
        Result += Str;
        return Result;
    }
    
    /**
     * Appends another string
     */
    void Append(const FString& Other)
    {
        if (!Other.IsEmpty())
        {
            Append(*Other, Other.Len());
        }
    }
    
    /**
     * Appends a C string
     */
    void Append(const TCHAR* Str)
    {
        if (Str && *Str)
        {
            Append(Str, static_cast<SizeType>(std::wcslen(Str)));
        }
    }
    
    /**
     * Appends a substring
     */
    void Append(const TCHAR* Str, SizeType Count)
    {
        if (Str && Count > 0)
        {
            // Remove null terminator if present
            if (!IsEmpty())
            {
                Data.SetNumUninitialized(Data.Num() - 1);
            }
            
            // Append characters
            SizeType OldLen = Data.Num();
            Data.SetNumUninitialized(OldLen + Count + 1);
            
            for (SizeType i = 0; i < Count; ++i)
            {
                Data[OldLen + i] = Str[i];
            }
            Data[OldLen + Count] = L'\0';
        }
    }
    
    /**
     * Appends a single character
     */
    void AppendChar(TCHAR Char)
    {
        if (!IsEmpty())
        {
            Data[Data.Num() - 1] = Char;
            Data.Add(L'\0');
        }
        else
        {
            Data.Add(Char);
            Data.Add(L'\0');
        }
    }
    
    // ========================================================================
    // Comparison
    // ========================================================================
    
    /**
     * Case-sensitive equality comparison
     */
    bool operator==(const FString& Other) const
    {
        return Equals(Other, true);
    }
    
    /**
     * Case-sensitive inequality comparison
     */
    bool operator!=(const FString& Other) const
    {
        return !Equals(Other, true);
    }
    
    /**
     * Case-sensitive comparison with C string
     */
    bool operator==(const TCHAR* Str) const
    {
        if (!Str)
        {
            return IsEmpty();
        }
        return std::wcscmp(**this, Str) == 0;
    }
    
    bool operator!=(const TCHAR* Str) const
    {
        return !(*this == Str);
    }
    
    /**
     * Compares strings
     * @param bCaseSensitive If true, comparison is case-sensitive
     */
    bool Equals(const FString& Other, bool bCaseSensitive = true) const
    {
        if (Len() != Other.Len())
        {
            return false;
        }
        
        if (bCaseSensitive)
        {
            return std::wcscmp(**this, *Other) == 0;
        }
        else
        {
            return Compare(Other, false) == 0;
        }
    }
    
    /**
     * Compares strings
     * @return <0 if this < Other, 0 if equal, >0 if this > Other
     */
    int32 Compare(const FString& Other, bool bCaseSensitive = true) const
    {
        if (bCaseSensitive)
        {
            return std::wcscmp(**this, *Other);
        }
        else
        {
            // Case-insensitive comparison
            const TCHAR* A = **this;
            const TCHAR* B = *Other;
            
            while (*A && *B)
            {
                TCHAR CharA = (*A >= L'A' && *A <= L'Z') ? (*A + L'a' - L'A') : *A;
                TCHAR CharB = (*B >= L'A' && *B <= L'Z') ? (*B + L'a' - L'A') : *B;
                
                if (CharA != CharB)
                {
                    return CharA - CharB;
                }
                
                ++A;
                ++B;
            }
            
            return *A - *B;
        }
    }
    
    /**
     * Less than comparison (for sorting)
     */
    bool operator<(const FString& Other) const
    {
        return Compare(Other) < 0;
    }
    
    // ========================================================================
    // Searching
    // ========================================================================
    
    /**
     * Finds first occurrence of substring
     * @return Index of first occurrence, or INDEX_NONE_VALUE if not found
     */
    SizeType Find(const TCHAR* SubStr, bool bCaseSensitive = true, bool bSearchFromEnd = false, SizeType StartPosition = INDEX_NONE_VALUE) const
    {
        if (!SubStr || !*SubStr || IsEmpty())
        {
            return INDEX_NONE_VALUE;
        }
        
        const TCHAR* Start = **this;
        SizeType SubLen = static_cast<SizeType>(std::wcslen(SubStr));
        SizeType StrLen = Len();
        
        if (SubLen > StrLen)
        {
            return INDEX_NONE_VALUE;
        }
        
        if (bSearchFromEnd)
        {
            SizeType SearchStart = (StartPosition == INDEX_NONE_VALUE) ? StrLen - SubLen : std::min(StartPosition, StrLen - SubLen);
            
            for (SizeType i = SearchStart; i >= 0; --i)
            {
                bool bFound = true;
                for (SizeType j = 0; j < SubLen; ++j)
                {
                    TCHAR A = Start[i + j];
                    TCHAR B = SubStr[j];
                    
                    if (!bCaseSensitive)
                    {
                        if (A >= L'A' && A <= L'Z') A += L'a' - L'A';
                        if (B >= L'A' && B <= L'Z') B += L'a' - L'A';
                    }
                    
                    if (A != B)
                    {
                        bFound = false;
                        break;
                    }
                }
                
                if (bFound)
                {
                    return i;
                }
            }
        }
        else
        {
            SizeType SearchStart = (StartPosition == INDEX_NONE_VALUE) ? 0 : StartPosition;
            
            for (SizeType i = SearchStart; i <= StrLen - SubLen; ++i)
            {
                bool bFound = true;
                for (SizeType j = 0; j < SubLen; ++j)
                {
                    TCHAR A = Start[i + j];
                    TCHAR B = SubStr[j];
                    
                    if (!bCaseSensitive)
                    {
                        if (A >= L'A' && A <= L'Z') A += L'a' - L'A';
                        if (B >= L'A' && B <= L'Z') B += L'a' - L'A';
                    }
                    
                    if (A != B)
                    {
                        bFound = false;
                        break;
                    }
                }
                
                if (bFound)
                {
                    return i;
                }
            }
        }
        
        return INDEX_NONE_VALUE;
    }
    
    /**
     * Finds first occurrence of character
     */
    SizeType Find(TCHAR Char, SizeType StartPosition = 0) const
    {
        const TCHAR* Start = **this;
        for (SizeType i = StartPosition; i < Len(); ++i)
        {
            if (Start[i] == Char)
            {
                return i;
            }
        }
        return INDEX_NONE_VALUE;
    }
    
    /**
     * Checks if string contains substring
     */
    bool Contains(const TCHAR* SubStr, bool bCaseSensitive = true) const
    {
        return Find(SubStr, bCaseSensitive) != INDEX_NONE_VALUE;
    }
    
    /**
     * Checks if string starts with prefix
     */
    bool StartsWith(const TCHAR* Prefix, bool bCaseSensitive = true) const
    {
        if (!Prefix || !*Prefix)
        {
            return true;
        }
        
        SizeType PrefixLen = static_cast<SizeType>(std::wcslen(Prefix));
        if (PrefixLen > Len())
        {
            return false;
        }
        
        const TCHAR* Start = **this;
        for (SizeType i = 0; i < PrefixLen; ++i)
        {
            TCHAR A = Start[i];
            TCHAR B = Prefix[i];
            
            if (!bCaseSensitive)
            {
                if (A >= L'A' && A <= L'Z') A += L'a' - L'A';
                if (B >= L'A' && B <= L'Z') B += L'a' - L'A';
            }
            
            if (A != B)
            {
                return false;
            }
        }
        
        return true;
    }
    
    /**
     * Checks if string ends with suffix
     */
    bool EndsWith(const TCHAR* Suffix, bool bCaseSensitive = true) const
    {
        if (!Suffix || !*Suffix)
        {
            return true;
        }
        
        SizeType SuffixLen = static_cast<SizeType>(std::wcslen(Suffix));
        if (SuffixLen > Len())
        {
            return false;
        }
        
        const TCHAR* Start = **this + Len() - SuffixLen;
        for (SizeType i = 0; i < SuffixLen; ++i)
        {
            TCHAR A = Start[i];
            TCHAR B = Suffix[i];
            
            if (!bCaseSensitive)
            {
                if (A >= L'A' && A <= L'Z') A += L'a' - L'A';
                if (B >= L'A' && B <= L'Z') B += L'a' - L'A';
            }
            
            if (A != B)
            {
                return false;
            }
        }
        
        return true;
    }
    
    // ========================================================================
    // Substring Operations
    // ========================================================================
    
    /**
     * Returns substring starting at given index
     */
    FString Mid(SizeType Start, SizeType Count = INT32_MAX) const
    {
        if (Start >= Len())
        {
            return FString();
        }
        
        Count = std::min(Count, Len() - Start);
        return FString(Count, **this + Start);
    }
    
    /**
     * Returns leftmost characters
     */
    FString Left(SizeType Count) const
    {
        return Mid(0, Count);
    }
    
    /**
     * Returns rightmost characters
     */
    FString Right(SizeType Count) const
    {
        if (Count >= Len())
        {
            return *this;
        }
        return Mid(Len() - Count);
    }
    
    /**
     * Returns string with leftmost characters removed
     */
    FString RightChop(SizeType Count) const
    {
        return Mid(Count);
    }
    
    /**
     * Returns string with rightmost characters removed
     */
    FString LeftChop(SizeType Count) const
    {
        return Left(Len() - Count);
    }
    
    // ========================================================================
    // Case Conversion
    // ========================================================================
    
    /**
     * Converts to uppercase
     */
    FString ToUpper() const
    {
        FString Result(*this);
        for (SizeType i = 0; i < Result.Len(); ++i)
        {
            TCHAR& C = Result[i];
            if (C >= L'a' && C <= L'z')
            {
                C -= L'a' - L'A';
            }
        }
        return Result;
    }
    
    /**
     * Converts to lowercase
     */
    FString ToLower() const
    {
        FString Result(*this);
        for (SizeType i = 0; i < Result.Len(); ++i)
        {
            TCHAR& C = Result[i];
            if (C >= L'A' && C <= L'Z')
            {
                C += L'a' - L'A';
            }
        }
        return Result;
    }
    
    // ========================================================================
    // Trimming
    // ========================================================================
    
    /**
     * Trims whitespace from start
     */
    FString TrimStart() const
    {
        SizeType Start = 0;
        while (Start < Len() && IsWhitespace((*this)[Start]))
        {
            ++Start;
        }
        return Mid(Start);
    }
    
    /**
     * Trims whitespace from end
     */
    FString TrimEnd() const
    {
        SizeType End = Len();
        while (End > 0 && IsWhitespace((*this)[End - 1]))
        {
            --End;
        }
        return Left(End);
    }
    
    /**
     * Trims whitespace from both ends
     */
    FString TrimStartAndEnd() const
    {
        return TrimStart().TrimEnd();
    }
    
    // ========================================================================
    // Replace
    // ========================================================================
    
    /**
     * Replaces all occurrences of From with To
     */
    FString Replace(const TCHAR* From, const TCHAR* To, bool bCaseSensitive = true) const
    {
        if (!From || !*From)
        {
            return *this;
        }
        
        FString Result;
        SizeType FromLen = static_cast<SizeType>(std::wcslen(From));
        SizeType LastEnd = 0;
        SizeType Pos = Find(From, bCaseSensitive, false, 0);
        
        while (Pos != INDEX_NONE_VALUE)
        {
            Result.Append(**this + LastEnd, Pos - LastEnd);
            if (To && *To)
            {
                Result.Append(To);
            }
            LastEnd = Pos + FromLen;
            Pos = Find(From, bCaseSensitive, false, LastEnd);
        }
        
        Result.Append(**this + LastEnd, Len() - LastEnd);
        return Result;
    }
    
    // ========================================================================
    // Conversion
    // ========================================================================
    
    /**
     * Converts to std::string (ANSI)
     */
    std::string ToAnsiString() const
    {
        return StringConv::WideToAnsi(**this);
    }
    
    /**
     * Converts to std::wstring
     */
    std::wstring ToWideString() const
    {
        return std::wstring(**this);
    }
    
    /**
     * Converts string to integer
     */
    int32 ToInt() const
    {
        return static_cast<int32>(std::wcstol(**this, nullptr, 10));
    }
    
    /**
     * Converts string to float
     */
    float ToFloat() const
    {
        return std::wcstof(**this, nullptr);
    }
    
    /**
     * Converts string to double
     */
    double ToDouble() const
    {
        return std::wcstod(**this, nullptr);
    }
    
    // ========================================================================
    // Static Factory Methods
    // ========================================================================
    
    /**
     * Creates string from integer
     */
    static FString FromInt(int32 Value)
    {
        wchar_t Buffer[32];
        std::swprintf(Buffer, 32, L"%d", Value);
        return FString(Buffer);
    }
    
    /**
     * Creates string from float
     */
    static FString FromFloat(float Value)
    {
        wchar_t Buffer[64];
        std::swprintf(Buffer, 64, L"%f", Value);
        return FString(Buffer);
    }
    
    /**
     * Creates string from double
     */
    static FString FromDouble(double Value)
    {
        wchar_t Buffer[64];
        std::swprintf(Buffer, 64, L"%f", Value);
        return FString(Buffer);
    }
    
    /**
     * Printf-style string formatting
     */
    static FString Printf(const TCHAR* Format, ...)
    {
        va_list Args;
        va_start(Args, Format);
        
        // Determine required size
        va_list ArgsCopy;
        va_copy(ArgsCopy, Args);
        int Size = std::vswprintf(nullptr, 0, Format, ArgsCopy);
        va_end(ArgsCopy);
        
        if (Size < 0)
        {
            va_end(Args);
            return FString();
        }
        
        // Format string
        std::wstring Buffer(Size + 1, L'\0');
        std::vswprintf(Buffer.data(), Buffer.size(), Format, Args);
        va_end(Args);
        
        return FString(Buffer.c_str());
    }
    
    // ========================================================================
    // Conversion to std::wstring
    // ========================================================================
    
    /**
     * Converts to std::wstring
     */
    std::wstring ToWString() const
    {
        if (IsEmpty())
        {
            return std::wstring();
        }
        return std::wstring(**this, Len());
    }
    
    /**
     * Converts to std::string (ANSI)
     */
    std::string ToStdString() const
    {
        return StringConv::WideToAnsi(**this);
    }
    
    // ========================================================================
    // Hashing
    // ========================================================================
    
    friend uint32 GetTypeHash(const FString& Str)
    {
        return GetTypeHash(*Str);
    }
    
    // ========================================================================
    // Iteration
    // ========================================================================
    
    FORCEINLINE TCHAR* begin() { return GetCharArray(); }
    FORCEINLINE const TCHAR* begin() const { return GetCharArray(); }
    FORCEINLINE TCHAR* end() { return GetCharArray() + Len(); }
    FORCEINLINE const TCHAR* end() const { return GetCharArray() + Len(); }
    
private:
    static bool IsWhitespace(TCHAR C)
    {
        return C == L' ' || C == L'\t' || C == L'\n' || C == L'\r';
    }
};

// ============================================================================
// String Literals
// ============================================================================

/**
 * Concatenation with C string on left
 */
inline FString operator+(const TCHAR* Lhs, const FString& Rhs)
{
    return FString(Lhs) + Rhs;
}

// ============================================================================
// TEXT Macro
// ============================================================================

#ifndef TEXT
    #define TEXT(x) L##x
#endif

} // namespace MonsterEngine
