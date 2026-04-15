#include "LLVMBackend.h"

namespace angara {

void LLVMBackend::codegenTopLevelDecls(const std::vector<std::shared_ptr<Stmt>>& statements) {
    for (const auto& stmt : statements) {
        if (auto s = std::dynamic_pointer_cast<const VarDeclStmt>(stmt))
            codegenGlobalVarDecl(*s);
        else if (auto s = std::dynamic_pointer_cast<const FuncStmt>(stmt))
            codegenFunctionDecl(*s, m_module_name);
        else if (auto s = std::dynamic_pointer_cast<const ClassStmt>(stmt))
            codegenClassDecl(*s);
        else if (auto s = std::dynamic_pointer_cast<const DataStmt>(stmt))
            codegenDataDecl(*s);
        else if (auto s = std::dynamic_pointer_cast<const EnumStmt>(stmt))
            codegenEnumDecl(*s);
    }
}

void LLVMBackend::codegenGlobalVarDecl(const VarDeclStmt& stmt) {
    const std::string name = "g_" + m_module_name + "_" + sanitizeName(stmt.name.lexeme);

    // Create a global variable initialized to nil
    auto* init_const = llvm::ConstantStruct::get(m_angara_obj_type, {
        llvm::ConstantInt::get(llvm::Type::getInt32Ty(*m_context), 0), // nil tag
        llvm::ConstantInt::get(llvm::Type::getInt64Ty(*m_context), 0)  // nil value
    });

    auto* global = new llvm::GlobalVariable(
        *m_module, m_angara_obj_type, false,
        llvm::GlobalValue::PrivateLinkage, init_const, name);

    m_globals[name] = global;
    // Also register with shorter name for local module access
    m_globals["g_" + sanitizeName(stmt.name.lexeme)] = global;
}

void LLVMBackend::codegenFunctionDecl(const FuncStmt& stmt, const std::string& module_name) {
    const std::string func_name = mangleName(module_name, stmt.name.lexeme);

    // All Angara functions take AngaraObject parameters
    std::vector<llvm::Type*> param_types(stmt.params.size(), m_angara_obj_type);
    auto* fn_type = llvm::FunctionType::get(m_angara_obj_type, param_types, false);

    auto* fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage,
                                       func_name, m_module.get());

    // Set parameter names
    size_t idx = 0;
    for (auto& arg : fn->args()) {
        arg.setName(sanitizeName(stmt.params[idx].name.lexeme));
        idx++;
    }

