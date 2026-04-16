// Angara LLVM Backend — Self-contained, no C runtime dependency
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
#include <llvm/Transforms/Scalar.h>
#include <iostream>

namespace angara {

LLVMBackend::~LLVMBackend() {
    // Release LLVM objects without destroying — prevents crash in LLVM destructors.
    // Safe because the process is about to exit anyway.
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
    // Use provided target triple, or fall back to host default
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
    codegenMainFunction(stmts, moduleName, allMods);
    std::string base = "ang_" + moduleName;
    // Always dump IR for debugging
    {
        std::error_code ec;
        llvm::raw_fd_ostream ir(base+".ll", ec, llvm::sys::fs::OF_Text);
        if (!ec) { mod->print(ir,nullptr); ir.close(); }
        irPath = base+".ll";
    }
    std::string ve; llvm::raw_string_ostream es(ve);
    if (llvm::verifyModule(*mod, &es)) { std::cerr<<"Verify: "<<ve<<"\n"; return false; }
    std::error_code ec;
    // Use the same target triple that was set in the constructor
    std::string le; auto* tgt = llvm::TargetRegistry::lookupTarget(llvm::Triple(targetTriple.str()),le);
    if (!tgt) { std::cerr<<"No target: "<<le<<"\n"; return false; }
    llvm::TargetOptions opt; auto* tm = tgt->createTargetMachine(targetTriple,"generic","",opt,std::nullopt);
    if (!tm) { std::cerr<<"No TM\n"; return false; }

    // Run O2 optimization
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

    // Emit object file in a sub-scope so legacy PassManager is destroyed before we clean up
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
// AngaraObject inline constructors — payload is i64
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
    return callRt(rt->getFuncStringFromC(), {builder->CreateGlobalString(s)});
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

llvm::Value* LLVMBackend::callRt(llvm::FunctionCallee c, const std::vector<llvm::Value*>& a) { return builder->CreateCall(c, a); }

llvm::AllocaInst* LLVMBackend::allocLocal(llvm::Function* fn, const std::string& name) {
    llvm::IRBuilder<> tmp(&fn->getEntryBlock(), fn->getEntryBlock().begin());
    return tmp.CreateAlloca(objType, nullptr, name);
}

llvm::Value* LLVMBackend::loadVar(const std::string& n) {
    if (auto it=namedVals.find(n); it!=namedVals.end()) return builder->CreateLoad(objType, it->second, n);
    if (auto it=globals.find(n); it!=globals.end()) return builder->CreateLoad(objType, it->second, n);
    return makeNil();
}
void LLVMBackend::storeVar(const std::string& n, llvm::Value* v) {
    if (auto it=namedVals.find(n); it!=namedVals.end()) { builder->CreateStore(v,it->second); return; }
    if (auto it=globals.find(n); it!=globals.end()) { builder->CreateStore(v,it->second); return; }
}

// ============================================================================
// Helpers
// ============================================================================
std::string LLVMBackend::mangle(const std::string& m, const std::string& n) { return "__ang_"+m+"_"+sanitize(n); }
std::string LLVMBackend::mangleMethod(const std::string& c, const std::string& m) { return "__ang_"+sanitize(c)+"_"+sanitize(m); }
std::string LLVMBackend::sanitize(const std::string& n) { std::string r; for(char c:n) r+=(std::isalnum(c)||c=='_')?c:'_'; return r; }

// ============================================================================
// Expression codegen
// ============================================================================
llvm::Value* LLVMBackend::cg(const std::shared_ptr<Expr>& e) {
    if (!e) return makeNil();
    if (auto* p = dynamic_cast<const Literal*>(e.get())) return cgLiteral(*p);
    if (auto* p = dynamic_cast<const Binary*>(e.get())) return cgBinary(*p);
    if (auto* p = dynamic_cast<const Unary*>(e.get())) return cgUnary(*p);
    if (auto* p = dynamic_cast<const Grouping*>(e.get())) return cg(p->expression);
    if (auto* p = dynamic_cast<const VarExpr*>(e.get())) return loadVar(p->name.lexeme);
    if (auto* p = dynamic_cast<const AssignExpr*>(e.get())) return cgAssign(*p);
    if (auto* p = dynamic_cast<const UpdateExpr*>(e.get())) return cgUpdate(*p);
    if (auto* p = dynamic_cast<const CallExpr*>(e.get())) return cgCall(*p);
    if (auto* p = dynamic_cast<const GetExpr*>(e.get())) return cgGet(*p);
    if (auto* p = dynamic_cast<const ListExpr*>(e.get())) return cgList(*p);
    if (auto* p = dynamic_cast<const LogicalExpr*>(e.get())) return cgLogical(*p);
    if (auto* p = dynamic_cast<const SubscriptExpr*>(e.get())) return cgSubscript(*p);
    if (auto* p = dynamic_cast<const RecordExpr*>(e.get())) return cgRecord(*p);
    if (auto* p = dynamic_cast<const TernaryExpr*>(e.get())) return cgTernary(*p);
    if (auto* p = dynamic_cast<const ThisExpr*>(e.get())) return loadVar("this");
    if (auto* p = dynamic_cast<const SuperExpr*>(e.get())) return makeNil();
    if (auto* p = dynamic_cast<const IsExpr*>(e.get())) return cgIs(*p);
    if (auto* p = dynamic_cast<const MatchExpr*>(e.get())) return cgMatch(*p);
    if (auto* p = dynamic_cast<const SizeofExpr*>(e.get())) return makeI64((int64_t)16);
    if (auto* p = dynamic_cast<const RetypeExpr*>(e.get())) return cgRetype(*p);
    return makeNil();
}

llvm::Value* LLVMBackend::cgLiteral(const Literal& e) {
    const auto& tok = e.token;
    if (tok.type == TokenType::NIL) return makeNil();
    if (tok.type == TokenType::TRUE) return makeBool(true);
    if (tok.type == TokenType::FALSE) return makeBool(false);
    if (tok.type == TokenType::NUMBER_INT) return makeI64(std::stoll(tok.lexeme));
    if (tok.type == TokenType::NUMBER_FLOAT) return makeF64(std::stod(tok.lexeme));
    if (tok.type == TokenType::STRING) return makeStr(tok.lexeme);
    return makeNil();
}

llvm::Value* LLVMBackend::cgBinary(const Binary& e) {
    auto* l = cg(e.left), *r = cg(e.right);
    if (!l||!r) return makeNil();
    switch (e.op.type) {
        case TokenType::PLUS: {
            // Runtime dispatch: integers add directly, otherwise string concat
            auto* lTag = getTag(l);
            auto* rTag = getTag(r);
            auto* bothI64 = builder->CreateAnd(
                builder->CreateICmpEQ(lTag, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), TAG_I64)),
                builder->CreateICmpEQ(rTag, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), TAG_I64)));
            auto* fn = builder->GetInsertBlock()->getParent();
            auto* iaddBB = llvm::BasicBlock::Create(*ctx,"iadd",fn);
            auto* saddBB = llvm::BasicBlock::Create(*ctx,"sadd",fn);
            auto* maddBB = llvm::BasicBlock::Create(*ctx,"madd",fn);
            builder->CreateCondBr(bothI64, iaddBB, saddBB);
            builder->SetInsertPoint(iaddBB);
            auto* ia = makeI64(builder->CreateAdd(getI64(l),getI64(r)));
            iaddBB = builder->GetInsertBlock();
            builder->CreateBr(maddBB);
            builder->SetInsertPoint(saddBB);
            auto* sa = callRt(rt->getFuncStringConcat(),{l,r});
            saddBB = builder->GetInsertBlock();
            builder->CreateBr(maddBB);
            builder->SetInsertPoint(maddBB);
            auto* phi = builder->CreatePHI(objType,2);
            phi->addIncoming(ia,iaddBB); phi->addIncoming(sa,saddBB);
            return phi;
        }
        case TokenType::MINUS: return makeI64(builder->CreateSub(getI64(l),getI64(r)));
        case TokenType::STAR: return makeI64(builder->CreateMul(getI64(l),getI64(r)));
        case TokenType::SLASH: return makeI64(builder->CreateSDiv(getI64(l),getI64(r)));
        case TokenType::PERCENT: return makeI64(builder->CreateSRem(getI64(l),getI64(r)));
        case TokenType::LESS: return makeBool(builder->CreateICmpSLT(getI64(l),getI64(r)));
        case TokenType::LESS_EQUAL: return makeBool(builder->CreateICmpSLE(getI64(l),getI64(r)));
        case TokenType::GREATER: return makeBool(builder->CreateICmpSGT(getI64(l),getI64(r)));
        case TokenType::GREATER_EQUAL: return makeBool(builder->CreateICmpSGE(getI64(l),getI64(r)));
        case TokenType::EQUAL_EQUAL: return callRt(rt->getFuncEquals(),{l,r});
        case TokenType::BANG_EQUAL: { auto* eq=callRt(rt->getFuncEquals(),{l,r}); return makeBool(builder->CreateNot(getBool(eq))); }
        default: return makeNil();
    }
}

