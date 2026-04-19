#include "LLVMBackend.h"
#include "RuntimeBuilder.h"

namespace angara {

void LLVMBackend::codegenTopLevelDecls(const std::vector<std::shared_ptr<Stmt>>& statements) {
    codegenNativeModuleDecls(statements);
    for (const auto& stmt : statements) {
        if (auto s = std::dynamic_pointer_cast<const VarDeclStmt>(stmt))
            codegenGlobalVarDecl(*s);
        else if (auto s = std::dynamic_pointer_cast<const FuncStmt>(stmt)) {
            // Intrinsic functions are handled inline by cgCall — no LLVM function to emit
            if (s->is_intrinsic) continue;
            // Foreign functions get a C ABI wrapper
            if (s->is_foreign) { codegenForeignFuncDecl(*s); continue; }
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
}

void LLVMBackend::codegenFunctionDecl(const FuncStmt& stmt, const std::string& module_name) {
    const std::string func_name = mangle(module_name, stmt.name.lexeme);

    std::vector<llvm::Type*> param_types(stmt.params.size(), objType);
    auto* fn_type = llvm::FunctionType::get(objType, param_types, false);

    // Reuse existing declaration if one was forward-declared (e.g., from a closure wrapper)
    auto* fn = mod->getFunction(func_name);
    if (!fn) {
        fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage,
                                     func_name, mod.get());
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
    namedVals.clear();
    namedTypes.clear();

    idx = 0;
    for (auto& arg : fn->args()) {
        auto* alloca = allocLocal(fn, sanitize(stmt.params[idx].name.lexeme));
        builder->CreateStore(&arg, alloca);
        namedVals[sanitize(stmt.params[idx].name.lexeme)] = alloca;
        idx++;
    }

    if (stmt.body) {
        for (const auto& s : *stmt.body) {
            if (builder->GetInsertBlock()->getTerminator()) break;
            cgStmt(s);
        }
    }

    if (!builder->GetInsertBlock()->getTerminator())
        builder->CreateRet(makeNil());

    namedVals = std::move(saved_values);
    namedTypes = std::move(saved_types);
}

void LLVMBackend::codegenClassDecl(const ClassStmt& stmt) {
    std::string class_name = stmt.name.lexeme;

    std::vector<std::shared_ptr<VarDeclStmt>> class_fields;
    for (const auto& member : stmt.members) {
        if (auto field = std::dynamic_pointer_cast<const FieldMember>(member)) {
            class_fields.push_back(field->declaration);
        }
    }

    // Find init method and determine constructor params
    std::shared_ptr<FuncStmt> init_method;
    for (const auto& member : stmt.members) {
        if (auto method = std::dynamic_pointer_cast<const MethodMember>(member)) {
            if (method->declaration->name.lexeme == "init") {
                init_method = method->declaration;
                break;
            }
        }
    }

    // Generate methods FIRST (so init exists when constructor calls it)
    for (const auto& member : stmt.members) {
        if (auto method = std::dynamic_pointer_cast<const MethodMember>(member)) {
            const auto& method_stmt = method->declaration;
            std::string method_name = mangleMethod(class_name, method_stmt->name.lexeme);
            // Register method by name (for dispatch in cgCall)
            methodLookup[method_stmt->name.lexeme] = method_name;

            // 'this' is implicit (has_this flag), params doesn't include it
            // LLVM function: this, param0, param1, ...
            std::vector<llvm::Type*> param_types(method_stmt->params.size() + 1, objType);
            auto* fn_type = llvm::FunctionType::get(objType, param_types, false);
            auto* fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage,
                                               method_name, mod.get());

            auto* entry = llvm::BasicBlock::Create(*ctx, "entry", fn);
            builder->SetInsertPoint(entry);

            auto saved_values = std::move(namedVals);
            auto saved_types = std::move(namedTypes);
            namedVals.clear();
            namedTypes.clear();

            // First arg is always 'this'
            auto* this_alloca = allocLocal(fn, "this");
            builder->CreateStore(&*fn->arg_begin(), this_alloca);
            namedVals["this"] = this_alloca;

            // Remaining args: bind params[0..n] (this is NOT in params)
            size_t pi = 0;
            for (auto it = fn->arg_begin() + 1; it != fn->arg_end() && pi < method_stmt->params.size(); ++it, ++pi) {
                std::string pname = sanitize(method_stmt->params[pi].name.lexeme);
                auto* alloca = allocLocal(fn, pname);
                builder->CreateStore(&*it, alloca);
                namedVals[pname] = alloca;
            }

            if (method_stmt->body) {
                for (const auto& s : *method_stmt->body) {
                    if (builder->GetInsertBlock()->getTerminator()) break;
                    cgStmt(s);
                }
            }

            if (!builder->GetInsertBlock()->getTerminator())
                builder->CreateRet(makeNil());

            namedVals = std::move(saved_values);
            namedTypes = std::move(saved_types);
        }
    }

    // Constructor params = init method params (this is NOT in params, so no -1)
    size_t ctor_param_count = init_method
        ? init_method->params.size()
        : class_fields.size();

    // Constructor (generated after methods so it can call init)
    std::string ctor_name = "Angara_" + class_name + "_new";
    {
        std::vector<llvm::Type*> param_types(ctor_param_count, objType);
        auto* fn_type = llvm::FunctionType::get(objType, param_types, false);
        auto* fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage,
                                           ctor_name, mod.get());

        auto* entry = llvm::BasicBlock::Create(*ctx, "entry", fn);
        builder->SetInsertPoint(entry);

        auto saved_values = std::move(namedVals);
        namedVals.clear();

        llvm::Value* obj = callRt(rt->getFuncRecordNew(), {});

        if (init_method) {
            // Call init(this, args...) — first arg is 'this', rest are ctor params
            std::string init_mangled = mangleMethod(class_name, "init");
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
            // No init method: set fields directly from constructor args
            size_t i = 0;
            for (auto& arg : fn->args()) {
                if (i < class_fields.size()) {
                    auto* key_ptr = builder->CreateGlobalString(class_fields[i]->name.lexeme);
                    callRt(rt->getFuncRecordSet(), {obj, key_ptr, &arg});
                }
                i++;
            }
        }

        builder->CreateRet(obj);
        namedVals = std::move(saved_values);
    }
    // Register constructor for this class
    constructorLookup[class_name] = ctor_name;
}

void LLVMBackend::codegenDataDecl(const DataStmt& stmt) {
    std::string data_name = stmt.name.lexeme;

    std::vector<llvm::Type*> param_types(stmt.fields.size(), objType);
    auto* fn_type = llvm::FunctionType::get(objType, param_types, false);
    auto* fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage,
                                       "Angara_data_new_" + data_name, mod.get());

