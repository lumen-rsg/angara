#include "LLVMBackend.h"

namespace angara {

llvm::Value* LLVMBackend::codegenLiteral(const Literal& expr) {
    auto type = m_type_checker.m_expression_types.at(&expr);
    if (type->toString() == "i64")
        return createAngaraI64(std::stoll(expr.token.lexeme));
    if (type->toString() == "f64")
        return createAngaraF64(std::stod(expr.token.lexeme));
    if (type->toString() == "bool")
        return createAngaraBool(expr.token.lexeme == "true");
    if (type->toString() == "string")
        return createAngaraString(expr.token.lexeme);
    if (type->toString() == "nil")
        return createAngaraNil();
    return createAngaraNil();
}

llvm::Value* LLVMBackend::codegenGrouping(const Grouping& expr) {
    return codegenExpr(expr.expression);
}

llvm::Value* LLVMBackend::codegenVarExpr(const VarExpr& expr) {
    auto symbol = m_type_checker.m_variable_resolutions.at(&expr);
    const std::string& name = sanitizeName(expr.name.lexeme);

    // Check local scope first, then globals
    if (m_named_values.count(name))
        return loadVariable(name);
    if (m_globals.count(name))
        return loadVariable(name);

    // Module-level closure variable pattern: g_module_name
    std::string global_name = "g_" + name;
    if (m_globals.count(global_name))
        return loadVariable(global_name);

    // Cross-module closure: g_modulename_funcname
    if (symbol && symbol->from_module && !symbol->from_module->is_native) {
        std::string cross_mod = "g_" + symbol->from_module->name + "_" + name;
        if (m_globals.count(cross_mod))
            return loadVariable(cross_mod);
    }

    return createAngaraNil();
}

llvm::Value* LLVMBackend::codegenThisExpr(const ThisExpr& expr) {
    return loadVariable("this");
}

llvm::Value* LLVMBackend::codegenSuperExpr(const SuperExpr& expr) {
    // super() or super.method() - delegate to runtime class dispatch
    return loadVariable("this");
}

llvm::Value* LLVMBackend::codegenSizeofExpr(const SizeofExpr& expr) {
    // sizeof maps to the C sizeof equivalent - use a reasonable default
    return createAngaraI64(8);
}

llvm::Value* LLVMBackend::codegenRetypeExpr(const RetypeExpr& expr) {
    // retype<T>(c_ptr) - pass through as-is
    return codegenExpr(expr.expression);
}

} // namespace angara