llvm::Value* LLVMBackend::cgUnary(const Unary& e) {
    auto* o=cg(e.right); if(!o) return makeNil();
    if (e.op.type==TokenType::MINUS) return makeI64(builder->CreateNeg(getI64(o)));
    if (e.op.type==TokenType::BANG) return makeBool(builder->CreateNot(isTruthy(o)));
    return o;
}

llvm::Value* LLVMBackend::cgAssign(const AssignExpr& e) {
    auto* v = cg(e.value);
    if (auto* var = dynamic_cast<const VarExpr*>(e.target.get())) {
        storeVar(var->name.lexeme, v);
        return v;
    }
    if (auto* get = dynamic_cast<const GetExpr*>(e.target.get())) {
        // obj.field = value → record_set(obj, "field", value)
        auto* obj = cg(get->object);
        callRt(rt->getFuncRecordSet(), {obj, builder->CreateGlobalString(get->name.lexeme), v});
        return v;
    }
    if (auto* sub = dynamic_cast<const SubscriptExpr*>(e.target.get())) {
        // obj[key] = value — compile-time dispatch like cgSubscript
        auto* obj = cg(sub->object);
        if (auto* lit = dynamic_cast<const Literal*>(sub->index.get())) {
            if (lit->token.type == TokenType::STRING) {
                callRt(rt->getFuncRecordSet(), {obj, builder->CreateGlobalString(lit->token.lexeme), v});
                return v;
            }
        }
        callRt(rt->getFuncListSet(), {obj, cg(sub->index), v});
        return v;
    }
    return v;
}

