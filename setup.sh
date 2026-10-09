#!/bin/bash
#
# DRX Engine - Automatisches Setup Script
# Prüft und installiert alle Abhängigkeiten für VS Code
#

set -e

# Farben für Output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}"
echo "╔═══════════════════════════════════════════════════════════╗"
echo "║           DRX ENGINE - SETUP SCRIPT v1.0                  ║"
echo "║     Automatische Installation aller Abhängigkeiten        ║"
echo "╚═══════════════════════════════════════════════════════════╝"
echo -e "${NC}"

# Betriebssystem erkennen
detect_os() {
    if [[ "$OSTYPE" == "linux-gnu"* ]]; then
        if [ -f /etc/debian_version ]; then
            OS="debian"
        elif [ -f /etc/redhat-release ]; then
            OS="redhat"
        elif [ -f /etc/arch-release ]; then
            OS="arch"
        else
            OS="linux"
        fi
    elif [[ "$OSTYPE" == "darwin"* ]]; then
        OS="macos"
    elif [[ "$OSTYPE" == "msys" ]] || [[ "$OSTYPE" == "cygwin" ]] || [[ "$OSTYPE" == "win32" ]]; then
        OS="windows"
    else
        OS="unknown"
    fi
    echo -e "${BLUE}[INFO]${NC} Erkanntes Betriebssystem: ${GREEN}$OS${NC}"
}

# Funktion zum Prüfen ob Befehl existiert
check_command() {
    if command -v "$1" &> /dev/null; then
        echo -e "${GREEN}[✓]${NC} $1 gefunden: $(command -v $1)"
        return 0
    else
        echo -e "${RED}[✗]${NC} $1 NICHT gefunden"
        return 1
    fi
}

# Funktion zum Prüfen der Version
check_version() {
    local cmd=$1
    local min_version=$2
    local current_version=$($cmd --version 2>/dev/null | head -1 | grep -oE '[0-9]+\.[0-9]+' | head -1)

    if [ -n "$current_version" ]; then
        echo -e "    Version: ${YELLOW}$current_version${NC}"
    fi
}

echo ""
echo -e "${YELLOW}════════════════════════════════════════════════════════════${NC}"
echo -e "${YELLOW}  SCHRITT 1: Prüfe vorhandene Tools${NC}"
echo -e "${YELLOW}════════════════════════════════════════════════════════════${NC}"
echo ""

detect_os

MISSING_TOOLS=()

# Prüfe wichtige Tools
echo ""
echo -e "${BLUE}[Compiler & Build Tools]${NC}"

if ! check_command "gcc"; then
    MISSING_TOOLS+=("gcc")
fi
check_version "gcc" "9.0"

if ! check_command "g++"; then
    MISSING_TOOLS+=("g++")
fi

if ! check_command "cmake"; then
    MISSING_TOOLS+=("cmake")
else
    check_version "cmake" "3.16"
fi

if ! check_command "make"; then
    MISSING_TOOLS+=("make")
fi

check_command "ninja" || true  # Optional

echo ""
echo -e "${BLUE}[Vulkan & Grafik]${NC}"

if ! check_command "vulkaninfo"; then
    MISSING_TOOLS+=("vulkan")
fi

if ! check_command "glslangValidator"; then
    MISSING_TOOLS+=("glslang")
fi

echo ""
echo -e "${BLUE}[Python]${NC}"

if ! check_command "python3"; then
    MISSING_TOOLS+=("python3")
else
    check_version "python3" "3.9"
fi

if ! check_command "pip3"; then
    MISSING_TOOLS+=("pip3")
fi

echo ""
echo -e "${BLUE}[VS Code]${NC}"

if ! check_command "code"; then
    MISSING_TOOLS+=("vscode")
else
    echo -e "    ${BLUE}Installierte Extensions:${NC}"
    code --list-extensions 2>/dev/null | grep -E "(cpptools|cmake|python)" | while read ext; do
        echo -e "    ${GREEN}✓${NC} $ext"
    done
