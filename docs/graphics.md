# Tamarin Graphics & Native Ecosystem Specification

**Version:** 0.18.5  
**Target:** Raylib 5.5, SDL3 Platform & Multi-Backend `SDL_GPU`  
**Classification:** Native Systems Graphics, Windowing & High-Performance Rendering  

---

## 1. Overview and Design Philosophy

Tamarin provides first-class support for modern, high-performance 2D and 3D graphics, immediate-mode game development, and low-overhead cross-platform GPU pipelines without intermediate FFI wrappers or marshalling overhead.

Milestone v0.18.5 delivers native integration across two industry-leading graphics ecosystems:
1. **Raylib 5.5 (`tamarin-raylib`):** An immediate-mode, developer-friendly 2D and 3D game framework with native math, audio, shapes, models, and font rendering.
2. **SDL3 Core Platform (`tamarin-sdl3`):** Robust cross-platform window management, display modes, keyboard/mouse/gamepad input event processing, audio streams, and platform services.
3. **Modern Multi-Backend `SDL_GPU`:** Direct access to next-generation low-overhead graphics hardware backends—**Vulkan**, **Direct3D 12**, **Metal**, and **OpenGL**—via command buffers, render passes, and compute queues.
4. **Troop Project Templates (`troop new --template=...`):** Instant project scaffolding for games and applications with automatic target-aware manifest linking and directory structures.

```mermaid
graph TD
    A[Tamarin Source .tmn] --> B[Tamarin Compiler v0.18.5]
    B --> C[LLVM Backend & C ABI Lowering]
    C --> D[Native Linker Directives]
    D --> E[Native Graphics Binaries]
    
    subgraph "Graphics Backends"
        E --> F[Raylib 5.5 Engine]
        E --> G[SDL3 Platform & Windowing]
        E --> H[SDL_GPU Multi-Backend Pipeline]
    end
    
    subgraph "Hardware Drivers"
        H --> I[Vulkan - Linux / Windows / Android]
        H --> J[Direct3D 12 - Windows]
        H --> K[Metal - macOS / iOS]
        H --> L[OpenGL - Universal Fallback]
    end
```

---

## 2. Compiler Linker Directives

To support native graphics toolkits, the Tamarin compiler and driver provide industry-standard linker control flags:

| Flag | Purpose | Example |
| :--- | :--- | :--- |
| `-l <lib>`, `--link <lib>` | Links a native system library | `-l raylib`, `-l SDL3` |
| `-L <dir>`, `--lib-dir <dir>` | Prepends a directory to native library search paths | `-L lib/native`, `-L /usr/local/lib` |
| `-Wl,<arg>` | Passes comma-separated arguments directly to host linker | `-Wl,-rpath,$ORIGIN` |
| `-Xlinker <arg>` | Passes an exact single flag directly to host linker | `-Xlinker /NODEFAULTLIB:msvcrt` |

---

## 3. Raylib 5.5 Ecosystem (`tamarin-raylib`)

### 3.1 C ABI Struct-by-Value Lowering
Raylib relies extensively on passing and returning small structs by value (e.g., `Vector2`, `Vector3`, `Color`, `Camera3D`). The Tamarin compiler implements platform-compliant ABI lowering:
- **MSVC x64:** Structs of size 1, 2, 4, or 8 bytes (`sizeof(Color) == 4`, `sizeof(Vector2) == 8`) are packed into integers (`i32`, `i64`) and passed in scalar registers (`RCX`, `RDX`, `R8`, `R9`). Structs larger than 8 bytes (`Vector3`, `Camera3D`) are passed via invisible stack pointer references.
- **System V AMD64 (Linux / macOS):** Structs containing floating-point pairs (`Vector2`) are lowered to `xmm` registers, while composite structures are lowered into integer registers according to System V AMD64 classification.

