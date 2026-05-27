# Angara Audit Fixes Progress

## Critical Bugs
- [x] 1. Fix TargetMachine memory leak in LLVMBackend constructor
- [x] 2. Fix stoll/stod crash in ExprCodegen
- [x] 3. Fix null dereference in json_bridge functions
- [x] 4. Convert Token::to_string() from array to switch statement

## Code Quality
- [x] 5. Deduplicate type predicates (TypeChecker.cpp + LLVMBackend.cpp -> use Type.h)
- [x] 6. Move Token::print() to Token.cpp, remove <iostream> from Token.h
- [x] 7. Fix #include <Token.h> -> #include "Token.h" in Type.h
- [x] 8. Fix inconsistent include guards (StringUtils.h, Angara.h -> #pragma once)
- [x] 9. Deduplicate platform extension constants (shared Platform.h)
- [ ] 10. Deduplicate linker invocation logic (skipped — the two link paths have enough contextual differences that merging them would add complexity for little gain)
- [x] 11. Fix broad catch(...) blocks with logging
- [x] 12. Fix m_angara_module_names stale entries on codegen failure

## Build System
- [x] 13. Remove silent error swallowing (- prefix) in Makefile
- [x] 14. Add -Wextra -g -MMD -MP to Makefile

## CI
- [x] 15. Add PR triggers, run all test suites in CI