fi

echo ""
echo -e "${BLUE}[Git]${NC}"
check_command "git"

# Zusammenfassung fehlender Tools
echo ""
echo -e "${YELLOW}════════════════════════════════════════════════════════════${NC}"
echo -e "${YELLOW}  SCHRITT 2: Fehlende Abhängigkeiten${NC}"
echo -e "${YELLOW}════════════════════════════════════════════════════════════${NC}"
echo ""

if [ ${#MISSING_TOOLS[@]} -eq 0 ]; then
    echo -e "${GREEN}[✓] Alle wichtigen Tools sind installiert!${NC}"
else
    echo -e "${RED}Fehlende Tools: ${MISSING_TOOLS[*]}${NC}"
    echo ""

    read -p "Möchtest du die fehlenden Tools jetzt installieren? (j/n): " -n 1 -r
    echo ""

    if [[ $REPLY =~ ^[Jj]$ ]]; then
        echo ""
        echo -e "${BLUE}[INFO]${NC} Installiere fehlende Abhängigkeiten..."

        case $OS in
            debian)
                sudo apt-get update
                sudo apt-get install -y \
                    build-essential \
                    cmake \
                    git \
                    ninja-build \
                    pkg-config \
                    libvulkan-dev \
                    vulkan-tools \
                    vulkan-validationlayers \
                    glslang-tools \
                    spirv-tools \
                    libopenal-dev \
                    libx11-dev \
                    libxrandr-dev \
                    libxinerama-dev \
                    libxcursor-dev \
                    libxi-dev \
                    libgl1-mesa-dev \
                    libglu1-mesa-dev \
                    python3 \
                    python3-pip \
                    python3-venv
                ;;
            arch)
                sudo pacman -S --needed \
                    base-devel \
                    cmake \
                    git \
                    ninja \
                    vulkan-devel \
                    vulkan-tools \
                    vulkan-validation-layers \
                    glslang \
                    openal \
                    libx11 \
                    libxrandr \
                    libxinerama \
                    libxcursor \
                    libxi \
                    mesa \
                    glu \
                    python \
                    python-pip
                ;;
            macos)
                brew install \
                    cmake \
                    ninja \
                    git \
                    vulkan-headers \
                    vulkan-loader \
                    molten-vk \
                    glslang \
                    openal-soft \
                    python@3.11
                ;;
            *)
                echo -e "${RED}[FEHLER]${NC} Automatische Installation für $OS nicht unterstützt."
                echo "Bitte installiere die Tools manuell. Siehe INSTALL.md"
                ;;
        esac
    fi
fi

echo ""
echo -e "${YELLOW}════════════════════════════════════════════════════════════${NC}"
echo -e "${YELLOW}  SCHRITT 3: VS Code Extensions${NC}"
echo -e "${YELLOW}════════════════════════════════════════════════════════════${NC}"
echo ""

if check_command "code"; then
    REQUIRED_EXTENSIONS=(
        "ms-vscode.cpptools"
        "ms-vscode.cmake-tools"
        "twxs.cmake"
        "ms-python.python"
        "slevesque.shader"
    )

    INSTALLED_EXTENSIONS=$(code --list-extensions 2>/dev/null)

    for ext in "${REQUIRED_EXTENSIONS[@]}"; do
        if echo "$INSTALLED_EXTENSIONS" | grep -q "$ext"; then
            echo -e "${GREEN}[✓]${NC} $ext bereits installiert"
        else
            echo -e "${YELLOW}[→]${NC} Installiere $ext..."
            code --install-extension "$ext" --force 2>/dev/null || echo -e "${RED}    Fehler beim Installieren${NC}"
        fi
    done
else
    echo -e "${YELLOW}[!]${NC} VS Code nicht gefunden. Bitte installiere VS Code von:"
    echo "    https://code.visualstudio.com/"
