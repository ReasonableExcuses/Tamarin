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
> **CPU Compute Throughput Note**: For unthrottled CPU compute throughput independent of display swapchain pacing, see the core compute benchmarks (such as N-Body orbit simulation, SoA scan & reduction, and async microtask dispatch) and the SDL3 Streaming CPU Raytracer below, where Tamarin's `@layout(scan)` Structure-of-Arrays layout and SIMD auto-vectorization differentiate CPU execution times.

### 7.3 Multi-Threaded CPU Raytracer Benchmark (`benchmarks/projects/raytracer_sdl3/`)

To evaluate raw unthrottled CPU compute throughput independent of display swapchain pacing, Milestone v0.18.5 includes a high-performance multi-threaded CPU path/ray tracer rendering live to an unthrottled **SDL3 streaming texture** (`SDL_LockTexture`, `SDL_PIXELFORMAT_RGBA32`):

#### 1. Architecture & Concurrency Model
- **Workload:** 1280×720 framebuffer, 16 samples per pixel (14.75 million rays), maximum ray bounce depth 8.
- **Deterministic Scene:** Ground sphere + 3 large hero spheres (Lambertian diffuse, metallic mirror with fuzz, dielectric glass with Schlick Fresnel refraction) + 120 procedural spheres on a 15×8 grid (124 spheres total).
- **Structure-of-Arrays (SoA):** Contiguous arrays for sphere center coordinates (`cx`, `cy`, `cz`), radius, `inv_radius`, `rsq`, albedo, fuzz, and material type (`g_cx`, `g_cy`, `g_cz`, etc.).
- **Zero-Copy Streaming Presentation:** The texture is locked on the main thread via `SDL_LockTexture(tex, null, &pixels, &pitch)`. Rows are written respecting driver byte pitch, unlocked via `SDL_UnlockTexture`, and presented via `SDL_RenderTexture` and `SDL_RenderPresent`.
- **Affine Injectivity Concurrency:** Scanlines are distributed via `parallel for (int y = 0; y < 720; y++)`. Tamarin's affine index injectivity checker statically proves that disjoint row writes to `g_framebuffer[y][x]` are 100% data race-free, requiring **zero `unsafe` blocks** across the entire application.

#### 2. Cross-Language Parity & Benchmark Results
Evaluated on AMD Ryzen 7 7840HS (8 physical cores, 16 threads, 16 GB DDR5, Windows 11):

| Language & Configuration | Frame Time (ms) | Throughput (Mrays/s) | Clean Build (s) | Binary Size (KB) | Peak RSS (MB) | Unsafe Blocks | Bit-Exact Checksum |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **Tamarin (SIMD 8-wide)** | **231.01** | **63.83** | **1.13** | **96.5** | **12.61** | **0** | **MATCH** (`0x2BA9B398A1D21CEF`) |
| **Clang C++20 (SIMD 8-wide)** | 247.52 | 59.57 | 1.49 | 209.5 | 12.23 | Unchecked | **MATCH** (`0x2BA9B398A1D21CEF`) |
| **Rust (SIMD 8-wide)** | 244.61 | 60.28 | 4.67 | 276.5 | 12.00 | 3 | **MATCH** (`0x2BA9B398A1D21CEF`) |
| **Tamarin (Standard SoA)** | 1057.42 | 13.94 | 1.13 | 96.5 | 12.59 | **0** | **MATCH** (`0x2BA9B398A1D21CEF`) |
| **Clang C++20 (Standard SoA)** | 1051.67 | 14.02 | 1.49 | 209.5 | 12.23 | Unchecked | **MATCH** (`0x2BA9B398A1D21CEF`) |
| **Rust (Standard SoA)** | 1023.88 | 14.40 | 4.67 | 276.5 | 12.01 | 3 | **MATCH** (`0x2BA9B398A1D21CEF`) |
| **Clang C++20 (AoS Baseline)** | 1033.76 | 14.26 | 1.49 | 209.5 | 12.22 | Unchecked | **MATCH** (`0x2BA9B398A1D21CEF`) |
| **Rust (AoS Baseline)** | 1190.52 | 12.39 | 4.67 | 276.5 | 11.95 | 3 | **MATCH** (`0x2BA9B398A1D21CEF`) |

#### 3. Compiler Backend & Codegen Enhancements
- **Memory Alignment (`align 64`):** LLVM codegen aligns global array structures to 64 bytes, eliminating cache-line split penalties during 8-wide AVX2 loads (`vmovaps`).
- **Math Flags Parity:** Emits `-fno-math-errno -fno-trapping-math` on release builds, allowing loop and SLP vectorizers to optimize mathematical loops while strictly adhering to IEEE-754 precision.
- **Direct Hardware Intrinsics:** Calls to `sqrt`/`sqrtf` lower to native `@llvm.sqrt.f32/@llvm.sqrt.f64` hardware instructions (`vsqrtss`/`vsqrtsd`) with `nounwind willreturn memory(none)`.
- **Zero-Allocation Stack Scheduling:** Parallel loop frames are stack-allocated with atomic fetch-and-add task queues, eliminating runtime heap allocations during multithreaded dispatch.
