#include "LLVMBackend.h"

namespace angara {

llvm::Value* LLVMBackend::codegenCallExpr(const CallExpr& expr) {
    std::vector<llvm::Value*> args;
    for (const auto& arg : expr.arguments)
        args.push_back(codegenExpr(arg));

    auto callee_type = m_type_checker.m_expression_types.at(expr.callee.get());

    // Fill missing optional args with nil
    if (callee_type->kind == TypeKind::FUNCTION) {
        auto ft = std::dynamic_pointer_cast<FunctionType>(callee_type);
        if (!ft->is_variadic)
            while (args.size() < ft->param_types.size())
                args.push_back(createAngaraNil());
    } else if (callee_type->kind == TypeKind::CLASS) {
        auto ct = std::dynamic_pointer_cast<ClassType>(callee_type);
        auto it = ct->methods.find("init");
        if (it != ct->methods.end()) {
            auto ft = std::dynamic_pointer_cast<FunctionType>(it->second.type);
            while (args.size() < ft->param_types.size())
                args.push_back(createAngaraNil());
        }
    }

    // Case 1: Method call via GetExpr (obj.method())
    if (auto get = std::dynamic_pointer_cast<const GetExpr>(expr.callee)) {
        llvm::Value* obj = codegenExpr(get->object);
        const std::string& name = get->name.lexeme;
        auto obj_type = m_type_checker.m_expression_types.at(get->object.get());

        if (name == "deep_clone") return callRuntimeFunc("angara_deep_clone", {obj});
        if (obj_type->kind == TypeKind::THREAD && name == "join")
            return callRuntimeFunc("angara_thread_join", {obj});
        if (obj_type->kind == TypeKind::MUTEX && (name == "lock" || name == "unlock")) {
            callRuntimeFunc("angara_mutex_" + name, {obj});
            return createAngaraNil();
        }
        if (obj_type->kind == TypeKind::LIST) {
            if (name == "push") { callRuntimeFunc("angara_list_push", {obj, args[0]}); return createAngaraNil(); }
            if (name == "remove_at") return callRuntimeFunc("angara_list_remove_at", {obj, args[0]});
            if (name == "remove") return callRuntimeFunc("angara_list_remove", {obj, args[0]});
        }
        if (obj_type->kind == TypeKind::RECORD) {
            if (name == "remove") return callRuntimeFunc("angara_record_remove", {obj, args[0]});
            if (name == "keys") return callRuntimeFunc("angara_record_keys", {obj});
            if (name == "clone") return callRuntimeFunc("angara_record_clone", {obj});
        }

        // Instance method
        if (obj_type->kind == TypeKind::INSTANCE) {
            auto inst = std::dynamic_pointer_cast<InstanceType>(obj_type);
            const ClassType* owner = findPropertyOwner(inst->class_type.get(), name);
            if (!owner) return createAngaraNil();
            std::vector<llvm::Value*> full_args = {obj};
            full_args.insert(full_args.end(), args.begin(), args.end());
            if (owner->is_native) {
                std::string mangled = "Angara_" + owner->name + "_" + name;
                // Native: (int argc, AngaraObject* argv)
                auto* arr = m_builder->CreateAlloca(
                    llvm::ArrayType::get(m_angara_obj_type, full_args.size()));
                for (size_t i = 0; i < full_args.size(); i++)
                    m_builder->CreateStore(full_args[i],
                        m_builder->CreateGEP(llvm::ArrayType::get(m_angara_obj_type, full_args.size()),
                            arr, {llvm::ConstantInt::get(llvm::Type::getInt64Ty(*m_context), 0),
                                  llvm::ConstantInt::get(llvm::Type::getInt64Ty(*m_context), i)}));
                return callRuntimeFunc(mangled, {
                    llvm::ConstantInt::get(llvm::Type::getInt32Ty(*m_context), full_args.size()),
                    m_builder->CreateBitCast(arr, llvm::PointerType::get(m_angara_obj_type, 0))});
            }
            return callRuntimeFunc("Angara_" + owner->name + "_" + name, full_args);
        }

        // Module access
        if (obj_type->kind == TypeKind::MODULE) {
            auto mod = std::dynamic_pointer_cast<ModuleType>(obj_type);
            if (mod->is_native) {
                std::string mangled = "Angara_" + mod->name + "_" + name;
                auto* arr = m_builder->CreateAlloca(
                    llvm::ArrayType::get(m_angara_obj_type, args.size()));
                for (size_t i = 0; i < args.size(); i++)
                    m_builder->CreateStore(args[i],
                        m_builder->CreateGEP(llvm::ArrayType::get(m_angara_obj_type, args.size()),
                            arr, {llvm::ConstantInt::get(llvm::Type::getInt64Ty(*m_context), 0),
                                  llvm::ConstantInt::get(llvm::Type::getInt64Ty(*m_context), i)}));
                return callRuntimeFunc(mangled, {
                    llvm::ConstantInt::get(llvm::Type::getInt32Ty(*m_context), args.size()),
                    m_builder->CreateBitCast(arr, llvm::PointerType::get(m_angara_obj_type, 0))});
            }
            auto exp_it = mod->exports.find(name);
            if (exp_it != mod->exports.end() && exp_it->second->kind == TypeKind::CLASS)
                return callRuntimeFunc("Angara_" + name + "_new", args);
            // Closure call
            std::string closure = "g_" + mod->name + "_" + name;
            llvm::Value* closure_val = loadVariable(closure);
            auto* arr = m_builder->CreateAlloca(
                llvm::ArrayType::get(m_angara_obj_type, args.size()));
            for (size_t i = 0; i < args.size(); i++)
                m_builder->CreateStore(args[i],
                    m_builder->CreateGEP(llvm::ArrayType::get(m_angara_obj_type, args.size()),
                        arr, {llvm::ConstantInt::get(llvm::Type::getInt64Ty(*m_context), 0),
                              llvm::ConstantInt::get(llvm::Type::getInt64Ty(*m_context), i)}));
            return callRuntimeFunc("angara_call", {closure_val,
                llvm::ConstantInt::get(llvm::Type::getInt32Ty(*m_context), args.size()),
                m_builder->CreateBitCast(arr, llvm::PointerType::get(m_angara_obj_type, 0))});
        }
    }

    // Case 2: Simple name call
    if (auto var = std::dynamic_pointer_cast<const VarExpr>(expr.callee)) {
        const std::string& name = var->name.lexeme;
        auto symbol = m_type_checker.m_variable_resolutions.at(var.get());

        // Built-ins
        if (name == "len") return callRuntimeFunc("angara_len", {args[0]});
        if (name == "typeof") return callRuntimeFunc("angara_typeof", {args[0]});
        if (name == "string") return callRuntimeFunc("angara_to_string", {args[0]});
        if (name == "i64" || name == "int") return callRuntimeFunc("angara_to_i64", {args[0]});
        if (name == "f64" || name == "float") return callRuntimeFunc("angara_to_f64", {args[0]});
        if (name == "bool") return callRuntimeFunc("angara_to_bool", {args[0]});
        if (name == "Mutex") return callRuntimeFunc("angara_mutex_new", {});
        if (name == "Exception") return callRuntimeFunc("angara_exception_new", {args[0]});
        if (name == "spawn") {
            llvm::Value* closure_val = codegenExpr(expr.arguments[0]);
            std::vector<llvm::Value*> rest(args.begin() + 1, args.end());
            auto* arr = m_builder->CreateAlloca(
                llvm::ArrayType::get(m_angara_obj_type, rest.size()));
            for (size_t i = 0; i < rest.size(); i++)
                m_builder->CreateStore(rest[i],
                    m_builder->CreateGEP(llvm::ArrayType::get(m_angara_obj_type, rest.size()),
                        arr, {llvm::ConstantInt::get(llvm::Type::getInt64Ty(*m_context), 0),
                              llvm::ConstantInt::get(llvm::Type::getInt64Ty(*m_context), i)}));
            return callRuntimeFunc("angara_spawn_thread", {closure_val,
                llvm::ConstantInt::get(llvm::Type::getInt32Ty(*m_context), rest.size()),
                m_builder->CreateBitCast(arr, llvm::PointerType::get(m_angara_obj_type, 0))});
        }

        // Native module function
        if (symbol && symbol->from_module && symbol->from_module->is_native) {
            std::string mangled = "Angara_" + symbol->from_module->name + "_" + name;
            auto* arr = m_builder->CreateAlloca(
                llvm::ArrayType::get(m_angara_obj_type, args.size()));
            for (size_t i = 0; i < args.size(); i++)
                m_builder->CreateStore(args[i],
                    m_builder->CreateGEP(llvm::ArrayType::get(m_angara_obj_type, args.size()),
                        arr, {llvm::ConstantInt::get(llvm::Type::getInt64Ty(*m_context), 0),
                              llvm::ConstantInt::get(llvm::Type::getInt64Ty(*m_context), i)}));
            return callRuntimeFunc(mangled, {
                llvm::ConstantInt::get(llvm::Type::getInt32Ty(*m_context), args.size()),
                m_builder->CreateBitCast(arr, llvm::PointerType::get(m_angara_obj_type, 0))});
        }

        // Data/Class constructors
        if (callee_type->kind == TypeKind::DATA)
            return callRuntimeFunc("Angara_data_new_" + name, args);
        if (callee_type->kind == TypeKind::CLASS)
            return callRuntimeFunc("Angara_" + name + "_new", args);

        // Angara function call via closure
        if (symbol->type->kind == TypeKind::FUNCTION || symbol->type->kind == TypeKind::ANY) {
            llvm::Value* callee_val = codegenExpr(expr.callee);
            auto* arr = m_builder->CreateAlloca(
                llvm::ArrayType::get(m_angara_obj_type, args.size()));
            for (size_t i = 0; i < args.size(); i++)
                m_builder->CreateStore(args[i],
                    m_builder->CreateGEP(llvm::ArrayType::get(m_angara_obj_type, args.size()),
                        arr, {llvm::ConstantInt::get(llvm::Type::getInt64Ty(*m_context), 0),
                              llvm::ConstantInt::get(llvm::Type::getInt64Ty(*m_context), i)}));
            return callRuntimeFunc("angara_call", {callee_val,
                llvm::ConstantInt::get(llvm::Type::getInt32Ty(*m_context), args.size()),
                m_builder->CreateBitCast(arr, llvm::PointerType::get(m_angara_obj_type, 0))});
        }
    }

    // Case 3: Dynamic fallback
    llvm::Value* callee_val = codegenExpr(expr.callee);
    auto* arr = m_builder->CreateAlloca(
        llvm::ArrayType::get(m_angara_obj_type, args.size()));
    for (size_t i = 0; i < args.size(); i++)
        m_builder->CreateStore(args[i],
            m_builder->CreateGEP(llvm::ArrayType::get(m_angara_obj_type, args.size()),
                arr, {llvm::ConstantInt::get(llvm::Type::getInt64Ty(*m_context), 0),
                      llvm::ConstantInt::get(llvm::Type::getInt64Ty(*m_context), i)}));
    return callRuntimeFunc("angara_call", {callee_val,
        llvm::ConstantInt::get(llvm::Type::getInt32Ty(*m_context), args.size()),
        m_builder->CreateBitCast(arr, llvm::PointerType::get(m_angara_obj_type, 0))});
}

} // namespace angara