#include <windows.h>
#include <dwmapi.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <functional>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dwmapi.lib")

class OverlayWindow {
public:
    // Changed callback signature to pass pointer to the class
    using DrawCallback = std::function<void(OverlayWindow* overlay, int width, int height)>;

    OverlayWindow(HWND parentWindow)
        : m_parentWindow(parentWindow),
        m_overlayWindow(0),
        m_pD2DFactory(nullptr),
        m_pRenderTarget(nullptr),
        m_pDWriteFactory(nullptr),
        m_pTextFormatEnglish(nullptr),
        m_pOutlineBrush(nullptr),
        m_pOutline2Brush(nullptr),
        m_drawCallback(nullptr),
        m_relativeMouseX(-1),
        m_relativeMouseY(-1),
        m_cursorVisible(true) {
        SetRectEmpty(&m_thumbnailRect);
    }

    ~OverlayWindow() {
        CleanupD2D();

        if (m_overlayWindow) {
            DestroyWindow(m_overlayWindow);
            m_overlayWindow = 0;
        }
    }

    bool Create() {
        WNDCLASSEXA wcex{};
        wcex.cbSize = sizeof(WNDCLASSEXA);
        wcex.style = CS_HREDRAW | CS_VREDRAW;
        wcex.lpfnWndProc = StaticWndProc;
        wcex.hInstance = GetModuleHandleA(0);
        wcex.lpszClassName = "OverlayWindowClass";

        RegisterClassExA(&wcex);

        m_overlayWindow = CreateWindowExA(WS_EX_TOOLWINDOW, wcex.lpszClassName, wcex.lpszClassName,
            WS_POPUP | WS_VISIBLE, 0, 0, 0, 0, 0, 0, wcex.hInstance, this);
        if (!m_overlayWindow) return false;

        MARGINS Margin = { -1 };
        DwmExtendFrameIntoClientArea(m_overlayWindow, &Margin);

        SetWindowLongW(m_overlayWindow, GWL_EXSTYLE, WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW);
        ShowWindow(m_overlayWindow, 1);
        UpdateWindow(m_overlayWindow);

        if (!CreateDeviceD2D()) {
            CleanupD2D();
            return false;
        }

        return true;
    }

    void UpdatePosition(const RECT& thumbnailRect) {
        m_thumbnailRect = thumbnailRect;

        POINT ptClient = { 0, 0 };
        ClientToScreen(m_parentWindow, &ptClient);

        int x = ptClient.x + thumbnailRect.left;
        int y = ptClient.y + thumbnailRect.top;
        int width = thumbnailRect.right - thumbnailRect.left;
        int height = thumbnailRect.bottom - thumbnailRect.top;

        SetWindowPos(m_overlayWindow, HWND_TOPMOST, x, y, width, height, SWP_SHOWWINDOW);

        // Resize/Create D2D render target
        if (m_pD2DFactory) {
            if (m_pRenderTarget) {
                D2D1_SIZE_U size = m_pRenderTarget->GetPixelSize();
                if (size.width != width || size.height != height) {
                    // Release resources before resize
                    SafeRelease(&m_pOutline2Brush);
                    SafeRelease(&m_pOutlineBrush);
                    SafeRelease(&m_pRenderTarget);

                    // Create new render target with updated size
                    CreateRenderTarget(width, height);
                }
            }
            else {
                // Create render target for the first time
                CreateRenderTarget(width, height);
            }
        }
    }

    void SetDrawCallback(DrawCallback callback) {
        m_drawCallback = callback;
    }

    void UpdateMouseInfo(const POINT& relativeMousePos, bool visible) {
        m_relativeMouseX = relativeMousePos.x;
        m_relativeMouseY = relativeMousePos.y;
        m_cursorVisible = visible;
    }

    void Render() {
        if (!m_pRenderTarget) return;

        int width = m_thumbnailRect.right - m_thumbnailRect.left;
        int height = m_thumbnailRect.bottom - m_thumbnailRect.top;

        m_pRenderTarget->BeginDraw();
        m_pRenderTarget->Clear(D2D1::ColorF(D2D1::ColorF::Black, 0.0f)); // Transparent

        // Draw user-defined content
        if (m_drawCallback) m_drawCallback(this, width, height);

        // Draw cursor
        DrawCustomCursor();

        HRESULT hr = m_pRenderTarget->EndDraw();

        // If the render target was lost, recreate it
        if (hr == D2DERR_RECREATE_TARGET) {
            UpdatePosition(m_thumbnailRect);
        }
    }

