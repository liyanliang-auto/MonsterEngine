// Focused tests linked against the actual Release engine object files.
#include "Core/CoreMinimal.h"
#include "Core/HAL/FMemoryManager.h"
#include "Core/Application.h"
#include "Engine/Asset/GLTFTypes.h"
#include "CubeSceneApplication.h"
#include <Windows.h>
#include "cgltf/cgltf.h"
#include <fstream>
#include <stdexcept>
#include <thread>
#include <future>
#include <iostream>
#define private public
#include "Engine/Asset/GLTFLoader.h"
#undef private
#include "../../Source/Engine/Asset/GLTFLoader.cpp"
extern "C" {
void stbi_set_flip_vertically_on_load(int);
void stbi_set_flip_vertically_on_load_thread(int);
unsigned char* stbi_load_from_memory(const unsigned char*, int, int*, int*, int*, int);
void stbi_image_free(void*);
}
using namespace MonsterRender;
using namespace MonsterEngine;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
const unsigned char imageBytes[] = {'P','6','\n','1',' ','2','\n','2','5','5','\n',255,0,0,0,0,255};
void decoderCase(bool threadOverride) {
    if(threadOverride) stbi_set_flip_vertically_on_load_thread(1);
    FGLTFLoader loader; FGLTFLoadOptions options;
    cgltf_buffer buffer{}; buffer.data=const_cast<unsigned char*>(imageBytes);
    cgltf_buffer_view view{}; view.buffer=&buffer; view.size=sizeof(imageBytes);
    cgltf_image embedded{}; embedded.buffer_view=&view;
    cgltf_image file{}; file.uri=const_cast<char*>("orientation.ppm");
    for(cgltf_image* input : {&embedded, &file}) {
        FGLTFImage decoded;
        check(loader._loadImageData(input,"specs/20261014-183652-43pk/",options,decoded),"real loader decode failed");
        check(decoded.Width==1 && decoded.Height==2 && decoded.Channels==4,"image dimensions");
        check(decoded.Data[0]==255 && decoded.Data[2]==0 && decoded.Data[4]==0 && decoded.Data[6]==255,"glTF must decode red top and blue bottom");
        int w,h,c; auto* caller=stbi_load_from_memory(imageBytes,sizeof(imageBytes),&w,&h,&c,4);
        check(caller && caller[0]==0 && caller[2]==255,"loader changed caller's enabled flip state");
        stbi_image_free(caller);
    }
    cgltf_image empty{}; FGLTFImage decoded;
    check(!loader._loadImageData(nullptr,"",options,decoded),"null image accepted");
    check(!loader._loadImageData(&empty,"",options,decoded),"empty image accepted");
}
void tangentCases() {
    for(float uvY : {-0.00001f, 0.00001f, 0.0f}) {
        FGLTFPrimitive p;
        p.Positions.Add(Math::FVector3f(0,0,0)); p.Positions.Add(Math::FVector3f(1,0,0)); p.Positions.Add(Math::FVector3f(0,1,0));
        for(int i=0;i<3;++i) p.Normals.Add(Math::FVector3f(0,0,1));
        p.TexCoords0.Add(Math::FVector2f(0,0)); p.TexCoords0.Add(Math::FVector2f(1,0)); p.TexCoords0.Add(Math::FVector2f(0,uvY));
        p.Indices.Add(0);p.Indices.Add(1);p.Indices.Add(2);
        FGLTFLoader loader; loader._generateTangents(p);
        for(const auto& t:p.Tangents) {
            check(std::isfinite(t.X)&&std::isfinite(t.Y)&&std::isfinite(t.Z)&&std::isfinite(t.W),"degenerate basis is nonfinite");
            if(uvY!=0) check(t.X>0.99f && std::fabs(t.Y)<0.001f && t.W==(uvY<0?-1.0f:1.0f),"small UV determinant reversed tangent or handedness");
            else check(t.X==0 && t.Y==0 && t.Z==0,"degenerate UV must not invent a basis");
        }
    }
}
struct TestWindow: IWindow {
    String title;
    bool initialize(const WindowProperties&) override{return true;} void shutdown() override{} bool shouldClose()const override{return false;}
    void pollEvents()override{} void swapBuffers()override{} void setTitle(const String& t)override{title=t;} void setSize(uint32,uint32)override{} void setPosition(int32,int32)override{} void setFullscreen(bool)override{} void setVSync(bool)override{}
    WindowProperties getProperties()const override{return {};} uint32 getWidth()const override{return 1;} uint32 getHeight()const override{return 1;} String getTitle()const override{return title;}
    bool isFullscreen()const override{return false;} bool isMinimized()const override{return false;} bool isMaximized()const override{return false;} bool hasFocus()const override{return true;}
    void* getNativeHandle()const override{return nullptr;} void* getNativeDisplayHandle()const override{return nullptr;} IInputManager* getInputManager()const override{return nullptr;}
};
struct TestApplication: Application {
    TestApplication(){m_window=MakeUnique<TestWindow>();}
    using Application::tick;
    using Application::m_fps; using Application::m_frameCount; using Application::m_fpsSampleStart; using Application::m_deltaTime; using Application::m_lastFrameTime; using Application::m_minimized;
    int renderMillis=0; int rendered=0;
    void onRender()override{++rendered;std::this_thread::sleep_for(std::chrono::milliseconds(renderMillis));}
};
void fpsCases() {
    TestApplication app; auto now=std::chrono::steady_clock::now();
    app.m_fpsSampleStart=now-std::chrono::milliseconds(1500);app.m_lastFrameTime=now-std::chrono::seconds(3);app.m_frameCount=29;app.renderMillis=350;
    app.tick(); check(app.m_fps>=15 && app.m_fps<=17,"FPS ignored raw time or completed render cost");check(app.m_deltaTime<=0.100001f,"simulation clamp removed");check(app.rendered==1,"completed frame not rendered");
    check(app.getWindow()->getTitle().find("FPS:")!=String::npos,"dynamic title not updated");
    app.renderMillis=0;app.m_fps=77;app.m_fpsSampleStart=std::chrono::steady_clock::now()+std::chrono::seconds(10);app.tick();check(app.m_fps==77,"negative time published FPS");
    app.m_fpsSampleStart=std::chrono::steady_clock::now();app.tick();check(app.m_fps==77,"subsecond interval published FPS");
    app.m_minimized=true;app.m_frameCount=99;const auto before=std::chrono::steady_clock::now();app.tick();check(app.m_frameCount==0 && app.m_fpsSampleStart>=before,"minimized interval not excluded");
}
struct NativeCaptureApp: CubeSceneApplication {
    std::chrono::steady_clock::time_point begin;
    std::chrono::steady_clock::time_point frameDeadline;
    bool captured=false;
    void onInitialize()override{CubeSceneApplication::onInitialize();begin=std::chrono::steady_clock::now();frameDeadline=begin;}
    void onRender()override{
        CubeSceneApplication::onRender();
        // Test host cadence only: exercise actual raw-clock FPS with frame waits.
        frameDeadline+=std::chrono::nanoseconds(16666667);
        std::this_thread::sleep_until(frameDeadline);
        if(std::chrono::steady_clock::now()-frameDeadline>std::chrono::milliseconds(100))frameDeadline=std::chrono::steady_clock::now();
    }
    void onUpdate(float32 delta)override{
        CubeSceneApplication::onUpdate(delta);
        if(captured || std::chrono::steady_clock::now()-begin<std::chrono::seconds(3))return;
        HWND window=static_cast<HWND>(getWindow()->getNativeHandle());RECT rect{};GetWindowRect(window,&rect);
        const int w=rect.right-rect.left,h=rect.bottom-rect.top;
        HDC screen=GetDC(window),dc=CreateCompatibleDC(screen);HBITMAP bitmap=CreateCompatibleBitmap(screen,w,h);HGDIOBJ old=SelectObject(dc,bitmap);
        check(PrintWindow(window,dc,0)!=0,"inprocess native PrintWindow failed");
        SendMessageW(window,WM_PRINT,reinterpret_cast<WPARAM>(dc),PRF_NONCLIENT|PRF_CLIENT|PRF_ERASEBKGND);
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
        std::vector<unsigned char> pixels(size_t(w)*h*4);
        check(GetDIBits(dc,bitmap,0,h,pixels.data(),&info,DIB_RGB_COLORS)==h,"inprocess whole bitmap incomplete");
        BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(info.bmiHeader);header.bfSize=header.bfOffBits+DWORD(pixels.size());
        std::ofstream file("specs/20261014-183652-43pk/native-inprocess.bmp",std::ios::binary);file.write(reinterpret_cast<const char*>(&header),sizeof(header));file.write(reinterpret_cast<const char*>(&info.bmiHeader),sizeof(info.bmiHeader));file.write(reinterpret_cast<const char*>(pixels.data()),pixels.size());file.close();
        SendMessageW(window,WM_PRINT,reinterpret_cast<WPARAM>(dc),PRF_NONCLIENT|PRF_CLIENT|PRF_ERASEBKGND);
        check(GetDIBits(dc,bitmap,0,h,pixels.data(),&info,DIB_RGB_COLORS)==h,"repeat native bitmap incomplete");
        std::ofstream repeat("specs/20261014-183652-43pk/native-inprocess-repeat.bmp",std::ios::binary);repeat.write(reinterpret_cast<const char*>(&header),sizeof(header));repeat.write(reinterpret_cast<const char*>(&info.bmiHeader),sizeof(info.bmiHeader));repeat.write(reinterpret_cast<const char*>(pixels.data()),pixels.size());repeat.close();
        char text[512]{};GetWindowTextA(window,text,sizeof(text));std::ofstream meta("specs/20261014-183652-43pk/native-inprocess-title.txt");meta<<text<<"\nRaw inprocess SendMessage(WM_PRINT, PRF_NONCLIENT|PRF_CLIENT|PRF_ERASEBKGND); whole GetWindowRect; no external composition\n";
        SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(window,screen);captured=true;requestExit();
    }
};
int main(int argc,char** argv){try {
    if(argc>1 && std::string(argv[1])=="--native-capture"){
        NativeCaptureApp app;app.configurePBRShowcase();const int code=app.run();app.shutdown();check(app.captured && !app.hasCaptureFailed(),"native real showcase failed");return code;
    }
    FMemoryManager::Get().Initialize();
    std::ofstream file("specs/20261014-183652-43pk/orientation.ppm",std::ios::binary);file.write(reinterpret_cast<const char*>(imageBytes),sizeof(imageBytes));file.close();
    stbi_set_flip_vertically_on_load(1);
    std::async(std::launch::async,[]{decoderCase(false);}).get(); std::async(std::launch::async,[]{decoderCase(true);}).get();
    tangentCases(); fpsCases(); std::cout<<"PASS: external/embedded top-first decode; global/TLS flip preservation; null/empty images; mirrored/positive/degenerate UV tangents; raw FPS/render cost/clamp; negative/subsecond/minimized FPS\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}
