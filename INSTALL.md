# DRX Engine - Installation Guide

## System Requirements

### Minimum Requirements
- **OS:** Windows 10/11, Ubuntu 20.04+, or macOS 12+
- **CPU:** 4 cores, 2.5 GHz
- **RAM:** 8 GB
- **GPU:** Vulkan 1.2 compatible (NVIDIA GTX 1060 / AMD RX 580 or better)
- **Storage:** 5 GB free space
- **Python:** 3.9+

### Recommended Requirements
- **CPU:** 8 cores, 3.5 GHz
- **RAM:** 16 GB
- **GPU:** NVIDIA RTX 2070 / AMD RX 5700 or better
- **Storage:** 20 GB SSD

---

## IDE Setup

### Option 1: Visual Studio Code (Recommended)

**Version:** VS Code 1.80+ (Latest stable recommended)

**Download:** https://code.visualstudio.com/

**Required Extensions:**
```
1. C/C++ (Microsoft) - ms-vscode.cpptools
2. CMake Tools - ms-vscode.cmake-tools
3. CMake - twxs.cmake
4. Python - ms-python.python
5. Shader languages support - slevesque.shader
```

**Install Extensions via Terminal:**
```bash
code --install-extension ms-vscode.cpptools
code --install-extension ms-vscode.cmake-tools
code --install-extension twxs.cmake
code --install-extension ms-python.python
code --install-extension slevesque.shader
```

### Option 2: Visual Studio 2022 (Windows)

**Version:** Visual Studio 2022 (Community, Professional, or Enterprise)

**Download:** https://visualstudio.microsoft.com/vs/

**Required Workloads:**
- Desktop development with C++
- Game development with C++
- Python development (optional)

**Individual Components:**
- CMake tools for Windows
- Vulkan SDK
- Windows 10/11 SDK

### Option 3: CLion (Cross-platform)

**Version:** CLion 2023.2+

**Download:** https://www.jetbrains.com/clion/

---

## Dependencies Installation

### Ubuntu/Debian Linux

```bash
# Update package list
sudo apt-get update

# Install build essentials
sudo apt-get install -y \
    build-essential \
    cmake \
    git \
    ninja-build \
    pkg-config

# Install Vulkan SDK
sudo apt-get install -y \
    libvulkan-dev \
    vulkan-tools \
    vulkan-validationlayers-dev \
    spirv-tools

# Install GLFW (window management)
sudo apt-get install -y libglfw3-dev

# Install other dependencies
sudo apt-get install -y \
    libassimp-dev \
    libbullet-dev \
    libopenal-dev \
    liblua5.4-dev \
    libfreetype-dev \
    rapidjson-dev

# Install Python
sudo apt-get install -y python3 python3-pip python3-venv

# Verify installations
cmake --version
vulkaninfo | head -20
python3 --version
```

### Windows

**1. Install Visual Studio 2022:**
- Download from https://visualstudio.microsoft.com/
- Select "Desktop development with C++"

**2. Install Vulkan SDK:**
```powershell
# Download from https://vulkan.lunarg.com/sdk/home
# Or use winget:
winget install KhronosGroup.VulkanSDK
```

**3. Install CMake:**
```powershell
winget install Kitware.CMake
```

**4. Install Python:**
```powershell
winget install Python.Python.3.11
```

**5. Install vcpkg (Package Manager):**
```powershell
git clone https://github.com/Microsoft/vcpkg.git C:\vcpkg
cd C:\vcpkg
.\bootstrap-vcpkg.bat
.\vcpkg integrate install

# Install dependencies
.\vcpkg install glfw3:x64-windows
.\vcpkg install assimp:x64-windows
.\vcpkg install bullet3:x64-windows
.\vcpkg install openal-soft:x64-windows
.\vcpkg install lua:x64-windows
.\vcpkg install freetype:x64-windows
.\vcpkg install rapidjson:x64-windows
```

### macOS

```bash
# Install Homebrew (if not installed)
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install dependencies
brew install cmake ninja git
brew install vulkan-headers vulkan-loader molten-vk
brew install glfw assimp bullet openal-soft lua freetype rapidjson

# Install Python
brew install python@3.11

# Install Xcode Command Line Tools
xcode-select --install
```

---

## Python Environment Setup

```bash
# Navigate to project directory
cd /path/to/DrxGameEngine

# Create virtual environment
python3 -m venv venv

# Activate virtual environment
# Linux/macOS:
source venv/bin/activate
# Windows:
.\venv\Scripts\activate

# Install Python dependencies
pip install -r AI/requirements.txt

# If requirements.txt doesn't exist, install manually:
pip install anthropic aiohttp pyyaml jinja2 httpx
```

---

## Building the Engine

### Using CMake (All Platforms)

```bash
# Navigate to project root
cd /path/to/DrxGameEngine

# Create build directory
mkdir build && cd build

# Configure (Debug)
cmake .. -DCMAKE_BUILD_TYPE=Debug

# Configure (Release)
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build . --parallel $(nproc)

# Or with Ninja (faster)
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja
```

