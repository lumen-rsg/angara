#include "LLVMBackend.h"
#include "RuntimeBuilder.h"
#include <llvm/IR/Verifier.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/IR/DIBuilder.h>
#include <iostream>
#include <filesystem>

namespace angara {

LLVMBackend::~LLVMBackend() = default;

LLVMBackend::LLVMBackend(TypeChecker& tc, ErrorHandler& eh, const std::string& target_triple, bool freestanding, bool dump_ir, bool debug, bool emit_llvm)
    : m_type_checker(tc), m_errorHandler(eh), m_freestanding(freestanding), m_dump_ir(dump_ir), m_debug(debug), m_emit_llvm(emit_llvm) {
    ctx = std::make_unique<llvm::LLVMContext>();
    mod = std::make_unique<llvm::Module>("angara_module", *ctx);
    builder = std::make_unique<llvm::IRBuilder<>>(*ctx);
    std::string ttStr = target_triple.empty()
        ? llvm::sys::getDefaultTargetTriple()
        : target_triple;
    targetTriple = llvm::Triple(llvm::StringRef(ttStr));
    mod->setTargetTriple(targetTriple);
    std::string te;
    if (auto* t = llvm::TargetRegistry::lookupTarget(llvm::Triple(targetTriple.str()), te)) {
        llvm::TargetOptions opt;
        // RT-6: PIC relocation model + Small code model — must match the emitter
        // (below) so the DataLayout and emitted code agree.
        if (auto tm = std::unique_ptr<llvm::TargetMachine>(t->createTargetMachine(targetTriple,"generic","",opt,llvm::Reloc::PIC_,llvm::CodeModel::Small)))
            mod->setDataLayout(tm->createDataLayout());
    }

    // Set up DWARF debug info in debug mode
    if (m_debug) {
        mod->addModuleFlag(llvm::Module::Warning, "Debug Info Version", llvm::DEBUG_METADATA_VERSION);
#ifdef __APPLE__
        mod->addModuleFlag(llvm::Module::Warning, "Dwarf Version", 2);
#else
        mod->addModuleFlag(llvm::Module::Warning, "Dwarf Version", 5);
#endif
        auto diBuilder = std::make_unique<llvm::DIBuilder>(*mod);
        auto diFile = diBuilder->createFile("angara", ".");
        auto diCU = diBuilder->createCompileUnit(llvm::dwarf::DW_LANG_C, diFile, "angc", false, "", 0);
        m_di_builder = std::move(diBuilder);
        m_di_file = diFile;
        m_di_cu = diCU;
    }

    rt = std::make_unique<RuntimeBuilder>(*ctx, *mod, *builder, m_freestanding);
    rt->generateRuntime();
    objType = rt->getAngaraObjType();
}

llvm::DIFile* LLVMBackend::getOrCreateDIFile(const std::string& filename) {
    if (!m_di_builder) return nullptr;
    // Extract just the filename and directory
    std::string dir = ".";
    std::string name = filename;
    auto slash = name.rfind('/');
    if (slash != std::string::npos) {
        dir = name.substr(0, slash);
        name = name.substr(slash + 1);
    }
    return m_di_builder->createFile(name, dir);
}

void LLVMBackend::setDebugLoc(const Token& tok) {
    if (!m_debug || !m_di_scope) return;
    builder->SetCurrentDebugLocation(
        llvm::DILocation::get(*ctx, tok.line, tok.column, m_di_scope));
}

void LLVMBackend::setDebugLoc(int line, int col) {
    if (!m_debug || !m_di_scope) return;
    builder->SetCurrentDebugLocation(
        llvm::DILocation::get(*ctx, line, col, m_di_scope));
}

std::pair<std::unique_ptr<llvm::Module>, std::unique_ptr<llvm::LLVMContext>>
LLVMBackend::generateIR(const std::vector<std::shared_ptr<Stmt>>& stmts,
                        const std::shared_ptr<ModuleType>& moduleType,
                        std::vector<std::string>& allMods) {
    moduleName = moduleType ? moduleType->name : "main";
    createStrlitInitFn();
    codegenTopLevelDecls(stmts);
    bool has_user_main = false;
    for (const auto& stmt : stmts) {
        auto func = std::dynamic_pointer_cast<const FuncStmt>(stmt);
        if (func && func->name.lexeme == "main") { has_user_main = true; break; }
    }
    if (has_user_main) {
        codegenMainFunction(stmts, moduleName, allMods);
    }
    return {std::move(mod), std::move(ctx)};
}

bool LLVMBackend::generate(const std::vector<std::shared_ptr<Stmt>>& stmts,
    const std::shared_ptr<ModuleType>& moduleType, std::vector<std::string>& allMods) {
    moduleName = moduleType ? moduleType->name : "main";
    createStrlitInitFn();
    codegenTopLevelDecls(stmts);
    bool has_user_main = false;
    for (const auto& stmt : stmts) {
        auto func = std::dynamic_pointer_cast<const FuncStmt>(stmt);
        if (func && func->name.lexeme == "main") { has_user_main = true; break; }
    }
    if (has_user_main) {
        codegenMainFunction(stmts, moduleName, allMods);
    }
    std::string base = m_output_dir.empty()
        ? "ang_" + moduleName
        : m_output_dir + "/ang_" + moduleName;

    // TOOL-2: ensure the output directory exists before writing files
    if (!m_output_dir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(m_output_dir, ec);
    }

    if (m_dump_ir) {
        std::error_code ec;
        llvm::raw_fd_ostream ir(base+".ll", ec, llvm::sys::fs::OF_Text);
        if (!ec) { mod->print(ir,nullptr); ir.close(); }
        irPath = base+".ll";
    }
    if (m_emit_llvm) {
        mod->print(llvm::outs(), nullptr);
    }
    std::string ve; llvm::raw_string_ostream es(ve);
    if (llvm::verifyModule(*mod, &es)) { std::cerr<<"Verify: "<<ve<<"\n"; return false; }
    std::error_code ec;
    std::string le; auto* tgt = llvm::TargetRegistry::lookupTarget(llvm::Triple(targetTriple.str()),le);
    if (!tgt) { std::cerr<<"No target: "<<le<<"\n"; return false; }
    llvm::TargetOptions opt;
    auto tm = std::unique_ptr<llvm::TargetMachine>(
        tgt->createTargetMachine(targetTriple, "generic", "", opt,
                                 llvm::Reloc::PIC_, llvm::CodeModel::Small));
    if (!tm) { std::cerr<<"No TM\n"; return false; }

    {
        llvm::LoopAnalysisManager lam;
        llvm::FunctionAnalysisManager fam;
        llvm::CGSCCAnalysisManager cgam;
        llvm::ModuleAnalysisManager mam;
        llvm::PassBuilder pb(tm.get());
        pb.registerModuleAnalyses(mam);
        pb.registerCGSCCAnalyses(cgam);
        pb.registerFunctionAnalyses(fam);
        pb.registerLoopAnalyses(lam);
        pb.crossRegisterProxies(lam, fam, cgam, mam);
        llvm::ModulePassManager mpm = pb.buildPerModuleDefaultPipeline(
            m_debug ? llvm::OptimizationLevel::O0 : llvm::OptimizationLevel::O2);
        mpm.run(*mod, mam);
    }

    // Finalize debug info before object emission
    if (m_debug && m_di_builder) {
        m_di_builder->finalize();
    }

    {
        llvm::legacy::PassManager pm;
        llvm::raw_fd_ostream dest(base+".o",ec,llvm::sys::fs::OF_None);
        if (ec) { std::cerr<<"Open err: "<<ec.message()<<"\n"; return false; }
        if (tm->addPassesToEmitFile(pm,dest,nullptr,llvm::CodeGenFileType::ObjectFile)) { std::cerr<<"Emit err\n"; return false; }
        pm.run(*mod); dest.flush(); dest.close();
    }
    objPath = base+".o";
    return true;
}

llvm::Value* LLVMBackend::makeNil() {
    llvm::Value* r = llvm::UndefValue::get(objType);
    r = builder->CreateInsertValue(r, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), TAG_NIL), {0});
    r = builder->CreateInsertValue(r, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), 0), {1});
    return r;
}
llvm::Value* LLVMBackend::makeBool(bool v) {
    llvm::Value* r = llvm::UndefValue::get(objType);
    r = builder->CreateInsertValue(r, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), TAG_BOOL), {0});
    r = builder->CreateInsertValue(r, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), v?1:0), {1});
    return r;
}
llvm::Value* LLVMBackend::makeBool(llvm::Value* v) {
    llvm::Value* r = llvm::UndefValue::get(objType);
    r = builder->CreateInsertValue(r, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), TAG_BOOL), {0});
    r = builder->CreateInsertValue(r, builder->CreateZExt(v, llvm::Type::getInt64Ty(*ctx)), {1});
    return r;
}
llvm::Value* LLVMBackend::makeI64(int64_t v) {
    llvm::Value* r = llvm::UndefValue::get(objType);
    r = builder->CreateInsertValue(r, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), TAG_I64), {0});
    r = builder->CreateInsertValue(r, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), v), {1});
    return r;
}
llvm::Value* LLVMBackend::makeI64(llvm::Value* v) {
    llvm::Value* r = llvm::UndefValue::get(objType);
    r = builder->CreateInsertValue(r, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), TAG_I64), {0});
    r = builder->CreateInsertValue(r, v, {1});
    return r;
}
llvm::Value* LLVMBackend::makeF64(double v) {
    llvm::Value* r = llvm::UndefValue::get(objType);
    r = builder->CreateInsertValue(r, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), TAG_F64), {0});
    r = builder->CreateInsertValue(r, builder->CreateBitCast(llvm::ConstantFP::get(llvm::Type::getDoubleTy(*ctx),v), llvm::Type::getInt64Ty(*ctx)), {1});
    return r;
}
llvm::Value* LLVMBackend::makeF64(llvm::Value* v) {
    llvm::Value* r = llvm::UndefValue::get(objType);
    r = builder->CreateInsertValue(r, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), TAG_F64), {0});
    r = builder->CreateInsertValue(r, builder->CreateBitCast(v, llvm::Type::getInt64Ty(*ctx)), {1});
    return r;
}

