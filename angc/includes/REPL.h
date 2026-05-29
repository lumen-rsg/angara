#pragma once

#include <string>
#include <vector>
#include <memory>

namespace angara {

    class REPL {
    public:
        int run();

    private:
        void printPrompt();
        std::string readLine();
        void processInput(const std::string& input);

        // Accumulated declarations across REPL lines
        std::string m_preamble;
        int m_line_count = 0;
    };

} // namespace angara
