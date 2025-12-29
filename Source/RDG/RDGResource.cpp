#include "RDG/RDGResource.h"
#include "Core/Logging/LogMacros.h"

// Use global log category (defined in LogCategories.cpp)
using MonsterRender::LogRDG;

namespace MonsterRender {
namespace RDG {

#if RDG_ENABLE_DEBUG
void FRDGResource::validateRHIAccess() const
{
    if (!m_bAllowRHIAccess)
    {
        MR_LOG(LogRDG, Error, 
               "Attempting to access RHI resource '%s' outside of pass execution. "
               "RHI resources are only valid during pass execution.",
               *m_name);
    }
}
#endif

}} // namespace MonsterRender::RDG
