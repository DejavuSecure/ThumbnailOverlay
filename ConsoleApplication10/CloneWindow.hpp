// 克隆窗口
class CloneWindow {
public:
    CloneWindow(HWND SourceWindow = 0)
        : m_hMainWindow(0),
        m_hSourceWindow(SourceWindow),
        m_hThumbnail(0) {
        SetRectEmpty(&m_sourceClientRect);
        SetRectEmpty(&m_lastSourceClientRect);
        SetRectEmpty(&m_thumbnailRect);
    }

    ~CloneWindow() {
        if (m_hMainWindow) KillTimer(m_hMainWindow, 1001);

        if (m_hThumbnail) {
            DwmUnregisterThumbnail(m_hThumbnail);
            m_hThumbnail = 0;
        }
    }

    bool Create(HINSTANCE hInstance, int nCmdShow) {
        if (!IsWindow(m_hSourceWindow)) {
            MessageBox(0, L"源窗口句柄无效", L"错误", MB_ICONERROR);
            return false;
        }

        WNDCLASSEXW wcex = { 0 };
        wcex.cbSize = sizeof(WNDCLASSEX);
        wcex.style = CS_HREDRAW | CS_VREDRAW;
        wcex.lpfnWndProc = StaticWndProc;
        wcex.hInstance = hInstance;
        wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wcex.hbrBackground = CreateSolidBrush(RGB(0, 0, 0));
        wcex.lpszClassName = L"ThumbnailWindowClass";

        if (!RegisterClassExW(&wcex)) {
            MessageBox(0, L"无法注册窗口类", L"错误", MB_ICONERROR);
            return false;
        }

        m_hMainWindow = CreateWindowW(wcex.lpszClassName, wcex.lpszClassName, WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, 0, 800, 600, 0, 0, hInstance, this);
        if (!m_hMainWindow) {
            MessageBox(0, L"无法创建窗口", L"错误", MB_ICONERROR);
            return false;
        }

        if (!InitializeThumbnail()) {
            MessageBox(m_hMainWindow, L"无法初始化缩略图", L"错误", MB_ICONERROR);
            DestroyWindow(m_hMainWindow);
            m_hMainWindow = 0;
            return false;
        }

        SIZE minSize = GetMinimumSuggestedSize();
        RECT rcWindow;
        GetWindowRect(m_hMainWindow, &rcWindow);
        int curWidth = rcWindow.right - rcWindow.left;
        int curHeight = rcWindow.bottom - rcWindow.top;

        if (curWidth < minSize.cx || curHeight < minSize.cy) {
            SetWindowPos(m_hMainWindow, 0, 0, 0,
                max(curWidth, minSize.cx),
                max(curHeight, minSize.cy),
                SWP_NOMOVE | SWP_NOZORDER);
        }

        ShowWindow(m_hMainWindow, nCmdShow);
        UpdateWindow(m_hMainWindow);
        UpdateThumbnail();

        return true;
    }

    RECT GetThumbnailRect() const {
        return m_thumbnailRect;
    }

    HWND GetWindowHandle() const {
        return m_hMainWindow;
    }

