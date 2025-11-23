/**
 * DRX Engine - Visual Editor
 * Hauptprogramm für den visuellen Spiele-Editor
 */

#include "EditorApp.h"
#include "Core/Public/CoreTypes.h"
#include "Core/Public/Log.h"

int main(int argc, char** argv) {
    DRX_UNUSED(argc);
    DRX_UNUSED(argv);

    DRX_LOG_INFO("======================================");
    DRX_LOG_INFO("  DRX ENGINE EDITOR v1.0.0");
    DRX_LOG_INFO("======================================");

    // Editor Konfiguration
    DRX::Editor::EditorConfig config;
    config.window_title = "DRX Engine Editor";
    config.window_width = 1920;
    config.window_height = 1080;
    config.vsync = true;
    config.dark_mode = true;

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        DRX::String arg = argv[i];

        if (arg == "--project" && i + 1 < argc) {
            config.last_project_path = argv[++i];
        }
        else if (arg == "--scene" && i + 1 < argc) {
            config.last_scene_path = argv[++i];
        }
        else if (arg == "--width" && i + 1 < argc) {
            config.window_width = std::stoi(argv[++i]);
        }
        else if (arg == "--height" && i + 1 < argc) {
            config.window_height = std::stoi(argv[++i]);
        }
    }

    // Editor initialisieren und starten
    auto& editor = DRX::Editor::EditorApplication::Instance();

    if (!editor.Initialize(config)) {
        DRX_LOG_ERROR("Editor konnte nicht initialisiert werden!");
        return -1;
    }

    DRX_LOG_INFO("Editor gestartet!");
    DRX_LOG_INFO("");
    DRX_LOG_INFO("STEUERUNG:");
    DRX_LOG_INFO("  Rechte Maus + Ziehen = Kamera drehen");
    DRX_LOG_INFO("  W/A/S/D = Kamera bewegen");
    DRX_LOG_INFO("  Mausrad = Zoom");
    DRX_LOG_INFO("  Shift = Schneller bewegen");
    DRX_LOG_INFO("");
    DRX_LOG_INFO("SHORTCUTS:");
    DRX_LOG_INFO("  Ctrl+N = Neue Szene");
    DRX_LOG_INFO("  Ctrl+O = Szene öffnen");
    DRX_LOG_INFO("  Ctrl+S = Szene speichern");
    DRX_LOG_INFO("  Ctrl+Z = Rückgängig");
    DRX_LOG_INFO("  Delete = Objekt löschen");
    DRX_LOG_INFO("  W/E/R = Verschieben/Rotieren/Skalieren");
    DRX_LOG_INFO("");

    // Hauptschleife
    editor.Run();

    // Aufräumen
    editor.Shutdown();

    DRX_LOG_INFO("Editor beendet.");
    return 0;
}