void LLVMBackend::createStrlitInitFn() {
    if (m_strlit_init_fn) return;
    auto* fn_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(*ctx), false);
    // External linkage + module-unique name so the main module can call
    // each imported module's init function. Without this, string literals
    // in .an source modules stay zero-initialized (nil) at runtime.
    std::string init_name = "__ang_strlit_init_" + moduleName;
    m_strlit_init_fn = llvm::Function::Create(fn_type,
        llvm::Function::ExternalLinkage,
        init_name, mod.get());
    auto* entry_bb = llvm::BasicBlock::Create(*ctx, "entry", m_strlit_init_fn);
    llvm::IRBuilder<>(entry_bb).CreateRetVoid();
}

llvm::Value* LLVMBackend::makeStr(const std::string& s) {
    // Intern string literals: each unique literal is allocated once per module
    // and reused across all references. Under GC, the global is a permanent root
    // so the string lives for the program's lifetime — no incref needed.
    auto it = m_string_literal_cache.find(s);
    if (it != m_string_literal_cache.end()) {
        auto* cached = builder->CreateLoad(objType, it->second, "strlit");
        return cached;
    }

    // First occurrence: create a global variable and initialize it in the
    // centralized __ang_strlit_init function.  All literal init is collected
    // there and called once from main before any user code runs.  This avoids
    // a load-before-init bug where a literal whose init code was placed in
    // whichever function first referenced it during compilation gets loaded
    // by a different function that executes earlier at runtime.
    auto* gsptr = builder->CreateGlobalString(s);

    auto* global = new llvm::GlobalVariable(
        *mod, objType, false,
        llvm::GlobalValue::InternalLinkage,
        llvm::ConstantAggregateZero::get(objType),
        "__ang_strlit_" + std::to_string(m_string_literal_cache.size()));
    global->setDSOLocal(true);

    // m_strlit_init_fn is created eagerly in generate()/generateIR().
    // Emit init calls at the top of the init function (before its ret void).
    auto savedIP = builder->saveIP();
    auto& init_entry = m_strlit_init_fn->getEntryBlock();
    builder->SetInsertPoint(&init_entry, init_entry.getFirstInsertionPt());
    auto* new_str = callRtByName("__ang_string_from_c", {gsptr});
    // Pin the string literal so the GC never collects it.
    // String literals are stored in globals, not root frames, so without
    // pinning they'd be invisible to the collector and freed as unreachable.
    callRtByName("__ang_gc_pin", {new_str});
    // Clear is_unique so the __ang_string_concat in-place fast path never
    // fires on a shared global literal.  Without this, the first concat
    // that uses a literal as its left operand mutates the interned buffer
    // in place, corrupting every subsequent reference to that literal.
    callRtByName("__ang_gc_clear_unique", {new_str});
    builder->CreateStore(new_str, global);
    builder->restoreIP(savedIP);

    m_string_literal_cache[s] = global;

    // Load from the global — no incref under GC
    auto* loaded = builder->CreateLoad(objType, global, "strlit");
    return loaded;
}

llvm::Value* LLVMBackend::getI64(llvm::Value* o) { return builder->CreateExtractValue(o,{1}); }
llvm::Value* LLVMBackend::getF64(llvm::Value* o) { return builder->CreateBitCast(builder->CreateExtractValue(o,{1}), llvm::Type::getDoubleTy(*ctx)); }
llvm::Value* LLVMBackend::getBool(llvm::Value* o) { return builder->CreateTrunc(builder->CreateExtractValue(o,{1}), llvm::Type::getInt1Ty(*ctx)); }
llvm::Value* LLVMBackend::getTag(llvm::Value* o) { return builder->CreateExtractValue(o,{0}); }

llvm::Value* LLVMBackend::isTruthy(llvm::Value* o) {
    auto* tag = getTag(o);
    auto* is_obj = builder->CreateICmpEQ(tag, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), TAG_OBJ));
    auto* payload = builder->CreateExtractValue(o,{1});
    auto* nonzero = builder->CreateICmpNE(payload, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), 0));
    return builder->CreateSelect(is_obj, llvm::ConstantInt::getTrue(*ctx), nonzero);
}

llvm::Value* LLVMBackend::callRt(llvm::FunctionCallee c, const std::vector<llvm::Value*>& a) {
    return builder->CreateCall(c, a);
}

llvm::Value* LLVMBackend::callRtByName(const std::string& name, const std::vector<llvm::Value*>& a) {
    auto* fn = mod->getFunction(name);
    if (fn) return builder->CreateCall(fn, a);
    return makeNil();
}

llvm::AllocaInst* LLVMBackend::allocLocal(llvm::Function* fn, const std::string& name) {
    llvm::IRBuilder<> tmp(&fn->getEntryBlock(), fn->getEntryBlock().begin());
    auto* alloca = tmp.CreateAlloca(objType, nullptr, name);

    return alloca;
}