    int Run() {
        MSG msg = { 0 };
        while (GetMessage(&msg, 0, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        return (int)msg.wParam;
    }

    // 获取鼠标在源窗口中的相对位置 (0.0-1.0范围)
    // 返回{-1,-1}表示鼠标不在源窗口内或其他错误
    POINT GetRelativeMousePosition() const {
        POINT ptMouse = { 0, 0 };
        GetCursorPos(&ptMouse);

        // 检查窗口是否有效
        if (!IsWindow(m_hSourceWindow)) {
            return { -1, -1 };
        }

        // 将屏幕坐标转换为源窗口客户区坐标
        POINT ptSource = ptMouse;
        ScreenToClient(m_hSourceWindow, &ptSource);

        // 获取源窗口客户区大小
        RECT srcClientRect;
        GetClientRect(m_hSourceWindow, &srcClientRect);
        int srcWidth = srcClientRect.right - srcClientRect.left;
        int srcHeight = srcClientRect.bottom - srcClientRect.top;

        if (srcWidth <= 0 || srcHeight <= 0) {
            return { -1, -1 };
        }

        // 检查鼠标是否在源窗口内
        if (ptSource.x < 0 || ptSource.y < 0 ||
            ptSource.x > srcClientRect.right || ptSource.y > srcClientRect.bottom) {
            return { -1, -1 }; // 鼠标不在源窗口内
        }

        // 计算相对位置 (0-1000范围，避免浮点数)
        POINT result;
        result.x = (ptSource.x * 1000) / srcWidth;
        result.y = (ptSource.y * 1000) / srcHeight;

        return result;
    }

    bool IsMouseCursorVisible() const {
        CURSORINFO ci = { sizeof(CURSORINFO) };
        if (GetCursorInfo(&ci)) {
            return (ci.flags & CURSOR_SHOWING) != 0;
        }
        return true; // 默认可见
    }

    bool IsMouseInSourceWindow() const {
        if (!IsWindow(m_hSourceWindow)) {
            return false;
        }

        POINT ptMouse;
        GetCursorPos(&ptMouse);

        RECT rcWindow;
        GetClientRect(m_hSourceWindow, &rcWindow);

        POINT ptTopLeft = { rcWindow.left, rcWindow.top };
        POINT ptBottomRight = { rcWindow.right, rcWindow.bottom };
        ClientToScreen(m_hSourceWindow, &ptTopLeft);
        ClientToScreen(m_hSourceWindow, &ptBottomRight);

        rcWindow.left = ptTopLeft.x;
        rcWindow.top = ptTopLeft.y;
        rcWindow.right = ptBottomRight.x;
        rcWindow.bottom = ptBottomRight.y;

        return PtInRect(&rcWindow, ptMouse);
    }

private:
    HWND m_hMainWindow;
    HWND m_hSourceWindow;
    HTHUMBNAIL m_hThumbnail;
    RECT m_sourceClientRect;
    RECT m_lastSourceClientRect;
    RECT m_thumbnailRect;

    bool InitializeThumbnail() {
        HRESULT hr = DwmRegisterThumbnail(m_hMainWindow, m_hSourceWindow, &m_hThumbnail);
        if (FAILED(hr)) {
            return false;
        }

        if (!GetClientRectInWindowCoords(m_hSourceWindow, &m_sourceClientRect)) {
            DwmUnregisterThumbnail(m_hThumbnail);
            m_hThumbnail = 0;
            return false;
        }

        m_lastSourceClientRect = m_sourceClientRect;
        SetTimer(m_hMainWindow, 1001, 500, 0);
        return true;
    }

    bool UpdateThumbnail() {
        if (!m_hThumbnail || !IsWindow(m_hMainWindow))
            return false;

        RECT rcClient;
        if (!GetClientRect(m_hMainWindow, &rcClient))
            return false;

        int clientWidth = rcClient.right - rcClient.left;
        int clientHeight = rcClient.bottom - rcClient.top;

        if (clientWidth <= 0 || clientHeight <= 0)
            return false;

        int srcWidth = m_sourceClientRect.right - m_sourceClientRect.left;
        int srcHeight = m_sourceClientRect.bottom - m_sourceClientRect.top;

        if (srcWidth <= 0 || srcHeight <= 0)
            return false;

        float srcAspectRatio = (float)srcWidth / srcHeight;
        float clientAspectRatio = (float)clientWidth / clientHeight;

        int dstWidth, dstHeight;
        if (srcAspectRatio > clientAspectRatio) {
            dstWidth = clientWidth;
            dstHeight = (int)(dstWidth / srcAspectRatio);
        }
        else {
            dstHeight = clientHeight;
            dstWidth = (int)(dstHeight * srcAspectRatio);
        }

        int x = (clientWidth - dstWidth) / 2;
        int y = (clientHeight - dstHeight) / 2;

        m_thumbnailRect.left = x;
        m_thumbnailRect.top = y;
        m_thumbnailRect.right = x + dstWidth;
        m_thumbnailRect.bottom = y + dstHeight;

        DWM_THUMBNAIL_PROPERTIES props = { 0 };
        props.dwFlags = DWM_TNP_VISIBLE | DWM_TNP_RECTDESTINATION | DWM_TNP_RECTSOURCE | DWM_TNP_OPACITY;
        props.fVisible = TRUE;
        props.opacity = 255;
        props.rcSource = m_sourceClientRect;
        props.rcDestination = { x, y, x + dstWidth, y + dstHeight };

        return SUCCEEDED(DwmUpdateThumbnailProperties(m_hThumbnail, &props));
    }

    bool CheckSourceSizeChanged() {
        if (!IsWindow(m_hSourceWindow))
            return false;

        RECT currentRect;
        if (!GetClientRectInWindowCoords(m_hSourceWindow, &currentRect))
            return false;

        if (memcmp(&currentRect, &m_lastSourceClientRect, sizeof(RECT)) != 0) {
            m_lastSourceClientRect = currentRect;
            m_sourceClientRect = currentRect;
            return true;
        }

        return false;
    }

    SIZE GetMinimumSuggestedSize() {
        SIZE size = { 320, 240 };

        if (!IsWindow(m_hSourceWindow))
            return size;

        RECT srcClientRect;
        if (!GetClientRectInWindowCoords(m_hSourceWindow, &srcClientRect))
            return size;

        int srcWidth = srcClientRect.right - srcClientRect.left;
        int srcHeight = srcClientRect.bottom - srcClientRect.top;

        if (srcWidth <= 0 || srcHeight <= 0)
            return size;

        UINT sourceDpi = GetDpiForWindow(m_hSourceWindow);
        UINT targetDpi = GetDpiForWindow(m_hMainWindow);
        float dpiRatio = (float)targetDpi / sourceDpi;

        int contentWidth = (int)(srcWidth * dpiRatio) + 16;
        int contentHeight = (int)(srcHeight * dpiRatio) + 16;

        RECT rcWindow, rcClient;
        GetWindowRect(m_hMainWindow, &rcWindow);
        GetClientRect(m_hMainWindow, &rcClient);

        int nonClientWidth = (rcWindow.right - rcWindow.left) - rcClient.right;
        int nonClientHeight = (rcWindow.bottom - rcWindow.top) - rcClient.bottom;

        size.cx = contentWidth + nonClientWidth;
        size.cy = contentHeight + nonClientHeight;

        return size;
    }

    UINT GetDpiForWindow(HWND hWnd) {
        UINT dpi = 96;

        HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
        if (hMonitor) {
            UINT dpiX = 0, dpiY = 0;
            if (SUCCEEDED(GetDpiForMonitor(hMonitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) {
                dpi = dpiX;
            }
        }

        return dpi;
    }

    bool GetClientRectInWindowCoords(HWND hWnd, RECT* pClientRect) {
        if (!IsWindow(hWnd) || !pClientRect)
            return false;

        WINDOWINFO wi = { sizeof(WINDOWINFO) };
        if (!GetWindowInfo(hWnd, &wi))
            return false;

        RECT rcWindow;
        if (!GetWindowRect(hWnd, &rcWindow))
            return false;

        pClientRect->left = wi.rcClient.left - rcWindow.left;
        pClientRect->top = wi.rcClient.top - rcWindow.top;
        pClientRect->right = wi.rcClient.right - rcWindow.left;
        pClientRect->bottom = wi.rcClient.bottom - rcWindow.top;

        return true;
    }

    static LRESULT CALLBACK StaticWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
        CloneWindow* pThis = 0;

        if (message == WM_CREATE) {
            CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
            pThis = (CloneWindow*)pCreate->lpCreateParams;
            SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pThis);
        }
        else {
            pThis = (CloneWindow*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
        }

        if (pThis) {
            return pThis->WndProc(hWnd, message, wParam, lParam);
        }
        return DefWindowProc(hWnd, message, wParam, lParam);
    }

    LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_CREATE:
            break;

        case WM_SIZE:
            if (m_hThumbnail) {
                UpdateThumbnail();
            }
            break;

        case WM_TIMER:
            if (wParam == 1001) {
                if (CheckSourceSizeChanged()) {
                    UpdateThumbnail();
                }
            }
            break;

        case WM_GETMINMAXINFO:
        {
            MINMAXINFO* pMMI = (MINMAXINFO*)lParam;
            SIZE minSize = GetMinimumSuggestedSize();
            pMMI->ptMinTrackSize.x = minSize.cx;
            pMMI->ptMinTrackSize.y = minSize.cy;
        }
        break;

        case WM_DPICHANGED:
        {
            RECT* prcNewWindow = (RECT*)lParam;
            SetWindowPos(hWnd, 0,
                prcNewWindow->left, prcNewWindow->top,
                prcNewWindow->right - prcNewWindow->left,
                prcNewWindow->bottom - prcNewWindow->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
            UpdateThumbnail();
        }
        break;

        case WM_DESTROY:
            KillTimer(hWnd, 1001);
            if (m_hThumbnail) {
                DwmUnregisterThumbnail(m_hThumbnail);
                m_hThumbnail = 0;
            }
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }

        return 0;
    }
};