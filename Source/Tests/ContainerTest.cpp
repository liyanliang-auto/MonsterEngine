// Copyright Monster Engine. All Rights Reserved.

/**
 * @file ContainerTest.cpp
 * @brief Test suite for container implementations
 * 
 * Tests TArray, TSparseArray, TSet, TMap, FString, FName, and serialization.
 */

#include "Containers/Containers.h"
#include "Core/Logging/LogMacros.h"

#include <iostream>
#include <cassert>

// Use MonsterRender logging types
namespace ELogVerbosity = MonsterRender::ELogVerbosity;
using MonsterRender::LogTemp;

namespace MonsterEngine
{

// ============================================================================
// Test Helpers
// ============================================================================

static int32 TestsPassed = 0;
static int32 TestsFailed = 0;

#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            MR_LOG(LogTemp, Error, "FAILED: %s - %s", #condition, message); \
            ++TestsFailed; \
        } else { \
            ++TestsPassed; \
        } \
    } while(0)

#define TEST_SECTION(name) \
    MR_LOG(LogTemp, Log, "=== Testing %s ===", name)

// ============================================================================
// TArray Tests
// ============================================================================

void TestTArray()
{
    TEST_SECTION("TArray");
    
    // Basic operations
    {
        TArray<int32> Arr;
        TEST_ASSERT(Arr.IsEmpty(), "New array should be empty");
        TEST_ASSERT(Arr.Num() == 0, "New array should have 0 elements");
        
        Arr.Add(10);
        Arr.Add(20);
        Arr.Add(30);
        
        TEST_ASSERT(Arr.Num() == 3, "Array should have 3 elements");
        TEST_ASSERT(Arr[0] == 10, "First element should be 10");
        TEST_ASSERT(Arr[1] == 20, "Second element should be 20");
        TEST_ASSERT(Arr[2] == 30, "Third element should be 30");
        TEST_ASSERT(Arr.First() == 10, "First() should return 10");
        TEST_ASSERT(Arr.Last() == 30, "Last() should return 30");
    }
    
    // Initializer list
    {
        TArray<int32> Arr = {1, 2, 3, 4, 5};
        TEST_ASSERT(Arr.Num() == 5, "Initializer list should create 5 elements");
        TEST_ASSERT(Arr[4] == 5, "Last element should be 5");
    }
    
    // Find and Contains
    {
        TArray<int32> Arr = {10, 20, 30, 40, 50};
        TEST_ASSERT(Arr.Find(30) == 2, "Find should return index 2");
        TEST_ASSERT(Arr.Find(100) == INDEX_NONE_VALUE, "Find should return INDEX_NONE for missing element");
        TEST_ASSERT(Arr.Contains(40), "Contains should return true for 40");
        TEST_ASSERT(!Arr.Contains(100), "Contains should return false for 100");
    }
    
    // Remove operations
    {
        TArray<int32> Arr = {1, 2, 3, 4, 5};
        Arr.RemoveAt(2);
        TEST_ASSERT(Arr.Num() == 4, "After RemoveAt, should have 4 elements");
        TEST_ASSERT(Arr[2] == 4, "After RemoveAt(2), element at 2 should be 4");
        
        Arr.Remove(4);
        TEST_ASSERT(Arr.Num() == 3, "After Remove(4), should have 3 elements");
        TEST_ASSERT(!Arr.Contains(4), "After Remove(4), should not contain 4");
    }
    
    // Insert
    {
        TArray<int32> Arr = {1, 3, 4};
        Arr.Insert(2, 1);
        TEST_ASSERT(Arr.Num() == 4, "After Insert, should have 4 elements");
        TEST_ASSERT(Arr[1] == 2, "Inserted element should be at index 1");
    }
    
    // Sort
    {
        TArray<int32> Arr = {5, 2, 8, 1, 9};
        Arr.Sort();
        TEST_ASSERT(Arr[0] == 1, "After sort, first should be 1");
        TEST_ASSERT(Arr[4] == 9, "After sort, last should be 9");
    }
    
    // Range-based for
    {
        TArray<int32> Arr = {1, 2, 3};
        int32 Sum = 0;
        for (int32 Val : Arr)
        {
            Sum += Val;
        }
        TEST_ASSERT(Sum == 6, "Range-based for should iterate all elements");
    }
    
    // Copy and move
    {
        TArray<int32> Arr1 = {1, 2, 3};
        TArray<int32> Arr2 = Arr1;
        TEST_ASSERT(Arr2.Num() == 3, "Copy should preserve size");
        TEST_ASSERT(Arr2[0] == 1, "Copy should preserve values");
        
        TArray<int32> Arr3 = std::move(Arr1);
        TEST_ASSERT(Arr3.Num() == 3, "Move should preserve size");
        TEST_ASSERT(Arr1.IsEmpty(), "Moved-from array should be empty");
    }
    
    MR_LOG_INFO("TArray tests completed");
}

