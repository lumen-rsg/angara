#include "../../includes/Version.h"
#include <cctype>
#include <stdexcept>
#include <sstream>

namespace angara {

// ── Version parsing ────────────────────────────────────────────────────

std::optional<Version> Version::parse(const std::string& s) {
    Version v;
    const char* p = s.c_str();
    const char* end = p + s.size();

    // major
    char* next = nullptr;
    v.major = static_cast<int>(std::strtol(p, &next, 10));
    if (next == p) return std::nullopt;
    p = next;

    if (p >= end || *p != '.') return std::nullopt;
    p++; // skip '.'

    // minor
    v.minor = static_cast<int>(std::strtol(p, &next, 10));
    if (next == p) return std::nullopt;
    p = next;

    if (p >= end || *p != '.') return std::nullopt;
    p++; // skip '.'

    // patch
    v.patch = static_cast<int>(std::strtol(p, &next, 10));
    if (next == p) return std::nullopt;
    p = next;

    // optional prerelease
    if (p < end && *p == '-') {
        p++; // skip '-'
        const char* start = p;
        while (p < end && *p != '+') p++;
        v.prerelease = std::string(start, p - start);
    }

    // optional build
    if (p < end && *p == '+') {
        p++; // skip '+'
        v.build = std::string(p, end - p);
    } else if (p < end) {
        return std::nullopt; // trailing garbage
    }

    return v;
}

std::string Version::to_string() const {
    std::ostringstream oss;
    oss << major << "." << minor << "." << patch;
    if (!prerelease.empty()) oss << "-" << prerelease;
    if (!build.empty()) oss << "+" << build;
    return oss.str();
}

// ── VersionConstraint parsing ───────────────────────────────────────────

std::optional<VersionConstraint> VersionConstraint::parse(const std::string& s) {
    VersionConstraint vc;
    vc.m_raw = s;

    if (s == "*" || s.empty()) {
        vc.m_clauses.push_back({Op::ANY, Version{}});
        return vc;
    }

    std::istringstream stream(s);
    std::string token;
    while (stream >> token) {
        Clause c;
        const char* start = token.c_str();

        if (token.starts_with("^")) {
            c.op = Op::CARET;
            start = start + 1;
        } else if (token.starts_with("~")) {
            c.op = Op::TILDE;
            start = start + 1;
        } else if (token.starts_with(">=")) {
            c.op = Op::GE;
            start = start + 2;
        } else if (token.starts_with("<=")) {
            c.op = Op::LE;
            start = start + 2;
        } else if (token.starts_with(">")) {
            c.op = Op::GT;
            start = start + 1;
        } else if (token.starts_with("<")) {
            c.op = Op::LT;
            start = start + 1;
        } else if (token.starts_with("=")) {
            c.op = Op::EQ;
            start = start + 1;
        } else {
            c.op = Op::EQ;
        }

        auto v = Version::parse(std::string(start));
        if (!v) return std::nullopt;
        c.version = *v;

        vc.m_clauses.push_back(c);
    }

    if (vc.m_clauses.empty()) return std::nullopt;
    return vc;
}

bool VersionConstraint::is_any() const {
    return m_clauses.size() == 1 && m_clauses[0].op == Op::ANY;
}

std::string VersionConstraint::to_string() const {
    return m_raw;
}

// ── Clause matching ─────────────────────────────────────────────────────

bool VersionConstraint::clause_matches(const Clause& c, const Version& v) const {
    switch (c.op) {
    case Op::EQ: return v == c.version;
    case Op::GE: return v >= c.version;
    case Op::GT: return v > c.version;
    case Op::LE: return v <= c.version;
    case Op::LT: return v < c.version;
    case Op::ANY: return true;
    case Op::CARET: {
        // ^X.Y.Z  →  >=X.Y.Z, <(X+1).0.0   (if X>0)
        // ^0.Y.Z  →  >=0.Y.Z, <0.(Y+1).0   (if X=0, Y>0)
        // ^0.0.Z  →  >=0.0.Z, <0.0.(Z+1)   (if X=0, Y=0)
        if (v < c.version) return false;
        if (c.version.major > 0) {
            return v.major == c.version.major;
        } else if (c.version.minor > 0) {
            return v.major == 0 && v.minor == c.version.minor;
        } else {
            return v.major == 0 && v.minor == 0 && v.patch == c.version.patch;
        }
    }
    case Op::TILDE: {
        // ~X.Y.Z  →  >=X.Y.Z, <X.(Y+1).0
        if (v < c.version) return false;
        return v.major == c.version.major && v.minor == c.version.minor;
    }
    }
    return false;
}

bool VersionConstraint::is_satisfied_by(const Version& v) const {
    for (const auto& clause : m_clauses) {
        if (!clause_matches(clause, v)) return false;
    }
    return true;
}

} // namespace angara
