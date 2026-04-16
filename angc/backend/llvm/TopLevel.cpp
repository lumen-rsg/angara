#include "LLVMBackend.h"
#include "RuntimeBuilder.h"

namespace angara {

void LLVMBackend::codegenTopLevelDecls(const std::vector<std::shared_ptr<Stmt>>& statements) {
    for (const auto& stmt : statements) {
        if (auto s = std::dynamic_pointer_cast<const VarDeclStmt>(stmt))
            codegenGlobalVarDecl(*s);
        else if (auto s = std::dynamic_pointer_cast<const FuncStmt>(stmt))
            codegenFunctionDecl(*s, moduleName);
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

    auto* fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage,
                                       func_name, mod.get());

    size_t idx = 0;
    for (auto& arg : fn->args()) {
        arg.setName(sanitize(stmt.params[idx].name.lexeme));
        idx++;
    }

    auto* entry = llvm::BasicBlock::Create(*ctx, "entry", fn);
    builder->SetInsertPoint(entry);

    auto saved_values = std::move(namedVals);
    namedVals.clear();

    idx = 0;
    for (auto& arg : fn->args()) {
        auto* alloca = allocLocal(fn, sanitize(stmt.params[idx].name.lexeme));
        builder->CreateStore(&arg, alloca);
        namedVals[sanitize(stmt.params[idx].name.lexeme)] = alloca;
        idx++;
    }

    if (stmt.body) {
        for (const auto& s : *stmt.body)
            cgStmt(s);
    }

    if (!builder->GetInsertBlock()->getTerminator())
        builder->CreateRet(makeNil());

    namedVals = std::move(saved_values);
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
            namedVals.clear();

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
                for (const auto& s : *method_stmt->body)
                    cgStmt(s);
            }

            if (!builder->GetInsertBlock()->getTerminator())
                builder->CreateRet(makeNil());

            namedVals = std::move(saved_values);
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

    for (const auto& [name, alloca] : namedVals) {
        llvm::Value* val = builder->CreateLoad(objType, alloca);
        callRt(rt->getFuncDecref(), {val});
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

} // namespace angara