fi

echo ""
echo -e "${YELLOW}════════════════════════════════════════════════════════════${NC}"
echo -e "${YELLOW}  SCHRITT 4: Python Umgebung${NC}"
echo -e "${YELLOW}════════════════════════════════════════════════════════════${NC}"
echo ""

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ -d "$SCRIPT_DIR/AI" ]; then
    cd "$SCRIPT_DIR"

    if [ ! -d "venv" ]; then
        echo -e "${BLUE}[INFO]${NC} Erstelle Python Virtual Environment..."
        python3 -m venv venv
    fi

    echo -e "${BLUE}[INFO]${NC} Aktiviere Virtual Environment..."
    source venv/bin/activate

    echo -e "${BLUE}[INFO]${NC} Installiere Python Abhängigkeiten..."
    pip install --upgrade pip

    if [ -f "AI/requirements.txt" ]; then
        pip install -r AI/requirements.txt
    else
        pip install anthropic aiohttp pyyaml jinja2 httpx
    fi

    echo -e "${GREEN}[✓]${NC} Python Umgebung eingerichtet"
else
    echo -e "${YELLOW}[!]${NC} AI Ordner nicht gefunden"
fi

echo ""
echo -e "${YELLOW}════════════════════════════════════════════════════════════${NC}"
echo -e "${YELLOW}  SCHRITT 5: VS Code Konfiguration${NC}"
echo -e "${YELLOW}════════════════════════════════════════════════════════════${NC}"
echo ""

# Erstelle .vscode Ordner und Konfigurationen
mkdir -p "$SCRIPT_DIR/.vscode"

# settings.json
cat > "$SCRIPT_DIR/.vscode/settings.json" << 'SETTINGS'
{
    "cmake.configureOnOpen": true,
    "cmake.buildDirectory": "${workspaceFolder}/build",
    "cmake.generator": "Ninja",
    "C_Cpp.default.configurationProvider": "ms-vscode.cmake-tools",
    "C_Cpp.default.cppStandard": "c++17",
    "C_Cpp.default.intelliSenseMode": "linux-gcc-x64",
    "files.associations": {
        "*.h": "cpp",
        "*.hpp": "cpp",
        "*.vert": "glsl",
        "*.frag": "glsl",
        "*.glsl": "glsl"
    },
    "editor.formatOnSave": true,
    "editor.tabSize": 4,
    "files.trimTrailingWhitespace": true,
    "python.defaultInterpreterPath": "${workspaceFolder}/venv/bin/python"
}
SETTINGS
echo -e "${GREEN}[✓]${NC} .vscode/settings.json erstellt"

# launch.json
cat > "$SCRIPT_DIR/.vscode/launch.json" << 'LAUNCH'
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "Debug DRX Editor",
            "type": "cppdbg",
            "request": "launch",
            "program": "${workspaceFolder}/build/bin/DRXEditor",
            "args": [],
            "stopAtEntry": false,
            "cwd": "${workspaceFolder}",
            "environment": [],
            "externalConsole": false,
            "MIMode": "gdb",
            "setupCommands": [
                {
                    "description": "Enable pretty-printing",
                    "text": "-enable-pretty-printing",
                    "ignoreFailures": true
                }
            ],
            "preLaunchTask": "CMake: build"
        },
        {
            "name": "Debug FPS Demo",
            "type": "cppdbg",
            "request": "launch",
            "program": "${workspaceFolder}/build/bin/FPSDemo",
            "args": [],
            "stopAtEntry": false,
            "cwd": "${workspaceFolder}",
            "environment": [],
            "externalConsole": false,
            "MIMode": "gdb",
            "preLaunchTask": "CMake: build"
        }
    ]
}
LAUNCH
echo -e "${GREEN}[✓]${NC} .vscode/launch.json erstellt"

