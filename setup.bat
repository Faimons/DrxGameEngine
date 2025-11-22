@echo off
REM DRX Engine - Windows Setup Script
REM Prüft und installiert alle Abhängigkeiten für VS Code

setlocal enabledelayedexpansion

echo.
echo ╔═══════════════════════════════════════════════════════════╗
echo ║           DRX ENGINE - SETUP SCRIPT v1.0                  ║
echo ║     Automatische Installation aller Abhängigkeiten        ║
echo ╚═══════════════════════════════════════════════════════════╝
echo.

REM Prüfe Admin-Rechte
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo [WARNUNG] Dieses Script benoetigt Administrator-Rechte fuer einige Installationen.
    echo           Starte das Script als Administrator fuer vollstaendige Installation.
    echo.
)

echo ════════════════════════════════════════════════════════════
echo   SCHRITT 1: Pruefe vorhandene Tools
echo ════════════════════════════════════════════════════════════
echo.

REM Prüfe Git
where git >nul 2>&1
if %errorlevel% equ 0 (
    echo [OK] Git gefunden
    git --version
) else (
    echo [X] Git NICHT gefunden
    set MISSING_GIT=1
)

REM Prüfe CMake
where cmake >nul 2>&1
if %errorlevel% equ 0 (
    echo [OK] CMake gefunden
    cmake --version | findstr /R "version"
) else (
    echo [X] CMake NICHT gefunden
    set MISSING_CMAKE=1
)

REM Prüfe Python
where python >nul 2>&1
if %errorlevel% equ 0 (
    echo [OK] Python gefunden
    python --version
) else (
    echo [X] Python NICHT gefunden
    set MISSING_PYTHON=1
)

REM Prüfe VS Code
where code >nul 2>&1
if %errorlevel% equ 0 (
    echo [OK] VS Code gefunden
) else (
    echo [X] VS Code NICHT gefunden
    set MISSING_VSCODE=1
)

REM Prüfe Visual Studio / MSVC
where cl >nul 2>&1
if %errorlevel% equ 0 (
    echo [OK] MSVC Compiler gefunden
) else (
    echo [!] MSVC Compiler nicht im PATH - Visual Studio installiert?
)

echo.
echo ════════════════════════════════════════════════════════════
echo   SCHRITT 2: Installation fehlender Tools
echo ════════════════════════════════════════════════════════════
echo.

REM Prüfe ob winget verfügbar ist
where winget >nul 2>&1
if %errorlevel% neq 0 (
    echo [WARNUNG] winget nicht gefunden. Bitte installiere Tools manuell:
    echo   - Git: https://git-scm.com/download/win
    echo   - CMake: https://cmake.org/download/
    echo   - Python: https://www.python.org/downloads/
    echo   - VS Code: https://code.visualstudio.com/
    echo   - Visual Studio: https://visualstudio.microsoft.com/
    echo   - Vulkan SDK: https://vulkan.lunarg.com/sdk/home
    goto :skip_install
)

if defined MISSING_GIT (
    echo Installiere Git...
    winget install Git.Git --silent --accept-package-agreements
)

if defined MISSING_CMAKE (
    echo Installiere CMake...
    winget install Kitware.CMake --silent --accept-package-agreements
)

if defined MISSING_PYTHON (
    echo Installiere Python...
    winget install Python.Python.3.11 --silent --accept-package-agreements
)

if defined MISSING_VSCODE (
    echo Installiere VS Code...
    winget install Microsoft.VisualStudioCode --silent --accept-package-agreements
)

echo.
echo Installiere Vulkan SDK...
winget install KhronosGroup.VulkanSDK --silent --accept-package-agreements 2>nul

:skip_install

echo.
echo ════════════════════════════════════════════════════════════
echo   SCHRITT 3: VS Code Extensions
echo ════════════════════════════════════════════════════════════
echo.