llvm::Value* LLVMBackend::cgUpdate(const UpdateExpr& e) {
    if (auto* var = dynamic_cast<const VarExpr*>(e.target.get())) {
        auto* cur = loadVar(var->name.lexeme);
        llvm::Value* result = nullptr;
        if (e.op.type == TokenType::PLUS_PLUS)
            result = makeI64(builder->CreateAdd(getI64(cur), llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx),1)));
        else if (e.op.type == TokenType::MINUS_MINUS)
            result = makeI64(builder->CreateSub(getI64(cur), llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx),1)));
        else result = cur;
        storeVar(var->name.lexeme, result);
        return result;
    }
    return makeNil();
}

llvm::Value* LLVMBackend::cgCall(const CallExpr& expr) {
    if (!expr.callee) return makeNil();
    if (auto* get = dynamic_cast<const GetExpr*>(expr.callee.get())) {
        if (auto* obj = dynamic_cast<const VarExpr*>(get->object.get())) {
            std::string modName = obj->name.lexeme, fnName = get->name.lexeme;
            // Check if this is a method call: obj.method()
            {
                auto mit = methodLookup.find(fnName);
                if (mit != methodLookup.end()) {
                    llvm::Function* mf = this->mod->getFunction(mit->second);
                    if (mf) {
                        std::vector<llvm::Value*> args;
                        args.push_back(loadVar(modName)); // 'this'
                        for (auto& a : expr.arguments) args.push_back(cg(a));
                        auto* ft = mf->getFunctionType();
                        while (args.size() < ft->getNumParams()) args.push_back(makeNil());
                        return builder->CreateCall(mf, args);
                    }
                }
            }
            if (modName=="io") {
                if (fnName=="println"||fnName=="print") {
                    // io.println(stream_id, content) and io.print(stream_id, content)
                    // We pass (stream_id, content) to the runtime function
                    if (expr.arguments.size() >= 2) {
                        if (fnName=="println") callRt(rt->getFuncIOPrintln(), {cg(expr.arguments[0]), cg(expr.arguments[1])});
                        else callRt(rt->getFuncIOPrint(), {cg(expr.arguments[0]), cg(expr.arguments[1])});
                    } else if (!expr.arguments.empty()) {
                        // Single arg: just content, default to stdout
                        if (fnName=="println") callRt(rt->getFuncIOPrintln(), {cg(expr.arguments[0])});
                        else callRt(rt->getFuncIOPrint(), {cg(expr.arguments[0])});
                    }
                    return makeNil();
                }
                if (fnName=="write") {
                    if (expr.arguments.size() >= 2)
                        callRt(rt->getFuncIOWrite(), {cg(expr.arguments[0]), cg(expr.arguments[1])});
                    return makeNil();
                }
                if (fnName=="flush") {
                    if (!expr.arguments.empty()) callRt(rt->getFuncIOFlush(), {cg(expr.arguments[0])});
                    return makeNil();
                }
                if (fnName=="read_line") return callRt(rt->getFuncIOReadLine(), {});
                if (fnName=="read_all") return callRt(rt->getFuncIOReadAll(), {});
            }
            return callModuleFn(modName, fnName, expr.arguments);
        }
        return cg(get->object);
    }
    if (auto* var = dynamic_cast<const VarExpr*>(expr.callee.get())) {
        std::string fn = var->name.lexeme;
        if (fn=="string") {
            if (!expr.arguments.empty()) return callRt(rt->getFuncToString(),{cg(expr.arguments[0])});
            return makeStr("");
        }
        // Check if this is a class/data constructor call
        auto cit = constructorLookup.find(fn);
        if (cit != constructorLookup.end()) {
            llvm::Function* ctor = this->mod->getFunction(cit->second);
            if (ctor) {
                std::vector<llvm::Value*> args;
                for (auto& a : expr.arguments) args.push_back(cg(a));
                auto* ft = ctor->getFunctionType();
                while (args.size() < ft->getNumParams()) args.push_back(makeNil());
                return builder->CreateCall(ctor, args);
            }
        }
        return callModuleFn(moduleName, fn, expr.arguments);
    }
    return makeNil();
}