llvm::AllocaInst* LLVMBackend::allocLocal(llvm::Function* fn, const std::string& name,
                                            const std::shared_ptr<Type>& type) {
    if (type && isUnboxableType(type)) {
        // Raw primitive: allocate the native LLVM type, skip GC root registration
        auto kind = localKindForType(type);
        llvm::IRBuilder<> tmp(&fn->getEntryBlock(), fn->getEntryBlock().getFirstInsertionPt());
        return tmp.CreateAlloca(llvmTypeForLocalKind(kind), nullptr, name);
    }
    // Boxed or unknown: use the original allocLocal (objType + GC root)
    return allocLocal(fn, name);
}

// RT-2: returns a cached DWARF DIType for a LocalKind, so gdb/lldb can show a
// variable's raw storage type (i64, double, bool, pointer, or the {i32,i64}
// AngaraObject struct for boxed values).
llvm::DIType* LLVMBackend::diTypeForLocalKind(LocalKind kind) {
    if (!m_debug || !m_di_builder) return nullptr;
    auto it = m_di_types.find(static_cast<int>(kind));
    if (it != m_di_types.end()) return it->second;

    llvm::DIType* diType = nullptr;
    switch (kind) {
        case LocalKind::RAW_I64:
            diType = m_di_builder->createBasicType("i64", 64, llvm::dwarf::DW_ATE_signed);
            break;
        case LocalKind::RAW_F64:
            diType = m_di_builder->createBasicType("f64", 64, llvm::dwarf::DW_ATE_float);
            break;
        case LocalKind::RAW_I1:
            diType = m_di_builder->createBasicType("bool", 8, llvm::dwarf::DW_ATE_boolean);
            break;
        case LocalKind::RAW_PTR:
            diType = m_di_builder->createPointerType(nullptr, 64);
            break;
        case LocalKind::BOXED: {
            // Show the {i32 tag, i64 payload} struct so the tag is visible.
            auto* tag_t = m_di_builder->createBasicType("tag", 32, llvm::dwarf::DW_ATE_unsigned);
            auto* payload_t = m_di_builder->createBasicType("payload", 64, llvm::dwarf::DW_ATE_unsigned);
            auto* tag_member = m_di_builder->createMemberType(
                m_di_cu, "tag", m_di_file, 0, 32, 32, 0, llvm::DINode::FlagZero, tag_t);
            auto* payload_member = m_di_builder->createMemberType(
                m_di_cu, "payload", m_di_file, 0, 64, 64, 32, llvm::DINode::FlagZero, payload_t);
            diType = m_di_builder->createStructType(
                m_di_cu, "AngaraObject", m_di_file, 0, 128, 64,
                llvm::DINode::FlagZero, nullptr,
                m_di_builder->getOrCreateArray({tag_member, payload_member}));
            break;
        }
    }
    m_di_types[static_cast<int>(kind)] = diType;
    return diType;
}

// RT-2: emit llvm.dbg.declare for a local variable at the current insert point,
// making it inspectable in gdb/lldb.
void LLVMBackend::emitDbgDeclare(llvm::AllocaInst* alloca, const std::string& name,
                                  int line, int col, LocalKind kind) {
    if (!m_debug || !m_di_scope || !m_di_builder || !alloca) return;
    auto* diType = diTypeForLocalKind(kind);
    if (!diType) return;
    auto* localVar = m_di_builder->createAutoVariable(
        m_di_scope, name, m_di_file, line, diType, true,
        llvm::DINode::FlagZero);
    m_di_builder->insertDeclare(
        alloca, localVar, m_di_builder->createExpression(),
        llvm::DILocation::get(*ctx, line, col, m_di_scope),
        builder->GetInsertBlock());
}

void LLVMBackend::emitGcPushFrame(llvm::Function* fn, int /*slot_count*/) {
    // v5: no GC frame. Only the BUG-5 exception-chain snapshot remains.
    if (auto* chain_gv = rt->getExceptionChain()) {
        auto* ptr_ty = llvm::PointerType::get(*ctx, 0);
        llvm::IRBuilder<> tmp(&fn->getEntryBlock(), fn->getEntryBlock().begin());
        m_exc_chain_save = tmp.CreateAlloca(ptr_ty, nullptr, "exc_chain_save");
        tmp.CreateStore(tmp.CreateLoad(ptr_ty, chain_gv, "entry_chain"), m_exc_chain_save);
    }
}

void LLVMBackend::emitGcPopFrame() {
    // v5: no GC frame pop. Only the BUG-5 exception-chain restore remains.
    if (m_exc_chain_save) {
        auto* ptr_ty = llvm::PointerType::get(*ctx, 0);
        builder->CreateStore(builder->CreateLoad(ptr_ty, m_exc_chain_save, "saved_chain"),
                             rt->getExceptionChain());
    }
}

llvm::Value* LLVMBackend::emitGcThreadSetup() {
    auto* state_type = rt->getGcThreadStateType();
    auto& dl = mod->getDataLayout();
    uint64_t state_size_val = dl.getTypeAllocSize(state_type);
    auto* state_size = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), state_size_val);

    // malloc a GcThreadState
    auto* state_raw = callRtByName("malloc", {state_size});
    auto* state_ptr = builder->CreateBitCast(state_raw, llvm::PointerType::get(*ctx, 0), "gc_state");

    // Zero-init
    callRtByName("memset", {state_ptr, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), 0), state_size});

    // Register with GC
    callRtByName("__ang_gc_thread_register", {state_ptr});

    return state_ptr;
}

void LLVMBackend::emitGcTeardown(llvm::Value* state_ptr) {
    callRtByName("__ang_gc_thread_unregister", {state_ptr});
    callRtByName("free", {state_ptr});
}

llvm::Value* LLVMBackend::loadVar(const std::string& n) {
    if (auto it=namedVals.find(n); it!=namedVals.end()) {
        auto kit = namedKinds.find(n);
        if (kit != namedKinds.end() && kit->second != LocalKind::BOXED) {
            auto* raw = builder->CreateLoad(llvmTypeForLocalKind(kit->second), it->second, n);
            return boxRaw(raw, kit->second);
        }
        return builder->CreateLoad(objType, it->second, n);
    }
    if (auto it=globals.find(n); it!=globals.end()) return builder->CreateLoad(objType, it->second, n);
    if (auto it=globals.find("g_"+n); it!=globals.end()) return builder->CreateLoad(objType, it->second, n);
    return makeNil();
}
void LLVMBackend::storeVar(const std::string& n, llvm::Value* v) {
    if (auto it=namedVals.find(n); it!=namedVals.end()) {
        auto kit = namedKinds.find(n);
        if (kit != namedKinds.end() && kit->second != LocalKind::BOXED) {
            builder->CreateStore(unboxToRaw(v, kit->second), it->second);
            return;
        }
        builder->CreateStore(v, it->second);
        return;
    }
    if (auto it=globals.find(n); it!=globals.end()) { builder->CreateStore(v,it->second); return; }
    if (auto it=globals.find("g_"+n); it!=globals.end()) { builder->CreateStore(v,it->second); return; }
}

std::string LLVMBackend::mangle(const std::string& m, const std::string& n) { return "__ang_"+m+"_"+sanitize(n); }
std::string LLVMBackend::mangleMethod(const std::string& c, const std::string& m) { return "__ang_"+sanitize(c)+"_"+sanitize(m); }

