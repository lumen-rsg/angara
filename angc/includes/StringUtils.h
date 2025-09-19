//
// Created by cv2 on 9/20/25.
//

#ifndef ANGARA_STRINGUTILS_H
#define ANGARA_STRINGUTILS_H

#include <string>

namespace angara {

// Calculates the Levenshtein distance between two strings.
// The distance is the number of single-character edits (insertions,
// deletions, or substitutions) required to change one string into the other.
    size_t levenshtein_distance(const std::string& s1, const std::string& s2);

} // namespace angara

#endif // ANGARA_STRINGUTILS_H


