//
// Created by cv2 on 27.09.2025.
//

#include "CTranspiler.h"

namespace angara {

    std::string CTranspiler::transpileRetypeExpr(const RetypeExpr& expr) {
        // The `retype` operator now has a runtime effect. It must call a helper
        // to wrap the raw c_ptr in the correct Angara wrapper struct.

        // 1. Transpile the inner expression (the c_ptr).
        std::string inner_expr_str = transpileExpr(expr.expression);

        // 2. Get the semantic type of the target wrapper.
        auto target_type = m_type_checker.resolveType(expr.target_type);

        // 3. Get the C name of the wrapper struct.
        std::string c_wrapper_name = "Angara_" + target_type->toString();

        // 4. Generate the call to the runtime helper.
        return "angara_retype_c_ptr(" + inner_expr_str + ", sizeof(struct " + c_wrapper_name + "))";
    }

} // namespace angara