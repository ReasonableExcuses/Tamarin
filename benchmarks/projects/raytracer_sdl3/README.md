# Multi-Threaded CPU Raytracer Benchmark (SDL3 Streaming)

**Tamarin (v0.18.5+) vs Clang C++20 vs Rust**

A high-performance, deterministic, multi-threaded CPU path/ray tracer rendering live to an unthrottled **SDL3 streaming texture** (`SDL_LockTexture`, `SDL_PIXELFORMAT_RGBA32`).

---

## Benchmark Highlights

- **Bit-for-Bit Determinism:** Identical 1280×720 framebuffer, 16 samples per pixel, max bounce depth 8, deterministic per-pixel PCG32/Xorshift32 PRNG. Produces **100% identical RGBA32 checksums** across Tamarin, C++, and Rust.
- **Scene:** 124 spheres (ground sphere + 3 hero spheres: Lambertian diffuse, metal with fuzz, dielectric glass + 120 procedural spheres on a 15×8 grid).
- **Structure-of-Arrays (SoA):** Contiguous arrays for sphere center coordinates, radii, albedos, and material attributes enabling contiguous vectorized sweeps.
- **Affine Injectivity Concurrency:** Tamarin parallelizes scanlines across cores using `parallel for (int y = 0; y < 720; y++)` with **zero `unsafe` blocks** and zero data races.
- **Zero-Copy Presentation:** Locks SDL3 streaming texture, copies rows respecting driver byte pitch, and presents without display throttling.

---

## Project Structure

```
benchmarks/projects/raytracer_sdl3/
├── benchmark.py                 # Automated benchmark orchestrator & report generator
├── README.md                    # Project documentation
├── cpp/
│   ├── main.cpp                 # C++20 raytracer (std::jthread, SoA & AoS kernels, SDL3)
│   ├── sdl3_mini.h              # Minimal SDL3 C ABI bindings
│   └── raytracer_cpp.exe        # Compiled C++20 binary
├── rust/
│   ├── Cargo.toml               # Cargo config (Rayon 1.10)
│   ├── build.rs                 # Native SDL3 linker build script
│   └── src/main.rs              # Rust raytracer (Rayon par_chunks, SoA & AoS, SDL3)
└── tamarin/
    ├── troop.toml               # Troop project config (SDL3 linkage, -O3 release profile)
    └── src/main.tmn             # Tamarin raytracer (parallel for, SoA, SDL3 FFI)
```

---

## Running the Benchmark

### 1. Automated Comprehensive Benchmark
Runs clean compilation, binary sizing, working set memory inspection, and 10-frame headless execution across all configurations:
```powershell
python benchmarks/projects/raytracer_sdl3/benchmark.py
```
This automatically writes the verified results to `benchmarks/results/raytracer_sdl3_report.md`.

### 2. Interactive Live Rendering Mode (SDL3 Window)
Launch the interactive window with real-time framerate and ray throughput output (Press `Esc` or close window to exit):
```powershell
# Tamarin
.\\benchmarks\\projects\\raytracer_sdl3\\tamarin\\bin\\release\\raytracer_tamarin.exe

# Clang C++20
.\\benchmarks\\projects\\raytracer_sdl3\\cpp\\raytracer_cpp.exe

# Rust
.\\benchmarks\\projects\\raytracer_sdl3\\rust\\target\\release\\raytracer_rust.exe
```

### 3. Headless Verification Mode (CI & Reproducibility)
Run headless for a specific number of frames:
```powershell
# Tamarin
.\\benchmarks\\projects\\raytracer_sdl3\\tamarin\\bin\\release\\raytracer_tamarin.exe --headless 30

# Clang C++20 (SoA or AoS)
.\\benchmarks\\projects\\raytracer_sdl3\\cpp\\raytracer_cpp.exe --headless 30 --soa
.\\benchmarks\\projects\\raytracer_sdl3\\cpp\\raytracer_cpp.exe --headless 30 --aos

# Rust (SoA or AoS)
.\\benchmarks\\projects\\raytracer_sdl3\\rust\\target\\release\\raytracer_rust.exe --headless 30 --soa
.\\benchmarks\\projects\\raytracer_sdl3\\rust\\target\\release\\raytracer_rust.exe --headless 30 --aos
```
