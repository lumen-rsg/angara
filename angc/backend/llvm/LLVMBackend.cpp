// Angara LLVM Backend — Core Infrastructure
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
#include <iostream>

namespace angara {

// ============================================================================
// Constructor / Destructor
// ============================================================================

LLVMBackend::~LLVMBackend() {
    (void)rt.release();
    (void)builder.release();
    (void)mod.release();
    (void)ctx.release();
}

LLVMBackend::LLVMBackend(TypeChecker& tc, ErrorHandler& eh, const std::string& target_triple, bool freestanding)
    : m_type_checker(tc), m_errorHandler(eh), m_freestanding(freestanding) {
    ctx = std::make_unique<llvm::LLVMContext>();
    mod = std::make_unique<llvm::Module>("angara_module", *ctx);
    builder = std::make_unique<llvm::IRBuilder<>>(*ctx);
    std::string ttStr = target_triple.empty()
        ? llvm::sys::getDefaultTargetTriple()
        : target_triple;
    targetTriple = llvm::Triple(llvm::StringRef(ttStr));
    mod->setTargetTriple(targetTriple);
    llvm::InitializeAllTargetInfos(); llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs(); llvm::InitializeAllAsmParsers(); llvm::InitializeAllAsmPrinters();
    std::string te;
    if (auto* t = llvm::TargetRegistry::lookupTarget(llvm::Triple(targetTriple.str()), te)) {
        llvm::TargetOptions opt;
        if (auto* tm = t->createTargetMachine(targetTriple,"generic","",opt,std::nullopt))
            mod->setDataLayout(tm->createDataLayout());
    }
    rt = std::make_unique<RuntimeBuilder>(*ctx, *mod, *builder, m_freestanding);
    rt->generateRuntime();
    objType = rt->getAngaraObjType();
}

// ============================================================================
// Main entry
// ============================================================================

bool LLVMBackend::generate(const std::vector<std::shared_ptr<Stmt>>& stmts,
    const std::shared_ptr<ModuleType>& moduleType, std::vector<std::string>& allMods) {
    moduleName = moduleType ? moduleType->name : "main";
    codegenTopLevelDecls(stmts);
    // Only generate the C main entry point if this module defines a user "main" function.
    // Library modules (like collections) have no main — skip to avoid duplicate symbols.
    std::string user_main_name = mangle(moduleName, "main");
    if (mod->getFunction(user_main_name)) {
        codegenMainFunction(stmts, moduleName, allMods);
    }
    std::string base = "ang_" + moduleName;
    {
        std::error_code ec;
        llvm::raw_fd_ostream ir(base+".ll", ec, llvm::sys::fs::OF_Text);
        if (!ec) { mod->print(ir,nullptr); ir.close(); }
        irPath = base+".ll";
    }
    std::string ve; llvm::raw_string_ostream es(ve);
    if (llvm::verifyModule(*mod, &es)) { std::cerr<<"Verify: "<<ve<<"\n"; return false; }
    std::error_code ec;
    std::string le; auto* tgt = llvm::TargetRegistry::lookupTarget(llvm::Triple(targetTriple.str()),le);
    if (!tgt) { std::cerr<<"No target: "<<le<<"\n"; return false; }
    llvm::TargetOptions opt; auto* tm = tgt->createTargetMachine(targetTriple,"generic","",opt,std::nullopt);
    if (!tm) { std::cerr<<"No TM\n"; return false; }

    {
        llvm::LoopAnalysisManager lam;
        llvm::FunctionAnalysisManager fam;
        llvm::CGSCCAnalysisManager cgam;
        llvm::ModuleAnalysisManager mam;
        llvm::PassBuilder pb(tm);
        pb.registerModuleAnalyses(mam);
        pb.registerCGSCCAnalyses(cgam);
        pb.registerFunctionAnalyses(fam);
        pb.registerLoopAnalyses(lam);
        pb.crossRegisterProxies(lam, fam, cgam, mam);
        llvm::ModulePassManager mpm = pb.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);
        mpm.run(*mod, mam);
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

// ============================================================================
// AngaraObject inline constructors
// ============================================================================

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
llvm::Value* LLVMBackend::makeStr(const std::string& s) {
    return callRtByName("__ang_string_from_c", {builder->CreateGlobalString(s)});
}

// ============================================================================
// Extractors
// ============================================================================

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

// ============================================================================
// Runtime call helper
// ============================================================================

llvm::Value* LLVMBackend::callRt(llvm::FunctionCallee c, const std::vector<llvm::Value*>& a) {
    return builder->CreateCall(c, a);
}

llvm::Value* LLVMBackend::callRtByName(const std::string& name, const std::vector<llvm::Value*>& a) {
    auto* fn = mod->getFunction(name);
    if (fn) return builder->CreateCall(fn, a);
    return makeNil();
}

// ============================================================================
// Variable management
// ============================================================================

llvm::AllocaInst* LLVMBackend::allocLocal(llvm::Function* fn, const std::string& name) {
    llvm::IRBuilder<> tmp(&fn->getEntryBlock(), fn->getEntryBlock().begin());
    return tmp.CreateAlloca(objType, nullptr, name);
}

llvm::Value* LLVMBackend::loadVar(const std::string& n) {
    if (auto it=namedVals.find(n); it!=namedVals.end()) return builder->CreateLoad(objType, it->second, n);
    if (auto it=globals.find(n); it!=globals.end()) return builder->CreateLoad(objType, it->second, n);
    if (auto it=globals.find("g_"+n); it!=globals.end()) return builder->CreateLoad(objType, it->second, n);
    return makeNil();
}
void LLVMBackend::storeVar(const std::string& n, llvm::Value* v) {
    if (auto it=namedVals.find(n); it!=namedVals.end()) { builder->CreateStore(v,it->second); return; }
    if (auto it=globals.find(n); it!=globals.end()) { builder->CreateStore(v,it->second); return; }
    if (auto it=globals.find("g_"+n); it!=globals.end()) { builder->CreateStore(v,it->second); return; }
}

// ============================================================================
// Helpers
// ============================================================================

std::string LLVMBackend::mangle(const std::string& m, const std::string& n) { return "__ang_"+m+"_"+sanitize(n); }
std::string LLVMBackend::mangleMethod(const std::string& c, const std::string& m) { return "__ang_"+sanitize(c)+"_"+sanitize(m); }
std::string LLVMBackend::sanitize(const std::string& n) { std::string r; for(char c:n) r+=(std::isalnum(c)||c=='_')?c:'_'; return r; }

// ============================================================================
// Integer narrowing helpers (Option B: semantic truncation at boundaries)
// ============================================================================

bool LLVMBackend::isSizedIntType(const std::shared_ptr<Type>& type) {
    if (!type || type->kind != TypeKind::PRIMITIVE) return false;
    const auto& name = type->toString();
    return name == "i8"  || name == "i16" || name == "i32" || name == "i64" ||
           name == "u8"  || name == "u16" || name == "u32" || name == "u64";
}

bool LLVMBackend::isUnsignedIntType(const std::shared_ptr<Type>& type) {
    if (!type || type->kind != TypeKind::PRIMITIVE) return false;
    const auto& name = type->toString();
    return name == "u8" || name == "u16" || name == "u32" || name == "u64";
}

int LLVMBackend::getIntBitWidth(const std::shared_ptr<Type>& type) {
    if (!type || type->kind != TypeKind::PRIMITIVE) return 64;
    const auto& name = type->toString();
    if (name == "i8"  || name == "u8")  return 8;
    if (name == "i16" || name == "u16") return 16;
    if (name == "i32" || name == "u32") return 32;
    return 64; // i64, u64, or anything else
}

llvm::Value* LLVMBackend::truncateForType(llvm::Value* val, const std::shared_ptr<Type>& type) {
    if (!isSizedIntType(type)) return val;
    int bits = getIntBitWidth(type);
    if (bits >= 64) return val; // i64/u64 — no truncation needed

    // Extract the raw i64 payload
    llvm::Value* payload = getI64(val);

    // Truncate to the target bit width
    llvm::Type* truncTy = llvm::IntegerType::get(*ctx, bits);
    llvm::Value* truncated = builder->CreateTrunc(payload, truncTy, "narrow");

    // Zero-extend back to i64 (this masks out the high bits)
    llvm::Value* masked = builder->CreateZExt(truncated, llvm::Type::getInt64Ty(*ctx), "masked");

    return makeI64(masked);
}

} // namespace angara
