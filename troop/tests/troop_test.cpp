// ============================================================================
// troop_test.cpp - Automated Unit Tests for Troop Package Manager
// ============================================================================
#include "test_harness.h"
#include "troop/semver.h"
#include "troop/manifest.h"
#include "troop/lockfile.h"
#include "troop/dependency_resolver.h"
#include "troop/package_manager.h"
#include "tamarin/runtime/concurrency.h"
#include "tamarin/runtime/page_provider.h"

#include <atomic>
#include <vector>
#include <filesystem>
#include <fstream>

using namespace troop;

TAMARIN_TEST(Troop, SemVerParsingAndComparison) {
    auto v1 = SemVer::parse("1.2.3");
    EXPECT_TRUE(v1.has_value());
    EXPECT_EQ(v1->major, 1u);
    EXPECT_EQ(v1->minor, 2u);
    EXPECT_EQ(v1->patch, 3u);
    EXPECT_EQ(v1->toString(), "1.2.3");

    auto v2 = SemVer::parse("v0.15.0-alpha.1+x86");
    EXPECT_TRUE(v2.has_value());
    EXPECT_EQ(v2->major, 0u);
    EXPECT_EQ(v2->minor, 15u);
    EXPECT_EQ(v2->patch, 0u);
    EXPECT_EQ(v2->preRelease, "alpha.1");
    EXPECT_EQ(v2->buildMeta, "x86");

    auto va = *SemVer::parse("1.0.0");
    auto vb = *SemVer::parse("1.2.0");
    auto vc = *SemVer::parse("2.0.0");
    EXPECT_TRUE(va < vb);
    EXPECT_TRUE(vb < vc);
    EXPECT_TRUE(va < vc);
    EXPECT_TRUE(va == va);
    EXPECT_FALSE(va == vb);
    return true;
}

TAMARIN_TEST(Troop, VersionRequirements) {
    auto reqCaret = VersionReq::parse("^1.2.3");
    EXPECT_TRUE(reqCaret.has_value());
    EXPECT_TRUE(reqCaret->matches(*SemVer::parse("1.2.3")));
    EXPECT_TRUE(reqCaret->matches(*SemVer::parse("1.2.9")));
    EXPECT_TRUE(reqCaret->matches(*SemVer::parse("1.9.0")));
    EXPECT_FALSE(reqCaret->matches(*SemVer::parse("1.2.2")));
    EXPECT_FALSE(reqCaret->matches(*SemVer::parse("2.0.0")));

    auto reqTilde = VersionReq::parse("~1.2.3");
    EXPECT_TRUE(reqTilde.has_value());
    EXPECT_TRUE(reqTilde->matches(*SemVer::parse("1.2.3")));
    EXPECT_TRUE(reqTilde->matches(*SemVer::parse("1.2.8")));
    EXPECT_FALSE(reqTilde->matches(*SemVer::parse("1.3.0")));

    auto reqWildcard = VersionReq::parse("*");
    EXPECT_TRUE(reqWildcard.has_value());
    EXPECT_TRUE(reqWildcard->matches(*SemVer::parse("99.99.99")));

    auto reqRange = VersionReq::parse(">= 1.0.0, < 2.5.0");
    EXPECT_TRUE(reqRange.has_value());
    EXPECT_TRUE(reqRange->matches(*SemVer::parse("1.0.0")));
    EXPECT_TRUE(reqRange->matches(*SemVer::parse("2.4.9")));
    EXPECT_FALSE(reqRange->matches(*SemVer::parse("0.9.9")));
    EXPECT_FALSE(reqRange->matches(*SemVer::parse("2.5.0")));
    return true;
}

