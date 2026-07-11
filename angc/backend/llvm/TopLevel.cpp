#include "LLVMBackend.h"
#include "RuntimeBuilder.h"

namespace angara {

void LLVMBackend::codegenTopLevelDecls(const std::vector<std::shared_ptr<Stmt>>& statements) {
    codegenNativeModuleDecls(statements);

    // v5: collect tracked type names for drop cascades.
    for (const auto& stmt : statements) {
        if (auto s = std::dynamic_pointer_cast<const ClassStmt>(stmt))
            m_tracked_types.insert(s->name.lexeme);
        else if (auto s = std::dynamic_pointer_cast<const DataStmt>(stmt))
            if (s->is_owned)
                m_tracked_types.insert(s->name.lexeme);
    }

    // Forward-declaration pre-pass: declare every source function's signature
    // (and populate m_raw_functions) BEFORE emitting any body. This makes
    // function definition order irrelevant — a call to a function defined later
    // in the source resolves to the correct mangled `__ang_<mod>_<fn>` symbol
    // instead of falling through to the native-ABI `Angara_<mod>_<fn>(i32,ptr)`
    // fallback (a different name + signature → undefined reference at link).
    // Intrinsic/foreign/main/async funcs are skipped here; they are declared
    // via their own dedicated paths in the main loop below.
    for (const auto& stmt : statements) {
        if (auto s = std::dynamic_pointer_cast<const FuncStmt>(stmt)) {
            if (s->is_intrinsic || s->is_foreign || s->name.lexeme == "main" || s->is_async)
                continue;
            declareFunctionSignature(*s, moduleName);
        }
    }

    for (const auto& stmt : statements) {
        if (auto s = std::dynamic_pointer_cast<const VarDeclStmt>(stmt))
            codegenGlobalVarDecl(*s);
        else if (auto s = std::dynamic_pointer_cast<const FuncStmt>(stmt)) {
            if (s->is_intrinsic) continue;
            if (s->is_foreign) { codegenForeignFuncDecl(*s); continue; }
            if (s->name.lexeme == "main") continue;
            // LIB-4: async functions use the state-machine codegen path
            if (s->is_async) { codegenAsyncFuncDecl(*s, moduleName); continue; }
            codegenFunctionDecl(*s, moduleName);
        }
        else if (auto s = std::dynamic_pointer_cast<const ClassStmt>(stmt))
            codegenClassDecl(*s);
        else if (auto s = std::dynamic_pointer_cast<const DataStmt>(stmt))
            codegenDataDecl(*s);
        else if (auto s = std::dynamic_pointer_cast<const EnumStmt>(stmt))
            codegenEnumDecl(*s);
        else if (std::dynamic_pointer_cast<const TypeAliasStmt>(stmt))
            continue;  // LANG-12: type aliases are compile-time only
    }
}

void LLVMBackend::codegenGlobalVarDecl(const VarDeclStmt& stmt) {
    const std::string name = "g_" + moduleName + "_" + sanitize(stmt.name.lexeme);

    // F3: no-heap byte-array const. A top-level `const X as [u8; N] = b"..."`
    // lowers to a .rodata [N x i8] constant global — NOT a boxed {i32,i64}
    // AngaraObject. Detect it here and skip the objType global + the runtime
    // store in codegenMainFunction entirely.
    auto sym = const_cast<SymbolTable&>(m_type_checker.getSymbolTable()).resolve(stmt.name.lexeme);
    if (sym && sym->type && sym->type->kind == TypeKind::FIXED_ARRAY) {
        auto fa = std::dynamic_pointer_cast<FixedArrayType>(sym->type);
        if (fa && fa->element_type &&
            (fa->element_type->toString() == "u8" || fa->element_type->toString() == "i8") &&
            stmt.initializer) {
            if (auto lit = std::dynamic_pointer_cast<const Literal>(stmt.initializer)) {
                if (lit->token.type == TokenType::BYTE_STRING) {
                    const std::string& bytes = lit->token.lexeme;
                    auto* i8_ty = llvm::Type::getInt8Ty(*ctx);
                    auto* arr_ty = llvm::ArrayType::get(i8_ty, fa->size);
                    // Copy bytes into a ConstantDataArray (no null terminator —
                    // the array size is exact, enforced by the type checker).
                    std::vector<llvm::Constant*> elems;
                    elems.reserve(fa->size);
                    for (int i = 0; i < fa->size; ++i) {
                        unsigned char byte_val = static_cast<unsigned char>(i < static_cast<int>(bytes.size()) ? bytes[i] : 0);
                        elems.push_back(llvm::ConstantInt::get(i8_ty, byte_val));
                    }
                    auto* arr_const = llvm::ConstantArray::get(arr_ty, elems);
                    auto* gv = new llvm::GlobalVariable(
                        *mod, arr_ty, /*isConstant=*/true,
                        llvm::GlobalValue::InternalLinkage, arr_const, name);
                    gv->setDSOLocal(true);
                    gv->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
                    // Record under both the full name and the bare name (mirrors
                    // the globals[] dual-keying below) so loadVar finds it.
                    const std::string bare = sanitize(stmt.name.lexeme);
                    m_byte_array_globals[name] = {gv, fa->size};
                    m_byte_array_globals[bare] = {gv, fa->size};
                    m_byte_array_globals["g_" + bare] = {gv, fa->size};
                    return;  // skip the objType global
                }
            }
        }
    }

    auto* init_const = llvm::ConstantAggregateZero::get(objType);

    auto* global = new llvm::GlobalVariable(
        *mod, objType, false,
        llvm::GlobalValue::PrivateLinkage, init_const, name);

    globals[name] = global;
    globals["g_" + sanitize(stmt.name.lexeme)] = global;

    // For foreign const: also declare the external C global
    if (stmt.is_foreign) {
        auto sym = const_cast<SymbolTable&>(m_type_checker.getSymbolTable()).resolve(stmt.name.lexeme);
        if (sym && sym->type) {
            auto* c_type = resolveCFieldType(sym->type);
            auto* c_global = mod->getGlobalVariable(stmt.name.lexeme);
            if (!c_global) {
                c_global = new llvm::GlobalVariable(
                    *mod, c_type, false,
                    llvm::GlobalValue::ExternalLinkage, nullptr, stmt.name.lexeme);
            }
        }
    }
}

llvm::Function* LLVMBackend::declareFunctionSignature(const FuncStmt& stmt, const std::string& module_name) {
    const std::string func_name = mangle(module_name, stmt.name.lexeme);

    // Resolve semantic function type
    auto sem_sym = const_cast<SymbolTable&>(m_type_checker.getSymbolTable()).resolve(stmt.name.lexeme);
    auto sem_fn_type = (sem_sym && sem_sym->type && sem_sym->type->kind == TypeKind::FUNCTION)
        ? std::dynamic_pointer_cast<FunctionType>(sem_sym->type) : nullptr;

    // Check if this function can use a raw (unboxed) signature. Foreign funcs
    // use the FFI-marshallable set (includes `string`→char* and pointer types),
    // so `foreign func strlen(s as string) -> i64` gets a real C signature.
    // Non-foreign funcs keep the stricter unboxable set (locals only).
    // Exported functions are always boxed — cross-module calls use the
    // uniform (objType...) -> objType ABI, avoiding signature mismatches
    // between the defining and calling modules.
    bool is_foreign_fn = stmt.is_foreign;
    bool is_raw = false;
    RawFuncInfo raw_info;
    raw_info.return_kind = LocalKind::BOXED;

    if (sem_fn_type && stmt.type_params.empty() && !stmt.is_exported) {
        auto ret_type = sem_fn_type->return_type;
        auto can_marshal = is_foreign_fn ? isFFIMarshallable(ret_type)
                                        : (ret_type && isUnboxableType(ret_type));
        raw_info.return_kind = can_marshal
            ? (is_foreign_fn ? ffiKindForType(ret_type) : localKindForType(ret_type))
            : LocalKind::BOXED;

        bool all_marshalable = can_marshal;
        for (size_t i = 0; i < sem_fn_type->param_types.size() && all_marshalable; i++) {
            auto& pt = sem_fn_type->param_types[i];
            bool ok = is_foreign_fn ? isFFIMarshallable(pt) : isUnboxableType(pt);
            if (!ok) all_marshalable = false;
        }

        if (all_marshalable) {
            is_raw = true;
            for (size_t i = 0; i < sem_fn_type->param_types.size(); i++) {
                auto& pt = sem_fn_type->param_types[i];
                raw_info.param_kinds.push_back(is_foreign_fn ? ffiKindForType(pt)
                                                             : localKindForType(pt));
            }
            if (is_foreign_fn) {
                // Carry the semantic types so the call site can marshal
                // (string→char*) via marshalAngaraToC instead of plain unbox.
                raw_info.param_types = sem_fn_type->param_types;
                raw_info.return_type = ret_type;
            }
            m_raw_functions[func_name] = raw_info;
        }
    }

    // Build LLVM function signature
    std::vector<llvm::Type*> param_types;
    if (is_raw) {
        for (auto& kind : raw_info.param_kinds) {
            param_types.push_back(llvmTypeForLocalKind(kind));
        }
    } else {
        param_types.assign(stmt.params.size(), objType);
    }
    auto* fn_ret_type = is_raw ? llvmTypeForLocalKind(raw_info.return_kind) : objType;
    auto* fn_type = llvm::FunctionType::get(fn_ret_type, param_types, false);

    auto* fn = mod->getFunction(func_name);
    if (!fn) {
        fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage,
                                     func_name, mod.get());
    }
    return fn;
}

void LLVMBackend::codegenFunctionDecl(const FuncStmt& stmt, const std::string& module_name) {
    // Compute the signature (raw vs boxed) and create/reuse the llvm::Function.
    // The signature pre-pass (codegenTopLevelDecls) has already declared every
    // function, so this typically returns the existing declaration; the body is
    // emitted into it below. Keeping the signature logic in declareFunctionSignature
    // ensures forward references and definitions agree on type.
    llvm::Function* fn = declareFunctionSignature(stmt, module_name);
    const std::string func_name = mangle(module_name, stmt.name.lexeme);
    bool is_raw = (m_raw_functions.count(func_name) > 0);
    // Re-derive the semantic function type (for parameter type registration
    // and destructuring) and the raw-info struct (for return-kind handling).
    // Both were computed inside declareFunctionSignature; re-resolving here
    // avoids threading them through a return value.
    auto sem_sym = const_cast<SymbolTable&>(m_type_checker.getSymbolTable()).resolve(stmt.name.lexeme);
    auto sem_fn_type = (sem_sym && sem_sym->type && sem_sym->type->kind == TypeKind::FUNCTION)
        ? std::dynamic_pointer_cast<FunctionType>(sem_sym->type) : nullptr;
    RawFuncInfo raw_info;
    if (is_raw) raw_info = m_raw_functions[func_name];

    // SIMD-2: @inline annotation — force inlining at every call site
    if (stmt.is_inline) {
        fn->addFnAttr(llvm::Attribute::AlwaysInline);
    }

    // Attach DWARF debug info to this function in debug mode
    llvm::DIScope* saved_di_scope = m_di_scope;
    if (m_debug && m_di_builder && m_di_file) {
        std::string src_file = stmt.name.file ? *stmt.name.file : "unknown";
        auto diFile = getOrCreateDIFile(src_file);
        auto diFuncType = m_di_builder->createSubroutineType(
            m_di_builder->getOrCreateTypeArray({}));
        auto sp = m_di_builder->createFunction(
            diFile, stmt.name.lexeme, func_name, diFile,
            stmt.name.line, diFuncType, stmt.name.column,
            llvm::DINode::FlagZero, llvm::DISubprogram::SPFlagDefinition);
        fn->setSubprogram(sp);
        m_di_scope = sp;
        builder->SetCurrentDebugLocation(
            llvm::DILocation::get(*ctx, stmt.name.line, stmt.name.column, sp));
    }

    size_t idx = 0;
    for (auto& arg : fn->args()) {
        arg.setName(sanitize(stmt.params[idx].name.lexeme));
        idx++;
    }

    auto* entry = llvm::BasicBlock::Create(*ctx, "entry", fn);
    builder->SetInsertPoint(entry);

    auto saved_values = std::move(namedVals);
    auto saved_types = std::move(namedTypes);
    auto saved_kinds = std::move(namedKinds);
    namedVals.clear();
    namedTypes.clear();
    namedKinds.clear();

    // Register parameters
    idx = 0;
    for (auto& arg : fn->args()) {
        auto pname = sanitize(stmt.params[idx].name.lexeme);
        auto param_type = (sem_fn_type && idx < sem_fn_type->param_types.size())
            ? sem_fn_type->param_types[idx] : nullptr;
        auto* alloca = allocLocal(fn, pname, param_type);
        namedVals[pname] = alloca;
        if (param_type) {
            namedTypes[pname] = param_type;
            namedKinds[pname] = isUnboxableType(param_type)
                ? localKindForType(param_type) : LocalKind::BOXED;
        } else {
            namedKinds[pname] = LocalKind::BOXED;
        }
        // RT-2: emit debug info for the parameter (spilled to a stack alloca).
        emitDbgDeclare(alloca, pname, stmt.params[idx].name.line,
                       stmt.params[idx].name.column, namedKinds[pname]);
        if (is_raw) {
            // Raw arg arrives as raw type — store directly
            builder->CreateStore(&arg, alloca);
        } else {
            // Boxed arg arrives as objType — storeVar handles unboxing if raw alloca
            storeVar(pname, &arg);
        }
        idx++;
    }

    // LANG-10: destructured parameters — extract elements from tuple arguments
    for (size_t pi = 0; pi < stmt.params.size(); ++pi) {
        const auto& param = stmt.params[pi];
        if (param.destructure_names.empty()) continue;

        // The primary parameter holds the tuple value in its alloca.
        auto primary_pname = sanitize(param.name.lexeme);
        auto it = namedVals.find(primary_pname);
        if (it == namedVals.end()) continue;
        auto* tuple_alloca = it->second;

        // Resolve the tuple type
        auto param_type = (sem_fn_type && pi < sem_fn_type->param_types.size())
            ? sem_fn_type->param_types[pi] : nullptr;
        auto tuple_type = (param_type && param_type->kind == TypeKind::TUPLE)
            ? std::dynamic_pointer_cast<TupleType>(param_type) : nullptr;

        for (size_t di = 0; di < param.destructure_names.size(); ++di) {
            auto dname = sanitize(param.destructure_names[di].lexeme);
            auto* idx_val = makeI64(llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), di));
            auto* elem = callRtByName("__ang_list_get", {
                builder->CreateLoad(objType, tuple_alloca), idx_val
            });

            auto* alloca = allocLocal(fn, dname);
            namedVals[dname] = alloca;
            namedKinds[dname] = LocalKind::BOXED;
            if (tuple_type && di < tuple_type->element_types.size()) {
                namedTypes[dname] = tuple_type->element_types[di];
            }
            emitDbgDeclare(alloca, dname,
                param.destructure_names[di].line,
                param.destructure_names[di].column, LocalKind::BOXED);
            builder->CreateStore(elem, alloca);
        }
    }

    // Track whether we're inside a raw-signature function
    auto saved_raw_ret = m_current_raw_return_kind;
    m_current_raw_return_kind = is_raw ? raw_info.return_kind : std::optional<LocalKind>{};

    // Reset per-function codegen state. m_exc_chain_save holds an LLVM Value
    // (an alloca) from emitRtPushFrame; if a prior function set it and this one
    // doesn't push a frame, a stale value would make emitRtPopFrame reference
    // an instruction in another function → LLVM module-verify failure. Same for
    // the inlined-main members. Reset before the conditional push below.
    m_exc_chain_save = nullptr;
    m_inlined_main_ret_alloca = nullptr;
    m_inlined_main_cleanup_bb = nullptr;

    // Only push runtime frame if the function has heap-referencing values.
    // Raw primitive-only functions (like fib) don't need runtime tracking at all.
    bool needs_gc = !is_raw || functionNeedsGC(stmt);
    if (needs_gc) {
        emitRtPushFrame(fn, 256);
    }

    if (stmt.body) {
        for (const auto& s : *stmt.body) {
            if (builder->GetInsertBlock()->getTerminator()) break;
            cgStmt(s);
        }
    }

    if (!builder->GetInsertBlock()->getTerminator()) {
        if (m_exc_chain_save) emitRtPopFrame();
        if (is_raw) {
            auto* zero = llvm::ConstantInt::get(llvmTypeForLocalKind(raw_info.return_kind), 0);
            builder->CreateRet(zero);
        } else {
            builder->CreateRet(makeNil());
        }
    }

    namedVals = std::move(saved_values);
    namedTypes = std::move(saved_types);
    namedKinds = std::move(saved_kinds);
    m_current_raw_return_kind = saved_raw_ret;
    m_di_scope = saved_di_scope;
    if (m_debug) builder->SetCurrentDebugLocation(llvm::DebugLoc());
}

