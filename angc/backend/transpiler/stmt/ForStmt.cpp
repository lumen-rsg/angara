//
// Created by cv2 on 9/19/25.
//
#include "CTranspiler.h"
namespace angara {

    void CTranspiler::transpileForStmt(const ForStmt& stmt) {
        indent();
        // In C, the scope of a for-loop initializer is the loop itself.
        // So we don't need an extra `{}` block unless the body isn't one.
        (*m_current_out) << "for (";

        // --- 1. Initializer ---
        if (stmt.initializer) {
            // The initializer is a full statement. We need to generate its code
            // but without the trailing semicolon and newline. We can achieve this
            // by temporarily redirecting the output stream.
            std::stringstream init_ss;
            std::stringstream* temp_out = m_current_out;
            m_current_out = &init_ss;
            m_indent_level = 0; // No indent inside the for()

            transpileStmt(stmt.initializer);

            // Restore original stream and level
            m_current_out = temp_out;
            m_indent_level = 1; // Assuming we are inside main

            std::string init_str = init_ss.str();
            // Trim whitespace, semicolon, and newline from the end
            size_t last = init_str.find_last_not_of(" \n\r\t;");
            (*m_current_out) << (last == std::string::npos ? "" : init_str.substr(0, last + 1));
        }
        (*m_current_out) << "; ";

        // --- 2. Condition ---
        if (stmt.condition) {
            (*m_current_out) << "angara_is_truthy(" << transpileExpr(stmt.condition) << ")";
        }
        (*m_current_out) << "; ";

        // --- 3. Increment ---
        if (stmt.increment) {
            (*m_current_out) << transpileExpr(stmt.increment);
        }
        (*m_current_out) << ") ";

        // --- 4. Body ---
        transpileStmt(stmt.body);
    }

}