llvm::Value* LLVMBackend::callModuleFn(const std::string& mod, const std::string& fn,
                                        const std::vector<std::shared_ptr<Expr>>& args) {
    std::string mangled = mangle(mod, fn);
    llvm::Function* f = this->mod->getFunction(mangled);
    if (!f) f = this->mod->getFunction("__ang_"+sanitize(fn));
    if (!f) return makeNil();

    // Check if function takes direct AngaraObject params (i.e. no i32 count + ptr pair)
    auto ft = f->getFunctionType();
    bool direct_call = true;
    if (ft->getNumParams() < 1) direct_call = false;
    else {
        // If the first param is i32 and second is ptr, it's the count+array convention
        if (ft->getNumParams() == 2 &&
            ft->getParamType(0)->isIntegerTy(32) &&
            ft->getParamType(1)->isPointerTy()) {
            direct_call = false;
        }
    }

    if (direct_call) {
        // Call with individual AngaraObject arguments
        std::vector<llvm::Value*> llvmArgs;
        for (auto& a : args) llvmArgs.push_back(cg(a));
        // Pad with nil if fewer args than params
        while (llvmArgs.size() < ft->getNumParams()) llvmArgs.push_back(makeNil());
        return builder->CreateCall(f, llvmArgs);
    }

    // Call with count+array convention
    std::vector<llvm::Value*> llvmArgs;
    auto cnt = args.size();
    llvmArgs.push_back(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx),(int)cnt));
    if (cnt > 0) {
        auto* aa = builder->CreateAlloca(llvm::ArrayType::get(objType,cnt));
        for (size_t i=0; i<cnt; i++) {
            auto* ep = builder->CreateGEP(llvm::ArrayType::get(objType,cnt), aa,
                {llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx),0),
                 llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx),i)});
            builder->CreateStore(cg(args[i]), ep);
        }
        llvmArgs.push_back(builder->CreateBitCast(aa, llvm::PointerType::get(*ctx, 0)));
    } else {
        llvmArgs.push_back(llvm::ConstantPointerNull::get(llvm::PointerType::get(*ctx, 0)));
    }
    return builder->CreateCall(f, llvmArgs);
}

