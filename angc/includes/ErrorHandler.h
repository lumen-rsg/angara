//
// Created by cv2 on 8/27/25.
//

#pragma once
#include <string>
#include <vector>
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

    private:
        std::vector<std::string> m_lines;
        bool m_hadError = false;
        bool m_hadWarning = false;
        int m_errorCount = 0;
        int m_warningCount = 0;
    };
}