// LIB-4: walk expressions to collect AwaitExpr nodes.
void LLVMBackend::collectAwaitStates(const std::shared_ptr<Expr>& expr,
                                      std::vector<const AwaitExpr*>& awaits) {
    if (!expr) return;
    if (auto* a = dynamic_cast<const AwaitExpr*>(expr.get())) {
        awaits.push_back(a);
        return;
    }
    // Recurse into sub-expressions
    if (auto* b = dynamic_cast<const Binary*>(expr.get())) {
        collectAwaitStates(b->left, awaits); collectAwaitStates(b->right, awaits);
    } else if (auto* u = dynamic_cast<const Unary*>(expr.get())) {
        collectAwaitStates(u->right, awaits);
    } else if (auto* g = dynamic_cast<const Grouping*>(expr.get())) {
        collectAwaitStates(g->expression, awaits);
    } else if (auto* c = dynamic_cast<const CallExpr*>(expr.get())) {
        collectAwaitStates(c->callee, awaits);
        for (auto& a : c->arguments) collectAwaitStates(a, awaits);
    } else if (auto* gt = dynamic_cast<const GetExpr*>(expr.get())) {
        collectAwaitStates(gt->object, awaits);
    } else if (auto* l = dynamic_cast<const ListExpr*>(expr.get())) {
        for (auto& e : l->elements) collectAwaitStates(e, awaits);
    } else if (auto* tup = dynamic_cast<const TupleExpr*>(expr.get())) {
        for (auto& e : tup->elements) collectAwaitStates(e, awaits);
    } else if (auto* lo = dynamic_cast<const LogicalExpr*>(expr.get())) {
        collectAwaitStates(lo->left, awaits); collectAwaitStates(lo->right, awaits);
    } else if (auto* su = dynamic_cast<const SubscriptExpr*>(expr.get())) {
        collectAwaitStates(su->object, awaits); collectAwaitStates(su->index, awaits);
    } else if (auto* re = dynamic_cast<const RecordExpr*>(expr.get())) {
        for (auto& v : re->values) collectAwaitStates(v, awaits);
    } else if (auto* te = dynamic_cast<const TernaryExpr*>(expr.get())) {
        collectAwaitStates(te->condition, awaits);
        collectAwaitStates(te->thenBranch, awaits);
        collectAwaitStates(te->elseBranch, awaits);
    } else if (auto* ie = dynamic_cast<const IsExpr*>(expr.get())) {
        collectAwaitStates(ie->object, awaits);
    } else if (auto* ce = dynamic_cast<const CastExpr*>(expr.get())) {
        collectAwaitStates(ce->object, awaits);
    } else if (auto* de = dynamic_cast<const DerefExpr*>(expr.get())) {
        collectAwaitStates(de->right, awaits);
    } else if (auto* re = dynamic_cast<const RangeExpr*>(expr.get())) {
        collectAwaitStates(re->left, awaits); collectAwaitStates(re->right, awaits);
    } else if (auto* is = dynamic_cast<const InterpStringExpr*>(expr.get())) {
        for (auto& s : is->segments) if (s.second) collectAwaitStates(s.second, awaits);
    } else if (auto* me = dynamic_cast<const MatchExpr*>(expr.get())) {
        collectAwaitStates(me->condition, awaits);
        for (auto& c : me->cases) {
            for (auto& p : c.patterns) collectAwaitStates(p, awaits);
            if (c.guard) collectAwaitStates(*c.guard, awaits);
            if (c.body) collectAwaitStates(c.body, awaits);
        }
    } else if (auto* lam = dynamic_cast<const LambdaExpr*>(expr.get())) {
        // Don't recurse into lambda bodies — they're separate functions.
        return;
    } else if (auto* as = dynamic_cast<const AssignExpr*>(expr.get())) {
        collectAwaitStates(as->target, awaits);
        collectAwaitStates(as->value, awaits);
    } else if (auto* upd = dynamic_cast<const UpdateExpr*>(expr.get())) {
        collectAwaitStates(upd->target, awaits);
    }
}

void LLVMBackend::collectAwaitStatesStmt(const std::shared_ptr<Stmt>& stmt,
                                          std::vector<const AwaitExpr*>& awaits) {
    if (!stmt) return;
    if (auto* es = dynamic_cast<const ExpressionStmt*>(stmt.get())) {
        collectAwaitStates(es->expression, awaits);
    } else if (auto* vd = dynamic_cast<const VarDeclStmt*>(stmt.get())) {
        if (vd->initializer) collectAwaitStates(vd->initializer, awaits);
    } else if (auto* ret = dynamic_cast<const ReturnStmt*>(stmt.get())) {
        if (ret->value) collectAwaitStates(ret->value, awaits);
    } else if (auto* ifs = dynamic_cast<const IfStmt*>(stmt.get())) {
        collectAwaitStates(ifs->condition, awaits);
        if (ifs->thenBranch) collectAwaitStatesStmt(ifs->thenBranch, awaits);
        if (ifs->elseBranch) collectAwaitStatesStmt(ifs->elseBranch, awaits);
    } else if (auto* wh = dynamic_cast<const WhileStmt*>(stmt.get())) {
        collectAwaitStates(wh->condition, awaits);
        if (wh->body) collectAwaitStatesStmt(wh->body, awaits);
    } else if (auto* fr = dynamic_cast<const ForStmt*>(stmt.get())) {
        if (fr->initializer) collectAwaitStatesStmt(fr->initializer, awaits);
        if (fr->condition) collectAwaitStates(fr->condition, awaits);
        if (fr->increment) collectAwaitStates(fr->increment, awaits);
        if (fr->body) collectAwaitStatesStmt(fr->body, awaits);
    } else if (auto* fi = dynamic_cast<const ForInStmt*>(stmt.get())) {
        collectAwaitStates(fi->collection, awaits);
        if (fi->body) collectAwaitStatesStmt(fi->body, awaits);
    } else if (auto* blk = dynamic_cast<const BlockStmt*>(stmt.get())) {
        for (auto& s : blk->statements) collectAwaitStatesStmt(s, awaits);
    } else if (auto* thr = dynamic_cast<const ThrowStmt*>(stmt.get())) {
        collectAwaitStates(thr->expression, awaits);
    } else if (auto* trys = dynamic_cast<const TryStmt*>(stmt.get())) {
        if (trys->tryBlock) collectAwaitStatesStmt(trys->tryBlock, awaits);
        if (trys->catchBlock) collectAwaitStatesStmt(trys->catchBlock, awaits);
        if (trys->finallyBlock) collectAwaitStatesStmt(trys->finallyBlock, awaits);
    } else if (auto* uns = dynamic_cast<const UnsafeBlockStmt*>(stmt.get())) {
        for (auto& s : uns->block->statements) collectAwaitStatesStmt(s, awaits);
    } else if (auto* prv = dynamic_cast<const PrivilegedBlockStmt*>(stmt.get())) {
        for (auto& s : prv->block->statements) collectAwaitStatesStmt(s, awaits);
    }
    // DropStmt, BreakStmt, ContinueStmt, EmptyStmt: no expressions to scan
}

// LIB-4 Stage S: walk statements to collect VarDeclStmt nodes for frame slots.
void LLVMBackend::collectAsyncLocals(const std::shared_ptr<Stmt>& stmt,
                                      std::vector<std::pair<std::string, int>>& locals,
                                      std::map<std::string, LocalKind>& local_kinds,
                                      int& next_slot) {
    if (!stmt) return;
    if (auto* vd = dynamic_cast<const VarDeclStmt*>(stmt.get())) {
        std::string name = sanitize(vd->name.lexeme);
        locals.push_back({name, next_slot++});
        // L8: determine if this local can be stored unboxed in the frame
        auto kind = LocalKind::BOXED;  // default: boxed
        auto type_it = m_type_checker.getVariableTypes().find(vd);
        if (type_it != m_type_checker.getVariableTypes().end() && type_it->second) {
            if (isUnboxableType(type_it->second)) {
                kind = localKindForType(type_it->second);
            }
        }
        local_kinds[name] = kind;
        return;
    }
    if (auto* blk = dynamic_cast<const BlockStmt*>(stmt.get())) {
        for (auto& s : blk->statements) collectAsyncLocals(s, locals, local_kinds, next_slot);
    } else if (auto* ifs = dynamic_cast<const IfStmt*>(stmt.get())) {
        if (ifs->thenBranch) collectAsyncLocals(ifs->thenBranch, locals, local_kinds, next_slot);
        if (ifs->elseBranch) collectAsyncLocals(ifs->elseBranch, locals, local_kinds, next_slot);
    } else if (auto* wh = dynamic_cast<const WhileStmt*>(stmt.get())) {
        if (wh->body) collectAsyncLocals(wh->body, locals, local_kinds, next_slot);
    } else if (auto* fr = dynamic_cast<const ForStmt*>(stmt.get())) {
        if (fr->initializer) collectAsyncLocals(fr->initializer, locals, local_kinds, next_slot);
        if (fr->body) collectAsyncLocals(fr->body, locals, local_kinds, next_slot);
    } else if (auto* fi = dynamic_cast<const ForInStmt*>(stmt.get())) {
        if (fi->body) collectAsyncLocals(fi->body, locals, local_kinds, next_slot);
    } else if (auto* trys = dynamic_cast<const TryStmt*>(stmt.get())) {
        if (trys->tryBlock) collectAsyncLocals(trys->tryBlock, locals, local_kinds, next_slot);
        if (trys->catchBlock) collectAsyncLocals(trys->catchBlock, locals, local_kinds, next_slot);
        if (trys->finallyBlock) collectAsyncLocals(trys->finallyBlock, locals, local_kinds, next_slot);
    } else if (auto* uns = dynamic_cast<const UnsafeBlockStmt*>(stmt.get())) {
        for (auto& s : uns->block->statements) collectAsyncLocals(s, locals, local_kinds, next_slot);
    } else if (auto* prv = dynamic_cast<const PrivilegedBlockStmt*>(stmt.get())) {
        for (auto& s : prv->block->statements) collectAsyncLocals(s, locals, local_kinds, next_slot);
    }
    // ExpressionStmt, ReturnStmt, DropStmt, BreakStmt, ContinueStmt, ThrowStmt:
    // no VarDeclStmt children.
}

void LLVMBackend::collectAsyncLocalsExpr(const std::shared_ptr<Expr>& expr,
                                           std::vector<std::pair<std::string, int>>& locals,
                                           std::map<std::string, LocalKind>& local_kinds,
                                           int& next_slot) {
    // Currently no VarDeclStmt inside expressions; placeholder for future use.
    (void)expr; (void)locals; (void)local_kinds; (void)next_slot;
}