### 3.2 2D Immediate Mode Rendering Example
```tamarin
import raylib as rl;

int main() {
    rl::InitWindow(800, 450, "Tamarin 2D Graphics");
    rl::SetTargetFPS(60);

    rl::Color bgColor = { 245u8, 245u8, 245u8, 255u8 };
    rl::Color textColor = { 230u8, 41u8, 55u8, 255u8 };

    while (!rl::WindowShouldClose()) {
        rl::BeginDrawing();
        rl::ClearBackground(bgColor);
        rl::DrawText("Hello from Tamarin & Raylib 5.5!", 190, 200, 20, textColor);
        rl::EndDrawing();
    }

    rl::CloseWindow();
    return 0;
}
```

### 3.3 3D Rendering & Camera Control
```tamarin
import raylib as rl;

int main() {
    rl::InitWindow(800, 450, "Tamarin 3D Demo");
    rl::SetTargetFPS(60);

    rl::Camera3D camera;
    camera.position = { 0.0, 10.0, 10.0 };
    camera.target = { 0.0, 0.0, 0.0 };
    camera.up = { 0.0, 1.0, 0.0 };
    camera.fovy = 45.0;
    camera.projection = rl::CAMERA_PERSPECTIVE;

    while (!rl::WindowShouldClose()) {
        rl::BeginDrawing();
        rl::ClearBackground({ 240u8, 240u8, 240u8, 255u8 });

        rl::BeginMode3D(camera);
        rl::DrawGrid(10, 1.0);
        rl::DrawCube({ 0.0, 1.0, 0.0 }, 2.0, 2.0, 2.0, { 255u8, 0u8, 0u8, 255u8 });
        rl::DrawCubeWires({ 0.0, 1.0, 0.0 }, 2.0, 2.0, 2.0, { 0u8, 0u8, 0u8, 255u8 });
        rl::EndMode3D();

        rl::EndDrawing();
    }

    rl::CloseWindow();
    return 0;
}
```

---

## 4. SDL3 Core Platform & Windowing (`tamarin-sdl3`)

SDL3 provides the foundation for window management, multi-monitor display configuration, high-DPI scaling, and multi-threaded event pumping.

### 4.1 Window Creation and Event Loop
```tamarin
import sdl3 as sdl;

int main() {
    bool initOk = sdl::SDL_Init(0x00000020u32); // SDL_INIT_VIDEO
    if (!initOk) {
        print("Failed to initialize SDL3: ");
        print(sdl::SDL_GetError());
        print("\n");
        return 1;
    }

    *sdl::SDL_Window window = sdl::SDL_CreateWindow("Tamarin SDL3 Window", 800, 600, 0u64);
    if (window == null) {
        print("Failed to create SDL3 window\n");
        sdl::SDL_Quit();
        return 1;
    }

    sdl::SDL_Event event;
    bool running = true;
    while (running) {
        while (sdl::SDL_PollEvent(&event)) {
            if (event.type == (u32)sdl::SDL_EVENT_QUIT) {
                running = false;
            }
        }
    }

    sdl::SDL_DestroyWindow(window);
    sdl::SDL_Quit();
    return 0;
}
```

---

## 5. Modern Multi-Backend `SDL_GPU` Pipeline

`SDL_GPU` exposes explicit, low-overhead GPU resource management modeled after Vulkan, Direct3D 12, and Metal.

### 5.1 Architecture
- **Shader Format Mask:** Selects eligible modern GPU driver drivers at runtime (`SDL_GPU_SHADERFORMAT_SPIRV`, `SDL_GPU_SHADERFORMAT_DXBC`, `SDL_GPU_SHADERFORMAT_DXIL`, `SDL_GPU_SHADERFORMAT_MSL`).
- **Command Buffers (`*SDL_GPUCommandBuffer`):** Recorded without global driver lock contention.
- **Render Passes (`*SDL_GPURenderPass`):** Encapsulate render target attachments, depth/stencil buffers, load operations (`CLEAR`, `LOAD`, `DONT_CARE`), and store operations (`STORE`, `DONT_CARE`).

