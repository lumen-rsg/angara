//
// Created by cv2 on 9/20/25.
//
#pragma once

#include <string>

namespace angara {

// Calculates the Levenshtein distance between two strings.
// The distance is the number of single-character edits (insertions,
// deletions, or substitutions) required to change one string into the other.
    size_t levenshtein_distance(const std::string& s1, const std::string& s2);

    // Wraps a string in single quotes, escaping any embedded single quotes.
    // Safe to interpolate into a shell command passed to system().
    std::string shell_escape(const std::string& arg);

    // C7: Returns true if the string contains only characters safe for
    // compiler/linker flags (no shell metacharacters like ; | & ` > <).
    // Prints a warning to stderr and returns false if dangerous chars are found.
    bool is_safe_flags(const std::string& flags, const char* context);

    // L8: Returns true if `path` is a safe workspace-relative path: no `..`
    // components (path traversal), no null bytes, and not absolute. Config
    // fields like `path`/`entry`/`sources`/`include_dirs` are joined onto the
    // workspace root, so an absolute value or a `..`-component escapes it.
    // Prints a [SECURITY] warning and returns false on a rejected path.
    bool is_safe_path(const std::string& path, const char* context);

} // namespace angara