    auto* entry = llvm::BasicBlock::Create(*ctx, "entry", fn);
    builder->SetInsertPoint(entry);

    auto saved_values = std::move(namedVals);
    namedVals.clear();

    llvm::Value* obj = callRt(rt->getFuncRecordNew(), {});

    size_t i = 0;
    for (auto& arg : fn->args()) {
        auto* key_ptr = builder->CreateGlobalString(stmt.fields[i]->name.lexeme);
        callRt(rt->getFuncRecordSet(), {obj, key_ptr, &arg});
        i++;
    }

    builder->CreateRet(obj);
    namedVals = std::move(saved_values);
    // Register data class constructor
    constructorLookup[data_name] = "Angara_data_new_" + data_name;
}

void LLVMBackend::codegenForeignFuncDecl(const FuncStmt& stmt) {
    // Foreign function: generates a C ABI extern declaration + an AngaraObject wrapper.
    //
    // Angara code:  foreign func pinMode(pin as i64, mode as i64) -> nil;
    // Generates:
    //   1. LLVM declare:  declare void @pinMode(i64, i64)
    //   2. AngaraObject wrapper:  define @__ang_main_pinMode(AngaraObject, AngaraObject) -> AngaraObject
    //      which extracts i64 from each AngaraObject param, calls @pinMode, wraps result

    // --- Resolve return type ---
    auto isVoidType = [](const std::shared_ptr<ASTType>& type) -> bool {
        if (!type) return true; // no return type = void/nil
        if (auto* s = dynamic_cast<const SimpleType*>(type.get()))
            return s->name.lexeme == "nil" || s->name.lexeme == "void";
        return false;
    };

    auto resolveCType = [&](const std::shared_ptr<ASTType>& type) -> llvm::Type* {
        if (!type) return llvm::Type::getInt64Ty(*ctx);
        if (auto* s = dynamic_cast<const SimpleType*>(type.get())) {
            const auto& n = s->name.lexeme;
            if (n == "bool")   return llvm::Type::getInt1Ty(*ctx);
            if (n == "i8")     return llvm::Type::getInt8Ty(*ctx);
            if (n == "i16")    return llvm::Type::getInt16Ty(*ctx);
            if (n == "i32")    return llvm::Type::getInt32Ty(*ctx);
            if (n == "i64")    return llvm::Type::getInt64Ty(*ctx);
            if (n == "f32")    return llvm::Type::getFloatTy(*ctx);
            if (n == "f64")    return llvm::Type::getDoubleTy(*ctx);
            if (n == "string") return llvm::PointerType::get(*ctx, 0); // const char*
        }
        return llvm::Type::getInt64Ty(*ctx); // default
    };

    auto getTypeName = [](const std::shared_ptr<ASTType>& type) -> std::string {
        if (!type) return "i64";
        if (auto* s = dynamic_cast<const SimpleType*>(type.get()))
            return s->name.lexeme;
        return "i64";
    };

    bool returnsVoid = isVoidType(stmt.returnType);
    llvm::Type* cRetType = returnsVoid ? llvm::Type::getVoidTy(*ctx) : resolveCType(stmt.returnType);

    // Resolve param C types
    std::vector<llvm::Type*> cParamTypes;
    std::vector<std::string> paramTypeNames;
    for (const auto& param : stmt.params) {
        cParamTypes.push_back(resolveCType(param.type));
        paramTypeNames.push_back(getTypeName(param.type));
    }

    // 1. Declare the extern C function (resolved at link time)
    std::string cFuncName = stmt.name.lexeme;
    auto* cFnType = llvm::FunctionType::get(cRetType, cParamTypes, false);
    auto* cFunc = llvm::Function::Create(cFnType, llvm::Function::ExternalLinkage, cFuncName, mod.get());

    // 2. Generate AngaraObject wrapper: __ang_<module>_<name>(AngaraObject, ...) -> AngaraObject
    std::string wrapperName = mangle(moduleName, stmt.name.lexeme);
    std::vector<llvm::Type*> wrapperParamTypes(stmt.params.size(), objType);
    auto* wrapperFnType = llvm::FunctionType::get(objType, wrapperParamTypes, false);
    auto* wrapperFn = llvm::Function::Create(wrapperFnType, llvm::Function::ExternalLinkage,
                                               wrapperName, mod.get());

    auto* entry = llvm::BasicBlock::Create(*ctx, "entry", wrapperFn);
    builder->SetInsertPoint(entry);

    auto saved_values = std::move(namedVals);
    namedVals.clear();

    // Extract native C values from AngaraObject params
    std::vector<llvm::Value*> cArgs;
    size_t idx = 0;
    for (auto& arg : wrapperFn->args()) {
        std::string pname = sanitize(stmt.params[idx].name.lexeme);
        auto* alloca = allocLocal(wrapperFn, pname);
        builder->CreateStore(&arg, alloca);
        namedVals[pname] = alloca;

        const std::string& typeName = paramTypeNames[idx];

        if (typeName == "bool") {
            cArgs.push_back(getBool(&arg));
        } else if (typeName == "i8" || typeName == "i16" || typeName == "i32") {
            auto* cty = cParamTypes[idx];
            cArgs.push_back(builder->CreateTrunc(getI64(&arg), cty));
        } else if (typeName == "i64") {
            cArgs.push_back(getI64(&arg));
        } else if (typeName == "f64") {
            cArgs.push_back(getF64(&arg));
        } else if (typeName == "f32") {
            cArgs.push_back(builder->CreateFPTrunc(getF64(&arg), llvm::Type::getFloatTy(*ctx)));
        } else if (typeName == "string") {
            // Pass raw pointer from AngaraObject payload
            cArgs.push_back(builder->CreateIntToPtr(getI64(&arg), llvm::PointerType::get(*ctx, 0)));
        } else {
            cArgs.push_back(getI64(&arg));
        }
        idx++;
    }

    // Call the C function
    llvm::CallInst* cResult = builder->CreateCall(cFunc, cArgs);

    // Wrap C result into AngaraObject
    if (returnsVoid) {
        builder->CreateRet(makeNil());
    } else {
        const std::string retTypeName = getTypeName(stmt.returnType);
        if (retTypeName == "bool") {
            builder->CreateRet(makeBool(cResult));
        } else if (retTypeName == "i8" || retTypeName == "i16" || retTypeName == "i32") {
            builder->CreateRet(makeI64(builder->CreateZExt(cResult, llvm::Type::getInt64Ty(*ctx))));
        } else if (retTypeName == "i64") {
            builder->CreateRet(makeI64(cResult));
        } else if (retTypeName == "f64") {
            builder->CreateRet(makeF64(cResult));
        } else if (retTypeName == "f32") {
            builder->CreateRet(makeF64(builder->CreateFPExt(cResult, llvm::Type::getDoubleTy(*ctx))));
        } else if (retTypeName == "string") {
            // C string → Angara string via runtime
            builder->CreateRet(callRt(rt->getFuncStringFromC(), {cResult}));
        } else {
            builder->CreateRet(makeI64(cResult));
        }
    }

    namedVals = std::move(saved_values);
}

