//
// Created by cv2 on 9/19/25.
//
#include "TypeChecker.h"
namespace angara {

    std::any TypeChecker::visit(const RecordExpr& expr) {
        std::map<std::string, std::shared_ptr<Type>> inferred_fields;

        // 1. Iterate through all the key-value pairs in the literal.
        for (size_t i = 0; i < expr.keys.size(); ++i) {
            const Token& key_token = expr.keys[i];
            const std::string& key_name = key_token.lexeme;
            const auto& value_expr = expr.values[i];

            // 2. Check for duplicate keys in the same literal.
            if (inferred_fields.count(key_name)) {
                error(key_token, "Duplicate field '" + key_name + "' in record literal.");
                // Continue checking the rest of the fields even if one is a duplicate.
            }

            // 3. Recursively type check the value expression to infer its type.
            value_expr->accept(*this);
            auto value_type = popType();

            // 4. Store the inferred field name and type.
            inferred_fields[key_name] = value_type;
        }

        // 5. Create a new internal RecordType from the inferred fields and push it.
        pushAndSave(&expr, std::make_shared<RecordType>(inferred_fields));

        return {};
    }

}