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

    bool is_safe_path(const std::string& path, const char* context) {
        if (path.empty()) return true;  // empty is fine (unset/optional)

        // Null byte would truncate the path when handed to C APIs.
        if (path.find('\0') != std::string::npos) {
            std::cerr << "[SECURITY] Null byte in " << context
                      << " path rejected: \"" << path << "\"." << std::endl;
            return false;
        }

        // Absolute paths escape the workspace root (config paths are relative).
        // Treat both POSIX '/' and Windows '\' drive roots as absolute.
        if (path.front() == '/' || path.front() == '\\' ||
            (path.size() >= 2 && path[1] == ':')) {
            std::cerr << "[SECURITY] Absolute " << context
                      << " path rejected (must be workspace-relative): \""
                      << path << "\"." << std::endl;
            return false;
        }

        // Reject any path *component* equal to ".." (parent traversal). Split
        // on both separators so Windows-style '\' is covered too. A literal
        // ".." as a component — not merely a substring — is the real escape;
        // "foo..bar" / "..hidden" are benign file names.
        size_t start = 0;
        for (size_t i = 0; i <= path.size(); ++i) {
            if (i == path.size() || path[i] == '/' || path[i] == '\\') {
                if (i - start == 2 && path[start] == '.' && path[start + 1] == '.') {
                    std::cerr << "[SECURITY] Path traversal (\"..\") in " << context
                              << " path rejected: \"" << path << "\"." << std::endl;
                    return false;
                }
                start = i + 1;
            }
        }
        return true;
    }

} // namespace angara