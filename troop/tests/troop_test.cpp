// ============================================================================
// troop_test.cpp - Automated Unit Tests for Troop Package Manager
// ============================================================================
#include "test_harness.h"
#include "troop/semver.h"
#include "troop/manifest.h"
#include "troop/lockfile.h"
#include "troop/dependency_resolver.h"
#include "troop/package_manager.h"

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