// LIB-4 Stage S: codegen for async functions.
// Splits into a thin wrapper (allocates frame, calls resume, wraps in NativeInstance)
// and a resume function (state machine with switch dispatch).
void LLVMBackend::codegenAsyncFuncDecl(const FuncStmt& stmt, const std::string& module_name) {
    const std::string func_name = mangle(module_name, stmt.name.lexeme);

    // Resolve semantic function type to get param types
    auto sem_sym = const_cast<SymbolTable&>(m_type_checker.getSymbolTable()).resolve(stmt.name.lexeme);
    auto sem_fn_type = (sem_sym && sem_sym->type && sem_sym->type->kind == TypeKind::FUNCTION)
        ? std::dynamic_pointer_cast<FunctionType>(sem_sym->type) : nullptr;

    // ---- Collect await states ----
    std::vector<const AwaitExpr*> await_states;
    if (stmt.body) {
        for (auto& s : *stmt.body) {
            collectAwaitStatesStmt(s, await_states);
        }
    }

    // ---- Collect local variables for frame slots ----
    std::vector<std::pair<std::string, int>> local_slots;
    std::map<std::string, LocalKind> local_kinds;
    int next_local_slot = 6 + (int)stmt.params.size();  // after header (fields 0-5) + params
    if (stmt.body) {
        for (auto& s : *stmt.body) {
            collectAsyncLocals(s, local_slots, local_kinds, next_local_slot);
        }
    }

    // ---- Build frame struct type ----
    // C7 INVARIANT: the first 6 fields below are the fixed async header and MUST
    // stay layout-identical to m_async_frame_header_ty (the canonical {i32,
    // objType, objType, ptr, ptr, ptr} built at backend init). Child-frame
    // accesses in cgAwait GEP through that header type, not this full frame
    // type, so the header offsets must not diverge. Param slots (fields 6+)
    // and local slots follow and may vary per function.
    auto* state_ty = llvm::Type::getInt32Ty(*ctx);
    auto* ptr_ty = llvm::PointerType::get(*ctx, 0);
    std::vector<llvm::Type*> frame_fields = {state_ty, objType, objType, ptr_ty, ptr_ty, ptr_ty};

    // Parameter slots
    std::vector<int> param_field_idx;
    for (size_t pi = 0; pi < stmt.params.size(); ++pi) {
        param_field_idx.push_back((int)frame_fields.size());
        frame_fields.push_back(objType);
    }

    // Local variable slots — L8: use unboxed LLVM type for primitives
    for (size_t li = 0; li < local_slots.size(); ++li) {
        auto kit = local_kinds.find(local_slots[li].first);
        auto kind = (kit != local_kinds.end()) ? kit->second : LocalKind::BOXED;
        frame_fields.push_back(llvmTypeForLocalKind(kind));
    }

    auto* frame_struct_ty = llvm::StructType::get(*ctx, frame_fields, false);
    auto* frame_ptr_ty = llvm::PointerType::get(*ctx, 0);

    // ---- Declare free() for the finalizer ----
    auto* free_fn = mod->getFunction("free");
    if (!free_fn) {
        free_fn = llvm::Function::Create(
            llvm::FunctionType::get(llvm::Type::getVoidTy(*ctx), {ptr_ty}, false),
            llvm::Function::ExternalLinkage, "free", mod.get());
    }

    // ---- Build wrapper function signature: (objType params...) -> objType ----
    std::vector<llvm::Type*> param_types(stmt.params.size(), objType);
    auto* wrapper_fn_type = llvm::FunctionType::get(objType, param_types, false);

    auto* wrapper_fn = mod->getFunction(func_name);
    if (!wrapper_fn) {
        wrapper_fn = llvm::Function::Create(wrapper_fn_type, llvm::Function::ExternalLinkage,
                                            func_name, mod.get());
    }

    // Attach DWARF debug info
    llvm::DIScope* saved_di_scope = m_di_scope;
    if (m_debug && m_di_builder && m_di_file) {
        std::string src_file = stmt.name.file ? *stmt.name.file : "unknown";
        auto diFile = getOrCreateDIFile(src_file);
        auto diFuncType = m_di_builder->createSubroutineType(
            m_di_builder->getOrCreateTypeArray({}));
        auto sp = m_di_builder->createFunction(
            diFile, stmt.name.lexeme, func_name, diFile,
            stmt.name.line, diFuncType, stmt.name.column,
            llvm::DINode::FlagZero, llvm::DISubprogram::SPFlagDefinition);
        wrapper_fn->setSubprogram(sp);
        m_di_scope = sp;
        builder->SetCurrentDebugLocation(
            llvm::DILocation::get(*ctx, stmt.name.line, stmt.name.column, sp));
    }

    // Name wrapper parameters
    size_t idx = 0;
    for (auto& arg : wrapper_fn->args()) {
        arg.setName(sanitize(stmt.params[idx].name.lexeme));
        idx++;
    }

    auto* entry = llvm::BasicBlock::Create(*ctx, "entry", wrapper_fn);
    builder->SetInsertPoint(entry);

    // ---- Wrapper body: alloc frame, init, store params, call resume, wrap, return ----
    // Declare malloc
    auto* malloc_fn = mod->getFunction("malloc");
    if (!malloc_fn) {
        malloc_fn = llvm::Function::Create(
            llvm::FunctionType::get(ptr_ty, {llvm::Type::getInt64Ty(*ctx)}, false),
            llvm::Function::ExternalLinkage, "malloc", mod.get());
    }
    auto* frame_size = llvm::ConstantExpr::getSizeOf(frame_struct_ty);
    auto* frame_ptr = builder->CreateCall(malloc_fn, {frame_size}, "future_frame");

    // Init header fields
    // state = 0
    auto* typed_frame0 = builder->CreateBitCast(frame_ptr, frame_ptr_ty);
    auto* state_ptr0 = builder->CreateStructGEP(frame_struct_ty, typed_frame0, 0, "state_ptr");
    builder->CreateStore(llvm::ConstantInt::get(state_ty, 0), state_ptr0);
    // result = nil
    auto* result_ptr0 = builder->CreateStructGEP(frame_struct_ty, typed_frame0, 1, "result_ptr");
    builder->CreateStore(makeNil(), result_ptr0);
    // awaited = nil
    auto* awaited_ptr0 = builder->CreateStructGEP(frame_struct_ty, typed_frame0, 2, "awaited_ptr");
    builder->CreateStore(makeNil(), awaited_ptr0);
    // waker_fn = null
    auto* waker_fn_ptr0 = builder->CreateStructGEP(frame_struct_ty, typed_frame0, 3, "waker_fn_ptr");
    builder->CreateStore(llvm::ConstantPointerNull::get(ptr_ty), waker_fn_ptr0);
    // waker_ctx = null
    auto* waker_ctx_ptr0 = builder->CreateStructGEP(frame_struct_ty, typed_frame0, 4, "waker_ctx_ptr");
    builder->CreateStore(llvm::ConstantPointerNull::get(ptr_ty), waker_ctx_ptr0);
    // loop = null
    auto* loop_ptr0 = builder->CreateStructGEP(frame_struct_ty, typed_frame0, 5, "loop_ptr");
    builder->CreateStore(llvm::ConstantPointerNull::get(ptr_ty), loop_ptr0);

    // Store params in frame fields
    idx = 0;
    for (auto& arg : wrapper_fn->args()) {
        auto* typed_f = builder->CreateBitCast(frame_ptr, frame_ptr_ty);
        auto* field_p = builder->CreateStructGEP(frame_struct_ty, typed_f,
                                                  param_field_idx[idx], "param_ptr");
        builder->CreateStore(&arg, field_p);
        idx++;
    }

    // Call the resume function
    codegenAsyncResumeFunc(stmt, module_name, wrapper_fn, frame_struct_ty,
                           param_field_idx, await_states, local_slots, local_kinds, sem_fn_type);

    // Restore builder to wrapper function's entry block
    builder->SetInsertPoint(entry);

    // Now call foo$resume(frame_ptr) from the wrapper
    std::string resume_name = func_name + "$resume";
    auto* resume_fn = mod->getFunction(resume_name);
    builder->CreateCall(resume_fn, {frame_ptr});

    // Wrap frame in NativeInstance with free() finalizer
    auto* name_str = builder->CreateGlobalString("Future");
    auto* future_obj = callRtByName("__ang_api_native_instance_new",
        {frame_ptr, free_fn, name_str});
    builder->CreateRet(future_obj);

    m_di_scope = saved_di_scope;
    if (m_debug) builder->SetCurrentDebugLocation(llvm::DebugLoc());
}

// LIB-4 Stage S: generate the resume function (state machine) for an async function.
void LLVMBackend::codegenAsyncResumeFunc(const FuncStmt& stmt, const std::string& module_name,
                                          llvm::Function* wrapper_fn,
                                          llvm::StructType* frame_struct_ty,
                                          const std::vector<int>& param_field_idx,
                                          const std::vector<const AwaitExpr*>& await_states,
                                          const std::vector<std::pair<std::string, int>>& local_slots,
                                          const std::map<std::string, LocalKind>& local_kinds,
                                          const std::shared_ptr<FunctionType>& sem_fn_type) {
    const std::string func_name = mangle(module_name, stmt.name.lexeme);
    std::string resume_name = func_name + "$resume";
    int num_states = (int)await_states.size();

    auto* state_ty = llvm::Type::getInt32Ty(*ctx);
    auto* ptr_ty = llvm::PointerType::get(*ctx, 0);

    // Resume function: void (i8* frame_ptr)
    auto* resume_fn_type = llvm::FunctionType::get(llvm::Type::getVoidTy(*ctx), {ptr_ty}, false);
    auto* resume_fn = llvm::Function::Create(resume_fn_type, llvm::Function::InternalLinkage,
                                              resume_name, mod.get());
    auto* frame_arg = resume_fn->arg_begin();
    frame_arg->setName("frame");

    auto* entry_bb = llvm::BasicBlock::Create(*ctx, "entry", resume_fn);
    builder->SetInsertPoint(entry_bb);

    // Save/restore codegen state
    auto saved_values = std::move(namedVals);
    auto saved_types = std::move(namedTypes);
    auto saved_kinds = std::move(namedKinds);
    namedVals.clear();
    namedTypes.clear();
    namedKinds.clear();

    auto saved_in_async = m_in_async_function;
    auto saved_async_frame = m_current_async_frame;
    auto saved_async_frame_type = m_current_async_frame_type;
    auto saved_async_state_ptr = m_current_async_state_ptr;
    auto saved_async_result_ptr = m_current_async_result_ptr;
    auto saved_async_waker_fn_ptr = m_current_async_waker_fn_ptr;
    auto saved_async_waker_ctx_ptr = m_current_async_waker_ctx_ptr;
    auto saved_async_resume_fn = m_current_async_resume_fn;
    auto saved_local_slots = std::move(m_async_local_slots);
    m_async_local_slots.clear();

    // Set up frame tracking
    m_in_async_function = true;
    m_current_async_frame = frame_arg;
    m_current_async_frame_type = frame_struct_ty;
    m_current_async_resume_fn = resume_fn;

    // Cast frame and compute GEPs for header fields
    auto* typed_frame = builder->CreateBitCast(frame_arg, ptr_ty);
    m_current_async_state_ptr = builder->CreateStructGEP(frame_struct_ty, typed_frame, 0, "state_p");
    m_current_async_result_ptr = builder->CreateStructGEP(frame_struct_ty, typed_frame, 1, "result_p");
    m_current_async_waker_fn_ptr = builder->CreateStructGEP(frame_struct_ty, typed_frame, 3, "wfn_p");
    m_current_async_waker_ctx_ptr = builder->CreateStructGEP(frame_struct_ty, typed_frame, 4, "wctx_p");

    // Register params as frame-based "locals" (so loadVar/storeVar use frame GEP)
    // Note: params stay BOXED in the frame because the ABI always passes them boxed
    for (size_t pi = 0; pi < stmt.params.size(); ++pi) {
        auto pname = sanitize(stmt.params[pi].name.lexeme);
        m_async_local_slots[pname] = param_field_idx[pi];
        namedKinds[pname] = LocalKind::BOXED;
        auto param_type = (sem_fn_type && pi < sem_fn_type->param_types.size())
            ? sem_fn_type->param_types[pi] : nullptr;
        if (param_type) namedTypes[pname] = param_type;
    }

    // Register local var slots — L8: use unboxed LocalKind when available
    for (auto& [name, slot] : local_slots) {
        m_async_local_slots[name] = slot;
        auto kit = local_kinds.find(name);
        namedKinds[name] = (kit != local_kinds.end()) ? kit->second : LocalKind::BOXED;
    }

    // Create basic blocks
    auto* dispatch_bb = llvm::BasicBlock::Create(*ctx, "dispatch", resume_fn);
    auto* suspend_bb = llvm::BasicBlock::Create(*ctx, "async_suspend", resume_fn);
    auto* done_bb = llvm::BasicBlock::Create(*ctx, "async_done", resume_fn);

    // State entry blocks: state_0 (initial body), plus resume_N for N > 0
    std::vector<llvm::BasicBlock*> state_blocks;
    state_blocks.push_back(llvm::BasicBlock::Create(*ctx, "state_0", resume_fn));
    for (int i = 1; i <= num_states; ++i) {
        state_blocks.push_back(llvm::BasicBlock::Create(*ctx, "resume_" + std::to_string(i), resume_fn));
    }

    // ---- Dispatch: switch on frame.state ----
    builder->CreateBr(dispatch_bb);
    builder->SetInsertPoint(dispatch_bb);
    auto* state_val = builder->CreateLoad(state_ty, m_current_async_state_ptr, "cur_state");
    auto* switch_inst = builder->CreateSwitch(state_val, done_bb, num_states + 1);
    // case 0 → state_0 (initial run)
    switch_inst->addCase(llvm::ConstantInt::get(state_ty, 0), state_blocks[0]);
    // case i → resume_i (resume after await i-1)
    for (int i = 1; i <= num_states; ++i) {
        switch_inst->addCase(llvm::ConstantInt::get(state_ty, i), state_blocks[i]);
    }
    m_async_dispatch_switch = switch_inst;

    // ---- State 0: initial run (before first await) ----
    builder->SetInsertPoint(state_blocks[0]);

    m_async_await_idx = 0;
    m_async_await_cont_bbs.clear();
    for (int i = 1; i <= num_states; ++i) {
        m_async_await_cont_bbs.push_back(state_blocks[i]);
    }
    m_async_suspend_bb = suspend_bb;
    m_async_loop_bb = dispatch_bb;
    m_async_state_ty = state_ty;

    // Reset per-function codegen state
    m_exc_chain_save = nullptr;
    m_inlined_main_ret_alloca = nullptr;
    m_inlined_main_cleanup_bb = nullptr;
    emitRtPushFrame(resume_fn, 256);

    // Generate body — cgAwait will insert the check-branch-suspend and
    // jump to the next state block on resolved, or to suspend_bb on pending.
    if (stmt.body) {
        for (const auto& s : *stmt.body) {
            if (builder->GetInsertBlock()->getTerminator()) break;
            cgStmt(s);
        }
    }

    // Fallthrough: body completed without return → done
    if (!builder->GetInsertBlock()->getTerminator()) {
        builder->CreateStore(llvm::ConstantInt::get(state_ty, -1), m_current_async_state_ptr);
        builder->CreateBr(done_bb);
    }

    // ---- Suspend block: just return (frame already wrapped by caller) ----
    builder->SetInsertPoint(suspend_bb);
    if (m_exc_chain_save) emitRtPopFrame();
    builder->CreateRetVoid();

    // ---- Done block: cascade waker if set, then return ----
    builder->SetInsertPoint(done_bb);
    if (m_exc_chain_save) emitRtPopFrame();
    // If waker_fn is set, call it to cascade wake the parent
    auto* wfn = builder->CreateLoad(ptr_ty, m_current_async_waker_fn_ptr, "wfn");
    auto* has_waker = builder->CreateIsNotNull(wfn);
    auto* wake_bb = llvm::BasicBlock::Create(*ctx, "wake_parent", resume_fn);
    auto* exit_bb = llvm::BasicBlock::Create(*ctx, "resume_exit", resume_fn);
    builder->CreateCondBr(has_waker, wake_bb, exit_bb);

    builder->SetInsertPoint(wake_bb);
    auto* wctx = builder->CreateLoad(ptr_ty, m_current_async_waker_ctx_ptr, "wctx");
    auto* waker_ty = llvm::FunctionType::get(llvm::Type::getVoidTy(*ctx), {ptr_ty}, false);
    builder->CreateCall(waker_ty, wfn, {wctx});
    builder->CreateBr(exit_bb);

    builder->SetInsertPoint(exit_bb);
    builder->CreateRetVoid();

    // ---- Restore codegen state ----
    // First, give unreferenced placeholder state blocks an unreachable terminator.
    // cgAwait may replace some state_blocks entries with retry blocks,
    // leaving the original placeholders empty (no terminator).
    for (auto* bb : state_blocks) {
        if (!bb->getTerminator()) {
            builder->SetInsertPoint(bb);
            builder->CreateUnreachable();
        }
    }

    namedVals = std::move(saved_values);
    namedTypes = std::move(saved_types);
    namedKinds = std::move(saved_kinds);
    m_in_async_function = saved_in_async;
    m_current_async_frame = saved_async_frame;
    m_current_async_frame_type = saved_async_frame_type;
    m_current_async_state_ptr = saved_async_state_ptr;
    m_current_async_result_ptr = saved_async_result_ptr;
    m_current_async_waker_fn_ptr = saved_async_waker_fn_ptr;
    m_current_async_waker_ctx_ptr = saved_async_waker_ctx_ptr;
    m_current_async_resume_fn = saved_async_resume_fn;
    m_async_local_slots = std::move(saved_local_slots);
}

