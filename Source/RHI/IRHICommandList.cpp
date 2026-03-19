// Copyright Monster Engine. All Rights Reserved.

#include "RHI/IRHICommandList.h"

namespace MonsterRender {
namespace RHI {

// Initialize static members
std::atomic<uint32> IRHICommandList::s_nextUID(1);

} // namespace RHI
} // namespace MonsterRender
