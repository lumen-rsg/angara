//
// Created by cv2 on 8/27/25.
//

#pragma once
#include <string>
#include <vector>
#include <set>
#include "Token.h"
namespace angara {
    class ErrorHandler {
    public:
        virtual ~ErrorHandler() = default;

        explicit ErrorHandler(const std::string &source);

        virtual void report(const Token &token, const std::string &message, const std::string &code = "");
        virtual void warning(const Token &token, const std::string &message, const std::string &code = "");
        virtual void note(const Token &token, const std::string &message);

        bool hadError() const;
        bool hadWarning() const;
        int errorCount() const;
        int warningCount() const;

        void clearError();
        void printSummary() const;

        /// Sets whether warnings should be promoted to errors.
        void set_warnings_as_errors(bool val) { m_warnings_as_errors = val; }

        /// Suppresses a specific warning code (e.g., "W003").
        void suppress_warning(const std::string &code) { m_suppressed_codes.insert(code); }

        /// Suppresses all warnings.
        void suppress_all_warnings(bool val) { m_suppress_all = val; }

    private:
        std::vector<std::string> m_lines;
        bool m_hadError = false;
        bool m_hadWarning = false;
        int m_errorCount = 0;
        int m_warningCount = 0;

        bool m_warnings_as_errors = false;
        bool m_suppress_all = false;
        std::set<std::string> m_suppressed_codes;
    };
}