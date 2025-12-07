// Copyright Monster Engine. All Rights Reserved.

/**
 * @file SmartPointerTest.cpp
 * @brief Test suite for smart pointer implementations
 * 
 * Tests TSharedPtr, TSharedRef, TWeakPtr, TUniquePtr and TSharedFromThis.
 * Verifies reference counting, thread safety, and memory management.
 */

#include "Core/Templates/SharedPointer.h"
#include "Core/Templates/UniquePtr.h"
#include "Core/CoreTypes.h"

#include <cstdio>
#include <thread>
#include <vector>
#include <atomic>

namespace MonsterEngine
{

// ============================================================================
// Test Helper Classes
// ============================================================================

/** Simple test class to track construction/destruction */
class FTestObject
{
public:
    static std::atomic<int32> ConstructCount;
    static std::atomic<int32> DestructCount;

    int32 Value;

    FTestObject() : Value(0)
    {
        ++ConstructCount;
    }

    explicit FTestObject(int32 InValue) : Value(InValue)
    {
        ++ConstructCount;
    }

    virtual ~FTestObject()
    {
        ++DestructCount;
    }

    static void ResetCounters()
    {
        ConstructCount = 0;
        DestructCount = 0;
    }

    static int32 GetAliveCount()
    {
        return ConstructCount - DestructCount;
    }
};

std::atomic<int32> FTestObject::ConstructCount{0};
std::atomic<int32> FTestObject::DestructCount{0};

/** Derived class for testing polymorphism */
class FDerivedTestObject : public FTestObject
{
public:
    int32 DerivedValue;

    FDerivedTestObject() : FTestObject(), DerivedValue(0) {}
    explicit FDerivedTestObject(int32 InValue) : FTestObject(InValue), DerivedValue(InValue * 2) {}
};

/** Test class that inherits from TSharedFromThis */
class FSharedFromThisTest : public TSharedFromThis<FSharedFromThisTest>
{
public:
    int32 Value;

