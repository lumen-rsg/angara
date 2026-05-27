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

} // namespace angara