// LANG-13: resolve a method name to its mangled LLVM function for a given type.
// Walks the class chain via methodLookup, falling back to unqualified lookup.
std::string LLVMBackend::resolveMethodForType(const std::shared_ptr<Type>& type,
                                               const std::string& method_name) {
    if (!type) return "";
    std::shared_ptr<ClassType> cls;
    if (type->kind == TypeKind::INSTANCE) {
        auto inst = std::dynamic_pointer_cast<InstanceType>(type);
        if (inst) cls = inst->class_type;
    } else if (type->kind == TypeKind::CLASS) {
        cls = std::dynamic_pointer_cast<ClassType>(type);
    }
    if (!cls) return "";
    // Walk the class chain looking for a qualified key: ClassName.methodName
    auto current = cls;
    while (current) {
        auto qit = methodLookup.find(current->name + "." + method_name);
        if (qit != methodLookup.end()) return qit->second;
        current = current->superclass;
    }
    // Fallback to unqualified lookup (method defined in any class)
    auto mit = methodLookup.find(method_name);
    if (mit != methodLookup.end()) return mit->second;
    return "";
}
std::string LLVMBackend::sanitize(const std::string& n) { std::string r; for(char c:n) r+=(std::isalnum(c)||c=='_')?c:'_'; return r; }

bool LLVMBackend::isSizedIntType(const std::shared_ptr<Type>& type) {
    return isInteger(type);
}

bool LLVMBackend::isUnsignedIntType(const std::shared_ptr<Type>& type) {
    return isUnsignedInteger(type);
}

int LLVMBackend::getIntBitWidth(const std::shared_ptr<Type>& type) {
    if (!type || type->kind != TypeKind::PRIMITIVE) return 64;
    const auto& name = type->toString();
    if (name == "i8"  || name == "u8")  return 8;
    if (name == "i16" || name == "u16") return 16;
    if (name == "i32" || name == "u32") return 32;
    return 64;
}

llvm::Value* LLVMBackend::truncateForType(llvm::Value* val, const std::shared_ptr<Type>& type) {
    if (!isSizedIntType(type)) return val;
    int bits = getIntBitWidth(type);
    if (bits >= 64) return val;

    llvm::Value* payload = getI64(val);
    llvm::Type* truncTy = llvm::IntegerType::get(*ctx, bits);
    llvm::Value* truncated = builder->CreateTrunc(payload, truncTy, "narrow");
    // Sign-extend for signed types, zero-extend for unsigned types
    llvm::Value* extended = isUnsignedInteger(type)
        ? builder->CreateZExt(truncated, llvm::Type::getInt64Ty(*ctx), "masked")
        : builder->CreateSExt(truncated, llvm::Type::getInt64Ty(*ctx), "sign_ext");

    return makeI64(extended);
}

// --- Unboxed primitive support ---

bool LLVMBackend::isUnboxableType(const std::shared_ptr<Type>& type) {
    if (!type || type->kind != TypeKind::PRIMITIVE) return false;
    const auto& n = type->toString();
    // String is a heap type — never unbox
    if (n == "string") return false;
    // All numeric and bool primitives are unboxable
    return isInteger(type) || isFloat(type) || n == "bool";
}

LLVMBackend::LocalKind LLVMBackend::localKindForType(const std::shared_ptr<Type>& type) {
    if (!type || type->kind != TypeKind::PRIMITIVE) return LocalKind::BOXED;
    const auto& n = type->toString();
    if (n == "bool") return LocalKind::RAW_I1;
    if (isFloat(type)) return LocalKind::RAW_F64;
    if (isInteger(type)) return LocalKind::RAW_I64;
    return LocalKind::BOXED;
}

// FFI marshalling: the C-side kind for a foreign-function param/return.
// Like localKindForType but also maps `string` and pointer types to RAW_PTR
// (a C char*/pointer), so `foreign func strlen(s as string) -> i64` gets a
// real C signature instead of passing a boxed AngaraObject to libc.
LLVMBackend::LocalKind LLVMBackend::ffiKindForType(const std::shared_ptr<Type>& type) {
    if (!type) return LocalKind::BOXED;
    if (type->kind == TypeKind::PRIMITIVE) {
        const auto& n = type->toString();
        if (n == "string") return LocalKind::RAW_PTR;   // char*
        if (n == "bool")   return LocalKind::RAW_I1;
        if (isFloat(type)) return LocalKind::RAW_F64;
        if (isInteger(type)) return LocalKind::RAW_I64;
        return LocalKind::BOXED;
    }
    // Pointer types (*T, *void) marshal as a C pointer.
    if (type->kind == TypeKind::POINTER) return LocalKind::RAW_PTR;
    // SIMD-4: unboxed dynamic arrays pass as raw element pointer (e.g., f64[] → double*)
    if (type->kind == TypeKind::RAW_ARRAY) return LocalKind::RAW_PTR;
    return LocalKind::BOXED;
}

// Whether a type can be marshalled directly to C in a foreign-function
// signature (primitives + string + pointers). Aggregate/owned types stay boxed.
bool LLVMBackend::isFFIMarshallable(const std::shared_ptr<Type>& type) {
    if (!type) return false;
    if (type->kind == TypeKind::POINTER) return true;
    // SIMD-4: unboxed dynamic arrays are marshallable as element pointers
    if (type->kind == TypeKind::RAW_ARRAY) return true;
    if (type->kind == TypeKind::PRIMITIVE) {
        const auto& n = type->toString();
        return n == "string" || n == "bool" || isInteger(type) || isFloat(type);
    }
    return false;
}

llvm::Type* LLVMBackend::llvmTypeForLocalKind(LocalKind kind) {
    switch (kind) {
        case LocalKind::RAW_I1:  return llvm::Type::getInt1Ty(*ctx);
        case LocalKind::RAW_I64: return llvm::Type::getInt64Ty(*ctx);
        case LocalKind::RAW_F64: return llvm::Type::getDoubleTy(*ctx);
        case LocalKind::RAW_PTR: return llvm::PointerType::get(*ctx, 0);
        case LocalKind::BOXED:   return objType;
    }
    return objType;
}

llvm::Value* LLVMBackend::boxRaw(llvm::Value* raw, LocalKind kind) {
    switch (kind) {
        case LocalKind::RAW_I1:  return makeBool(raw);
        case LocalKind::RAW_I64: return makeI64(raw);
        case LocalKind::RAW_F64: return makeF64(raw);
        // RAW_PTR: a C pointer returned to Angara is carried as an opaque i64
        // payload (boxed). Marshalling back to a usable Angara value is the
        // caller's responsibility (e.g. via @own string adoption).
        case LocalKind::RAW_PTR: return makeI64(builder->CreatePtrToInt(raw, llvm::Type::getInt64Ty(*ctx)));
        case LocalKind::BOXED:   return raw;
    }
    return raw;
}

llvm::Value* LLVMBackend::unboxToRaw(llvm::Value* objVal, LocalKind kind) {
    switch (kind) {
        case LocalKind::RAW_I1:  return getBool(objVal);
        case LocalKind::RAW_I64: return getI64(objVal);
        case LocalKind::RAW_F64: return getF64(objVal);
        // RAW_PTR: treat the boxed value's i64 payload as a pointer. (For
        // string args, the call site uses marshalAngaraToC instead, which
        // extracts the char* correctly; this is the fallback for stored ptrs.)
        case LocalKind::RAW_PTR: return builder->CreateIntToPtr(getI64(objVal), llvm::PointerType::get(*ctx, 0));
        case LocalKind::BOXED:   return objVal;
    }
    return objVal;
}

