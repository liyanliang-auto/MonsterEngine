// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file Name.h
 * @brief Global unique name system following UE5 FName patterns
 * 
 * FName provides a globally unique, case-insensitive name system with:
 * - Flyweight pattern: All identical strings share the same entry
 * - O(1) comparison: Names are compared by index, not string content
 * - Thread-safe name table
 * - Number suffix support (e.g., "Actor_5")
 */

#include "Core/CoreTypes.h"
#include "Core/Templates/TypeTraits.h"
#include "Core/Templates/TypeHash.h"
#include "String.h"

#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>
#include <string>

namespace MonsterEngine
{

// Forward declarations
class FName;
class FNameEntry;
class FNamePool;

// ============================================================================
// FNameEntryId
// ============================================================================

/**
 * Opaque ID to a deduplicated name entry
 */
struct FNameEntryId
{
    /**
     * Default constructor - creates NAME_None equivalent
     */
    FNameEntryId() : Value(0) {}
    
    /**
     * Returns true if this is NAME_None
     */
    bool IsNone() const
    {
        return Value == 0;
    }
    
    /**
     * Comparison operators
     */
    bool operator==(FNameEntryId Other) const { return Value == Other.Value; }
    bool operator!=(FNameEntryId Other) const { return Value != Other.Value; }
    bool operator<(FNameEntryId Other) const { return Value < Other.Value; }
    
    /**
     * Explicit bool conversion
     */
    explicit operator bool() const { return Value != 0; }
    
    /**
     * Get process-specific integer (not stable across runs)
     */
    uint32 ToUnstableInt() const { return Value; }
    
    /**
     * Create from unstable int
     */
    static FNameEntryId FromUnstableInt(uint32 InValue)
    {
        FNameEntryId Id;
        Id.Value = InValue;
        return Id;
    }
    
    friend uint32 GetTypeHash(FNameEntryId Id)
    {
        return Id.Value;
    }
    
private:
    uint32 Value;
    
    friend class FNamePool;
    friend class FName;
};

// ============================================================================
// FNameEntry
// ============================================================================

/**
 * A global deduplicated name stored in the name table
 */
class FNameEntry
{
public:
    /**
     * Returns the name string
     */
    const std::wstring& GetString() const { return NameString; }
    
    /**
     * Returns the lowercase version for comparison
     */
    const std::wstring& GetComparisonString() const { return ComparisonString; }
    
    /**
     * Returns the name length
     */
    int32 GetNameLength() const { return static_cast<int32>(NameString.length()); }
    
    /**
     * Appends name to FString
     */
    void AppendNameToString(FString& OutString) const
    {
        OutString.Append(NameString.c_str());
    }
    
    /**
     * Returns name as FString
     */
    FString GetPlainNameString() const
    {
        return FString(NameString.c_str());
    }
    
private:
    friend class FNamePool;
    
    FNameEntry(const std::wstring& InName)
        : NameString(InName)
    {
        // Create lowercase comparison string
        ComparisonString = InName;
        for (wchar_t& C : ComparisonString)
        {
            if (C >= L'A' && C <= L'Z')
            {
                C += L'a' - L'A';
            }
        }
    }
    
    std::wstring NameString;
    std::wstring ComparisonString;
};

// ============================================================================
// FNamePool
// ============================================================================

/**
 * Global name pool (singleton)
 * Stores all unique name entries and provides thread-safe access
 */
class FNamePool
{
public:
    /**
     * Gets the singleton instance
     */
    static FNamePool& Get()
    {
        static FNamePool Instance;
        return Instance;
    }
    
    /**
     * Finds or adds a name entry
     * @return Entry ID for the name
     */
    FNameEntryId FindOrAdd(const wchar_t* Name)
    {
        if (!Name || !*Name)
        {
            return FNameEntryId(); // NAME_None
        }
        
        // Create comparison key (lowercase)
        std::wstring ComparisonKey = Name;
        for (wchar_t& C : ComparisonKey)
        {
            if (C >= L'A' && C <= L'Z')
            {
                C += L'a' - L'A';
            }
        }
        
        // Try read lock first (fast path)
        {
            std::shared_lock<std::shared_mutex> ReadLock(Mutex);
            auto It = NameToIndex.find(ComparisonKey);
            if (It != NameToIndex.end())
            {
                return FNameEntryId::FromUnstableInt(It->second);
            }
        }
        
        // Need to add - acquire write lock
        std::unique_lock<std::shared_mutex> WriteLock(Mutex);
        
        // Double-check after acquiring write lock
        auto It = NameToIndex.find(ComparisonKey);
        if (It != NameToIndex.end())
        {
            return FNameEntryId::FromUnstableInt(It->second);
        }
        
        // Add new entry
        uint32 NewIndex = static_cast<uint32>(Entries.size());
        Entries.push_back(std::make_unique<FNameEntry>(Name));
        NameToIndex[ComparisonKey] = NewIndex;
        
        return FNameEntryId::FromUnstableInt(NewIndex);
    }
    
