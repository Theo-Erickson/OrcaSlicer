// NativeSplash.cpp  (Windows only)
#include "NativeSplash.hpp"

#ifdef _WIN32

#include <wx/bitmap.h>
#include <wx/dcmemory.h>
#include <wx/graphics.h>
#include <wx/image.h>
#include <wx/brush.h>
#include <wx/font.h>
#include <wx/msw/wrapwin.h>   // pulls in <windows.h> the wx-safe way

#include "../BitmapCache.hpp"
#include "../GUI_App.hpp"
#include "SplashCompose.hpp"
#include "SplashRenderers.hpp"

namespace Slic3r {
namespace GUI {

namespace {

constexpr int  kFrameCount = 60;     // ~2 s loop at ~30 FPS
constexpr long kFrameStep  = 33;     // ms between frames
const     wchar_t* kWndClass = L"CrashSlicerNativeSplashWnd";

// wxBitmap -> top-down 32bpp DIB section (HBITMAP). Caller owns the HBITMAP.
HBITMAP bitmap_to_dib(const wxBitmap& bmp)
{
    wxImage img = bmp.ConvertToImage();
    if (!img.IsOk())
        return nullptr;
    const int w = img.GetWidth();
    const int h = img.GetHeight();

    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;   // top-down, matches wxImage row order
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void*   bits = nullptr;
    HBITMAP hbmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hbmp || !bits)
        return hbmp;

    const unsigned char* src = img.GetData();  // RGB, 3 bytes/pixel
    unsigned char*       dst = static_cast<unsigned char*>(bits);
    for (int i = 0; i < w * h; ++i) {
        dst[i * 4 + 0] = src[i * 3 + 2];  // B
        dst[i * 4 + 1] = src[i * 3 + 1];  // G
        dst[i * 4 + 2] = src[i * 3 + 0];  // R
        dst[i * 4 + 3] = 255;
    }
    return hbmp;
}

} // anonymous namespace

NativeSplash::NativeSplash(SplashTheme theme, const wxString& version, const wxString& subtitle, const wxPoint& pos)
    : m_theme(theme)
    , m_version(version)
    , m_subtitle(subtitle)
    , m_pos(pos)
{
    m_stop_event = CreateEventW(nullptr, TRUE /*manual reset*/, FALSE, nullptr);
}

NativeSplash::~NativeSplash()
{
    Stop();
    for (void* f : m_frames)
        if (f) DeleteObject(static_cast<HBITMAP>(f));
    m_frames.clear();
    if (m_stop_event) {
        CloseHandle(static_cast<HANDLE>(m_stop_event));
        m_stop_event = nullptr;
    }
}

void NativeSplash::prerender_frames()
{
    const SplashThemeDef& T = splash_theme_def(m_theme);
    wxColour bg(T.bg.r, T.bg.g, T.bg.b);
    m_bg         = RGB(T.bg.r, T.bg.g, T.bg.b);
    m_status_col = RGB(T.status_color.r, T.status_color.g, T.status_color.b);

    BitmapCache cache;
    wxBitmap    logo;
    if (wxBitmap* b = cache.load_svg("splash_logo_dark", 160, 160))
        logo = *b;

    wxFont fver  = wxFontInfo(11).Bold();
    wxFont fsub  = wxFontInfo(7);
    wxFont fstat = wxFontInfo(8);

    m_frames.reserve(kFrameCount);
    for (int i = 0; i < kFrameCount; ++i) {
        wxBitmap bmp(m_frame_w, m_frame_h, 24);
        {
            wxMemoryDC mdc(bmp);
            mdc.SetBackground(wxBrush(bg));
            mdc.Clear();
            if (wxGraphicsContext* gc = wxGraphicsContext::Create(mdc)) {
                SplashComposeInput in;
                in.theme        = m_theme;
                in.el_ms        = long(i) * kFrameStep;
                in.charge       = 1.0f;             // full charge, so looping doesn't pop
                in.logo         = logo.IsOk() ? &logo : nullptr;
                in.version      = m_version;
                in.sub          = m_subtitle;
                in.status       = wxString();       // status is drawn live on top, not baked
                in.font_version = fver;
                in.font_sub     = fsub;
                in.font_status  = fstat;
                draw_splash(gc, in);
                delete gc;
            }
        }
        m_frames.push_back(bitmap_to_dib(bmp));
    }
}

void NativeSplash::Start()
{
    if (m_started.exchange(true))
        return;
    prerender_frames();  // main thread
    m_thread = std::thread(&NativeSplash::thread_main, this);
}

void NativeSplash::SetStatus(const wxString& text)
{
    if (text.empty())
        return;
    std::lock_guard<std::mutex> lk(m_status_mutex);
    m_status = text.ToStdWstring();
}

void NativeSplash::Stop()
{
    if (!m_started.load())
        return;
    m_stop.store(true);
    if (m_stop_event)
        SetEvent(static_cast<HANDLE>(m_stop_event));
    if (m_thread.joinable())
        m_thread.join();
}

void NativeSplash::thread_main()
{
    HINSTANCE hInst = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = DefWindowProcW;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kWndClass;
    RegisterClassExW(&wc);  // harmless if the class already exists

    // DPI-scaled window size.
    HDC screen = GetDC(nullptr);
    int dpi = screen ? GetDeviceCaps(screen, LOGPIXELSX) : 96;
    if (screen) ReleaseDC(nullptr, screen);
    if (dpi <= 0) dpi = 96;
    m_win_w = MulDiv(m_frame_w, dpi, 96);
    m_win_h = MulDiv(m_frame_h, dpi, 96);

    // Center on the target monitor (the one holding the last main-window position).
    HMONITOR mon;
    if (m_pos == wxDefaultPosition) {
        POINT z = { 0, 0 };
        mon = MonitorFromPoint(z, MONITOR_DEFAULTTOPRIMARY);
    } else {
        POINT anchor = { m_pos.x, m_pos.y };
        mon = MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST);
    }
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    int wx = 0, wy = 0;
    if (GetMonitorInfoW(mon, &mi)) {
        int mw = mi.rcWork.right - mi.rcWork.left;
        int mh = mi.rcWork.bottom - mi.rcWork.top;
        wx = mi.rcWork.left + (mw - m_win_w) / 2;
        wy = mi.rcWork.top + (mh - m_win_h) / 2;
    }

