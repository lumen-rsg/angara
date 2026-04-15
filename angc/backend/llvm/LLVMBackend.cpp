#include "LLVMBackend.h"
#include <llvm/IR/Verifier.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <set>

namespace angara {

LLVMBackend::LLVMBackend(TypeChecker& type_checker, ErrorHandler& errorHandler)
    : m_type_checker(type_checker), m_errorHandler(errorHandler) {
    m_context = std::make_unique<llvm::LLVMContext>();
    m_module = std::make_unique<llvm::Module>("angara_module", *m_context);
    m_builder = std::make_unique<llvm::IRBuilder<>>(*m_context);

    // Define AngaraObject struct type: { i32 (type tag), i64 (payload/union) }
    m_angara_obj_type = llvm::StructType::create(
        *m_context,
        {llvm::Type::getInt32Ty(*m_context), llvm::Type::getInt64Ty(*m_context)},
        "AngaraObject"
    );
    m_angara_obj_ptr_type = llvm::PointerType::get(m_angara_obj_type, 0);

    // GenericAngaraFn: AngaraObject(int, AngaraObject*)
    m_generic_fn_type = getGenericAngaraFnType();

    // Set target triple
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    m_target_triple = llvm::Triple(llvm::sys::getDefaultTargetTriple());
    m_module->setTargetTriple(m_target_triple);
}

llvm::FunctionType* LLVMBackend::getGenericAngaraFnType() {
    return llvm::FunctionType::get(
        m_angara_obj_type,
        {llvm::Type::getInt32Ty(*m_context),
         llvm::PointerType::get(m_angara_obj_type, 0)},
        false
    );
}

std::string LLVMBackend::mangleName(const std::string& module, const std::string& name) {
    return "Angara_" + module + "_" + name;
}

std::string LLVMBackend::mangleMethod(const std::string& class_name, const std::string& method) {
    return "Angara_" + class_name + "_" + method;
}

std::string LLVMBackend::sanitizeName(const std::string& name) {
    static const std::set<std::string> c_keywords = {
        "auto", "break", "case", "char", "const", "continue", "default",
        "do", "double", "else", "enum", "extern", "float", "for", "goto",
        "if", "int", "long", "register", "return", "short", "signed",
        "sizeof", "static", "struct", "switch", "typedef", "union",
        "unsigned", "void", "volatile", "while"
    };
    if (c_keywords.count(name)) return name + "_";
    return name;
}

const ClassType* LLVMBackend::findPropertyOwner(const ClassType* klass, const std::string& prop_name) {
    if (!klass) return nullptr;
    if (klass->fields.count(prop_name) || klass->methods.count(prop_name)) return klass;
    return findPropertyOwner(klass->superclass.get(), prop_name);
}

const FuncStmt* LLVMBackend::findMethodAst(const ClassStmt& class_stmt, const std::string& name) {
    for (const auto& member : class_stmt.members) {
        if (auto method = std::dynamic_pointer_cast<const MethodMember>(member)) {
            if (method->declaration->name.lexeme == name) return method->declaration.get();
        }
    }
    return nullptr;
}

// --- AngaraObject Construction Helpers ---

llvm::Value* LLVMBackend::createAngaraNil() {
    return callRuntimeFunc("angara_create_nil", {});
}

llvm::Value* LLVMBackend::createAngaraBool(bool val) {
    return callRuntimeFunc("angara_create_bool", {
        llvm::ConstantInt::get(llvm::Type::getInt1Ty(*m_context), val)
    });
}

llvm::Value* LLVMBackend::createAngaraI64(int64_t val) {
    return callRuntimeFunc("angara_create_i64", {
        llvm::ConstantInt::get(llvm::Type::getInt64Ty(*m_context), val, true)
    });
}

llvm::Value* LLVMBackend::createAngaraF64(double val) {
    return callRuntimeFunc("angara_create_f64", {
        llvm::ConstantFP::get(llvm::Type::getDoubleTy(*m_context), val)
    });
}

llvm::Value* LLVMBackend::createAngaraString(const std::string& str) {
    auto* str_const = m_builder->CreateGlobalString(str);
    return callRuntimeFunc("angara_string_from_c", {str_const});
}

llvm::Value* LLVMBackend::extractI64(llvm::Value* obj) {
    auto* ptr = m_builder->CreateStructGEP(m_angara_obj_type, obj, 1, "i64_ptr");
    return m_builder->CreateLoad(llvm::Type::getInt64Ty(*m_context), ptr, "i64_val");
}

llvm::Value* LLVMBackend::extractF64(llvm::Value* obj) {
    auto* i64_val = extractI64(obj);
    return m_builder->CreateBitCast(i64_val, llvm::Type::getDoubleTy(*m_context), "f64_val");
}

llvm::Value* LLVMBackend::extractBool(llvm::Value* obj) {
    auto* i64_val = extractI64(obj);
    return m_builder->CreateTrunc(i64_val, llvm::Type::getInt1Ty(*m_context), "bool_val");
}

llvm::Value* LLVMBackend::extractObj(llvm::Value* obj) {
    auto* i64_val = extractI64(obj);
    return m_builder->CreateIntToPtr(i64_val,
        llvm::PointerType::get(llvm::Type::getInt8Ty(*m_context), 0), "obj_ptr");
}

llvm::Value* LLVMBackend::extractTypeTag(llvm::Value* obj) {
    auto* ptr = m_builder->CreateStructGEP(m_angara_obj_type, obj, 0, "tag_ptr");
    return m_builder->CreateLoad(llvm::Type::getInt32Ty(*m_context), ptr, "type_tag");
}

llvm::Value* LLVMBackend::isTruthy(llvm::Value* obj) {
    auto* fn = getOrDeclareRuntimeFunc("angara_is_truthy",
        llvm::FunctionType::get(llvm::Type::getInt1Ty(*m_context), {m_angara_obj_type}, false));
    return m_builder->CreateCall(fn, {obj}, "is_truthy");
}

llvm::Value* LLVMBackend::isNil(llvm::Value* obj) {
    auto* tag = extractTypeTag(obj);
    return m_builder->CreateICmpEQ(tag, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*m_context), 0), "is_nil");
}