    /**
     * Finds a name entry without adding
     * @return Entry ID, or invalid ID if not found
     */
    FNameEntryId Find(const wchar_t* Name) const
    {
        if (!Name || !*Name)
        {
            return FNameEntryId();
        }
        
        // Create comparison key (lowercase)
        std::wstring ComparisonKey = Name;
        for (wchar_t& C : ComparisonKey)
        {
            if (C >= L'A' && C <= L'Z')
            {
                C += L'a' - L'A';
            }
        }
        
        std::shared_lock<std::shared_mutex> ReadLock(Mutex);
        auto It = NameToIndex.find(ComparisonKey);
        if (It != NameToIndex.end())
        {
            return FNameEntryId::FromUnstableInt(It->second);
        }
        
        return FNameEntryId();
    }
    
    /**
     * Gets entry by ID
     */
    const FNameEntry* GetEntry(FNameEntryId Id) const
    {
        if (Id.IsNone())
        {
            return nullptr;
        }
        
        std::shared_lock<std::shared_mutex> ReadLock(Mutex);
        uint32 Index = Id.ToUnstableInt();
        if (Index < Entries.size())
        {
            return Entries[Index].get();
        }
        return nullptr;
    }
    
    /**
     * Returns number of unique names
     */
    int32 GetNumNames() const
    {
        std::shared_lock<std::shared_mutex> ReadLock(Mutex);
        return static_cast<int32>(Entries.size());
    }
    
private:
    FNamePool()
    {
        // Reserve entry 0 for NAME_None
        Entries.push_back(std::make_unique<FNameEntry>(L"None"));
        NameToIndex[L"none"] = 0;
    }
    
    ~FNamePool() = default;
    
    FNamePool(const FNamePool&) = delete;
    FNamePool& operator=(const FNamePool&) = delete;
    
    mutable std::shared_mutex Mutex;
    std::vector<std::unique_ptr<FNameEntry>> Entries;
    std::unordered_map<std::wstring, uint32> NameToIndex;
};

// ============================================================================
// FName
// ============================================================================

/**
 * A globally unique name
 * 
 * FName provides fast comparison and storage for names that are frequently
 * used and compared. All identical names share the same entry in the global
 * name table (Flyweight pattern).
 * 
 * Features:
 * - O(1) comparison (compares indices, not strings)
 * - Case-insensitive storage and comparison
 * - Number suffix support (e.g., "Actor_5")
 * - Thread-safe
 */
class FName
{
public:
    // ========================================================================
    // Constructors
    // ========================================================================
    
    /**
     * Default constructor - creates NAME_None
     */
    FName()
        : ComparisonIndex()
        , Number(0)
    {
    }
    
    /**
     * Constructor from wide string
     */
    FName(const wchar_t* Name)
        : Number(0)
    {
        Init(Name);
    }
    
    /**
     * Constructor from ANSI string
     */
    FName(const char* Name)
        : Number(0)
    {
        if (Name && *Name)
        {
            std::wstring Wide;
            while (*Name)
            {
                Wide += static_cast<wchar_t>(static_cast<unsigned char>(*Name++));
            }
            Init(Wide.c_str());
        }
    }
    
    /**
     * Constructor from FString
     */
    FName(const FString& Name)
        : Number(0)
    {
        Init(*Name);
    }
    
    /**
     * Constructor from std::string
     */
    FName(const std::string& Name)
        : FName(Name.c_str())
    {
    }
    
    /**
     * Constructor from std::wstring
     */
    FName(const std::wstring& Name)
        : FName(Name.c_str())
    {
    }
    
    /**
     * Constructor with explicit number
     */
    FName(const wchar_t* Name, int32 InNumber)
        : Number(InNumber)
    {
        // Don't parse number from string, use provided number
        ComparisonIndex = FNamePool::Get().FindOrAdd(Name);
    }
    
    /**
     * Copy constructor
     */
    FName(const FName&) = default;
    
    /**
     * Move constructor
     */
    FName(FName&&) = default;
    
    // ========================================================================
    // Assignment
    // ========================================================================
    
    FName& operator=(const FName&) = default;
    FName& operator=(FName&&) = default;
    
    // ========================================================================
    // Comparison
    // ========================================================================
    
    /**
     * Fast equality comparison (O(1))
     */
    FORCEINLINE bool operator==(const FName& Other) const
    {
        return ComparisonIndex == Other.ComparisonIndex && Number == Other.Number;
    }
    
