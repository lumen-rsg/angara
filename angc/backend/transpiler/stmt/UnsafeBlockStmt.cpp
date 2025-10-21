//
// Created by cv2 on 16.10.2025.
//

#include "CTranspiler.h"

namespace angara {

    void CTranspiler::transpileUnsafeBlockStmt(const UnsafeBlockStmt& stmt) {
        // The @unsafe block is a compile-time construct. At runtime,
        // it is just a regular C block. We can simply transpile
        // the inner BlockStmt.
        transpileBlock(*(stmt.block));
    }

} // namespace angara