#include "CLI.h"
#include <llvm/Support/TargetSelect.h>

int main(int argc, char* argv[]) {
    // One-time LLVM target initialization — must be called exactly once,
    // before any LLVMBackend is constructed.  These are NOT thread-safe
    // and must not be called concurrently (TOOL-2 / P1.1).
    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmParsers();
    llvm::InitializeAllAsmPrinters();

    angara::CLI cli;
    return cli.run(argc, argv);
}
