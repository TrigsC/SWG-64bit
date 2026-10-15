#include "Process.h"
#include <shellapi.h>
#include <fcntl.h>
#include <io.h>
#include <iostream>
#include <sstream>
#include <algorithm>

namespace {
void require(bool condition, const char* reason) { if (!condition) throw std::runtime_error(reason); }
template<class Function> void rejects(Function action) {
    bool rejected = false; try { action(); } catch (const std::exception&) { rejected = true; }
    require(rejected, "Invalid input was accepted");
}
std::filesystem::path self() {
    wchar_t path[32768]{}; GetModuleFileNameW(nullptr, path, 32768); return path;
}
DWORD finish(launcher::Process& process, std::string& output) {
    const auto deadline = GetTickCount64() + 5000;
    while (GetTickCount64() < deadline) {
        auto exit = process.poll([&](const auto& line) { output += line + "\n"; });
        if (exit) return *exit;
        Sleep(10);
    }
    process.stop(); throw std::runtime_error("Process fixture timed out");
}
int fixture(int argc, wchar_t** argv) {
    // Inspect actual CRT argv, including backslashes and quotes, rather than
    // comparing the quoting implementation to itself.
    if (argc != 5 || std::wstring_view(argv[2]) != L"path with spaces\\" ||
        std::wstring_view(argv[3]) != L"embedded\"quote" || std::wstring_view(argv[4]) != L"") return 3;
    _setmode(_fileno(stdin), _O_BINARY);
    auto credentials = clientcommon::readLoginCredentials(std::cin);
    if (credentials.username != "Synthetic account" || credentials.password != std::string("A\r\n\x1a" "Z")) return 4;
    std::cout << "HANDOFF_PASS\n";
    return 0;
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc > 1 && std::wstring_view(argv[1]) == L"--fixture") return fixture(argc, argv);
        if (argc > 1 && std::wstring_view(argv[1]) == L"--fail") return 7;
        if (argc > 1 && std::wstring_view(argv[1]) == L"--hang") { Sleep(INFINITE); return 1; }
        launcher::validateHost(L"example.invalid"); launcher::validateHost(L"[::1]");
        rejects([] { launcher::validateHost(L"https://example.invalid"); });
        rejects([] { launcher::validateHost(L"server --password injected"); });
        rejects([] { clientcommon::parseLoginPort("0"); });
        rejects([] { clientcommon::parseLoginPort("65536"); });
        rejects([] { clientcommon::credentialFrame("", "secret"); });
        rejects([] { clientcommon::credentialFrame("user", std::string(4097, 'a')); });
        rejects([] { clientcommon::credentialFrame("user", std::string("a\0b", 3)); });
        const std::string name = "Synthetic \xc3\xa9";
        auto character = clientcommon::parseCharacterRecord(clientcommon::characterRecord(42, 0x100000001, name));
        require(character && character->name == name && character->galaxy == 42 && character->id == 0x100000001, "Character record roundtrip failed");
        require(!clientcommon::parseCharacterRecord("NATIVE_CHARACTER 1 1 0a"), "Control character accepted");
        require(!clientcommon::parseCharacterRecord("NATIVE_CHARACTER 9999999999 1 61"), "Invalid galaxy accepted");
        require(!clientcommon::parseCharacterRecord("NATIVE_CHARACTER 1 1 ffz"), "Invalid hex accepted");
        require(!clientcommon::parseCharacterRecord("NATIVE_CHARACTER 1 18446744073709551616 61"), "Overflowing character ID accepted");
        require(clientcommon::launcherName(u"Synthetic \u00e9") == name, "Unicode character name changed");
        auto args = launcher::arguments(L"synthetic folder", L"example.invalid", L"44453", launcher::Mode::World, character);
        require(std::find(args.begin(), args.end(), L"--credentials-stdin") != args.end(), "Private stdin mode missing");
        require(std::find(args.begin(), args.end(), L"--galaxy-id") != args.end(), "Galaxy selection missing");
        require(std::find(args.begin(), args.end(), L"--password") == args.end(), "Password flag exposed");
        rejects([] { launcher::arguments(L"synthetic", L"example.invalid", L"44453", launcher::Mode::World); });
        launcher::Process process;
        auto frame = clientcommon::credentialFrame("Synthetic account", std::string("A\r\n\x1a" "Z"));
        process.start(self(), {L"--fixture", L"path with spaces\\", L"embedded\"quote", L""}, self().parent_path(), frame);
        require(frame.empty(), "Credential frame was retained");
        std::string output;
        require(finish(process, output) == 0 && output == "HANDOFF_PASS\n", "Private process handoff failed");
        process.start(self(), {L"--fail"}, self().parent_path(), frame);
        require(finish(process, output) == 7, "Child exit status was lost");
        process.start(self(), {L"--hang"}, self().parent_path(), frame); process.stop();
        require(finish(process, output) != 0, "Cancellation did not stop the child");
        frame = clientcommon::credentialFrame("Synthetic account", "Synthetic password");
        rejects([&] { process.start(self().parent_path() / "missing-helper.exe", {}, self().parent_path(), frame); });
        require(frame.empty(), "Failed spawn retained credential frame");
        std::cout << "LAUNCHER_TESTS_PASS\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
