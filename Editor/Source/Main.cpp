/**
 * DRX Engine - Visual Editor
 * Main entry point for the visual game editor
 */

#include "EditorApp.h"
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    std::cout << "======================================\n";
    std::cout << "  DRX ENGINE EDITOR v1.0.0\n";
    std::cout << "  Autonomous Game Engine\n";
    std::cout << "======================================\n\n";

    // Editor Configuration
    DRX::Editor::EditorConfig config;
    config.window_title = "DRX Engine Editor";
    config.window_width = 1920;
    config.window_height = 1080;
    config.vsync = true;
    config.dark_mode = true;

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--width" && i + 1 < argc) {
            config.window_width = std::stoi(argv[++i]);
        }
        else if (arg == "--height" && i + 1 < argc) {
            config.window_height = std::stoi(argv[++i]);
        }
        else if (arg == "--no-vsync") {
            config.vsync = false;
        }
        else if (arg == "--light-mode") {
            config.dark_mode = false;
        }
    }

    // Initialize and start editor
    auto& editor = DRX::Editor::EditorApplication::Instance();

    if (!editor.Initialize(config)) {
        std::cerr << "Failed to initialize editor!\n";
        return -1;
    }

    std::cout << "Editor started!\n\n";
    std::cout << "CONTROLS:\n";
    std::cout << "  Right Mouse + Drag = Rotate camera\n";
    std::cout << "  W/A/S/D = Move camera\n";
    std::cout << "  Mouse Wheel = Zoom\n";
    std::cout << "  Shift = Move faster\n\n";
    std::cout << "SHORTCUTS:\n";
    std::cout << "  Ctrl+N = New scene\n";
    std::cout << "  Ctrl+S = Save scene\n";
    std::cout << "  Ctrl+D = Duplicate\n";
    std::cout << "  Delete = Delete object\n";
    std::cout << "  W/E/R = Translate/Rotate/Scale\n\n";

    // Main loop
    editor.Run();

    // Cleanup
    editor.Shutdown();

    std::cout << "Editor closed.\n";
    return 0;
}
