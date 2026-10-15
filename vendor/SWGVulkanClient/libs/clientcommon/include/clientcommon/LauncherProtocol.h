#pragma once
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace clientcommon {
inline std::vector<unsigned char> credentialFrame(std::string_view user, std::string_view password) {
    for (auto field : {user, password})
        if (field.empty() || field.size() > 4096 || field.find('\0') != field.npos)
            throw std::runtime_error("Enter an account and password (up to 4096 UTF-8 bytes each)");
    std::vector<unsigned char> frame;
    for (auto field : {user, password}) {
        const auto size = static_cast<uint32_t>(field.size());
        for (int shift = 24; shift >= 0; shift -= 8) frame.push_back(static_cast<unsigned char>(size >> shift));
    }
    frame.insert(frame.end(), user.begin(), user.end());
    frame.insert(frame.end(), password.begin(), password.end());
    return frame;
}

struct LauncherCharacter { uint32_t galaxy; uint64_t id; std::string name; };
// Opt-in helper records contain no login token. Hex makes each name one
// bounded token, including spaces and non-ASCII UTF-8, without log injection.
inline std::string characterRecord(uint32_t galaxy, uint64_t id, std::string_view name) {
    if (name.empty() || name.size() > 1024) throw std::runtime_error("Invalid character name length");
    std::string result = "NATIVE_CHARACTER " + std::to_string(galaxy) + " " + std::to_string(id) + " ";
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char ch : name) { result += hex[ch >> 4]; result += hex[ch & 15]; }
    return result;
}
inline std::optional<LauncherCharacter> parseCharacterRecord(std::string_view line) {
    constexpr std::string_view prefix = "NATIVE_CHARACTER ";
    if (!line.starts_with(prefix) || line.size() > 2100) return {};
    line.remove_prefix(prefix.size());
    auto split = line.find(' ');
    if (split == line.npos || split == 0 || split > 10) return {};
    uint64_t galaxy = 0;
    for (char ch : line.substr(0, split)) {
        if (ch < '0' || ch > '9') return {};
        galaxy = galaxy * 10 + ch - '0';
    }
    // The current helper's --galaxy-id parser accepts signed int values.
    if (galaxy > std::numeric_limits<int>::max()) return {};
    line.remove_prefix(split + 1);
    split = line.find(' ');
    if (split == line.npos || split == 0 || split > 20) return {};
    uint64_t id = 0;
    for (char ch : line.substr(0, split)) {
        if (ch < '0' || ch > '9' || id > (std::numeric_limits<uint64_t>::max() - (ch - '0')) / 10) return {};
        id = id * 10 + ch - '0';
    }
    if (id == 0) return {};
    line.remove_prefix(split + 1);
    if (line.empty() || line.size() % 2 || line.size() > 2048) return {};
    auto digit = [](char ch) { return ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : -1; };
    std::string name;
    for (size_t i = 0; i < line.size(); i += 2) {
        const int high = digit(line[i]), low = digit(line[i + 1]);
        if (high < 0 || low < 0) return {};
        unsigned char ch = static_cast<unsigned char>(high * 16 + low);
        if (ch < 32 || ch == 127) return {};
        name += static_cast<char>(ch);
    }
    return LauncherCharacter{static_cast<uint32_t>(galaxy), id, std::move(name)};
}
inline std::string launcherName(const std::u16string& name) {
    std::string result;
    for (size_t i = 0; i < name.size(); ++i) {
        uint32_t code = name[i];
        if (code >= 0xd800 && code <= 0xdbff) {
            if (++i >= name.size() || name[i] < 0xdc00 || name[i] > 0xdfff) throw std::runtime_error("Invalid Unicode character name");
            code = 0x10000 + ((code - 0xd800) << 10) + name[i] - 0xdc00;
        } else if (code >= 0xdc00 && code <= 0xdfff) throw std::runtime_error("Invalid Unicode character name");
        if (code < 0x80) result += static_cast<char>(code);
        else if (code < 0x800) { result += static_cast<char>(0xc0 | (code >> 6)); result += static_cast<char>(0x80 | (code & 63)); }
        else if (code < 0x10000) { result += static_cast<char>(0xe0 | (code >> 12)); result += static_cast<char>(0x80 | ((code >> 6) & 63)); result += static_cast<char>(0x80 | (code & 63)); }
        else { result += static_cast<char>(0xf0 | (code >> 18)); result += static_cast<char>(0x80 | ((code >> 12) & 63)); result += static_cast<char>(0x80 | ((code >> 6) & 63)); result += static_cast<char>(0x80 | (code & 63)); }
    }
    return result;
}
}
