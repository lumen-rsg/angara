//
// Created by cv2 on 9/19/25.
//

#include "CTranspiler.h"
namespace angara {

    void CTranspiler::transpileVarDecl(const VarDeclStmt& stmt) {
        indent();
        auto var_type = m_type_checker.m_variable_types.at(&stmt);

        if (stmt.is_const) (*m_current_out) << "const ";
        (*m_current_out) << "AngaraObject " << sanitize_name(stmt.name.lexeme) ;

        if (stmt.initializer) {
            (*m_current_out) << " = " << transpileExpr(stmt.initializer);
        } else {
            (*m_current_out) << " = angara_create_nil()";
        }
        (*m_current_out) << ";\n";
    }

}