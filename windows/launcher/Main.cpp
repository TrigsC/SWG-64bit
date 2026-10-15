#include "Process.h"
#include <ShlObj.h>
#include <shellapi.h>
#include <chrono>

namespace {
constexpr UINT_PTR timerId = 1;
enum Id { Installation = 100, Browse, Host, Port, User, Password, Characters, List, Connect, Check, Stop, Status };
std::filesystem::path modulePath() {
    std::wstring path(32768, L'\0');
    auto count = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!count || count == path.size()) throw std::runtime_error("Cannot locate launcher");
    path.resize(count); return path;
}
std::wstring text(HWND window, int id) {
    HWND field = GetDlgItem(window, id);
    int length = GetWindowTextLengthW(field);
    std::wstring value(length + 1, L'\0');
    GetWindowTextW(field, value.data(), length + 1); value.resize(length); return value;
}
class App {
    HWND window_ = nullptr;
    launcher::Process child_;
    std::filesystem::path build_, helper_;
    std::vector<clientcommon::LauncherCharacter> characters_;
    launcher::Mode mode_ = launcher::Mode::Check;
    std::wstring listingRoot_, listingHost_, listingPort_, listingUser_;
    bool listed_ = false, worldReady_ = false, checked_ = false, stopped_ = false;
    bool smoke_ = false, smokeMarker_ = false;
    ULONGLONG deadline_ = 0;
    int result_ = 1;
    void message(const std::wstring& value) { SetDlgItemTextW(window_, Status, value.c_str()); }
    void controls(bool busy) {
        for (int id : {Installation, Browse, Host, Port, User, Password, Characters, List, Connect, Check})
            EnableWindow(GetDlgItem(window_, id), !busy);
        EnableWindow(GetDlgItem(window_, Stop), busy);
    }
    void clearCharacters() {
        characters_.clear(); listed_ = false;
        SendDlgItemMessageW(window_, Characters, CB_RESETCONTENT, 0, 0);
    }
    void loadSettings() {
        clearCharacters();
        auto settings = launcher::readSettings(text(window_, Installation));
        SetDlgItemTextW(window_, Host, settings.host.c_str());
        SetDlgItemTextW(window_, Port, settings.port.c_str());
    }
    void browse() {
        BROWSEINFOW info{}; info.hwndOwner = window_;
        info.lpszTitle = L"Select your existing SWGEmu installation";
        info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
        auto item = SHBrowseForFolderW(&info);
        if (!item) return;
        wchar_t path[MAX_PATH]{};
        if (SHGetPathFromIDListW(item, path)) {
            SetDlgItemTextW(window_, Installation, path); loadSettings();
        }
        CoTaskMemFree(item);
    }
    void start(launcher::Mode mode) {
        if (child_.active()) return;
        auto root = text(window_, Installation), host = text(window_, Host), port = text(window_, Port);
        if (!launcher::ownedInstallation(root)) throw std::runtime_error("Choose an SWG folder containing SWGEmu.exe and .tre archives");
        std::optional<clientcommon::LauncherCharacter> character;
        if (mode == launcher::Mode::World) {
            if (!listed_ || root != listingRoot_ || host != listingHost_ || port != listingPort_ || text(window_, User) != listingUser_)
                throw std::runtime_error("Load characters for this installation and server first");
            auto index = SendDlgItemMessageW(window_, Characters, CB_GETCURSEL, 0, 0);
            if (index != CB_ERR && static_cast<size_t>(index) < characters_.size()) character = characters_[index];
        }
        auto arguments = launcher::arguments(root, host, port, mode, character);
        std::vector<unsigned char> frame;
        if (mode != launcher::Mode::Check) {
            auto user = launcher::utf8(text(window_, User));
            auto passwordWide = text(window_, Password);
            auto password = launcher::utf8(passwordWide);
            struct Clear {
                std::wstring& wide; std::string& bytes;
                ~Clear() { if (!wide.empty()) SecureZeroMemory(wide.data(), wide.size() * sizeof(wchar_t)); if (!bytes.empty()) SecureZeroMemory(bytes.data(), bytes.size()); }
            } clear{passwordWide, password};
            frame = clientcommon::credentialFrame(user, password);
        }
        if (mode == launcher::Mode::Characters) clearCharacters();
        child_.start(helper_, arguments, root, frame);
        if (mode != launcher::Mode::Check) SetDlgItemTextW(window_, Password, L"");
        mode_ = mode; stopped_ = worldReady_ = checked_ = false;
        if (mode == launcher::Mode::Characters) { listingRoot_ = root; listingHost_ = host; listingPort_ = port; listingUser_ = text(window_, User); }
        controls(true);
        message(mode == launcher::Mode::Check ? L"Checking server..." : mode == launcher::Mode::Characters ? L"Loading existing characters..." : L"Connecting to the world...");
        deadline_ = GetTickCount64() + 90000;
    }
    void line(const std::string& value) {
        // Discard raw helper output: account IDs, server details and error text
        // are not a launcher log. Only bounded protocol records become UI state.
        if (smoke_) { if (value == "WINDOW_SMOKE_PASS (Vulkan not initialized)") smokeMarker_ = true; return; }
        if (mode_ == launcher::Mode::Characters) {
            if (auto character = clientcommon::parseCharacterRecord(value)) {
                if (characters_.size() >= 256) throw std::runtime_error("Character list exceeds launcher limit");
                auto label = launcher::wide(character->name) + L" (galaxy " + std::to_wstring(character->galaxy) + L")";
                characters_.push_back(*character);
                SendDlgItemMessageW(window_, Characters, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
            }
            if (value == "NATIVE_CHARACTER_LIST_COMPLETE") listed_ = true;
        }
        if (value.starts_with("NATIVE_WORLD_READY")) { worldReady_ = true; message(L"Connected. Close the world window or click Disconnect to leave."); }
        if (value.starts_with("NATIVE_CONNECTION_CHECK ")) checked_ = true;
    }
    void tick() {
        if (!child_.active()) return;
        if (GetTickCount64() > deadline_ && (smoke_ || mode_ != launcher::Mode::World || !worldReady_)) {
            child_.stop(); stopped_ = true;
        }
        auto exit = child_.poll([&](const auto& value) { line(value); });
        if (!exit) return;
        controls(false);
        if (smoke_) {
            result_ = *exit == 0 && smokeMarker_ && !stopped_ ? 0 : 1;
            DestroyWindow(window_); return;
        }
        if (stopped_) { clearCharacters(); message(L"Client stopped. Enter the password again to reconnect."); }
        else if (*exit != 0) { clearCharacters(); message(L"Client failed (exit " + std::to_wstring(*exit) + L"). Check the installation, server, credentials and Vulkan driver."); }
        else if (mode_ == launcher::Mode::Characters) {
            if (!listed_) { clearCharacters(); message(L"Character list did not complete. Try again."); }
            else if (characters_.empty()) message(L"No existing characters were returned for this account.");
            else { SendDlgItemMessageW(window_, Characters, CB_SETCURSEL, 0, 0); message(L"Choose an existing character and re-enter the password to connect."); }
        } else if (mode_ == launcher::Mode::Check) message(checked_ ? L"Server handshake succeeded." : L"Server check exited without confirmation.");
        else message(worldReady_ ? L"World closed. Enter the password again to reconnect." : L"Client exited before the world was ready.");
    }
    void create() {
        auto root = build_.parent_path().parent_path().parent_path();
        auto control = [&](const wchar_t* type, const wchar_t* value, DWORD style, int id, int x, int y, int w, int h) {
            auto handle = CreateWindowExW(std::wstring_view(type) == L"EDIT" ? WS_EX_CLIENTEDGE : 0,
                type, value, WS_CHILD | WS_VISIBLE | style, x, y, w, h, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
            if (!handle) throw std::runtime_error("Cannot create launcher control");
            SendMessageW(handle, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
            if (std::wstring_view(type) == L"EDIT") SendMessageW(handle, EM_SETLIMITTEXT, 4096, 0);
        };
        control(L"STATIC", L"SWG Native Windows Client — development build", 0, 0, 22, 18, 610, 24);
        int y = 55;
        auto row = [&](const wchar_t* label, const wchar_t* value, int id, DWORD extra = 0) {
            control(L"STATIC", label, 0, 0, 22, y + 3, 100, 24);
            control(L"EDIT", value, WS_TABSTOP | ES_AUTOHSCROLL | extra, id, 132, y, id == Installation ? 388 : 490, 25); y += 38;
        };
        row(L"SWG folder", root.c_str(), Installation);
        control(L"BUTTON", L"Browse...", WS_TABSTOP, Browse, 530, 55, 92, 25);
        row(L"Server", L"", Host); row(L"Port", L"44453", Port); row(L"Account", L"", User); row(L"Password", L"", Password, ES_PASSWORD);
        control(L"STATIC", L"Character", 0, 0, 22, y + 3, 100, 24);
        control(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, Characters, 132, y, 490, 180); y += 45;
        control(L"BUTTON", L"Check server", WS_TABSTOP, Check, 22, y, 135, 30);
        control(L"BUTTON", L"Load characters", WS_TABSTOP, List, 167, y, 145, 30);
        control(L"BUTTON", L"Connect", WS_TABSTOP | BS_DEFPUSHBUTTON, Connect, 322, y, 145, 30);
        control(L"BUTTON", L"Disconnect", WS_TABSTOP, Stop, 477, y, 145, 30); y += 50;
        control(L"STATIC", L"Choose your installation and load existing characters.", 0, Status, 22, y, 600, 62);
        control(L"STATIC", L"Passwords are sent through a private pipe and cleared after each request.\nSettings and credentials are not saved. Use only a server that permits this client.", 0, 0, 22, y + 68, 600, 52);
        controls(false); loadSettings();
        SetTimer(window_, timerId, 50, nullptr);
        if (smoke_) {
            std::vector<unsigned char> frame;
            child_.start(build_ / "Debug/windows-client-smoke.exe", {L"--window-only"}, build_, frame);
            deadline_ = GetTickCount64() + 10000;
        }
    }
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM w, LPARAM l) {
        auto self = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) { self = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); self->window_ = window; SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); }
        if (!self) return DefWindowProcW(window, message, w, l);
        try {
            switch (message) {
            case WM_CREATE: self->create(); return 0;
            case WM_COMMAND:
                switch (LOWORD(w)) {
                case Browse: self->browse(); break;
                case List: self->start(launcher::Mode::Characters); break;
                case Connect: self->start(launcher::Mode::World); break;
                case Check: self->start(launcher::Mode::Check); break;
                case Stop: self->stopped_ = true; self->child_.stop(); break;
                }
                return 0;
            case WM_TIMER: self->tick(); return 0;
            case WM_CLOSE: self->child_.stop(); DestroyWindow(window); return 0;
            case WM_DESTROY: KillTimer(window, timerId); PostQuitMessage(self->smoke_ ? self->result_ : 0); return 0;
            }
        } catch (const std::exception& error) {
            self->child_.stop(); self->message(launcher::wide(error.what()));
            if (message == WM_CREATE) return -1;
            if (self->smoke_) { self->result_ = 1; DestroyWindow(window); }
        }
        return DefWindowProcW(window, message, w, l);
    }
