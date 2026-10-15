#include "Process.h"
#include <array>
#include <algorithm>

namespace launcher {
void Process::start(const std::filesystem::path& executable, const std::vector<std::wstring>& arguments,
        const std::filesystem::path& directory, std::vector<unsigned char>& frame) {
    struct Clear { std::vector<unsigned char>& bytes; ~Clear() { if (!bytes.empty()) SecureZeroMemory(bytes.data(), bytes.size()); bytes.clear(); } } clear{frame};
    if (active()) throw std::runtime_error("A client process is already running");
    if (!std::filesystem::is_regular_file(executable)) throw std::runtime_error("Client helper is missing; build the Windows renderer target first");
    if (frame.size() > 8200) throw std::runtime_error("Credential frame exceeds limit");
    Handle inputRead, inputWrite, outputRead, outputWrite;
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE read = nullptr, write = nullptr;
    if (!CreatePipe(&read, &write, &security, 16384)) throw std::runtime_error("Cannot create private input pipe");
    inputRead.reset(read); inputWrite.reset(write);
    if (!SetHandleInformation(write, HANDLE_FLAG_INHERIT, 0)) throw std::runtime_error("Cannot protect private input pipe");
    if (!CreatePipe(&read, &write, &security, 65536)) throw std::runtime_error("Cannot create client output pipe");
    outputRead.reset(read); outputWrite.reset(write);
    if (!SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0)) throw std::runtime_error("Cannot protect client output pipe");
    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    std::vector<unsigned char> storage(size);
    auto attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &size)) throw std::runtime_error("Cannot prepare child handles");
    struct Attributes { LPPROC_THREAD_ATTRIBUTE_LIST value; ~Attributes() { DeleteProcThreadAttributeList(value); } } cleanup{attributes};
    HANDLE inherited[]{inputRead.value, outputWrite.value};
    if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr))
        throw std::runtime_error("Cannot restrict child handle inheritance");
    Handle job(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job.value || !SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
        throw std::runtime_error("Cannot create client process group");
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = inputRead.value;
    startup.StartupInfo.hStdOutput = startup.StartupInfo.hStdError = outputWrite.value;
    startup.lpAttributeList = attributes;
    std::wstring command = quote(executable.wstring());
    for (const auto& argument : arguments) command += L" " + quote(argument);
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
            EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW | CREATE_SUSPENDED,
            nullptr, directory.c_str(), &startup.StartupInfo, &info))
        throw std::runtime_error("Could not start the client helper");
    Handle child(info.hProcess), thread(info.hThread);
    if (!AssignProcessToJobObject(job.value, child.value) || ResumeThread(thread.value) == DWORD(-1)) {
        TerminateProcess(child.value, 1);
        throw std::runtime_error("Cannot manage the client process lifetime");
    }
    inputRead.reset(); outputWrite.reset();
    DWORD written = 0;
    if (!frame.empty() && (!WriteFile(inputWrite.value, frame.data(), static_cast<DWORD>(frame.size()), &written, nullptr) || written != frame.size())) {
        TerminateJobObject(job.value, 1);
        throw std::runtime_error("Private credential transfer failed");
    }
    inputWrite.reset();
    process_.reset(child.value); child.value = nullptr;
    output_.reset(outputRead.value); outputRead.value = nullptr;
    job_.reset(job.value); job.value = nullptr;
    pending_.clear(); dropping_ = false;
}
void Process::consume(std::string_view bytes, const std::function<void(const std::string&)>& line) {
    for (char ch : bytes) {
        if (ch == '\n') {
            if (!dropping_) { if (!pending_.empty() && pending_.back() == '\r') pending_.pop_back(); line(pending_); }
            pending_.clear(); dropping_ = false;
        } else if (!dropping_) {
            if (pending_.size() == 4096) { pending_.clear(); dropping_ = true; }
            else pending_ += ch;
        }
    }
}
std::optional<DWORD> Process::poll(const std::function<void(const std::string&)>& line) {
    if (!active()) return {};
    std::array<char, 4096> buffer{};
    for (size_t total = 0; total < 65536;) {
        DWORD available = 0, count = 0;
        if (!PeekNamedPipe(output_.value, nullptr, 0, nullptr, &available, nullptr) || available == 0) break;
        if (!ReadFile(output_.value, buffer.data(), (std::min)(available, static_cast<DWORD>(buffer.size())), &count, nullptr) || count == 0) break;
        consume(std::string_view(buffer.data(), count), line); total += count;
    }
    if (WaitForSingleObject(process_.value, 0) != WAIT_OBJECT_0) return {};
    // A noisy exited child may have more than one timer tick's drain budget.
    // Keep draining rather than discarding trailing completion records.
    DWORD remaining = 0;
    if (PeekNamedPipe(output_.value, nullptr, 0, nullptr, &remaining, nullptr) && remaining) return {};
    DWORD exit = 1; GetExitCodeProcess(process_.value, &exit);
    if (!pending_.empty() && !dropping_) line(pending_);
    process_.reset(); output_.reset(); job_.reset(); pending_.clear();
    return exit;
}
void Process::stop() {
    if (job_.value) TerminateJobObject(job_.value, 1);
}
}
