import os
import hashlib
import shutil
from PIL import Image

results_dir = "benchmarks/results"
t_bmp = os.path.join(results_dir, "raytracer_tamarin.bmp")
c_bmp = os.path.join(results_dir, "raytracer_cpp.bmp")
r_bmp = os.path.join(results_dir, "raytracer_rust.bmp")

h_t = hashlib.sha256(open(t_bmp, "rb").read()).hexdigest()
h_c = hashlib.sha256(open(c_bmp, "rb").read()).hexdigest()
h_r = hashlib.sha256(open(r_bmp, "rb").read()).hexdigest()

print(f"Tamarin BMP SHA-256: {h_t}")
print(f"C++     BMP SHA-256: {h_c}")
print(f"Rust    BMP SHA-256: {h_r}")
assert h_t == h_c == h_r, "BMP file hashes do not match!"
print("ALL THREE BMP SCREENSHOTS ARE 100% BIT-FOR-BIT IDENTICAL!")

# Convert to PNG for display in conversation artifacts
png_path = os.path.join(results_dir, "raytracer_render.png")
im = Image.open(t_bmp)
im.save(png_path)
print(f"PNG display screenshot saved: {png_path} ({os.path.getsize(png_path)} bytes)")

# Copy PNG to artifact directory
artifact_dir = r"C:\Users\hp\.gemini\antigravity\brain\a4e8917a-4572-4f0c-afa9-b05052261c02"
dest_path = os.path.join(artifact_dir, "raytracer_render.png")
shutil.copy(png_path, dest_path)
print(f"Artifact image deployed to: {dest_path}")