void LLVMBackend::codegenClassDecl(const ClassStmt& stmt) {
    std::string class_name = stmt.name.lexeme;

    // Track the superclass name so super() calls can resolve the parent init.
    m_current_superclass = stmt.superclass ? stmt.superclass->name.lexeme : "";

    std::vector<std::shared_ptr<VarDeclStmt>> class_fields;
    for (const auto& member : stmt.members) {
        if (auto field = std::dynamic_pointer_cast<const FieldMember>(member)) {
            class_fields.push_back(field->declaration);
        }
    }

    std::shared_ptr<FuncStmt> init_method;
    std::string init_class_name = class_name;
    size_t init_param_count = 0;

    for (const auto& member : stmt.members) {
        if (auto method = std::dynamic_pointer_cast<const MethodMember>(member)) {
            if (method->declaration->name.lexeme == "init") {
                init_method = method->declaration;
                init_param_count = init_method->params.size();
                break;
            }
        }
    }

    if (!init_method) {
        auto sym = const_cast<SymbolTable&>(m_type_checker.getSymbolTable()).resolve(class_name);
        if (sym && sym->type->kind == TypeKind::CLASS) {
            auto ct = std::dynamic_pointer_cast<ClassType>(sym->type);
            const auto* init_prop = ct->findProperty("init");
            if (init_prop) {
                auto ft = std::dynamic_pointer_cast<FunctionType>(init_prop->type);
                if (ft) {
                    init_param_count = ft->param_types.size();
                }
                auto current = ct;
                while (current) {
                    if (current->methods.count("init")) {
                        init_class_name = current->name;
                        break;
                    }
                    current = current->superclass;
                }
            }
        }
    }

    for (const auto& member : stmt.members) {
        if (auto method = std::dynamic_pointer_cast<const MethodMember>(member)) {
            const auto& method_stmt = method->declaration;
            std::string method_name = mangleMethod(class_name, method_stmt->name.lexeme);
            // Store both unqualified and class-qualified keys
            methodLookup[method_stmt->name.lexeme] = method_name;
            methodLookup[class_name + "." + method_stmt->name.lexeme] = method_name;

            std::vector<llvm::Type*> param_types(method_stmt->params.size() + 1, objType);
            auto* fn_type = llvm::FunctionType::get(objType, param_types, false);
            auto* fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage,
                                               method_name, mod.get());

            auto* entry = llvm::BasicBlock::Create(*ctx, "entry", fn);
            builder->SetInsertPoint(entry);

            auto saved_values = std::move(namedVals);
            auto saved_types = std::move(namedTypes);
            auto saved_kinds = std::move(namedKinds);
            namedVals.clear();
            namedTypes.clear();
            namedKinds.clear();

            auto* this_alloca = allocLocal(fn, "this");
            builder->CreateStore(&*fn->arg_begin(), this_alloca);
            namedVals["this"] = this_alloca;
            namedKinds["this"] = LocalKind::BOXED;
            auto sym = const_cast<SymbolTable&>(m_type_checker.getSymbolTable()).resolve(class_name);
            if (sym && sym->type->kind == TypeKind::CLASS) {
                namedTypes["this"] = std::make_shared<InstanceType>(std::dynamic_pointer_cast<ClassType>(sym->type));
            }

            size_t pi = 0;
            for (auto it = fn->arg_begin() + 1; it != fn->arg_end() && pi < method_stmt->params.size(); ++it, ++pi) {
                std::string pname = sanitize(method_stmt->params[pi].name.lexeme);
                auto* alloca = allocLocal(fn, pname);
                builder->CreateStore(&*it, alloca);
                namedVals[pname] = alloca;
                namedKinds[pname] = LocalKind::BOXED;
            }

            emitRtPushFrame(fn, 256);

            if (method_stmt->body) {
                for (const auto& s : *method_stmt->body) {
                    if (builder->GetInsertBlock()->getTerminator()) break;
                    cgStmt(s);
                }
            }

            if (!builder->GetInsertBlock()->getTerminator()) {
                if (m_exc_chain_save) emitRtPopFrame();
                builder->CreateRet(makeNil());
            }

            namedVals = std::move(saved_values);
            namedTypes = std::move(saved_types);
            namedKinds = std::move(saved_kinds);

            // L12: clear the debug location after each method so a stale line
            // loc doesn't bleed into the next method's prologue (matches the
            // post-function clear at line ~282/599). Only meaningful with -g.
            if (m_debug) builder->SetCurrentDebugLocation(llvm::DebugLoc());
        }
    }

    // TS-1: emit per-(class,interface) vtables. For each trait/contract this
    // class adopts, build a ConstantArray of the class's implementing method
    // function pointers (one per interface method in declaration order) and a
    // global for it. Slot order = the interface's method-map order. Default-
    // method bodies (Phase D) would fill slots the class doesn't override.
    {
        auto sym = const_cast<SymbolTable&>(m_type_checker.getSymbolTable()).resolve(class_name);
        if (sym && sym->type && sym->type->kind == TypeKind::CLASS) {
            auto cls = std::dynamic_pointer_cast<ClassType>(sym->type);
            auto* ptr_ty = llvm::PointerType::get(*ctx, 0);
            // TS-1/Phase D: emit a trait's default method body as a function
            // (memoized — once per trait-method, shared by all non-overriding
            // classes). Signature matches a regular method: (obj this, ...args) -> obj.
            auto get_or_emit_default = [&](const std::string& trait_name,
                                           const std::string& mname,
                                           const std::shared_ptr<const FuncStmt>& body) -> std::string {
                std::string fn_name = "__ang_default_" + trait_name + "_" + mname;
                if (mod->getFunction(fn_name)) return fn_name;  // already emitted
                std::vector<llvm::Type*> param_types(body->params.size() + 1, objType);
                auto* fn_type = llvm::FunctionType::get(objType, param_types, false);
                auto* fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage,
                                                   fn_name, mod.get());
                auto* entry = llvm::BasicBlock::Create(*ctx, "entry", fn);
                builder->SetInsertPoint(entry);
                auto saved_values = std::move(namedVals);
                auto saved_types = std::move(namedTypes);
                auto saved_kinds = std::move(namedKinds);
                namedVals.clear(); namedTypes.clear(); namedKinds.clear();
                auto* this_alloca = allocLocal(fn, "this");
                builder->CreateStore(&*fn->arg_begin(), this_alloca);
                namedVals["this"] = this_alloca;
                namedKinds["this"] = LocalKind::BOXED;
                size_t pi = 0;
                for (auto it = fn->arg_begin() + 1; it != fn->arg_end() && pi < body->params.size(); ++it, ++pi) {
                    std::string pname = sanitize(body->params[pi].name.lexeme);
                    auto* alloca = allocLocal(fn, pname);
                    builder->CreateStore(&*it, alloca);
                    namedVals[pname] = alloca;
                    namedKinds[pname] = LocalKind::BOXED;
                }
                emitRtPushFrame(fn, 256);
                if (body->body) {
                    for (const auto& s : *body->body) {
                        if (builder->GetInsertBlock()->getTerminator()) break;
                        cgStmt(s);
                    }
                }
                if (!builder->GetInsertBlock()->getTerminator()) {
                    if (m_exc_chain_save) emitRtPopFrame();
                    builder->CreateRet(makeNil());
                }
                namedVals = std::move(saved_values);
                namedTypes = std::move(saved_types);
                namedKinds = std::move(saved_kinds);
                return fn_name;
            };
            auto emit_vtable = [&](const std::shared_ptr<TraitType>& trait) {
                const std::string& iface_name = trait->name;
                std::vector<llvm::Constant*> fns;
                for (const auto& [mname, sig] : trait->methods) {
                    // Resolve the implementing method by walking the class chain
                    // (matches static dispatch): Class.method then bare method.
                    std::string resolved;
                    for (auto cur = cls; cur && resolved.empty(); cur = cur->superclass) {
                        auto qit = methodLookup.find(cur->name + "." + mname);
                        if (qit != methodLookup.end()) resolved = qit->second;
                    }
                    if (resolved.empty()) {
                        auto mit = methodLookup.find(mname);
                        if (mit != methodLookup.end()) resolved = mit->second;
                    }
                    // TS-1/Phase D: if the class doesn't override, fall back to the
                    // trait's default body (if any).
                    if (resolved.empty()) {
                        auto dit = trait->default_bodies.find(mname);
                        if (dit != trait->default_bodies.end() && dit->second) {
                            resolved = get_or_emit_default(trait->name, mname, dit->second);
                        }
                    }
                    if (auto* f = mod->getFunction(resolved)) {
                        fns.push_back(llvm::ConstantExpr::getBitCast(f, ptr_ty));
                    } else {
                        fns.push_back(llvm::ConstantPointerNull::get(ptr_ty));
                    }
                }
                std::string vkey = class_name + "->" + iface_name;
                auto* arr_ty = llvm::ArrayType::get(ptr_ty, fns.size());
                auto* arr = llvm::ConstantArray::get(arr_ty, fns);
                auto* gv = new llvm::GlobalVariable(*mod, arr_ty, true,
                    llvm::GlobalValue::InternalLinkage, arr, "__ang_vtable_" + vkey);
                traitVtables[vkey] = gv;
                for (const auto& [mname, sig] : trait->methods) {
                    // Record slot index = the method's position in the iteration order.
                    // (std::map is ordered, so iteration is deterministic; recompute idx.)
                    int idx = 0;
                    for (const auto& [n, s] : trait->methods) {
                        if (n == mname) break;
                        idx++;
                    }
                    traitMethodSlots[iface_name + "." + mname] = idx;
                }
            };
            for (const auto& trait : cls->adopted_traits) {
                emit_vtable(trait);
            }
            // Contracts have no default bodies (parser rejects method bodies in
            // contracts), so emit a plain vtable from the class's overrides only.
            for (const auto& contract : cls->signed_contracts) {
                const std::string& iface_name = contract->name;
                std::vector<llvm::Constant*> fns;
                std::vector<std::string> names;
                for (const auto& [n, info] : contract->methods) names.push_back(n);
                for (const auto& mname : names) {
                    std::string resolved;
                    for (auto cur = cls; cur && resolved.empty(); cur = cur->superclass) {
                        auto qit = methodLookup.find(cur->name + "." + mname);
                        if (qit != methodLookup.end()) resolved = qit->second;
                    }
                    if (resolved.empty()) {
                        auto mit = methodLookup.find(mname);
                        if (mit != methodLookup.end()) resolved = mit->second;
                    }
                    if (auto* f = mod->getFunction(resolved)) {
                        fns.push_back(llvm::ConstantExpr::getBitCast(f, ptr_ty));
                    } else {
                        fns.push_back(llvm::ConstantPointerNull::get(ptr_ty));
                    }
                }
                std::string vkey = class_name + "->" + iface_name;
                auto* arr_ty = llvm::ArrayType::get(ptr_ty, fns.size());
                auto* arr = llvm::ConstantArray::get(arr_ty, fns);
                auto* gv = new llvm::GlobalVariable(*mod, arr_ty, true,
                    llvm::GlobalValue::InternalLinkage, arr, "__ang_vtable_" + vkey);
                traitVtables[vkey] = gv;
                for (size_t i = 0; i < names.size(); ++i) {
                    traitMethodSlots[iface_name + "." + names[i]] = static_cast<int>(i);
                }
            }
        }
    }

    size_t ctor_param_count = (init_method || init_param_count > 0)
        ? init_param_count
        : class_fields.size();

    std::string ctor_name = "Angara_" + class_name + "_new";
    {
        std::vector<llvm::Type*> param_types(ctor_param_count, objType);
        auto* fn_type = llvm::FunctionType::get(objType, param_types, false);
        auto* fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage,
                                           ctor_name, mod.get());

        auto* entry = llvm::BasicBlock::Create(*ctx, "entry", fn);
        builder->SetInsertPoint(entry);

        auto saved_values = std::move(namedVals);
        auto saved_kinds = std::move(namedKinds);
        namedVals.clear();
        namedKinds.clear();

        llvm::Value* obj = callRtByName("__ang_record_new", {});

        if (init_method || init_param_count > 0) {
            std::string init_mangled = mangleMethod(init_class_name, "init");
            llvm::Function* init_fn = this->mod->getFunction(init_mangled);
            if (init_fn) {
                std::vector<llvm::Value*> args;
                args.push_back(obj);
                for (auto& arg : fn->args()) args.push_back(&arg);
                auto* ft = init_fn->getFunctionType();
                while (args.size() < ft->getNumParams()) args.push_back(makeNil());
                builder->CreateCall(init_fn, args);
            }
        } else {
            size_t i = 0;
            for (auto& arg : fn->args()) {
                if (i < class_fields.size()) {
                    auto* key_ptr = builder->CreateGlobalString(class_fields[i]->name.lexeme);
                    callRtByName("__ang_record_set", {obj, key_ptr, &arg});
                }
                i++;
            }
        }

        builder->CreateRet(obj);
        namedVals = std::move(saved_values);
        namedKinds = std::move(saved_kinds);
    }
    constructorLookup[class_name] = ctor_name;
    m_current_superclass.clear();
}