void LLVMBackend::codegenEnumDecl(const EnumStmt& stmt) {
    for (size_t i = 0; i < stmt.variants.size(); i++) {
        std::string name = "Angara_enum_" + stmt.name.lexeme + "_" + stmt.variants[i]->name.lexeme;
        auto* init_const = llvm::ConstantStruct::get(objType, {
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), TAG_I64),
            llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), i)
        });
        auto* global = new llvm::GlobalVariable(
            *mod, objType, true,
            llvm::GlobalValue::PrivateLinkage, init_const, name);
        globals["g_" + stmt.variants[i]->name.lexeme] = global;
    }
}

void LLVMBackend::codegenMainFunction(const std::vector<std::shared_ptr<Stmt>>& statements,
                                        const std::string& module_name,
                                        const std::vector<std::string>&) {
    // In freestanding mode, generate _start entry point (no libc dependency)
    std::string entry_name = m_freestanding ? "_start" : "main";
    auto* main_type = llvm::FunctionType::get(
        m_freestanding ? llvm::Type::getVoidTy(*ctx) : llvm::Type::getInt32Ty(*ctx), false);
    auto* main_fn = llvm::Function::Create(main_type, llvm::Function::ExternalLinkage,
                                            entry_name, mod.get());

    auto* entry = llvm::BasicBlock::Create(*ctx, "entry", main_fn);
    builder->SetInsertPoint(entry);

    namedVals.clear();
    namedTypes.clear();

    for (const auto& stmt : statements) {
        if (std::dynamic_pointer_cast<const FuncStmt>(stmt)) continue;
        if (std::dynamic_pointer_cast<const ClassStmt>(stmt)) continue;
        if (std::dynamic_pointer_cast<const DataStmt>(stmt)) continue;
        if (std::dynamic_pointer_cast<const EnumStmt>(stmt)) continue;
        if (std::dynamic_pointer_cast<const TraitStmt>(stmt)) continue;
        if (std::dynamic_pointer_cast<const ContractStmt>(stmt)) continue;
        if (std::dynamic_pointer_cast<const AttachStmt>(stmt)) continue;
        if (std::dynamic_pointer_cast<const ForeignHeaderStmt>(stmt)) continue;

        if (auto var_decl = std::dynamic_pointer_cast<const VarDeclStmt>(stmt)) {
            const std::string name = sanitize(var_decl->name.lexeme);
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

    std::string main_func_name = mangle(module_name, "main");
    auto* user_main = mod->getFunction(main_func_name);
    if (user_main) {
        builder->CreateCall(user_main, {});
    }

    if (!m_freestanding) {
    for (const auto& [name, alloca] : namedVals) {
        llvm::Value* val = builder->CreateLoad(objType, alloca);
        callRt(rt->getFuncDecref(), {val});
    }
    }

    if (m_freestanding) {
        // Freestanding: infinite loop (no OS to return to)
        auto* halt_bb = llvm::BasicBlock::Create(*ctx, "halt", main_fn);
        builder->CreateBr(halt_bb);
        builder->SetInsertPoint(halt_bb);
        builder->CreateBr(halt_bb); // infinite halt loop
    } else {
        builder->CreateRet(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), 0));
    }
}