TAMARIN_TEST(Troop, ManifestParsingAndSerialization) {
    std::string tomlContent = R"(
[package]
name = "my_app"
version = "0.2.1"
edition = "2026"
authors = ["Alice <alice@example.com>", "Bob"]
description = "A great Tamarin app"
license = "MIT"
kind = "bin"

[dependencies]
math = "1.0.0"
utils = { path = "../utils" }
network = { git = "https://github.com/tamarin-lang/net", tag = "v0.1" }

[profile.dev]
opt_level = 0
fast_math = false
crt = "dynamic"

[profile.release]
opt_level = 3
fast_math = true
crt = "static"
)";

    std::string err;
    auto manifest = Manifest::parseString(tomlContent, "fake_dir/troop.toml", err);
    EXPECT_TRUE(manifest.has_value());
    EXPECT_EQ(manifest->package.name, "my_app");
    EXPECT_EQ(manifest->package.version.toString(), "0.2.1");
    EXPECT_EQ(manifest->package.authors.size(), 2u);
    EXPECT_EQ(manifest->package.kind, "bin");

    EXPECT_EQ(manifest->dependencies.size(), 3u);
    EXPECT_TRUE(manifest->dependencies.count("math") > 0);
    EXPECT_TRUE(manifest->dependencies.count("utils") > 0);
    EXPECT_EQ(manifest->dependencies["utils"].sourceKind, DependencySourceKind::Path);
    EXPECT_EQ(manifest->dependencies["utils"].path, "../utils");

    EXPECT_EQ(manifest->profileRelease.optLevel, 3);
    EXPECT_TRUE(manifest->profileRelease.fastMath);
    EXPECT_EQ(manifest->profileRelease.crt, "static");

    // Test round-trip serialization
    std::string serialized = manifest->toToml();
    auto manifest2 = Manifest::parseString(serialized, "fake_dir/troop.toml", err);
    EXPECT_TRUE(manifest2.has_value());
    EXPECT_EQ(manifest2->package.name, "my_app");
    EXPECT_EQ(manifest2->dependencies.size(), 3u);
    EXPECT_EQ(manifest2->profileRelease.optLevel, 3);
    return true;
}

TAMARIN_TEST(Troop, NativeLinksAndLibDirsParsingAndSerialization) {
    std::string tomlContent = R"(
[package]
name = "game_app"
version = "0.1.0"
links = ["raylib", "m", "dl"]
lib-dirs = ["lib/native", "vendor/raylib"]

[target.windows]
links = ["winmm", "gdi32"]
lib-dirs = ["win_lib"]
)";

    std::string err;
    auto manifest = Manifest::parseString(tomlContent, "fake_dir/troop.toml", err);
    EXPECT_TRUE(manifest.has_value());
    EXPECT_EQ(manifest->package.name, "game_app");
    EXPECT_EQ(manifest->package.links.size(), 3u);
    EXPECT_EQ(manifest->package.links[0], "raylib");
    EXPECT_EQ(manifest->package.links[1], "m");
    EXPECT_EQ(manifest->package.links[2], "dl");
    EXPECT_EQ(manifest->package.libDirs.size(), 2u);
    EXPECT_EQ(manifest->package.libDirs[0], "lib/native");
    EXPECT_EQ(manifest->package.libDirs[1], "vendor/raylib");
    EXPECT_EQ(manifest->targetConfigs["windows"].links.size(), 2u);
    EXPECT_EQ(manifest->targetConfigs["windows"].links[0], "winmm");
    EXPECT_EQ(manifest->targetConfigs["windows"].libDirs.size(), 1u);
    EXPECT_EQ(manifest->targetConfigs["windows"].libDirs[0], "win_lib");

    // Test round-trip
    std::string serialized = manifest->toToml();
    auto manifest2 = Manifest::parseString(serialized, "fake_dir/troop.toml", err);
    EXPECT_TRUE(manifest2.has_value());
    EXPECT_EQ(manifest2->package.links.size(), 3u);
    EXPECT_EQ(manifest2->package.links[0], "raylib");
    EXPECT_EQ(manifest2->package.libDirs.size(), 2u);
    EXPECT_EQ(manifest2->package.libDirs[0], "lib/native");
    EXPECT_EQ(manifest2->targetConfigs["windows"].links.size(), 2u);
    EXPECT_EQ(manifest2->targetConfigs["windows"].links[0], "winmm");
    return true;
}

