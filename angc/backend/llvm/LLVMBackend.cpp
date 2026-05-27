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

LLVMBackend::~LLVMBackend() = default;

LLVMBackend::LLVMBackend(TypeChecker& tc, ErrorHandler& eh, const std::string& target_triple, bool freestanding, bool dump_ir)
    : m_type_checker(tc), m_errorHandler(eh), m_freestanding(freestanding), m_dump_ir(dump_ir) {
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
        if (auto tm = std::unique_ptr<llvm::TargetMachine>(t->createTargetMachine(targetTriple,"generic","",opt,std::nullopt)))
            mod->setDataLayout(tm->createDataLayout());
    }
    rt = std::make_unique<RuntimeBuilder>(*ctx, *mod, *builder, m_freestanding);
    rt->generateRuntime();
    objType = rt->getAngaraObjType();
}

bool LLVMBackend::generate(const std::vector<std::shared_ptr<Stmt>>& stmts,
    const std::shared_ptr<ModuleType>& moduleType, std::vector<std::string>& allMods) {
    moduleName = moduleType ? moduleType->name : "main";
    codegenTopLevelDecls(stmts);
    std::string user_main_name = mangle(moduleName, "main");
    if (mod->getFunction(user_main_name)) {
        codegenMainFunction(stmts, moduleName, allMods);
    }
    std::string base = "ang_" + moduleName;
    if (m_dump_ir) {
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

std::string LLVMBackend::mangle(const std::string& m, const std::string& n) { return "__ang_"+m+"_"+sanitize(n); }
std::string LLVMBackend::mangleMethod(const std::string& c, const std::string& m) { return "__ang_"+sanitize(c)+"_"+sanitize(m); }
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
    llvm::Value* masked = builder->CreateZExt(truncated, llvm::Type::getInt64Ty(*ctx), "masked");

    return makeI64(masked);
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
    if (type->kind == TypeKind::DATA) {
        // Foreign data struct pointer
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
            // Extract char* from AngaraString: payload -> AngaraString* -> GEP(field 2) -> load char*
            auto* str_ptr = builder->CreateIntToPtr(getI64(obj), llvm::PointerType::get(*ctx, 0));
            auto* string_type = rt->getStringType();
            auto* chars_ptr = builder->CreateStructGEP(string_type, str_ptr, 2);
            return builder->CreateLoad(llvm::PointerType::get(*ctx, 0), chars_ptr);
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

    return getI64(obj);
}

llvm::Value* LLVMBackend::marshalCToAngara(llvm::Value* c_val, const std::shared_ptr<Type>& type) {
    if (!type) return makeI64(c_val);

    if (type->kind == TypeKind::PRIMITIVE) {
        const auto& n = type->toString();
        if (n == "bool") return makeBool(c_val);
        if (n == "f64")  return makeF64(c_val);
        if (n == "f32")  return makeF64(builder->CreateFPExt(c_val, llvm::Type::getDoubleTy(*ctx)));
        if (n == "string") return callRtByName("__ang_string_from_c", {c_val});
        // Integer types: zext/sext to i64
        auto* cty = resolveCFieldType(type);
        if (cty->getIntegerBitWidth() < 64) {
            return makeI64(builder->CreateZExt(c_val, llvm::Type::getInt64Ty(*ctx)));
        }
        return makeI64(c_val);
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
            auto* null_finalizer = llvm::ConstantPointerNull::get(llvm::PointerType::get(*ctx, 0));
            auto* native_obj = callRtByName("__ang_api_native_instance_new",
                                             {c_val, null_finalizer, name_str});
            auto* wrap_end_bb = builder->GetInsertBlock();
            builder->CreateBr(merge_bb);

            builder->SetInsertPoint(merge_bb);
            auto* phi = builder->CreatePHI(objType, 2);
            phi->addIncoming(makeNil(), entry_bb);
            phi->addIncoming(native_obj, wrap_end_bb);
            return phi;
        }
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

    // GEP to get a pointer to the field
    auto* field_ptr = builder->CreateStructGEP(struct_type, struct_ptr, field_index);

    // Determine the field type and load + marshal
    auto field_it = data_type->fields.find(e.name.lexeme);
    if (field_it == data_type->fields.end()) return makeNil();
    auto& field_type = field_it->second.type;

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
    bool returnsVoid = func_type->return_type->kind == TypeKind::NIL;

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
