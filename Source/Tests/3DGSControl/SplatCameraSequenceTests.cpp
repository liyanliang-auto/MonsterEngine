#include "Renderer/Splat/SplatCameraSequence.h"
#include "Engine/Camera/FPSCameraController.h"
#include "SplatSceneApplication.h"
#include "imgui.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

using namespace MonsterRender::Splat;

int RunSplatCameraSequenceTests()
{
    using namespace MonsterEngine;
    {
        MonsterRender::SplatSceneApplication app;
        assert(app.IsControlUIVisible());
        app.SetControlUIVisible(false);
        assert(!app.IsControlUIVisible());
        app.onKeyPressed(MonsterRender::EKey::F1);
        assert(app.IsControlUIVisible());
        app.onKeyPressed(MonsterRender::EKey::F1);
        assert(!app.IsControlUIVisible());
        app.SetControlUIVisible(true);
        assert(app.IsControlUIVisible());
    }
    FFPSCameraController camera;
    camera.SetPosition(FVector(1.0, 2.0, 3.0));
    camera.SetRotation(80.0f, -10.0f, 15.0f);
    auto cameraSettings = camera.GetSettings();
    cameraSettings.MouseSensitivity = 0.4f;
    cameraSettings.MovementSpeed = 10.0f;
    cameraSettings.MinFOV = 30.0f;
    cameraSettings.MaxFOV = 120.0f;
    cameraSettings.bLocalVerticalMovement = true;
    camera.SetSettings(cameraSettings);
    camera.SetFOV(110.0f);
    assert(camera.GetFOV() == 110.0f);
    assert(camera.GetPosition().X == 1.0 && camera.GetRoll() == 15.0f);
    camera.ProcessMouseMovement(10.0f, 0.0f);
    assert(std::abs(camera.GetYaw() - 84.0f) < 0.001f);
    const FVector beforeMove = camera.GetPosition();
    camera.ProcessKeyboard(ECameraMovement::Forward, 0.5f);
    assert(std::abs((camera.GetPosition() - beforeMove).Size() - 5.0) < 0.001);
    camera.ProcessRoll(90.0f, 1.0f);
    assert(camera.GetRoll() == 105.0f);
    camera.ProcessMouseScroll(100.0f);
    assert(camera.GetFOV() == 30.0f);
    camera.ProcessMouseScroll(-100.0f);
    assert(camera.GetFOV() == 120.0f);

    FSplatControlState invalid;
    invalid.FOV=200;
    invalid.NearPlane=-1;
    invalid.GaussianScale=0;
    assert(!invalid.IsValid());
    invalid.ClampToValid();
    assert(invalid.IsValid());
    invalid.MouseSensitivity = 10.0f;
    invalid.MovementSpeed = -2.0f;
    invalid.NearPlane = 2000.0f;
    invalid.FarPlane = 1.0f;
    invalid.ClampToValid();
    assert(invalid.IsValid());
    assert(invalid.MouseSensitivity == 0.4f && invalid.MovementSpeed == 1.0f);
    assert(invalid.FarPlane > invalid.NearPlane);

    FSplatCameraSequence sequence;
    assert(!sequence.Play());
    FSplatCameraKeyframe first, second, third;
    first.TimeSeconds=0; first.State.Yaw=170; first.State.Roll=170;
    second.TimeSeconds=1; second.State.Yaw=-170; second.State.Roll=-170;
    second.State.PositionX=10; second.State.FOV=90; second.State.bWireframe=true;
    second.State.NearPlane=0.5f; second.State.FarPlane=500.0f;
    second.State.GaussianScale=2.0f; second.State.bDistanceCulling=false;
    third.TimeSeconds=3; third.State.PositionX=20; third.State.FOV=100;
    assert(sequence.AddOrReplace(first));
    assert(sequence.AddOrReplace(second));
    assert(sequence.AddOrReplace(third));
    assert(sequence.Sample(-1).Yaw==170);
    assert(sequence.Sample(3).PositionX==20);
    const auto midpoint=sequence.Sample(0.5);
    assert(std::isfinite(midpoint.PositionX) && midpoint.PositionX>0 && midpoint.PositionX<10);
    assert(std::abs(midpoint.Yaw)>170);
    assert(!midpoint.bWireframe);
    assert(midpoint.bDistanceCulling);
    assert(!sequence.Sample(1).bDistanceCulling);
    assert(midpoint.NearPlane > 0.1f && midpoint.NearPlane < 0.5f);
    assert(midpoint.FarPlane > 500.0f && midpoint.FarPlane < 1000.0f);
    assert(midpoint.GaussianScale > 1.0f && midpoint.GaussianScale < 2.0f);
    assert(sequence.Sample(1).bWireframe);
    assert(midpoint.FOV>45 && midpoint.FOV<90);

    std::string error;
    assert(!sequence.SetKeyframeTime(2,1,&error));
    assert(sequence.GetKeyframes()[2].TimeSeconds==3);
    assert(sequence.Play());
    FSplatCameraKeyframe replacement=second;
    replacement.State.PositionX=100;
    assert(sequence.AddOrReplace(replacement));
    assert(sequence.Sample(1).PositionX==10); // active playback snapshot is stable
    sequence.Tick(5);
    assert(!sequence.IsPlaying() && sequence.GetPlaybackTime()==3);
    sequence.SetLooping(true);
    assert(sequence.Play());
    sequence.Tick(4);
    assert(sequence.IsPlaying() && sequence.GetPlaybackTime()==1);
    sequence.Stop();
    assert(sequence.GetPlaybackTime()==0);
    assert(sequence.Sample(1).PositionX==100);

    const std::string path="Source/Tests/3DGSControl/validation/sequence_roundtrip_tmp.json";
    assert(sequence.SaveJson(path,&error));
    FSplatCameraSequence loaded;
    assert(loaded.LoadJson(path,&error));
    assert(loaded.GetKeyframes().size()==3);
    assert(loaded.Sample(1).IsValid());
    assert(loaded.Sample(1).bWireframe);
    loaded.Seek(1.5);
    assert(loaded.GetPlaybackTime()==1.5);
    assert(loaded.Play());
    loaded.Pause();
    assert(!loaded.IsPlaying() && loaded.GetPlaybackTime()==1.5);
    loaded.Stop();
    assert(loaded.SetKeyframeTime(1,1.25,&error));
    assert(loaded.GetKeyframes()[1].TimeSeconds==1.25);
    {
        std::ofstream bad(path,std::ios::trunc);
        bad << "{\"version\":1,\"timeUnit\":\"seconds\",\"keyframes\":[{\"time\":-1}]}";
    }
    assert(!loaded.LoadJson(path,&error));
    assert(loaded.GetKeyframes().size()==3);
    assert(loaded.Remove(1) && loaded.GetKeyframes().size()==2);
    assert(!loaded.Remove(99));
    std::remove(path.c_str());
    FSplatCameraSequence demo;
    assert(demo.LoadJson("resources/3dgs_sequences/bonsai_demo.json", &error));
    assert(demo.GetKeyframes().size()==4 && demo.GetDuration()==6.0);
    assert(demo.Sample(4.0).bWireframe && !demo.Sample(4.0).bDistanceCulling);
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    ImFont* font = io.Fonts->AddFontFromFileTTF(
        "C:/Windows/Fonts/msyh.ttc", 16.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull());
    assert(font && io.Fonts->Build());
    std::ifstream uiSource("Source/SplatSceneApplication.cpp", std::ios::binary);
    assert(uiSource);
    const std::string uiText((std::istreambuf_iterator<char>(uiSource)), {});
    for (size_t i = 0; i + 2 < uiText.size(); ++i)
    {
        const unsigned char a = static_cast<unsigned char>(uiText[i]);
        if ((a & 0xF0u) != 0xE0u) continue;
        const unsigned char b = static_cast<unsigned char>(uiText[i + 1]);
        const unsigned char c = static_cast<unsigned char>(uiText[i + 2]);
        if ((b & 0xC0u) != 0x80u || (c & 0xC0u) != 0x80u) continue;
        const unsigned codepoint = ((a & 0x0Fu) << 12) | ((b & 0x3Fu) << 6) | (c & 0x3Fu);
        if (codepoint >= 0x4E00u && codepoint <= 0x9FFFu)
            assert(font->IsGlyphInFont(static_cast<ImWchar>(codepoint)));
        i += 2;
    }
    ImGui::DestroyContext();
    std::puts("3DGS control, camera, sequence, JSON tests passed");
    return 0;
}

#ifdef SPLAT_CONTROL_STANDALONE_TEST
int main() { return RunSplatCameraSequenceTests(); }
#endif
