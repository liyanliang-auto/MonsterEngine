// Copyright Monster Engine. All Rights Reserved.
#pragma once

#include "Renderer/Splat/SplatControlState.h"
#include <cstddef>
#include <string>
#include <vector>

namespace MonsterRender::Splat
{

struct FSplatCameraKeyframe
{
    double TimeSeconds = 0.0;
    FSplatControlState State;
};

/** Main-thread-owned sequence. Sample() never mutates playback state. */
class FSplatCameraSequence
{
public:
    enum class EPositionInterpolation { Smooth, Linear };

    bool AddOrReplace(const FSplatCameraKeyframe& keyframe, std::string* error = nullptr);
    bool Remove(size_t index);
    bool SetKeyframeTime(size_t index, double timeSeconds, std::string* error = nullptr);
    void Clear();
    const std::vector<FSplatCameraKeyframe>& GetKeyframes() const { return m_keyframes; }
    FSplatControlState Sample(double timeSeconds) const;

    bool Play();
    void Pause() { m_playing = false; }
    void Stop();
    void Seek(double timeSeconds);
    void Tick(double deltaSeconds);
    bool IsPlaying() const { return m_playing; }
    void SetLooping(bool looping) { m_looping = looping; }
    bool IsLooping() const { return m_looping; }
    double GetPlaybackTime() const { return m_currentTime; }
    double GetDuration() const { const auto& keys = ActiveKeyframes(); return keys.empty() ? 0.0 : keys.back().TimeSeconds; }
    void SetPositionInterpolation(EPositionInterpolation mode) { m_interpolation = mode; }
    EPositionInterpolation GetPositionInterpolation() const { return m_interpolation; }

    bool SaveJson(const std::string& path, std::string* error = nullptr) const;
    bool LoadJson(const std::string& path, std::string* error = nullptr);

private:
    const std::vector<FSplatCameraKeyframe>& ActiveKeyframes() const { return m_playbackKeyframes.empty() ? m_keyframes : m_playbackKeyframes; }
    std::vector<FSplatCameraKeyframe> m_keyframes;
    std::vector<FSplatCameraKeyframe> m_playbackKeyframes;
    double m_currentTime = 0.0;
    bool m_playing = false;
    bool m_looping = false;
    EPositionInterpolation m_interpolation = EPositionInterpolation::Smooth;
};

} // namespace MonsterRender::Splat