TAMARIN_TEST(Troop, LockfileSerializationAndChecksum) {
    Lockfile lock;
    lock.version = 1;

    LockedPackage p1;
    p1.name = "root_pkg";
    p1.version = *SemVer::parse("0.1.0");
    p1.source = "root";
    p1.checksum = "abcd1234abcd1234";
    p1.dependencies = {"utils"};
    lock.packages.push_back(p1);

    LockedPackage p2;
    p2.name = "utils";
    p2.version = *SemVer::parse("1.2.0");
    p2.source = "path+../utils";
    p2.checksum = "feedbeefcafebabe";
    lock.packages.push_back(p2);

    std::string tomlStr = lock.toToml();
    std::string err;
    auto parsedLock = Lockfile::parseString(tomlStr, err);
    EXPECT_TRUE(parsedLock.has_value());
    EXPECT_EQ(parsedLock->packages.size(), 2u);
    EXPECT_EQ(parsedLock->packages[0].name, "root_pkg");
    EXPECT_EQ(parsedLock->packages[1].name, "utils");
    EXPECT_EQ(parsedLock->packages[1].version.toString(), "1.2.0");
    return true;
}

TAMARIN_TEST(Troop, ScaffoldingAndNewPackage) {
    auto tempBase = std::filesystem::temp_directory_path() / "troop_test_scaffold";
    std::error_code ec;
    std::filesystem::remove_all(tempBase, ec);
    std::filesystem::create_directories(tempBase);

    PackageManager pm;
    std::string err;
    auto pkgDir = tempBase / "demo_app";
    bool ok = pm.newPackage(pkgDir, "demo_app", false, err);
    EXPECT_TRUE(ok);

    EXPECT_TRUE(std::filesystem::exists(pkgDir / "troop.toml"));
    EXPECT_TRUE(std::filesystem::exists(pkgDir / "src" / "main.tam"));
    EXPECT_TRUE(std::filesystem::exists(pkgDir / "tests" / "test_basic.tam"));
    EXPECT_TRUE(std::filesystem::exists(pkgDir / ".gitignore"));

    auto manifest = Manifest::loadFromFile(pkgDir / "troop.toml", err);
    EXPECT_TRUE(manifest.has_value());
    EXPECT_EQ(manifest->package.name, "demo_app");
    EXPECT_EQ(manifest->package.kind, "bin");

    std::filesystem::remove_all(tempBase, ec);
    return true;
}

TAMARIN_TEST(Troop, DependencyResolutionAndCycleDetection) {
    auto tempBase = std::filesystem::temp_directory_path() / "troop_test_resolve";
    std::error_code ec;
    std::filesystem::remove_all(tempBase, ec);

    auto pkgADir = tempBase / "pkg_a";
    auto pkgBDir = tempBase / "pkg_b";

    PackageManager pm;
    std::string err;
    EXPECT_TRUE(pm.newPackage(pkgADir, "pkg_a", true, err));
    EXPECT_TRUE(pm.newPackage(pkgBDir, "pkg_b", true, err));

    // Make pkg_a depend on pkg_b via path
    DependencySpec depB;
    depB.name = "pkg_b";
    depB.sourceKind = DependencySourceKind::Path;
    depB.path = "../pkg_b";
    depB.versionReq = *VersionReq::parse("*");

    EXPECT_TRUE(pm.addDependency(pkgADir / "troop.toml", "pkg_b", depB, err));

    auto manifestA = Manifest::loadFromFile(pkgADir / "troop.toml", err);
    EXPECT_TRUE(manifestA.has_value());

    DependencyResolver resolver;
    auto res = resolver.resolve(*manifestA);
    EXPECT_TRUE(res.success);
    EXPECT_EQ(res.packages.size(), 2u);
    EXPECT_TRUE(res.packages.count("pkg_b") > 0);

    // Now introduce circular dependency: pkg_b depends on pkg_a
    DependencySpec depA;
    depA.name = "pkg_a";
    depA.sourceKind = DependencySourceKind::Path;
    depA.path = "../pkg_a";
    depA.versionReq = *VersionReq::parse("*");
    EXPECT_TRUE(pm.addDependency(pkgBDir / "troop.toml", "pkg_a", depA, err));

    auto resCycle = resolver.resolve(*manifestA);
    EXPECT_FALSE(resCycle.success);
    EXPECT_TRUE(resCycle.error.find("cyclic dependency detected") != std::string::npos);

    std::filesystem::remove_all(tempBase, ec);
    return true;
}

