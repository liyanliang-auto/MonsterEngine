// Copyright Monster Engine. All Rights Reserved.

/**
 * @file ColorAndContainerTest.cpp
 * @brief Test file for FLinearColor, FColor, FText, TQueue, TDeque, TCircularBuffer
 */

#include "Core/Color.h"
#include "Containers/Text.h"
#include "Containers/Queue.h"
#include "Containers/Deque.h"
#include "Containers/CircularBuffer.h"
#include "Containers/Array.h"
#include "Core/Logging/Logging.h"

#include <iostream>
#include <cassert>

// Use MonsterRender logging types
namespace ELogVerbosity = MonsterRender::ELogVerbosity;
using MonsterRender::LogCore;

namespace MonsterEngine
{
namespace Tests
{

// ============================================================================
// Color Tests
// ============================================================================

void TestFLinearColor()
{
    MR_LOG(LogCore, Log, "=== Testing FLinearColor ===");
    
    // Test constructors
    FLinearColor Default;
    FLinearColor Red(1.0f, 0.0f, 0.0f, 1.0f);
    FLinearColor Green = FLinearColor::Green;
    
    // Test operators
    FLinearColor Yellow = Red + Green;
    assert(Yellow.R == 1.0f && Yellow.G == 1.0f && Yellow.B == 0.0f);
    MR_LOG(LogCore, Log, "  FLinearColor addition: PASSED");
    
    FLinearColor Scaled = Red * 0.5f;
    assert(Scaled.R == 0.5f);
    MR_LOG(LogCore, Log, "  FLinearColor scalar multiplication: PASSED");
    
    // Test color operations
    float Luminance = FLinearColor(0.5f, 0.5f, 0.5f, 1.0f).GetLuminance();
    assert(Luminance > 0.0f && Luminance < 1.0f);
    MR_LOG(LogCore, Log, "  FLinearColor luminance: PASSED");
    
    // Test HSV conversion
    FLinearColor HSV = Red.LinearRGBToHSV();
    FLinearColor BackToRGB = HSV.HSVToLinearRGB();
    assert(BackToRGB.Equals(Red, 0.01f));
    MR_LOG(LogCore, Log, "  FLinearColor HSV conversion: PASSED");
    
    // Test lerp
    FLinearColor Lerped = FLinearColor::Lerp(Red, Green, 0.5f);
    assert(Lerped.R == 0.5f && Lerped.G == 0.5f);
    MR_LOG(LogCore, Log, "  FLinearColor lerp: PASSED");
    
    // Test color temperature
    FLinearColor Warm = FLinearColor::MakeFromColorTemperature(3000.0f);
    FLinearColor Cool = FLinearColor::MakeFromColorTemperature(10000.0f);
    assert(Warm.R > Warm.B);  // Warm should be more red
    assert(Cool.B > Cool.R);  // Cool should be more blue
    MR_LOG(LogCore, Log, "  FLinearColor color temperature: PASSED");
    
    MR_LOG(LogCore, Log, "=== FLinearColor Tests PASSED ===");
}

void TestFColor()
{
    MR_LOG(LogCore, Log, "=== Testing FColor ===");
    
    // Test constructors
    FColor White(255, 255, 255, 255);
    FColor Red(255, 0, 0, 255);
    FColor FromHex = FColor::FromHex("#FF0000");
    
    assert(FromHex.R == 255 && FromHex.G == 0 && FromHex.B == 0);
    MR_LOG(LogCore, Log, "  FColor FromHex: PASSED");
    
    // Test packed formats
    uint32_t ARGB = Red.ToPackedARGB();
    uint32_t RGBA = Red.ToPackedRGBA();
    assert(ARGB != RGBA);  // Different byte orders
    MR_LOG(LogCore, Log, "  FColor packed formats: PASSED");
    
    // Test conversion to linear
    FLinearColor Linear = Red.ReinterpretAsLinear();
    assert(Linear.R == 1.0f && Linear.G == 0.0f && Linear.B == 0.0f);
    MR_LOG(LogCore, Log, "  FColor to linear: PASSED");
    
    // Test hex string
    std::string Hex = White.ToHex();
    assert(Hex == "FFFFFFFF");
    MR_LOG(LogCore, Log, "  FColor ToHex: PASSED");
    
    // Test static colors
    assert(FColor::Red.R == 255 && FColor::Red.G == 0);
    assert(FColor::Green.G == 255 && FColor::Green.R == 0);
    assert(FColor::Blue.B == 255 && FColor::Blue.R == 0);
    MR_LOG(LogCore, Log, "  FColor static colors: PASSED");
    
    MR_LOG(LogCore, Log, "=== FColor Tests PASSED ===");
}

// ============================================================================
// Text Tests
// ============================================================================

void TestFText()
{
    MR_LOG(LogCore, Log, "=== Testing FText ===");
    
    // Test empty text
    FText Empty = FText::GetEmpty();
    assert(Empty.IsEmpty());
    MR_LOG(LogCore, Log, "  FText empty: PASSED");
    
    // Test from string
    FText Hello = FText::FromString(L"Hello, World!");
    assert(!Hello.IsEmpty());
    assert(Hello.ToString() == L"Hello, World!");
    MR_LOG(LogCore, Log, "  FText FromString: PASSED");
    
    // Test culture invariant
    FText Invariant = FText::AsCultureInvariant(L"Invariant Text");
    assert(Invariant.IsCultureInvariant());
    MR_LOG(LogCore, Log, "  FText culture invariant: PASSED");
    
    // Test formatting with ordered arguments
    FText Pattern = FText::FromString(L"Hello {0}, welcome to {1}!");
    std::vector<FText> Args = {
        FText::FromString(L"User"),
        FText::FromString(L"MonsterEngine")
    };
    FText Formatted = FText::FormatOrdered(Pattern, Args);
    assert(Formatted.ToString() == L"Hello User, welcome to MonsterEngine!");
    MR_LOG(LogCore, Log, "  FText FormatOrdered: PASSED");
    
    // Test number formatting
    FText Number = FText::AsNumber(12345);
    assert(!Number.IsEmpty());
    MR_LOG(LogCore, Log, "  FText AsNumber: PASSED");
    
    // Test percent formatting
    FText Percent = FText::AsPercent(0.75);
    assert(Percent.ToString().find(L"%") != std::wstring::npos);
    MR_LOG(LogCore, Log, "  FText AsPercent: PASSED");
    
    // Test memory formatting
    FText Memory = FText::AsMemory(1024 * 1024 * 100);  // 100 MB
    assert(Memory.ToString().find(L"MiB") != std::wstring::npos ||
           Memory.ToString().find(L"MB") != std::wstring::npos);
    MR_LOG(LogCore, Log, "  FText AsMemory: PASSED");
    
    // Test case conversion
    FText Upper = Hello.ToUpper();
    FText Lower = Hello.ToLower();
    assert(Upper.ToString() != Lower.ToString());
    MR_LOG(LogCore, Log, "  FText case conversion: PASSED");
    
    // Test comparison
    FText A = FText::FromString(L"Alpha");
    FText B = FText::FromString(L"Beta");
    assert(A.CompareTo(B) < 0);
    MR_LOG(LogCore, Log, "  FText comparison: PASSED");
    
    MR_LOG(LogCore, Log, "=== FText Tests PASSED ===");
}

// ============================================================================
// Queue Tests
// ============================================================================

void TestTQueue()
{
    MR_LOG(LogCore, Log, "=== Testing TQueue ===");
    
    // Test SPSC queue
    TQueue<int> Queue;
    
    // Test enqueue
    assert(Queue.Enqueue(1));
    assert(Queue.Enqueue(2));
    assert(Queue.Enqueue(3));
    MR_LOG(LogCore, Log, "  TQueue enqueue: PASSED");
    
    // Test dequeue
    int Value;
    assert(Queue.Dequeue(Value) && Value == 1);
    assert(Queue.Dequeue(Value) && Value == 2);
    assert(Queue.Dequeue(Value) && Value == 3);
    MR_LOG(LogCore, Log, "  TQueue dequeue: PASSED");
    
    // Test empty
    assert(Queue.IsEmpty());
    assert(!Queue.Dequeue(Value));
    MR_LOG(LogCore, Log, "  TQueue empty: PASSED");
    
    // Test peek
    Queue.Enqueue(42);
    assert(Queue.Peek(Value) && Value == 42);
    assert(!Queue.IsEmpty());  // Peek doesn't remove
    MR_LOG(LogCore, Log, "  TQueue peek: PASSED");
    
    // Test pop
    assert(Queue.Pop());
    assert(Queue.IsEmpty());
    MR_LOG(LogCore, Log, "  TQueue pop: PASSED");
    
    // Test MPSC queue
    TMpscQueue<std::string> MpscQueue;
    MpscQueue.Enqueue("Hello");
    MpscQueue.Enqueue("World");
    std::string Str;
    assert(MpscQueue.Dequeue(Str) && Str == "Hello");
    MR_LOG(LogCore, Log, "  TMpscQueue: PASSED");
    
    MR_LOG(LogCore, Log, "=== TQueue Tests PASSED ===");
}

// ============================================================================
// Deque Tests
// ============================================================================

void TestTDeque()
{
    MR_LOG(LogCore, Log, "=== Testing TDeque ===");
    
    // Test construction
    TDeque<int> Deque;
    assert(Deque.IsEmpty());
    MR_LOG(LogCore, Log, "  TDeque construction: PASSED");
    
    // Test push back
    Deque.PushBack(1);
    Deque.PushBack(2);
    Deque.PushBack(3);
    assert(Deque.Num() == 3);
    assert(Deque.First() == 1);
    assert(Deque.Last() == 3);
    MR_LOG(LogCore, Log, "  TDeque PushBack: PASSED");
    
    // Test push front
    Deque.PushFront(0);
    assert(Deque.Num() == 4);
    assert(Deque.First() == 0);
    MR_LOG(LogCore, Log, "  TDeque PushFront: PASSED");
    
    // Test index access
    assert(Deque[0] == 0);
    assert(Deque[1] == 1);
    assert(Deque[2] == 2);
    assert(Deque[3] == 3);
    MR_LOG(LogCore, Log, "  TDeque index access: PASSED");
    
    // Test pop back
    Deque.PopBack();
    assert(Deque.Num() == 3);
    assert(Deque.Last() == 2);
    MR_LOG(LogCore, Log, "  TDeque PopBack: PASSED");
    
    // Test pop front
    Deque.PopFront();
    assert(Deque.Num() == 2);
    assert(Deque.First() == 1);
    MR_LOG(LogCore, Log, "  TDeque PopFront: PASSED");
    
    // Test pop value
    int Value = Deque.PopFrontValue();
    assert(Value == 1);
    assert(Deque.Num() == 1);
    MR_LOG(LogCore, Log, "  TDeque PopFrontValue: PASSED");
    
    // Test initializer list
    TDeque<int> InitList = {10, 20, 30, 40, 50};
    assert(InitList.Num() == 5);
    assert(InitList[2] == 30);
    MR_LOG(LogCore, Log, "  TDeque initializer list: PASSED");
    
    // Test iteration
    int Sum = 0;
    for (int Val : InitList)
    {
        Sum += Val;
    }
    assert(Sum == 150);
    MR_LOG(LogCore, Log, "  TDeque iteration: PASSED");
    
    // Test clear
    InitList.Clear();
    assert(InitList.IsEmpty());
    MR_LOG(LogCore, Log, "  TDeque clear: PASSED");
    
    MR_LOG(LogCore, Log, "=== TDeque Tests PASSED ===");
}

// ============================================================================
// Circular Buffer Tests
// ============================================================================

void TestTCircularBuffer()
{
    MR_LOG(LogCore, Log, "=== Testing TCircularBuffer ===");
    
    // Test construction (capacity rounds up to power of 2)
    TCircularBuffer<int> Buffer(10);
    assert(Buffer.GetCapacity() == 16);  // Rounded up from 10
    MR_LOG(LogCore, Log, "  TCircularBuffer capacity rounding: PASSED");
    
    // Test index wrapping
    Buffer[0] = 100;
    Buffer[15] = 200;
    Buffer[16] = 300;  // Should wrap to index 0
    assert(Buffer[0] == 300);  // Wrapped
    assert(Buffer[16] == 300);  // Same as [0]
    MR_LOG(LogCore, Log, "  TCircularBuffer index wrapping: PASSED");
    
    // Test next/previous index
    assert(Buffer.GetNextIndex(15) == 0);  // Wraps
    assert(Buffer.GetPreviousIndex(0) == 15);  // Wraps
    MR_LOG(LogCore, Log, "  TCircularBuffer next/previous: PASSED");
    
    // Test with initial value
    TCircularBuffer<float> FloatBuffer(8, 1.0f);
    assert(FloatBuffer.GetCapacity() == 8);
    assert(FloatBuffer[0] == 1.0f);
    assert(FloatBuffer[7] == 1.0f);
    MR_LOG(LogCore, Log, "  TCircularBuffer initial value: PASSED");
    
    MR_LOG(LogCore, Log, "=== TCircularBuffer Tests PASSED ===");
}

void TestTCircularQueue()
{
    MR_LOG(LogCore, Log, "=== Testing TCircularQueue ===");
    
    // Test construction
    TCircularQueue<int> Queue(4);
    assert(Queue.IsEmpty());
    assert(Queue.Max() >= 4);
    MR_LOG(LogCore, Log, "  TCircularQueue construction: PASSED");
    
    // Test enqueue
    assert(Queue.Enqueue(1));
    assert(Queue.Enqueue(2));
    assert(Queue.Enqueue(3));
    assert(Queue.Num() == 3);
    MR_LOG(LogCore, Log, "  TCircularQueue enqueue: PASSED");
    
    // Test dequeue
    int Value;
    assert(Queue.Dequeue(Value) && Value == 1);
    assert(Queue.Dequeue(Value) && Value == 2);
    assert(Queue.Num() == 1);
    MR_LOG(LogCore, Log, "  TCircularQueue dequeue: PASSED");
    
    // Test peek
    assert(Queue.Peek(Value) && Value == 3);
    assert(Queue.Num() == 1);  // Peek doesn't remove
    MR_LOG(LogCore, Log, "  TCircularQueue peek: PASSED");
    
    // Test empty
    Queue.Empty();
    assert(Queue.IsEmpty());
    MR_LOG(LogCore, Log, "  TCircularQueue empty: PASSED");
    
    MR_LOG(LogCore, Log, "=== TCircularQueue Tests PASSED ===");
}

// ============================================================================
// Main Test Runner
// ============================================================================

void RunColorAndContainerTests()
{
    MR_LOG(LogCore, Log, "");
    MR_LOG(LogCore, Log, "========================================");
    MR_LOG(LogCore, Log, "  Color and Container Tests");
    MR_LOG(LogCore, Log, "========================================");
    MR_LOG(LogCore, Log, "");
    
    TestFLinearColor();
    TestFColor();
    TestFText();
    TestTQueue();
    TestTDeque();
    TestTCircularBuffer();
    TestTCircularQueue();
    
    MR_LOG(LogCore, Log, "");
    MR_LOG(LogCore, Log, "========================================");
    MR_LOG(LogCore, Log, "  ALL TESTS PASSED!");
    MR_LOG(LogCore, Log, "========================================");
    MR_LOG(LogCore, Log, "");
}

} // namespace Tests
} // namespace MonsterEngine
