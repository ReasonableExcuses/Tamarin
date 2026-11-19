// ============================================================================
// semver.cpp - Semantic Versioning implementation
// ============================================================================
#include "troop/semver.h"

#include <charconv>
#include <sstream>
#include <algorithm>
#include <cctype>

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

std::optional<SemVer> SemVer::parse(std::string_view str) {
    str = trim(str);
    if (str.empty()) return std::nullopt;
    if (str.front() == 'v' || str.front() == 'V') {
        str.remove_prefix(1);
    }

    SemVer res;

    // Split build metadata if present (+)
    auto plusPos = str.find('+');
    if (plusPos != std::string_view::npos) {
        res.buildMeta = std::string(str.substr(plusPos + 1));
        str = str.substr(0, plusPos);
    }

    // Split pre-release if present (-)
    auto dashPos = str.find('-');
    if (dashPos != std::string_view::npos) {
        res.preRelease = std::string(str.substr(dashPos + 1));
        str = str.substr(0, dashPos);
    }

    // Parse major.minor.patch
    auto firstDot = str.find('.');
    if (firstDot == std::string_view::npos) {
        // Just major
        uint32_t val = 0;
        auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), val);
        if (ec != std::errc() || ptr != str.data() + str.size()) return std::nullopt;
        res.major = val;
        return res;
    }

    uint32_t majorVal = 0;
    auto [ptr1, ec1] = std::from_chars(str.data(), str.data() + firstDot, majorVal);
    if (ec1 != std::errc() || ptr1 != str.data() + firstDot) return std::nullopt;
    res.major = majorVal;

    auto rest = str.substr(firstDot + 1);
    auto secondDot = rest.find('.');
    if (secondDot == std::string_view::npos) {
        // major.minor
        uint32_t minorVal = 0;
        auto [ptr2, ec2] = std::from_chars(rest.data(), rest.data() + rest.size(), minorVal);
        if (ec2 != std::errc() || ptr2 != rest.data() + rest.size()) return std::nullopt;
        res.minor = minorVal;
        return res;
    }

    uint32_t minorVal = 0;
    auto [ptr2, ec2] = std::from_chars(rest.data(), rest.data() + secondDot, minorVal);
    if (ec2 != std::errc() || ptr2 != rest.data() + secondDot) return std::nullopt;
    res.minor = minorVal;

    auto patchStr = rest.substr(secondDot + 1);
    uint32_t patchVal = 0;
    auto [ptr3, ec3] = std::from_chars(patchStr.data(), patchStr.data() + patchStr.size(), patchVal);
    if (ec3 != std::errc() || ptr3 != patchStr.data() + patchStr.size()) return std::nullopt;
    res.patch = patchVal;

    return res;
}

std::string SemVer::toString() const {
    std::ostringstream ss;
    ss << major << "." << minor << "." << patch;
    if (!preRelease.empty()) {
        ss << "-" << preRelease;
    }
    if (!buildMeta.empty()) {
        ss << "+" << buildMeta;
    }
    return ss.str();
}

bool VersionPredicate::matches(const SemVer& ver) const {
    switch (op) {
        case VersionOp::Exact:
            return ver == target;

        case VersionOp::Caret: {
            // ^1.2.3 := >=1.2.3, <2.0.0
            // ^0.2.3 := >=0.2.3, <0.3.0
            // ^0.0.3 := ==0.0.3
            if (ver < target) return false;
            if (target.major > 0) {
                return ver.major == target.major;
            }
            if (target.minor > 0) {
                return ver.major == 0 && ver.minor == target.minor;
            }
            return ver.major == 0 && ver.minor == 0 && ver.patch == target.patch;
        }

        case VersionOp::Tilde: {
            // ~1.2.3 := >=1.2.3, <1.3.0
            if (ver < target) return false;
            return ver.major == target.major && ver.minor == target.minor;
        }

        case VersionOp::GreaterThan:
            return ver > target;

        case VersionOp::GreaterThanOrEqual:
            return ver >= target;

        case VersionOp::LessThan:
            return ver < target;

        case VersionOp::LessThanOrEqual:
            return ver <= target;

        case VersionOp::Wildcard: {
            if (wildcardDepth == 0) return true; // "*"
            if (wildcardDepth == 1) return ver.major == target.major; // "1.*"
            if (wildcardDepth == 2) return ver.major == target.major && ver.minor == target.minor; // "1.2.*"
            return true;
        }
    }
    return false;
}