    // Drawing utility functions
    void DrawTextWithOutline(const wchar_t* text, D2D1_POINT_2F origin, float fontSize, D2D1::ColorF textColor) {
        if (!m_pRenderTarget || !m_pDWriteFactory || !m_pTextFormatEnglish) return;

        ID2D1SolidColorBrush* pTextBrush = nullptr;
        m_pRenderTarget->CreateSolidColorBrush(textColor, &pTextBrush);

        IDWriteTextLayout* pTextLayout = nullptr;
        UINT32 textLength = static_cast<UINT32>(wcslen(text));
        m_pDWriteFactory->CreateTextLayout(
            text,
            textLength,
            m_pTextFormatEnglish,
            FLT_MAX,
            FLT_MAX,
            &pTextLayout);

        pTextLayout->SetFontSize(fontSize, DWRITE_TEXT_RANGE{ 0, textLength });

        float outlineOffset = 1.0f;
        for (float x = -outlineOffset; x <= outlineOffset; x += outlineOffset) {
            for (float y = -outlineOffset; y <= outlineOffset; y += outlineOffset) {
                if (x != 0.0f || y != 0.0f) {
                    D2D1_POINT_2F outlinePoint = D2D1::Point2F(origin.x + x, origin.y + y);
                    m_pRenderTarget->DrawTextLayout(
                        outlinePoint,
                        pTextLayout,
                        m_pOutlineBrush);
                }
            }
        }

        outlineOffset = 2.0f;
        for (float x = -outlineOffset; x <= outlineOffset; x += outlineOffset) {
            for (float y = -outlineOffset; y <= outlineOffset; y += outlineOffset) {
                if (x != 0.0f || y != 0.0f) {
                    D2D1_POINT_2F outlinePoint = D2D1::Point2F(origin.x + x, origin.y + y);
                    m_pRenderTarget->DrawTextLayout(
                        outlinePoint,
                        pTextLayout,
                        m_pOutline2Brush);
                }
            }
        }

        m_pRenderTarget->DrawTextLayout(
            origin,
            pTextLayout,
            pTextBrush);

        if (pTextLayout) pTextLayout->Release();
        if (pTextBrush) pTextBrush->Release();
    }

    void DrawLine(D2D1_POINT_2F startPoint, D2D1_POINT_2F endPoint, float strokeWidth, D2D1::ColorF color) {
        if (!m_pRenderTarget) return;
        if (!(startPoint.x - endPoint.x) && !(startPoint.y - endPoint.y)) return;

        ID2D1SolidColorBrush* pLineBrush = nullptr;
        m_pRenderTarget->CreateSolidColorBrush(color, &pLineBrush);
        m_pRenderTarget->DrawLine(startPoint, endPoint, pLineBrush, strokeWidth);

        if (pLineBrush) pLineBrush->Release();
    }

    void DrawSolidCircle(D2D1_POINT_2F center, float radius, D2D1::ColorF color) {
        if (!m_pRenderTarget) return;

        ID2D1SolidColorBrush* pCircleBrush = nullptr;
        m_pRenderTarget->CreateSolidColorBrush(color, &pCircleBrush);
        m_pRenderTarget->FillEllipse(D2D1::Ellipse(center, radius, radius), pCircleBrush);

        if (pCircleBrush) pCircleBrush->Release();
    }

    void DrawHollowCircle(D2D1_POINT_2F center, float radius, float strokeWidth, D2D1::ColorF color) {
        if (!m_pRenderTarget) return;

        ID2D1SolidColorBrush* pCircleBrush = nullptr;
        m_pRenderTarget->CreateSolidColorBrush(color, &pCircleBrush);
        m_pRenderTarget->DrawEllipse(D2D1::Ellipse(center, radius, radius), pCircleBrush, strokeWidth);

        if (pCircleBrush) pCircleBrush->Release();
    }

