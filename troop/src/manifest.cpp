// ============================================================================
// manifest.cpp - troop.toml Manifest Parser & Serializer Implementation
// ============================================================================
#include "troop/manifest.h"

#include <fstream>
#include <sstream>
#include <cctype>
#include <algorithm>

namespace troop {

static std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.remove_suffix(1);
    }
    return s;
}

static std::string stripQuotes(std::string_view s) {
    s = trim(s);
    if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\''))) {
        s.remove_prefix(1);
        s.remove_suffix(1);
    }
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char next = s[++i];
            if (next == 'n') out.push_back('\n');
            else if (next == 't') out.push_back('\t');
            else if (next == 'r') out.push_back('\r');
            else if (next == '\\') out.push_back('\\');
            else if (next == '"') out.push_back('"');
            else out.push_back(next);
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

static std::vector<std::string> parseStringArray(std::string_view s) {
    std::vector<std::string> out;
    s = trim(s);
    if (s.starts_with('[') && s.ends_with(']')) {
        s.remove_prefix(1);
        s.remove_suffix(1);
    }
    size_t start = 0;
    while (start < s.size()) {
        auto comma = s.find(',', start);
        std::string_view piece = (comma == std::string_view::npos) ? s.substr(start) : s.substr(start, comma - start);
        piece = trim(piece);
        if (!piece.empty()) {
            out.push_back(stripQuotes(piece));
        }
        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }
    return out;
}

static std::map<std::string, std::string> parseInlineTable(std::string_view s) {
    std::map<std::string, std::string> out;
    s = trim(s);
    if (s.starts_with('{') && s.ends_with('}')) {
        s.remove_prefix(1);
        s.remove_suffix(1);
    }
    size_t start = 0;
    while (start < s.size()) {
        auto comma = s.find(',', start);
        std::string_view piece = (comma == std::string_view::npos) ? s.substr(start) : s.substr(start, comma - start);
        piece = trim(piece);
        if (!piece.empty()) {
            auto eq = piece.find('=');
            if (eq != std::string_view::npos) {
                std::string k = std::string(trim(piece.substr(0, eq)));
                std::string v = stripQuotes(piece.substr(eq + 1));
                out[k] = v;
            }
        }
        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }
    return out;
}

std::string DependencySpec::toTomlString() const {
    if (sourceKind == DependencySourceKind::Path) {
        return "{ path = \"" + path + "\" }";
    }
    if (sourceKind == DependencySourceKind::Git) {
        std::string s = "{ git = \"" + gitUrl + "\"";
        if (!gitTag.empty()) s += ", tag = \"" + gitTag + "\"";
        if (!gitBranch.empty()) s += ", branch = \"" + gitBranch + "\"";
        s += " }";
        return s;
    }
    std::string vStr = versionReq.toString();
    if (vStr.empty()) vStr = "*";
    return "\"" + vStr + "\"";
}

