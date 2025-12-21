#include "RDG/RDGResource.h"
#include "Core/Logging/LogMacros.h"

DECLARE_LOG_CATEGORY_EXTERN(LogRDG, Log, All);
DEFINE_LOG_CATEGORY(LogRDG);

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