TAMARIN_TEST(Troop, TemplateRaylibGame) {
    std::error_code ec;
    auto tempBase = std::filesystem::temp_directory_path() / "troop_test_raylib_tpl";
    std::filesystem::remove_all(tempBase, ec);
    std::filesystem::create_directories(tempBase);

    PackageManager pm;
    std::string err;
    auto gameDir = tempBase / "test_game";
    EXPECT_TRUE(pm.newPackage(gameDir, "test_game", false, err, "raylib-game"));

    EXPECT_TRUE(std::filesystem::exists(gameDir / "troop.toml"));
    EXPECT_TRUE(std::filesystem::exists(gameDir / "src" / "main.tmn"));

    auto manifest = Manifest::loadFromFile(gameDir / "troop.toml", err);
    EXPECT_TRUE(manifest.has_value());
    EXPECT_EQ(manifest->package.name, "test_game");
    EXPECT_EQ(manifest->package.entryPoint, "src/main.tmn");
    EXPECT_TRUE(std::find(manifest->package.links.begin(), manifest->package.links.end(), "raylib") != manifest->package.links.end());

    std::ifstream srcFile(gameDir / "src" / "main.tmn");
    std::string content((std::istreambuf_iterator<char>(srcFile)), std::istreambuf_iterator<char>());
    EXPECT_TRUE(content.find("import raylib as rl;") != std::string::npos);
    EXPECT_TRUE(content.find("rl::InitWindow") != std::string::npos);

    std::filesystem::remove_all(tempBase, ec);
    return true;
}

TAMARIN_TEST(Troop, TemplateSDL3App) {
    std::error_code ec;
    auto tempBase = std::filesystem::temp_directory_path() / "troop_test_sdl3_tpl";
    std::filesystem::remove_all(tempBase, ec);
    std::filesystem::create_directories(tempBase);

    PackageManager pm;
    std::string err;
    auto appDir = tempBase / "test_app";
    EXPECT_TRUE(pm.newPackage(appDir, "test_app", false, err, "sdl3-app"));

    EXPECT_TRUE(std::filesystem::exists(appDir / "troop.toml"));
    EXPECT_TRUE(std::filesystem::exists(appDir / "src" / "main.tmn"));

    auto manifest = Manifest::loadFromFile(appDir / "troop.toml", err);
    EXPECT_TRUE(manifest.has_value());
    EXPECT_EQ(manifest->package.name, "test_app");
    EXPECT_EQ(manifest->package.entryPoint, "src/main.tmn");
    EXPECT_TRUE(std::find(manifest->package.links.begin(), manifest->package.links.end(), "SDL3") != manifest->package.links.end());

    std::ifstream srcFile(appDir / "src" / "main.tmn");
    std::string content((std::istreambuf_iterator<char>(srcFile)), std::istreambuf_iterator<char>());
    EXPECT_TRUE(content.find("import sdl3 as sdl;") != std::string::npos);
    EXPECT_TRUE(content.find("sdl::SDL_Init") != std::string::npos);

    std::filesystem::remove_all(tempBase, ec);
    return true;
}