// ============================================================================
// TSparseArray Tests
// ============================================================================

void TestTSparseArray()
{
    TEST_SECTION("TSparseArray");
    
    // Basic operations
    {
        TSparseArray<int32> Arr;
        TEST_ASSERT(Arr.IsEmpty(), "New sparse array should be empty");
        
        int32 Idx1 = Arr.Add(100);
        int32 Idx2 = Arr.Add(200);
        int32 Idx3 = Arr.Add(300);
        
        TEST_ASSERT(Arr.Num() == 3, "Should have 3 elements");
        TEST_ASSERT(Arr[Idx1] == 100, "Element at Idx1 should be 100");
        TEST_ASSERT(Arr[Idx2] == 200, "Element at Idx2 should be 200");
        TEST_ASSERT(Arr[Idx3] == 300, "Element at Idx3 should be 300");
    }
    
    // Remove and reuse
    {
        TSparseArray<int32> Arr;
        int32 Idx1 = Arr.Add(10);
        int32 Idx2 = Arr.Add(20);
        int32 Idx3 = Arr.Add(30);
        
        Arr.RemoveAt(Idx2);
        TEST_ASSERT(Arr.Num() == 2, "After remove, should have 2 elements");
        TEST_ASSERT(!Arr.IsAllocated(Idx2), "Removed index should not be allocated");
        
        // Add new element - should reuse freed index
        int32 Idx4 = Arr.Add(40);
        TEST_ASSERT(Arr.Num() == 3, "After add, should have 3 elements");
        TEST_ASSERT(Arr[Idx4] == 40, "New element should be 40");
    }
    
    // Iteration
    {
        TSparseArray<int32> Arr;
        Arr.Add(1);
        Arr.Add(2);
        Arr.Add(3);
        Arr.RemoveAt(1);
        
        int32 Sum = 0;
        for (int32 Val : Arr)
        {
            Sum += Val;
        }
        TEST_ASSERT(Sum == 4, "Iteration should skip removed elements (1+3=4)");
    }
    
    MR_LOG_INFO("TSparseArray tests completed");
}

// ============================================================================
// TSet Tests
// ============================================================================

void TestTSet()
{
    TEST_SECTION("TSet");
    
    // Basic operations
    {
        TSet<int32> Set;
        TEST_ASSERT(Set.IsEmpty(), "New set should be empty");
        
        Set.Add(10);
        Set.Add(20);
        Set.Add(30);
        
        TEST_ASSERT(Set.Num() == 3, "Set should have 3 elements");
        TEST_ASSERT(Set.Contains(20), "Set should contain 20");
        TEST_ASSERT(!Set.Contains(40), "Set should not contain 40");
    }
    
    // Duplicates
    {
        TSet<int32> Set;
        Set.Add(10);
        Set.Add(10);
        Set.Add(10);
        
        TEST_ASSERT(Set.Num() == 1, "Set should ignore duplicates");
    }
    
    // Remove
    {
        TSet<int32> Set = {1, 2, 3, 4, 5};
        Set.Remove(3);
        TEST_ASSERT(Set.Num() == 4, "After remove, should have 4 elements");
        TEST_ASSERT(!Set.Contains(3), "Should not contain removed element");
    }
    
    // Set operations
    {
        TSet<int32> A = {1, 2, 3, 4};
        TSet<int32> B = {3, 4, 5, 6};
        
        TSet<int32> Intersection = A.Intersect(B);
        TEST_ASSERT(Intersection.Num() == 2, "Intersection should have 2 elements");
        TEST_ASSERT(Intersection.Contains(3), "Intersection should contain 3");
        TEST_ASSERT(Intersection.Contains(4), "Intersection should contain 4");
        
        TSet<int32> Union = A.Union(B);
        TEST_ASSERT(Union.Num() == 6, "Union should have 6 elements");
        
        TSet<int32> Diff = A.Difference(B);
        TEST_ASSERT(Diff.Num() == 2, "Difference should have 2 elements");
        TEST_ASSERT(Diff.Contains(1), "Difference should contain 1");
        TEST_ASSERT(Diff.Contains(2), "Difference should contain 2");
    }
    
    MR_LOG_INFO("TSet tests completed");
}