llvm::Value* LLVMBackend::cgGet(const GetExpr& e) {
    auto* obj = cg(e.object);
    // Property access: obj.field → record_get(obj, "field")
    return callRt(rt->getFuncRecordGet(), {obj, builder->CreateGlobalString(e.name.lexeme)});
}

llvm::Value* LLVMBackend::cgList(const ListExpr& e) {
    auto* l = callRt(rt->getFuncListNew(),{});
    for (auto& el : e.elements) callRt(rt->getFuncListPush(),{l, cg(el)});
    return l;
}

llvm::Value* LLVMBackend::cgLogical(const LogicalExpr& e) {
    auto* l = cg(e.left);
    auto* fn = builder->GetInsertBlock()->getParent();
    if (e.op.type == TokenType::LOGICAL_AND) {
        auto* leftBB = builder->GetInsertBlock();
        auto* rhs = llvm::BasicBlock::Create(*ctx,"and_r",fn);
        auto* merge = llvm::BasicBlock::Create(*ctx,"and_m",fn);
        builder->CreateCondBr(isTruthy(l), rhs, merge);
        builder->SetInsertPoint(rhs);
        auto* r = cg(e.right); rhs = builder->GetInsertBlock();
        builder->CreateBr(merge);
        builder->SetInsertPoint(merge);
        auto* phi = builder->CreatePHI(objType,2);
        phi->addIncoming(l, leftBB); phi->addIncoming(r, rhs);
        return phi;
    }
    if (e.op.type == TokenType::LOGICAL_OR) {
        auto* leftBB = builder->GetInsertBlock();
        auto* rhs = llvm::BasicBlock::Create(*ctx,"or_r",fn);
        auto* merge = llvm::BasicBlock::Create(*ctx,"or_m",fn);
        builder->CreateCondBr(isTruthy(l), merge, rhs);
        builder->SetInsertPoint(rhs);
        auto* r = cg(e.right); rhs = builder->GetInsertBlock();
        builder->CreateBr(merge);
        builder->SetInsertPoint(merge);
        auto* phi = builder->CreatePHI(objType,2);
        phi->addIncoming(l, leftBB); phi->addIncoming(r, rhs);
        return phi;
    }
    return l;
}

llvm::Value* LLVMBackend::cgSubscript(const SubscriptExpr& e) {
    auto* obj = cg(e.object);
    // Compile-time dispatch: if index is a string literal, use record_get; else list_get
    if (auto* lit = dynamic_cast<const Literal*>(e.index.get())) {
        if (lit->token.type == TokenType::STRING) {
            return callRt(rt->getFuncRecordGet(), {obj, builder->CreateGlobalString(lit->token.lexeme)});
        }
    }
    return callRt(rt->getFuncListGet(), {obj, cg(e.index)});
}