llvm::Type* LLVMBackend::resolveCFieldType(const std::shared_ptr<Type>& type) {
    if (!type) return llvm::Type::getInt64Ty(*ctx);

    if (type->kind == TypeKind::PRIMITIVE) {
        const auto& n = type->toString();
        if (n == "bool")   return llvm::Type::getInt1Ty(*ctx);
        if (n == "i8"  || n == "u8")  return llvm::Type::getInt8Ty(*ctx);
        if (n == "i16" || n == "u16") return llvm::Type::getInt16Ty(*ctx);
        if (n == "i32" || n == "u32") return llvm::Type::getInt32Ty(*ctx);
        if (n == "i64" || n == "u64") return llvm::Type::getInt64Ty(*ctx);
        if (n == "f32")    return llvm::Type::getFloatTy(*ctx);
        if (n == "f64")    return llvm::Type::getDoubleTy(*ctx);
        if (n == "string") return llvm::PointerType::get(*ctx, 0);  // char*
    }
    if (type->kind == TypeKind::FIXED_ARRAY) {
        auto arr = std::dynamic_pointer_cast<FixedArrayType>(type);
        return llvm::ArrayType::get(resolveCFieldType(arr->element_type), arr->size);
    }
    // SIMD-4: unboxed dynamic array → pointer to element type (e.g., f64[] → double*)
    if (type->kind == TypeKind::RAW_ARRAY) {
        auto raw_arr = std::dynamic_pointer_cast<RawArrayType>(type);
        return llvm::PointerType::get(resolveCFieldType(raw_arr->element_type), 0);
    }
    if (type->kind == TypeKind::DATA) {
        auto dt = std::dynamic_pointer_cast<DataType>(type);
        if (dt && dt->is_foreign && !dt->is_opaque) {
            // Inline struct type (for struct fields embedded by value)
            auto it = m_foreign_struct_types.find(dt->name);
            if (it != m_foreign_struct_types.end()) {
                return it->second;
            }
        }
        // Opaque or unresolved: void pointer
        return llvm::PointerType::get(*ctx, 0);
    }
    if (type->kind == TypeKind::POINTER) {
        auto ptr_type = std::dynamic_pointer_cast<PointerType>(type);
        // Byval pointer (^Type): return the struct type directly for by-value passing
        if (ptr_type && ptr_type->byval && ptr_type->pointee_type->kind == TypeKind::DATA) {
            auto dt = std::dynamic_pointer_cast<DataType>(ptr_type->pointee_type);
            if (dt && dt->is_foreign && !dt->is_opaque) {
                auto it = m_foreign_struct_types.find(dt->name);
                if (it != m_foreign_struct_types.end()) {
                    return it->second;
                }
            }
        }
        // All pointer types map to LLVM opaque pointer (regardless of depth)
        return llvm::PointerType::get(*ctx, 0);
    }
    if (type->kind == TypeKind::VOID) {
        return llvm::Type::getVoidTy(*ctx);
    }
    if (type->kind == TypeKind::FUNCTION) {
        // C function pointer — opaque ptr
        return llvm::PointerType::get(*ctx, 0);
    }
    return llvm::Type::getInt64Ty(*ctx);
}