### 5.2 Complete `SDL_GPU` Setup & Render Pass
```tamarin
import sdl3 as sdl;

int main() {
    bool initOk = sdl::SDL_Init(0x00000020u32); // SDL_INIT_VIDEO
    if (!initOk) {
        return 1;
    }

    *sdl::SDL_Window window = sdl::SDL_CreateWindow(
        "Tamarin SDL_GPU Window",
        800,
        600,
        (u64)sdl::SDL_WINDOW_RESIZABLE
    );
    if (window == null) {
        sdl::SDL_Quit();
        return 1;
    }

    // Create GPU device accepting Vulkan, D3D12, or Metal
    *sdl::SDL_GPUDevice device = sdl::SDL_CreateGPUDevice(
        0x00000001u32 | 0x00000002u32 | 0x00000004u32, // Vulkan | D3D12 | Metal
        false,
        null
    );
    if (device == null) {
        sdl::SDL_DestroyWindow(window);
        sdl::SDL_Quit();
        return 0; // Graceful fallback on headless machines without GPU hardware
    }

    // Claim window for GPU rendering
    bool claimOk = sdl::SDL_ClaimWindowForGPUDevice(device, window);
    if (!claimOk) {
        sdl::SDL_DestroyGPUDevice(device);
        sdl::SDL_DestroyWindow(window);
        sdl::SDL_Quit();
        return 1;
    }

    // Acquire command buffer
    *sdl::SDL_GPUCommandBuffer cmdBuf = sdl::SDL_AcquireGPUCommandBuffer(device);
    if (cmdBuf != null) {
        *sdl::SDL_GPUTexture swapchainTexture = null;
        u32 width = 0u32;
        u32 height = 0u32;

        if (sdl::SDL_AcquireGPUSwapchainTexture(cmdBuf, window, &swapchainTexture, &width, &height)) {
            if (swapchainTexture != null) {
                sdl::SDL_GPUColorTargetInfo colorTarget;
                colorTarget.texture = swapchainTexture;
                colorTarget.mip_level = 0u32;
                colorTarget.layer_or_depth_plane = 0u32;
                colorTarget.clear_color = { 0.392, 0.584, 0.929, 1.0 }; // Cornflower blue
                colorTarget.load_op = (u32)sdl::SDL_GPU_LOADOP_CLEAR;
                colorTarget.store_op = (u32)sdl::SDL_GPU_STOREOP_STORE;

                *sdl::SDL_GPURenderPass pass = sdl::SDL_BeginGPURenderPass(cmdBuf, &colorTarget, 1u32, null);
                if (pass != null) {
                    sdl::SDL_EndGPURenderPass(pass);
                }
            }
        }
        sdl::SDL_SubmitGPUCommandBuffer(cmdBuf);
    }

    sdl::SDL_ReleaseWindowFromGPUDevice(device, window);
    sdl::SDL_DestroyGPUDevice(device);
    sdl::SDL_DestroyWindow(window);
    sdl::SDL_Quit();
    return 0;
}
```

---

## 6. Troop Package Manager Templates

Troop introduces first-class project templates for immediate project bootstrapping:

### 6.1 Scaffolding a Raylib Game
```bash
troop new my_game --template=raylib-game
cd my_game
troop run
```

Generated `troop.toml`:
```toml
[package]
name = "my_game"
version = "0.1.0"
edition = "2026"
kind = "bin"
entry = "src/main.tmn"
links = ["raylib"]

[target.windows]
links = ["winmm", "gdi32", "user32", "shell32", "opengl32"]
lib-dirs = ["native"]

[target.linux]
links = ["m", "pthread", "dl", "GL", "rt", "X11"]

[target.macos]
links = ["-framework OpenGL", "-framework Cocoa", "-framework IOKit", "-framework CoreVideo"]
lib-dirs = ["/opt/homebrew/lib", "/usr/local/lib"]
```

### 6.2 Scaffolding an SDL3 Application
```bash
troop new my_app --template=sdl3-app
cd my_app
troop run
```

Generated `troop.toml`:
```toml
[package]
name = "my_app"
version = "0.1.0"
edition = "2026"
kind = "bin"
entry = "src/main.tmn"
links = ["SDL3"]

[target.windows]
lib-dirs = ["native"]

[target.linux]
lib-dirs = ["/usr/local/lib"]

[target.macos]
lib-dirs = ["/opt/homebrew/lib", "/usr/local/lib"]
```
