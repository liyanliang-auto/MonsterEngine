#pragma once
#include "Core/CoreTypes.h"
#include "RHI/SplatGPUPrimitives.h"

namespace MonsterRender::RHI {
/** Observable backend outcomes; Timeout and NotReady never authorize rendering. */
enum class EFrameResult : uint8 { Success, NotReady, Timeout, OutOfDate, Suboptimal, SurfaceLost, DeviceLost, Error };
/** One transaction; token exists only for a new successful native submission. */
struct FFrameSubmitResult {
    EFrameResult status = EFrameResult::NotReady;
    EFrameResult acquireStatus = EFrameResult::NotReady;
    EFrameResult submitStatus = EFrameResult::NotReady;
    EFrameResult presentStatus = EFrameResult::NotReady;
    int32 acquireResult = 0;
    int32 submitResult = 0;
    int32 presentResult = 0;
    bool acquired = false;
    bool submitted = false;
    FGPUCompletionToken token{};
    uint64 surfaceGeneration = 0;
};
}
