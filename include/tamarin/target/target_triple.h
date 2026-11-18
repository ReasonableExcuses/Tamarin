// ============================================================================
// target_triple.h - Target Architecture, OS, and LLVM Cross-Compilation Triple
// ============================================================================
#pragma once

#include <string>
#include <string_view>
#include <algorithm>

namespace tamarin::target {

enum class Arch {
    X86_64,
    AArch64,
    Unknown
};

enum class Vendor {
    PC,
    Apple,
    Unknown
};

enum class OS {
    Windows,
    Linux,
    Darwin,
    Unknown
};

enum class Environment {
    MSVC,
    GNU,
    None,
    Unknown
};

class TargetTriple {
public:
    TargetTriple() = default;

    TargetTriple(Arch a, Vendor v, OS o, Environment e, std::string rawStr = "")
        : arch_(a), vendor_(v), os_(o), env_(e), rawString_(std::move(rawStr)) {}

    static TargetTriple getHost() {
#if defined(_WIN32)
        return TargetTriple(Arch::X86_64, Vendor::PC, OS::Windows, Environment::MSVC, "x86_64-pc-windows-msvc");
#elif defined(__APPLE__)
#  if defined(__aarch64__) || defined(__arm64__)
        return TargetTriple(Arch::AArch64, Vendor::Apple, OS::Darwin, Environment::None, "aarch64-apple-darwin");
#  else
        return TargetTriple(Arch::X86_64, Vendor::Apple, OS::Darwin, Environment::None, "x86_64-apple-darwin");
#  endif
#elif defined(__linux__)
#  if defined(__aarch64__)
        return TargetTriple(Arch::AArch64, Vendor::Unknown, OS::Linux, Environment::GNU, "aarch64-unknown-linux-gnu");
#  else
        return TargetTriple(Arch::X86_64, Vendor::Unknown, OS::Linux, Environment::GNU, "x86_64-unknown-linux-gnu");
#  endif
#else
        return TargetTriple(Arch::X86_64, Vendor::Unknown, OS::Linux, Environment::GNU, "x86_64-unknown-linux-gnu");
#endif
    }

    static TargetTriple host() { return getHost(); }

    static TargetTriple parse(std::string_view input) {
        if (input.empty() || input == "host" || input == "native") {
            return getHost();
        }

        std::string s(input);
        std::string lowerS = s;
        std::transform(lowerS.begin(), lowerS.end(), lowerS.begin(), [](unsigned char c) { return (char)std::tolower(c); });

        Arch a = Arch::Unknown;
        Vendor v = Vendor::Unknown;
        OS o = OS::Unknown;
        Environment e = Environment::Unknown;

        if (lowerS.find("x86_64") != std::string::npos || lowerS.find("amd64") != std::string::npos) {
            a = Arch::X86_64;
        } else if (lowerS.find("aarch64") != std::string::npos || lowerS.find("arm64") != std::string::npos) {
            a = Arch::AArch64;
        }

        if (lowerS.find("apple") != std::string::npos || lowerS.find("darwin") != std::string::npos || lowerS.find("macos") != std::string::npos) {
            v = Vendor::Apple;
            o = OS::Darwin;
            e = Environment::None;
        } else if (lowerS.find("windows") != std::string::npos || lowerS.find("win32") != std::string::npos) {
            v = Vendor::PC;
            o = OS::Windows;
            e = lowerS.find("gnu") != std::string::npos ? Environment::GNU : Environment::MSVC;
        } else if (lowerS.find("linux") != std::string::npos) {
            v = Vendor::Unknown;
            o = OS::Linux;
            e = Environment::GNU;
        }

        if (a == Arch::Unknown) a = Arch::X86_64;
        if (o == OS::Unknown) o = OS::Windows;

        return TargetTriple(a, v, o, e, s);
    }

    [[nodiscard]] Arch arch() const { return arch_; }
    [[nodiscard]] Vendor vendor() const { return vendor_; }
    [[nodiscard]] OS os() const { return os_; }
    [[nodiscard]] Environment env() const { return env_; }

    [[nodiscard]] bool isWindows() const { return os_ == OS::Windows; }
    [[nodiscard]] bool isLinux() const { return os_ == OS::Linux; }
    [[nodiscard]] bool isDarwin() const { return os_ == OS::Darwin; }
    [[nodiscard]] bool isAArch64() const { return arch_ == Arch::AArch64; }
    [[nodiscard]] bool isX86_64() const { return arch_ == Arch::X86_64; }

    [[nodiscard]] std::string str() const {
        if (!rawString_.empty()) return rawString_;
        std::string res;
        res += (arch_ == Arch::AArch64) ? "aarch64-" : "x86_64-";
        if (vendor_ == Vendor::Apple) res += "apple-";
        else if (vendor_ == Vendor::PC) res += "pc-";
        else res += "unknown-";

        if (os_ == OS::Darwin) res += "darwin";
        else if (os_ == OS::Linux) res += "linux-gnu";
        else if (os_ == OS::Windows) res += (env_ == Environment::GNU ? "windows-gnu" : "windows-msvc");
        else res += "unknown";
        return res;
    }

    [[nodiscard]] std::string llvmDataLayout() const {
        if (isWindows()) {
            return "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128";
        }
        if (isDarwin()) {
            if (isAArch64()) {
                return "e-m:o-i64:64-i128:128-n32:64-S128";
            }
            return "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128";
        }
        if (isLinux()) {
            if (isAArch64()) {
                return "e-m:e-i8:8:32-i16:16:32-i64:64-i128:128-n32:64-S128";
            }
            return "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128";
        }
        return "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128";
    }

    [[nodiscard]] std::string executableExtension() const {
        return isWindows() ? ".exe" : "";
    }

    [[nodiscard]] std::string sharedLibraryExtension() const {
        if (isWindows()) return ".dll";
        if (isDarwin()) return ".dylib";
        return ".so";
    }

    [[nodiscard]] std::string staticLibraryExtension() const {
        return isWindows() ? ".lib" : ".a";
    }

    [[nodiscard]] std::string objectExtension() const {
        return isWindows() ? ".obj" : ".o";
    }

private:
    Arch arch_ = Arch::X86_64;
    Vendor vendor_ = Vendor::PC;
    OS os_ = OS::Windows;
    Environment env_ = Environment::MSVC;
    std::string rawString_;
};

} // namespace tamarin::target
