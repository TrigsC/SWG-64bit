#pragma once
#include "Model.h"
#include <functional>

namespace launcher {
struct Handle {
    HANDLE value = nullptr;
    Handle() = default;
    explicit Handle(HANDLE value) : value(value) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() { reset(); }
    void reset(HANDLE next = nullptr) { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); value = next; }
};
class Process {
    Handle process_, output_, job_;
    std::string pending_;
    bool dropping_ = false;
    void consume(std::string_view bytes, const std::function<void(const std::string&)>& line);
public:
    ~Process() { stop(); }
    bool active() const { return process_.value != nullptr; }
    void start(const std::filesystem::path& executable, const std::vector<std::wstring>& arguments,
               const std::filesystem::path& directory, std::vector<unsigned char>& frame);
    std::optional<DWORD> poll(const std::function<void(const std::string&)>& line);
    void stop();
};
}