void LLVMBackend::codegenDataDecl(const DataStmt& stmt) {
    if (stmt.is_foreign) {
        codegenForeignDataDecl(stmt);
        return;
    }

    std::string data_name = stmt.name.lexeme;

    std::vector<llvm::Type*> param_types(stmt.fields.size(), objType);
    auto* fn_type = llvm::FunctionType::get(objType, param_types, false);
    auto* fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage,
                                       "Angara_data_new_" + data_name, mod.get());

    auto* entry = llvm::BasicBlock::Create(*ctx, "entry", fn);
    builder->SetInsertPoint(entry);

    auto saved_values = std::move(namedVals);
    auto saved_kinds = std::move(namedKinds);
    namedVals.clear();
    namedKinds.clear();

    llvm::Value* obj = callRtByName("__ang_record_new", {});

    size_t i = 0;
    for (auto& arg : fn->args()) {
        auto* key_ptr = builder->CreateGlobalString(stmt.fields[i]->name.lexeme);
        callRtByName("__ang_record_set", {obj, key_ptr, &arg});
        i++;
    }

    builder->CreateRet(obj);
    namedVals = std::move(saved_values);
    namedKinds = std::move(saved_kinds);
    constructorLookup[data_name] = "Angara_data_new_" + data_name;
}

void LLVMBackend::codegenForeignDataDecl(const DataStmt& stmt) {
    std::string data_name = stmt.name.lexeme;

    // Resolve the semantic DataType from the type checker
    auto sym = const_cast<SymbolTable&>(m_type_checker.getSymbolTable()).resolve(data_name);
    auto data_type = std::dynamic_pointer_cast<DataType>(sym->type);

    // Opaque type: "foreign data FILE;" — no struct layout needed
    if (stmt.is_opaque) {
        data_type->is_opaque = true;
        m_foreign_data_types[data_name] = data_type;

        // Constructor: returns a nil placeholder; actual pointer set by foreign function return
        std::string ctor_name = "Angara_foreign_new_" + data_name;
        auto* fn_type = llvm::FunctionType::get(objType, false);
        auto* fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage,
                                           ctor_name, mod.get());
        auto* entry = llvm::BasicBlock::Create(*ctx, "entry", fn);
        builder->SetInsertPoint(entry);
        builder->CreateRet(makeNil());
        constructorLookup[data_name] = ctor_name;
        return;
    }

    // Structured foreign data: create a named LLVM struct with C-compatible layout
    std::vector<llvm::Type*> field_types;
    std::vector<std::string> field_names;
    uint64_t max_field_size = 0;
    llvm::Type* largest_field_type = nullptr;

    for (const auto& field_decl : stmt.fields) {
        auto field_type = data_type->fields[field_decl->name.lexeme].type;
        auto* llvm_ft = resolveCFieldType(field_type);
        field_names.push_back(field_decl->name.lexeme);

        if (data_type->is_union) {
            // For unions, find the largest field
            uint64_t fsize = mod->getDataLayout().getTypeAllocSize(llvm_ft).getFixedValue();
            if (fsize > max_field_size) {
                max_field_size = fsize;
                largest_field_type = llvm_ft;
            }
        } else {
            field_types.push_back(llvm_ft);
        }
    }

    llvm::StructType* struct_type;
    if (data_type->is_union) {
        // Union: struct with one field (the largest) — all fields overlap at offset 0
        if (!largest_field_type) largest_field_type = llvm::Type::getInt64Ty(*ctx);
        struct_type = llvm::StructType::create(*ctx, {largest_field_type}, "AngaraFU_" + data_name);
    } else {
        struct_type = llvm::StructType::create(*ctx, field_types, "AngaraFD_" + data_name);
    }

    m_foreign_struct_types[data_name] = struct_type;
    m_foreign_data_types[data_name] = data_type;
    m_foreign_field_order[data_name] = std::move(field_names);

    // Generate a finalizer that calls free() on the C struct data
    std::string fin_name = "Angara_foreign_free_" + data_name;
    auto* fin_type = llvm::FunctionType::get(llvm::Type::getVoidTy(*ctx),
        {llvm::PointerType::get(*ctx, 0)}, false);
    auto* fin_fn = llvm::Function::Create(fin_type, llvm::Function::ExternalLinkage,
                                           fin_name, mod.get());
    auto* fin_entry = llvm::BasicBlock::Create(*ctx, "entry", fin_fn);
    builder->SetInsertPoint(fin_entry);
    auto* free_fn = mod->getFunction("free");
    if (!free_fn) {
        auto* free_type = llvm::FunctionType::get(llvm::Type::getVoidTy(*ctx),
            {llvm::PointerType::get(*ctx, 0)}, false);
        free_fn = llvm::Function::Create(free_type, llvm::Function::ExternalLinkage,
                                          "free", mod.get());
    }
    builder->CreateCall(free_fn, {fin_fn->arg_begin()});
    builder->CreateRetVoid();

    // Constructor: malloc + memset(0) + wrap in NativeInstance
    std::string ctor_name = "Angara_foreign_new_" + data_name;
    auto* fn_type = llvm::FunctionType::get(objType, false);
    auto* fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage,
                                       ctor_name, mod.get());
    auto* entry = llvm::BasicBlock::Create(*ctx, "entry", fn);
    builder->SetInsertPoint(entry);

    // Allocate the C struct using libc malloc
    uint64_t struct_size = mod->getDataLayout().getTypeAllocSize(struct_type).getFixedValue();
    auto* malloc_fn = mod->getFunction("malloc");
    if (!malloc_fn) {
        auto* malloc_type = llvm::FunctionType::get(llvm::PointerType::get(*ctx, 0),
                                                     {llvm::Type::getInt64Ty(*ctx)}, false);
        malloc_fn = llvm::Function::Create(malloc_type, llvm::Function::ExternalLinkage,
                                            "malloc", mod.get());
    }
    auto* raw_mem = builder->CreateCall(malloc_fn,
        {llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), struct_size)});

    // Zero-fill with memset
    auto* memset_fn = mod->getFunction("memset");
    if (!memset_fn) {
        auto* memset_type = llvm::FunctionType::get(llvm::PointerType::get(*ctx, 0),
            {llvm::PointerType::get(*ctx, 0), llvm::Type::getInt32Ty(*ctx),
             llvm::Type::getInt64Ty(*ctx)}, false);
        memset_fn = llvm::Function::Create(memset_type, llvm::Function::ExternalLinkage,
                                            "memset", mod.get());
    }
    builder->CreateCall(memset_fn, {
        raw_mem,
        llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), 0),
        llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), struct_size)
    });

    // Wrap in NativeInstance via __ang_api_native_instance_new
    auto* name_str = builder->CreateGlobalString(data_name);
    auto* finalizer = fin_fn;
    auto* native_obj = callRtByName("__ang_api_native_instance_new",
                                     {raw_mem, finalizer, name_str});

    builder->CreateRet(native_obj);
    constructorLookup[data_name] = ctor_name;
}