llvm::Value* LLVMBackend::cgRecord(const RecordExpr& e) {
    auto* r = callRt(rt->getFuncRecordNew(),{});
    for (size_t i=0; i<e.keys.size(); i++)
        callRt(rt->getFuncRecordSet(), {r, builder->CreateGlobalString(e.keys[i].lexeme), cg(e.values[i])});
    return r;
}

llvm::Value* LLVMBackend::cgTernary(const TernaryExpr& e) {
    auto* fn = builder->GetInsertBlock()->getParent();
    auto* tb = llvm::BasicBlock::Create(*ctx,"tt",fn);
    auto* eb = llvm::BasicBlock::Create(*ctx,"te",fn);
    auto* mg = llvm::BasicBlock::Create(*ctx,"tm",fn);
    builder->CreateCondBr(isTruthy(cg(e.condition)), tb, eb);
    builder->SetInsertPoint(tb); auto* tv = cg(e.thenBranch); tb = builder->GetInsertBlock(); builder->CreateBr(mg);
    builder->SetInsertPoint(eb); auto* ev = cg(e.elseBranch); eb = builder->GetInsertBlock(); builder->CreateBr(mg);
    builder->SetInsertPoint(mg);
    auto* phi = builder->CreatePHI(objType,2);
    phi->addIncoming(tv,tb); phi->addIncoming(ev,eb);
    return phi;
}

llvm::Value* LLVMBackend::cgIs(const IsExpr& e) {
    auto* tag = getTag(cg(e.object));
    return makeBool(builder->CreateICmpEQ(tag, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), TAG_I64)));
}

llvm::Value* LLVMBackend::cgMatch(const MatchExpr& e) {
    auto* subj = cg(e.condition);
    auto* fn = builder->GetInsertBlock()->getParent();
    auto* mg = llvm::BasicBlock::Create(*ctx,"me",fn);
    std::vector<std::pair<llvm::BasicBlock*,llvm::Value*>> inc;
    for (auto& c : e.cases) {
        auto* eq = callRt(rt->getFuncEquals(), {subj, cg(c.pattern)});
        auto* bb = llvm::BasicBlock::Create(*ctx,"mb",fn);
        auto* nb = llvm::BasicBlock::Create(*ctx,"mn",fn);
        builder->CreateCondBr(getBool(eq), bb, nb);
        builder->SetInsertPoint(bb);
        auto* r = cg(c.body); bb = builder->GetInsertBlock();
        builder->CreateBr(mg); inc.push_back({bb,r});
        builder->SetInsertPoint(nb);
    }
    builder->CreateBr(mg); inc.push_back({builder->GetInsertBlock(), makeNil()});
    builder->SetInsertPoint(mg);
    auto* phi = builder->CreatePHI(objType, inc.size());
    for (auto& [b,v] : inc) phi->addIncoming(v,b);
    return phi;
}

llvm::Value* LLVMBackend::cgRetype(const RetypeExpr& e) { return cg(e.expression); }

// ============================================================================
// Statement codegen
// ============================================================================
void LLVMBackend::cgStmt(const std::shared_ptr<Stmt>& s) {
    if (!s) return;
    if (auto* p = dynamic_cast<const VarDeclStmt*>(s.get())) cgVarDecl(*p);
    else if (auto* p = dynamic_cast<const ExpressionStmt*>(s.get())) cg(p->expression);
    else if (auto* p = dynamic_cast<const BlockStmt*>(s.get())) cgBlock(*p);
    else if (auto* p = dynamic_cast<const IfStmt*>(s.get())) cgIf(*p);
    else if (auto* p = dynamic_cast<const WhileStmt*>(s.get())) cgWhile(*p);
    else if (auto* p = dynamic_cast<const ForStmt*>(s.get())) cgFor(*p);
    else if (auto* p = dynamic_cast<const ForInStmt*>(s.get())) cgForIn(*p);
    else if (auto* p = dynamic_cast<const ReturnStmt*>(s.get())) cgReturn(*p);
    else if (auto* p = dynamic_cast<const BreakStmt*>(s.get())) { if (loopExit) builder->CreateBr(loopExit); }
    else if (auto* p = dynamic_cast<const ThrowStmt*>(s.get())) cgThrow(*p);
    else if (auto* p = dynamic_cast<const TryStmt*>(s.get())) cgTry(*p);
    else if (auto* p = dynamic_cast<const UnsafeBlockStmt*>(s.get())) { if (p->block) for (auto& st : p->block->statements) cgStmt(st); }
}

