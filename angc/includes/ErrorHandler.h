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

        virtual void report(const Token &token, const std::string &message);
        virtual void note(const Token &token, const std::string &message);

        bool hadError() const;

        void clearError();

    private:
        std::vector<std::string> m_lines;
        bool m_hadError = false;
    };
}