// ============================================================================
// Native Module Support — declare externals for attach'd native modules
// ============================================================================
void LLVMBackend::codegenNativeModuleDecls(const std::vector<std::shared_ptr<Stmt>>& statements) {
    // Use m_module_resolutions from type checker, keyed by raw AttachStmt ptr.
    // The shared_ptrs in 'statements' keep the objects alive.
    for (const auto& stmt : statements) {
        auto attach = std::dynamic_pointer_cast<const AttachStmt>(stmt);
        if (!attach) continue;
        auto res_it = m_type_checker.m_module_resolutions.find(attach.get());
        if (res_it == m_type_checker.m_module_resolutions.end()) continue;
        auto& mod_type = res_it->second;
        if (!mod_type || !mod_type->is_native) continue;
        const std::string& mod_name = mod_type->name;

        // Declare exported functions and create wrappers
        for (auto& [export_name, export_type] : mod_type->exports) {
            auto func_type = std::dynamic_pointer_cast<FunctionType>(export_type);
            if (!func_type) {
                // It's a class type — register methods
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
                        std::vector<llvm::Type*> mwp(mpc, objType);
                        auto* mwt = llvm::FunctionType::get(objType, mwp, false);
                        auto* mw = llvm::Function::Create(mwt, llvm::Function::ExternalLinkage, mwn, mod.get());
                        auto* me = llvm::BasicBlock::Create(*ctx, "entry", mw);
                        auto* ms = builder->GetInsertBlock();
                        builder->SetInsertPoint(me);
                        if (mpc > 0) {
                            auto* mat = llvm::ArrayType::get(objType, mpc);
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
                                llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), mpc),
                                builder->CreateBitCast(ma, llvm::PointerType::get(*ctx, 0))});
                            builder->CreateRet(mr);
                        } else {
                            auto* nmf = mod->getFunction(nmn);
                            auto* mr = builder->CreateCall(nmf, {
                                llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), 0),
                                llvm::ConstantPointerNull::get(llvm::PointerType::get(*ctx, 0))});
                            builder->CreateRet(mr);
                        }
                        if (ms) builder->SetInsertPoint(ms);
                        methodLookup[method_name] = mwn;
                    }
                }
                continue;
            }
            int param_count = (int)func_type->param_types.size();

            // Declare native: AngaraObject Angara_<mod>_<name>(i32, AngaraObject*)
            std::string native_name = "Angara_" + mod_name + "_" + export_name;
            auto* native_fn_type = llvm::FunctionType::get(objType,
                {llvm::Type::getInt32Ty(*ctx), llvm::PointerType::get(*ctx, 0)}, false);
            llvm::Function::Create(native_fn_type, llvm::Function::ExternalLinkage,
                                    native_name, mod.get());

            // Wrapper: __ang_<mod>_<name>(AngaraObject...) -> AngaraObject
            std::string wrapper_name = mangle(mod_name, export_name);
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
                auto* cr = builder->CreateCall(nf, {
                    llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), param_count),
                    builder->CreateBitCast(aa, llvm::PointerType::get(*ctx, 0))});
                builder->CreateRet(cr);
            } else {
                auto* nf = mod->getFunction(native_name);
                auto* cr = builder->CreateCall(nf, {
                    llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), 0),
                    llvm::ConstantPointerNull::get(llvm::PointerType::get(*ctx, 0))});
                builder->CreateRet(cr);
            }
            if (sb) builder->SetInsertPoint(sb);

            // Register as constructor if returns native class instance
            if (func_type->return_type) {
                if (auto it = std::dynamic_pointer_cast<InstanceType>(func_type->return_type)) {
                    auto ct = it->class_type;
                    if (ct && ct->is_native) constructorLookup[export_name] = wrapper_name;
                }
            }
        }
    }
}

} // namespace angara