llvm::Value* LLVMBackend::callRuntimeFunc(const std::string& name,
                                            const std::vector<llvm::Value*>& args) {
    auto* i8ptr = llvm::PointerType::get(llvm::Type::getInt8Ty(*m_context), 0);

    // Determine function type based on name
    llvm::FunctionType* fn_type = nullptr;

    if (name == "angara_create_nil") {
        fn_type = llvm::FunctionType::get(m_angara_obj_type, false);
    } else if (name == "angara_create_bool") {
        fn_type = llvm::FunctionType::get(m_angara_obj_type, {llvm::Type::getInt1Ty(*m_context)}, false);
    } else if (name == "angara_create_i64") {
        fn_type = llvm::FunctionType::get(m_angara_obj_type, {llvm::Type::getInt64Ty(*m_context)}, false);
    } else if (name == "angara_create_f64") {
        fn_type = llvm::FunctionType::get(m_angara_obj_type, {llvm::Type::getDoubleTy(*m_context)}, false);
    } else if (name == "angara_string_from_c" || name == "angara_throw_error") {
        fn_type = llvm::FunctionType::get(
            name == "angara_throw_error" ? llvm::Type::getVoidTy(*m_context) : m_angara_obj_type,
            {i8ptr}, false);
    } else if (name == "angara_equals" || name == "angara_string_concat" ||
               name == "angara_len" || name == "angara_deep_clone" ||
               name == "angara_to_string" || name == "angara_to_i64" ||
               name == "angara_to_f64" || name == "angara_to_bool" ||
               name == "angara_typeof" || name == "angara_thread_join") {
        fn_type = llvm::FunctionType::get(m_angara_obj_type, {m_angara_obj_type}, false);
    } else if (name == "angara_is_truthy") {
        fn_type = llvm::FunctionType::get(llvm::Type::getInt1Ty(*m_context), {m_angara_obj_type}, false);
    } else if (name == "angara_list_push" || name == "angara_mutex_lock" ||
               name == "angara_mutex_unlock") {
        fn_type = llvm::FunctionType::get(llvm::Type::getVoidTy(*m_context),
                                           {m_angara_obj_type, m_angara_obj_type}, false);
    } else if (name == "angara_list_get" || name == "angara_list_remove_at" ||
               name == "angara_list_remove" || name == "angara_record_remove") {
        fn_type = llvm::FunctionType::get(m_angara_obj_type, {m_angara_obj_type, m_angara_obj_type}, false);
    } else if (name == "angara_list_new_with_elements") {
        fn_type = llvm::FunctionType::get(m_angara_obj_type,
            {llvm::Type::getInt64Ty(*m_context), llvm::PointerType::get(m_angara_obj_type, 0)}, false);
    } else if (name == "angara_list_new" || name == "angara_record_new" ||
               name == "angara_mutex_new") {
        fn_type = llvm::FunctionType::get(m_angara_obj_type, false);
    } else if (name == "angara_call") {
        fn_type = llvm::FunctionType::get(m_angara_obj_type,
            {m_angara_obj_type, llvm::Type::getInt32Ty(*m_context),
             llvm::PointerType::get(m_angara_obj_type, 0)}, false);
    } else if (name == "angara_exception_new") {
        fn_type = llvm::FunctionType::get(m_angara_obj_type, {m_angara_obj_type}, false);
    } else if (name == "angara_spawn_thread") {
        fn_type = llvm::FunctionType::get(m_angara_obj_type,
            {m_angara_obj_type, llvm::Type::getInt32Ty(*m_context),
             llvm::PointerType::get(m_angara_obj_type, 0)}, false);
    } else if (name == "angara_record_get") {
        fn_type = llvm::FunctionType::get(m_angara_obj_type,
            {m_angara_obj_type, i8ptr}, false);
    } else if (name == "angara_record_set") {
        fn_type = llvm::FunctionType::get(llvm::Type::getVoidTy(*m_context),
            {m_angara_obj_type, i8ptr, m_angara_obj_type}, false);
    } else if (name == "angara_runtime_init" || name == "angara_runtime_shutdown") {
        fn_type = llvm::FunctionType::get(llvm::Type::getVoidTy(*m_context), false);
    } else {
        // Generic fallback: AngaraObject(AngaraObject, ...)
        std::vector<llvm::Type*> param_types(args.size(), m_angara_obj_type);
        fn_type = llvm::FunctionType::get(m_angara_obj_type, param_types, false);
    }

    auto* fn = getOrDeclareRuntimeFunc(name, fn_type);
    return m_builder->CreateCall(fn, args, "rt_" + name);
}