where code >nul 2>&1
if %errorlevel% equ 0 (
    echo Installiere VS Code Extensions...
    code --install-extension ms-vscode.cpptools --force
    code --install-extension ms-vscode.cmake-tools --force
    code --install-extension twxs.cmake --force
    code --install-extension ms-python.python --force
    code --install-extension slevesque.shader --force
    echo [OK] VS Code Extensions installiert
) else (
    echo [!] VS Code nicht gefunden - Extensions koennen nicht installiert werden
)

echo.
echo ════════════════════════════════════════════════════════════
echo   SCHRITT 4: Python Umgebung
echo ════════════════════════════════════════════════════════════
echo.

if not exist "venv" (
    echo Erstelle Python Virtual Environment...
    python -m venv venv
)

echo Aktiviere Virtual Environment...
call venv\Scripts\activate.bat

echo Installiere Python Abhaengigkeiten...
pip install --upgrade pip
pip install anthropic aiohttp pyyaml jinja2 httpx

echo [OK] Python Umgebung eingerichtet

echo.
echo ════════════════════════════════════════════════════════════
echo   SCHRITT 5: VS Code Konfiguration
echo ════════════════════════════════════════════════════════════
echo.

if not exist ".vscode" mkdir .vscode

REM settings.json
(
echo {
echo     "cmake.configureOnOpen": true,
echo     "cmake.buildDirectory": "${workspaceFolder}/build",
echo     "C_Cpp.default.configurationProvider": "ms-vscode.cmake-tools",
echo     "C_Cpp.default.cppStandard": "c++17",
echo     "files.associations": {
echo         "*.h": "cpp",
echo         "*.vert": "glsl",
echo         "*.frag": "glsl"
echo     },
echo     "python.defaultInterpreterPath": "${workspaceFolder}/venv/Scripts/python.exe"
echo }
) > .vscode\settings.json
echo [OK] .vscode\settings.json erstellt

REM launch.json
(
echo {
echo     "version": "0.2.0",
echo     "configurations": [
echo         {
echo             "name": "Debug DRX Editor",
echo             "type": "cppvsdbg",
echo             "request": "launch",
echo             "program": "${workspaceFolder}/build/bin/Release/DRXEditor.exe",
echo             "args": [],
echo             "cwd": "${workspaceFolder}",
echo             "preLaunchTask": "CMake: build"
echo         },
echo         {
echo             "name": "Debug FPS Demo",
echo             "type": "cppvsdbg",
echo             "request": "launch",
echo             "program": "${workspaceFolder}/build/bin/Release/FPSDemo.exe",
echo             "args": [],
echo             "cwd": "${workspaceFolder}",
echo             "preLaunchTask": "CMake: build"
echo         }
echo     ]
echo }
) > .vscode\launch.json
echo [OK] .vscode\launch.json erstellt

echo.
echo ════════════════════════════════════════════════════════════
echo   SCHRITT 6: Projekt bauen
echo ════════════════════════════════════════════════════════════
echo.

set /p BUILD_NOW="Moechtest du das Projekt jetzt bauen? (j/n): "
if /i "%BUILD_NOW%"=="j" (
    if not exist "build" mkdir build
    cd build

    echo Konfiguriere CMake...
    cmake .. -DCMAKE_BUILD_TYPE=Release

    echo Baue Projekt...
    cmake --build . --config Release --parallel

    if %errorlevel% equ 0 (
        echo [OK] Build erfolgreich!
    ) else (
        echo [X] Build fehlgeschlagen.
    )
    cd ..
)

echo.
echo ════════════════════════════════════════════════════════════
echo   SETUP ABGESCHLOSSEN!
echo ════════════════════════════════════════════════════════════
echo.
echo Naechste Schritte:
echo   1. Oeffne VS Code: code .
echo   2. Druecke Ctrl+Shift+B zum Bauen
echo   3. Druecke F5 zum Debuggen
echo.
echo Oder starte direkt:
echo   build\bin\Release\FPSDemo.exe
echo.

pause
