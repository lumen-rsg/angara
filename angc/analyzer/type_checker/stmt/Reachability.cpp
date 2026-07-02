#include "TypeChecker.h"
#include "Stmt.h"
namespace angara {

    // TS-6: definite-return analysis. `definitelyReturns(stmt)` is true iff every
    // control-flow path through `stmt` ends in a `return` or `throw` (so control
    // cannot fall off the end of `stmt`). Used to verify that a non-void function
    // returns on every path. Conservative: loops and matches-as-last-statement are
    // treated as non-terminating (they may not execute / may not be exhaustive),
    // so the only false negatives are legal functions the analysis can't prove —
    // those produce a (recoverable) diagnostic the programmer silences by making
    // the return explicit, which is the safe direction.
    bool TypeChecker::definitelyReturns(const std::shared_ptr<const Stmt>& stmt) {
        if (!stmt) return false;

        if (auto block = std::dynamic_pointer_cast<const BlockStmt>(stmt)) {
            // A block definitely returns iff any statement in it does (everything
            // after a terminating statement is dead code).
            for (const auto& s : block->statements) {
                if (definitelyReturns(s)) return true;
            }
            return false;
        }

        if (std::dynamic_pointer_cast<const ReturnStmt>(stmt)) return true;
        if (std::dynamic_pointer_cast<const ThrowStmt>(stmt)) return true;

        if (auto if_stmt = std::dynamic_pointer_cast<const IfStmt>(stmt)) {
            // `if` definitely returns iff both branches do. No else => can't prove.
            if (!if_stmt->elseBranch) return false;
            return definitelyReturns(if_stmt->thenBranch) &&
                   definitelyReturns(if_stmt->elseBranch);
        }

        if (auto try_stmt = std::dynamic_pointer_cast<const TryStmt>(stmt)) {
            // try/catch definitely returns iff both the try block and the catch
            // block do. (A finally doesn't add termination; if the try throws and
            // the catch doesn't return, control falls through.) No catch => can't
            // prove (an uncaught throw propagates, but we can't rely on that).
            if (!try_stmt->catchBlock) return false;
            return definitelyReturns(try_stmt->tryBlock) &&
                   definitelyReturns(try_stmt->catchBlock);
        }

        // Loops (while/for/for-in) may execute zero times, so they never prove a
        // return on their own. (A `while(true){}` with no break is a real
        // terminator, but detecting it reliably needs condition evaluation — out
        // of scope; the programmer can add an explicit return after the loop.)
        // break/continue don't return a value, so they don't satisfy the
        // obligation either. Everything else (expression/var/drop/attach/empty/
        // nested fn/class/data/enum/contract/trait declarations) falls through.
        return false;
    }

}