    // Register closure global BEFORE generating body (for recursive calls)
    auto* closure_global = new llvm::GlobalVariable(
        *m_module, m_angara_obj_type, false,
        llvm::GlobalValue::PrivateLinkage,
        llvm::ConstantStruct::get(m_angara_obj_type, {
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(*m_context), 0),
            llvm::ConstantInt::get(llvm::Type::getInt64Ty(*m_context), 0)
        }),
        "g_" + module_name + "_" + stmt.name.lexeme);

    m_globals["g_" + module_name + "_" + stmt.name.lexeme] = closure_global;
    m_globals["g_" + stmt.name.lexeme] = closure_global;

    // Create entry block
    auto* entry = llvm::BasicBlock::Create(*m_context, "entry", fn);
    m_builder->SetInsertPoint(entry);

    // Save and clear named values for new scope
    auto saved_values = std::move(m_named_values);
    m_named_values.clear();

    // Create allocas for parameters
    idx = 0;
    for (auto& arg : fn->args()) {
        auto* alloca = createAlloca(fn, sanitizeName(stmt.params[idx].name.lexeme));
        m_builder->CreateStore(&arg, alloca);
        m_named_values[sanitizeName(stmt.params[idx].name.lexeme)] = alloca;
        idx++;
    }

    // Generate body
    if (stmt.body) {
        for (const auto& s : *stmt.body)
            codegenStmt(s);
    }

    // Add implicit nil return if no terminator
    if (!m_builder->GetInsertBlock()->getTerminator())
        m_builder->CreateRet(createAngaraNil());

    // Restore scope
    m_named_values = std::move(saved_values);

    // Generate wrapper function: AngaraObject angara_w_Angara_mod_fn(int argc, AngaraObject* argv)
    {
        std::string wrapper_name = "angara_w_" + func_name;
        auto* i32_type = llvm::Type::getInt32Ty(*m_context);
        auto* obj_ptr_type = llvm::PointerType::get(m_angara_obj_type, 0);
        auto* wrapper_type = llvm::FunctionType::get(m_angara_obj_type, {i32_type, obj_ptr_type}, false);
        auto* wrapper_fn = llvm::Function::Create(wrapper_type, llvm::Function::PrivateLinkage,
                                                    wrapper_name, m_module.get());

        auto* entry = llvm::BasicBlock::Create(*m_context, "entry", wrapper_fn);
        m_builder->SetInsertPoint(entry);

        auto* argc = &*wrapper_fn->arg_begin();
        auto* argv = &*(wrapper_fn->arg_begin() + 1);

        // Extract args from argv and call the actual function
        std::vector<llvm::Value*> call_args;
        for (size_t i = 0; i < stmt.params.size(); i++) {
            auto* gep = m_builder->CreateGEP(m_angara_obj_type, argv,
                {llvm::ConstantInt::get(i32_type, i)}, "arg" + std::to_string(i));
            call_args.push_back(m_builder->CreateLoad(m_angara_obj_type, gep, "a" + std::to_string(i)));
        }

        auto* callee = m_module->getFunction(func_name);
        if (callee) {
            llvm::Value* result = m_builder->CreateCall(callee, call_args, "result");
            m_builder->CreateRet(result);
        } else {
            m_builder->CreateRet(createAngaraNil());
        }
    }

}

void LLVMBackend::codegenClassDecl(const ClassStmt& stmt) {
    std::string class_name = stmt.name.lexeme;

    // Count fields from members to generate constructor
    std::vector<std::shared_ptr<VarDeclStmt>> class_fields;
    for (const auto& member : stmt.members) {
        if (auto field = std::dynamic_pointer_cast<const FieldMember>(member)) {
            class_fields.push_back(field->declaration);
        }
    }

    // Generate constructor: Angara_ClassName_new(args...)
    {
        std::vector<llvm::Type*> param_types(class_fields.size(), m_angara_obj_type);
        auto* fn_type = llvm::FunctionType::get(m_angara_obj_type, param_types, false);
        auto* fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage,
                                           "Angara_" + class_name + "_new", m_module.get());

        auto* entry = llvm::BasicBlock::Create(*m_context, "entry", fn);
        m_builder->SetInsertPoint(entry);

        auto saved_values = std::move(m_named_values);
        m_named_values.clear();

        // Create a record to hold fields
        llvm::Value* obj = callRuntimeFunc("angara_record_new", {});

        size_t i = 0;
        for (auto& arg : fn->args()) {
            std::string field_name = sanitizeName(class_fields[i]->name.lexeme);
            auto* alloca = createAlloca(fn, field_name);
            m_builder->CreateStore(&arg, alloca);
            m_named_values[field_name] = alloca;

            // Set field in record
            callRuntimeFunc("angara_record_set",
                {obj, m_builder->CreateGlobalString(class_fields[i]->name.lexeme), &arg});
            i++;
        }

        m_builder->CreateRet(obj);
        m_named_values = std::move(saved_values);
    }

    // Generate methods
    std::string saved_class = m_current_class_name;
    m_current_class_name = class_name;

    for (const auto& member : stmt.members) {
        if (auto method = std::dynamic_pointer_cast<const MethodMember>(member)) {
            const auto& method_stmt = method->declaration;
            std::string method_name = mangleMethod(class_name, method_stmt->name.lexeme);

            // Methods take 'this' + params
            std::vector<llvm::Type*> param_types(method_stmt->params.size() + 1, m_angara_obj_type);
            auto* fn_type = llvm::FunctionType::get(m_angara_obj_type, param_types, false);
            auto* fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage,
                                               method_name, m_module.get());

            auto* entry = llvm::BasicBlock::Create(*m_context, "entry", fn);
            m_builder->SetInsertPoint(entry);

            auto saved_values = std::move(m_named_values);
            m_named_values.clear();

            // First param is 'this'
            auto* this_alloca = createAlloca(fn, "this");
            m_builder->CreateStore(&*fn->arg_begin(), this_alloca);
            m_named_values["this"] = this_alloca;

            // Rest are params
            size_t i = 0;
            for (auto it = fn->arg_begin() + 1; it != fn->arg_end(); ++it, ++i) {
                std::string pname = sanitizeName(method_stmt->params[i].name.lexeme);
                auto* alloca = createAlloca(fn, pname);
                m_builder->CreateStore(&*it, alloca);
                m_named_values[pname] = alloca;
            }

            if (method_stmt->body) {
                for (const auto& s : *method_stmt->body)
                    codegenStmt(s);
            }

            if (!m_builder->GetInsertBlock()->getTerminator())
                m_builder->CreateRet(createAngaraNil());

            m_named_values = std::move(saved_values);
        }
    }

    m_current_class_name = saved_class;
}