    FORCEINLINE bool operator!=(const FName& Other) const
    {
        return !(*this == Other);
    }
    
    /**
     * Comparison for sorting
     */
    bool operator<(const FName& Other) const
    {
        if (ComparisonIndex != Other.ComparisonIndex)
        {
            return ComparisonIndex < Other.ComparisonIndex;
        }
        return Number < Other.Number;
    }
    
    /**
     * Compares with string (slower, requires lookup)
     */
    bool operator==(const wchar_t* Other) const
    {
        return *this == FName(Other);
    }
    
    bool operator!=(const wchar_t* Other) const
    {
        return !(*this == Other);
    }
    
    // ========================================================================
    // Accessors
    // ========================================================================
    
    /**
     * Returns true if this is NAME_None
     */
    FORCEINLINE bool IsNone() const
    {
        return ComparisonIndex.IsNone() || ComparisonIndex.ToUnstableInt() == 0;
    }
    
    /**
     * Returns true if this is a valid name (not None)
     */
    FORCEINLINE bool IsValid() const
    {
        return !IsNone();
    }
    
    /**
     * Returns the number suffix
     */
    FORCEINLINE int32 GetNumber() const
    {
        return Number;
    }
    
    /**
     * Returns the comparison index
     */
    FORCEINLINE FNameEntryId GetComparisonIndex() const
    {
        return ComparisonIndex;
    }
    
    /**
     * Returns the name entry
     */
    const FNameEntry* GetEntry() const
    {
        return FNamePool::Get().GetEntry(ComparisonIndex);
    }
    
    // ========================================================================
    // String Conversion
    // ========================================================================
    
    /**
     * Returns the plain name string (without number)
     */
    FString GetPlainNameString() const
    {
        const FNameEntry* Entry = GetEntry();
        if (Entry)
        {
            return Entry->GetPlainNameString();
        }
        return FString(TEXT("None"));
    }
    
    /**
     * Converts to FString (includes number if present)
     */
    FString ToString() const
    {
        if (IsNone())
        {
            return FString(TEXT("None"));
        }
        
        FString Result = GetPlainNameString();
        
        if (Number != 0)
        {
            Result += TEXT("_");
            Result += FString::FromInt(Number - 1); // External number is internal - 1
        }
        
        return Result;
    }
    
    /**
     * Appends to FString
     */
    void AppendString(FString& OutString) const
    {
        OutString += ToString();
    }
    
    // ========================================================================
    // Hashing
    // ========================================================================
    
    friend uint32 GetTypeHash(const FName& Name)
    {
        return HashCombineFast(GetTypeHash(Name.ComparisonIndex), GetTypeHash(Name.Number));
    }
    
    // ========================================================================
    // Static Methods
    // ========================================================================
    
    /**
     * Returns NAME_None
     */
    static FName None()
    {
        return FName();
    }
    
    /**
     * Returns number of unique names in the table
     */
    static int32 GetNumNames()
    {
        return FNamePool::Get().GetNumNames();
    }
    
private:
    void Init(const wchar_t* Name)
    {
        if (!Name || !*Name)
        {
            ComparisonIndex = FNameEntryId();
            Number = 0;
            return;
        }
        
        // Check for number suffix (e.g., "Actor_5")
        std::wstring NameStr = Name;
        size_t UnderscorePos = NameStr.rfind(L'_');
        
        if (UnderscorePos != std::wstring::npos && UnderscorePos < NameStr.length() - 1)
        {
            // Check if everything after underscore is a number
            bool bIsNumber = true;
            int32 ParsedNumber = 0;
            
            for (size_t i = UnderscorePos + 1; i < NameStr.length(); ++i)
            {
                wchar_t C = NameStr[i];
                if (C >= L'0' && C <= L'9')
                {
                    ParsedNumber = ParsedNumber * 10 + (C - L'0');
                }
                else
                {
                    bIsNumber = false;
                    break;
                }
            }
            
            if (bIsNumber)
            {
                // Split name and number
                std::wstring BaseName = NameStr.substr(0, UnderscorePos);
                ComparisonIndex = FNamePool::Get().FindOrAdd(BaseName.c_str());
                Number = ParsedNumber + 1; // Internal number is external + 1
                return;
            }
        }
        
        // No number suffix
        ComparisonIndex = FNamePool::Get().FindOrAdd(Name);
        Number = 0;
    }
    
    FNameEntryId ComparisonIndex;
    int32 Number; // 0 means no number, otherwise internal number (external + 1)
};

// ============================================================================
// Common Names
// ============================================================================

/**
 * NAME_None constant
 */
#define NAME_None FName()

} // namespace MonsterEngine
