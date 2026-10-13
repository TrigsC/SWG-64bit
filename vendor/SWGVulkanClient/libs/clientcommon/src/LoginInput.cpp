#include "clientcommon/LoginInput.h"
#include <array>
#include <stdexcept>
#include <csignal>
namespace clientcommon {
namespace {volatile std::sig_atomic_t cancellation=0;void cancel(int){cancellation=1;}}
namespace {volatile std::sig_atomic_t movementDiagnostic=0;void movementTest(int){movementDiagnostic=1;}}
void installClientCancellationHandler() {cancellation=0;std::signal(SIGINT,cancel);std::signal(SIGTERM,cancel);
#ifdef SIGUSR1
std::signal(SIGUSR1,movementTest);
#endif
}
bool takeMovementDiagnosticRequest(){if(!movementDiagnostic)return false;movementDiagnostic=0;return true;}
bool clientCancellationRequested(){return cancellation!=0;}

LoginCredentials readLoginCredentials(std::istream& input) {
    std::array<unsigned char,8> header{};
    if(!input.read(reinterpret_cast<char*>(header.data()),header.size()))throw std::runtime_error("Incomplete login input");
    auto length=[&](int offset) {uint32_t value=0;for(int i=0;i<4;++i)value=(value<<8)|header[offset+i];return value;};
    const auto usernameLength=length(0),passwordLength=length(4);
    if(usernameLength==0||usernameLength>4096||passwordLength==0||passwordLength>4096)throw std::runtime_error("Invalid login field length");
    LoginCredentials result;result.username.resize(usernameLength);result.password.resize(passwordLength);
    if(!input.read(result.username.data(),usernameLength)||!input.read(result.password.data(),passwordLength))throw std::runtime_error("Incomplete login fields");
    if(result.username.find('\0')!=std::string::npos||result.password.find('\0')!=std::string::npos)throw std::runtime_error("Invalid login field encoding");
    return result;
}
uint16_t parseLoginPort(const std::string& value) {
    if(value.empty())throw std::runtime_error("Login port must be between 1 and 65535");
    uint32_t port=0;for(unsigned char ch:value) {
        if(ch<'0'||ch>'9'||port>6553)throw std::runtime_error("Login port must be between 1 and 65535");
        port=port*10+(ch-'0');
    }
    if(port==0||port>65535)throw std::runtime_error("Login port must be between 1 and 65535");return uint16_t(port);
}
}