// ============================================================================
// TMap Tests
// ============================================================================

void TestTMap()
{
    TEST_SECTION("TMap");
    
    // Basic operations
    {
        TMap<int32, FString> Map;
        TEST_ASSERT(Map.IsEmpty(), "New map should be empty");
        
        Map.Add(1, FString(L"One"));
        Map.Add(2, FString(L"Two"));
        Map.Add(3, FString(L"Three"));
        
        TEST_ASSERT(Map.Num() == 3, "Map should have 3 elements");
        TEST_ASSERT(Map.Contains(2), "Map should contain key 2");
        TEST_ASSERT(!Map.Contains(4), "Map should not contain key 4");
    }
    
    // Find
    {
        TMap<int32, int32> Map;
        Map.Add(10, 100);
        Map.Add(20, 200);
        
        int32* Value = Map.Find(10);
        TEST_ASSERT(Value != nullptr, "Find should return pointer for existing key");
        TEST_ASSERT(*Value == 100, "Found value should be 100");
        
        int32* Missing = Map.Find(30);
        TEST_ASSERT(Missing == nullptr, "Find should return nullptr for missing key");
    }
    
    // Bracket operator
    {
        TMap<int32, int32> Map;
        Map[1] = 10;
        Map[2] = 20;
        
        TEST_ASSERT(Map[1] == 10, "Bracket operator should get value");
        TEST_ASSERT(Map[2] == 20, "Bracket operator should get value");
        
        Map[1] = 100;
        TEST_ASSERT(Map[1] == 100, "Bracket operator should update value");
    }
    
    // Remove
    {
        TMap<int32, int32> Map = {{1, 10}, {2, 20}, {3, 30}};
        Map.Remove(2);
        TEST_ASSERT(Map.Num() == 2, "After remove, should have 2 elements");
        TEST_ASSERT(!Map.Contains(2), "Should not contain removed key");
    }
    
    // GetKeys
    {
        TMap<int32, int32> Map = {{1, 10}, {2, 20}, {3, 30}};
        TArray<int32> Keys = Map.GetKeys();
        TEST_ASSERT(Keys.Num() == 3, "GetKeys should return 3 keys");
    }
    
    MR_LOG_INFO("TMap tests completed");
}

// ============================================================================
// FString Tests
// ============================================================================