public:
    int run(HINSTANCE instance, bool smoke) {
        smoke_ = smoke;
        build_ = modulePath().parent_path().parent_path();
        helper_ = build_ / "swg/tools/dummyclient/Debug/dummyclient.exe";
        WNDCLASSW klass{}; klass.hInstance = instance; klass.lpfnWndProc = procedure;
        klass.lpszClassName = L"SWGWindowsLauncher"; klass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        klass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        if (!RegisterClassW(&klass)) return 1;
        HWND window = CreateWindowW(klass.lpszClassName, L"SWG Native Windows Client", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
            CW_USEDEFAULT, CW_USEDEFAULT, 665, 530, nullptr, nullptr, instance, this);
        if (!window) return 1;
        ShowWindow(window, SW_SHOW);
        MSG message{};
        int code;
        while ((code = GetMessageW(&message, nullptr, 0, 0)) > 0)
            if (!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
        return code < 0 ? 1 : static_cast<int>(message.wParam);
    }
};
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR command, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int result = 1;
    try { App app; result = app.run(instance, std::wstring_view(command) == L"--smoke-test"); }
    catch (...) { MessageBoxW(nullptr, L"Could not start the Windows launcher.", L"SWG Native Client", MB_OK | MB_ICONERROR); }
    CoUninitialize(); return result;
}