void LLVMBackend::cgVarDecl(const VarDeclStmt& s) {
    auto* v = s.initializer ? cg(s.initializer) : makeNil();
    if (auto* fn = builder->GetInsertBlock()->getParent()) {
        auto* a = allocLocal(fn, s.name.lexeme);
        builder->CreateStore(v, a);
        namedVals[s.name.lexeme] = a;
    }
}

void LLVMBackend::cgBlock(const BlockStmt& s) {
    auto sv = namedVals;
    for (auto& st : s.statements) cgStmt(st);
    namedVals = sv;
}

void LLVMBackend::cgIf(const IfStmt& s) {
    auto* fn = builder->GetInsertBlock()->getParent();
    auto* tb = llvm::BasicBlock::Create(*ctx,"then",fn);
    auto* eb = llvm::BasicBlock::Create(*ctx,"else",fn);
    auto* mg = llvm::BasicBlock::Create(*ctx,"ifm",fn);
    builder->CreateCondBr(isTruthy(cg(s.condition)), tb, eb);
    builder->SetInsertPoint(tb);
    cgStmt(s.thenBranch);
    if (!builder->GetInsertBlock()->getTerminator()) builder->CreateBr(mg);
    builder->SetInsertPoint(eb);
    if (s.elseBranch) cgStmt(s.elseBranch);
    if (!builder->GetInsertBlock()->getTerminator()) builder->CreateBr(mg);
    builder->SetInsertPoint(mg);
}

void LLVMBackend::cgWhile(const WhileStmt& s) {
    auto* fn = builder->GetInsertBlock()->getParent();
    auto* lp = llvm::BasicBlock::Create(*ctx,"wc",fn);
    auto* bd = llvm::BasicBlock::Create(*ctx,"wb",fn);
    auto* en = llvm::BasicBlock::Create(*ctx,"we",fn);
    auto* sv = loopExit; loopExit = en; loopDepth++;
    builder->CreateBr(lp);
    builder->SetInsertPoint(lp);
    builder->CreateCondBr(isTruthy(cg(s.condition)), bd, en);
    builder->SetInsertPoint(bd);
    cgStmt(s.body);
    if (!builder->GetInsertBlock()->getTerminator()) builder->CreateBr(lp);
    builder->SetInsertPoint(en);
    loopExit = sv; loopDepth--;
}

void LLVMBackend::cgFor(const ForStmt& s) {
    auto* fn = builder->GetInsertBlock()->getParent();
    auto sv = namedVals;
    if (s.initializer) cgStmt(s.initializer);
    auto* lp = llvm::BasicBlock::Create(*ctx,"fc",fn);
    auto* bd = llvm::BasicBlock::Create(*ctx,"fb",fn);
    auto* en = llvm::BasicBlock::Create(*ctx,"fe",fn);
    auto* sv2 = loopExit; loopExit = en; loopDepth++;
    builder->CreateBr(lp);
    builder->SetInsertPoint(lp);
    if (s.condition) builder->CreateCondBr(isTruthy(cg(s.condition)), bd, en);
    else builder->CreateBr(bd);
    builder->SetInsertPoint(bd);
    cgStmt(s.body);
    if (!builder->GetInsertBlock()->getTerminator()) {
        if (s.increment) cg(s.increment);
        builder->CreateBr(lp);
    }
    builder->SetInsertPoint(en);
    loopExit = sv2; loopDepth--; namedVals = sv;
}

