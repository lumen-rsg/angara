#include "LLVMBackend.h"
#include "RuntimeBuilder.h"

namespace angara {

void LLVMBackend::codegenTopLevelDecls(const std::vector<std::shared_ptr<Stmt>>& statements) {
    codegenNativeModuleDecls(statements);
    for (const auto& stmt : statements) {
        if (auto s = std::dynamic_pointer_cast<const VarDeclStmt>(stmt))
            codegenGlobalVarDecl(*s);
        else if (auto s = std::dynamic_pointer_cast<const FuncStmt>(stmt)) {
            if (s->is_intrinsic) continue;
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
            methodLookup[method_stmt->name.lexeme] = method_name;

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

            auto* this_alloca = allocLocal(fn, "this");
            builder->CreateStore(&*fn->arg_begin(), this_alloca);
            namedVals["this"] = this_alloca;

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
        namedVals.clear();

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
    namedVals.clear();

    llvm::Value* obj = callRtByName("__ang_record_new", {});

    size_t i = 0;
    for (auto& arg : fn->args()) {
        auto* key_ptr = builder->CreateGlobalString(stmt.fields[i]->name.lexeme);
        callRtByName("__ang_record_set", {obj, key_ptr, &arg});
        i++;
    }

    builder->CreateRet(obj);
    namedVals = std::move(saved_values);
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
    for (const auto& field_decl : stmt.fields) {
        auto field_type = data_type->fields[field_decl->name.lexeme].type;
        field_types.push_back(resolveCFieldType(field_type));
        field_names.push_back(field_decl->name.lexeme);
    }

    auto* struct_type = llvm::StructType::create(*ctx, field_types, "AngaraFD_" + data_name);
    m_foreign_struct_types[data_name] = struct_type;
    m_foreign_data_types[data_name] = data_type;
    m_foreign_field_order[data_name] = std::move(field_names);

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
    auto* null_finalizer = llvm::ConstantPointerNull::get(llvm::PointerType::get(*ctx, 0));
    auto* native_obj = callRtByName("__ang_api_native_instance_new",
                                     {raw_mem, null_finalizer, name_str});

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
    // Foreign data returns are pointers by default (can be opaque handles or struct pointers)
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

    // Create the Angara wrapper function
    std::string wrapperName = mangle(moduleName, stmt.name.lexeme);
    std::vector<llvm::Type*> wrapperParamTypes(stmt.params.size(), objType);
    auto* wrapperFnType = llvm::FunctionType::get(objType, wrapperParamTypes, false);
    auto* wrapperFn = llvm::Function::Create(wrapperFnType, llvm::Function::ExternalLinkage,
                                               wrapperName, mod.get());

    auto* entry = llvm::BasicBlock::Create(*ctx, "entry", wrapperFn);
    builder->SetInsertPoint(entry);

    auto saved_values = std::move(namedVals);
    namedVals.clear();

    // Unpack AngaraObject args -> C args
    std::vector<llvm::Value*> cArgs;
    size_t idx = 0;
    for (auto& arg : wrapperFn->args()) {
        std::string pname = sanitize(stmt.params[idx].name.lexeme);
        auto* alloca = allocLocal(wrapperFn, pname);
        builder->CreateStore(&arg, alloca);
        namedVals[pname] = alloca;

        auto& ptype = func_type->param_types[idx];
        cArgs.push_back(marshalAngaraToC(&arg, ptype));
        idx++;
    }

    llvm::CallInst* cResult = builder->CreateCall(cFunc, cArgs);

    // Repack C result -> AngaraObject
    if (returnsVoid) {
        builder->CreateRet(makeNil());
    } else {
        builder->CreateRet(marshalCToAngara(cResult, ret_type));
    }

    namedVals = std::move(saved_values);
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
            namedVals.clear();

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

            constructorLookup[stmt.name.lexeme + "." + variant->name.lexeme] = ctor_name;
        }
    }
}

void LLVMBackend::codegenMainFunction(const std::vector<std::shared_ptr<Stmt>>& statements,
                                        const std::string& module_name,
                                        const std::vector<std::string>&) {
    std::string entry_name = m_freestanding ? "_start" : "main";
    auto* main_type = llvm::FunctionType::get(
        m_freestanding ? llvm::Type::getVoidTy(*ctx) : llvm::Type::getInt32Ty(*ctx), false);
    auto* main_fn = llvm::Function::Create(main_type, llvm::Function::ExternalLinkage,
                                            entry_name, mod.get());

    auto* entry = llvm::BasicBlock::Create(*ctx, "entry", main_fn);
    builder->SetInsertPoint(entry);

    namedVals.clear();
    namedTypes.clear();

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
        callRtByName("__ang_decref", {val});
    }
    }

    if (m_freestanding) {
        auto* halt_bb = llvm::BasicBlock::Create(*ctx, "halt", main_fn);
        builder->CreateBr(halt_bb);
        builder->SetInsertPoint(halt_bb);
        builder->CreateBr(halt_bb);
    } else {
        builder->CreateRet(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), 0));
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
                    }
                }
                continue;
            }
            int param_count = (int)func_type->param_types.size();

            std::string native_name = "Angara_" + mod_name + "_" + export_name;
            auto* native_fn_type = llvm::FunctionType::get(objType,
                {llvm::Type::getInt32Ty(*ctx), llvm::PointerType::get(*ctx, 0)}, false);
            llvm::Function::Create(native_fn_type, llvm::Function::ExternalLinkage,
                                    native_name, mod.get());

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
                if (!nf) {
                    continue;
                }
                auto* cr = builder->CreateCall(nf, {
                    llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), param_count),
                    builder->CreateBitCast(aa, llvm::PointerType::get(*ctx, 0))});
                builder->CreateRet(cr);
            } else {
                auto* nf = mod->getFunction(native_name);
                if (!nf) {
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

}