TAMARIN_TEST(Troop, CheckPackage) {
    std::error_code ec;
    auto tempBase = std::filesystem::temp_directory_path() / "troop_test_check";
    std::filesystem::remove_all(tempBase, ec);
    std::filesystem::create_directories(tempBase);

    PackageManager pm;
    std::string err;
    auto pkgDir = tempBase / "test_check_pkg";
    EXPECT_TRUE(pm.newPackage(pkgDir, "test_check_pkg", false, err));

    int res = pm.checkPackage(pkgDir / "troop.toml");
    EXPECT_EQ(res, 0);

    std::filesystem::remove_all(tempBase, ec);
    return true;
}

TAMARIN_TEST(Troop, AddAndRemoveDependency) {
    std::error_code ec;
    auto tempBase = std::filesystem::temp_directory_path() / "troop_test_rm_dep";
    std::filesystem::remove_all(tempBase, ec);
    std::filesystem::create_directories(tempBase);

    PackageManager pm;
    std::string err;
    auto pkgDir = tempBase / "main_pkg";
    auto depDir = tempBase / "dep_pkg";
    EXPECT_TRUE(pm.newPackage(pkgDir, "main_pkg", false, err));
    EXPECT_TRUE(pm.newPackage(depDir, "dep_pkg", true, err));

    DependencySpec spec;
    spec.name = "dep_pkg";
    spec.sourceKind = DependencySourceKind::Path;
    spec.path = "../dep_pkg";
    spec.versionReq = *VersionReq::parse("^0.1.0");

    EXPECT_TRUE(pm.addDependency(pkgDir / "troop.toml", "dep_pkg", spec, err));

    auto manifestAfterAdd = Manifest::loadFromFile(pkgDir / "troop.toml", err);
    EXPECT_TRUE(manifestAfterAdd.has_value());
    EXPECT_TRUE(manifestAfterAdd->dependencies.count("dep_pkg") > 0);

    // Now test printTree
    int treeRes = pm.printTree(pkgDir / "troop.toml");
    EXPECT_EQ(treeRes, 0);

    // Now remove dependency
    EXPECT_TRUE(pm.removeDependency(pkgDir / "troop.toml", "dep_pkg", err));

    auto manifestAfterRm = Manifest::loadFromFile(pkgDir / "troop.toml", err);
    EXPECT_TRUE(manifestAfterRm.has_value());
    EXPECT_EQ(manifestAfterRm->dependencies.count("dep_pkg"), 0u);

    std::filesystem::remove_all(tempBase, ec);
    return true;
}

TAMARIN_TEST(Troop, AddDevDependency) {
    std::error_code ec;
    auto tempBase = std::filesystem::temp_directory_path() / "troop_test_dev_dep";
    std::filesystem::remove_all(tempBase, ec);
    std::filesystem::create_directories(tempBase);

    PackageManager pm;
    std::string err;
    auto pkgDir = tempBase / "dev_pkg";
    EXPECT_TRUE(pm.newPackage(pkgDir, "dev_pkg", false, err));

    DependencySpec spec;
    spec.name = "test_helper";
    spec.versionReq = *VersionReq::parse("~1.2.0");

    EXPECT_TRUE(pm.addDependency(pkgDir / "troop.toml", "test_helper", spec, err, true));

    auto manifest = Manifest::loadFromFile(pkgDir / "troop.toml", err);
    EXPECT_TRUE(manifest.has_value());
    EXPECT_TRUE(manifest->devDependencies.count("test_helper") > 0);
    EXPECT_EQ(manifest->dependencies.count("test_helper"), 0u);

    std::filesystem::remove_all(tempBase, ec);
    return true;
}

TAMARIN_TEST(Runtime, Concurrency_ParallelForRaceFree) {
    constexpr int64_t N = 10000;
    std::vector<int64_t> data(N, 0);
    tamarin_parallel_for(0, N, 64, [](int64_t start, int64_t end, void* ctx) {
        auto* arr = static_cast<std::vector<int64_t>*>(ctx);
        for (int64_t i = start; i < end; ++i) {
            (*arr)[i] = i * 2 + 1;
        }
    }, &data);

    for (int64_t i = 0; i < N; ++i) {
        EXPECT_EQ(data[i], i * 2 + 1);
    }
    return true;
}

