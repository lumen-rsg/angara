#include "TypeChecker.h"
namespace angara {

    std::any TypeChecker::visit(const RecordExpr& expr) {
        // Freestanding gate (E917): records are heap-allocated and need the
        // record runtime, neither of which exists on bare metal.
        // F11: relaxed when a freestanding allocator is enabled.
        if (m_is_in_freestanding_mode && !m_has_fs_allocator) {
            error(expr.keys.empty() ? Token() : expr.keys[0],
                  "Record literals are not available in --freestanding mode "
                  "(records require heap allocation and the record runtime). "
                  "Lay data out in plain integer globals or foreign structs instead.",
                  "E917");
            pushAndSave(&expr, m_type_error);
            return {};
        }
        std::map<std::string, std::shared_ptr<Type>> inferred_fields;

        for (size_t i = 0; i < expr.keys.size(); ++i) {
            const Token& key_token = expr.keys[i];
            const std::string& key_name = key_token.lexeme;
            const auto& value_expr = expr.values[i];

            if (inferred_fields.count(key_name)) {
                error(key_token, "Duplicate field '" + key_name + "' in record literal.", "E382");
            }

            value_expr->accept(*this);
            auto value_type = popType();

            inferred_fields[key_name] = value_type;
        }

        pushAndSave(&expr, std::make_shared<RecordType>(inferred_fields));

        return {};
    }

}