std::optional<VersionReq> VersionReq::parse(std::string_view str) {
    str = trim(str);
    if (str.empty()) return std::nullopt;

    VersionReq req;
    req.rawString_ = std::string(str);

    // Support comma-separated clauses: ">= 1.0.0, < 2.0.0"
    size_t start = 0;
    while (start < str.size()) {
        auto commaPos = str.find(',', start);
        std::string_view piece = (commaPos == std::string_view::npos)
                                     ? str.substr(start)
                                     : str.substr(start, commaPos - start);
        piece = trim(piece);

        if (!piece.empty()) {
            VersionPredicate pred;
            if (piece == "*") {
                pred.op = VersionOp::Wildcard;
                pred.wildcardDepth = 0;
            } else if (piece.starts_with(">=")) {
                pred.op = VersionOp::GreaterThanOrEqual;
                auto v = SemVer::parse(piece.substr(2));
                if (!v) return std::nullopt;
                pred.target = *v;
            } else if (piece.starts_with("<=")) {
                pred.op = VersionOp::LessThanOrEqual;
                auto v = SemVer::parse(piece.substr(2));
                if (!v) return std::nullopt;
                pred.target = *v;
            } else if (piece.starts_with('>')) {
                pred.op = VersionOp::GreaterThan;
                auto v = SemVer::parse(piece.substr(1));
                if (!v) return std::nullopt;
                pred.target = *v;
            } else if (piece.starts_with('<')) {
                pred.op = VersionOp::LessThan;
                auto v = SemVer::parse(piece.substr(1));
                if (!v) return std::nullopt;
                pred.target = *v;
            } else if (piece.starts_with('=')) {
                pred.op = VersionOp::Exact;
                auto v = SemVer::parse(piece.substr(1));
                if (!v) return std::nullopt;
                pred.target = *v;
            } else if (piece.starts_with('^')) {
                pred.op = VersionOp::Caret;
                auto v = SemVer::parse(piece.substr(1));
                if (!v) return std::nullopt;
                pred.target = *v;
            } else if (piece.starts_with('~')) {
                pred.op = VersionOp::Tilde;
                auto v = SemVer::parse(piece.substr(1));
                if (!v) return std::nullopt;
                pred.target = *v;
            } else if (piece.find('*') != std::string_view::npos) {
                pred.op = VersionOp::Wildcard;
                if (piece.starts_with("*")) {
                    pred.wildcardDepth = 0;
                } else {
                    auto dot = piece.find('.');
                    if (dot != std::string_view::npos) {
                        auto nextDot = piece.find('.', dot + 1);
                        if (nextDot == std::string_view::npos) {
                            pred.wildcardDepth = 1; // "1.*"
                            auto v = SemVer::parse(piece.substr(0, dot));
                            if (!v) return std::nullopt;
                            pred.target = *v;
                        } else {
                            pred.wildcardDepth = 2; // "1.2.*"
                            auto v = SemVer::parse(piece.substr(0, nextDot));
                            if (!v) return std::nullopt;
                            pred.target = *v;
                        }
                    }
                }
            } else {
                // Default: caret requirement (like Cargo/Rust: "1.2.3" means "^1.2.3")
                pred.op = VersionOp::Caret;
                auto v = SemVer::parse(piece);
                if (!v) return std::nullopt;
                pred.target = *v;
            }
            req.predicates_.push_back(pred);
        }

        if (commaPos == std::string_view::npos) break;
        start = commaPos + 1;
    }

    if (req.predicates_.empty()) return std::nullopt;
    return req;
}

bool VersionReq::matches(const SemVer& ver) const {
    for (const auto& pred : predicates_) {
        if (!pred.matches(ver)) return false;
    }
    return true;
}

std::string VersionReq::toString() const {
    if (!rawString_.empty()) return rawString_;
    if (predicates_.empty()) return "*";
    std::string out;
    for (size_t i = 0; i < predicates_.size(); ++i) {
        if (i > 0) out += ", ";
        const auto& p = predicates_[i];
        switch (p.op) {
            case VersionOp::Exact: out += "=" + p.target.toString(); break;
            case VersionOp::Caret: out += "^" + p.target.toString(); break;
            case VersionOp::Tilde: out += "~" + p.target.toString(); break;
            case VersionOp::GreaterThan: out += ">" + p.target.toString(); break;
            case VersionOp::GreaterThanOrEqual: out += ">=" + p.target.toString(); break;
            case VersionOp::LessThan: out += "<" + p.target.toString(); break;
            case VersionOp::LessThanOrEqual: out += "<=" + p.target.toString(); break;
            case VersionOp::Wildcard:
                if (p.wildcardDepth == 0) out += "*";
                else if (p.wildcardDepth == 1) out += std::to_string(p.target.major) + ".*";
                else out += std::to_string(p.target.major) + "." + std::to_string(p.target.minor) + ".*";
                break;
        }
    }
    return out;
}

} // namespace troop