llvm::Value* LLVMBackend::marshalAngaraToC(llvm::Value* obj, const std::shared_ptr<Type>& type) {
    if (!type) return getI64(obj);

    if (type->kind == TypeKind::PRIMITIVE) {
        const auto& n = type->toString();
        if (n == "bool")   return getBool(obj);
        if (n == "string") {
            // Extract char* from AngaraString. The boxed payload is a pointer to
            // the AngaraString struct { ObjHeader, i64 len, i64 cap, ptr chars },
            // so chars is at field index 3.
            auto* str_ptr = builder->CreateIntToPtr(getI64(obj), llvm::PointerType::get(*ctx, 0));
            auto* string_type = rt->getStringType();
            auto* chars_ptr = builder->CreateStructGEP(string_type, str_ptr, 3);
            auto* raw = builder->CreateLoad(llvm::PointerType::get(*ctx, 0), chars_ptr);
            auto prim = std::dynamic_pointer_cast<PrimitiveType>(type);
            if (prim && prim->is_owned) {
                // @own param: strdup so C gets its own copy to free
                auto* strdup_fn = mod->getFunction("strdup");
                return builder->CreateCall(strdup_fn, {raw}, "owned_str");
            }
            return raw;
        }
        auto* cty = resolveCFieldType(type);
        if (n == "f64")    return getF64(obj);
        if (n == "f32")    return builder->CreateFPTrunc(getF64(obj), llvm::Type::getFloatTy(*ctx));
        // Integer types: extract i64 then truncate
        if (cty->getIntegerBitWidth() < 64) {
            return builder->CreateTrunc(getI64(obj), cty);
        }
        return getI64(obj);
    }
    if (type->kind == TypeKind::DATA) {
        auto dt = std::dynamic_pointer_cast<DataType>(type);
        if (dt && dt->is_foreign) {
            // Extract data pointer from NativeInstance
            auto* data_ptr = callRtByName("__ang_api_native_instance_data", {obj});
            if (!dt->is_opaque) {
                // Bitcast to the struct pointer type
                auto it = m_foreign_struct_types.find(dt->name);
                if (it != m_foreign_struct_types.end()) {
                    return builder->CreateBitCast(data_ptr, llvm::PointerType::get(it->second, 0));
                }
            }
            return data_ptr; // opaque: return void*
        }
    }
    if (type->kind == TypeKind::POINTER) {
        auto ptr_type = std::dynamic_pointer_cast<PointerType>(type);
        // Byval struct: extract data pointer from NativeInstance, then load the struct value
        if (ptr_type && ptr_type->byval && ptr_type->pointee_type->kind == TypeKind::DATA) {
            auto dt = std::dynamic_pointer_cast<DataType>(ptr_type->pointee_type);
            if (dt && dt->is_foreign && !dt->is_opaque) {
                auto it = m_foreign_struct_types.find(dt->name);
                if (it != m_foreign_struct_types.end()) {
                    auto* data_ptr = callRtByName("__ang_api_native_instance_data", {obj});
                    auto* struct_ptr = builder->CreateBitCast(data_ptr,
                        llvm::PointerType::get(it->second, 0));
                    return builder->CreateLoad(it->second, struct_ptr);
                }
            }
        }
        // Pointer is stored as NativeInstance — extract the raw data pointer
        return callRtByName("__ang_api_native_instance_data", {obj});
    }
    // SIMD-4: unboxed dynamic array → raw element buffer pointer
    // AngaraRawArray = { ObjHeader, i64 count, i64 capacity, i64 elem_size, ptr buf }
    // The boxed payload is ptrtoint(heap_ptr), so unbox to get the struct pointer,
    // then GEP to field 4 (the element buffer pointer).
    if (type->kind == TypeKind::RAW_ARRAY) {
        auto* arr_ptr = builder->CreateIntToPtr(getI64(obj), llvm::PointerType::get(*ctx, 0));
        auto* buf_ptr = builder->CreateLoad(llvm::PointerType::get(*ctx, 0),
            builder->CreateStructGEP(rt->getRawArrayType(), arr_ptr, 4), "raw_buf");
        return buf_ptr;
    }
    if (type->kind == TypeKind::FUNCTION) {
        // Angara closure → C function pointer via trampoline + context
        auto func_type = std::dynamic_pointer_cast<FunctionType>(type);

        static int trampoline_counter = 0;
        std::string key = "ffi_trampoline_" + std::to_string(trampoline_counter++);

        // Allocate a context struct (just an AngaraObject) on the heap to hold the closure.
        // The trampoline will receive this pointer through the last parameter (userdata convention).
        auto* malloc_fn = mod->getFunction("malloc");
        if (!malloc_fn) {
            auto* malloc_type = llvm::FunctionType::get(llvm::PointerType::get(*ctx, 0),
                {llvm::Type::getInt64Ty(*ctx)}, false);
            malloc_fn = llvm::Function::Create(malloc_type, llvm::Function::ExternalLinkage,
                                               "malloc", mod.get());
        }
        auto* ctx_size = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx),
            mod->getDataLayout().getTypeAllocSize(objType).getFixedValue());
        auto* ctx_mem = builder->CreateCall(malloc_fn, {ctx_size});
        auto* ctx_ptr = builder->CreateBitCast(ctx_mem, llvm::PointerType::get(objType, 0));

        // Store the closure into the context
        builder->CreateStore(obj, ctx_ptr);

        // Expose the raw void* context pointer for the wrapper to pass as userdata
        m_pending_callback_context = ctx_mem;

        // Generate the trampoline. Convention: the LAST parameter is void* userdata
        // pointing to the context struct. All preceding params are the actual C callback args.
        int total_params = func_type->param_types.size();
        int callback_arg_count = total_params - 1; // last param is userdata

        // Build C param types for the trampoline (all params including userdata)
        std::vector<llvm::Type*> tram_param_types;
        for (const auto& pt : func_type->param_types) {
            tram_param_types.push_back(resolveCFieldType(pt));
        }
        auto* c_return_type = resolveCFieldType(func_type->return_type);
        auto* trampoline_type = llvm::FunctionType::get(c_return_type, tram_param_types, false);
        auto* trampoline = llvm::Function::Create(trampoline_type,
            llvm::Function::InternalLinkage, "Angara_" + key, mod.get());

        // Emit the trampoline body
        auto* tram_entry = llvm::BasicBlock::Create(*ctx, "entry", trampoline);
        auto saved_insert_point = builder->saveIP();
        builder->SetInsertPoint(tram_entry);

        // 1. Load the closure from the userdata (last parameter)
        auto* userdata_arg = trampoline->arg_begin() + (total_params - 1);
        auto* ctx_loaded = builder->CreateBitCast(userdata_arg, llvm::PointerType::get(objType, 0));
        auto* closure = builder->CreateLoad(objType, ctx_loaded, "closure");

        // 2. Marshal each C callback arg (all except last) to AngaraObject
        auto* args_array = builder->CreateAlloca(objType,
            llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), std::max(callback_arg_count, 1)),
            "args");
        for (int i = 0; i < callback_arg_count; i++) {
            auto* arg_val = trampoline->arg_begin() + i;
            auto* angara_val = marshalCToAngara(arg_val, func_type->param_types[i]);
            auto* slot = builder->CreateGEP(objType, args_array,
                {llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), i)});
            builder->CreateStore(angara_val, slot);
        }

        // RT-1: wrap the callback invocation in a setjmp/try so a throw lands
        // inside the trampoline (not across C frames). Mirrors cgTry exactly.
        auto* frameType = llvm::StructType::create(*ctx,
            {llvm::ArrayType::get(llvm::Type::getInt8Ty(*ctx), 512),
             llvm::PointerType::get(*ctx, 0)}, "EF");
        auto* frame = builder->CreateAlloca(frameType);
        auto* frame_raw = builder->CreateBitCast(frame, llvm::PointerType::get(*ctx, 0));
        auto* prev_addr = builder->CreateStructGEP(frameType, frame, 1);
        auto* old_chain = builder->CreateLoad(llvm::PointerType::get(*ctx, 0),
            rt->getExceptionChain(), "old_chain");
        builder->CreateStore(old_chain, prev_addr);
        builder->CreateStore(frame_raw, rt->getExceptionChain());

        auto* jmp_buf_ptr = builder->CreateStructGEP(frameType, frame, 0);
        auto* i8_ptr_ty = llvm::PointerType::get(*ctx, 0);
        auto* setjmp_fn = mod->getFunction("setjmp");
        auto* sr = builder->CreateCall(
            llvm::FunctionType::get(llvm::Type::getInt32Ty(*ctx), {i8_ptr_ty}, false),
            setjmp_fn,
            {builder->CreateBitCast(jmp_buf_ptr, i8_ptr_ty)}, "setjmp_result");
        if (auto* ci = llvm::dyn_cast<llvm::CallInst>(sr)) {
            ci->addFnAttr(llvm::Attribute::ReturnsTwice);
        }

        auto* tram = trampoline;
        auto* normalBB = llvm::BasicBlock::Create(*ctx, "cb_normal", tram);
        auto* caughtBB = llvm::BasicBlock::Create(*ctx, "cb_caught", tram);
        builder->CreateCondBr(
            builder->CreateICmpEQ(sr, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), 0)),
            normalBB, caughtBB);

        // Normal path: run the callback, pop the frame, marshal + ret.
        builder->SetInsertPoint(normalBB);
        auto* call_result = callRtByName("__ang_call", {
            closure,
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), callback_arg_count),
            builder->CreateBitCast(args_array, llvm::PointerType::get(*ctx, 0))
        });
        // Pop the exception frame (restore the chain to the outer frame).
        callRtByName("__ang_try_end", {});
        auto* c_result = marshalAngaraToC(call_result, func_type->return_type);
        if (func_type->return_type->kind == TypeKind::VOID) {
            builder->CreateRetVoid();
        } else {
            builder->CreateRet(c_result);
        }

        // Caught-throw path: __ang_throw already unlinked our frame. Translate
        // to a C error return (the @on_throw value, or 0 if unset).
        builder->SetInsertPoint(caughtBB);
        int64_t throw_ret = func_type->on_throw_value.value_or(0);
        if (func_type->return_type->kind == TypeKind::VOID) {
            builder->CreateRetVoid();
        } else {
            builder->CreateRet(llvm::ConstantInt::get(c_return_type, throw_ret, true));
        }

        builder->restoreIP(saved_insert_point);

        // Return the trampoline function pointer
        return trampoline;
    }

    return getI64(obj);
}

