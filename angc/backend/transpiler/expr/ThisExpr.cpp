//
// Created by cv2 on 9/19/25.
//

#include "CTranspiler.h"
namespace angara {

    std::string CTranspiler::transpileThisExpr(const ThisExpr& expr) {
        // Inside a method, the first parameter is always the instance object.
        // Our convention is to name this C parameter 'this_obj'.
        return "this_obj";
    }

}