void TestFString()
{
    TEST_SECTION("FString");
    
    // Construction
    {
        FString Empty;
        TEST_ASSERT(Empty.IsEmpty(), "Default string should be empty");
        
        FString FromAnsi("Hello");
        TEST_ASSERT(FromAnsi.Len() == 5, "ANSI string should have length 5");
        
        FString FromWide(L"World");
        TEST_ASSERT(FromWide.Len() == 5, "Wide string should have length 5");
    }
    
    // Concatenation
    {
        FString A(L"Hello");
        FString B(L" World");
        FString C = A + B;
        TEST_ASSERT(C.Len() == 11, "Concatenated string should have length 11");
        
        A += B;
        TEST_ASSERT(A.Len() == 11, "After +=, string should have length 11");
    }
    
    // Comparison
    {
        FString A(L"Test");
        FString B(L"Test");
        FString C(L"test");
        
        TEST_ASSERT(A == B, "Equal strings should compare equal");
        TEST_ASSERT(A != C, "Different case strings should not be equal (case-sensitive)");
        TEST_ASSERT(A.Equals(C, false), "Case-insensitive comparison should match");
    }
    
    // Find
    {
        FString Str(L"Hello World");
        TEST_ASSERT(Str.Find(L"World") == 6, "Find should return correct index");
        TEST_ASSERT(Str.Find(L"xyz") == INDEX_NONE_VALUE, "Find should return INDEX_NONE for missing");
        TEST_ASSERT(Str.Contains(L"llo"), "Contains should return true");
    }
    
    // StartsWith/EndsWith
    {
        FString Str(L"Hello World");
        TEST_ASSERT(Str.StartsWith(L"Hello"), "StartsWith should match");
        TEST_ASSERT(Str.EndsWith(L"World"), "EndsWith should match");
        TEST_ASSERT(!Str.StartsWith(L"World"), "StartsWith should not match");
    }
    
    // Substring
    {
        FString Str(L"Hello World");
        FString Mid = Str.Mid(6, 5);
        TEST_ASSERT(Mid == L"World", "Mid should extract substring");
        
        FString Left = Str.Left(5);
        TEST_ASSERT(Left == L"Hello", "Left should extract prefix");
        
        FString Right = Str.Right(5);
        TEST_ASSERT(Right == L"World", "Right should extract suffix");
    }
    
    // Case conversion
    {
        FString Str(L"Hello World");
        TEST_ASSERT(Str.ToUpper() == L"HELLO WORLD", "ToUpper should convert");
        TEST_ASSERT(Str.ToLower() == L"hello world", "ToLower should convert");
    }
    
    // Replace
    {
        FString Str(L"Hello World");
        FString Replaced = Str.Replace(L"World", L"Universe");
        TEST_ASSERT(Replaced == L"Hello Universe", "Replace should work");
    }
    
    // Numeric conversion
    {
        FString IntStr = FString::FromInt(42);
        TEST_ASSERT(IntStr.ToInt() == 42, "Int conversion should round-trip");
        
        FString FloatStr = FString::FromFloat(3.14f);
        float Val = FloatStr.ToFloat();
        TEST_ASSERT(Val > 3.13f && Val < 3.15f, "Float conversion should be close");
    }
    
    MR_LOG_INFO("FString tests completed");
}

// ============================================================================
// FName Tests
// ============================================================================

void TestFName()
{
    TEST_SECTION("FName");
    
    // Construction
    {
        FName Empty;
        TEST_ASSERT(Empty.IsNone(), "Default FName should be None");
        
        FName Name1(L"TestName");
        TEST_ASSERT(!Name1.IsNone(), "Named FName should not be None");
        TEST_ASSERT(Name1.IsValid(), "Named FName should be valid");
    }
    
    // Comparison (O(1))
    {
        FName A(L"MyName");
        FName B(L"MyName");
        FName C(L"OtherName");
        
        TEST_ASSERT(A == B, "Same names should be equal");
        TEST_ASSERT(A != C, "Different names should not be equal");
    }
    
    // Case insensitivity
    {
        FName Lower(L"testname");
        FName Upper(L"TESTNAME");
        FName Mixed(L"TestName");
        
        TEST_ASSERT(Lower == Upper, "Names should be case-insensitive");
        TEST_ASSERT(Lower == Mixed, "Names should be case-insensitive");
    }
    
    // Number suffix
    {
        FName WithNumber(L"Actor_5");
        TEST_ASSERT(WithNumber.GetNumber() == 6, "Number should be parsed (internal = external + 1)");
        
        FString Str = WithNumber.ToString();
        TEST_ASSERT(Str == L"Actor_5", "ToString should include number");
    }
    
    // Global deduplication (Flyweight)
    {
        int32 CountBefore = FName::GetNumNames();
        
        FName A(L"UniqueTestName123");
        FName B(L"UniqueTestName123");
        FName C(L"UniqueTestName123");
        
        int32 CountAfter = FName::GetNumNames();
        TEST_ASSERT(CountAfter == CountBefore + 1, "Same name should only add one entry");
        
        // All should share same index
        TEST_ASSERT(A.GetComparisonIndex() == B.GetComparisonIndex(), "Same names should share index");
        TEST_ASSERT(B.GetComparisonIndex() == C.GetComparisonIndex(), "Same names should share index");
    }
    
    MR_LOG_INFO("FName tests completed");
}

// ============================================================================
// Serialization Tests
// ============================================================================