    // Not WS_EX_TOPMOST: the splash shows on top at creation but can be covered
    // (clicked away / hidden) like the original Windows splash, rather than
    // floating over every other application. WS_EX_TOOLWINDOW keeps it out of
    // the taskbar and Alt-Tab.
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, kWndClass, L"",
                                WS_POPUP, wx, wy, m_win_w, m_win_h,
                                nullptr, nullptr, hInst, nullptr);
    if (!hwnd)
        return;
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);

    HDC     winDC  = GetDC(hwnd);
    HDC     backDC = CreateCompatibleDC(winDC);
    HBITMAP back   = CreateCompatibleBitmap(winDC, m_win_w, m_win_h);
    HGDIOBJ oldBack = SelectObject(backDC, back);
    HDC     frameDC = CreateCompatibleDC(winDC);
    SetStretchBltMode(backDC, HALFTONE);
    SetBrushOrgEx(backDC, 0, 0, nullptr);

    int   fontPx = int(11.0 * m_win_h / 360.0 + 0.5);
    HFONT font   = CreateFontW(-fontPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    const size_t    n          = m_frames.size();
    HANDLE          ev         = static_cast<HANDLE>(m_stop_event);
    const ULONGLONG start_tick = GetTickCount64();

    while (!m_stop.load()) {
        // Keep the window responsive, but don't let incoming input pace the
        // animation: the frame is chosen purely by elapsed wall-clock time, so
        // moving the mouse over the splash can't speed it up.
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        size_t idx = 0;
        if (n > 0) {
            ULONGLONG elapsed = GetTickCount64() - start_tick;
            idx = size_t(elapsed / (ULONGLONG) kFrameStep) % n;
        }

        if (n > 0 && m_frames[idx]) {
            HGDIOBJ oldF = SelectObject(frameDC, static_cast<HBITMAP>(m_frames[idx]));
            StretchBlt(backDC, 0, 0, m_win_w, m_win_h, frameDC, 0, 0, m_frame_w, m_frame_h, SRCCOPY);
            SelectObject(frameDC, oldF);
        }

        std::wstring status;
        {
            std::lock_guard<std::mutex> lk(m_status_mutex);
            status = m_status;
        }
        if (!status.empty()) {
            HGDIOBJ oldFont = SelectObject(backDC, font);
            SetBkMode(backDC, TRANSPARENT);
            SetTextColor(backDC, m_status_col);
            RECT rc;
            rc.left   = 0;
            rc.right  = m_win_w;
            rc.top    = LONG(282.0 * m_win_h / 360.0);
            rc.bottom = rc.top + fontPx * 2 + 4;
            DrawTextW(backDC, status.c_str(), -1, &rc, DT_CENTER | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(backDC, oldFont);
        }

        BitBlt(winDC, 0, 0, m_win_w, m_win_h, backDC, 0, 0, SRCCOPY);

        // Sleep ~one frame; wake early only when asked to stop (never on input).
        if (ev)
            WaitForSingleObject(ev, kFrameStep);
        else
            Sleep(kFrameStep);
    }

    if (font) DeleteObject(font);
    SelectObject(backDC, oldBack);
    DeleteObject(back);
    DeleteDC(frameDC);
    DeleteDC(backDC);
    ReleaseDC(hwnd, winDC);
    DestroyWindow(hwnd);
}

} // namespace GUI
} // namespace Slic3r

#endif // _WIN32
