#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>
#undef DrawText
#include <iostream>
#include <dwmapi.h>
#include <shellscalingapi.h>
#include <vector>
#include <mutex>
#include <functional>
#include <map>
#include <string>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shcore.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "ws2_32.lib")

#include "CloneWindow.hpp"
#include "OverlayWindow.hpp"  // Now using our Direct2D-based OverlayWindow

// Custom draw function - updated for Direct2D
void CustomDraw(OverlayWindow* Overlay, int Width, int Height) {
    Overlay->DrawHollowCircle({ Width / 2.f, Height / 2.f }, 100.f, 1.f, D2D1::ColorF(1.0, 1.0, 1.0, 0.4));
}

int main(int argc, char* argv[]) {
    timeBeginPeriod(1);
    SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
    
    // Create clone window
    CloneWindow cloneWindow((HWND)FindWindowA("UnrealWindow", NULL));

    if (!cloneWindow.Create(GetModuleHandleA(0), SW_SHOW)) {
        std::cerr << "Failed to create clone window." << std::endl;
        return 1;
    }

    // Create overlay window
    OverlayWindow overlayWindow(cloneWindow.GetWindowHandle());
    if (!overlayWindow.Create()) {
        std::cerr << "Failed to create overlay window." << std::endl;
        return 1;
    }

    // Set custom draw callback
    overlayWindow.SetDrawCallback(CustomDraw);

    // Initial update of overlay position
    overlayWindow.UpdatePosition(cloneWindow.GetThumbnailRect());

    // Main message loop
    bool done = false;
    MSG msg;
    ZeroMemory(&msg, sizeof(msg));

    while (!done) {
        // Process Windows messages
        while (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }

        if (done) break;

        // Get relative mouse position and update overlay
        POINT relativeMousePos = cloneWindow.GetRelativeMousePosition();
        bool cursorVisible = cloneWindow.IsMouseCursorVisible() && cloneWindow.IsMouseInSourceWindow();
        overlayWindow.UpdateMouseInfo(relativeMousePos, cursorVisible);

        overlayWindow.UpdatePosition(cloneWindow.GetThumbnailRect());
        overlayWindow.Render();
    }

    return (int)msg.wParam;
}