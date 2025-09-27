//
// Created by cv2 on 27.09.2025.
//

#include "CTranspiler.h"

namespace angara {

    std::string CTranspiler::transpileSizeofExpr(const SizeofExpr& expr) {
        // 1. Look up the resolved inner type from the map we populated in the TypeChecker.
        auto it = m_type_checker.m_sizeof_resolutions.find(&expr);
        if (it == m_type_checker.m_sizeof_resolutions.end()) {
            return "/* <compiler_error_unresolved_sizeof> */";
        }
        auto resolved_type = it->second;

        // 2. Get the corresponding C type name.
        std::string c_type_name = getCTypeNameForSizeof(resolved_type);

        // 3. Generate the C `sizeof` expression and box the result into a u64 AngaraObject.
        return "angara_from_c_u64(sizeof(" + c_type_name + "))";
    }

} // namespace angara