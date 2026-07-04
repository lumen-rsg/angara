#include "Formatter.h"
#include <cstdio>

namespace angara {

    // LANG-4: render a CHAR token's decimal-code-point lexeme back as a char
    // literal body, reversing the common escapes for readable output.
    // LANG-5: for code points > 0xFF, use \\u{XXXXXX} instead of truncated \\xHH.
    static std::string renderCharLexeme(const std::string& lexeme) {
        long cp = 0;
        try { cp = std::stol(lexeme); } catch (...) { return "?"; }
        switch (cp) {
            case '\n': return "\\n";
            case '\r': return "\\r";
            case '\t': return "\\t";
            case '\\': return "\\\\";
            case '\'': return "\\'";
            case '\0': return "\\0";
            default:
                if (cp >= 0x20 && cp < 0x7F) return std::string(1, static_cast<char>(cp));
                if (cp >= 0x80 && cp <= 0xFF) {
                    char buf[6];
                    std::snprintf(buf, sizeof(buf), "\\x%02lX", cp);
                    return buf;
                }
                // Code point > 0xFF: use Unicode escape.
                char buf[12];
                std::snprintf(buf, sizeof(buf), "\\u{%lX}", cp);
                return buf;
        }
    }

void Formatter::writeIndent() {
    if (m_at_line_start) {
        for (int i = 0; i < m_indent; i++) m_out << "    ";
        m_at_line_start = false;
    }
}

void Formatter::increaseIndent() { m_indent++; }
void Formatter::decreaseIndent() { m_indent--; }
void Formatter::newLine() { m_out << "\n"; m_at_line_start = true; }
void Formatter::write(const std::string& s) { writeIndent(); m_out << s; }
void Formatter::writeLine(const std::string& s) { writeIndent(); m_out << s; newLine(); }

std::string Formatter::fmtExpr(const std::shared_ptr<Expr>& expr) {
    if (!expr) return "";
    auto result = expr->accept(*this);
    return std::any_cast<std::string>(result);
}

void Formatter::fmtExprInto(const std::shared_ptr<Expr>& expr) {
    if (!expr) return;
    auto result = expr->accept(*this);
    m_out << std::any_cast<std::string>(result);
}

void Formatter::fmtStmt(const std::shared_ptr<Stmt>& stmt) {
    if (!stmt) return;
    stmt->accept(*this, stmt);
}

void Formatter::fmtBlock(const BlockStmt* block) {
    writeLine("{");
    increaseIndent();
    for (auto& s : block->statements) fmtStmt(s);
    decreaseIndent();
    writeLine("}");
}

void Formatter::fmtParams(const std::vector<Parameter>& params, bool has_this) {
    m_out << "(";
    bool first = true;
    if (has_this) { m_out << "this"; first = false; }
    for (auto& p : params) {
        if (!first) m_out << ", ";
        first = false;
        m_out << p.name.lexeme;
        if (p.type) { m_out << " as "; fmtType(p.type); }
        if (p.is_variadic) m_out << "...";
    }
    m_out << ")";
}

void Formatter::fmtType(const std::shared_ptr<ASTType>& type) {
    if (!type) return;
    if (auto* t = dynamic_cast<const SimpleType*>(type.get())) {
        m_out << t->name.lexeme;
    } else if (auto* t = dynamic_cast<const GenericType*>(type.get())) {
        m_out << t->name.lexeme << "<";
        bool first = true;
        for (auto& a : t->arguments) { if (!first) m_out << ", "; first = false; fmtType(a); }
        m_out << ">";
    } else if (auto* t = dynamic_cast<const FunctionTypeExpr*>(type.get())) {
        m_out << "function(";
        bool first = true;
        for (auto& p : t->param_types) { if (!first) m_out << ", "; first = false; fmtType(p); }
        m_out << ") -> "; fmtType(t->return_type);
    } else if (auto* t = dynamic_cast<const RecordTypeExpr*>(type.get())) {
        m_out << "{";
        bool first = true;
        for (auto& f : t->fields) { if (!first) m_out << ", "; first = false; m_out << f.name.lexeme << ": "; fmtType(f.type); }
        m_out << "}";
    } else if (auto* t = dynamic_cast<const OptionalTypeNode*>(type.get())) {
        fmtType(t->base_type); m_out << "?";
    } else if (auto* t = dynamic_cast<const FixedArrayTypeExpr*>(type.get())) {
        fmtType(t->element_type); m_out << "[" << t->size << "]";
    } else if (auto* t = dynamic_cast<const PointerTypeExpr*>(type.get())) {
        if (t->byval) m_out << "^";
        for (int i = 0; i < t->depth; i++) m_out << "*";
        fmtType(t->pointee_type);
    } else if (auto* t = dynamic_cast<const OwnedTypeNode*>(type.get())) {
        m_out << "@own "; fmtType(t->inner_type);
    }
}

void Formatter::fmtAccess(AccessLevel access) {
    if (access == AccessLevel::PUBLIC) writeLine("public:");
    else if (access == AccessLevel::PRIVATE) writeLine("private:");
}

void Formatter::fmtMembers(const std::vector<std::shared_ptr<ClassMember>>& members) {
    for (auto& member : members) {
        if (auto* field = dynamic_cast<const FieldMember*>(member.get())) {
            if (field->access == AccessLevel::PUBLIC || field->access == AccessLevel::PRIVATE) fmtAccess(field->access);
            fmtStmt(field->declaration);
        } else if (auto* method = dynamic_cast<const MethodMember*>(member.get())) {
            if (method->access == AccessLevel::PUBLIC || method->access == AccessLevel::PRIVATE) fmtAccess(method->access);
            fmtStmt(method->declaration);
        }
    }
}


std::string Formatter::format(const std::vector<std::shared_ptr<Stmt>>& statements) {
    m_out.str(""); m_indent = 0; m_at_line_start = true;
    for (auto& s : statements) fmtStmt(s);
    return m_out.str();
}


void Formatter::visit(std::shared_ptr<const ExpressionStmt> stmt) {
    writeLine(fmtExpr(stmt->expression) + ";");
}

void Formatter::visit(std::shared_ptr<const VarDeclStmt> stmt) {
    std::string prefix;
    if (stmt->is_static) prefix += "static ";
    prefix += stmt->is_const ? "const " : "let ";
    write(prefix + stmt->name.lexeme);
    if (stmt->typeAnnotation) { m_out << " as "; fmtType(stmt->typeAnnotation); }
    if (stmt->initializer) m_out << " = " << fmtExpr(stmt->initializer);
    m_out << ";"; newLine();
}

void Formatter::visit(std::shared_ptr<const BlockStmt> stmt) { fmtBlock(stmt.get()); }

void Formatter::visit(std::shared_ptr<const IfStmt> stmt) {
    write("if (");
    if (stmt->declaration) {
        m_out << "let " << stmt->declaration->name.lexeme;
        if (stmt->declaration->typeAnnotation) { m_out << " as "; fmtType(stmt->declaration->typeAnnotation); }
        m_out << " = " << fmtExpr(stmt->condition);
    } else {
        fmtExprInto(stmt->condition);
    }
    m_out << ") ";
    fmtStmt(stmt->thenBranch);
    if (stmt->elseBranch) {
        if (auto* elseIf = dynamic_cast<const IfStmt*>(stmt->elseBranch.get())) {
            write("orif (");
            if (elseIf->declaration) {
                m_out << "let " << elseIf->declaration->name.lexeme;
                if (elseIf->declaration->typeAnnotation) { m_out << " as "; fmtType(elseIf->declaration->typeAnnotation); }
                m_out << " = " << fmtExpr(elseIf->condition);
            } else {
                fmtExprInto(elseIf->condition);
            }
            m_out << ") ";
            fmtStmt(elseIf->thenBranch);
            if (elseIf->elseBranch) { write("else "); fmtStmt(elseIf->elseBranch); }
        } else {
            write("else "); fmtStmt(stmt->elseBranch);
        }
    }
}

void Formatter::visit(std::shared_ptr<const WhileStmt> stmt) {
    write("while ("); fmtExprInto(stmt->condition); m_out << ") ";
    if (auto* block = dynamic_cast<const BlockStmt*>(stmt->body.get())) {
        fmtBlock(block);
    } else { newLine(); increaseIndent(); fmtStmt(stmt->body); decreaseIndent(); }
}

void Formatter::visit(std::shared_ptr<const ForStmt> stmt) {
    write("for (");
    if (stmt->initializer) fmtStmt(stmt->initializer);
    if (stmt->condition) fmtExprInto(stmt->condition);
    m_out << "; ";
    if (stmt->increment) fmtExprInto(stmt->increment);
    m_out << ")"; newLine();
}

void Formatter::visit(std::shared_ptr<const ForInStmt> stmt) {
    writeLine("for (" + stmt->name.lexeme + " in " + fmtExpr(stmt->collection) + ")");
    if (auto* block = dynamic_cast<const BlockStmt*>(stmt->body.get())) {
        fmtBlock(block);
    } else { increaseIndent(); fmtStmt(stmt->body); decreaseIndent(); }
}

void Formatter::visit(std::shared_ptr<const FuncStmt> stmt) {
    std::string prefix;
    if (stmt->is_exported) prefix += "export ";
    if (stmt->is_intrinsic) prefix += "intrinsic ";
    if (stmt->is_foreign) prefix += "foreign ";
    if (stmt->is_static) prefix += "static ";
    prefix += "func ";
    write(prefix + stmt->name.lexeme);
    if (!stmt->type_params.empty()) {
        m_out << "<";
        bool first = true;
        for (auto& tp : stmt->type_params) { if (!first) m_out << ", "; first = false; m_out << tp.lexeme; }
        m_out << ">";
    }
    fmtParams(stmt->params, stmt->has_this);
    if (stmt->returnType) { m_out << " -> "; fmtType(stmt->returnType); }
    if (stmt->body) {
        m_out << " {";
        newLine();
        increaseIndent();
        for (auto& s : *stmt->body) fmtStmt(s);
        decreaseIndent();
        writeLine("}");
    } else { m_out << ";"; newLine(); }
}

void Formatter::visit(std::shared_ptr<const ReturnStmt> stmt) {
    writeLine(stmt->value ? "return " + fmtExpr(stmt->value) + ";" : "return;");
}

void Formatter::visit(std::shared_ptr<const AttachStmt> stmt) {
    write("attach ");
    if (!stmt->names.empty()) {
        bool first = true;
        for (auto& n : stmt->names) { if (!first) m_out << ", "; first = false; m_out << n.lexeme; }
        m_out << " from " << stmt->modulePath.lexeme;
    } else {
        m_out << stmt->modulePath.lexeme;
        if (stmt->alias) m_out << " as " << stmt->alias->lexeme;
    }
    m_out << ";"; newLine();
}

void Formatter::visit(std::shared_ptr<const ThrowStmt> stmt) {
    writeLine("throw " + fmtExpr(stmt->expression) + ";");
}

void Formatter::visit(std::shared_ptr<const TryStmt> stmt) {
    write("try ");
    if (auto* block = dynamic_cast<const BlockStmt*>(stmt->tryBlock.get())) fmtBlock(block);
    else fmtStmt(stmt->tryBlock);
    write("catch (");
    m_out << stmt->catchName.lexeme;
    if (stmt->catchType) { m_out << " as "; fmtType(stmt->catchType); }
    m_out << ") ";
    if (auto* block = dynamic_cast<const BlockStmt*>(stmt->catchBlock.get())) fmtBlock(block);
    else fmtStmt(stmt->catchBlock);
}

void Formatter::visit(std::shared_ptr<const ClassStmt> stmt) {
    std::string prefix;
    if (stmt->is_exported) prefix += "export ";
    write(prefix + "class " + stmt->name.lexeme);
    if (stmt->superclass) m_out << " inherits " << stmt->superclass->name.lexeme;
    if (!stmt->traits.empty()) {
        m_out << " uses "; bool first = true;
        for (auto& t : stmt->traits) { if (!first) m_out << ", "; first = false; m_out << t->name.lexeme; }
    }
    if (!stmt->contracts.empty()) {
        m_out << " signs "; bool first = true;
        for (auto& c : stmt->contracts) { if (!first) m_out << ", "; first = false; m_out << c->name.lexeme; }
    }
    m_out << " {"; newLine();
    increaseIndent(); fmtMembers(stmt->members); decreaseIndent();
    writeLine("}");
}

void Formatter::visit(std::shared_ptr<const TraitStmt> stmt) {
    std::string prefix;
    if (stmt->is_exported) prefix += "export ";
    write(prefix + "trait " + stmt->name.lexeme + " {"); newLine();
    increaseIndent(); for (auto& m : stmt->methods) fmtStmt(m); decreaseIndent();
    writeLine("}");
}

void Formatter::visit(std::shared_ptr<const ContractStmt> stmt) {
    std::string prefix;
    if (stmt->is_exported) prefix += "export ";
    write(prefix + "contract " + stmt->name.lexeme + " {"); newLine();
    increaseIndent(); fmtMembers(stmt->members); decreaseIndent();
    writeLine("}");
}

void Formatter::visit(std::shared_ptr<const BreakStmt>) { writeLine("break;"); }
void Formatter::visit(std::shared_ptr<const ContinueStmt>) { writeLine("continue;"); }

void Formatter::visit(std::shared_ptr<const DataStmt> stmt) {
    std::string prefix;
    if (stmt->is_exported) prefix += "export ";
    if (stmt->is_foreign) prefix += "foreign ";
    prefix += stmt->is_union ? "union " : "data ";
    write(prefix + stmt->name.lexeme);
    if (!stmt->type_params.empty()) {
        m_out << "<"; bool first = true;
        for (auto& tp : stmt->type_params) { if (!first) m_out << ", "; first = false; m_out << tp.lexeme; }
        m_out << ">";
    }
    if (stmt->is_opaque) { m_out << ";"; newLine(); return; }
    m_out << " {"; newLine();
    increaseIndent(); for (auto& f : stmt->fields) fmtStmt(f); decreaseIndent();
    writeLine("}");
}

void Formatter::visit(std::shared_ptr<const EnumStmt> stmt) {
    std::string prefix;
    if (stmt->is_exported) prefix += "export ";
    write(prefix + "enum " + stmt->name.lexeme + " {"); newLine();
    increaseIndent();
    for (auto& v : stmt->variants) {
        write(v->name.lexeme);
        if (!v->params.empty()) {
            m_out << "("; bool first = true;
            for (auto& p : v->params) { if (!first) m_out << ", "; first = false; fmtType(p.type); }
            m_out << ")";
        }
        m_out << ","; newLine();
    }
    decreaseIndent(); writeLine("}");
}

void Formatter::visit(std::shared_ptr<const UnsafeBlockStmt> stmt) {
    write("@unsafe "); fmtBlock(stmt->block.get());
}

void Formatter::visit(std::shared_ptr<const EmptyStmt>) { writeLine(";"); }


std::any Formatter::visit(const Literal& expr) {
    if (expr.token.type == TokenType::STRING) return "\"" + expr.token.lexeme + "\"";
    if (expr.token.type == TokenType::CHAR) return "'" + renderCharLexeme(expr.token.lexeme) + "'";  // LANG-4
    return expr.token.lexeme;
}
std::any Formatter::visit(const Binary& expr) { return fmtExpr(expr.left) + " " + expr.op.lexeme + " " + fmtExpr(expr.right); }
std::any Formatter::visit(const Unary& expr) { return expr.op.lexeme + fmtExpr(expr.right); }
std::any Formatter::visit(const Grouping& expr) { return "(" + fmtExpr(expr.expression) + ")"; }
std::any Formatter::visit(const VarExpr& expr) { return expr.name.lexeme; }
std::any Formatter::visit(const AssignExpr& expr) { return fmtExpr(expr.target) + " " + expr.op.lexeme + " " + fmtExpr(expr.value); }

std::any Formatter::visit(const UpdateExpr& expr) {
    return expr.isPrefix ? expr.op.lexeme + fmtExpr(expr.target) : fmtExpr(expr.target) + expr.op.lexeme;
}

std::any Formatter::visit(const CallExpr& expr) {
    std::string r = fmtExpr(expr.callee) + "(";
    bool first = true;
    for (auto& a : expr.arguments) { if (!first) r += ", "; first = false; r += fmtExpr(a); }
    return r + ")";
}

std::any Formatter::visit(const GetExpr& expr) {
    return fmtExpr(expr.object) + expr.op.lexeme + expr.name.lexeme;
}

std::any Formatter::visit(const ListExpr& expr) {
    std::string r = "[";
    bool first = true;
    for (auto& e : expr.elements) { if (!first) r += ", "; first = false; r += fmtExpr(e); }
    return r + "]";
}

std::any Formatter::visit(const LogicalExpr& expr) {
    return fmtExpr(expr.left) + " " + expr.op.lexeme + " " + fmtExpr(expr.right);
}

std::any Formatter::visit(const SubscriptExpr& expr) {
    return fmtExpr(expr.object) + "[" + fmtExpr(expr.index) + "]";
}

std::any Formatter::visit(const RecordExpr& expr) {
    std::string r = "{";
    bool first = true;
    for (size_t i = 0; i < expr.keys.size(); i++) {
        if (!first) r += ", "; first = false;
        r += expr.keys[i].lexeme + ": " + fmtExpr(expr.values[i]);
    }
    return r + "}";
}

std::any Formatter::visit(const TernaryExpr& expr) {
    return fmtExpr(expr.condition) + " ? " + fmtExpr(expr.thenBranch) + " : " + fmtExpr(expr.elseBranch);
}

std::any Formatter::visit(const ThisExpr&) { return std::string("this"); }
std::any Formatter::visit(const SuperExpr& expr) {
    return expr.method ? "super." + expr.method->lexeme : std::string("super");
}

std::any Formatter::visit(const IsExpr& expr) {
    std::ostringstream tmp;
    tmp << fmtExpr(expr.object) << " is ";
    // Format type into tmp
    std::ostringstream saved;
    std::swap(m_out, saved);
    fmtType(expr.type);
    std::string type_str = m_out.str();
    std::swap(m_out, saved);
    tmp << type_str;
    return tmp.str();
}

std::any Formatter::visit(const CastExpr& expr) {
    std::ostringstream tmp;
    tmp << fmtExpr(expr.object) << " as ";
    std::ostringstream saved;
    std::swap(m_out, saved);
    fmtType(expr.target);
    std::string type_str = m_out.str();
    std::swap(m_out, saved);
    tmp << type_str;
    return tmp.str();
}

std::any Formatter::visit(const DerefExpr& expr) { return expr.op.lexeme + fmtExpr(expr.right); }

std::any Formatter::visit(const MatchExpr& expr) {
    std::string r = "match (" + fmtExpr(expr.condition) + ") { ";
    for (auto& c : expr.cases) {
        r += "case ";
        for (size_t pi = 0; pi < c.patterns.size(); ++pi) {
            if (pi > 0) r += " | ";
            r += fmtExpr(c.patterns[pi]);
        }
        if (!c.variables.empty()) {
            r += "(";
            for (size_t vi = 0; vi < c.variables.size(); ++vi) {
                if (vi > 0) r += ", ";
                r += c.variables[vi].lexeme;
            }
            r += ")";
        }
        if (c.guard) r += " if " + fmtExpr(*c.guard);
        r += ": " + fmtExpr(c.body) + ", ";
    }
    return r + "}";
}

std::any Formatter::visit(const LambdaExpr& expr) {
    std::string r = "func(";
    bool first = true;
    for (size_t i = 0; i < expr.param_names.size(); i++) {
        if (!first) r += ", "; first = false;
        r += expr.param_names[i].lexeme;
        if (i < expr.param_types.size()) {
            std::ostringstream tmp;
            std::ostringstream saved;
            std::swap(m_out, saved);
            fmtType(expr.param_types[i]);
            r += " as " + m_out.str();
            std::swap(m_out, saved);
        }
    }
    r += ")";
    if (expr.returnType) {
        std::ostringstream saved;
        std::swap(m_out, saved);
        fmtType(expr.returnType);
        r += " -> " + m_out.str();
        std::swap(m_out, saved);
    }
    r += " { ... }";
    return r;
}

std::any Formatter::visit(const RangeExpr& expr) {
    std::string r = fmtExpr(expr.left);
    r += (expr.op.type == TokenType::DOT_DOT_DOT) ? "..." : "..";
    r += fmtExpr(expr.right);
    return r;
}

std::any Formatter::visit(const InterpStringExpr& expr) {
    std::string r = "$\"";
    for (const auto& [lit, sub] : expr.segments) {
        r += lit;
        if (sub) r += "{" + fmtExpr(sub) + "}";
    }
    r += "\"";
    return r;
}

} // namespace angara

#include "Stmt.h"
namespace angara {
    void Formatter::visit(std::shared_ptr<const DropStmt> stmt) {
        m_out << "drop " << stmt->name.lexeme << ";\n";
    }
} // namespace angara