    explicit FSharedFromThisTest(int32 InValue) : Value(InValue) {}
};

// ============================================================================
// Test Macros
// ============================================================================

static int32 TestsPassed = 0;
static int32 TestsFailed = 0;

#define TEST_CHECK(Condition, Message) \
    do { \
        if (Condition) { \
            ++TestsPassed; \
            printf("  [PASS] %s\n", Message); \
        } else { \
            ++TestsFailed; \
            printf("  [FAIL] %s\n", Message); \
        } \
    } while(0)

#define TEST_SECTION(Name) \
    printf("\n--- %s ---\n", Name); \
    fflush(stdout)

// ============================================================================
// TUniquePtr Tests
// ============================================================================

void TestTUniquePtr()
{
    TEST_SECTION("TUniquePtr Tests");
    FTestObject::ResetCounters();

    // Test 1: Basic construction and destruction
    {
        TUniquePtr<FTestObject> Ptr = MakeUnique<FTestObject>(42);
        TEST_CHECK(Ptr.IsValid(), "TUniquePtr: IsValid after construction");
        TEST_CHECK(Ptr->Value == 42, "TUniquePtr: Value access via ->");
        TEST_CHECK((*Ptr).Value == 42, "TUniquePtr: Value access via *");
        TEST_CHECK(FTestObject::GetAliveCount() == 1, "TUniquePtr: Object alive during scope");
    }
    TEST_CHECK(FTestObject::GetAliveCount() == 0, "TUniquePtr: Object destroyed after scope");

    // Test 2: Move semantics
    FTestObject::ResetCounters();
    {
        TUniquePtr<FTestObject> Ptr1 = MakeUnique<FTestObject>(100);
        TUniquePtr<FTestObject> Ptr2 = std::move(Ptr1);
        
        TEST_CHECK(!Ptr1.IsValid(), "TUniquePtr: Source invalid after move");
        TEST_CHECK(Ptr2.IsValid(), "TUniquePtr: Destination valid after move");
        TEST_CHECK(Ptr2->Value == 100, "TUniquePtr: Value preserved after move");
        TEST_CHECK(FTestObject::GetAliveCount() == 1, "TUniquePtr: Only one object exists after move");
    }

    // Test 3: Release
    FTestObject::ResetCounters();
    {
        TUniquePtr<FTestObject> Ptr = MakeUnique<FTestObject>(200);
        FTestObject* RawPtr = Ptr.Release();
        
        TEST_CHECK(!Ptr.IsValid(), "TUniquePtr: Invalid after Release");
        TEST_CHECK(RawPtr != nullptr, "TUniquePtr: Release returns valid pointer");
        TEST_CHECK(RawPtr->Value == 200, "TUniquePtr: Released pointer has correct value");
        TEST_CHECK(FTestObject::GetAliveCount() == 1, "TUniquePtr: Object still alive after Release");
        
        delete RawPtr; // Manual cleanup
    }
    TEST_CHECK(FTestObject::GetAliveCount() == 0, "TUniquePtr: Object destroyed after manual delete");

    // Test 4: Reset
    FTestObject::ResetCounters();
    {
        TUniquePtr<FTestObject> Ptr = MakeUnique<FTestObject>(300);
        Ptr.Reset(new FTestObject(400));
        
        TEST_CHECK(Ptr->Value == 400, "TUniquePtr: New value after Reset");
        TEST_CHECK(FTestObject::ConstructCount == 2, "TUniquePtr: Two constructions");
        TEST_CHECK(FTestObject::DestructCount == 1, "TUniquePtr: One destruction from Reset");
    }

    // Test 5: Array specialization
    {
        TUniquePtr<int32[]> ArrayPtr = MakeUnique<int32[]>(5);
        ArrayPtr[0] = 10;
        ArrayPtr[4] = 50;
        
        TEST_CHECK(ArrayPtr[0] == 10, "TUniquePtr<T[]>: Array access [0]");
        TEST_CHECK(ArrayPtr[4] == 50, "TUniquePtr<T[]>: Array access [4]");
    }

    // Test 6: Nullptr comparison
    {
        TUniquePtr<FTestObject> Ptr;
        TEST_CHECK(Ptr == nullptr, "TUniquePtr: Empty equals nullptr");
        TEST_CHECK(!Ptr, "TUniquePtr: Empty is falsy");
        
        Ptr = MakeUnique<FTestObject>();
        TEST_CHECK(Ptr != nullptr, "TUniquePtr: Valid not equals nullptr");
        TEST_CHECK(!!Ptr, "TUniquePtr: Valid is truthy");
    }
}

// ============================================================================
// TSharedPtr Tests
// ============================================================================

void TestTSharedPtr()
{
    TEST_SECTION("TSharedPtr Tests");
    FTestObject::ResetCounters();

    // Test 1: Basic construction
    {
        TSharedPtr<FTestObject> Ptr = MakeShared<FTestObject>(42);
        TEST_CHECK(Ptr.IsValid(), "TSharedPtr: IsValid after construction");
        TEST_CHECK(Ptr->Value == 42, "TSharedPtr: Value access");
        TEST_CHECK(Ptr.GetSharedReferenceCount() == 1, "TSharedPtr: RefCount is 1");
        TEST_CHECK(Ptr.IsUnique(), "TSharedPtr: IsUnique with single reference");
    }
    TEST_CHECK(FTestObject::GetAliveCount() == 0, "TSharedPtr: Object destroyed after scope");

    // Test 2: Copy semantics (reference counting)
    FTestObject::ResetCounters();
    {
        TSharedPtr<FTestObject> Ptr1 = MakeShared<FTestObject>(100);
        {
            TSharedPtr<FTestObject> Ptr2 = Ptr1;
            
            TEST_CHECK(Ptr1.Get() == Ptr2.Get(), "TSharedPtr: Same object after copy");
            TEST_CHECK(Ptr1.GetSharedReferenceCount() == 2, "TSharedPtr: RefCount is 2 after copy");
            TEST_CHECK(!Ptr1.IsUnique(), "TSharedPtr: Not unique with two references");
            TEST_CHECK(FTestObject::GetAliveCount() == 1, "TSharedPtr: Still one object");
        }
        TEST_CHECK(Ptr1.GetSharedReferenceCount() == 1, "TSharedPtr: RefCount back to 1");
        TEST_CHECK(FTestObject::GetAliveCount() == 1, "TSharedPtr: Object still alive");
    }
    TEST_CHECK(FTestObject::GetAliveCount() == 0, "TSharedPtr: Object destroyed when last ref gone");

    // Test 3: Move semantics
    FTestObject::ResetCounters();
    {
        TSharedPtr<FTestObject> Ptr1 = MakeShared<FTestObject>(200);
        TSharedPtr<FTestObject> Ptr2 = std::move(Ptr1);
        
        TEST_CHECK(!Ptr1.IsValid(), "TSharedPtr: Source invalid after move");
        TEST_CHECK(Ptr2.IsValid(), "TSharedPtr: Destination valid after move");
        TEST_CHECK(Ptr2->Value == 200, "TSharedPtr: Value preserved after move");
        TEST_CHECK(Ptr2.GetSharedReferenceCount() == 1, "TSharedPtr: RefCount still 1 after move");
    }

    // Test 4: Nullptr handling
    {
        TSharedPtr<FTestObject> Ptr;
        TEST_CHECK(!Ptr.IsValid(), "TSharedPtr: Default constructed is invalid");
        TEST_CHECK(Ptr == nullptr, "TSharedPtr: Default constructed equals nullptr");
        TEST_CHECK(Ptr.GetSharedReferenceCount() == 0, "TSharedPtr: RefCount is 0 for null");
    }

    // Test 5: Reset
    FTestObject::ResetCounters();
    {
        TSharedPtr<FTestObject> Ptr = MakeShared<FTestObject>(300);
        Ptr.Reset();
        
        TEST_CHECK(!Ptr.IsValid(), "TSharedPtr: Invalid after Reset");
        TEST_CHECK(FTestObject::GetAliveCount() == 0, "TSharedPtr: Object destroyed after Reset");
    }

    // Test 6: Polymorphism
    FTestObject::ResetCounters();
    {
        TSharedPtr<FTestObject> BasePtr = MakeShared<FDerivedTestObject>(50);
        TEST_CHECK(BasePtr->Value == 50, "TSharedPtr: Polymorphic access works");
        
        TSharedPtr<FDerivedTestObject> DerivedPtr = StaticCastSharedPtr<FDerivedTestObject>(BasePtr);
        TEST_CHECK(DerivedPtr->DerivedValue == 100, "TSharedPtr: Static cast works");
        TEST_CHECK(BasePtr.GetSharedReferenceCount() == 2, "TSharedPtr: Cast shares ownership");
    }
}

// ============================================================================
// TSharedRef Tests
// ============================================================================

void TestTSharedRef()
{
    TEST_SECTION("TSharedRef Tests");
    FTestObject::ResetCounters();

    // Test 1: Basic construction (must be non-null)
    {
        TSharedRef<FTestObject> Ref = MakeShared<FTestObject>(42);
        TEST_CHECK(Ref->Value == 42, "TSharedRef: Value access");
        TEST_CHECK(Ref.GetSharedReferenceCount() == 1, "TSharedRef: RefCount is 1");
    }
    TEST_CHECK(FTestObject::GetAliveCount() == 0, "TSharedRef: Object destroyed after scope");

    // Test 2: Copy semantics
    FTestObject::ResetCounters();
    {
        TSharedRef<FTestObject> Ref1 = MakeShared<FTestObject>(100);
        TSharedRef<FTestObject> Ref2 = Ref1;
        
        TEST_CHECK(&Ref1.Get() == &Ref2.Get(), "TSharedRef: Same object after copy");
        TEST_CHECK(Ref1.GetSharedReferenceCount() == 2, "TSharedRef: RefCount is 2");
    }

    // Test 3: Conversion to TSharedPtr
    FTestObject::ResetCounters();
    {
        TSharedRef<FTestObject> Ref = MakeShared<FTestObject>(200);
        TSharedPtr<FTestObject> Ptr = Ref.ToSharedPtr();
        
        TEST_CHECK(Ptr.IsValid(), "TSharedRef->TSharedPtr: Valid");
        TEST_CHECK(Ptr.Get() == &Ref.Get(), "TSharedRef->TSharedPtr: Same object");
        TEST_CHECK(Ref.GetSharedReferenceCount() == 2, "TSharedRef->TSharedPtr: Shares ownership");
    }

    // Test 4: Direct access (no null check needed)
    {
        TSharedRef<FTestObject> Ref = MakeShared<FTestObject>(300);
        FTestObject& DirectRef = Ref.Get();
        DirectRef.Value = 400;
        
        TEST_CHECK(Ref->Value == 400, "TSharedRef: Direct reference modification");
    }
}

// ============================================================================
// TWeakPtr Tests
// ============================================================================

void TestTWeakPtr()
{
    TEST_SECTION("TWeakPtr Tests");
    FTestObject::ResetCounters();

    // Test 1: Basic weak reference
    {
        TWeakPtr<FTestObject> WeakPtr;
        {
            TSharedPtr<FTestObject> SharedPtr = MakeShared<FTestObject>(42);
            WeakPtr = SharedPtr;
            
            TEST_CHECK(WeakPtr.IsValid(), "TWeakPtr: Valid while shared exists");
            
            TSharedPtr<FTestObject> Pinned = WeakPtr.Pin();
            TEST_CHECK(Pinned.IsValid(), "TWeakPtr: Pin succeeds while shared exists");
            TEST_CHECK(Pinned->Value == 42, "TWeakPtr: Pinned has correct value");
        }
        
        TEST_CHECK(!WeakPtr.IsValid(), "TWeakPtr: Invalid after shared destroyed");
        
        TSharedPtr<FTestObject> Pinned = WeakPtr.Pin();
        TEST_CHECK(!Pinned.IsValid(), "TWeakPtr: Pin fails after shared destroyed");
    }
    TEST_CHECK(FTestObject::GetAliveCount() == 0, "TWeakPtr: Object properly destroyed");

    // Test 2: Weak doesn't extend lifetime
    FTestObject::ResetCounters();
    {
        TWeakPtr<FTestObject> WeakPtr;
        {
            TSharedPtr<FTestObject> SharedPtr = MakeShared<FTestObject>(100);
            WeakPtr = SharedPtr;
            TEST_CHECK(FTestObject::GetAliveCount() == 1, "TWeakPtr: One object alive");
        }
        TEST_CHECK(FTestObject::GetAliveCount() == 0, "TWeakPtr: Weak doesn't prevent destruction");
    }

    // Test 3: Multiple weak references
    {
        TSharedPtr<FTestObject> SharedPtr = MakeShared<FTestObject>(200);
        TWeakPtr<FTestObject> Weak1 = SharedPtr;
        TWeakPtr<FTestObject> Weak2 = SharedPtr;
        TWeakPtr<FTestObject> Weak3 = Weak1;
        
        TEST_CHECK(Weak1.IsValid(), "TWeakPtr: Weak1 valid");
        TEST_CHECK(Weak2.IsValid(), "TWeakPtr: Weak2 valid");
        TEST_CHECK(Weak3.IsValid(), "TWeakPtr: Weak3 valid");
        TEST_CHECK(SharedPtr.GetSharedReferenceCount() == 1, "TWeakPtr: Weak refs don't increase shared count");
    }

    // Test 4: Reset
    {
        TSharedPtr<FTestObject> SharedPtr = MakeShared<FTestObject>(300);
        TWeakPtr<FTestObject> WeakPtr = SharedPtr;
        
        TEST_CHECK(WeakPtr.IsValid(), "TWeakPtr: Valid before reset");
        WeakPtr.Reset();
        TEST_CHECK(!WeakPtr.IsValid(), "TWeakPtr: Invalid after reset");
    }
}

// ============================================================================
// TSharedFromThis Tests
// ============================================================================

void TestTSharedFromThis()
{
    TEST_SECTION("TSharedFromThis Tests");

    // Test 1: AsShared from shared pointer
    {
        TSharedPtr<FSharedFromThisTest> Ptr = MakeShared<FSharedFromThisTest>(42);
        
        TSharedRef<FSharedFromThisTest> FromThis = Ptr->AsShared();
        TEST_CHECK(&FromThis.Get() == Ptr.Get(), "TSharedFromThis: AsShared returns same object");
        TEST_CHECK(Ptr.GetSharedReferenceCount() == 2, "TSharedFromThis: AsShared shares ownership");
    }

    // Test 2: AsWeak
    {
        TSharedPtr<FSharedFromThisTest> Ptr = MakeShared<FSharedFromThisTest>(100);
        
        TWeakPtr<FSharedFromThisTest> WeakFromThis = Ptr->AsWeak();
        TEST_CHECK(WeakFromThis.IsValid(), "TSharedFromThis: AsWeak returns valid weak ptr");
        TEST_CHECK(WeakFromThis.Pin().Get() == Ptr.Get(), "TSharedFromThis: AsWeak points to same object");
    }

    // Test 3: DoesSharedInstanceExist
    {
        TSharedPtr<FSharedFromThisTest> Ptr = MakeShared<FSharedFromThisTest>(200);
        TEST_CHECK(Ptr->DoesSharedInstanceExist(), "TSharedFromThis: DoesSharedInstanceExist returns true");
    }
}

// ============================================================================
// Thread Safety Tests
// ============================================================================

void TestThreadSafety()
{
    TEST_SECTION("Thread Safety Tests");
    FTestObject::ResetCounters();

    // Test 1: Concurrent reference counting (ThreadSafe mode)
    {
        TSharedPtr<FTestObject, ESPMode::ThreadSafe> SharedPtr = 
            MakeShared<FTestObject, ESPMode::ThreadSafe>(42);
        
        std::atomic<int32> SuccessCount{0};
        constexpr int32 NumThreads = 8;
        constexpr int32 IterationsPerThread = 1000;
        
        std::vector<std::thread> Threads;
        Threads.reserve(NumThreads);
        
        for (int32 i = 0; i < NumThreads; ++i)
        {
            Threads.emplace_back([&SharedPtr, &SuccessCount]()
            {
                for (int32 j = 0; j < IterationsPerThread; ++j)
                {
                    // Create and destroy copies rapidly
                    TSharedPtr<FTestObject, ESPMode::ThreadSafe> LocalCopy = SharedPtr;
                    if (LocalCopy.IsValid() && LocalCopy->Value == 42)
                    {
                        ++SuccessCount;
                    }
                }
            });
        }
        
        for (auto& Thread : Threads)
        {
            Thread.join();
        }
        
        TEST_CHECK(SuccessCount == NumThreads * IterationsPerThread, 
                   "ThreadSafe: All concurrent accesses succeeded");
        TEST_CHECK(SharedPtr.GetSharedReferenceCount() == 1, 
                   "ThreadSafe: RefCount correct after concurrent access");
    }
    TEST_CHECK(FTestObject::GetAliveCount() == 0, "ThreadSafe: Object properly destroyed");

    // Test 2: Concurrent weak pointer pinning
    FTestObject::ResetCounters();
    {
        TSharedPtr<FTestObject, ESPMode::ThreadSafe> SharedPtr = 
            MakeShared<FTestObject, ESPMode::ThreadSafe>(100);
        TWeakPtr<FTestObject, ESPMode::ThreadSafe> WeakPtr = SharedPtr;
        
        std::atomic<int32> PinSuccessCount{0};
        constexpr int32 NumThreads = 4;
        constexpr int32 IterationsPerThread = 500;
        
        std::vector<std::thread> Threads;
        Threads.reserve(NumThreads);
        
        for (int32 i = 0; i < NumThreads; ++i)
        {
            Threads.emplace_back([&WeakPtr, &PinSuccessCount]()
            {
                for (int32 j = 0; j < IterationsPerThread; ++j)
                {
                    TSharedPtr<FTestObject, ESPMode::ThreadSafe> Pinned = WeakPtr.Pin();
                    if (Pinned.IsValid())
                    {
                        ++PinSuccessCount;
                    }
                }
            });
        }
        
        for (auto& Thread : Threads)
        {
            Thread.join();
        }
        
        TEST_CHECK(PinSuccessCount == NumThreads * IterationsPerThread,
                   "ThreadSafe: All weak pointer pins succeeded");
    }
}

// ============================================================================
// Custom Deleter Tests
// ============================================================================

void TestCustomDeleters()
{
    TEST_SECTION("Custom Deleter Tests");

    // Test 1: TUniquePtr with custom deleter
    {
        static int32 CustomDeleteCount = 0;
        
        auto CustomDeleter = [](FTestObject* Ptr) {
            ++CustomDeleteCount;
            delete Ptr;
        };
        
        {
            TUniquePtr<FTestObject, decltype(CustomDeleter)> Ptr(new FTestObject(42), CustomDeleter);
            TEST_CHECK(Ptr->Value == 42, "CustomDeleter: TUniquePtr value access");
        }
        
        TEST_CHECK(CustomDeleteCount == 1, "CustomDeleter: TUniquePtr custom deleter called");
    }

    // Test 2: TSharedPtr with custom deleter via MakeShareable
    {
        static int32 CustomDeleteCount = 0;
        FTestObject::ResetCounters();
        
        {
            TSharedPtr<FTestObject> Ptr = MakeShareable(new FTestObject(100), 
                [](FTestObject* Obj) {
                    ++CustomDeleteCount;
                    delete Obj;
                });
            TEST_CHECK(Ptr->Value == 100, "CustomDeleter: TSharedPtr value access");
        }
        
        TEST_CHECK(CustomDeleteCount == 1, "CustomDeleter: TSharedPtr custom deleter called");
    }
}

} // namespace MonsterEngine

// ============================================================================
// Main Test Entry Point
// ============================================================================

void RunSmartPointerTests()
{
    using namespace MonsterEngine;

    printf("==========================================\n");
    printf("  Smart Pointer Tests\n");
    printf("==========================================\n");
    fflush(stdout);

    TestsPassed = 0;
    TestsFailed = 0;

    TestTUniquePtr();
    TestTSharedPtr();
    TestTSharedRef();
    TestTWeakPtr();
    TestTSharedFromThis();
    TestThreadSafety();
    TestCustomDeleters();

    printf("\n==========================================\n");
    printf("  Smart Pointer Tests Summary\n");
    printf("==========================================\n");
    printf("Passed: %d\n", TestsPassed);
    printf("Failed: %d\n", TestsFailed);
    printf("Total:  %d\n", TestsPassed + TestsFailed);
    printf("==========================================\n");
    fflush(stdout);
}
