#pragma once
#include <istream>
#include <string>
#include <cstdint>
namespace clientcommon {
struct LoginCredentials {std::string username,password;};
// Private stdin pipe: uint32 BE username length + password length, followed
// by UTF-8 bytes. Bounded fields; passwords never enter process arguments.
LoginCredentials readLoginCredentials(std::istream& input);
void installClientCancellationHandler();
bool clientCancellationRequested();
bool takeMovementDiagnosticRequest();
uint16_t parseLoginPort(const std::string& value);
}