### Using VS Code

1. Open the project folder in VS Code
2. Install recommended extensions when prompted
3. Press `Ctrl+Shift+P` and select "CMake: Configure"
4. Select your compiler (GCC, Clang, or MSVC)
5. Press `F7` to build

### Using Visual Studio 2022

1. Open Visual Studio 2022
2. Select "Open a local folder"
3. Navigate to DrxGameEngine folder
4. Visual Studio will auto-detect CMakeLists.txt
5. Select configuration (Debug/Release)
6. Build -> Build All (`Ctrl+Shift+B`)

---

## Running the Engine

### Run the Editor
```bash
# From build directory
./bin/DRXEditor

# Or on Windows
.\bin\Release\DRXEditor.exe
```

### Run the FPS Demo
```bash
./bin/FPSDemo
```

### Run Tests
```bash
./bin/DRXTests
# Or using ctest
ctest --output-on-failure
```

---

## Project Structure After Build

```
DrxGameEngine/
├── build/
│   ├── bin/           # Executables
│   │   ├── DRXEditor
│   │   ├── FPSDemo
│   │   └── DRXTests
│   └── lib/           # Libraries
│       ├── libDRXCore.a
│       ├── libDRXRenderer.a
│       └── ...
├── Assets/            # Game assets
│   ├── Shaders/
│   ├── Textures/
│   └── Models/
└── ...
```

---

## Configuration

### VS Code Settings (`.vscode/settings.json`)

```json
{
    "cmake.configureOnOpen": true,
    "cmake.buildDirectory": "${workspaceFolder}/build",
    "cmake.generator": "Ninja",
    "C_Cpp.default.configurationProvider": "ms-vscode.cmake-tools",
    "C_Cpp.default.cppStandard": "c++17",
    "files.associations": {
        "*.h": "cpp",
        "*.vert": "glsl",
        "*.frag": "glsl"
    }
}
```

### VS Code Launch Configuration (`.vscode/launch.json`)

```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "Debug DRXEditor",
            "type": "cppdbg",
            "request": "launch",
            "program": "${workspaceFolder}/build/bin/DRXEditor",
            "args": [],
            "cwd": "${workspaceFolder}",
            "environment": [],
            "externalConsole": false,
            "MIMode": "gdb",
            "setupCommands": [
                {
                    "description": "Enable pretty-printing",
                    "text": "-enable-pretty-printing"
                }
            ]
        },
        {
            "name": "Debug FPSDemo",
            "type": "cppdbg",
            "request": "launch",
            "program": "${workspaceFolder}/build/bin/FPSDemo",
            "args": [],
            "cwd": "${workspaceFolder}",
            "environment": [],
            "externalConsole": false,
            "MIMode": "gdb"
        }
    ]
}
```

---

## Troubleshooting

### Vulkan Not Found
```bash
# Check Vulkan installation
vulkaninfo

# Set environment variable
export VULKAN_SDK=/path/to/vulkan/sdk
export PATH=$VULKAN_SDK/bin:$PATH
export LD_LIBRARY_PATH=$VULKAN_SDK/lib:$LD_LIBRARY_PATH
```

### CMake Can't Find Dependencies
```bash
# Specify vcpkg toolchain (Windows)
cmake .. -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake

# Or set CMAKE_PREFIX_PATH
cmake .. -DCMAKE_PREFIX_PATH=/usr/local
```

### Shader Compilation Fails
```bash
# Install glslc (part of Vulkan SDK)
# Verify installation
glslc --version

# Compile shaders manually
glslc -fshader-stage=vertex pbr.vert -o pbr.vert.spv
glslc -fshader-stage=fragment pbr.frag -o pbr.frag.spv
```

### Python Import Errors
```bash
# Ensure virtual environment is activated
source venv/bin/activate

# Reinstall dependencies
pip install --upgrade -r AI/requirements.txt
```

---

## Quick Start Commands

```bash
# Clone and setup (one-liner)
git clone <repo-url> DrxGameEngine && cd DrxGameEngine && \
    mkdir build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release && \
    cmake --build . --parallel

# Run everything
./bin/DRXEditor
```

---

## Environment Variables

| Variable | Description | Example |
|----------|-------------|---------|
| `VULKAN_SDK` | Path to Vulkan SDK | `/usr/local/vulkan` |
| `DRX_ASSETS_PATH` | Custom assets path | `./Assets` |
| `DRX_LOG_LEVEL` | Logging verbosity | `DEBUG`, `INFO`, `WARN`, `ERROR` |
| `ANTHROPIC_API_KEY` | Claude API key for AI | `sk-ant-...` |

---

## Next Steps

1. Read the README.md for project overview
2. Explore the `Samples/FPSDemo` for example code
3. Check `Engine/Source/` for engine architecture
4. Start building your game!

---

## Support

- GitHub Issues: Report bugs and feature requests
- Documentation: See `Docs/` folder
- Community: Discord (coming soon)

**Happy Game Development!**
