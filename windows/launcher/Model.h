#pragma once
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>
#include "clientcommon/LoginInput.h"
#include "clientcommon/LauncherProtocol.h"

namespace launcher {
inline std::string utf8(std::wstring_view value) {
    if (value.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (!size) throw std::runtime_error("Invalid Unicode input");
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}
inline std::wstring wide(std::string_view value) {
    if (value.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (!size) throw std::runtime_error("Invalid UTF-8 input");
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}
// Quote for the Windows CRT argv parser; shell execution is never used.
inline std::wstring quote(std::wstring_view value) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (wchar_t ch : value) {
        if (ch == L'\\') { ++slashes; continue; }
        result.append(ch == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        result += ch; slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    return result + L'"';
}
inline void validateHost(std::wstring_view host) {
    if (host.empty() || host.size() > 253) throw std::runtime_error("Enter a server hostname or IP address");
    for (wchar_t ch : host)
        if (!((ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9') ||
              ch == L'.' || ch == L'-' || ch == L':' || ch == L'[' || ch == L']' || ch == L'_'))
            throw std::runtime_error("Use a hostname or IP address without spaces or a URL scheme");
}
inline bool ownedInstallation(const std::filesystem::path& root) {
    if (!std::filesystem::is_regular_file(root / "SWGEmu.exe")) return false;
    for (const auto& entry : std::filesystem::directory_iterator(root))
        if (entry.is_regular_file() && entry.path().extension() == L".tre") return true;
    return false;
}
struct Settings { std::wstring host; std::wstring port = L"44453"; };
inline Settings readSettings(const std::filesystem::path& root) {
    Settings result;
    std::ifstream stream(root / "swgemu_login.cfg");
    std::string line;
    size_t bytes = 0;
    auto trim = [](std::string value) {
        auto first = value.find_first_not_of(" \r\n\t"), last = value.find_last_not_of(" \r\n\t");
        return first == value.npos ? std::string{} : value.substr(first, last - first + 1);
    };
    while (std::getline(stream, line) && (bytes += line.size()) <= 65536) {
        auto split = line.find('=');
        if (split == line.npos) continue;
        auto key = trim(line.substr(0, split)), value = trim(line.substr(split + 1));
        if (key == "loginServerAddress0") result.host = wide(value);
        if (key == "loginServerPort0") result.port = wide(value);
    }
    return result;
}
enum class Mode { Check, Characters, World };
inline std::vector<std::wstring> arguments(const std::filesystem::path& root, std::wstring host,
        std::wstring port, Mode mode, const std::optional<clientcommon::LauncherCharacter>& character = {}) {
    validateHost(host);
    clientcommon::parseLoginPort(utf8(port));
    std::vector<std::wstring> result{L"--host", std::move(host), L"--port", std::move(port)};
    if (mode == Mode::Check) result.push_back(L"--connection-check");
    else {
        result.insert(result.end(), {L"--credentials-stdin", L"--client-path", root.wstring()});
        if (mode == Mode::Characters) result.push_back(L"--list-characters");
        else {
            if (!character) throw std::runtime_error("Load and select an existing character first");
            result.insert(result.end(), {L"--native-client", L"--character-id", std::to_wstring(character->id), L"--galaxy-id", std::to_wstring(character->galaxy)});
        }
    }
    return result;
}
}