llvm::Value* LLVMBackend::marshalCToAngara(llvm::Value* c_val, const std::shared_ptr<Type>& type) {
    if (!type) return makeI64(c_val);

    if (type->kind == TypeKind::PRIMITIVE) {
        const auto& n = type->toString();
        if (n == "bool") return makeBool(c_val);
        if (n == "f64")  return makeF64(c_val);
        if (n == "f32")  return makeF64(builder->CreateFPExt(c_val, llvm::Type::getDoubleTy(*ctx)));
        if (n == "string") {
            // Check if @own — zero-copy adoption vs strdup
            auto prim = std::dynamic_pointer_cast<PrimitiveType>(type);
            if (prim && prim->is_owned) {
                return callRtByName("__ang_string_take_c", {c_val});
            }
            return callRtByName("__ang_string_from_c", {c_val});
        }
        // Integer types: zext for unsigned, sext for signed, then store in i64
        auto* cty = resolveCFieldType(type);
        if (cty->getIntegerBitWidth() < 64) {
            if (isUnsignedInteger(type)) {
                return makeI64(builder->CreateZExt(c_val, llvm::Type::getInt64Ty(*ctx)));
            }
            return makeI64(builder->CreateSExt(c_val, llvm::Type::getInt64Ty(*ctx)));
        }
        return makeI64(c_val);
    }
    if (type->kind == TypeKind::POINTER) {
        auto ptr_type = std::dynamic_pointer_cast<PointerType>(type);
        if (ptr_type && ptr_type->byval && ptr_type->pointee_type->kind == TypeKind::DATA) {
            // Struct returned by value — alloca, store, wrap in NativeInstance
            auto dt = std::dynamic_pointer_cast<DataType>(ptr_type->pointee_type);
            if (dt && dt->is_foreign && !dt->is_opaque) {
                auto it = m_foreign_struct_types.find(dt->name);
                if (it != m_foreign_struct_types.end()) {
                    auto* struct_type = it->second;
                    auto* alloca = builder->CreateAlloca(struct_type);
                    builder->CreateStore(c_val, alloca);
                    auto* void_ptr = builder->CreateBitCast(alloca, llvm::PointerType::get(*ctx, 0));
                    auto* name_str = builder->CreateGlobalString(dt->name);
                    // Use free()-based finalizer — the alloca'd copy needs to be freed
                    // Actually, for stack-allocated structs, we need malloc + memcpy
                    // so the finalizer can free it later
                    auto* malloc_fn = mod->getFunction("malloc");
                    if (!malloc_fn) {
                        auto* malloc_type = llvm::FunctionType::get(llvm::PointerType::get(*ctx, 0),
                            {llvm::Type::getInt64Ty(*ctx)}, false);
                        malloc_fn = llvm::Function::Create(malloc_type, llvm::Function::ExternalLinkage,
                            "malloc", mod.get());
                    }
                    auto* struct_size = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx),
                        mod->getDataLayout().getTypeAllocSize(struct_type).getFixedValue());
                    auto* heap_mem = builder->CreateCall(malloc_fn, {struct_size});
                    auto* heap_typed = builder->CreateBitCast(heap_mem, llvm::PointerType::get(struct_type, 0));
                    // Copy the struct value to heap
                    builder->CreateStore(c_val, heap_typed);
                    // Wrap in NativeInstance
                    auto* fin_fn = mod->getFunction("Angara_foreign_free_" + dt->name);
                    if (!fin_fn) fin_fn = mod->getFunction("free");
                    llvm::Value* finalizer = fin_fn
                        ? static_cast<llvm::Value*>(fin_fn)
                        : llvm::ConstantPointerNull::get(llvm::PointerType::get(*ctx, 0));
                    auto* native_obj = callRtByName("__ang_api_native_instance_new",
                        {heap_mem, finalizer, name_str});
                    return native_obj;
                }
            }
        }
    }
    if (type->kind == TypeKind::DATA) {
        auto dt = std::dynamic_pointer_cast<DataType>(type);
        if (dt && dt->is_foreign) {
            // Wrap the C pointer in a NativeInstance; if NULL, return nil
            auto* fn = builder->GetInsertBlock()->getParent();
            auto* entry_bb = builder->GetInsertBlock();
            auto* wrap_bb = llvm::BasicBlock::Create(*ctx, "ffi_wrap", fn);
            auto* merge_bb = llvm::BasicBlock::Create(*ctx, "ffi_merge", fn);

            auto* is_null = builder->CreateICmpEQ(c_val,
                llvm::ConstantPointerNull::get(llvm::PointerType::get(*ctx, 0)));
            builder->CreateCondBr(is_null, merge_bb, wrap_bb);

            builder->SetInsertPoint(wrap_bb);
            auto* name_str = builder->CreateGlobalString(dt->name);
            // For non-opaque foreign data, use the free()-based finalizer generated in codegenForeignDataDecl.
            // For opaque types (e.g., FILE*), use null — the user must manually release the resource.
            llvm::Value* finalizer = llvm::ConstantPointerNull::get(llvm::PointerType::get(*ctx, 0));
            if (!dt->is_opaque) {
                auto* fin_fn = mod->getFunction("Angara_foreign_free_" + dt->name);
                if (fin_fn) finalizer = fin_fn;
            }
            auto* native_obj = callRtByName("__ang_api_native_instance_new",
                                             {c_val, finalizer, name_str});
            auto* wrap_end_bb = builder->GetInsertBlock();
            builder->CreateBr(merge_bb);

            builder->SetInsertPoint(merge_bb);
            auto* phi = builder->CreatePHI(objType, 2);
            phi->addIncoming(makeNil(), entry_bb);
            phi->addIncoming(native_obj, wrap_end_bb);
            return phi;
        }
    }
    if (type->kind == TypeKind::POINTER) {
        // Wrap C pointer in NativeInstance with free() finalizer (ARC-managed).
        // If NULL, return nil. This ensures malloc'd pointers are freed on scope exit.
        auto* fn = builder->GetInsertBlock()->getParent();
        auto* entry_bb = builder->GetInsertBlock();
        auto* wrap_bb = llvm::BasicBlock::Create(*ctx, "ptr_wrap", fn);
        auto* merge_bb = llvm::BasicBlock::Create(*ctx, "ptr_merge", fn);

        auto* is_null = builder->CreateICmpEQ(c_val,
            llvm::ConstantPointerNull::get(llvm::PointerType::get(*ctx, 0)));
        builder->CreateCondBr(is_null, merge_bb, wrap_bb);

        builder->SetInsertPoint(wrap_bb);
        auto* name_str = builder->CreateGlobalString("*void");
        auto* free_fn = mod->getFunction("free");
        if (!free_fn) {
            auto* free_type = llvm::FunctionType::get(llvm::Type::getVoidTy(*ctx),
                {llvm::PointerType::get(*ctx, 0)}, false);
            free_fn = llvm::Function::Create(free_type, llvm::Function::ExternalLinkage,
                                              "free", mod.get());
        }
        auto* native_obj = callRtByName("__ang_api_native_instance_new",
                                         {c_val, free_fn, name_str});
        auto* wrap_end_bb = builder->GetInsertBlock();
        builder->CreateBr(merge_bb);

        builder->SetInsertPoint(merge_bb);
        auto* phi = builder->CreatePHI(objType, 2);
        phi->addIncoming(makeNil(), entry_bb);
        phi->addIncoming(native_obj, wrap_end_bb);
        return phi;
    }
    if (type->kind == TypeKind::FUNCTION) {
        // C function pointer → Angara closure wrapper
        auto func_type = std::dynamic_pointer_cast<FunctionType>(type);

        static int wrapper_counter = 0;
        std::string key = "ffi_cfn_wrapper_" + std::to_string(wrapper_counter++);

        // Store the C function pointer in a global (the wrapper will load and call it)
        auto* c_fnptr_global = new llvm::GlobalVariable(
            *mod, llvm::PointerType::get(*ctx, 0), false,
            llvm::GlobalValue::InternalLinkage,
            llvm::ConstantPointerNull::get(llvm::PointerType::get(*ctx, 0)),
            "Angara_" + key + "_cfn");
        builder->CreateStore(c_val, c_fnptr_global);

        // Generate a wrapper with Angara closure calling convention:
        // AngaraObject wrapper(i32 argc, void* args_array)
        auto* wrapper_type = llvm::FunctionType::get(objType, {
            llvm::Type::getInt32Ty(*ctx), llvm::PointerType::get(*ctx, 0)
        }, false);
        auto* wrapper = llvm::Function::Create(wrapper_type,
            llvm::Function::InternalLinkage, "Angara_" + key, mod.get());

        auto saved_insert_point = builder->saveIP();
        auto* wrapper_entry = llvm::BasicBlock::Create(*ctx, "entry", wrapper);
        builder->SetInsertPoint(wrapper_entry);

        // 1. Load the C function pointer from the global
        auto* c_fn = builder->CreateLoad(llvm::PointerType::get(*ctx, 0), c_fnptr_global, "c_fn");

        // 2. Unpack AngaraObject args from the array, marshal each to C
        int argc = func_type->param_types.size();
        auto* args_ptr = wrapper->arg_begin() + 1; // void* args_array
        std::vector<llvm::Value*> c_args;
        for (int i = 0; i < argc; i++) {
            auto* slot = builder->CreateGEP(objType, args_ptr,
                {llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), i)});
            auto* ang_obj = builder->CreateLoad(objType, slot, "arg");
            c_args.push_back(marshalAngaraToC(ang_obj, func_type->param_types[i]));
        }

        // 3. Call the C function pointer with the marshalled args
        // Build the C function type for the call
        std::vector<llvm::Type*> c_param_types;
        for (const auto& pt : func_type->param_types) {
            c_param_types.push_back(resolveCFieldType(pt));
        }
        auto* c_ret_type = resolveCFieldType(func_type->return_type);
        auto* c_fn_type = llvm::FunctionType::get(c_ret_type, c_param_types, false);
        auto* c_result = builder->CreateCall(c_fn_type, c_fn, c_args);

        // 4. Marshal the result back to AngaraObject
        if (func_type->return_type->kind == TypeKind::VOID) {
            builder->CreateRet(makeNil());
        } else {
            builder->CreateRet(marshalCToAngara(c_result, func_type->return_type));
        }

        builder->restoreIP(saved_insert_point);

        // Create a closure wrapping this wrapper function
        llvm::Value* closure;
        auto* closure_new_fn = mod->getFunction("__ang_closure_new");
        if (closure_new_fn) {
            closure = builder->CreateCall(closure_new_fn, {
                wrapper,
                llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), argc),
                llvm::ConstantInt::get(llvm::Type::getInt1Ty(*ctx), 1), // native = true
                llvm::ConstantPointerNull::get(llvm::PointerType::get(*ctx, 0)) // no env
            });
        } else {
            closure = makeNil();
        }
        return closure;
    }

    return makeI64(c_val);
}

