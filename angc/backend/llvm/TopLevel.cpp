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

    for (const auto& stmt : statements) {
        if (auto s = std::dynamic_pointer_cast<const VarDeclStmt>(stmt))
            codegenGlobalVarDecl(*s);
        else if (auto s = std::dynamic_pointer_cast<const FuncStmt>(stmt)) {
            if (s->is_intrinsic) continue;
            if (s->is_foreign) { codegenForeignFuncDecl(*s); continue; }
            if (s->name.lexeme == "main") continue;
            codegenFunctionDecl(*s, moduleName);
        }
        else if (auto s = std::dynamic_pointer_cast<const ClassStmt>(stmt))
            codegenClassDecl(*s);
        else if (auto s = std::dynamic_pointer_cast<const DataStmt>(stmt))
            codegenDataDecl(*s);
        else if (auto s = std::dynamic_pointer_cast<const EnumStmt>(stmt))
            codegenEnumDecl(*s);
    }
}

void LLVMBackend::codegenGlobalVarDecl(const VarDeclStmt& stmt) {
    const std::string name = "g_" + moduleName + "_" + sanitize(stmt.name.lexeme);

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

void LLVMBackend::codegenFunctionDecl(const FuncStmt& stmt, const std::string& module_name) {
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
        if (is_raw) {
            // Raw arg arrives as raw type — store directly
            builder->CreateStore(&arg, alloca);
        } else {
            // Boxed arg arrives as objType — storeVar handles unboxing if raw alloca
            storeVar(pname, &arg);
        }
        idx++;
    }

    // Track whether we're inside a raw-signature function
    auto saved_raw_ret = m_current_raw_return_kind;
    m_current_raw_return_kind = is_raw ? raw_info.return_kind : std::optional<LocalKind>{};

    // Reset per-function codegen state. m_exc_chain_save holds an LLVM Value
    // (an alloca) from emitGcPushFrame; if a prior function set it and this one
    // doesn't push a frame, a stale value would make emitGcPopFrame reference
    // an instruction in another function → LLVM module-verify failure. Same for
    // the inlined-main members. Reset before the conditional push below.
    m_exc_chain_save = nullptr;
    m_inlined_main_ret_alloca = nullptr;
    m_inlined_main_cleanup_bb = nullptr;

    // Only push GC frame if the function has heap-referencing values.
    // Raw primitive-only functions (like fib) don't need GC at all.
    bool needs_gc = !is_raw || functionNeedsGC(stmt);
    if (needs_gc) {
        emitGcPushFrame(fn, 256);
    }

    if (stmt.body) {
        for (const auto& s : *stmt.body) {
            if (builder->GetInsertBlock()->getTerminator()) break;
            cgStmt(s);
        }
    }

    if (!builder->GetInsertBlock()->getTerminator()) {
        if (m_exc_chain_save) emitGcPopFrame();
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

            emitGcPushFrame(fn, 256);

            if (method_stmt->body) {
                for (const auto& s : *method_stmt->body) {
                    if (builder->GetInsertBlock()->getTerminator()) break;
                    cgStmt(s);
                }
            }

            if (!builder->GetInsertBlock()->getTerminator()) {
                if (m_exc_chain_save) emitGcPopFrame();
                builder->CreateRet(makeNil());
            }

            namedVals = std::move(saved_values);
            namedTypes = std::move(saved_types);
            namedKinds = std::move(saved_kinds);
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
            auto emit_vtable = [&](const std::string& iface_name,
                                   const std::vector<std::string>& method_names) {
                std::vector<llvm::Constant*> fns;
                for (const auto& mname : method_names) {
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
                // Record slot indices for indirect dispatch.
                for (size_t i = 0; i < method_names.size(); ++i) {
                    traitMethodSlots[iface_name + "." + method_names[i]] = static_cast<int>(i);
                }
            };
            for (const auto& trait : cls->adopted_traits) {
                std::vector<std::string> names;
                for (const auto& [n, sig] : trait->methods) names.push_back(n);
                emit_vtable(trait->name, names);
            }
            for (const auto& contract : cls->signed_contracts) {
                std::vector<std::string> names;
                for (const auto& [n, info] : contract->methods) names.push_back(n);
                emit_vtable(contract->name, names);
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
                    + "callback context will be leaked");
            }
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
                                        const std::vector<std::string>& all_module_names) {
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

    // GC: register main thread and push root frame
    llvm::Value* gc_thread_state = nullptr;
    if (!m_freestanding) {
        gc_thread_state = emitGcThreadSetup();
        emitGcPushFrame(main_fn, 256);
    }

    if (!m_freestanding) {
        auto* vtable = rt->getAPIVtable();
        if (vtable) {
            auto* i32_ty = llvm::Type::getInt32Ty(*ctx);
            auto* ptr_ty = llvm::PointerType::get(*ctx, 0);
            auto* init_fn_type = llvm::FunctionType::get(ptr_ty,
                {llvm::PointerType::get(i32_ty, 0), ptr_ty}, false);

            for (const auto& stmt : statements) {
                auto attach = std::dynamic_pointer_cast<const AttachStmt>(stmt);
                if (!attach) continue;
                auto res_it = m_type_checker.getModuleResolutions().find(attach.get());
                if (res_it == m_type_checker.getModuleResolutions().end()) continue;
                auto& mod_type = res_it->second;
                if (!mod_type || !mod_type->is_native) continue;

                std::string init_name = "Angara_" + mod_type->name + "_Init";
                auto* init_fn = mod->getFunction(init_name);
                if (!init_fn) {
                    init_fn = llvm::Function::Create(init_fn_type, llvm::Function::ExternalLinkage,
                                                      init_name, mod.get());
                }
                auto* def_count_alloca = builder->CreateAlloca(i32_ty);
                builder->CreateStore(llvm::ConstantInt::get(i32_ty, 0), def_count_alloca);
                builder->CreateCall(init_fn, {def_count_alloca, vtable});
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

            // Cleanup block: pop frame, teardown GC thread, branch to exit
            builder->SetInsertPoint(cleanup_bb);
            if (m_exc_chain_save) emitGcPopFrame();
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
            // Print GC stats before teardown (debug builds only)
            if (m_debug) {
                auto print_stats = rt->getGcPrintStatsFunc();
                builder->CreateCall(print_stats);
            }
            emitGcTeardown(gc_thread_state);
        }
        builder->CreateRet(exit_code);
    }
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
                        auto* mw = llvm::Function::Create(mwt, llvm::Function::ExternalLinkage, mwn, mod.get());
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
            auto* wf = llvm::Function::Create(wft, llvm::Function::ExternalLinkage, wrapper_name, mod.get());
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

// --- GC necessity scanner ---

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
            if (!isUnboxableType(type_it->second)) return true; // boxed local needs GC
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
        // BUG-5: a try needs the enclosing function to carry a GC frame so the
        // exception-chain save/restore in emitGcPushFrame/emitGcPopFrame runs —
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