llvm::Function* LLVMBackend::getOrDeclareRuntimeFunc(const std::string& name,
                                                       llvm::FunctionType* fn_type) {
    auto it = m_runtime_funcs.find(name);
    if (it != m_runtime_funcs.end()) return it->second;

    auto* fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage, name, m_module.get());
    m_runtime_funcs[name] = fn;
    return fn;
}

void LLVMBackend::declareRuntimeFunctions() {
    auto* i8ptr = llvm::PointerType::get(llvm::Type::getInt8Ty(*m_context), 0);

    // Pre-declare all commonly used runtime functions
    getOrDeclareRuntimeFunc("angara_create_nil",
        llvm::FunctionType::get(m_angara_obj_type, false));
    getOrDeclareRuntimeFunc("angara_create_bool",
        llvm::FunctionType::get(m_angara_obj_type, {llvm::Type::getInt1Ty(*m_context)}, false));
    getOrDeclareRuntimeFunc("angara_create_i64",
        llvm::FunctionType::get(m_angara_obj_type, {llvm::Type::getInt64Ty(*m_context)}, false));
    getOrDeclareRuntimeFunc("angara_create_f64",
        llvm::FunctionType::get(m_angara_obj_type, {llvm::Type::getDoubleTy(*m_context)}, false));
    getOrDeclareRuntimeFunc("angara_string_from_c",
        llvm::FunctionType::get(m_angara_obj_type, {i8ptr}, false));
    getOrDeclareRuntimeFunc("angara_equals",
        llvm::FunctionType::get(m_angara_obj_type, {m_angara_obj_type, m_angara_obj_type}, false));
    getOrDeclareRuntimeFunc("angara_is_truthy",
        llvm::FunctionType::get(llvm::Type::getInt1Ty(*m_context), {m_angara_obj_type}, false));
    getOrDeclareRuntimeFunc("angara_string_concat",
        llvm::FunctionType::get(m_angara_obj_type, {m_angara_obj_type, m_angara_obj_type}, false));
    getOrDeclareRuntimeFunc("angara_len",
        llvm::FunctionType::get(m_angara_obj_type, {m_angara_obj_type}, false));
    getOrDeclareRuntimeFunc("angara_deep_clone",
        llvm::FunctionType::get(m_angara_obj_type, {m_angara_obj_type}, false));
    getOrDeclareRuntimeFunc("angara_to_string",
        llvm::FunctionType::get(m_angara_obj_type, {m_angara_obj_type}, false));
    getOrDeclareRuntimeFunc("angara_to_i64",
        llvm::FunctionType::get(m_angara_obj_type, {m_angara_obj_type}, false));
    getOrDeclareRuntimeFunc("angara_to_f64",
        llvm::FunctionType::get(m_angara_obj_type, {m_angara_obj_type}, false));
    getOrDeclareRuntimeFunc("angara_to_bool",
        llvm::FunctionType::get(m_angara_obj_type, {m_angara_obj_type}, false));
    getOrDeclareRuntimeFunc("angara_typeof",
        llvm::FunctionType::get(m_angara_obj_type, {m_angara_obj_type}, false));
    getOrDeclareRuntimeFunc("angara_call",
        llvm::FunctionType::get(m_angara_obj_type,
            {m_angara_obj_type, llvm::Type::getInt32Ty(*m_context),
             llvm::PointerType::get(m_angara_obj_type, 0)}, false));
    getOrDeclareRuntimeFunc("angara_runtime_init",
        llvm::FunctionType::get(llvm::Type::getVoidTy(*m_context), false));
    getOrDeclareRuntimeFunc("angara_runtime_shutdown",
        llvm::FunctionType::get(llvm::Type::getVoidTy(*m_context), false));
    getOrDeclareRuntimeFunc("angara_throw_error",
        llvm::FunctionType::get(llvm::Type::getVoidTy(*m_context), {i8ptr}, false));
}

