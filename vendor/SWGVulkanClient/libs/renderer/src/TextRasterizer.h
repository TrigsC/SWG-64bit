#pragma once
#include <string>
#include <vector>
#include <cstdint>
namespace renderer {
std::vector<uint8_t> rasterizeText(const std::wstring& text, int& width, int& height);
}
