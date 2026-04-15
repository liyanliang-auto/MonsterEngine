#include "RHI/RHIDefinitions.h"
#include "Core/Logging/Logging.h"
#include <cassert>

using namespace MonsterRender;
using namespace MonsterRender::RHI;

void TestSlotConversion()
{
    printf("=== Testing Slot Conversion ===\n");
    
    // Test GetGlobalBinding
    printf("Testing GetGlobalBinding():\n");
    assert(GetGlobalBinding(0, 0) == 0);
    printf("  ✓ GetGlobalBinding(0, 0) = 0\n");
    
    assert(GetGlobalBinding(1, 0) == 8);
    printf("  ✓ GetGlobalBinding(1, 0) = 8\n");
    
    assert(GetGlobalBinding(1, 1) == 9);
    printf("  ✓ GetGlobalBinding(1, 1) = 9\n");
    
    assert(GetGlobalBinding(1, 2) == 10);
    printf("  ✓ GetGlobalBinding(1, 2) = 10\n");
    
    assert(GetGlobalBinding(2, 1) == 17);
    printf("  ✓ GetGlobalBinding(2, 1) = 17\n");
    
    assert(GetGlobalBinding(2, 2) == 18);
    printf("  ✓ GetGlobalBinding(2, 2) = 18\n");
    
    assert(GetGlobalBinding(2, 3) == 19);
    printf("  ✓ GetGlobalBinding(2, 3) = 19\n");
    
    // Test GetSetAndBinding
    printf("\nTesting GetSetAndBinding():\n");
    uint32 set, binding;
    
    GetSetAndBinding(0, set, binding);
    assert(set == 0 && binding == 0);
    printf("  ✓ GetSetAndBinding(0) -> set=0, binding=0\n");
    
    GetSetAndBinding(8, set, binding);
    assert(set == 1 && binding == 0);
    printf("  ✓ GetSetAndBinding(8) -> set=1, binding=0\n");
    
    GetSetAndBinding(9, set, binding);
    assert(set == 1 && binding == 1);
    printf("  ✓ GetSetAndBinding(9) -> set=1, binding=1\n");
    
    GetSetAndBinding(10, set, binding);
    assert(set == 1 && binding == 2);
    printf("  ✓ GetSetAndBinding(10) -> set=1, binding=2\n");
    
    GetSetAndBinding(17, set, binding);
    assert(set == 2 && binding == 1);
    printf("  ✓ GetSetAndBinding(17) -> set=2, binding=1\n");
    
    GetSetAndBinding(18, set, binding);
    assert(set == 2 && binding == 2);
    printf("  ✓ GetSetAndBinding(18) -> set=2, binding=2\n");
    
    GetSetAndBinding(19, set, binding);
    assert(set == 2 && binding == 3);
    printf("  ✓ GetSetAndBinding(19) -> set=2, binding=3\n");
    
    printf("\n=== All Slot Conversion Tests PASSED ===\n");
}

void RunSlotConversionTests()
{
    TestSlotConversion();
}
