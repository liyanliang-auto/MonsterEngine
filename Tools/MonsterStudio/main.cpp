#include "StudioWindow.h"
#include "resource.h"

#include <commctrl.h>
#include <objbase.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) {
        wchar_t error[256]{};
        wchar_t title[256]{};
        LoadStringW(instance, IDS_INIT_ERROR, error, 256);
        LoadStringW(instance, IDS_APP_TITLE, title, 256);
        MessageBoxW(nullptr, error, title, MB_OK | MB_ICONERROR);
        return 1;
    }
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    int exitCode = 1;
    {
        StudioWindow studio(instance);
        if (studio.Create()) {
            ShowWindow(studio.Handle(), show);
            UpdateWindow(studio.Handle());
            MSG message{};
            while (GetMessageW(&message, nullptr, 0, 0) > 0) {
                if (!IsDialogMessageW(studio.Handle(), &message)) {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
            }
            exitCode = static_cast<int>(message.wParam);
        }
    }
    CoUninitialize();
    return exitCode;
}
