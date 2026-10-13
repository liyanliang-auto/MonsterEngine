// Copyright Monster Engine. All Rights Reserved.

#include "RHI/IRHICommandList.h"

namespace MonsterRender {
namespace RHI {

// Initialize static members
std::atomic<uint32> IRHICommandList::s_nextUID(1);
void IRHICommandList::dispatchIndirect(IRHIBuffer*, uint64_t) {
    MR_LOG_ERROR("dispatchIndirect unavailable on this command list backend");
}

} // namespace RHI
} // namespace MonsterRender