// --- Variable Management ---

llvm::AllocaInst* LLVMBackend::createAlloca(llvm::Function* fn, const std::string& name) {
    llvm::IRBuilder<> tmp_b(&fn->getEntryBlock(), fn->getEntryBlock().begin());
    return tmp_b.CreateAlloca(m_angara_obj_type, nullptr, name);
}

llvm::Value* LLVMBackend::loadVariable(const std::string& name) {
    auto it = m_named_values.find(name);
    if (it != m_named_values.end()) {
        return m_builder->CreateLoad(m_angara_obj_type, it->second, name + "_val");
    }
    // Check globals
    auto git = m_globals.find(name);
    if (git != m_globals.end()) {
        return m_builder->CreateLoad(m_angara_obj_type, git->second, name + "_val");
    }
    m_had_error = true;
    return createAngaraNil();
}

void LLVMBackend::storeVariable(const std::string& name, llvm::Value* val) {
    auto it = m_named_values.find(name);
    if (it != m_named_values.end()) {
        m_builder->CreateStore(val, it->second);
        return;
    }
    auto git = m_globals.find(name);
    if (git != m_globals.end()) {
        m_builder->CreateStore(val, git->second);
        return;
    }
    m_had_error = true;
}

// --- Main Generate Entry Point ---

bool LLVMBackend::generate(const std::vector<std::shared_ptr<Stmt>>& statements,
                            const std::shared_ptr<ModuleType>& module_type,
                            std::vector<std::string>& all_module_names) {
    if (m_had_error) return false;

    m_module_name = module_type->name;
    m_module->setModuleIdentifier(m_module_name);

    // 1. Declare runtime functions
    declareRuntimeFunctions();

    // 2. Process top-level declarations (structs, globals, functions)
    codegenTopLevelDecls(statements);

    // 3. Generate main function if present
    auto main_symbol = m_type_checker.m_symbols.resolve("main");
    if (main_symbol) {
        codegenMainFunction(statements, m_module_name, all_module_names);
    }

    // 4. Verify the module
    std::string verify_err;
    llvm::raw_string_ostream verify_stream(verify_err);
    if (llvm::verifyModule(*m_module, &verify_stream)) {
        std::fprintf(stderr, "LLVM module verification failed: %s\n", verify_err.c_str());
        m_had_error = true;
        return false;
    }

    // 5. Write LLVM IR to file (for debugging)
    m_ir_file_path = m_module_name + ".ll";
    {
        std::error_code ec;
        llvm::raw_fd_ostream ir_out(m_ir_file_path, ec, llvm::sys::fs::OF_Text);
        if (!ec) {
            m_module->print(ir_out, nullptr);
        }
    }

    // 6. Emit object file
    m_object_file_path = m_module_name + ".o";
    std::string error;
    const llvm::Target* target = llvm::TargetRegistry::lookupTarget(m_target_triple, error);
    if (!target) {
        std::fprintf(stderr, "LLVM target not found: %s\n", error.c_str());
        return false;
    }

    llvm::TargetOptions opt;
    auto* target_machine = target->createTargetMachine(
        m_target_triple, "generic", "", opt, llvm::Reloc::PIC_);
    if (!target_machine) {
        std::fprintf(stderr, "Failed to create LLVM target machine\n");
        return false;
    }

    std::error_code ec;
    llvm::raw_fd_ostream obj_out(m_object_file_path, ec, llvm::sys::fs::OF_None);
    if (ec) {
        std::fprintf(stderr, "Cannot open output file: %s\n", m_object_file_path.c_str());
        return false;
    }

    llvm::legacy::PassManager pass;
    if (target_machine->addPassesToEmitFile(pass, obj_out, nullptr,
                                             llvm::CodeGenFileType::ObjectFile)) {
        std::fprintf(stderr, "Target machine cannot emit object file\n");
        return false;
    }

    pass.run(*m_module);
    obj_out.flush();
    delete target_machine;

    return !m_had_error;
}

} // namespace angara