std::optional<Manifest> Manifest::loadFromFile(const std::filesystem::path& path, std::string& errorMsg) {
    std::ifstream file(path);
    if (!file.is_open()) {
        errorMsg = "cannot open manifest file: " + path.string();
        return std::nullopt;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return parseString(buffer.str(), path, errorMsg);
}

std::optional<Manifest> Manifest::parseString(std::string_view content, const std::filesystem::path& manifestPath, std::string& errorMsg) {
    Manifest m;
    m.manifestPath = manifestPath;

    std::string section;
    std::istringstream stream{std::string(content)};
    std::string line;
    int lineNum = 0;

    while (std::getline(stream, line)) {
        lineNum++;
        std::string_view sv = trim(line);

        // Strip comments
        auto commentPos = sv.find('#');
        if (commentPos != std::string_view::npos) {
            sv = trim(sv.substr(0, commentPos));
        }

        if (sv.empty()) continue;

        // Check for section header [section]
        if (sv.front() == '[' && sv.back() == ']') {
            section = std::string(trim(sv.substr(1, sv.size() - 2)));
            continue;
        }

        // Key = Value
        auto eqPos = sv.find('=');
        if (eqPos == std::string_view::npos) {
            errorMsg = "line " + std::to_string(lineNum) + ": syntax error, expected 'key = value'";
            return std::nullopt;
        }

        std::string key = std::string(trim(sv.substr(0, eqPos)));
        std::string_view rawVal = trim(sv.substr(eqPos + 1));

        if (section == "package") {
            if (key == "name") {
                m.package.name = stripQuotes(rawVal);
            } else if (key == "version") {
                auto v = SemVer::parse(stripQuotes(rawVal));
                if (!v) {
                    errorMsg = "line " + std::to_string(lineNum) + ": invalid semver: " + std::string(rawVal);
                    return std::nullopt;
                }
                m.package.version = *v;
            } else if (key == "edition") {
                m.package.edition = stripQuotes(rawVal);
            } else if (key == "authors") {
                m.package.authors = parseStringArray(rawVal);
            } else if (key == "description") {
                m.package.description = stripQuotes(rawVal);
            } else if (key == "license") {
                m.package.license = stripQuotes(rawVal);
            } else if (key == "kind") {
                m.package.kind = stripQuotes(rawVal);
            } else if (key == "entry") {
                m.package.entryPoint = stripQuotes(rawVal);
            }
        } else if (section == "dependencies" || section == "dev-dependencies") {
            DependencySpec spec;
            spec.name = key;
            if (rawVal.starts_with('{')) {
                auto tbl = parseInlineTable(rawVal);
                if (tbl.count("path")) {
                    spec.sourceKind = DependencySourceKind::Path;
                    spec.path = tbl["path"];
                } else if (tbl.count("git")) {
                    spec.sourceKind = DependencySourceKind::Git;
                    spec.gitUrl = tbl["git"];
                    if (tbl.count("tag")) spec.gitTag = tbl["tag"];
                    if (tbl.count("branch")) spec.gitBranch = tbl["branch"];
                }
                if (tbl.count("version")) {
                    auto req = VersionReq::parse(tbl["version"]);
                    if (req) spec.versionReq = *req;
                }
            } else {
                spec.sourceKind = DependencySourceKind::Registry;
                auto req = VersionReq::parse(stripQuotes(rawVal));
                if (req) {
                    spec.versionReq = *req;
                } else {
                    auto fallbackReq = VersionReq::parse("*");
                    if (fallbackReq) spec.versionReq = *fallbackReq;
                }
            }
            if (section == "dependencies") {
                m.dependencies[key] = spec;
            } else {
                m.devDependencies[key] = spec;
            }
        } else if (section == "profile.dev") {
            if (key == "opt_level") {
                m.profileDev.optLevel = std::stoi(std::string(rawVal));
            } else if (key == "fast_math") {
                m.profileDev.fastMath = (rawVal == "true");
            } else if (key == "crt") {
                m.profileDev.crt = stripQuotes(rawVal);
            } else if (key == "lto") {
                m.profileDev.lto = (rawVal == "true");
            }
        } else if (section == "profile.release") {
            if (key == "opt_level") {
                m.profileRelease.optLevel = std::stoi(std::string(rawVal));
            } else if (key == "fast_math") {
                m.profileRelease.fastMath = (rawVal == "true");
            } else if (key == "crt") {
                m.profileRelease.crt = stripQuotes(rawVal);
            } else if (key == "lto") {
                m.profileRelease.lto = (rawVal == "true");
            }
        } else if (section == "workspace") {
            if (key == "members") {
                m.workspace.members = parseStringArray(rawVal);
            }
        }
    }

    if (m.package.name.empty() && !m.workspace.isWorkspace()) {
        errorMsg = "manifest missing [package] name";
        return std::nullopt;
    }

    return m;
}

Manifest Manifest::createDefault(const std::string& name, bool isLib) {
    Manifest m;
    m.package.name = name;
    m.package.version = SemVer{0, 1, 0, "", ""};
    m.package.edition = "2026";
    m.package.kind = isLib ? "lib" : "bin";
    m.package.entryPoint = isLib ? "src/lib.tam" : "src/main.tam";
    return m;
}

std::string Manifest::toToml() const {
    std::ostringstream ss;
    if (workspace.isWorkspace()) {
        ss << "[workspace]\nmembers = [";
        for (size_t i = 0; i < workspace.members.size(); ++i) {
            if (i > 0) ss << ", ";
            ss << "\"" << workspace.members[i] << "\"";
        }
        ss << "]\n\n";
    }

    if (!package.name.empty()) {
        ss << "[package]\n";
        ss << "name = \"" << package.name << "\"\n";
        ss << "version = \"" << package.version.toString() << "\"\n";
        ss << "edition = \"" << package.edition << "\"\n";
        if (!package.authors.empty()) {
            ss << "authors = [";
            for (size_t i = 0; i < package.authors.size(); ++i) {
                if (i > 0) ss << ", ";
                ss << "\"" << package.authors[i] << "\"";
            }
            ss << "]\n";
        }
        if (!package.description.empty()) {
            ss << "description = \"" << package.description << "\"\n";
        }
        if (!package.license.empty()) {
            ss << "license = \"" << package.license << "\"\n";
        }
        ss << "kind = \"" << package.kind << "\"\n";
        if (!package.entryPoint.empty()) {
            ss << "entry = \"" << package.entryPoint << "\"\n";
        }
        ss << "\n";
    }

    ss << "[dependencies]\n";
    for (const auto& [name, spec] : dependencies) {
        ss << name << " = " << spec.toTomlString() << "\n";
    }
    ss << "\n";

    if (!devDependencies.empty()) {
        ss << "[dev-dependencies]\n";
        for (const auto& [name, spec] : devDependencies) {
            ss << name << " = " << spec.toTomlString() << "\n";
        }
        ss << "\n";
    }

    ss << "[profile.dev]\n";
    ss << "opt_level = " << profileDev.optLevel << "\n";
    ss << "fast_math = " << (profileDev.fastMath ? "true" : "false") << "\n";
    ss << "crt = \"" << profileDev.crt << "\"\n\n";

    ss << "[profile.release]\n";
    ss << "opt_level = " << profileRelease.optLevel << "\n";
    ss << "fast_math = " << (profileRelease.fastMath ? "true" : "false") << "\n";
    ss << "crt = \"" << profileRelease.crt << "\"\n";

    return ss.str();
}

bool Manifest::saveToFile(const std::filesystem::path& path, std::string& errorMsg) const {
    std::ofstream file(path);
    if (!file.is_open()) {
        errorMsg = "cannot write manifest file: " + path.string();
        return false;
    }
    file << toToml();
    return true;
}

std::filesystem::path Manifest::getPackageRootDir() const {
    if (!manifestPath.empty()) {
        return manifestPath.parent_path();
    }
    return std::filesystem::current_path();
}

std::filesystem::path Manifest::getEntryPointPath() const {
    auto root = getPackageRootDir();
    if (!package.entryPoint.empty()) {
        return root / package.entryPoint;
    }
    if (package.kind == "lib") {
        return root / "src" / "lib.tam";
    }
    return root / "src" / "main.tam";
}

} // namespace troop