void LLVMBackend::codegenForeignFuncDecl(const FuncStmt& stmt) {
    // Resolve the semantic FunctionType from the type checker
    auto sym = const_cast<SymbolTable&>(m_type_checker.getSymbolTable()).resolve(stmt.name.lexeme);
    auto func_type = std::dynamic_pointer_cast<FunctionType>(sym->type);

    bool returnsVoid = !func_type || func_type->return_type->kind == TypeKind::NIL
                        || func_type->return_type->kind == TypeKind::VOID;
    auto ret_type = func_type ? func_type->return_type : nullptr;
    bool isVariadic = func_type && func_type->is_variadic;

    // Build C function signature using resolved semantic types
    // Foreign data returns are pointers by default (^Type for by-value)
    llvm::Type* cRetType;
    if (returnsVoid) {
        cRetType = llvm::Type::getVoidTy(*ctx);
    } else if (ret_type->kind == TypeKind::DATA) {
        cRetType = llvm::PointerType::get(*ctx, 0); // pointer return
    } else {
        cRetType = resolveCFieldType(ret_type);
    }

    std::vector<llvm::Type*> cParamTypes;
    for (size_t i = 0; i < func_type->param_types.size(); i++) {
        auto& ptype = func_type->param_types[i];
        // Check for byval pointer (^Type) — pass struct by value
        if (ptype->kind == TypeKind::POINTER) {
            auto ptr_type = std::dynamic_pointer_cast<PointerType>(ptype);
            if (ptr_type && ptr_type->byval) {
                cParamTypes.push_back(resolveCFieldType(ptype));
                continue;
            }
        }
        // Foreign data (non-opaque): pass as pointer to the struct
        if (ptype->kind == TypeKind::DATA) {
            auto dt = std::dynamic_pointer_cast<DataType>(ptype);
            if (dt && dt->is_foreign && !dt->is_opaque) {
                auto it = m_foreign_struct_types.find(dt->name);
                if (it != m_foreign_struct_types.end()) {
                    cParamTypes.push_back(llvm::PointerType::get(*ctx, 0));
                    continue;
                }
            }
            // Opaque or unresolved: void pointer
            cParamTypes.push_back(llvm::PointerType::get(*ctx, 0));
        } else {
            cParamTypes.push_back(resolveCFieldType(ptype));
        }
    }

    // Declare the raw C function
    std::string cFuncName = stmt.name.lexeme;
    auto* cFnType = llvm::FunctionType::get(cRetType, cParamTypes, isVariadic);
    auto* cFunc = mod->getFunction(cFuncName);
    if (!cFunc) {
        cFunc = llvm::Function::Create(cFnType, llvm::Function::ExternalLinkage, cFuncName, mod.get());
    }

    // For variadic functions, skip wrapper generation — calls go directly to the C function
    if (isVariadic) {
        m_variadic_foreign_funcs[cFuncName] = func_type;
        return;
    }

    // Identify which *void params are userdata slots for FUNCTION callbacks.
    // Convention: the next *void param after a FUNCTION param is its userdata slot.
    // These are stripped from the Angara wrapper signature — the user doesn't see them.
    std::set<size_t> userdata_slots;
    for (size_t i = 0; i < func_type->param_types.size(); i++) {
        if (func_type->param_types[i]->kind == TypeKind::FUNCTION) {
            for (size_t j = i + 1; j < func_type->param_types.size(); j++) {
                if (func_type->param_types[j]->kind == TypeKind::POINTER) {
                    userdata_slots.insert(j);
                    break;
                }
            }
        }
    }

    // Create the Angara wrapper function — exclude userdata slots from the signature
    std::string wrapperName = mangle(moduleName, stmt.name.lexeme);
    size_t wrapper_param_count = func_type->param_types.size() - userdata_slots.size();
    std::vector<llvm::Type*> wrapperParamTypes(wrapper_param_count, objType);
    auto* wrapperFnType = llvm::FunctionType::get(objType, wrapperParamTypes, false);
    auto* wrapperFn = llvm::Function::Create(wrapperFnType, llvm::Function::ExternalLinkage,
                                               wrapperName, mod.get());

    auto* entry = llvm::BasicBlock::Create(*ctx, "entry", wrapperFn);
    builder->SetInsertPoint(entry);

    auto saved_values = std::move(namedVals);
    auto saved_kinds = std::move(namedKinds);
    namedVals.clear();
    namedKinds.clear();

    // Build a mapping: wrapper_arg_index -> c_param_index
    std::vector<size_t> wrapper_to_c;  // wrapper arg index -> C param index
    size_t widx = 0;
    for (size_t i = 0; i < func_type->param_types.size(); i++) {
        if (userdata_slots.count(i)) continue;
        wrapper_to_c.push_back(i);
        widx++;
    }

    // Allocate locals for all non-userdata wrapper params
    for (size_t wi = 0; wi < wrapper_to_c.size(); wi++) {
        size_t ci = wrapper_to_c[wi];
        std::string pname = sanitize(stmt.params[ci].name.lexeme);
        auto* alloca = allocLocal(wrapperFn, pname);
        auto* arg_val = wrapperFn->arg_begin() + wi;
        builder->CreateStore(arg_val, alloca);
        namedVals[pname] = alloca;
    }

    // Marshal all C params. For userdata slots, auto-fill with callback context.
    std::vector<llvm::Value*> cArgs(func_type->param_types.size(), nullptr);
    std::vector<bool> filled(func_type->param_types.size(), false);

    // M25: trampoline callback contexts allocated during marshalling. Each
    // foreign-func callback gets a heap-allocated context holding the closure;
    // the C call invokes the callback synchronously (the trampoline path is for
    // `foreign func` only — stored/async callbacks go through native modules
    // that manage their own lifetimes). So the context is safe to free once the
    // C call below returns. We accumulate them here and free after the call.
    std::vector<llvm::Value*> callback_contexts;

    // First pass: marshal wrapper params (non-userdata)
    for (size_t wi = 0; wi < wrapper_to_c.size(); wi++) {
        size_t ci = wrapper_to_c[wi];
        auto& ptype = func_type->param_types[ci];
        m_pending_callback_context = nullptr;
        auto* arg_val = wrapperFn->arg_begin() + wi;
        cArgs[ci] = marshalAngaraToC(arg_val, ptype);
        filled[ci] = true;

        if (m_pending_callback_context) {
            // This was a FUNCTION param — fill its paired userdata slot with context
            bool found = false;
            for (size_t j = ci + 1; j < func_type->param_types.size(); j++) {
                if (userdata_slots.count(j) && !filled[j]) {
                    cArgs[j] = m_pending_callback_context;
                    filled[j] = true;
                    found = true;
                    break;
                }
            }
            if (!found) {
                m_errorHandler.warning(stmt.name,
                    "Callback parameter '" + stmt.params[ci].name.lexeme
                    + "' has no matching *void userdata parameter — "
                    + "the callback cannot be invoked from C");
            }
            // M25: the context is reachable only during the C call (synchronous
            // trampoline path). Free it after the call returns whether or not a
            // userdata slot was found — in the no-slot case C never sees it, so
            // it's dead immediately; in the slot case it's dead once the call ends.
            callback_contexts.push_back(m_pending_callback_context);
            m_pending_callback_context = nullptr;
        }
    }

    // Fill any remaining userdata slots with null (shouldn't happen if paired correctly)
    for (size_t i = 0; i < func_type->param_types.size(); i++) {
        if (!filled[i]) {
            cArgs[i] = llvm::ConstantPointerNull::get(llvm::PointerType::get(*ctx, 0));
        }
    }

    llvm::CallInst* cResult = builder->CreateCall(cFunc, cArgs);

    // M25: the C call has returned, so every trampoline callback context it was
    // handed is no longer reachable — free them now. (The trampoline path serves
    // synchronous `foreign func` callbacks only; async/stored callbacks go
    // through native modules that own their lifetimes.) Mirrors the get-or-declare
    // pattern for `free` used elsewhere in the backend.
    if (!callback_contexts.empty()) {
        auto* free_fn = mod->getFunction("free");
        if (!free_fn) {
            auto* free_type = llvm::FunctionType::get(
                llvm::Type::getVoidTy(*ctx),
                {llvm::PointerType::get(*ctx, 0)}, false);
            free_fn = llvm::Function::Create(free_type, llvm::Function::ExternalLinkage,
                                             "free", mod.get());
        }
        for (auto* ctx_ptr : callback_contexts) {
            builder->CreateCall(free_fn, {ctx_ptr});
        }
    }

    // Repack C result -> AngaraObject
    if (returnsVoid) {
        builder->CreateRet(makeNil());
    } else {
        builder->CreateRet(marshalCToAngara(cResult, ret_type));
    }

    namedVals = std::move(saved_values);
    namedKinds = std::move(saved_kinds);
}
void LLVMBackend::codegenEnumDecl(const EnumStmt& stmt) {
    for (size_t i = 0; i < stmt.variants.size(); i++) {
        const auto& variant = stmt.variants[i];
        std::string name = "Angara_enum_" + stmt.name.lexeme + "_" + variant->name.lexeme;

        if (variant->params.empty()) {
            // Simple variant: global constant with TAG_I64 and variant index
            auto* init_const = llvm::ConstantStruct::get(objType, {
                llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), TAG_I64),
                llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), i)
            });
            auto* global = new llvm::GlobalVariable(
                *mod, objType, true,
                llvm::GlobalValue::PrivateLinkage, init_const, name);
            globals["g_" + variant->name.lexeme] = global;
            // Also register qualified name for unambiguous lookup
            globals["g_" + stmt.name.lexeme + "." + variant->name.lexeme] = global;
        } else {
            // Variant with associated data: constructor function
            size_t param_count = variant->params.size();
            std::string ctor_name = "__ang_" + stmt.name.lexeme + "_" + variant->name.lexeme;

            std::vector<llvm::Type*> param_types(param_count, objType);
            auto* fn_type = llvm::FunctionType::get(objType, param_types, false);
            auto* fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage,
                                               ctor_name, mod.get());

            auto* entry = llvm::BasicBlock::Create(*ctx, "entry", fn);
            builder->SetInsertPoint(entry);

            auto saved_values = std::move(namedVals);
            auto saved_kinds = std::move(namedKinds);
            namedVals.clear();
            namedKinds.clear();

            // Create a record to hold the variant data
            llvm::Value* record = callRtByName("__ang_record_new", {});

            // Store the variant index as __tag
            {
                auto* tag_val = makeI64(llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), i));
                auto* tag_key = builder->CreateGlobalString("__tag");
                callRtByName("__ang_record_set", {record, tag_key, tag_val});
            }

            // Store each parameter
            size_t pidx = 0;
            for (auto& arg : fn->args()) {
                std::string field_name = "_" + std::to_string(pidx);
                auto* key_ptr = builder->CreateGlobalString(field_name);
                callRtByName("__ang_record_set", {record, key_ptr, &arg});
                pidx++;
            }

            builder->CreateRet(record);
            namedVals = std::move(saved_values);
            namedKinds = std::move(saved_kinds);

            constructorLookup[stmt.name.lexeme + "." + variant->name.lexeme] = ctor_name;
        }

        // Store declaration-order index for match codegen
        enumVariantIndex[stmt.name.lexeme + "." + variant->name.lexeme] = static_cast<int>(i);
    }
}

void LLVMBackend::codegenMainFunction(const std::vector<std::shared_ptr<Stmt>>& statements,
                                        const std::string& module_name,
                                        const std::vector<std::string>& all_module_names,
                                        const std::vector<std::string>& native_module_names) {
    std::string entry_name = m_freestanding ? "_start" : "main";
    auto* main_type = llvm::FunctionType::get(
        m_freestanding ? llvm::Type::getVoidTy(*ctx) : llvm::Type::getInt32Ty(*ctx), false);
    auto* main_fn = llvm::Function::Create(main_type, llvm::Function::ExternalLinkage,
                                            entry_name, mod.get());

    // Attach DWARF debug info to main in debug mode
    if (m_debug && m_di_builder && m_di_file) {
        auto diFuncType = m_di_builder->createSubroutineType(
            m_di_builder->getOrCreateTypeArray({}));
        auto sp = m_di_builder->createFunction(
            m_di_file, entry_name, entry_name, m_di_file,
            1, diFuncType, 0,
            llvm::DINode::FlagZero, llvm::DISubprogram::SPFlagDefinition);
        main_fn->setSubprogram(sp);
        m_di_scope = sp;
        builder->SetCurrentDebugLocation(
            llvm::DILocation::get(*ctx, 1, 0, sp));
    }

    auto* entry = llvm::BasicBlock::Create(*ctx, "entry", main_fn);
    builder->SetInsertPoint(entry);

    namedVals.clear();
    namedTypes.clear();
    namedKinds.clear();

    // Reset per-function codegen state before emitting main/_start. The last
    // user function leaves m_exc_chain_save pointing at its alloca; if we don't
    // clear it, the main body (which in freestanding mode never calls
    // emitRtPushFrame to install a fresh value) would reuse that stale alloca
    // in emitRtPopFrame → a module-verify "instruction in another function"
    // failure and no object emitted. Mirrors the reset in cgFunction.
    m_exc_chain_save = nullptr;
    m_inlined_main_ret_alloca = nullptr;
    m_inlined_main_cleanup_bb = nullptr;

    // RT: register main thread and push root frame
    llvm::Value* gc_thread_state = nullptr;
    if (!m_freestanding) {
        gc_thread_state = emitRtThreadSetup();
        emitRtPushFrame(main_fn, 256);
    }

    if (!m_freestanding) {
        auto* vtable = rt->getAPIVtable();
        if (vtable) {
            auto* i32_ty = llvm::Type::getInt32Ty(*ctx);
            auto* ptr_ty = llvm::PointerType::get(*ctx, 0);
            auto* init_fn_type = llvm::FunctionType::get(ptr_ty,
                {llvm::PointerType::get(i32_ty, 0), ptr_ty}, false);

            // Helper: emit an `Angara_<mod>_Init(out_count, vtable)` call.
            // Returns the module name so callers can record it as initialized.
            auto emit_native_init = [&](const std::string& mod_name) {
                std::string init_name = "Angara_" + mod_name + "_Init";
                auto* init_fn = mod->getFunction(init_name);
                if (!init_fn) {
                    init_fn = llvm::Function::Create(init_fn_type, llvm::Function::ExternalLinkage,
                                                      init_name, mod.get());
                }
                auto* def_count_alloca = builder->CreateAlloca(i32_ty);
                builder->CreateStore(llvm::ConstantInt::get(i32_ty, 0), def_count_alloca);
                builder->CreateCall(init_fn, {def_count_alloca, vtable});
            };

            // Initialize native modules attached in THIS (entry) file.
            std::set<std::string> inited_native;
            for (const auto& stmt : statements) {
                auto attach = std::dynamic_pointer_cast<const AttachStmt>(stmt);
                if (!attach) continue;
                auto res_it = m_type_checker.getModuleResolutions().find(attach.get());
                if (res_it == m_type_checker.getModuleResolutions().end()) continue;
                auto& mod_type = res_it->second;
                if (!mod_type || !mod_type->is_native) continue;
                emit_native_init(mod_type->name);
                inited_native.insert(mod_type->name);
            }

            // BUG 4 fix: also initialize native modules attached only in OTHER
            // (imported) translation units. Without this, a native call executed
            // from within an imported function segfaults — the module was never
            // initialized, since its Init call was only ever emitted here, in the
            // entry file's main(), and the entry file didn't attach that module.
            for (const auto& mod_name : native_module_names) {
                if (inited_native.count(mod_name)) continue;
                emit_native_init(mod_name);
                inited_native.insert(mod_name);
            }
        }
    }


    // Initialize all interned string literals before any user code runs.
    // The init function is created eagerly in generate()/generateIR() and
    // accumulates init calls as makeStr discovers literals during codegen.
    // Since the function body is finalized at module emission time (after all
    // codegen), this call always executes every init, even for literals first
    // referenced in functions compiled after this call site.
    builder->CreateCall(m_strlit_init_fn);

    // Call each imported .an module's string-literal init function.
    // Each module has its own __ang_strlit_init_<moduleName> with
    // ExternalLinkage; we emit calls to them here so their string
    // literals are initialized before any user code runs.
    for (const auto& mod_name : all_module_names) {
        std::string init_name = "__ang_strlit_init_" + mod_name;
        if (auto* init_fn = mod->getFunction(init_name)) {
            builder->CreateCall(init_fn);
        } else {
            // Declare it as an external — the linker resolves it from
            // the imported module's object file.
            auto* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*ctx), false);
            auto* ext_fn = llvm::Function::Create(ft,
                llvm::Function::ExternalLinkage, init_name, mod.get());
            builder->CreateCall(ext_fn);
        }
    }

    for (const auto& stmt : statements) {
        if (std::dynamic_pointer_cast<const FuncStmt>(stmt)) continue;
        if (std::dynamic_pointer_cast<const ClassStmt>(stmt)) continue;
        if (std::dynamic_pointer_cast<const DataStmt>(stmt)) continue;
        if (std::dynamic_pointer_cast<const EnumStmt>(stmt)) continue;
        if (std::dynamic_pointer_cast<const TraitStmt>(stmt)) continue;
        if (std::dynamic_pointer_cast<const ContractStmt>(stmt)) continue;
        if (std::dynamic_pointer_cast<const AttachStmt>(stmt)) continue;
        if (std::dynamic_pointer_cast<const TypeAliasStmt>(stmt)) continue;  // LANG-12

        if (auto var_decl = std::dynamic_pointer_cast<const VarDeclStmt>(stmt)) {
            const std::string name = sanitize(var_decl->name.lexeme);

            if (var_decl->is_foreign) {
                // Load the C global and marshal to AngaraObject
                auto sym = const_cast<SymbolTable&>(m_type_checker.getSymbolTable()).resolve(var_decl->name.lexeme);
                if (sym && sym->type) {
                    auto* c_global = mod->getGlobalVariable(var_decl->name.lexeme);
                    if (c_global) {
                        auto* c_val = builder->CreateLoad(c_global->getValueType(), c_global, var_decl->name.lexeme);
                        auto* angara_val = marshalCToAngara(c_val, sym->type);

                        auto* alloca = allocLocal(main_fn, name);
                        builder->CreateStore(angara_val, alloca);
                        namedVals[name] = alloca;

                        std::string global_name = "g_" + module_name + "_" + name;
                        if (globals.count(global_name))
                            builder->CreateStore(angara_val, globals[global_name]);
                    }
                }
                continue;
            }

            // F3: byte-array globals are LLVM .rodata constants — no runtime
            // store. They were emitted in codegenGlobalVarDecl and are read-only.
            if (m_byte_array_globals.count(name) ||
                m_byte_array_globals.count("g_" + name) ||
                m_byte_array_globals.count("g_" + module_name + "_" + name)) {
                continue;
            }

            llvm::Value* init_val = var_decl->initializer ? cg(var_decl->initializer) : makeNil();

            auto* alloca = allocLocal(main_fn, name);
            builder->CreateStore(init_val, alloca);
            namedVals[name] = alloca;

            std::string global_name = "g_" + module_name + "_" + name;
            if (globals.count(global_name))
                builder->CreateStore(init_val, globals[global_name]);
        } else {
            cgStmt(stmt);
        }
    }

    llvm::Value* exit_code = llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), 0);

    // Inline the user's main body directly — no wrapper function needed
    for (const auto& stmt : statements) {
        auto func = std::dynamic_pointer_cast<const FuncStmt>(stmt);
        if (func && func->name.lexeme == "main" && func->body) {
            // Save/restore namedVals so top-level vars remain accessible
            auto saved_values = std::move(namedVals);
            auto saved_types = std::move(namedTypes);
            auto saved_kinds = std::move(namedKinds);
            namedVals.clear();
            namedTypes.clear();
            namedKinds.clear();

            // Create a return-value alloca and a cleanup block
            auto* ret_alloca = builder->CreateAlloca(llvm::Type::getInt32Ty(*ctx));
            builder->CreateStore(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), 0), ret_alloca);
            auto* cleanup_bb = llvm::BasicBlock::Create(*ctx, "main_cleanup", main_fn);
            auto* exit_bb = llvm::BasicBlock::Create(*ctx, "main_exit", main_fn);

            // Set the inlined-main context so cgReturn can emit branch instead of ret
            m_inlined_main_ret_alloca = ret_alloca;
            m_inlined_main_cleanup_bb = cleanup_bb;

            for (const auto& s : *func->body) {
                if (builder->GetInsertBlock()->getTerminator()) break;
                cgStmt(s);
            }

            // Fall-through: branch to cleanup
            if (!builder->GetInsertBlock()->getTerminator()) {
                builder->CreateBr(cleanup_bb);
            }

            // Cleanup block: pop frame, teardown runtime thread, branch to exit
            builder->SetInsertPoint(cleanup_bb);
            if (m_exc_chain_save) emitRtPopFrame();
            builder->CreateBr(exit_bb);

            // Exit block: load return value and return
            builder->SetInsertPoint(exit_bb);
            exit_code = builder->CreateLoad(llvm::Type::getInt32Ty(*ctx), ret_alloca);

            m_inlined_main_ret_alloca = nullptr;
            m_inlined_main_cleanup_bb = nullptr;

            namedVals = std::move(saved_values);
            namedTypes = std::move(saved_types);
            namedKinds = std::move(saved_kinds);
            break;
        }
    }

    if (m_freestanding) {
        auto* halt_bb = llvm::BasicBlock::Create(*ctx, "halt", main_fn);
        builder->CreateBr(halt_bb);
        builder->SetInsertPoint(halt_bb);
        builder->CreateBr(halt_bb);
    } else {
        if (gc_thread_state) {
            // Print runtime stats before teardown (debug builds only)
            if (m_debug) {
                auto print_stats = rt->getRtPrintStatsFunc();
                builder->CreateCall(print_stats);
            }
            emitRtTeardown(gc_thread_state);
        }
        builder->CreateRet(exit_code);
    }
}