    void DrawHollowDiamond(D2D1_POINT_2F center, float radius, float strokeWidth, D2D1::ColorF color) {
        if (!m_pRenderTarget) return;

        // Calculate the four vertices of the diamond
        D2D1_POINT_2F top = { center.x, center.y - radius };
        D2D1_POINT_2F right = { center.x + radius, center.y };
        D2D1_POINT_2F bottom = { center.x, center.y + radius };
        D2D1_POINT_2F left = { center.x - radius, center.y };

        // Create brush
        ID2D1SolidColorBrush* pDiamondBrush = nullptr;
        m_pRenderTarget->CreateSolidColorBrush(color, &pDiamondBrush);

        // Draw the four sides of the diamond
        m_pRenderTarget->DrawLine(top, right, pDiamondBrush, strokeWidth);
        m_pRenderTarget->DrawLine(right, bottom, pDiamondBrush, strokeWidth);
        m_pRenderTarget->DrawLine(bottom, left, pDiamondBrush, strokeWidth);
        m_pRenderTarget->DrawLine(left, top, pDiamondBrush, strokeWidth);

        // Release resources
        if (pDiamondBrush) pDiamondBrush->Release();
    }

    void DrawCornerBox(D2D1_POINT_2F TopLeft, D2D1_POINT_2F TopRight, D2D1_POINT_2F BottomLeft, D2D1_POINT_2F BottomRight, float lineWidth, D2D1::ColorF color) {
        if (!m_pRenderTarget) return;

        float _01f = 4.0f;

        float TopOneThird = TopLeft.x + (TopRight.x - TopLeft.x) / _01f;
        float TopTwoThirds = TopRight.x - (TopRight.x - TopLeft.x) / _01f;

        float RightOneThird = TopRight.y + (BottomRight.y - TopRight.y) / _01f;
        float RightTwoThirds = BottomRight.y - (BottomRight.y - TopRight.y) / _01f;

        float BottomOneThird = BottomLeft.x + (BottomRight.x - BottomLeft.x) / _01f;
        float BottomTwoThirds = BottomRight.x - (BottomRight.x - BottomLeft.x) / _01f;

        float LeftOneThird = TopLeft.y + (BottomLeft.y - TopLeft.y) / _01f;
        float LeftTwoThirds = BottomLeft.y - (BottomLeft.y - TopLeft.y) / _01f;

        // Draw the lines for each side
        DrawLine(TopLeft, { TopOneThird, TopLeft.y }, lineWidth, color);
        DrawLine({ TopTwoThirds, TopRight.y }, TopRight, lineWidth, color);

        DrawLine(TopRight, { TopRight.x, RightOneThird }, lineWidth, color);
        DrawLine({ BottomRight.x, RightTwoThirds }, BottomRight, lineWidth, color);

        DrawLine(BottomLeft, { BottomOneThird, BottomLeft.y }, lineWidth, color);
        DrawLine({ BottomTwoThirds, BottomRight.y }, BottomRight, lineWidth, color);

        DrawLine(TopLeft, { TopLeft.x, LeftOneThird }, lineWidth, color);
        DrawLine({ BottomLeft.x, LeftTwoThirds }, BottomLeft, lineWidth, color);
    }

    D2D1_SIZE_F GetTextSize(const wchar_t* text, float fontSize) {
        if (!m_pDWriteFactory || !m_pTextFormatEnglish) return D2D1::SizeF(0, 0);

        IDWriteTextLayout* pTextLayout = nullptr;
        UINT32 textLength = static_cast<UINT32>(wcslen(text));
        m_pDWriteFactory->CreateTextLayout(
            text,
            textLength,
            m_pTextFormatEnglish,
            FLT_MAX,
            FLT_MAX,
            &pTextLayout);

        pTextLayout->SetFontSize(fontSize, DWRITE_TEXT_RANGE{ 0, textLength });

        DWRITE_TEXT_METRICS textMetrics;
        pTextLayout->GetMetrics(&textMetrics);

        if (pTextLayout) pTextLayout->Release();

        return D2D1::SizeF(textMetrics.width, textMetrics.height);
    }

    void DrawSolidRectangle(D2D1_RECT_F rect, D2D1::ColorF color) {
        if (!m_pRenderTarget) return;

        ID2D1SolidColorBrush* pRectBrush = nullptr;
        m_pRenderTarget->CreateSolidColorBrush(color, &pRectBrush);
        m_pRenderTarget->FillRectangle(rect, pRectBrush);

        if (pRectBrush) pRectBrush->Release();
    }

    void DrawHollowRectangle(D2D1_RECT_F rect, float strokeWidth, D2D1::ColorF color) {
        if (!m_pRenderTarget) return;

        ID2D1SolidColorBrush* pRectBrush = nullptr;
        m_pRenderTarget->CreateSolidColorBrush(color, &pRectBrush);
        m_pRenderTarget->DrawRectangle(rect, pRectBrush, strokeWidth);

        if (pRectBrush) pRectBrush->Release();
    }

private:
    HWND m_parentWindow;
    HWND m_overlayWindow;
    RECT m_thumbnailRect;

