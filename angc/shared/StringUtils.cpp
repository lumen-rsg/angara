//
// Created by cv2 on 9/20/25.
//

#include "StringUtils.h"
#include <vector>
#include <iostream>
#include <algorithm>

namespace angara {

    size_t levenshtein_distance(const std::string& s1, const std::string& s2) {
        const size_t len1 = s1.size(), len2 = s2.size();
        std::vector<size_t> col(len2 + 1), prevCol(len2 + 1);

        for (size_t i = 0; i < prevCol.size(); i++) {
            prevCol[i] = i;
        }

        for (size_t i = 0; i < len1; i++) {
            col[0] = i + 1;
            for (size_t j = 0; j < len2; j++) {
                col[j + 1] = std::min({ prevCol[1 + j] + 1, col[j] + 1, prevCol[j] + (s1[i] == s2[j] ? 0 : 1) });
            }
            col.swap(prevCol);
        }
        return prevCol[len2];
    }

    std::string shell_escape(const std::string& arg) {
        std::string escaped = "'";
        for (char c : arg) {
            if (c == '\'') {
                escaped += "'\\''";
            } else {
                escaped += c;
            }
        }
        escaped += "'";
        return escaped;
    }

    bool is_safe_flags(const std::string& flags, const char* context) {
        for (size_t i = 0; i < flags.size(); i++) {
            char c = flags[i];
            // Reject shell metacharacters that enable command injection.
            // H14: also reject '$', '(', ')', '#' — '$' enables $(...) and ${...}
            // expansion/subshell injection, parens form subshells and command
            // grouping, and '#' starts a shell comment. All slip past the
            // original blocklist.
            if (c == ';' || c == '|' || c == '&' || c == '`' ||
                c == '>' || c == '<' || c == '\n' || c == '\r' || c == '\0' ||
                c == '$' || c == '(' || c == ')' || c == '#') {
                std::cerr << "[SECURITY] Dangerous character '" << c
                          << "' (0x" << std::hex << static_cast<int>(c) << std::dec
                          << ") in " << context << " flags rejected to prevent "
                          << "command injection." << std::endl;
                return false;
            }
        }
        return true;
    }

} // namespace angara