TAMARIN_TEST(Runtime, Concurrency_TaskScopeRaceFree) {
    std::atomic<int64_t> counter{0};
    void* scope = tamarin_task_scope_begin();
    EXPECT_TRUE(scope != nullptr);

    constexpr int NUM_TASKS = 50;
    for (int i = 0; i < NUM_TASKS; ++i) {
        tamarin_task_spawn(scope, [](void* ctx) {
            auto* c = static_cast<std::atomic<int64_t>*>(ctx);
            c->fetch_add(1, std::memory_order_relaxed);
        }, &counter);
    }

    tamarin_task_scope_end(scope);
    EXPECT_EQ(counter.load(), NUM_TASKS);
    return true;
}

TAMARIN_TEST(Runtime, Concurrency_AsyncTaskLifecycle) {
    int64_t inputVal = 42;
    void* task = tamarin_async_task_create([](void* ctx, void* res) {
        int64_t v = *static_cast<int64_t*>(ctx);
        *static_cast<int64_t*>(res) = v * 2;
    }, &inputVal, sizeof(int64_t), sizeof(int64_t));

    EXPECT_TRUE(task != nullptr);
    int64_t* result = static_cast<int64_t*>(tamarin_async_task_await(task));
    EXPECT_TRUE(result != nullptr);
    EXPECT_EQ(*result, 84);
    EXPECT_EQ(tamarin_async_task_is_ready(task), 1);
    tamarin_async_task_free(task);

    // Unawaited task recycling test
    for (int i = 0; i < 70; ++i) {
        tamarin_async_task_create([](void* ctx, void* res) {
            *static_cast<int64_t*>(res) = 123;
        }, &inputVal, sizeof(int64_t), sizeof(int64_t));
    }
    tamarin_yield();
    return true;
}

TAMARIN_TEST(Runtime, Slab_GenerationalRetirement) {
    TamarinSlab slab;
    __tamarin_slab_init(&slab, sizeof(uint64_t), alignof(uint64_t), 4);

    uint64_t val = 12345;
    uint64_t h1 = __tamarin_slab_insert(&slab, &val);
    EXPECT_TRUE(__tamarin_slab_is_valid(&slab, h1));

    uint32_t slot = static_cast<uint32_t>(h1 & 0xFFFFFFFF);
    slab.generations[slot] = 0xFFFFFFFD; // Odd -> active
    uint64_t retiredHandle = (static_cast<uint64_t>(0xFFFFFFFD) << 32) | slot;
    EXPECT_TRUE(__tamarin_slab_is_valid(&slab, retiredHandle));

    __tamarin_slab_remove(&slab, retiredHandle, nullptr);
    EXPECT_EQ(slab.generations[slot], 0xFFFFFFFF);

    EXPECT_EQ(__tamarin_slab_is_valid(&slab, retiredHandle), 0);
    uint64_t genMaxHandle = (static_cast<uint64_t>(0xFFFFFFFF) << 32) | slot;
    EXPECT_EQ(__tamarin_slab_is_valid(&slab, genMaxHandle), 0);

    __tamarin_slab_clear(&slab, nullptr);
    EXPECT_EQ(slab.generations[slot], 0xFFFFFFFF);

    __tamarin_slab_destroy(&slab, nullptr);
    return true;
}

TAMARIN_TEST(Troop, TestPackageWithProfileFlagsAndDllDeploy) {
    auto tempBase = std::filesystem::temp_directory_path() / "troop_test_pkg_profile";
    std::error_code ec;
    std::filesystem::remove_all(tempBase, ec);
    std::filesystem::create_directories(tempBase);

    PackageManager pm;
    std::string err;
    auto pkgDir = tempBase / "test_app";
    EXPECT_TRUE(pm.newPackage(pkgDir, "test_app", false, err));

    int testRes = pm.testPackage(pkgDir / "troop.toml", false);
    EXPECT_EQ(testRes, 0);

    std::filesystem::remove_all(tempBase, ec);
    return true;
}
