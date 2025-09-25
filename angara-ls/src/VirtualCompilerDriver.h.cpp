//
// Created by cv2 on 24.09.2025.
//

#include "VirtualCompilerDriver.h"
namespace angara {
    std::string VirtualCompilerDriver::read_file(const std::string& path) {
        // 1. First, check if the requested path is an open file in the editor.
        if (m_docManager.is_open(path)) {
            // If yes, return the live, in-memory content instead of reading from disk.
            return m_docManager.get_content(path).value();
        }

        // 2. If it's not an open file (e.g., an attached module on disk),
        //    fall back to the base class's implementation to read from the real file system.
        return CompilerDriver::read_file(path);
    }
}
