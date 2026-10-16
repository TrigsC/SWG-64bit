#include "renderer/Window.h"
#include <cmath>
#include <stdexcept>
#include <string>

namespace renderer {
namespace {
SDL_Window* focusWindow = nullptr;
Window* inputOwner=nullptr;
int inputKey(SDL_Keycode key){
    switch(key){case SDLK_LCTRL:case SDLK_RCTRL:return 0x11;case SDLK_LEFT:return TextFocus::Left;case SDLK_RIGHT:return TextFocus::Right;case SDLK_HOME:return TextFocus::Home;case SDLK_END:return TextFocus::End;case SDLK_DELETE:return TextFocus::Delete;case SDLK_KP_ENTER:return 13;default:break;}
    int value=int(key);if(value>='a'&&value<='z')value+='A'-'a';return value;
}
std::runtime_error failure(const char* operation) { return std::runtime_error(std::string(operation)+": "+SDL_GetError()); }
std::string utf8(const wchar_t* title) {
    std::string result;
    for (; *title; ++title) {
        uint32_t c = static_cast<uint32_t>(*title);
        if(c < 0x80) result += char(c);
        else if(c < 0x800) {result+=char(0xC0|(c>>6));result+=char(0x80|(c&63));}
        else if(c < 0x10000) {result+=char(0xE0|(c>>12));result+=char(0x80|((c>>6)&63));result+=char(0x80|(c&63));}
        else {result+=char(0xF0|(c>>18));result+=char(0x80|((c>>12)&63));result+=char(0x80|((c>>6)&63));result+=char(0x80|(c&63));}
    }
    return result;
}
}
Window::Window(int width, int height, const wchar_t* title) {
    if(focusWindow) throw std::runtime_error("Window: input API supports one window per process");
    if(!SDL_Init(SDL_INIT_VIDEO)) throw failure("SDL_Init");
    // The executable already links Vulkan. Let SDL use that loader instead of
    // a build-tree path that becomes invalid when the app is moved or rebuilt.
    if(!SDL_Vulkan_LoadLibrary(nullptr)) {auto error=failure("SDL_Vulkan_LoadLibrary");SDL_QuitSubSystem(SDL_INIT_VIDEO);throw error;}
    hwnd_=SDL_CreateWindow(utf8(title).c_str(),width,height,SDL_WINDOW_VULKAN|SDL_WINDOW_RESIZABLE|SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if(!hwnd_) {auto error=failure("SDL_CreateWindow");SDL_QuitSubSystem(SDL_INIT_VIDEO);throw error;}
    focusWindow=hwnd_;inputOwner=this;SDL_GetWindowSizeInPixels(hwnd_,&width_,&height_);
}
void Window::setTitle(const wchar_t* title) {SDL_SetWindowTitle(hwnd_,utf8(title).c_str());}
float Window::pixelDensity() const {return SDL_GetWindowPixelDensity(hwnd_);}
Window::~Window() {
    if(focusWindow==hwnd_){focusWindow=nullptr;inputOwner=nullptr;}
    SDL_DestroyWindow(hwnd_);SDL_Vulkan_UnloadLibrary();SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
bool Window::pumpMessages() {
    textFocus_.beginFrame();
    auto syncText=[&](bool previous){if(previous!=textFocus_.active()){if(textFocus_.active())SDL_StartTextInput(hwnd_);else SDL_StopTextInput(hwnd_);}};
    SDL_Event event;
    while(SDL_PollEvent(&event)) {
        const bool wasText=textFocus_.active();
        if(event.type==SDL_EVENT_QUIT || (event.type==SDL_EVENT_WINDOW_CLOSE_REQUESTED&&event.window.windowID==SDL_GetWindowID(hwnd_))) quit_=true;
        if(event.type==SDL_EVENT_WINDOW_FOCUS_LOST&&event.window.windowID==SDL_GetWindowID(hwnd_)) {textFocus_.loseWindowFocus();keyPresses_.clear();actionSelection_.reset();for(int i=0;i<3;++i){dragDx_[i]=dragDy_[i]=0;dragging_[i]=false;}accumDx_=accumDy_=accumWheelDelta_=0;haveLastMouse_=false;}
        if(event.type==SDL_EVENT_KEY_UP&&event.key.windowID==SDL_GetWindowID(hwnd_))textFocus_.keyUp(inputKey(event.key.key));
        if(event.type==SDL_EVENT_KEY_DOWN&&event.key.windowID==SDL_GetWindowID(hwnd_)) {
            int key=inputKey(event.key.key);const bool shortcut=event.key.mod&(SDL_KMOD_CTRL|SDL_KMOD_GUI);
            const bool consumed=textFocus_.keyDown(key,event.key.repeat,shortcut);
            if(consumed)keyPresses_.clear();else if(!event.key.repeat)keyPresses_.insert(key);
            if(wasText&&shortcut&&key=='V'){
                char* text=SDL_GetClipboardText();if(text){textFocus_.insertUtf8(text);SDL_free(text);}
            }
        }
        if(event.type==SDL_EVENT_TEXT_INPUT&&event.text.windowID==SDL_GetWindowID(hwnd_))textFocus_.insertUtf8(event.text.text);
        if((event.type==SDL_EVENT_MOUSE_BUTTON_DOWN||event.type==SDL_EVENT_MOUSE_BUTTON_UP)&&event.button.windowID==SDL_GetWindowID(hwnd_)) {
            int i=event.button.button==SDL_BUTTON_LEFT?0:event.button.button==SDL_BUTTON_RIGHT?1:event.button.button==SDL_BUTTON_MIDDLE?2:-1;
            if(i>=0) {
                float scale=SDL_GetWindowPixelDensity(hwnd_);
                int x=int(std::lround(event.button.x*scale)),y=int(std::lround(event.button.y*scale));
                if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN) {
                    textFocus_.keyDown(0x1000+i);
                    const bool input=x>=textEntryRect_[0]&&x<textEntryRect_[2]&&y>=textEntryRect_[1]&&y<textEntryRect_[3];
                    bool tab=false;
                    if(i==0)for(size_t t=0;t<chatTabRects_.size();++t){const auto& r=chatTabRects_[t];if(x>=r[0]&&x<r[2]&&y>=r[1]&&y<r[3]){chatTabSelection_=t;tab=true;break;}}
                    bool action=false;
                    if(i==0&&!tab)for(size_t a=0;a<actionRects_.size();++a){const auto& r=actionRects_[a];if(x>=r[0]&&x<r[2]&&y>=r[1]&&y<r[3]){actionSelection_=a;action=true;break;}}
                    if(i==0&&!tab&&!action){if(input)textFocus_.focus();else textFocus_.blur();}
                    const bool overChat=x>=chatAreaRect_[0]&&x<chatAreaRect_[2]&&y>=chatAreaRect_[1]&&y<chatAreaRect_[3];
                    if(overChat||tab||action)textFocus_.blockUntilRelease(0x1000+i);
                }else textFocus_.keyUp(0x1000+i);
                // Clicks carry an authoritative position even when SDL has
                // coalesced or omitted the preceding mouse-motion event.
                lastMouseX_=x;lastMouseY_=y;haveLastMouse_=true;
                // A coalesced drag may have no motion event before its release.
                if(event.type==SDL_EVENT_MOUSE_BUTTON_UP&&dragging_[i]) {dragDx_[i]+=x-dragX_[i];dragDy_[i]+=y-dragY_[i];}
                dragX_[i]=x;dragY_[i]=y;dragging_[i]=event.type==SDL_EVENT_MOUSE_BUTTON_DOWN;
            }
        }
        if(event.type==SDL_EVENT_MOUSE_MOTION&&event.motion.windowID==SDL_GetWindowID(hwnd_)) {
            const float scale=SDL_GetWindowPixelDensity(hwnd_);
            int x=int(std::lround(event.motion.x*scale)),y=int(std::lround(event.motion.y*scale));
            if(haveLastMouse_) {accumDx_+=x-lastMouseX_;accumDy_+=y-lastMouseY_;}
            for(int i=0;i<3;++i)if(dragging_[i]) {
                dragDx_[i]+=x-dragX_[i];dragDy_[i]+=y-dragY_[i];
                dragX_[i]=x;dragY_[i]=y;
            }
            lastMouseX_=x;lastMouseY_=y;haveLastMouse_=true;
        }
        if(event.type==SDL_EVENT_MOUSE_WHEEL&&event.wheel.windowID==SDL_GetWindowID(hwnd_)) {
            float delta=event.wheel.y;
            if(event.wheel.direction==SDL_MOUSEWHEEL_FLIPPED) delta=-delta;
            const float density=SDL_GetWindowPixelDensity(hwnd_);
            const int wx=int(std::lround(event.wheel.mouse_x*density)),wy=int(std::lround(event.wheel.mouse_y*density));
            const bool overChat=wx>=chatAreaRect_[0]&&wx<chatAreaRect_[2]&&wy>=chatAreaRect_[1]&&wy<chatAreaRect_[3];
            if(textFocus_.active()||overChat)chatWheel_+=int(std::lround(delta*120));else accumWheelDelta_+=int(std::lround(delta*120));
        }
        syncText(wasText);
        if(textFocus_.blocked()){keyPresses_.clear();accumDx_=accumDy_=0;for(int i=0;i<3;++i)dragDx_[i]=dragDy_[i]=0;}
    }
    if(textFocus_.active()) {
        const float d=SDL_GetWindowPixelDensity(hwnd_);SDL_Rect r{int(textEntryRect_[0]/d),int(textEntryRect_[1]/d),int((textEntryRect_[2]-textEntryRect_[0])/d),int((textEntryRect_[3]-textEntryRect_[1])/d)};
        SDL_SetTextInputArea(hwnd_,&r,0);
    }
    SDL_GetWindowSizeInPixels(hwnd_,&width_,&height_);
    if(SDL_GetWindowFlags(hwnd_)&SDL_WINDOW_MINIMIZED) width_=height_=0;
    return !quit_;
}
bool Window::takeKeyPress(int key) {
    if(SDL_GetKeyboardFocus()!=hwnd_||textFocus_.blocksKey(key)) {keyPresses_.erase(key);return false;}
    return keyPresses_.erase(key)!=0;
}
bool Window::isKeyDown(int key) {
    if(SDL_GetKeyboardFocus()!=focusWindow||!focusWindow||!inputOwner||inputOwner->textFocus_.blocksKey(key)) return false;
    const bool* keys=SDL_GetKeyboardState(nullptr);
    if(key==Control) return keys[SDL_SCANCODE_LCTRL]||keys[SDL_SCANCODE_RCTRL];
    if(key==Escape) return keys[SDL_SCANCODE_ESCAPE];
    if(key>='A'&&key<='Z') key+='a'-'A';
    SDL_Scancode scan=SDL_GetScancodeFromKey(SDL_Keycode(key),nullptr);
    return scan!=SDL_SCANCODE_UNKNOWN&&keys[scan];
}
bool Window::isMouseButtonDown(int button) {
    if(SDL_GetKeyboardFocus()!=focusWindow||!focusWindow||!inputOwner||inputOwner->textFocus_.blocksKey(0x1000+button)) return false;
    int id=button==0?SDL_BUTTON_LEFT:button==1?SDL_BUTTON_RIGHT:SDL_BUTTON_MIDDLE;
    return (SDL_GetMouseState(nullptr,nullptr)&SDL_BUTTON_MASK(id))!=0;
}
void Window::takeMouseDragDelta(int button,int& dx,int& dy) {
    dx=dy=0;if(button<0||button>2)return;
    if(textFocus_.blocksKey(0x1000+button)){dragDx_[button]=dragDy_[button]=0;return;}
    dx=dragDx_[button];dy=dragDy_[button];dragDx_[button]=dragDy_[button]=0;
}
void Window::takeMouseDelta(int& dx,int& dy) {if(textFocus_.blocked()){dx=dy=accumDx_=accumDy_=0;return;}dx=accumDx_;dy=accumDy_;accumDx_=accumDy_=0;}
void Window::cursorPosition(int& x,int& y) const {
#ifdef __APPLE__
    // Cocoa-injected event coordinates can disagree with the physical cursor
    // on macOS 26. SDL's global query reads NSEvent.mouseLocation directly;
    // both global and window positions use screen points, then scale once.
    float globalX=0,globalY=0;int windowX=0,windowY=0;
    SDL_GetGlobalMouseState(&globalX,&globalY);
    SDL_GetWindowPosition(hwnd_,&windowX,&windowY);
    const float scale=SDL_GetWindowPixelDensity(hwnd_);
    x=int(std::lround((globalX-windowX)*scale));
    y=int(std::lround((globalY-windowY)*scale));
#else
    x=lastMouseX_;y=lastMouseY_;
#endif
}
int Window::takeMouseWheelDelta() {int delta=textFocus_.blocked()?0:accumWheelDelta_;accumWheelDelta_=0;return delta;}
}
