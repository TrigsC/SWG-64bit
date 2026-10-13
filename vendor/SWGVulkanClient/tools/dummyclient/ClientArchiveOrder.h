#pragma once
#include <string>
#include <vector>
namespace dummyclient {
// Read the user's searchTree order: higher-numbered patches override base archives.
// Legacy installations without this config retain the supplied upstream defaults.
std::vector<std::string> clientArchiveOrder(const std::string& root,
                                          const std::vector<std::string>& fallback);
}
