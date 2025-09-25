//
// Created by cv2 on 24.09.2025.
//
#pragma once

#include "CompilerDriver.h"
#include "DocumentManager.h"

namespace angara {
    class VirtualCompilerDriver final : public CompilerDriver {
    public:
        explicit VirtualCompilerDriver(DocumentManager& docManager) : m_docManager(docManager) {}

    protected:
        // Override the file reading method
        std::string read_file(const std::string& path) override;

    private:
        DocumentManager& m_docManager;
    };

}