void TestSerialization()
{
    TEST_SECTION("Serialization");
    
    // Basic types
    {
        TArray<uint8> Buffer;
        
        // Write
        {
            FMemoryWriter Writer(Buffer);
            int32 IntVal = 42;
            float FloatVal = 3.14f;
            bool BoolVal = true;
            
            Writer << IntVal;
            Writer << FloatVal;
            Writer << BoolVal;
        }
        
        // Read
        {
            FMemoryReader Reader(Buffer);
            int32 IntVal = 0;
            float FloatVal = 0.0f;
            bool BoolVal = false;
            
            Reader << IntVal;
            Reader << FloatVal;
            Reader << BoolVal;
            
            TEST_ASSERT(IntVal == 42, "Int should serialize correctly");
            TEST_ASSERT(FloatVal > 3.13f && FloatVal < 3.15f, "Float should serialize correctly");
            TEST_ASSERT(BoolVal == true, "Bool should serialize correctly");
        }
    }
    
    // String
    {
        TArray<uint8> Buffer;
        
        {
            FMemoryWriter Writer(Buffer);
            std::string Str = "Hello World";
            Writer << Str;
        }
        
        {
            FMemoryReader Reader(Buffer);
            std::string Str;
            Reader << Str;
            TEST_ASSERT(Str == "Hello World", "String should serialize correctly");
        }
    }
    
    // TArray
    {
        TArray<uint8> Buffer;
        
        {
            FMemoryWriter Writer(Buffer);
            TArray<int32> Arr = {1, 2, 3, 4, 5};
            Writer << Arr;
        }
        
        {
            FMemoryReader Reader(Buffer);
            TArray<int32> Arr;
            Reader << Arr;
            TEST_ASSERT(Arr.Num() == 5, "Array should serialize with correct size");
            TEST_ASSERT(Arr[0] == 1, "Array elements should serialize correctly");
            TEST_ASSERT(Arr[4] == 5, "Array elements should serialize correctly");
        }
    }
    
    // TMap
    {
        TArray<uint8> Buffer;
        
        {
            FMemoryWriter Writer(Buffer);
            TMap<int32, int32> Map;
            Map.Add(1, 100);
            Map.Add(2, 200);
            Map.Add(3, 300);
            Writer << Map;
        }
        
        {
            FMemoryReader Reader(Buffer);
            TMap<int32, int32> Map;
            Reader << Map;
            TEST_ASSERT(Map.Num() == 3, "Map should serialize with correct size");
            TEST_ASSERT(Map.Contains(1), "Map should contain key 1");
            TEST_ASSERT(Map.Contains(2), "Map should contain key 2");
            TEST_ASSERT(Map.Contains(3), "Map should contain key 3");
            int32* Val1 = Map.Find(1);
            int32* Val2 = Map.Find(2);
            int32* Val3 = Map.Find(3);
            TEST_ASSERT(Val1 && *Val1 == 100, "Map value for key 1 should be 100");
            TEST_ASSERT(Val2 && *Val2 == 200, "Map value for key 2 should be 200");
            TEST_ASSERT(Val3 && *Val3 == 300, "Map value for key 3 should be 300");
        }
    }
    
    // TSet
    {
        TArray<uint8> Buffer;
        
        {
            FMemoryWriter Writer(Buffer);
            TSet<int32> Set;
            Set.Add(10);
            Set.Add(20);
            Set.Add(30);
            Writer << Set;
        }
        
        {
            FMemoryReader Reader(Buffer);
            TSet<int32> Set;
            Reader << Set;
            TEST_ASSERT(Set.Num() == 3, "Set should serialize with correct size");
            TEST_ASSERT(Set.Contains(10), "Set should contain 10");
            TEST_ASSERT(Set.Contains(20), "Set should contain 20");
            TEST_ASSERT(Set.Contains(30), "Set should contain 30");
        }
    }
    
    MR_LOG_INFO("Serialization tests completed");
}

// ============================================================================
// Main Test Runner
// ============================================================================

void RunContainerTests()
{
    MR_LOG_INFO("========================================");
    MR_LOG_INFO("  Container System Tests");
    MR_LOG_INFO("========================================");
    
    TestsPassed = 0;
    TestsFailed = 0;
    
    TestTArray();
    TestTSparseArray();
    TestTSet();
    TestTMap();
    TestFString();
    TestFName();
    TestSerialization();
    
    MR_LOG_INFO("========================================");
    MR_LOG_INFO("  Test Results: %d passed, %d failed", TestsPassed, TestsFailed);
    MR_LOG_INFO("========================================");
    
    if (TestsFailed > 0)
    {
        MR_LOG_ERROR("Some tests failed!");
    }
    else
    {
        MR_LOG_INFO("All tests passed!");
    }
}

} // namespace MonsterEngine
