// Synthetic platform probe: never reads game files or opens network sockets.
#include "renderer/Window.h"
#include "renderer/VulkanRenderer.h"
#include <cstring>
#include <exception>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    if (argc != 2 || (std::strcmp(argv[1], "--window-only") != 0 &&
                      std::strcmp(argv[1], "--render") != 0)) {
        std::cerr << "Usage: windows-client-smoke --window-only | --render\n";
        return 2;
    }
    try {
        renderer::Window window(640, 480, L"SWG Windows x64 platform smoke");
        if (!window.pumpMessages()) throw std::runtime_error("Window closed before smoke");
        if (std::strcmp(argv[1], "--window-only") == 0) {
            std::cout << "WINDOW_SMOKE_PASS (Vulkan not initialized)\n";
            return 0;
        }
        renderer::VulkanRenderer graphics(window.handle(), window.width(), window.height());
        std::cout << graphics.capabilityReport() << '\n';
        for (int frame = 0; frame < 3; ++frame) {
            if (!window.pumpMessages()) throw std::runtime_error("Window closed before presentation");
            graphics.beginFrame(0.08f, 0.12f, 0.18f);
            graphics.endFrame();
        }
        std::cout << "RENDER_SMOKE_PASS (three synthetic frames)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "WINDOWS_SMOKE_FAIL: " << error.what() << '\n';
        return 1;
    }
}
