# OpenTIC

<p align="center">
  <strong>Next-Generation Fantasy Console with Integrated AI Assistant & Retro Toolchain</strong>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/OpenTIC-v1.2.0-blue.svg" alt="OpenTIC Version" />
  <img src="https://img.shields.io/badge/Lua-5.3-blue.svg" alt="Lua 5.3" />
  <img src="https://img.shields.io/badge/AI%20Assistant-Integrated%20(F6)-brightgreen.svg" alt="AI Copilot" />
  <img src="https://img.shields.io/badge/License-MIT-green.svg" alt="License" />
</p>

---

## 🌟 Overview

**OpenTIC** is a modern fantasy console and game creation environment evolved from the classic fantasy console ecosystem. Designed for crafting, playing, and sharing tiny retro games, OpenTIC natively embeds an autonomous **AI Game Development Assistant** directly into the console interface.

Whether you are sketching a quick prototype, generating pixel sprites, designing maps, or refactoring Lua game logic, OpenTIC gives you a full suite of retro creation tools augmented by modern AI capabilities.

---

## ✨ Key Features

### 🎨 Unified Studio Visuals & Source Han Sans Typography
* **Unified 512x288 High-Resolution Studio**: All studio interfaces (Code Editor, Sprite Editor, Map Editor, SFX Editor, Music Editor, Console, and AI Copilot) share a unified 512x288 buffer with crisp 2x scaling.
* **Source Han Sans (思源黑体)**: Non-antialiased retro 1-bit rasterization of Source Han Sans across the top toolbar, mode indicators, and the AI Assistant for seamless Chinese and English rendering.
* **Zero Window Jumping**: Running your game (`TIC_RUN_MODE`) executes at the native 240x136 fantasy console canvas while preserving the outer window dimensions and position without any resizing or jitter.

### 🤖 Native AI Game Copilot (Press `F6` or type `ai`)
* **In-Console AI Assistant**: Press `F6` anytime in the studio or in-game to summon the AI assistant.
* **Full Code & Asset Manipulation**: The AI can directly edit Lua game code and manipulate cartridge asset blocks (`<TILES>`, `<SPRITES>`, `<MAP>`, `<PALETTE>`, `<SFX>`).
* **Token & Speed Optimized Bridge**: Intelligent cartridge context separation prevents uploading massive raw asset hex chunks (`<MAP>`, `<TILES>`, etc.) unless asset edits are requested, cutting LLM token usage by up to 90% and speeding up response times dramatically.
* **Instant Hot-Reload**: Generated code and assets are immediately injected into memory—test your changes instantly without restarting the console.
* **Interactive Slash Commands (`/`)**:
  - `/key <provider> <key>`: Configure AI API keys (supports DeepSeek, OpenAI, Anthropic, Gemini, Groq, etc.) with instant persistence.
  - `/model <model_name>`: Switch active LLM models on the fly.
  - `/status`: Inspect active model, configured providers, and bridge status.
  - `/undo`: One-click instant cartridge rollback to the previous checkpoint.
  - `/clear`: Clear chat history.
* **Extended Multi-Step Timeout**: 60-minute task execution threshold allows complex, iterative game generation and extensive refactoring without premature cancellation.

### 🎮 Retro Fantasy Console Specifications
* **Display**: 240x136 pixels, 16 configurable colors palette.
* **Sprites**: 256 8x8 background tiles and 256 8x8 foreground sprites (with flip, rotate, and scale).
* **Map**: 240x136 cells (1920x1088 virtual map coordinates).
* **Sound**: 4 channels with configurable waveforms, SFX editor, and tracker-based music editor.
* **Input**: 4 gamepads (8 buttons each), keyboard, and mouse input.
* **Scripting**: Modern sandboxed Lua 5.3 engine with fast execution.
* **Cartridge Portability**: Standalone text (`.lua`), binary (`.tic`), and cartridge image (`.png`) formats.

---

## 🚀 Quick Start

### 1. Launch OpenTIC
Run `tic80.exe` (or `opentic.exe`). You will be greeted by the OpenTIC console:

```text
 OpenTIC fantasy console 1.2.0-dev (OpenTIC)
 OpenTIC Community (C) 2026
```

### 2. Summon the AI Assistant
* Press **`F6`** (or type `ai` in the console) to open the AI workspace.
* Type `/` to open the interactive popup command palette.
* Set your API key:
  ```text
  /key deepseek sk-your-key-here
  ```
* Start creating! Ask OpenTIC in plain language:
  > *"创建一个弹球打砖块游戏，要有小球碰撞反弹音效，并为挡板和砖块生成好看的像素精灵图"*

