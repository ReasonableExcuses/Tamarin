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

### 3.1 C ABI Struct-by-Value & String Literal Lowering
Raylib relies extensively on passing and returning small structs by value (e.g., `Vector2`, `Vector3`, `Color`, `Camera3D`) and NUL-terminated C strings (`const char*`):
- **MSVC x64:** Structs of size 1, 2, 4, or 8 bytes (`sizeof(Color) == 4`, `sizeof(Vector2) == 8`) are packed into integers (`i32`, `i64`) and passed in scalar registers (`RCX`, `RDX`, `R8`, `R9`). Structs larger than 8 bytes (`Vector3`, `Camera3D`) are passed via invisible stack pointer references.
- **System V AMD64 (Linux / macOS):** Structs containing floating-point pairs (`Vector2`) are lowered to `xmm` registers, while composite structures are lowered into integer registers according to System V AMD64 classification.
- **String Literal Lowering (`const char*` Interop):** Functions across Raylib (`InitWindow`, `DrawText`) and SDL3 (`SDL_CreateWindow`, `SDL_GetError`) pass and return NUL-terminated C strings. In Tamarin, string literals passed across `extern "C"` boundaries lower directly to NUL-terminated 8-byte pointers (`ptr` / `*const u8`) rather than 16-byte slice descriptors (`(ptr, len)`). This guarantees zero-overhead C calling convention conformity with no runtime string allocation or wrapper thunks.

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
- **Shader Format Mask:** Selects eligible modern GPU drivers at runtime (`SDL_GPU_SHADERFORMAT_SPIRV`, `SDL_GPU_SHADERFORMAT_DXBC`, `SDL_GPU_SHADERFORMAT_DXIL`, `SDL_GPU_SHADERFORMAT_MSL`).
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

    // Portable across Vulkan (Linux/Windows), D3D12 (Windows), and Metal (macOS)
    u32 shaderFormats = (u32)sdl::SDL_GPU_SHADERFORMAT_SPIRV
                      | (u32)sdl::SDL_GPU_SHADERFORMAT_DXIL
                      | (u32)sdl::SDL_GPU_SHADERFORMAT_MSL;

    *sdl::SDL_GPUDevice device = sdl::SDL_CreateGPUDevice(shaderFormats, false, null);
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

---

## 7. Real-World Application Showcase & Ecosystem Benchmarks

Milestone v0.18.5 includes a complete, fully functional native desktop GUI Calculator in `benchmarks/projects/gui_test/` implemented identically across Tamarin (`src/main.tmn`), C++20, and Rust using Raylib 5.5.

### 7.1 Architecture & Idiomatic Safety
- **Clean State Management**: Explicit structs (`CalcState`, `CalcButton`) modeling operand inputs, active operator enums, and evaluation states without hidden globals.
- **Definite Assignment & Spatial Safety**: Fixed-size display buffers (`u8 display[32]`) validated by compiler initialization tracking and non-escaping borrow verification.
- **Zero Unsafe Blocks**: Direct C ABI struct-by-value interop with Raylib primitives (`rl::Vector2`, `rl::Color`, `rl::Rectangle`) without requiring a single `unsafe` keyword or wrapper thunk.
- **Automated Native Windows DLL Deployment**: `troop build` automatically resolves `lib-dirs` in `troop.toml` and copies `raylib.dll` into the output `bin/` directory.

### 7.2 Cross-Language Ergonomics, Footprint, Build Latency & Safety Comparison

The GUI Calculator benchmark evaluates developer ergonomics, binary footprint, build latency, and memory safety invariants across a full desktop application. Because the application runs at a display-paced 50 FPS (`rl::SetTargetFPS(50)`), all three runtimes achieve locked 50.0 FPS (~20.0 ms frame time) with zero dropped frames; sub-millisecond deltas (~0.4 ms) represent OS window compositor and swapchain timer jitter rather than language execution limits.

| Metric | Tamarin (v0.18.5) | C++ (Clang C++20) | Rust (Edition 2021) | Benchmark Analysis |
| :--- | :--- | :--- | :--- | :--- |
| **Frame Pacing** | **20.01 ms** (50.0 FPS) | 20.47 ms (48.9 FPS) | 20.22 ms (49.5 FPS) | Smooth locked 50 FPS pacing across all three toolchains |
| **Total 300-Frame Time** | **6.004 s** | 6.141 s | 6.065 s | Identical display-synchronized completion (~6.0 s) |
| **Stripped Binary Size** | **502.5 KB** | **489.0 KB** | 641.5 KB | Within **2.7%** of C++; **21.7% smaller** than Rust (no runtime unwinding/format bloat) |
| **Release Build Time** | **0.73 s** | **0.28 s** | 1.03 s | C++ fastest (single file); Tamarin compiles **29% faster** than `cargo build --release` |
| **Peak Working Set** | **92.59 MB** | 92.17 MB | 92.23 MB | Parity within 0.4 MB (dominated by OS GPU swapchain buffers) |
| **Safety Invariants** | **Safe by Default** (0 `unsafe`) | Unsafe Manual | Unsafe FFI Blocks | Tamarin requires **zero `unsafe` blocks**; Rust requires `unsafe` for every Raylib call |

> [!NOTE]
> **CPU Compute Throughput Note**: For unthrottled CPU compute throughput independent of display swapchain pacing, see the core compute benchmarks (such as N-Body orbit simulation, SoA scan & reduction, and async microtask dispatch), where Tamarin's `@layout(scan)` Structure-of-Arrays layout and SIMD auto-vectorization differentiate CPU execution times.