void LLVMBackend::cgForIn(const ForInStmt& s) {
    auto* fn = builder->GetInsertBlock()->getParent();
    auto sv = namedVals;
    auto* iter = cg(s.collection);
    auto* len = callRt(rt->getFuncLen(),{iter});
    auto* cnt = getI64(len);
    auto* ac = allocLocal(fn, s.name.lexeme);
    namedVals[s.name.lexeme] = ac;
    auto* lp = llvm::BasicBlock::Create(*ctx,"fic",fn);
    auto* bd = llvm::BasicBlock::Create(*ctx,"fib",fn);
    auto* en = llvm::BasicBlock::Create(*ctx,"fie",fn);
    auto* sv2 = loopExit; loopExit = en; loopDepth++;
    auto* ia = allocLocal(fn,"__fi");
    builder->CreateStore(llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx),0), ia);
    builder->CreateBr(lp);
    builder->SetInsertPoint(lp);
    auto* i = builder->CreateLoad(llvm::Type::getInt64Ty(*ctx), ia, "i");
    builder->CreateCondBr(builder->CreateICmpSLT(i, cnt), bd, en);
    builder->SetInsertPoint(bd);
    builder->CreateStore(callRt(rt->getFuncListGet(),{iter, makeI64(i)}), ac);
    cgStmt(s.body);
    if (!builder->GetInsertBlock()->getTerminator()) {
        builder->CreateStore(builder->CreateAdd(i, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx),1)), ia);
        builder->CreateBr(lp);
    }
    builder->SetInsertPoint(en);
    loopExit = sv2; loopDepth--; namedVals = sv;
}

void LLVMBackend::cgReturn(const ReturnStmt& s) {
    builder->CreateRet(s.value ? cg(s.value) : makeNil());
}

void LLVMBackend::cgThrow(const ThrowStmt& s) {
    callRt(rt->getFuncThrow(), {callRt(rt->getFuncExceptionNew(), {cg(s.expression)})});
}

void LLVMBackend::cgTry(const TryStmt& s) {
    auto* fn = builder->GetInsertBlock()->getParent();
    auto* tryBB = llvm::BasicBlock::Create(*ctx,"try_body",fn);
    auto* catchBB = llvm::BasicBlock::Create(*ctx,"catch",fn);
    auto* afterAll = llvm::BasicBlock::Create(*ctx,"after_try",fn);
    auto* frameType = llvm::StructType::create(*ctx,
        {llvm::ArrayType::get(llvm::Type::getInt8Ty(*ctx),200),
         llvm::PointerType::get(*ctx, 0)}, "EF");
    auto* frame = builder->CreateAlloca(frameType);
    // __ang_try_begin: push exception frame via setjmp.
    // Returns 0 on first call (normal entry), non-zero if longjmp'd (exception thrown)
    auto* sr = callRt(rt->getFuncTryBegin(),
        {builder->CreateBitCast(frame, llvm::PointerType::get(*ctx, 0))});
    // sr == 0 → enter try block; sr != 0 → exception was thrown, enter catch
    builder->CreateCondBr(
        builder->CreateICmpEQ(sr, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx),0)),
        tryBB, catchBB);

    // --- Try block ---
    builder->SetInsertPoint(tryBB);
    cgStmt(s.tryBlock);
    if (!builder->GetInsertBlock()->getTerminator()) {
        callRt(rt->getFuncTryEnd(),{});
        builder->CreateBr(afterAll);
    }

    // --- Catch block (only reached when exception was thrown) ---
    builder->SetInsertPoint(catchBB);
    if (s.catchBlock) {
        auto* exc = builder->CreateLoad(objType, rt->getCurrentException(), "exc");
        auto sv = namedVals;
        auto* ea = allocLocal(fn,"__exc");
        builder->CreateStore(exc, ea);
        namedVals["__exception"] = ea;
        cgStmt(s.catchBlock);
        namedVals = sv;
    }
    if (!builder->GetInsertBlock()->getTerminator()) {
        builder->CreateBr(afterAll);
    }

    // --- After try/catch ---
    builder->SetInsertPoint(afterAll);
}

} // namespace angara