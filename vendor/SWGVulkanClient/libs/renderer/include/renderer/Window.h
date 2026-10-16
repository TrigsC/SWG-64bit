#pragma once

#include <cstdint>
#include <unordered_set>
#include <array>
#include <vector>
#include "TextFocus.h"

#ifdef _WIN32
#include <Windows.h>
#else
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#endif

namespace renderer {

// One focused Win32 or SDL window with drawable-pixel mouse input.
// Cameras orbit while the right button is held; the offline viewer also
// accepts left drags. Key taps and button-associated movement are retained
// until the render loop consumes them, including releases between frames.
class Window {
public:
#ifdef _WIN32
    using NativeHandle = HWND;
#else
    using NativeHandle = SDL_Window*;
#endif
    enum Key { Space = 0x20, Control = 0x11, Escape = 0x1B };
    Window(int width, int height, const wchar_t* title);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Drains native events. Returns false once the window requests closure.
    bool pumpMessages();

    void setTitle(const wchar_t* title);
    NativeHandle handle() const { return hwnd_; }
    int width() const { return width_; }
    int height() const { return height_; }
    // Original UI coordinates stay in screen points on Retina displays.
    float pixelDensity() const;
    void enableTextEntry(bool enabled){textFocus_.enable(enabled);}
    const TextFocus& textFocus() const{return textFocus_;}
    void enableTextSubmission(bool enabled){textFocus_.enableSubmission(enabled);}
    std::optional<std::u16string> takeTextSubmission(){return textFocus_.takeSubmission();}
    void finishTextSubmission(bool accepted){textFocus_.finishSubmission(accepted);
#ifndef _WIN32
        if(!textFocus_.active())SDL_StopTextInput(hwnd_);
#endif
    }
    void setTextEntryRect(std::array<int,4> rect){textEntryRect_=rect;}
    void setChatAreaRect(std::array<int,4> rect){chatAreaRect_=rect;}
    int takeChatWheelDelta(){int delta=chatWheel_;chatWheel_=0;return delta;}
    void setChatTabRects(std::vector<std::array<int,4>> rects){if(rects.size()>32)rects.resize(32);chatTabRects_=std::move(rects);}
    std::optional<size_t> takeChatTabSelection(){auto value=chatTabSelection_;chatTabSelection_.reset();return value;}
    void setActionRects(std::vector<std::array<int,4>> rects){if(rects.size()>8)rects.resize(8);actionRects_=std::move(rects);}
    std::optional<size_t> takeActionSelection(){auto value=actionSelection_;actionSelection_.reset();return value;}

    // Virtual-key code (VK_...) queried via GetAsyncKeyState, gated on this
    // window actually being the foreground/focused window (real bug found
    // live: without this gate, typing in a completely different
    // application that happened to contain one of this tool's single-letter
    // debug toggle keys fired that toggle even with the game window
    // unfocused).
    static bool isKeyDown(int virtualKeyCode);
    // Consume a non-repeated key-down event, including taps released between frames.
    bool takeKeyPress(int virtualKeyCode);
    // button: 0 = left, 1 = right, 2 = middle. Same foreground-window gate
    // as isKeyDown().
    static bool isMouseButtonDown(int button);

    // Mouse movement (in pixels) since the last call to this function -
    // resets the accumulator each call, so call it exactly once per frame.
    void takeMouseDelta(int& dx, int& dy);
    // Motion recorded while a button was down; survives release between frames.
    void takeMouseDragDelta(int button, int& dx, int& dy);

    // Absolute cursor position, in client-area pixels, as of the most
    // recent WM_MOUSEMOVE - unlike takeMouseDelta() this does NOT reset
    // anything (safe to call every frame regardless of whether the mouse
    // actually moved this frame), for screen-to-world ray picking (Phase 17
    // Step 5). (0, 0) with haveMouse_ still false is a real possibility
    // right after window creation, before the first WM_MOUSEMOVE arrives.
    void cursorPosition(int& x, int& y) const;

    // Accumulated WM_MOUSEWHEEL delta (multiples of WHEEL_DELTA == 120, one
    // notch) since the last call - resets to 0 each call, call once per
    // frame. Positive = away from the user (zoom in, by convention).
    int takeMouseWheelDelta();

private:
#ifdef _WIN32
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

    NativeHandle hwnd_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    bool quit_ = false;
    std::unordered_set<int> keyPresses_;
    TextFocus textFocus_;
    std::array<int,4> textEntryRect_{};
    std::array<int,4> chatAreaRect_{};
    int chatWheel_=0;
    std::vector<std::array<int,4>> chatTabRects_;
    std::optional<size_t> chatTabSelection_;
    std::vector<std::array<int,4>> actionRects_;
    std::optional<size_t> actionSelection_;
    bool actionMouseHeld_=false;

    // Updated from WM_MOUSEMOVE inside wndProc(), drained by
    // takeMouseDelta(). Raw screen-space deltas are computed by comparing
    // successive WM_MOUSEMOVE positions - simple and sufficient for a
    // hold-right-mouse-to-look camera (no need for WM_INPUT/raw input at
    // this fidelity level).
    int lastMouseX_ = 0;
    int lastMouseY_ = 0;
    bool haveLastMouse_ = false;
    int accumDx_ = 0;
    int accumDy_ = 0;
    int accumWheelDelta_ = 0;
    int dragDx_[3] = {};
    int dragDy_[3] = {};
#ifndef _WIN32
    bool dragging_[3] = {};
    int dragX_[3] = {}, dragY_[3] = {};
#endif
};

} // namespace renderer