void LLVMBackend::codegenDataDecl(const DataStmt& stmt) {
    // Data classes get constructor similar to regular classes
    std::string data_name = stmt.name.lexeme;

    // Constructor: Angara_data_new_DataName(args...)
    {
        std::vector<llvm::Type*> param_types(stmt.fields.size(), m_angara_obj_type);
        auto* fn_type = llvm::FunctionType::get(m_angara_obj_type, param_types, false);
        auto* fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage,
                                           "Angara_data_new_" + data_name, m_module.get());

        auto* entry = llvm::BasicBlock::Create(*m_context, "entry", fn);
        m_builder->SetInsertPoint(entry);

        auto saved_values = std::move(m_named_values);
        m_named_values.clear();

        llvm::Value* obj = callRuntimeFunc("angara_record_new", {});

        size_t i = 0;
        for (auto& arg : fn->args()) {
            callRuntimeFunc("angara_record_set",
                {obj, m_builder->CreateGlobalString(stmt.fields[i]->name.lexeme), &arg});
            i++;
        }

        m_builder->CreateRet(obj);
        m_named_values = std::move(saved_values);
    }
}

void LLVMBackend::codegenEnumDecl(const EnumStmt& stmt) {
    // Enums are represented as i64 values at runtime
    // Generate constants for each variant
    for (size_t i = 0; i < stmt.variants.size(); i++) {
        std::string name = "Angara_enum_" + stmt.name.lexeme + "_" + stmt.variants[i]->name.lexeme;
        auto* global = new llvm::GlobalVariable(
            *m_module, m_angara_obj_type, true,
            llvm::GlobalValue::PrivateLinkage,
            llvm::ConstantStruct::get(m_angara_obj_type, {
                llvm::ConstantInt::get(llvm::Type::getInt32Ty(*m_context), 2), // i64 tag
                llvm::ConstantInt::get(llvm::Type::getInt64Ty(*m_context), i)
            }),
            name);
        m_globals["g_" + stmt.variants[i]->name.lexeme] = global;
    }
}