### 3. Retro Toolchain Hotkeys
| Hotkey | Mode / Function |
| :--- | :--- |
| **`F1`** / `Alt+1` | Code Editor |
| **`F2`** / `Alt+2` | Sprite / Tile Editor |
| **`F3`** / `Alt+3` | World Map Editor |
| **`F4`** / `Alt+4` | Sound Effects (SFX) Editor |
| **`F5`** / `Alt+5` | Music Tracker Editor |
| **`F6`** | **OpenTIC AI Assistant** |
| **`Ctrl + R`** | Run / Test Current Game |
| **`Ctrl + S`** | Save Cartridge |
| **`Esc`** | Switch between Game, Console, and Editors |

---

## 🎨 Cartridge Asset Format Reference

OpenTIC cartridges (`.lua`) store code and assets in a clean, human-readable structure:

### 1. Sprite & Tile Chunk (`-- <SPRITES>` / `-- <TILES>`)
Each sprite/tile is an 8x8 pixel grid encoded as 64 hexadecimal characters (`0` to `f`), representing palette color indices 0 through 15:
```lua
-- <SPRITES>
-- 001:0044440004ffff404ffff4444ffff44404ffff40004444000004400000044000
-- 002:00bbbb000beeeeb0bee33eebbee33eebbee33eeb0beeeeb000bbbb0000000000
-- </SPRITES>
```
Rendered in Lua with `spr(1, x, y, 0)` (`0` is transparent color key).

### 2. Palette Chunk (`-- <PALETTE>`)
16 RGB colors stored as 96 hexadecimal characters (6 hex digits `RRGGBB` per color):
```lua
-- <PALETTE>
-- 000:1a1c2c5d275db13e53ef7d57ffcd75a7f07038b76425717929366f3b5dc941a6f673eff7f4f4f494b0c2566c86333c57
-- </PALETTE>
```

### 3. Tilemap Chunk (`-- <MAP>`)
Rows of 2-digit hex tile IDs:
```lua
-- <MAP>
-- 000:010101010101010101010101010101010101010101010101010101010101
-- 001:010000000000000000000000000000000000000000000000000000000001
-- </MAP>
```

---

## 🛠️ Building from Source

### Prerequisites
* **CMake** 3.16 or newer
* **C/C++ Compiler** (MSVC on Windows, GCC/Clang on Linux and macOS)
* **Node.js** (for running the local AI bridge service)

### Build Instructions (Windows - Visual Studio / MSVC)
```powershell
# 1. Clone repository
git clone https://github.com/NoodleStormno/OpenTIC.git
cd OpenTIC

# 2. Configure CMake
cmake -B build_msvc -G "Visual Studio 17 2022" -A x64 -DBUILD_PRO=ON

# 3. Build Release target
cmake --build build_msvc --config Release --target OpenTIC

# 4. Binaries are generated at:
# build_msvc/bin/OpenTIC.exe
```

### Build Instructions (Linux / Ubuntu)
```bash
sudo apt update
sudo apt install build-essential cmake libsdl2-dev libasound2-dev libgl1-mesa-dev
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_PRO=ON
cmake --build build -j$(nproc)
```

---

## 📂 Project Structure

```text
OpenTIC/
├── assets/                    # TrueType fonts and branding assets
│   └── SourceHanSansSC-Regular.otf
├── cmake/                     # Modular CMake configurations
├── src/                       # Core console source code
│   ├── studio/                # Studio frontend, editors, UI
│   │   ├── editors/
│   │   │   ├── ai.c           # High-Res OpenTIC AI copilot interface
│   │   │   ├── ai.h
│   │   │   ├── code.c         # Lua code editor
│   │   │   ├── sprite.c       # Sprite & tile editor
│   │   │   ├── map.c          # World map editor
│   │   │   ├── sfx.c          # SFX synthesizer
│   │   │   └── music.c        # Tracker music editor
│   │   ├── screens/           # Console, menu, start screens
│   │   └── stb_truetype.h     # High-speed TrueType rasterizer
│   ├── core/                  # Virtual machine, RAM/VRAM emulation
│   └── system/                # Platform adapters (SDL2, Naett networking)
└── tools/
    └── bridge/
        └── tic-omp-bridge.js  # OpenTIC AI background bridge daemon
```

---

## 📄 License

OpenTIC is licensed under the [MIT License](LICENSE).  
Portions derived from the upstream TIC-80 project are Copyright (c) 2017-2026 Vadim Grigoruk and the TIC-80 Contributors.