// LIB-7: Returns a human-readable type name for use in runtime error messages.
static std::string typeNameForError(const std::shared_ptr<Type>& type) {
    if (!type) return "unknown";
    switch (type->kind) {
        case TypeKind::PRIMITIVE: {
            auto& name = std::dynamic_pointer_cast<PrimitiveType>(type)->name;
            if (name == "i64") return "integer";
            if (name == "f64") return "float";
            if (name == "bool") return "bool";
            if (name == "string") return "string";
            return name;
        }
        case TypeKind::LIST:   return "list";
        case TypeKind::RECORD: return "record";
        case TypeKind::NIL:    return "nil";
        case TypeKind::ANY:    return "any";
        case TypeKind::OPTIONAL:
            return typeNameForError(std::dynamic_pointer_cast<OptionalType>(type)->wrapped_type) + "?";
        case TypeKind::FUTURE: return "future";
        case TypeKind::INSTANCE:
            return std::dynamic_pointer_cast<InstanceType>(type)->class_type->name;
        default:
            return type->toString();
    }
}

llvm::BasicBlock* LLVMBackend::emitNativeTypeGuard(
    llvm::Value* arg_val,
    const std::shared_ptr<Type>& expected,
    const std::string& fn_name,
    int param_idx)
{
    using namespace llvm;
    auto* fn = builder->GetInsertBlock()->getParent();
    auto* i32_ty = llvm::Type::getInt32Ty(*ctx);

    // If "any", no guard needed — return current block unchanged.
    if (expected && expected->kind == TypeKind::ANY) {
        return builder->GetInsertBlock();
    }

    // Create the success-continuation block and the error block.
    auto* next_bb = BasicBlock::Create(*ctx, "guard.ok", fn);
    auto* err_bb  = BasicBlock::Create(*ctx, "guard.err", fn);

    // Helper: emit a check that arg's tag equals a specific TAG_* constant.
    auto emitTagCheck = [&](int tag_val) -> Value* {
        auto* tag = builder->CreateExtractValue(arg_val, {0});
        return builder->CreateICmpEQ(tag, ConstantInt::get(i32_ty, tag_val));
    };

    // Helper: after confirming arg is TAG_OBJ, load the heap object's type
    // from the ObjHeader and compare against an expected OBJ_* constant.
    // Returns an i1 condition; creates intermediate blocks as needed.
    auto emitHeapTypeCheck = [&](int expected_obj_type) -> Value* {
        auto* is_obj = emitTagCheck(TAG_OBJ);
        auto* check_bb = BasicBlock::Create(*ctx, "heap.ck", fn);
        // Branch: if obj → check_bb to inspect header; otherwise → err_bb.
        builder->CreateCondBr(is_obj, check_bb, err_bb);
        builder->SetInsertPoint(check_bb);
        auto* payload = builder->CreateExtractValue(arg_val, {1});
        auto* ptr = builder->CreateIntToPtr(payload, llvm::PointerType::get(*ctx, 0));
        auto* hdr_ptr = builder->CreateBitCast(ptr, llvm::PointerType::get(i32_ty, 0));
        auto* obj_type = builder->CreateLoad(i32_ty, hdr_ptr);
        return builder->CreateICmpEQ(obj_type, ConstantInt::get(i32_ty, expected_obj_type));
    };

    Value* cond = nullptr;
    bool    fully_handled    = false; // true when the case already branched to next_bb/err_bb

    if (!expected) {
        cond = ConstantInt::getTrue(*ctx);
    } else {
        switch (expected->kind) {
            case TypeKind::PRIMITIVE: {
                auto& name = std::dynamic_pointer_cast<PrimitiveType>(expected)->name;
                if (name == "i64")      cond = emitTagCheck(TAG_I64);
                else if (name == "f64") cond = emitTagCheck(TAG_F64);
                else if (name == "bool")cond = emitTagCheck(TAG_BOOL);
                else if (name == "string") {
                    cond = emitHeapTypeCheck(OBJ_STRING);
                }
                else cond = ConstantInt::getTrue(*ctx); // unknown primitive — skip
                break;
            }
            case TypeKind::NIL:
                cond = emitTagCheck(TAG_NIL);
                break;
            case TypeKind::LIST:
                cond = emitHeapTypeCheck(OBJ_LIST);
                break;
            case TypeKind::RECORD:
                cond = emitHeapTypeCheck(OBJ_RECORD);
                break;
            case TypeKind::INSTANCE: {
                // Native class instance — check OBJ_NATIVE_INSTANCE + class name.
                auto inst = std::dynamic_pointer_cast<InstanceType>(expected);
                std::string expected_class_name = inst->class_type->name;

                // Step 1: tag == OBJ ?
                auto* is_obj = emitTagCheck(TAG_OBJ);
                auto* obj_bb = BasicBlock::Create(*ctx, "inst.ck", fn);
                builder->CreateCondBr(is_obj, obj_bb, err_bb);
                builder->SetInsertPoint(obj_bb);

                // Step 2: obj_type == OBJ_NATIVE_INSTANCE ?
                auto* payload = builder->CreateExtractValue(arg_val, {1});
                auto* ptr = builder->CreateIntToPtr(payload, llvm::PointerType::get(*ctx, 0));
                auto* hdr_ptr = builder->CreateBitCast(ptr, llvm::PointerType::get(i32_ty, 0));
                auto* obj_type = builder->CreateLoad(i32_ty, hdr_ptr);
                auto* is_native = builder->CreateICmpEQ(obj_type, ConstantInt::get(i32_ty, OBJ_NATIVE_INSTANCE));
                auto* name_bb = BasicBlock::Create(*ctx, "name.ck", fn);
                builder->CreateCondBr(is_native, name_bb, err_bb);
                builder->SetInsertPoint(name_bb);

                // Step 3: class name matches via strcmp ?
                auto* native_inst_type = rt->getNativeInstanceType();
                auto* inst_ptr = builder->CreateBitCast(ptr, llvm::PointerType::get(native_inst_type, 0));
                // struct AngaraNativeInstance { ObjHeader, i8* data, i8* finalize, i8* name }
                auto* name_field_ptr = builder->CreateStructGEP(native_inst_type, inst_ptr, 3);
                auto* actual_name = builder->CreateLoad(llvm::PointerType::get(*ctx, 0), name_field_ptr);
                auto* expected_name_global = builder->CreateGlobalString(expected_class_name);
                auto* expected_name_ptr = builder->CreateBitCast(expected_name_global, llvm::PointerType::get(*ctx, 0));
                auto* strcmp_fn = mod->getFunction("strcmp");
                llvm::Value* cmp_result = nullptr;
                if (strcmp_fn)
                    cmp_result = builder->CreateCall(strcmp_fn, {actual_name, expected_name_ptr});
                else
                    cmp_result = ConstantInt::get(i32_ty, 0); // no strcmp — skip name check
                auto* name_match = builder->CreateICmpEQ(cmp_result, ConstantInt::get(i32_ty, 0));
                builder->CreateCondBr(name_match, next_bb, err_bb);

                fully_handled = true;
                break;
            }
            case TypeKind::FUTURE:
                // Futures are stored as native instances at runtime
                cond = emitHeapTypeCheck(OBJ_NATIVE_INSTANCE);
                break;
            case TypeKind::OPTIONAL: {
                auto opt = std::dynamic_pointer_cast<OptionalType>(expected);
                auto* is_nil = emitTagCheck(TAG_NIL);
                // Create a block to check the inner type when arg is not nil.
                auto* inner_bb = BasicBlock::Create(*ctx, "opt.ck", fn);
                builder->CreateCondBr(is_nil, next_bb, inner_bb);
                builder->SetInsertPoint(inner_bb);
                // Recurse: emit the inner-type check into inner_bb.
                // This will create its own next_bb'/err_bb' and branch.
                // We need to capture the result carefully.
                // Instead, handle inner check inline for simplicity.
                auto inner_kind = opt->wrapped_type->kind;
                Value* inner_cond = nullptr;
                if (inner_kind == TypeKind::PRIMITIVE) {
                    auto& iname = std::dynamic_pointer_cast<PrimitiveType>(opt->wrapped_type)->name;
                    if (iname == "i64")      inner_cond = emitTagCheck(TAG_I64);
                    else if (iname == "f64") inner_cond = emitTagCheck(TAG_F64);
                    else if (iname == "bool")inner_cond = emitTagCheck(TAG_BOOL);
                    else if (iname == "string") {
                        inner_cond = emitHeapTypeCheck(OBJ_STRING);
                    }
                    else inner_cond = ConstantInt::getTrue(*ctx);
                } else if (inner_kind == TypeKind::LIST) {
                    inner_cond = emitHeapTypeCheck(OBJ_LIST);
                } else if (inner_kind == TypeKind::RECORD) {
                    inner_cond = emitHeapTypeCheck(OBJ_RECORD);
                } else if (inner_kind == TypeKind::INSTANCE || inner_kind == TypeKind::FUTURE) {
                    inner_cond = emitHeapTypeCheck(OBJ_NATIVE_INSTANCE);
                } else {
                    inner_cond = ConstantInt::getTrue(*ctx);
                }
                // Branch on the inner check result.
                builder->CreateCondBr(inner_cond, next_bb, err_bb);
                fully_handled = true;
                break;
            }
            default:
                cond = ConstantInt::getTrue(*ctx);
                break;
        }
    }

    // Branch to success or error (unless the case already handled branching).
    if (!fully_handled) {
        // emitHeapTypeCheck already branched on tag!=OBJ to err_bb;
        // we still need the final branch on the obj_type / tag comparison.
        builder->CreateCondBr(cond, next_bb, err_bb);
    }

    // --- Build the error block ---
    builder->SetInsertPoint(err_bb);
    std::string msg = fn_name + "(param " + std::to_string(param_idx) + "): expected ";
    msg += typeNameForError(expected);
    auto* msg_global = builder->CreateGlobalString(msg);
    auto* msg_i8 = builder->CreateBitCast(msg_global, llvm::PointerType::get(*ctx, 0));
    auto* str_fn = mod->getFunction("__ang_string_from_c");
    llvm::Value* str_val = nullptr;
    if (str_fn)
        str_val = builder->CreateCall(str_fn, {msg_i8});
    else
        str_val = ConstantAggregateZero::get(objType);
    auto* exc_fn = mod->getFunction("__ang_exception_new");
    llvm::Value* exc_val = nullptr;
    if (exc_fn)
        exc_val = builder->CreateCall(exc_fn, {str_val});
    else
        exc_val = ConstantAggregateZero::get(objType);
    auto* throw_fn = mod->getFunction("__ang_throw");
    if (throw_fn) builder->CreateCall(throw_fn, {exc_val});
    builder->CreateUnreachable();

    // --- Continue from success block ---
    builder->SetInsertPoint(next_bb);
    return next_bb;
}

