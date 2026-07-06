#pragma once

#include <string>
#include <vector>
#include <optional>
#include <compare>

namespace angara {

/// Semver version (major.minor.patch[-prerelease][+build]).
struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;
    std::string prerelease;  // e.g. "alpha.1"
    std::string build;       // e.g. "20260101"

    /// Parse a version string. Returns std::nullopt on failure.
    static std::optional<Version> parse(const std::string& s);

    /// Format as "major.minor.patch[-prerelease][+build]".
    std::string to_string() const;

    auto operator<=>(const Version&) const = default;
};

/// A version constraint that a Version must satisfy.
///
/// Supported constraint operators:
///   ^1.2.3  — compatible with 1.2.3 (>=1.2.3, <2.0.0)
///   ~1.2.3  — approximately 1.2.3 (>=1.2.3, <1.3.0)
///   >=1.0.0 — greater than or equal
///   >1.0.0  — strictly greater
///   <=1.0.0 — less than or equal
///   <1.0.0  — strictly less
///   =1.2.3  — exactly equal
///   *       — any version
///
/// Multiple constraints separated by spaces are AND-ed together.
class VersionConstraint {
public:
    /// Parse a constraint string. Returns std::nullopt on failure.
    static std::optional<VersionConstraint> parse(const std::string& s);

    /// Returns true if the given version satisfies this constraint.
    bool is_satisfied_by(const Version& v) const;

    /// Returns the original constraint string.
    std::string to_string() const;

    /// Returns true if this is the wildcard "*" constraint.
    bool is_any() const;

private:
    enum class Op { EQ, GE, GT, LE, LT, CARET, TILDE, ANY };

    struct Clause {
        Op op;
        Version version;
    };

    std::vector<Clause> m_clauses;
    std::string m_raw;

    bool clause_matches(const Clause& c, const Version& v) const;
};

} // namespace angara