void LLVMBackend::codegenMainFunction(const std::vector<std::shared_ptr<Stmt>>& statements,
                                        const std::string& module_name,
                                        const std::vector<std::string>& all_module_names) {
    // Generate: int main() { angara_runtime_init(); ... statements ...; angara_runtime_shutdown(); return 0; }
    auto* main_type = llvm::FunctionType::get(llvm::Type::getInt32Ty(*m_context), false);
    auto* main_fn = llvm::Function::Create(main_type, llvm::Function::ExternalLinkage,
                                            "main", m_module.get());

    auto* entry = llvm::BasicBlock::Create(*m_context, "entry", main_fn);
    m_builder->SetInsertPoint(entry);

    // Clear scope for main
    m_named_values.clear();

    // Call runtime init
    callRuntimeFunc("angara_runtime_init", {});

    // Initialize closure globals for all declared functions
    for (const auto& stmt : statements) {
        if (auto func_stmt = std::dynamic_pointer_cast<const FuncStmt>(stmt)) {
            const std::string fname = func_stmt->name.lexeme;
            std::string closure_name = "g_" + module_name + "_" + fname;
            if (fname == "main") closure_name = "g_angara_main_closure";
            auto git = m_globals.find(closure_name);
            if (git == m_globals.end()) {
                // Also try short name
                git = m_globals.find("g_" + fname);
            }
            if (git != m_globals.end()) {
                std::string wrapper_name = "angara_w_" + mangleName(module_name, fname);
                auto* wrapper_fn = m_module->getFunction(wrapper_name);
                if (wrapper_fn) {
                    auto* fn_ptr = m_builder->CreateBitCast(wrapper_fn,
                        llvm::PointerType::get(llvm::Type::getInt8Ty(*m_context), 0), "fn_ptr");
                    auto* arity = llvm::ConstantInt::get(llvm::Type::getInt32Ty(*m_context), func_stmt->params.size());
                    auto* is_native = llvm::ConstantInt::get(llvm::Type::getInt1Ty(*m_context), 0);
                    // angara_closure_new(void* fn, int arity, bool is_native)
                    auto* closure_obj = callRuntimeFunc("angara_closure_new", {fn_ptr, arity, is_native});
                    m_builder->CreateStore(closure_obj, git->second);
                }
            }
        }
    }

    // Process all top-level statements (variable decls become local, expressions are evaluated)
    for (const auto& stmt : statements) {
        if (std::dynamic_pointer_cast<const FuncStmt>(stmt)) continue;    // Already generated
        if (std::dynamic_pointer_cast<const ClassStmt>(stmt)) continue;   // Already generated
        if (std::dynamic_pointer_cast<const DataStmt>(stmt)) continue;    // Already generated
        if (std::dynamic_pointer_cast<const EnumStmt>(stmt)) continue;    // Already generated
        if (std::dynamic_pointer_cast<const TraitStmt>(stmt)) continue;   // Skip traits
        if (std::dynamic_pointer_cast<const ContractStmt>(stmt)) continue;
        if (std::dynamic_pointer_cast<const AttachStmt>(stmt)) continue;
        if (std::dynamic_pointer_cast<const ForeignHeaderStmt>(stmt)) continue;

        if (auto var_decl = std::dynamic_pointer_cast<const VarDeclStmt>(stmt)) {
            // Global variable initialization
            const std::string name = sanitizeName(var_decl->name.lexeme);
            llvm::Value* init_val = var_decl->initializer ? codegenExpr(var_decl->initializer) : createAngaraNil();

            auto* alloca = createAlloca(main_fn, name);
            m_builder->CreateStore(init_val, alloca);
            m_named_values[name] = alloca;

            // Also update the global if it was pre-declared
            std::string global_name = "g_" + module_name + "_" + name;
            if (m_globals.count(global_name))
                m_builder->CreateStore(init_val, m_globals[global_name]);
        } else {
            codegenStmt(stmt);
        }
    }

    // Decref all local variables in main before shutdown
    for (const auto& [name, alloca] : m_named_values) {
        llvm::Value* val = m_builder->CreateLoad(m_angara_obj_type, alloca, name + "_cleanup");
        callRuntimeFunc("angara_decref", {val});
    }

    // Call runtime shutdown
    callRuntimeFunc("angara_runtime_shutdown", {});

    m_builder->CreateRet(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*m_context), 0));
}

} // namespace angara