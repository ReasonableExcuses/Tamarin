use std::env;
use std::fs;
use std::path::PathBuf;

fn main() {
    let manifest_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let native_dir = manifest_dir.join("../../../../lib/raylib/native");
    if let Ok(canon) = native_dir.canonicalize() {
        println!("cargo:rustc-link-search=native={}", canon.display());
    } else {
        println!("cargo:rustc-link-search=native={}", native_dir.display());
    }

    println!("cargo:rustc-link-lib=raylib");
    println!("cargo:rustc-link-lib=winmm");
    println!("cargo:rustc-link-lib=gdi32");
    println!("cargo:rustc-link-lib=user32");
    println!("cargo:rustc-link-lib=shell32");
    println!("cargo:rustc-link-lib=opengl32");

    // Copy raylib.dll to target directory
    if let Ok(out_dir) = env::var("OUT_DIR") {
        let out_path = PathBuf::from(out_dir);
        // target/release is 3 levels up from OUT_DIR
        let target_dir = out_path.ancestors().nth(3);
        if let Some(target) = target_dir {
            let dll_src = native_dir.join("raylib.dll");
            let dll_dst = target.join("raylib.dll");
            let _ = fs::copy(&dll_src, &dll_dst);
        }
    }
}