# tasks.json
cat > "$SCRIPT_DIR/.vscode/tasks.json" << 'TASKS'
{
    "version": "2.0.0",
    "tasks": [
        {
            "label": "CMake: configure",
            "type": "shell",
            "command": "cmake",
            "args": ["-B", "build", "-DCMAKE_BUILD_TYPE=Debug"],
            "group": "build",
            "problemMatcher": []
        },
        {
            "label": "CMake: build",
            "type": "shell",
            "command": "cmake",
            "args": ["--build", "build", "--parallel"],
            "group": {
                "kind": "build",
                "isDefault": true
            },
            "problemMatcher": ["$gcc"]
        },
        {
            "label": "CMake: clean",
            "type": "shell",
            "command": "cmake",
            "args": ["--build", "build", "--target", "clean"],
            "problemMatcher": []
        },
        {
            "label": "Run FPS Demo",
            "type": "shell",
            "command": "./build/bin/FPSDemo",
            "problemMatcher": [],
            "dependsOn": "CMake: build"
        }
    ]
}
TASKS
echo -e "${GREEN}[✓]${NC} .vscode/tasks.json erstellt"

# c_cpp_properties.json
cat > "$SCRIPT_DIR/.vscode/c_cpp_properties.json" << 'CPPPROPS'
{
    "configurations": [
        {
            "name": "Linux",
            "includePath": [
                "${workspaceFolder}/**",
                "${workspaceFolder}/Engine/Source/**",
                "${workspaceFolder}/ThirdParty/**",
                "/usr/include",
                "/usr/local/include"
            ],
            "defines": [
                "DRX_PLATFORM_LINUX",
                "DRX_DEBUG"
            ],
            "compilerPath": "/usr/bin/g++",
            "cStandard": "c17",
            "cppStandard": "c++17",
            "intelliSenseMode": "linux-gcc-x64",
            "configurationProvider": "ms-vscode.cmake-tools"
        }
    ],
    "version": 4
}
CPPPROPS
echo -e "${GREEN}[✓]${NC} .vscode/c_cpp_properties.json erstellt"

echo ""
echo -e "${YELLOW}════════════════════════════════════════════════════════════${NC}"
echo -e "${YELLOW}  SCHRITT 6: Projekt bauen${NC}"
echo -e "${YELLOW}════════════════════════════════════════════════════════════${NC}"
echo ""

read -p "Möchtest du das Projekt jetzt bauen? (j/n): " -n 1 -r
echo ""

if [[ $REPLY =~ ^[Jj]$ ]]; then
    cd "$SCRIPT_DIR"
    mkdir -p build
    cd build

    echo -e "${BLUE}[INFO]${NC} Konfiguriere CMake..."
    cmake .. -DCMAKE_BUILD_TYPE=Debug -G "Ninja" 2>/dev/null || cmake .. -DCMAKE_BUILD_TYPE=Debug

    echo -e "${BLUE}[INFO]${NC} Baue Projekt..."
    cmake --build . --parallel

    if [ $? -eq 0 ]; then
        echo -e "${GREEN}[✓]${NC} Build erfolgreich!"
    else
        echo -e "${RED}[✗]${NC} Build fehlgeschlagen. Prüfe die Fehlermeldungen oben."
    fi
fi

echo ""
echo -e "${GREEN}════════════════════════════════════════════════════════════${NC}"
echo -e "${GREEN}  SETUP ABGESCHLOSSEN!${NC}"
echo -e "${GREEN}════════════════════════════════════════════════════════════${NC}"
echo ""
echo -e "Nächste Schritte:"
echo -e "  1. Öffne VS Code: ${YELLOW}code $SCRIPT_DIR${NC}"
echo -e "  2. Drücke ${YELLOW}Ctrl+Shift+B${NC} zum Bauen"
echo -e "  3. Drücke ${YELLOW}F5${NC} zum Debuggen"
echo ""
echo -e "Oder starte direkt:"
echo -e "  ${YELLOW}./build/bin/FPSDemo${NC}"
echo ""