void LLVMBackend::codegenNativeModuleDecls(const std::vector<std::shared_ptr<Stmt>>& statements) {
    for (const auto& stmt : statements) {
        auto attach = std::dynamic_pointer_cast<const AttachStmt>(stmt);
        if (!attach) continue;
        auto res_it = m_type_checker.getModuleResolutions().find(attach.get());
        if (res_it == m_type_checker.getModuleResolutions().end()) continue;
        auto& mod_type = res_it->second;
        if (!mod_type || !mod_type->is_native) continue;
        const std::string& mod_name = mod_type->name;
        for (auto& [export_name, export_type] : mod_type->exports) {
            auto func_type = std::dynamic_pointer_cast<FunctionType>(export_type);
            if (!func_type) {
                auto class_type = std::dynamic_pointer_cast<ClassType>(export_type);
                if (class_type && class_type->is_native) {
                    for (auto& [method_name, method_info] : class_type->methods) {
                        auto mft = std::dynamic_pointer_cast<FunctionType>(method_info.type);
                        if (!mft) continue;
                        int mpc = (int)mft->param_types.size();
                        std::string nmn = "Angara_" + class_type->name + "_" + method_name;
                        auto* nmt = llvm::FunctionType::get(objType,
                            {llvm::Type::getInt32Ty(*ctx), llvm::PointerType::get(*ctx, 0)}, false);
                        llvm::Function::Create(nmt, llvm::Function::ExternalLinkage, nmn, mod.get());

                        std::string mwn = mangleMethod(class_type->name, method_name);
                        int total_args = mpc + 1;
                        std::vector<llvm::Type*> mwp(total_args, objType);
                        auto* mwt = llvm::FunctionType::get(objType, mwp, false);
                        // BUG 1 fix: native-module wrappers are emitted once per TU
                        // that attaches the module, so they'd collide as "multiple
                        // definition" under ExternalLinkage. Use LinkOnceODR + comdat
                        // so the linker folds identical copies. The wrapper body is a
                        // pure forwarder (deterministic per name), so ODR holds.
                        auto* mw = llvm::Function::Create(mwt, llvm::Function::LinkOnceODRLinkage, mwn, mod.get());
                        mw->setComdat(mod->getOrInsertComdat(mwn));
                        auto* me = llvm::BasicBlock::Create(*ctx, "entry", mw);
                        auto* ms = builder->GetInsertBlock();
                        builder->SetInsertPoint(me);
                        {
                            auto* mat = llvm::ArrayType::get(objType, total_args);
                            auto* ma = builder->CreateAlloca(mat);
                            int mi = 0;
                            for (auto& marg : mw->args()) {
                                auto* ep2 = builder->CreateGEP(mat, ma,
                                    {llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), 0),
                                     llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), mi++)});
                                builder->CreateStore(&marg, ep2);
                            }
                            // LIB-7: emit runtime type guards for each argument
                            {
                                auto self_type = std::make_shared<InstanceType>(class_type);
                                emitNativeTypeGuard(mw->getArg(0), self_type,
                                                    class_type->name + "." + method_name, 0);
                                for (int pi = 0; pi < mpc; pi++) {
                                    emitNativeTypeGuard(mw->getArg(pi + 1), mft->param_types[pi],
                                                        class_type->name + "." + method_name, pi + 1);
                                }
                            }
                            auto* nmf = mod->getFunction(nmn);
                            auto* mr = builder->CreateCall(nmf, {
                                llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), total_args),
                                builder->CreateBitCast(ma, llvm::PointerType::get(*ctx, 0))});
                            builder->CreateRet(mr);
                        }
                        if (ms) builder->SetInsertPoint(ms);
                        methodLookup[method_name] = mwn;
                        methodLookup[class_type->name + "." + method_name] = mwn;
                    }
                }
                continue;
            }
            int param_count = (int)func_type->param_types.size();
            bool is_variadic = func_type->is_variadic;

            std::string native_name = "Angara_" + mod_name + "_" + export_name;
            auto* native_fn_type = llvm::FunctionType::get(objType,
                {llvm::Type::getInt32Ty(*ctx), llvm::PointerType::get(*ctx, 0)}, false);
            llvm::Function::Create(native_fn_type, llvm::Function::ExternalLinkage,
                                    native_name, mod.get());

            std::string wrapper_name = mangle(mod_name, export_name);

            // For variadic native functions, skip the fixed-arity wrapper.
            // The call site (callModuleFn) handles packing args into the
            // (argc, args[]) native calling convention directly.
            if (is_variadic) {
                // Register the native name as a known function so callModuleFn
                // can find it and use the native (argc, ptr) calling convention.
                continue;
            }

            std::vector<llvm::Type*> wpt(param_count, objType);
            auto* wft = llvm::FunctionType::get(objType, wpt, false);
            // BUG 1 fix: see the method-wrapper site above — LinkOnceODR + comdat
            // lets each attaching TU emit an identical copy that the linker folds,
            // avoiding "multiple definition of __ang_<mod>_<fn>".
            auto* wf = llvm::Function::Create(wft, llvm::Function::LinkOnceODRLinkage, wrapper_name, mod.get());
            wf->setComdat(mod->getOrInsertComdat(wrapper_name));
            auto* we = llvm::BasicBlock::Create(*ctx, "entry", wf);
            auto* sb = builder->GetInsertBlock();
            builder->SetInsertPoint(we);
            if (param_count > 0) {
                auto* at = llvm::ArrayType::get(objType, param_count);
                auto* aa = builder->CreateAlloca(at);
                int ai = 0;
                for (auto& arg : wf->args()) {
                    auto* ep = builder->CreateGEP(at, aa,
                        {llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), 0),
                         llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), ai++)});
                    builder->CreateStore(&arg, ep);
                }
                // LIB-7: emit runtime type guards for each argument
                for (int pi = 0; pi < param_count; pi++) {
                    emitNativeTypeGuard(wf->getArg(pi), func_type->param_types[pi],
                                        mod_name + "." + export_name, pi);
                }
                auto* nf = mod->getFunction(native_name);
                if (!nf) {
                    if (sb) builder->SetInsertPoint(sb);
                    continue;
                }
                auto* cr = builder->CreateCall(nf, {
                    llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), param_count),
                    builder->CreateBitCast(aa, llvm::PointerType::get(*ctx, 0))});
                builder->CreateRet(cr);
            } else {
                auto* nf = mod->getFunction(native_name);
                if (!nf) {
                    if (sb) builder->SetInsertPoint(sb);
                    continue;
                }
                auto* cr = builder->CreateCall(nf, {
                    llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), 0),
                    llvm::ConstantPointerNull::get(llvm::PointerType::get(*ctx, 0))});
                builder->CreateRet(cr);
            }
            if (sb) builder->SetInsertPoint(sb);

            if (func_type->return_type) {
                if (auto it = std::dynamic_pointer_cast<InstanceType>(func_type->return_type)) {
                    auto ct = it->class_type;
                    if (ct && ct->is_native) constructorLookup[export_name] = wrapper_name;
                }
            }
        }
    }
}

// --- heap-allocation scanner ---

bool LLVMBackend::functionNeedsGC(const FuncStmt& stmt) {
    if (!stmt.body) return false;
    for (const auto& s : *stmt.body) {
        if (stmtNeedsGC(s)) return true;
    }
    return false;
}

bool LLVMBackend::stmtNeedsGC(const std::shared_ptr<Stmt>& s) {
    if (!s) return false;

    if (auto* p = dynamic_cast<const VarDeclStmt*>(s.get())) {
        // Check resolved type from type checker
        auto type_it = m_type_checker.getVariableTypes().find(p);
        if (type_it != m_type_checker.getVariableTypes().end()) {
            if (!isUnboxableType(type_it->second)) return true; // boxed local needs runtime tracking
        } else if (!p->typeAnnotation) {
            return true; // no type info → assume boxed
        }
        // Also check initializer expression
        if (p->initializer && exprNeedsGC(p->initializer)) return true;
        return false;
    }
    if (auto* p = dynamic_cast<const BlockStmt*>(s.get())) {
        for (auto& st : p->statements)
            if (stmtNeedsGC(st)) return true;
        return false;
    }
    if (auto* p = dynamic_cast<const IfStmt*>(s.get())) {
        if (exprNeedsGC(p->condition)) return true;
        if (stmtNeedsGC(p->thenBranch)) return true;
        if (stmtNeedsGC(p->elseBranch)) return true;
        return false;
    }
    if (auto* p = dynamic_cast<const WhileStmt*>(s.get())) {
        if (exprNeedsGC(p->condition)) return true;
        if (stmtNeedsGC(p->body)) return true;
        return false;
    }
    if (auto* p = dynamic_cast<const ForStmt*>(s.get())) {
        if (stmtNeedsGC(p->initializer)) return true;
        if (p->condition && exprNeedsGC(p->condition)) return true;
        if (p->increment && exprNeedsGC(p->increment)) return true;
        if (stmtNeedsGC(p->body)) return true;
        return false;
    }
    if (auto* p = dynamic_cast<const ForInStmt*>(s.get())) {
        if (exprNeedsGC(p->collection)) return true;
        if (stmtNeedsGC(p->body)) return true;
        return false;
    }
    if (auto* p = dynamic_cast<const ReturnStmt*>(s.get())) {
        return p->value ? exprNeedsGC(p->value) : false;
    }
    if (auto* p = dynamic_cast<const ExpressionStmt*>(s.get())) {
        return exprNeedsGC(p->expression);
    }
    if (auto* p = dynamic_cast<const TryStmt*>(s.get())) {
        // BUG-5: a try needs the enclosing function to carry a runtime frame so the
        // exception-chain save/restore in emitRtPushFrame/emitRtPopFrame runs —
        // otherwise a return/break/continue out of the try leaks the frame.
        (void)p;
        return true;
    }
    if (auto* p = dynamic_cast<const ThrowStmt*>(s.get())) {
        return exprNeedsGC(p->expression);
    }
    return false;
}

bool LLVMBackend::exprNeedsGC(const std::shared_ptr<Expr>& e) {
    if (!e) return false;

    // String literals are heap-allocated
    if (auto* p = dynamic_cast<const Literal*>(e.get())) {
        return p->token.type == TokenType::STRING;
    }
    // List construction is heap-allocated
    if (dynamic_cast<const ListExpr*>(e.get())) return true;
    // Record construction is heap-allocated
    if (dynamic_cast<const RecordExpr*>(e.get())) return true;
    // Lambda creates a closure (heap-allocated)
    if (dynamic_cast<const LambdaExpr*>(e.get())) return true;

    // Binary: recurse
    if (auto* p = dynamic_cast<const Binary*>(e.get()))
        return exprNeedsGC(p->left) || exprNeedsGC(p->right);
    // Unary: recurse
    if (auto* p = dynamic_cast<const Unary*>(e.get()))
        return exprNeedsGC(p->right);
    // Grouping: recurse
    if (auto* p = dynamic_cast<const Grouping*>(e.get()))
        return exprNeedsGC(p->expression);
    // Call: check arguments
    if (auto* p = dynamic_cast<const CallExpr*>(e.get())) {
        for (auto& arg : p->arguments)
            if (exprNeedsGC(arg)) return true;
        return exprNeedsGC(p->callee);
    }
    // Get: recurse
    if (auto* p = dynamic_cast<const GetExpr*>(e.get()))
        return exprNeedsGC(p->object);
    // Logical: recurse
    if (auto* p = dynamic_cast<const LogicalExpr*>(e.get()))
        return exprNeedsGC(p->left) || exprNeedsGC(p->right);
    // Subscript: recurse
    if (auto* p = dynamic_cast<const SubscriptExpr*>(e.get()))
        return exprNeedsGC(p->object) || exprNeedsGC(p->index);
    // Assign: recurse
    if (auto* p = dynamic_cast<const AssignExpr*>(e.get()))
        return exprNeedsGC(p->value);
    // Update (++/--): recurse
    if (auto* p = dynamic_cast<const UpdateExpr*>(e.get()))
        return exprNeedsGC(p->target);
    // Ternary: recurse
    if (auto* p = dynamic_cast<const TernaryExpr*>(e.get()))
        return exprNeedsGC(p->condition) || exprNeedsGC(p->thenBranch) || exprNeedsGC(p->elseBranch);
    // Is: recurse
    if (auto* p = dynamic_cast<const IsExpr*>(e.get()))
        return exprNeedsGC(p->object);
    // Cast: recurse
    if (auto* p = dynamic_cast<const CastExpr*>(e.get()))
        return exprNeedsGC(p->object);
    // Deref: recurse
    if (auto* p = dynamic_cast<const DerefExpr*>(e.get()))
        return exprNeedsGC(p->right);
    // Match: check condition and cases
    if (auto* p = dynamic_cast<const MatchExpr*>(e.get())) {
        if (exprNeedsGC(p->condition)) return true;
        for (auto& cs : p->cases)
            if (exprNeedsGC(cs.body)) return true;
        return false;
    }
    return false;
}

}