    // D2D Resources
    ID2D1Factory* m_pD2DFactory;
    ID2D1HwndRenderTarget* m_pRenderTarget;
    IDWriteFactory* m_pDWriteFactory;
    IDWriteTextFormat* m_pTextFormatEnglish;
    ID2D1SolidColorBrush* m_pOutlineBrush;
    ID2D1SolidColorBrush* m_pOutline2Brush;

    DrawCallback m_drawCallback;
    int m_relativeMouseX;  // Mouse X position (0-1000 range)
    int m_relativeMouseY;  // Mouse Y position (0-1000 range)
    bool m_cursorVisible;

    void DrawCustomCursor() {
        // If relative mouse position is valid and cursor is visible
        if (m_relativeMouseX >= 0 && m_relativeMouseY >= 0 && m_cursorVisible && m_pRenderTarget) {
            // Get current window size
            int width = m_thumbnailRect.right - m_thumbnailRect.left;
            int height = m_thumbnailRect.bottom - m_thumbnailRect.top;

            if (width <= 0 || height <= 0) return;

            // Convert relative position (0-1000) to pixel coordinates
            float pixelX = (m_relativeMouseX * width) / 1000.0f;
            float pixelY = (m_relativeMouseY * height) / 1000.0f;

            // Draw filled circle
            DrawSolidCircle(D2D1::Point2F(pixelX, pixelY), 5.0f, D2D1::ColorF(D2D1::ColorF::White));

            // Draw outline
            DrawHollowCircle(D2D1::Point2F(pixelX, pixelY), 5.0f, 1.0f, D2D1::ColorF(D2D1::ColorF::Black));
        }
    }

    bool CreateDeviceD2D() {
        HRESULT hr;

        // Create D2D factory
        hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_pD2DFactory);
        if (FAILED(hr)) return false;

        // Create DirectWrite factory
        hr = DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(&m_pDWriteFactory));
        if (FAILED(hr)) return false;

        // Create text format
        hr = m_pDWriteFactory->CreateTextFormat(
            L"TT Lakes",
            NULL,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            14.0f,
            L"en-us",
            &m_pTextFormatEnglish);
        if (FAILED(hr)) return false;

        // The render target will be created in the UpdatePosition method
        return true;
    }

    void CreateRenderTarget(int width, int height) {
        if (!m_pD2DFactory || width <= 0 || height <= 0) return;

        D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED));
        D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps = D2D1::HwndRenderTargetProperties(
            m_overlayWindow, D2D1::SizeU(width, height));

        HRESULT hr = m_pD2DFactory->CreateHwndRenderTarget(
            rtProps, hwndProps, &m_pRenderTarget);

        if (SUCCEEDED(hr)) {
            // Create brushes
            m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.3f, 0.3f, 0.3f, 0.35f), &m_pOutlineBrush);
            m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.2f, 0.2f, 0.2f, 0.08f), &m_pOutline2Brush);

            m_pRenderTarget->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            m_pRenderTarget->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_DEFAULT);
        }

    }

    void CleanupD2D() {
        SafeRelease(&m_pOutline2Brush);
        SafeRelease(&m_pOutlineBrush);
        SafeRelease(&m_pTextFormatEnglish);
        SafeRelease(&m_pRenderTarget);
        SafeRelease(&m_pDWriteFactory);
        SafeRelease(&m_pD2DFactory);
    }

    // Helper function for safely releasing COM objects
    template <typename Interface>
    void SafeRelease(Interface** ppInterfaceToRelease) {
        if (*ppInterfaceToRelease != nullptr) {
            (*ppInterfaceToRelease)->Release();
            (*ppInterfaceToRelease) = nullptr;
        }
    }

    static LRESULT CALLBACK StaticWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        OverlayWindow* pThis = nullptr;

        if (msg == WM_CREATE) {
            CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
            pThis = (OverlayWindow*)pCreate->lpCreateParams;
            SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pThis);
        }
        else {
            pThis = (OverlayWindow*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
        }

        if (pThis) {
            return pThis->WndProc(hWnd, msg, wParam, lParam);
        }

        return DefWindowProc(hWnd, msg, wParam, lParam);
    }

    LRESULT WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
        case WM_SIZE:
            // The render target resize is handled in UpdatePosition
            return 0;

        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU)
                return 0;
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }

        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
};