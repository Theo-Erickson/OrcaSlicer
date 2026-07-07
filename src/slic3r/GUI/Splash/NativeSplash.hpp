#pragma once
// NativeSplash.hpp
//
// Windows-only animated splash that runs on its OWN thread and window, so it
// keeps animating while the main thread is blocked in synchronous startup —
// where the wx event loop isn't running yet and a normal wxTimer can't fire.
//
// Frames are pre-rendered on the main thread using the shared draw_splash()
// (so the art matches SplashFrame / the Preferences preview exactly), then the
// background thread blits them at a fixed rate and draws the live status text
// on top each frame. Non-Windows builds don't use this — they fall back to the
// stepped SplashFrame.
//
// PCH note: no <windows.h> here. Win32 handles are stored as void* / unsigned
// long and cast in the .cpp.

#ifdef _WIN32

#include <wx/gdicmn.h>   // wxPoint
#include <wx/string.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "SplashThemes.hpp"

namespace Slic3r {
namespace GUI {

class NativeSplash
{
public:
    NativeSplash(SplashTheme theme, const wxString& version, const wxString& subtitle, const wxPoint& pos);
    ~NativeSplash();

    // Pre-renders the frames (in the constructor) and spawns the render thread.
    void Start();

    // Thread-safe update of the status/action line (called from the main thread).
    void SetStatus(const wxString& text);

    // Signal the render thread to stop and join it.
    void Stop();

private:
    void prerender_frames();   // main thread
    void thread_main();        // background thread

    SplashTheme m_theme;
    wxString    m_version;
    wxString    m_subtitle;
    wxPoint     m_pos;

    int m_frame_w = 480;   // logical frame size (matches SPLASH_W/H)
    int m_frame_h = 360;
    int m_win_w   = 480;   // actual window size (DPI-scaled)
    int m_win_h   = 360;

    std::vector<void*> m_frames;       // HBITMAP DIB sections, one per frame
    unsigned long      m_bg         = 0;  // COLORREF
    unsigned long      m_status_col = 0;  // COLORREF

    std::thread       m_thread;
    std::atomic<bool> m_stop{ false };
    std::atomic<bool> m_started{ false };
    std::mutex        m_status_mutex;
    std::wstring      m_status;
    void*             m_stop_event = nullptr;  // HANDLE (manual-reset event)
};

} // namespace GUI
} // namespace Slic3r

#endif // _WIN32