llvm::Value* LLVMBackend::cgForeignFieldAccess(const GetExpr& e, std::shared_ptr<DataType> data_type) {
    auto* obj = cg(e.object);

    // Extract the raw data pointer from the NativeInstance
    auto* data_ptr = callRtByName("__ang_api_native_instance_data", {obj});

    // Get the LLVM struct type for this foreign data
    auto it = m_foreign_struct_types.find(data_type->name);
    if (it == m_foreign_struct_types.end()) return makeNil();

    auto* struct_type = it->second;
    auto* struct_ptr = builder->CreateBitCast(data_ptr, llvm::PointerType::get(*ctx, 0));

    // Find the field index using declaration order (matching C struct layout)
    auto order_it = m_foreign_field_order.find(data_type->name);
    if (order_it == m_foreign_field_order.end()) return makeNil();

    unsigned field_index = 0;
    bool found = false;
    for (const auto& fname : order_it->second) {
        if (fname == e.name.lexeme) { found = true; break; }
        field_index++;
    }
    if (!found) return makeNil();

    // Determine the field type and load + marshal
    auto field_it = data_type->fields.find(e.name.lexeme);
    if (field_it == data_type->fields.end()) return makeNil();
    auto& field_type = field_it->second.type;

    // For unions, all fields overlap at offset 0 — load directly from struct_ptr
    // For structs, use GEP to get the field pointer at the correct offset
    llvm::Value* field_ptr;
    if (data_type->is_union) {
        field_ptr = struct_ptr; // all fields at offset 0
    } else {
        field_ptr = builder->CreateStructGEP(struct_type, struct_ptr, field_index);
    }

    if (field_type->kind == TypeKind::FIXED_ARRAY) {
        // i8[N] field: get pointer to first element and create Angara string from it
        auto arr_type = std::dynamic_pointer_cast<FixedArrayType>(field_type);
        auto* llvm_arr_type = llvm::ArrayType::get(resolveCFieldType(arr_type->element_type), arr_type->size);
        // GEP: field_ptr is a pointer to the array field; get pointer to first element
        auto* elem_ptr = builder->CreateGEP(llvm_arr_type, field_ptr,
            {llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), 0),
             llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), 0)});
        auto* char_ptr = builder->CreateBitCast(elem_ptr, llvm::PointerType::get(*ctx, 0));
        return callRtByName("__ang_string_from_c", {char_ptr});
    }

    if (field_type->kind == TypeKind::DATA) {
        // Nested foreign struct: wrap the embedded struct pointer in a NativeInstance
        auto nested_dt = std::dynamic_pointer_cast<DataType>(field_type);
        if (nested_dt && nested_dt->is_foreign && !nested_dt->is_opaque) {
            auto* field_void_ptr = builder->CreateBitCast(field_ptr, llvm::PointerType::get(*ctx, 0));
            auto* name_str = builder->CreateGlobalString(nested_dt->name);
            auto* null_finalizer = llvm::ConstantPointerNull::get(llvm::PointerType::get(*ctx, 0));
            return callRtByName("__ang_api_native_instance_new",
                {field_void_ptr, null_finalizer, name_str});
        }
    }

    // Load the raw C value
    auto* c_val = builder->CreateLoad(resolveCFieldType(field_type), field_ptr);
    return marshalCToAngara(c_val, field_type);
}

llvm::Value* LLVMBackend::callVariadicForeignFn(const std::string& c_func_name,
                                                  const std::shared_ptr<FunctionType>& func_type,
                                                  const std::vector<std::shared_ptr<Expr>>& args) {
    // Look up the raw C function
    auto* cFunc = mod->getFunction(c_func_name);
    if (!cFunc) return makeNil();

    size_t fixed_count = func_type->param_types.size();
    const auto& expr_types = m_type_checker.getExpressionTypes();
    bool returnsVoid = func_type->return_type->kind == TypeKind::NIL
                     || func_type->return_type->kind == TypeKind::VOID;

    std::vector<llvm::Value*> cArgs;

    // Marshal fixed params
    for (size_t i = 0; i < fixed_count && i < args.size(); i++) {
        auto* obj = cg(args[i]);
        cArgs.push_back(marshalAngaraToC(obj, func_type->param_types[i]));
    }

    // Marshal variadic args with C default argument promotions
    for (size_t i = fixed_count; i < args.size(); i++) {
        auto* obj = cg(args[i]);

        // Determine expression type
        auto type_it = expr_types.find(args[i].get());
        std::shared_ptr<Type> arg_type = (type_it != expr_types.end()) ? type_it->second : nullptr;

        if (!arg_type || arg_type->kind == TypeKind::ERROR) {
            // Unknown type — pass as i64
            cArgs.push_back(getI64(obj));
            continue;
        }

        // Marshal to C type, then apply default argument promotions
        auto* c_val = marshalAngaraToC(obj, arg_type);

        // Default argument promotions for C variadics:
        // - float -> double
        // - integer types smaller than int -> int (i32)
        if (arg_type->kind == TypeKind::PRIMITIVE) {
            const auto& n = arg_type->toString();
            if (n == "f32") {
                // float promotes to double
                c_val = builder->CreateFPExt(c_val, llvm::Type::getDoubleTy(*ctx));
            } else if (n == "bool" || n == "i8" || n == "u8" || n == "i16" || n == "u16") {
                // Promote to i32 (C int)
                c_val = builder->CreateZExt(c_val, llvm::Type::getInt32Ty(*ctx));
            }
        }

        cArgs.push_back(c_val);
    }

    // Call the C function
    llvm::CallInst* result = builder->CreateCall(cFunc, cArgs);

    // Marshal return value
    if (returnsVoid) {
        return makeNil();
    }
    return marshalCToAngara(result, func_type->return_type);
}

} // namespace angara
