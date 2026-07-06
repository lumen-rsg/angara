#include "LLVMBackend.h"
#include "RuntimeBuilder.h"
#include <llvm/IR/Intrinsics.h>
#include <functional>

namespace angara {

llvm::Value* LLVMBackend::cg(const std::shared_ptr<Expr>& e) {
    if (!e) return makeNil();
    if (auto* p = dynamic_cast<const Literal*>(e.get())) return cgLiteral(*p);
    if (auto* p = dynamic_cast<const Binary*>(e.get())) return cgBinary(*p);
    if (auto* p = dynamic_cast<const Unary*>(e.get())) return cgUnary(*p);
    if (auto* p = dynamic_cast<const Grouping*>(e.get())) return cg(p->expression);
    if (auto* p = dynamic_cast<const VarExpr*>(e.get())) {
        auto type_it = m_type_checker.getExpressionTypes().find(e.get());
        if (type_it != m_type_checker.getExpressionTypes().end() &&
            type_it->second->kind == TypeKind::FUNCTION) {
            auto func_type = std::dynamic_pointer_cast<FunctionType>(type_it->second);
            if (namedVals.find(sanitize(p->name.lexeme)) != namedVals.end()) {
                return loadVar(p->name.lexeme);
            }
            std::string mangled = mangle(moduleName, p->name.lexeme);
            int arity = (int)func_type->param_types.size();

            std::string wrapper_name = "__ang_wrap_" + sanitize(p->name.lexeme) + "_" + std::to_string(m_lambda_counter++);

            auto* i32_ty = llvm::Type::getInt32Ty(*ctx);
            auto* ptr_ty = llvm::PointerType::get(*ctx, 0);
            auto* wrapper_fn_type = llvm::FunctionType::get(objType, {i32_ty, ptr_ty}, false);
            auto* wrapper_fn = llvm::Function::Create(wrapper_fn_type, llvm::Function::PrivateLinkage,
                                                       wrapper_name, mod.get());

            auto* saved_insert_block = builder->GetInsertBlock();
            auto* entry = llvm::BasicBlock::Create(*ctx, "entry", wrapper_fn);
            builder->SetInsertPoint(entry);

            auto* args_ptr = wrapper_fn->arg_begin() + 1;

            std::vector<llvm::Value*> direct_args;
            auto* wrap_arr_type = llvm::ArrayType::get(objType, arity);
            for (int i = 0; i < arity; i++) {
                auto* elem_ptr = builder->CreateGEP(wrap_arr_type, args_ptr,
                    {llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), 0),
                     llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), i)});
                direct_args.push_back(builder->CreateLoad(objType, elem_ptr));
            }

            llvm::Function* target_fn = this->mod->getFunction(mangled);
            if (!target_fn) target_fn = this->mod->getFunction("__ang_" + sanitize(p->name.lexeme));

            if (target_fn) {
                auto raw_it = m_raw_functions.find(mangled);
                if (raw_it != m_raw_functions.end()) {
                    // Target uses a raw (unboxed) LLVM signature: unbox each
                    // argument and box the result so the wrapper still returns
                    // an AngaraObject.
                    const auto& info = raw_it->second;
                    std::vector<llvm::Value*> raw_args;
                    for (int i = 0; i < arity; i++) {
                        auto kind = (i < (int)info.param_kinds.size())
                            ? info.param_kinds[i] : LocalKind::BOXED;
                        raw_args.push_back(unboxToRaw(direct_args[i], kind));
                    }
                    auto* raw_result = builder->CreateCall(target_fn, raw_args);
                    builder->CreateRet(boxRaw(raw_result, info.return_kind));
                } else {
                    auto* result = builder->CreateCall(target_fn, direct_args);
                    builder->CreateRet(result);
                }
            } else {
                auto* direct_fn_type = llvm::FunctionType::get(objType,
                    std::vector<llvm::Type*>(arity, objType), false);
                auto* forward_fn = llvm::Function::Create(direct_fn_type, llvm::Function::ExternalLinkage,
                                                           mangled, mod.get());
                auto* result = builder->CreateCall(forward_fn, direct_args);
                builder->CreateRet(result);
            }

            if (saved_insert_block) builder->SetInsertPoint(saved_insert_block);

            return callRtByName("__ang_closure_new", {
                wrapper_fn,
                llvm::ConstantInt::get(i32_ty, arity),
                llvm::ConstantInt::get(llvm::Type::getInt1Ty(*ctx), 0),
                llvm::ConstantPointerNull::get(llvm::PointerType::get(*ctx, 0)),
                llvm::ConstantInt::get(i32_ty, 0)  // env_count = 0 (no captures)
            });
        }
        return loadVar(p->name.lexeme);
    }
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
    if (auto* p = dynamic_cast<const CastExpr*>(e.get())) return cgCast(*p);
    if (auto* p = dynamic_cast<const DerefExpr*>(e.get())) return cgDeref(*p);
    if (auto* p = dynamic_cast<const MatchExpr*>(e.get())) return cgMatch(*p);
    if (auto* p = dynamic_cast<const LambdaExpr*>(e.get())) return cgLambda(*p);
    if (auto* p = dynamic_cast<const RangeExpr*>(e.get())) return cgRange(*p);
    if (auto* p = dynamic_cast<const InterpStringExpr*>(e.get())) return cgInterpString(*p);
    if (auto* p = dynamic_cast<const TupleExpr*>(e.get())) return cgTuple(*p);  // LANG-10
    if (auto* p = dynamic_cast<const AwaitExpr*>(e.get())) return cgAwait(*p);  // LIB-4
    return makeNil();
}

llvm::Value* LLVMBackend::cgLiteral(const Literal& e) {
    const auto& tok = e.token;
    if (tok.type == TokenType::NIL) return makeNil();
    if (tok.type == TokenType::TRUE) return makeBool(true);
    if (tok.type == TokenType::FALSE) return makeBool(false);
    if (tok.type == TokenType::NUMBER_INT) {
        const auto& s = tok.lexeme;
        int base = 0;          // 0 = auto-detect (0x → hex, 0 → octal, else decimal)
        size_t offset = 0;     // characters to skip past the prefix
        if (s.size() >= 2 && s[0] == '0') {
            if (s[1] == 'b' || s[1] == 'B') { base = 2;  offset = 2; }
            else if (s[1] == 'o' || s[1] == 'O') { base = 8;  offset = 2; }
            // 0x is handled by base 0 (auto-detect)
        }
        try {
            if (offset > 0)
                return makeI64(std::stoll(s.substr(offset), nullptr, base));
            else
                return makeI64(std::stoll(s, nullptr, 0));
        }
        catch (const std::exception&) { return makeI64(static_cast<int64_t>(0)); }
    }
    if (tok.type == TokenType::NUMBER_FLOAT) {
        try { return makeF64(std::stod(tok.lexeme)); }
        catch (const std::exception&) { return makeF64(0.0); }
    }
    if (tok.type == TokenType::STRING)      return makeStr(tok.lexeme);
    if (tok.type == TokenType::RAW_STRING)  return makeStr(tok.lexeme);   // LANG-6
    if (tok.type == TokenType::BYTE_STRING) return makeStr(tok.lexeme);   // LANG-6
    // LANG-4: char literal — the lexer stored the resolved code point as a
    // decimal string; emit it as a TAG_I64 integer (char IS a 32-bit int at
    // runtime, per the C/Java model).
    if (tok.type == TokenType::CHAR) {
        try { return makeI64(std::stoll(tok.lexeme)); }
        catch (const std::exception&) { return makeI64(static_cast<int64_t>(0)); }
    }
    return makeNil();
}

// LANG-4: type-aware string conversion. Routes a char-typed operand through
// __ang_char_to_string (renders the code point as the glyph) and everything
// else through __ang_to_string. `src` is the source expression whose static
// type is consulted.
llvm::Value* LLVMBackend::toStrTyped(const std::shared_ptr<Expr>& src) {
    auto* val = cg(src);
    auto it = m_type_checker.getExpressionTypes().find(src.get());
    if (it != m_type_checker.getExpressionTypes().end() && isChar(it->second)) {
        return callRtByName("__ang_char_to_string", {val});
    }
    return callRtByName("__ang_to_string", {val});
}

llvm::Value* LLVMBackend::cgBinary(const Binary& e) {
    auto* l = cg(e.left), *r = cg(e.right);
    if (!l||!r) return makeNil();

    // Compile-time type lookup for fast-path arithmetic
    auto lt_it = m_type_checker.getExpressionTypes().find(e.left.get());
    auto rt_it = m_type_checker.getExpressionTypes().find(e.right.get());
    bool left_typed = lt_it != m_type_checker.getExpressionTypes().end();
    bool right_typed = rt_it != m_type_checker.getExpressionTypes().end();
    bool types_known = left_typed && right_typed;

    // RT-5: integer wrap flags. NSW/NUW tell LLVM overflow won't happen,
    // unlocking int optimizations (overflow becomes poison/UB). No Angara
    // syntax relies on wrap, so this is safe. NUW only when both operands
    // are unsigned; NSW when at least one is signed (the conservative flag
    // for mixed/unknown signedness — the generic tag-dispatch path uses
    // NSW-only since runtime sign is unknown).
    bool int_nuw = false, int_nsw = false;
    if (types_known) {
        auto& ltype = lt_it->second;
        auto& rtype = rt_it->second;
        if (isInteger(ltype) && isInteger(rtype)) {
            bool both_uns = isUnsignedIntType(ltype) && isUnsignedIntType(rtype);
            int_nuw = both_uns;
            int_nsw = true;   // safe for signed, unsigned (no wrap), and mixed
        }
    }

    // Helper: convert an AngaraObject to double based on its runtime tag.
    // If TAG_F64, bitcast payload; if TAG_I64, SIToFP convert.
    auto* f64_ty = llvm::Type::getDoubleTy(*ctx);
    auto* i32_ty = llvm::Type::getInt32Ty(*ctx);
    auto toDouble = [&](llvm::Value* val, llvm::Value* tag) -> llvm::Value* {
        return builder->CreateSelect(
            builder->CreateICmpEQ(tag, llvm::ConstantInt::get(i32_ty, TAG_F64)),
            getF64(val),
            builder->CreateSIToFP(getI64(val), f64_ty));
    };

    // SIMD-5: helper for vector binary operations (vector+vector and scalar+vector)
    auto vectorOp = [&](bool is_add, bool is_mul, bool is_div) -> std::optional<llvm::Value*> {
        if (!types_known) return std::nullopt;
        auto& ltype = lt_it->second;
        auto& rtype = rt_it->second;
        bool left_vec = ltype->kind == TypeKind::VECTOR;
        bool right_vec = rtype->kind == TypeKind::VECTOR;

        // Vector ±/* vector
        if (left_vec && right_vec) {
            auto lvec = std::dynamic_pointer_cast<VectorType>(ltype);
            auto rvec = std::dynamic_pointer_cast<VectorType>(rtype);
            if (!sameType(ltype, rtype)) return std::nullopt;
            auto* raw_l = extractVector(l, *lvec);
            auto* raw_r = extractVector(r, *rvec);
            llvm::Value* result;
            if (isFloat(lvec->element_type) || isFloat(rvec->element_type)) {
                if (is_add) result = builder->CreateFAdd(raw_l, raw_r, "vecadd");
                else if (is_mul) result = builder->CreateFMul(raw_l, raw_r, "vecmul");
                else if (is_div) result = builder->CreateFDiv(raw_l, raw_r, "vecdiv");
                else result = builder->CreateFSub(raw_l, raw_r, "vecsub");
            } else {
                if (is_add) result = builder->CreateAdd(raw_l, raw_r, "vecadd");
                else if (is_mul) result = builder->CreateMul(raw_l, raw_r, "vecmul");
                else if (is_div) result = builder->CreateSDiv(raw_l, raw_r, "vecdiv");
                else result = builder->CreateSub(raw_l, raw_r, "vecsub");
            }
            return makeVector(result, *lvec);
        }

        // Vector ±/* scalar (broadcast scalar to vector)
        if (left_vec && isNumeric(rtype)) {
            auto lvec = std::dynamic_pointer_cast<VectorType>(ltype);
            auto* raw_l = extractVector(l, *lvec);
            auto* scalar = isFloat(rtype) ? getF64(r) :
                isInteger(rtype) ? getI64(r) : nullptr;
            if (!scalar) return std::nullopt;
            // Broadcast: for float vectors, SIToFP int scalar; for int vectors, SIToFP... no, just use same type
            auto* vec_ty = llvmTypeForVector(*lvec);
            if (isFloat(lvec->element_type) && !isFloat(rtype)) {
                scalar = builder->CreateSIToFP(scalar, isFloat(lvec->element_type) ?
                    static_cast<llvm::Type*>(llvm::Type::getFloatTy(*ctx)) :
                    static_cast<llvm::Type*>(llvm::Type::getDoubleTy(*ctx)));
            }
            // Check if scalar needs splat
            auto* splat = [&]() -> llvm::Value* {
                if (auto* fvt = llvm::dyn_cast<llvm::FixedVectorType>(vec_ty)) {
                    // Use insertelement+shufflevector to create a splat
                    auto* undef_vec = llvm::UndefValue::get(fvt);
                    auto* ins = builder->CreateInsertElement(undef_vec, scalar, (uint64_t)0);
                    auto* zero = llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), 0);
                    auto* mask = llvm::ConstantVector::getSplat(
                        llvm::ElementCount::getFixed(lvec->size), zero);
                    return builder->CreateShuffleVector(ins, undef_vec, mask);
                }
                return scalar;
            }();
            llvm::Value* result;
            if (isFloat(lvec->element_type)) {
                if (is_add) result = builder->CreateFAdd(raw_l, splat, "vecsadd");
                else if (is_mul) result = builder->CreateFMul(raw_l, splat, "vecsmul");
                else if (is_div) result = builder->CreateFDiv(raw_l, splat, "vecsdiv");
                else result = builder->CreateFSub(raw_l, splat, "vecssub");
            } else {
                if (is_add) result = builder->CreateAdd(raw_l, splat, "vecsadd");
                else if (is_mul) result = builder->CreateMul(raw_l, splat, "vecsmul");
                else if (is_div) result = builder->CreateSDiv(raw_l, splat, "vecsdiv");
                else result = builder->CreateSub(raw_l, splat, "vecssub");
            }
            return makeVector(result, *lvec);
        }

        // Scalar ±/* vector — swap args and recurse via the vector+scalar path
        if (isNumeric(ltype) && right_vec) {
            // Swap operands: scalar * vec and scalar + vec are commutative
            if (is_add || is_mul) {
                // Commutative: just handle as vec op scalar with swapped args
                auto rvec = std::dynamic_pointer_cast<VectorType>(rtype);
                auto* raw_r = extractVector(r, *rvec);
                auto* scalar = isFloat(ltype) ? getF64(l) :
                    isInteger(ltype) ? getI64(l) : nullptr;
                if (!scalar) return std::nullopt;
                auto* vec_ty = llvmTypeForVector(*rvec);
                if (isFloat(rvec->element_type) && !isFloat(ltype)) {
                    scalar = builder->CreateSIToFP(scalar, llvm::Type::getDoubleTy(*ctx));
                }
                auto* undef_vec = llvm::UndefValue::get(llvm::dyn_cast<llvm::FixedVectorType>(vec_ty));
                auto* ins = builder->CreateInsertElement(undef_vec, scalar, (uint64_t)0);
                auto* zero = llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), 0);
                auto* mask = llvm::ConstantVector::getSplat(
                    llvm::ElementCount::getFixed(rvec->size), zero);
                auto* splat = builder->CreateShuffleVector(ins, undef_vec, mask);
                llvm::Value* result;
                if (isFloat(rvec->element_type)) {
                    if (is_add) result = builder->CreateFAdd(splat, raw_r, "vecsadd");
                    else result = builder->CreateFMul(splat, raw_r, "vecsmul");
                } else {
                    if (is_add) result = builder->CreateAdd(splat, raw_r, "vecsadd");
                    else result = builder->CreateMul(splat, raw_r, "vecsmul");
                }
                return makeVector(result, *rvec);
            }
            // scalar - vector: splat scalar, then scalar_splat - vector
            if (!is_add && !is_mul && !is_div) {
                // This is subtraction: scalar - vector
                auto rvec = std::dynamic_pointer_cast<VectorType>(rtype);
                auto* raw_r = extractVector(r, *rvec);
                auto* scalar = isFloat(ltype) ? getF64(l) :
                    isInteger(ltype) ? getI64(l) : nullptr;
                if (!scalar) return std::nullopt;
                auto* vec_ty = llvmTypeForVector(*rvec);
                if (isFloat(rvec->element_type) && !isFloat(ltype)) {
                    scalar = builder->CreateSIToFP(scalar, llvm::Type::getDoubleTy(*ctx));
                }
                auto* undef_vec = llvm::UndefValue::get(llvm::dyn_cast<llvm::FixedVectorType>(vec_ty));
                auto* ins = builder->CreateInsertElement(undef_vec, scalar, (uint64_t)0);
                auto* zero = llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), 0);
                auto* mask = llvm::ConstantVector::getSplat(
                    llvm::ElementCount::getFixed(rvec->size), zero);
                auto* splat = builder->CreateShuffleVector(ins, undef_vec, mask);
                llvm::Value* result;
                if (isFloat(rvec->element_type)) {
                    result = builder->CreateFSub(splat, raw_r, "scalar_minus_vec");
                } else {
                    result = builder->CreateSub(splat, raw_r, "scalar_minus_vec");
                }
                return makeVector(result, *rvec);
            }
            // scalar / vec: not supported (nonsensical)
            return std::nullopt;
        }

        return std::nullopt;
    };

    switch (e.op.type) {
        case TokenType::PLUS: {
            // SIMD-5: vector fast path
            if (auto vec_result = vectorOp(true, false, false)) return *vec_result;
            // Fast path: typed primitives — skip tag dispatch entirely
            if (types_known) {
                auto& ltype = lt_it->second;
                auto& rtype = rt_it->second;
                if (isInteger(ltype) && isInteger(rtype))
                    return makeI64(builder->CreateAdd(getI64(l), getI64(r), "", int_nuw, int_nsw));
                if (isFloat(ltype) || isFloat(rtype)) {
                    auto* ld = isFloat(ltype) ? getF64(l) : builder->CreateSIToFP(getI64(l), f64_ty);
                    auto* rd = isFloat(rtype) ? getF64(r) : builder->CreateSIToFP(getI64(r), f64_ty);
                    return makeF64(builder->CreateFAdd(ld, rd));
                }
            }
            // If the type checker knows either operand is a string, skip the
            // runtime tag dispatch and call __ang_string_concat directly.
            // (Under the LANG-4 integer model, char is not a string — char+char
            // is integer arithmetic handled by the numeric fast path above, and
            // string+char is a type error, so no char pre-conversion is needed
            // here. Users concat a char into a string via explicit string(c).)
            {
                auto lt = m_type_checker.getExpressionTypes().find(e.left.get());
                auto rt = m_type_checker.getExpressionTypes().find(e.right.get());
                bool left_is_string = (lt != m_type_checker.getExpressionTypes().end() &&
                                       lt->second->toString() == "string");
                bool right_is_string = (rt != m_type_checker.getExpressionTypes().end() &&
                                        rt->second->toString() == "string");
                if (left_is_string || right_is_string) {
                    return callRtByName("__ang_string_concat", {l, r});
                }
            }
            // LANG-13: check for user-defined opAdd on the left type.
            {
                auto lt = m_type_checker.getExpressionTypes().find(e.left.get());
                if (lt != m_type_checker.getExpressionTypes().end()) {
                    std::string mname = resolveMethodForType(lt->second, "opAdd");
                    if (!mname.empty()) {
                        llvm::Function* mf = mod->getFunction(mname);
                        if (mf) return builder->CreateCall(mf, {l, r});
                    }
                }
            }
            auto* lTag = getTag(l);
            auto* rTag = getTag(r);
            auto* bothI64 = builder->CreateAnd(
                builder->CreateICmpEQ(lTag, llvm::ConstantInt::get(i32_ty, TAG_I64)),
                builder->CreateICmpEQ(rTag, llvm::ConstantInt::get(i32_ty, TAG_I64)));
            auto* eitherF64 = builder->CreateOr(
                builder->CreateICmpEQ(lTag, llvm::ConstantInt::get(i32_ty, TAG_F64)),
                builder->CreateICmpEQ(rTag, llvm::ConstantInt::get(i32_ty, TAG_F64)));
            auto* fn = builder->GetInsertBlock()->getParent();
            auto* iaddBB = llvm::BasicBlock::Create(*ctx,"iadd",fn);
            auto* faddBB = llvm::BasicBlock::Create(*ctx,"fadd",fn);
            auto* saddBB = llvm::BasicBlock::Create(*ctx,"sadd",fn);
            auto* maddBB = llvm::BasicBlock::Create(*ctx,"madd",fn);
            auto* checkF64BB = llvm::BasicBlock::Create(*ctx,"chkf",fn);
            builder->CreateCondBr(bothI64, iaddBB, checkF64BB);
            builder->SetInsertPoint(iaddBB);
            auto* ia = makeI64(builder->CreateAdd(getI64(l),getI64(r), "", /*HasNUW*/false, /*HasNSW*/true));
            iaddBB = builder->GetInsertBlock();
            builder->CreateBr(maddBB);
            builder->SetInsertPoint(checkF64BB);
            builder->CreateCondBr(eitherF64, faddBB, saddBB);
            builder->SetInsertPoint(faddBB);
            auto* fa = makeF64(builder->CreateFAdd(toDouble(l, lTag), toDouble(r, rTag)));
            faddBB = builder->GetInsertBlock();
            builder->CreateBr(maddBB);
            builder->SetInsertPoint(saddBB);
            auto* sa = callRtByName("__ang_string_concat",{l,r});
            saddBB = builder->GetInsertBlock();
            builder->CreateBr(maddBB);
            builder->SetInsertPoint(maddBB);
            auto* phi = builder->CreatePHI(objType,3);
            phi->addIncoming(ia,iaddBB); phi->addIncoming(fa,faddBB); phi->addIncoming(sa,saddBB);
            return phi;
        }
        case TokenType::MINUS: {
            // SIMD-5: vector fast path
            if (auto vec_result = vectorOp(false, false, false)) return *vec_result;
            // Fast path: typed primitives
            if (types_known) {
                auto& ltype = lt_it->second;
                auto& rtype = rt_it->second;
                if (isInteger(ltype) && isInteger(rtype))
                    return makeI64(builder->CreateSub(getI64(l), getI64(r), "", int_nuw, int_nsw));
                if (isFloat(ltype) || isFloat(rtype)) {
                    auto* ld = isFloat(ltype) ? getF64(l) : builder->CreateSIToFP(getI64(l), f64_ty);
                    auto* rd = isFloat(rtype) ? getF64(r) : builder->CreateSIToFP(getI64(r), f64_ty);
                    return makeF64(builder->CreateFSub(ld, rd));
                }
            }
            // LANG-13: check for user-defined opSub on the left type.
            {
                auto lt = m_type_checker.getExpressionTypes().find(e.left.get());
                if (lt != m_type_checker.getExpressionTypes().end()) {
                    std::string mname = resolveMethodForType(lt->second, "opSub");
                    if (!mname.empty()) {
                        llvm::Function* mf = mod->getFunction(mname);
                        if (mf) return builder->CreateCall(mf, {l, r});
                    }
                }
            }
            auto* lTag = getTag(l);
            auto* rTag = getTag(r);
            auto* eitherF64 = builder->CreateOr(
                builder->CreateICmpEQ(lTag, llvm::ConstantInt::get(i32_ty, TAG_F64)),
                builder->CreateICmpEQ(rTag, llvm::ConstantInt::get(i32_ty, TAG_F64)));
            auto* fn = builder->GetInsertBlock()->getParent();
            auto* fsubBB = llvm::BasicBlock::Create(*ctx,"fsub",fn);
            auto* isubBB = llvm::BasicBlock::Create(*ctx,"isub",fn);
            auto* msubBB = llvm::BasicBlock::Create(*ctx,"msub",fn);
            builder->CreateCondBr(eitherF64, fsubBB, isubBB);
            builder->SetInsertPoint(fsubBB);
            auto* fa = makeF64(builder->CreateFSub(toDouble(l, lTag),toDouble(r, rTag)));
            fsubBB = builder->GetInsertBlock();
            builder->CreateBr(msubBB);
            builder->SetInsertPoint(isubBB);
            auto* ia = makeI64(builder->CreateSub(getI64(l),getI64(r), "", /*HasNUW*/false, /*HasNSW*/true));
            isubBB = builder->GetInsertBlock();
            builder->CreateBr(msubBB);
            builder->SetInsertPoint(msubBB);
            auto* phi = builder->CreatePHI(objType,2);
            phi->addIncoming(fa,fsubBB); phi->addIncoming(ia,isubBB);
            return phi;
        }
        case TokenType::STAR: {
            // SIMD-5: vector fast path
            if (auto vec_result = vectorOp(false, true, false)) return *vec_result;
            // Fast path: typed primitives
            if (types_known) {
                auto& ltype = lt_it->second;
                auto& rtype = rt_it->second;
                if (isInteger(ltype) && isInteger(rtype))
                    return makeI64(builder->CreateMul(getI64(l), getI64(r), "", int_nuw, int_nsw));
                if (isFloat(ltype) || isFloat(rtype)) {
                    auto* ld = isFloat(ltype) ? getF64(l) : builder->CreateSIToFP(getI64(l), f64_ty);
                    auto* rd = isFloat(rtype) ? getF64(r) : builder->CreateSIToFP(getI64(r), f64_ty);
                    return makeF64(builder->CreateFMul(ld, rd));
                }
            }
            {
                auto lt = m_type_checker.getExpressionTypes().find(e.left.get());
                if (lt != m_type_checker.getExpressionTypes().end() && lt->second->toString() == "string") {
                    return callRtByName("__ang_string_repeat", {l, r});
                }
            }
            // LANG-13: check for user-defined opMul on the left type.
            {
                auto lt = m_type_checker.getExpressionTypes().find(e.left.get());
                if (lt != m_type_checker.getExpressionTypes().end()) {
                    std::string mname = resolveMethodForType(lt->second, "opMul");
                    if (!mname.empty()) {
                        llvm::Function* mf = mod->getFunction(mname);
                        if (mf) return builder->CreateCall(mf, {l, r});
                    }
                }
            }
            auto* lTag = getTag(l);
            auto* rTag = getTag(r);
            auto* eitherF64 = builder->CreateOr(
                builder->CreateICmpEQ(lTag, llvm::ConstantInt::get(i32_ty, TAG_F64)),
                builder->CreateICmpEQ(rTag, llvm::ConstantInt::get(i32_ty, TAG_F64)));
            auto* fn = builder->GetInsertBlock()->getParent();
            auto* fmulBB = llvm::BasicBlock::Create(*ctx,"fmul",fn);
            auto* imulBB = llvm::BasicBlock::Create(*ctx,"imul",fn);
            auto* mmulBB = llvm::BasicBlock::Create(*ctx,"mmul",fn);
            builder->CreateCondBr(eitherF64, fmulBB, imulBB);
            builder->SetInsertPoint(fmulBB);
            auto* fa = makeF64(builder->CreateFMul(toDouble(l, lTag),toDouble(r, rTag)));
            fmulBB = builder->GetInsertBlock();
            builder->CreateBr(mmulBB);
            builder->SetInsertPoint(imulBB);
            auto* ia = makeI64(builder->CreateMul(getI64(l),getI64(r), "", /*HasNUW*/false, /*HasNSW*/true));
            imulBB = builder->GetInsertBlock();
            builder->CreateBr(mmulBB);
            builder->SetInsertPoint(mmulBB);
            auto* phi = builder->CreatePHI(objType,2);
            phi->addIncoming(fa,fmulBB); phi->addIncoming(ia,imulBB);
            return phi;
        }
        case TokenType::SLASH: {
            // SIMD-5: vector fast path
            if (auto vec_result = vectorOp(false, false, true)) return *vec_result;
            // Fast path: typed primitives
            if (types_known) {
                auto& ltype = lt_it->second;
                auto& rtype = rt_it->second;
                if (isInteger(ltype) && isInteger(rtype)) {
                    bool unsign = isUnsignedIntType(ltype) || isUnsignedIntType(rtype);
                    // H12: runtime zero-check before integer division
                    auto* rval = getI64(r);
                    auto* zero = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), 0);
                    auto* is_zero = builder->CreateICmpEQ(rval, zero);
                    auto* parent_fn = builder->GetInsertBlock()->getParent();
                    auto* err_bb = llvm::BasicBlock::Create(*ctx, "div_zero_err", parent_fn);
                    auto* ok_bb = llvm::BasicBlock::Create(*ctx, "div_zero_ok", parent_fn);
                    builder->CreateCondBr(is_zero, err_bb, ok_bb);
                    builder->SetInsertPoint(err_bb);
                    auto* err_msg = builder->CreateGlobalString("division by zero");
                    callRtByName("__ang_api_throw_error", {err_msg});
                    builder->CreateUnreachable();
                    builder->SetInsertPoint(ok_bb);
                    return makeI64(unsign ? builder->CreateUDiv(getI64(l), rval)
                                          : builder->CreateSDiv(getI64(l), rval));
                }
                if (isFloat(ltype) || isFloat(rtype)) {
                    auto* ld = isFloat(ltype) ? getF64(l) : builder->CreateSIToFP(getI64(l), f64_ty);
                    auto* rd = isFloat(rtype) ? getF64(r) : builder->CreateSIToFP(getI64(r), f64_ty);
                    return makeF64(builder->CreateFDiv(ld, rd));
                }
            }
            // LANG-13: check for user-defined opDiv on the left type.
            {
                auto lt = m_type_checker.getExpressionTypes().find(e.left.get());
                if (lt != m_type_checker.getExpressionTypes().end()) {
                    std::string mname = resolveMethodForType(lt->second, "opDiv");
                    if (!mname.empty()) {
                        llvm::Function* mf = mod->getFunction(mname);
                        if (mf) return builder->CreateCall(mf, {l, r});
                    }
                }
            }
            auto* lTag = getTag(l);
            auto* rTag = getTag(r);
            auto* eitherF64 = builder->CreateOr(
                builder->CreateICmpEQ(lTag, llvm::ConstantInt::get(i32_ty, TAG_F64)),
                builder->CreateICmpEQ(rTag, llvm::ConstantInt::get(i32_ty, TAG_F64)));
            auto* fn = builder->GetInsertBlock()->getParent();
            auto* fdivBB = llvm::BasicBlock::Create(*ctx,"fdiv",fn);
            auto* idivBB = llvm::BasicBlock::Create(*ctx,"idiv",fn);
            auto* mdivBB = llvm::BasicBlock::Create(*ctx,"mdiv",fn);
            builder->CreateCondBr(eitherF64, fdivBB, idivBB);
            builder->SetInsertPoint(fdivBB);
            auto* fa = makeF64(builder->CreateFDiv(toDouble(l, lTag),toDouble(r, rTag)));
            fdivBB = builder->GetInsertBlock();
            builder->CreateBr(mdivBB);
            builder->SetInsertPoint(idivBB);
            // H12: runtime zero-check before integer division
            {
                auto* rval = getI64(r);
                auto* zero = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), 0);
                auto* is_zero = builder->CreateICmpEQ(rval, zero);
                auto* err_bb = llvm::BasicBlock::Create(*ctx, "div_zero_err", fn);
                auto* ok_bb = llvm::BasicBlock::Create(*ctx, "div_zero_ok", fn);
                builder->CreateCondBr(is_zero, err_bb, ok_bb);
                builder->SetInsertPoint(err_bb);
                auto* err_msg = builder->CreateGlobalString("division by zero");
                callRtByName("__ang_api_throw_error", {err_msg});
                builder->CreateUnreachable();
                builder->SetInsertPoint(ok_bb);
            }
            auto lt = m_type_checker.getExpressionTypes().find(e.left.get());
            auto rt2 = m_type_checker.getExpressionTypes().find(e.right.get());
            bool unsigned_div = (lt != m_type_checker.getExpressionTypes().end() && isUnsignedIntType(lt->second)) ||
                                (rt2 != m_type_checker.getExpressionTypes().end() && isUnsignedIntType(rt2->second));
            auto* ia = makeI64(unsigned_div ? builder->CreateUDiv(getI64(l),getI64(r))
                                            : builder->CreateSDiv(getI64(l),getI64(r)));
            idivBB = builder->GetInsertBlock();
            builder->CreateBr(mdivBB);
            builder->SetInsertPoint(mdivBB);
            auto* phi = builder->CreatePHI(objType,2);
            phi->addIncoming(fa,fdivBB); phi->addIncoming(ia,idivBB);
            return phi;
        }
        case TokenType::PERCENT: {
            // Fast path: typed primitives
            if (types_known) {
                auto& ltype = lt_it->second;
                auto& rtype = rt_it->second;
                if (isInteger(ltype) && isInteger(rtype)) {
                    bool unsign = isUnsignedIntType(ltype) || isUnsignedIntType(rtype);
                    // H12: runtime zero-check before integer modulo
                    auto* rval = getI64(r);
                    auto* zero = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), 0);
                    auto* is_zero = builder->CreateICmpEQ(rval, zero);
                    auto* parent_fn = builder->GetInsertBlock()->getParent();
                    auto* err_bb = llvm::BasicBlock::Create(*ctx, "mod_zero_err", parent_fn);
                    auto* ok_bb = llvm::BasicBlock::Create(*ctx, "mod_zero_ok", parent_fn);
                    builder->CreateCondBr(is_zero, err_bb, ok_bb);
                    builder->SetInsertPoint(err_bb);
                    auto* err_msg = builder->CreateGlobalString("modulo by zero");
                    callRtByName("__ang_api_throw_error", {err_msg});
                    builder->CreateUnreachable();
                    builder->SetInsertPoint(ok_bb);
                    return makeI64(unsign ? builder->CreateURem(getI64(l), rval)
                                          : builder->CreateSRem(getI64(l), rval));
                }
                if (isFloat(ltype) || isFloat(rtype)) {
                    auto* ld = isFloat(ltype) ? getF64(l) : builder->CreateSIToFP(getI64(l), f64_ty);
                    auto* rd = isFloat(rtype) ? getF64(r) : builder->CreateSIToFP(getI64(r), f64_ty);
                    return makeF64(builder->CreateFRem(ld, rd));
                }
            }
            // LANG-13: check for user-defined opRem on the left type.
            {
                auto lt = m_type_checker.getExpressionTypes().find(e.left.get());
                if (lt != m_type_checker.getExpressionTypes().end()) {
                    std::string mname = resolveMethodForType(lt->second, "opRem");
                    if (!mname.empty()) {
                        llvm::Function* mf = mod->getFunction(mname);
                        if (mf) return builder->CreateCall(mf, {l, r});
                    }
                }
            }
            auto* lTag = getTag(l);
            auto* rTag = getTag(r);
            auto* eitherF64 = builder->CreateOr(
                builder->CreateICmpEQ(lTag, llvm::ConstantInt::get(i32_ty, TAG_F64)),
                builder->CreateICmpEQ(rTag, llvm::ConstantInt::get(i32_ty, TAG_F64)));
            auto* fn = builder->GetInsertBlock()->getParent();
            auto* fmodBB = llvm::BasicBlock::Create(*ctx,"fmod",fn);
            auto* imodBB = llvm::BasicBlock::Create(*ctx,"imod",fn);
            auto* mmodBB = llvm::BasicBlock::Create(*ctx,"mmod",fn);
            builder->CreateCondBr(eitherF64, fmodBB, imodBB);
            builder->SetInsertPoint(fmodBB);
            auto* fa = makeF64(builder->CreateFRem(toDouble(l, lTag),toDouble(r, rTag)));
            fmodBB = builder->GetInsertBlock();
            builder->CreateBr(mmodBB);
            builder->SetInsertPoint(imodBB);
            // H12: runtime zero-check before integer modulo
            {
                auto* rval = getI64(r);
                auto* zero = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), 0);
                auto* is_zero = builder->CreateICmpEQ(rval, zero);
                auto* err_bb = llvm::BasicBlock::Create(*ctx, "mod_zero_err", fn);
                auto* ok_bb = llvm::BasicBlock::Create(*ctx, "mod_zero_ok", fn);
                builder->CreateCondBr(is_zero, err_bb, ok_bb);
                builder->SetInsertPoint(err_bb);
                auto* err_msg = builder->CreateGlobalString("modulo by zero");
                callRtByName("__ang_api_throw_error", {err_msg});
                builder->CreateUnreachable();
                builder->SetInsertPoint(ok_bb);
            }
            auto lt = m_type_checker.getExpressionTypes().find(e.left.get());
            auto rt2 = m_type_checker.getExpressionTypes().find(e.right.get());
            bool unsigned_mod = (lt != m_type_checker.getExpressionTypes().end() && isUnsignedIntType(lt->second)) ||
                                (rt2 != m_type_checker.getExpressionTypes().end() && isUnsignedIntType(rt2->second));
            auto* ia = makeI64(unsigned_mod ? builder->CreateURem(getI64(l),getI64(r))
                                            : builder->CreateSRem(getI64(l),getI64(r)));
            imodBB = builder->GetInsertBlock();
            builder->CreateBr(mmodBB);
            builder->SetInsertPoint(mmodBB);
            auto* phi = builder->CreatePHI(objType,2);
            phi->addIncoming(fa,fmodBB); phi->addIncoming(ia,imodBB);
            return phi;
        }
        case TokenType::AMPERSAND: return makeI64(builder->CreateAnd(getI64(l),getI64(r)));
        case TokenType::PIPE:      return makeI64(builder->CreateOr(getI64(l),getI64(r)));
        case TokenType::CARET:     return makeI64(builder->CreateXor(getI64(l),getI64(r)));
        case TokenType::LSHIFT: {
            // H12: runtime bounds-check — shift amount must be < 64
            auto* lval = getI64(l);
            auto* rval = getI64(r);
            auto* bitwidth = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), 64);
            auto* is_overflow = builder->CreateICmpUGE(rval, bitwidth);
            auto* parent_fn = builder->GetInsertBlock()->getParent();
            auto* err_bb = llvm::BasicBlock::Create(*ctx, "shl_overflow_err", parent_fn);
            auto* ok_bb = llvm::BasicBlock::Create(*ctx, "shl_overflow_ok", parent_fn);
            builder->CreateCondBr(is_overflow, err_bb, ok_bb);
            builder->SetInsertPoint(err_bb);
            auto* err_msg = builder->CreateGlobalString("shift amount out of range");
            callRtByName("__ang_api_throw_error", {err_msg});
            builder->CreateUnreachable();
            builder->SetInsertPoint(ok_bb);
            return makeI64(builder->CreateShl(lval, rval));
        }
        case TokenType::RSHIFT: {
            // H12: runtime bounds-check — shift amount must be < 64
            auto* lval = getI64(l);
            auto* rval = getI64(r);
            auto* bitwidth = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), 64);
            auto* is_overflow = builder->CreateICmpUGE(rval, bitwidth);
            auto* parent_fn = builder->GetInsertBlock()->getParent();
            auto* err_bb = llvm::BasicBlock::Create(*ctx, "shr_overflow_err", parent_fn);
            auto* ok_bb = llvm::BasicBlock::Create(*ctx, "shr_overflow_ok", parent_fn);
            builder->CreateCondBr(is_overflow, err_bb, ok_bb);
            builder->SetInsertPoint(err_bb);
            auto* err_msg = builder->CreateGlobalString("shift amount out of range");
            callRtByName("__ang_api_throw_error", {err_msg});
            builder->CreateUnreachable();
            builder->SetInsertPoint(ok_bb);
            return makeI64(builder->CreateAShr(lval, rval));
        }
        case TokenType::LESS:
        case TokenType::LESS_EQUAL:
        case TokenType::GREATER:
        case TokenType::GREATER_EQUAL: {
            // LANG-13: check for user-defined opCmp method on the left type.
            {
                auto lt = m_type_checker.getExpressionTypes().find(e.left.get());
                if (lt != m_type_checker.getExpressionTypes().end()) {
                    std::string mname = resolveMethodForType(lt->second, "opCmp");
                    if (!mname.empty()) {
                        llvm::Function* mf = mod->getFunction(mname);
                        if (mf) {
                            auto* cmp_result = builder->CreateCall(mf, {l, r});
                            auto* cmp_val = getI64(cmp_result);
                            auto* zero = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), 0);
                            llvm::Value* bool_val;
                            switch (e.op.type) {
                                case TokenType::LESS:        bool_val = builder->CreateICmpSLT(cmp_val, zero); break;
                                case TokenType::LESS_EQUAL:  bool_val = builder->CreateICmpSLE(cmp_val, zero); break;
                                case TokenType::GREATER:     bool_val = builder->CreateICmpSGT(cmp_val, zero); break;
                                case TokenType::GREATER_EQUAL: bool_val = builder->CreateICmpSGE(cmp_val, zero); break;
                                default: bool_val = builder->CreateICmpSLT(cmp_val, zero); break;
                            }
                            return makeBool(bool_val);
                        }
                    }
                }
            }
            // Check if both operands are strings (compile-time type info)
            {
                auto lt = m_type_checker.getExpressionTypes().find(e.left.get());
                auto rt = m_type_checker.getExpressionTypes().find(e.right.get());
                if (lt != m_type_checker.getExpressionTypes().end() &&
                    rt != m_type_checker.getExpressionTypes().end() &&
                    lt->second->toString() == "string" && rt->second->toString() == "string") {
                    // String comparison: call __ang_string_compare, compare result against 0
                    auto* cmp_obj = callRtByName("__ang_string_compare", {l, r});
                    auto* cmp_val = getI64(cmp_obj);
                    auto* zero = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), 0);
                    llvm::Value* bool_val;
                    switch (e.op.type) {
                        case TokenType::LESS:        bool_val = builder->CreateICmpSLT(cmp_val, zero); break;
                        case TokenType::LESS_EQUAL:  bool_val = builder->CreateICmpSLE(cmp_val, zero); break;
                        case TokenType::GREATER:     bool_val = builder->CreateICmpSGT(cmp_val, zero); break;
                        case TokenType::GREATER_EQUAL: bool_val = builder->CreateICmpSGE(cmp_val, zero); break;
                        default: bool_val = builder->CreateICmpSLT(cmp_val, zero); break;
                    }
                    return makeBool(bool_val);
                }
            }
            // Fast path: typed primitives — skip tag dispatch
            if (types_known) {
                auto& ltype = lt_it->second;
                auto& rtype = rt_it->second;
                if (isInteger(ltype) && isInteger(rtype)) {
                    bool unsign = isUnsignedIntType(ltype) || isUnsignedIntType(rtype);
                    llvm::Value* cmp;
                    switch (e.op.type) {
                        case TokenType::LESS:          cmp = unsign ? builder->CreateICmpULT(getI64(l), getI64(r)) : builder->CreateICmpSLT(getI64(l), getI64(r)); break;
                        case TokenType::LESS_EQUAL:    cmp = unsign ? builder->CreateICmpULE(getI64(l), getI64(r)) : builder->CreateICmpSLE(getI64(l), getI64(r)); break;
                        case TokenType::GREATER:       cmp = unsign ? builder->CreateICmpUGT(getI64(l), getI64(r)) : builder->CreateICmpSGT(getI64(l), getI64(r)); break;
                        case TokenType::GREATER_EQUAL: cmp = unsign ? builder->CreateICmpUGE(getI64(l), getI64(r)) : builder->CreateICmpSGE(getI64(l), getI64(r)); break;
                        default: cmp = builder->CreateICmpSLT(getI64(l), getI64(r)); break;
                    }
                    return makeBool(cmp);
                }
                if (isFloat(ltype) || isFloat(rtype)) {
                    auto* ld = isFloat(ltype) ? getF64(l) : builder->CreateSIToFP(getI64(l), f64_ty);
                    auto* rd = isFloat(rtype) ? getF64(r) : builder->CreateSIToFP(getI64(r), f64_ty);
                    llvm::Value* cmp;
                    switch (e.op.type) {
                        case TokenType::LESS:          cmp = builder->CreateFCmpOLT(ld, rd); break;
                        case TokenType::LESS_EQUAL:    cmp = builder->CreateFCmpOLE(ld, rd); break;
                        case TokenType::GREATER:       cmp = builder->CreateFCmpOGT(ld, rd); break;
                        case TokenType::GREATER_EQUAL: cmp = builder->CreateFCmpOGE(ld, rd); break;
                        default: cmp = builder->CreateFCmpOLT(ld, rd); break;
                    }
                    return makeBool(cmp);
                }
            }
            // Numeric comparison (original tag-dispatch code)
            auto* lTag = getTag(l);
            auto* rTag = getTag(r);
            auto* eitherF64 = builder->CreateOr(
                builder->CreateICmpEQ(lTag, llvm::ConstantInt::get(i32_ty, TAG_F64)),
                builder->CreateICmpEQ(rTag, llvm::ConstantInt::get(i32_ty, TAG_F64)));
            auto* fn = builder->GetInsertBlock()->getParent();
            auto* fcmpBB = llvm::BasicBlock::Create(*ctx,"fcmp",fn);
            auto* icmpBB = llvm::BasicBlock::Create(*ctx,"icmp",fn);
            auto* mcmpBB = llvm::BasicBlock::Create(*ctx,"mcmp",fn);
            builder->CreateCondBr(eitherF64, fcmpBB, icmpBB);
            builder->SetInsertPoint(fcmpBB);
            llvm::Value* fb;
            switch (e.op.type) {
                case TokenType::LESS:        fb = builder->CreateFCmpOLT(toDouble(l, lTag),toDouble(r, rTag)); break;
                case TokenType::LESS_EQUAL:  fb = builder->CreateFCmpOLE(toDouble(l, lTag),toDouble(r, rTag)); break;
                case TokenType::GREATER:     fb = builder->CreateFCmpOGT(toDouble(l, lTag),toDouble(r, rTag)); break;
                case TokenType::GREATER_EQUAL: fb = builder->CreateFCmpOGE(toDouble(l, lTag),toDouble(r, rTag)); break;
                default: fb = builder->CreateFCmpOLT(toDouble(l, lTag),toDouble(r, rTag)); break;
            }
            auto* fresult = makeBool(fb);
            fcmpBB = builder->GetInsertBlock();
            builder->CreateBr(mcmpBB);
            builder->SetInsertPoint(icmpBB);
            auto lt = m_type_checker.getExpressionTypes().find(e.left.get());
            auto rt2 = m_type_checker.getExpressionTypes().find(e.right.get());
            bool unsigned_cmp = (lt != m_type_checker.getExpressionTypes().end() && isUnsignedIntType(lt->second)) ||
                                (rt2 != m_type_checker.getExpressionTypes().end() && isUnsignedIntType(rt2->second));
            llvm::Value* ib;
            switch (e.op.type) {
                case TokenType::LESS:        ib = unsigned_cmp ? builder->CreateICmpULT(getI64(l),getI64(r)) : builder->CreateICmpSLT(getI64(l),getI64(r)); break;
                case TokenType::LESS_EQUAL:  ib = unsigned_cmp ? builder->CreateICmpULE(getI64(l),getI64(r)) : builder->CreateICmpSLE(getI64(l),getI64(r)); break;
                case TokenType::GREATER:     ib = unsigned_cmp ? builder->CreateICmpUGT(getI64(l),getI64(r)) : builder->CreateICmpSGT(getI64(l),getI64(r)); break;
                case TokenType::GREATER_EQUAL: ib = unsigned_cmp ? builder->CreateICmpUGE(getI64(l),getI64(r)) : builder->CreateICmpSGE(getI64(l),getI64(r)); break;
                default: ib = builder->CreateICmpSLT(getI64(l),getI64(r)); break;
            }
            auto* iresult = makeBool(ib);
            icmpBB = builder->GetInsertBlock();
            builder->CreateBr(mcmpBB);
            builder->SetInsertPoint(mcmpBB);
            auto* phi = builder->CreatePHI(objType,2);
            phi->addIncoming(fresult,fcmpBB); phi->addIncoming(iresult,icmpBB);
            return phi;
        }
        // LANG-13: check for user-defined opEquals method on the left type.
        case TokenType::EQUAL_EQUAL: {
            auto lt = m_type_checker.getExpressionTypes().find(e.left.get());
            if (lt != m_type_checker.getExpressionTypes().end()) {
                std::string mname = resolveMethodForType(lt->second, "opEquals");
                if (!mname.empty()) {
                    llvm::Function* mf = mod->getFunction(mname);
                    if (mf) return builder->CreateCall(mf, {l, r});
                }
            }
            return callRtByName("__ang_equals",{l,r});
        }
        case TokenType::BANG_EQUAL: {
            auto lt = m_type_checker.getExpressionTypes().find(e.left.get());
            if (lt != m_type_checker.getExpressionTypes().end()) {
                std::string mname = resolveMethodForType(lt->second, "opEquals");
                if (!mname.empty()) {
                    llvm::Function* mf = mod->getFunction(mname);
                    if (mf) {
                        auto* eq = builder->CreateCall(mf, {l, r});
                        return makeBool(builder->CreateNot(getBool(eq)));
                    }
                }
            }
            auto* eq=callRtByName("__ang_equals",{l,r});
            return makeBool(builder->CreateNot(getBool(eq)));
        }
        default: return makeNil();
    }
}

llvm::Value* LLVMBackend::cgUnary(const Unary& e) {
    auto* o=cg(e.right); if(!o) return makeNil();
    if (e.op.type==TokenType::MINUS) {
        // LANG-13: check for user-defined opNeg method on the type.
        {
            auto rt = m_type_checker.getExpressionTypes().find(e.right.get());
            if (rt != m_type_checker.getExpressionTypes().end()) {
                std::string mname = resolveMethodForType(rt->second, "opNeg");
                if (!mname.empty()) {
                    llvm::Function* mf = mod->getFunction(mname);
                    if (mf) return builder->CreateCall(mf, {o});
                }
            }
        }
        auto* tag = getTag(o);
        auto* isF64 = builder->CreateICmpEQ(tag, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), TAG_F64));
        auto* fn = builder->GetInsertBlock()->getParent();
        auto* fnegBB = llvm::BasicBlock::Create(*ctx,"fneg",fn);
        auto* inegBB = llvm::BasicBlock::Create(*ctx,"ineg",fn);
        auto* mnegBB = llvm::BasicBlock::Create(*ctx,"mneg",fn);
        builder->CreateCondBr(isF64, fnegBB, inegBB);
        builder->SetInsertPoint(fnegBB);
        auto* fv = makeF64(builder->CreateFNeg(getF64(o)));
        fnegBB = builder->GetInsertBlock();
        builder->CreateBr(mnegBB);
        builder->SetInsertPoint(inegBB);
        auto* iv = makeI64(builder->CreateNeg(getI64(o)));
        inegBB = builder->GetInsertBlock();
        builder->CreateBr(mnegBB);
        builder->SetInsertPoint(mnegBB);
        auto* phi = builder->CreatePHI(objType,2);
        phi->addIncoming(fv,fnegBB); phi->addIncoming(iv,inegBB);
        return phi;
    }
    if (e.op.type==TokenType::TILDE) return makeI64(builder->CreateNot(getI64(o)));
    if (e.op.type==TokenType::BANG) return makeBool(builder->CreateNot(isTruthy(o)));
    return o;
}

// TS-1: box a concrete instance into a trait object when assigning it into a
// trait/contract-typed slot. Looks up the per-(class,interface) vtable global
// emitted in codegenClassDecl and calls __ang_trait_object_new. No-op for any
// other (non-trait/contract) destination type.
llvm::Value* LLVMBackend::maybeBoxTraitObject(llvm::Value* value, const Expr* src_expr,
                                              const std::shared_ptr<Type>& dst_type) {
    if (!dst_type || !src_expr) return value;
    if (dst_type->kind != TypeKind::TRAIT &&
        dst_type->kind != TypeKind::CONTRACT &&
        dst_type->kind != TypeKind::TRAIT_OBJECT) {
        return value;
    }
    // Resolve the interface name.
    std::string iface_name;
    if (dst_type->kind == TypeKind::TRAIT_OBJECT) {
        auto to = std::dynamic_pointer_cast<TraitObjectType>(dst_type);
        if (!to || !to->interface_type) return value;
        iface_name = to->interface_type->toString();
    } else {
        iface_name = dst_type->toString();
    }
    if (iface_name.rfind("contract<", 0) == 0) {
        iface_name = iface_name.substr(9, iface_name.size() - 10);
    }
    // Resolve the source's concrete class name.
    auto src_it = m_type_checker.getExpressionTypes().find(src_expr);
    if (src_it == m_type_checker.getExpressionTypes().end() || !src_it->second) return value;
    std::string class_name;
    if (src_it->second->kind == TypeKind::INSTANCE) {
        class_name = std::dynamic_pointer_cast<InstanceType>(src_it->second)->class_type->name;
    } else if (src_it->second->kind == TypeKind::CLASS) {
        class_name = std::dynamic_pointer_cast<ClassType>(src_it->second)->name;
    } else {
        return value;  // not a boxable instance
    }
    auto vt_it = traitVtables.find(class_name + "->" + iface_name);
    if (vt_it == traitVtables.end()) return value;  // no vtable (shouldn't happen if conformance checked)
    auto* vtable = vt_it->second;
    auto* vtable_ptr = builder->CreateBitCast(vtable, llvm::PointerType::get(*ctx, 0));
    return callRtByName("__ang_trait_object_new", {value, vtable_ptr});
}

llvm::Value* LLVMBackend::cgAssign(const AssignExpr& e) {
    auto* v = cg(e.value);

    // LANG-10: destructuring assignment — (a, b) = tuple_expr
    if (auto* tuple = dynamic_cast<const TupleExpr*>(e.target.get())) {
        if (e.op.type != TokenType::EQUAL) {
            // Compound assignment (e.g. +=) with destructuring is not supported.
            // The parser shouldn't let this through, but guard anyway.
            return v;
        }
        // Store the tuple value in a temporary so we can extract elements.
        auto* fn = builder->GetInsertBlock()->getParent();
        auto* tmp_alloca = allocLocal(fn, "__dtuple_tmp");
        builder->CreateStore(v, tmp_alloca);

        for (size_t i = 0; i < tuple->elements.size(); ++i) {
            auto* var = dynamic_cast<const VarExpr*>(tuple->elements[i].get());
            if (!var) continue;
            auto* idx_val = makeI64(llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), i));
            auto* elem = callRtByName("__ang_list_get", {
                builder->CreateLoad(objType, tmp_alloca), idx_val
            });
            storeVar(var->name.lexeme, elem);
        }
        return v;
    }

    // LANG-15: compound assignment (e.g. a += b).  When the operator is not plain
    // '=', synthesise a Binary expression (target OP value) and run it through
    // the normal binary codegen so we get the combined value.  The result is then
    // stored exactly like a plain assignment.
    if (e.op.type != TokenType::EQUAL) {
        TokenType binOp;
        switch (e.op.type) {
            case TokenType::PLUS_EQUAL:   binOp = TokenType::PLUS;   break;
            case TokenType::MINUS_EQUAL:  binOp = TokenType::MINUS;  break;
            case TokenType::STAR_EQUAL:   binOp = TokenType::STAR;   break;
            case TokenType::SLASH_EQUAL:  binOp = TokenType::SLASH;  break;
            case TokenType::PERCENT_EQUAL: binOp = TokenType::PERCENT; break;
            case TokenType::AMPERSAND_EQUAL: binOp = TokenType::AMPERSAND; break;
            case TokenType::PIPE_EQUAL:   binOp = TokenType::PIPE;   break;
            case TokenType::CARET_EQUAL:  binOp = TokenType::CARET;  break;
            case TokenType::LSHIFT_EQUAL: binOp = TokenType::LSHIFT; break;
            case TokenType::RSHIFT_EQUAL: binOp = TokenType::RSHIFT; break;
            default: binOp = TokenType::EQUAL; break;
        }
        Token binTok(binOp, e.op.lexeme.substr(0, e.op.lexeme.size() - 1),
                     e.op.line, e.op.column, e.op.file);
        auto synth = std::make_shared<Binary>(e.target, binTok, e.value);
        v = cgBinary(*synth);
    }

    if (auto* var = dynamic_cast<const VarExpr*>(e.target.get())) {
        // TS-1: box into a trait object if the target is trait/contract-typed.
        auto tgt_it = namedTypes.find(var->name.lexeme);
        if (tgt_it != namedTypes.end() && tgt_it->second) {
            v = maybeBoxTraitObject(v, e.value.get(), tgt_it->second);
        }
        // v5: data types copy-on-assign. If the target variable is a plain
        // `data` type (not owned/class — those are tracked for drop), deep-clone
        // the value so the target is independent of the source.
        auto type_it = namedTypes.find(var->name.lexeme);
        if (type_it != namedTypes.end() && type_it->second) {
            auto& vt = type_it->second;
            if (vt->kind == TypeKind::DATA &&
                m_tracked_types.count(vt->toString()) == 0) {
                v = callRtByName("__ang_deep_clone", {v});
            }
            if (isSizedIntType(vt)) {
                v = truncateForType(v, vt);
            }
        }
        storeVar(var->name.lexeme, v);
        return v;
    }
    if (auto* get = dynamic_cast<const GetExpr*>(e.target.get())) {
        // Check for foreign data field write
        auto type_it = m_type_checker.getExpressionTypes().find(get->object.get());
        if (type_it != m_type_checker.getExpressionTypes().end() && type_it->second->kind == TypeKind::DATA) {
            auto dt = std::dynamic_pointer_cast<DataType>(type_it->second);
            if (dt && dt->is_foreign && !dt->is_opaque) {
                auto* obj = cg(get->object);
                auto* data_ptr = callRtByName("__ang_api_native_instance_data", {obj});
                auto sit = m_foreign_struct_types.find(dt->name);
                if (sit == m_foreign_struct_types.end()) return v;
                auto* struct_type = sit->second;
                auto* struct_ptr = builder->CreateBitCast(data_ptr, llvm::PointerType::get(*ctx, 0));

                auto order_it = m_foreign_field_order.find(dt->name);
                if (order_it == m_foreign_field_order.end()) return v;

                unsigned field_index = 0;
                bool found = false;
                for (const auto& fname : order_it->second) {
                    if (fname == get->name.lexeme) { found = true; break; }
                    field_index++;
                }
                if (!found) return v;

                auto field_it = dt->fields.find(get->name.lexeme);
                if (field_it == dt->fields.end()) return v;
                auto& field_type = field_it->second.type;

                llvm::Value* field_ptr;
                if (dt->is_union) {
                    field_ptr = struct_ptr;
                } else {
                    field_ptr = builder->CreateStructGEP(struct_type, struct_ptr, field_index);
                }

                auto* c_val = marshalAngaraToC(v, field_type);
                builder->CreateStore(c_val, field_ptr);
                return v;
            }
        }
        auto* obj = cg(get->object);
        callRtByName("__ang_record_set", {obj, builder->CreateGlobalString(get->name.lexeme), v});
        return v;
    }
    if (auto* sub = dynamic_cast<const SubscriptExpr*>(e.target.get())) {
        auto* obj = cg(sub->object);
        if (auto* lit = dynamic_cast<const Literal*>(sub->index.get())) {
            if (lit->token.type == TokenType::STRING) {
                callRtByName("__ang_record_set", {obj, builder->CreateGlobalString(lit->token.lexeme), v});
                return v;
            }
        }
        // Check if the object is a record type to dispatch correctly for variable keys
        auto type_it = m_type_checker.getExpressionTypes().find(sub->object.get());
        bool is_record = type_it != m_type_checker.getExpressionTypes().end() &&
            (type_it->second->kind == TypeKind::RECORD ||
             type_it->second->kind == TypeKind::INSTANCE ||
             type_it->second->kind == TypeKind::GENERIC_INSTANCE);
        if (is_record) {
            auto* key_obj = cg(sub->index);
            auto* fn_as_cstr = this->mod->getFunction("__ang_api_as_cstr");
            if (fn_as_cstr) {
                auto* key_cstr = builder->CreateCall(fn_as_cstr, {key_obj});
                callRtByName("__ang_record_set", {obj, key_cstr, v});
                return v;
            }
        }

        // SIMD-1: unboxed dynamic array — emit direct GEP + store
        if (type_it != m_type_checker.getExpressionTypes().end() &&
            type_it->second->kind == TypeKind::RAW_ARRAY) {
            auto raw_arr_type = std::dynamic_pointer_cast<RawArrayType>(type_it->second);
            if (raw_arr_type && raw_arr_type->element_type) {
                auto elem_kind = localKindForType(raw_arr_type->element_type);
                if (elem_kind != LocalKind::BOXED) {
                    auto* elem_llvm_ty = llvmTypeForLocalKind(elem_kind);
                    auto* i64_ty = llvm::Type::getInt64Ty(*ctx);

                    auto* idx_obj = cg(sub->index);
                    auto* idx_val = getI64(idx_obj);

                    auto* payload = builder->CreateExtractValue(obj, {1}, "arr_payload");
                    auto* arr_ptr = builder->CreateIntToPtr(payload,
                        llvm::PointerType::get(*ctx, 0), "arr_ptr");

                    auto* buf_ptr = builder->CreateLoad(llvm::PointerType::get(*ctx, 0),
                        builder->CreateStructGEP(rt->getRawArrayType(), arr_ptr, 4), "buf_ptr");

                    auto* elem_ptr = builder->CreateGEP(elem_llvm_ty, buf_ptr, {idx_val}, "elem_ptr");

                    // Unbox the value and store directly
                    auto* raw_val = unboxToRaw(v, elem_kind);
                    builder->CreateStore(raw_val, elem_ptr);
                    return v;
                }
            }
        }

        // SIMD-5: vector subscript assignment — insert element into vector
        if (type_it != m_type_checker.getExpressionTypes().end() &&
            type_it->second->kind == TypeKind::VECTOR) {
            auto vec_type = std::dynamic_pointer_cast<VectorType>(type_it->second);
            if (vec_type && vec_type->element_type) {
                // Extract the raw LLVM vector from the boxed object
                auto* raw_vec = extractVector(obj, *vec_type);

                // Get the index as i32
                auto* idx_obj = cg(sub->index);
                auto* idx_val = getI64(idx_obj);
                auto* idx_i32 = builder->CreateTrunc(idx_val,
                    llvm::Type::getInt32Ty(*ctx), "idx_i32");

                // Unbox the value to raw scalar
                auto elem_kind = localKindForType(vec_type->element_type);
                auto* raw_val = unboxToRaw(v, elem_kind);

                // Insert the element
                auto* new_vec = builder->CreateInsertElement(raw_vec, raw_val, idx_i32, "vec_ins");

                // Store back to the heap object
                auto* payload = builder->CreateExtractValue(obj, {1}, "vec_payload2");
                auto* vec_ptr = builder->CreateIntToPtr(payload,
                    llvm::PointerType::get(*ctx, 0));
                auto* data_ptr = builder->CreateLoad(llvm::PointerType::get(*ctx, 0),
                    builder->CreateStructGEP(rt->getVectorType(), vec_ptr, 3), "data_ptr2");
                auto* vec_ty = llvmTypeForVector(*vec_type);
                auto* typed_ptr = builder->CreateBitCast(data_ptr,
                    llvm::PointerType::get(vec_ty, 0), "typed_ptr2");
                builder->CreateStore(new_vec, typed_ptr);
                return v;
            }
        }

        callRtByName("__ang_list_set", {obj, cg(sub->index), v});
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

    // LANG-11: helper to get resolved call args (reordered + defaults-filled).
    auto getArgs = [&](const CallExpr& call) -> const std::vector<std::shared_ptr<Expr>>& {
        const auto& resolved = m_type_checker.getResolvedArgs();
        auto it = resolved.find(&call);
        return (it != resolved.end()) ? it->second : call.arguments;
    };

    // Handle immediate lambda invocation: (func(...) -> T { ... })(args)
    if (dynamic_cast<const LambdaExpr*>(expr.callee.get())) {
        auto* callee = cg(expr.callee);
        std::vector<llvm::Value*> llvmArgs;
        for (auto& a : getArgs(expr)) llvmArgs.push_back(cg(a));
        return cgClosureCall(callee, llvmArgs);
    }

    // Handle super.method(args...) calls
    if (auto* get = dynamic_cast<const GetExpr*>(expr.callee.get())) {
        if (auto* super_expr = dynamic_cast<const SuperExpr*>(get->object.get())) {
            if (!m_current_superclass.empty()) {
                std::string method_name = mangleMethod(m_current_superclass, get->name.lexeme);
                llvm::Function* mf = this->mod->getFunction(method_name);
                if (mf) {
                    std::vector<llvm::Value*> args;
                    args.push_back(loadVar("this"));
                    for (auto& a : getArgs(expr)) args.push_back(cg(a));
                    auto* ft = mf->getFunctionType();
                    while (args.size() < ft->getNumParams()) args.push_back(makeNil());
                    return builder->CreateCall(mf, args);
                }
            }
            return makeNil();
        }
        // Handle this.method(args...) calls
        if (dynamic_cast<const ThisExpr*>(get->object.get())) {
            std::string fnName = get->name.lexeme;
            // Use namedTypes["this"] to find the class, then walk the chain
            auto tit = namedTypes.find("this");
            if (tit != namedTypes.end() && tit->second->kind == TypeKind::INSTANCE) {
                auto inst = std::dynamic_pointer_cast<InstanceType>(tit->second);
                if (inst && inst->class_type) {
                    auto cls = inst->class_type;
                    while (cls) {
                        auto qit = methodLookup.find(cls->name + "." + fnName);
                        if (qit != methodLookup.end()) {
                            llvm::Function* mf = this->mod->getFunction(qit->second);
                            if (mf) {
                                std::vector<llvm::Value*> args;
                                args.push_back(loadVar("this"));
                                for (auto& a : getArgs(expr)) args.push_back(cg(a));
                                auto* ft = mf->getFunctionType();
                                while (args.size() < ft->getNumParams()) args.push_back(makeNil());
                                return builder->CreateCall(mf, args);
                            }
                        }
                        cls = cls->superclass;
                    }
                }
            }
            // Fallback to unqualified lookup
            auto mit = methodLookup.find(fnName);
            if (mit != methodLookup.end()) {
                llvm::Function* mf = this->mod->getFunction(mit->second);
                if (mf) {
                    std::vector<llvm::Value*> args;
                    args.push_back(loadVar("this"));
                    for (auto& a : getArgs(expr)) args.push_back(cg(a));
                    auto* ft = mf->getFunctionType();
                    while (args.size() < ft->getNumParams()) args.push_back(makeNil());
                    return builder->CreateCall(mf, args);
                }
            }
        }
        // TS-1: trait/contract object — indirect dispatch via the vtable, for an
        // arbitrary receiver expression (var, subscript, call, ...). When the
        // receiver's static type is a trait/contract (or trait-object view),
        // evaluate the receiver, load the function pointer from its vtable, and
        // call indirectly. (The bare-VarExpr fast path below handles the same
        // case without re-evaluating; this catches shapes[i].draw() etc.)
        {
            auto rot = m_type_checker.getExpressionTypes().find(get->object.get());
            if (rot != m_type_checker.getExpressionTypes().end()) {
                auto& rt2 = rot->second;
                if (rt2 && (rt2->kind == TypeKind::TRAIT ||
                            rt2->kind == TypeKind::CONTRACT ||
                            rt2->kind == TypeKind::TRAIT_OBJECT) &&
                    !dynamic_cast<const VarExpr*>(get->object.get())) {
                    std::string iface_name;
                    if (rt2->kind == TypeKind::TRAIT_OBJECT) {
                        auto to = std::dynamic_pointer_cast<TraitObjectType>(rt2);
                        if (to && to->interface_type) iface_name = to->interface_type->toString();
                    } else {
                        iface_name = rt2->toString();
                    }
                    if (iface_name.rfind("contract<", 0) == 0) {
                        iface_name = iface_name.substr(9, iface_name.size() - 10);
                    }
                    auto slot_it = traitMethodSlots.find(iface_name + "." + get->name.lexeme);
                    if (slot_it != traitMethodSlots.end()) {
                        auto* recv_val = cg(get->object);
                        auto* payload = builder->CreateExtractValue(recv_val, {1}, "to_payload");
                        auto* to_ptr = builder->CreateIntToPtr(payload, rt->getTraitObjectType()->getPointerTo());
                        auto* vtable_ptr = builder->CreateLoad(llvm::PointerType::get(*ctx, 0),
                            builder->CreateStructGEP(rt->getTraitObjectType(), to_ptr, 2), "vtable");
                        auto* slot_ptr = builder->CreateGEP(llvm::PointerType::get(*ctx, 0), vtable_ptr,
                            {llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), slot_it->second)}, "slot");
                        auto* fn_ptr = builder->CreateLoad(llvm::PointerType::get(*ctx, 0), slot_ptr, "mfn");
                        std::vector<llvm::Type*> param_tys(getArgs(expr).size() + 1, objType);
                        auto* mfn_ty = llvm::FunctionType::get(objType, param_tys, false);
                        auto* embedded_recv = builder->CreateLoad(objType,
                            builder->CreateStructGEP(rt->getTraitObjectType(), to_ptr, 1), "to_recv");
                        std::vector<llvm::Value*> args;
                        args.push_back(embedded_recv);
                        for (auto& a : getArgs(expr)) args.push_back(cg(a));
                        return builder->CreateCall(mfn_ty, fn_ptr, args);
                    }
                }
            }
        }
        if (auto* obj = dynamic_cast<const VarExpr*>(get->object.get())) {
            std::string modName = obj->name.lexeme, fnName = get->name.lexeme;

            // TS-1: trait/contract object — indirect dispatch via the vtable.
            // When the receiver's static type is a trait/contract (or a trait-
            // object view), the concrete method is unknown at compile time; load
            // the function pointer from the trait object's vtable and call it.
            // Mirrors __ang_call's closure branch.
            auto recv_type_it = m_type_checker.getExpressionTypes().find(obj);
            if (recv_type_it != m_type_checker.getExpressionTypes().end()) {
                auto& rt2 = recv_type_it->second;
                if (rt2 && (rt2->kind == TypeKind::TRAIT ||
                            rt2->kind == TypeKind::CONTRACT ||
                            rt2->kind == TypeKind::TRAIT_OBJECT)) {
                    // Resolve the interface name (TRAIT/CONTRACT directly; for a
                    // TRAIT_OBJECT, its interface_type).
                    std::string iface_name;
                    if (rt2->kind == TypeKind::TRAIT_OBJECT) {
                        auto to = std::dynamic_pointer_cast<TraitObjectType>(rt2);
                        if (to && to->interface_type) iface_name = to->interface_type->toString();
                    } else {
                        iface_name = rt2->toString();
                    }
                    // Contracts toString as "contract<Name>"; normalize to bare name.
                    if (iface_name.rfind("contract<", 0) == 0) {
                        iface_name = iface_name.substr(9, iface_name.size() - 10);
                    }
                    auto slot_it = traitMethodSlots.find(iface_name + "." + fnName);
                    if (slot_it != traitMethodSlots.end()) {
                        auto* recv_val = loadVar(modName);
                        // Unbox the trait object pointer.
                        auto* payload = builder->CreateExtractValue(recv_val, {1}, "to_payload");
                        auto* to_ptr = builder->CreateIntToPtr(payload, rt->getTraitObjectType()->getPointerTo());
                        // Load the vtable pointer (field 2) and the slot.
                        auto* vtable_ptr = builder->CreateLoad(llvm::PointerType::get(*ctx, 0),
                            builder->CreateStructGEP(rt->getTraitObjectType(), to_ptr, 2), "vtable");
                        auto* slot_ptr = builder->CreateGEP(llvm::PointerType::get(*ctx, 0), vtable_ptr,
                            {llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), slot_it->second)}, "slot");
                        auto* fn_ptr = builder->CreateLoad(llvm::PointerType::get(*ctx, 0), slot_ptr, "mfn");
                        // Build the method signature: (obj, obj...) -> obj.
                        std::vector<llvm::Type*> param_tys(getArgs(expr).size() + 1, objType);
                        auto* mfn_ty = llvm::FunctionType::get(objType, param_tys, false);
                        // The receiver passed to the method is the concrete
                        // instance embedded in the trait object (field 1), so
                        // `this` inside the impl is the real object.
                        auto* embedded_recv = builder->CreateLoad(objType,
                            builder->CreateStructGEP(rt->getTraitObjectType(), to_ptr, 1), "to_recv");
                        std::vector<llvm::Value*> args;
                        args.push_back(embedded_recv);
                        for (auto& a : getArgs(expr)) args.push_back(cg(a));
                        return builder->CreateCall(mfn_ty, fn_ptr, args);
                    }
                }
            }
            {
                // Type-aware method dispatch: use the variable's class type to
                // look up the qualified key (e.g. "Animal.speak"), walking the
                // superclass chain if the method isn't found on the exact class.
                std::string resolved_method;
                auto type_it = m_type_checker.getExpressionTypes().find(obj);
                if (type_it != m_type_checker.getExpressionTypes().end() &&
                    (type_it->second->kind == TypeKind::INSTANCE ||
                     type_it->second->kind == TypeKind::CLASS)) {
                    // For INSTANCE types, walk class_type; for native CLASS
                    // types (returned by module constructors like amqp.connect),
                    // use the class name directly.
                    std::shared_ptr<ClassType> cls;
                    if (type_it->second->kind == TypeKind::INSTANCE) {
                        auto inst = std::dynamic_pointer_cast<InstanceType>(type_it->second);
                        if (inst) cls = inst->class_type;
                    } else {
                        cls = std::dynamic_pointer_cast<ClassType>(type_it->second);
                    }
                    while (cls) {
                        auto qit = methodLookup.find(cls->name + "." + fnName);
                        if (qit != methodLookup.end()) {
                            resolved_method = qit->second;
                            break;
                        }
                        cls = cls->superclass;
                    }
                }
                // Fall back to unqualified lookup if no qualified match
                if (resolved_method.empty()) {
                    auto mit = methodLookup.find(fnName);
                    if (mit != methodLookup.end())
                        resolved_method = mit->second;
                }
                // Fallback: resolve via codegen's namedTypes if expression-type
                // pointer lookup missed (handles native instances from module
                // constructors whose VarExpr pointer may not be in the type
                // checker's expression-type map).
                if (resolved_method.empty()) {
                    auto nt_it = namedTypes.find(modName);
                    if (nt_it != namedTypes.end() && nt_it->second) {
                        auto& t = nt_it->second;
                        std::shared_ptr<ClassType> cls;
                        if (t->kind == TypeKind::INSTANCE) {
                            auto inst = std::dynamic_pointer_cast<InstanceType>(t);
                            if (inst) cls = inst->class_type;
                        } else if (t->kind == TypeKind::CLASS) {
                            cls = std::dynamic_pointer_cast<ClassType>(t);
                        }
                        while (cls) {
                            auto qit = methodLookup.find(cls->name + "." + fnName);
                            if (qit != methodLookup.end()) { resolved_method = qit->second; break; }
                            cls = cls->superclass;
                        }
                        if (resolved_method.empty()) {
                            auto mit2 = methodLookup.find(fnName);
                            if (mit2 != methodLookup.end()) resolved_method = mit2->second;
                        }
                    }
                }
                if (!resolved_method.empty()) {
                    llvm::Function* mf = this->mod->getFunction(resolved_method);
                    if (mf) {
                        std::vector<llvm::Value*> args;
                        args.push_back(loadVar(modName));
                        for (auto& a : getArgs(expr)) args.push_back(cg(a));
                        auto* ft = mf->getFunctionType();
                        while (args.size() < ft->getNumParams()) args.push_back(makeNil());
                        return builder->CreateCall(mf, args);
                    }
                }
            }
            std::string gkey = "g_" + sanitize(modName);
            bool is_var = namedVals.find(sanitize(modName)) != namedVals.end() ||
                          globals.find(gkey) != globals.end();

            // Check for enum variant constructor (e.g., WebEvent.KeyPress("h"))
            if (!is_var) {
                std::string enum_global = "g_" + modName + "." + fnName;
                std::string ctor_key = modName + "." + fnName;

                // Simple enum variant (no data) — return global constant
                auto git = globals.find(enum_global);
                if (git != globals.end()) {
                    return builder->CreateLoad(objType, git->second);
                }

                // Enum variant with associated data — call constructor
                auto cit = constructorLookup.find(ctor_key);
                if (cit != constructorLookup.end()) {
                    llvm::Function* ctor_fn = this->mod->getFunction(cit->second);
                    if (ctor_fn) {
                        std::vector<llvm::Value*> args;
                        for (auto& a : getArgs(expr)) args.push_back(cg(a));
                        auto* ft = ctor_fn->getFunctionType();
                        while (args.size() < ft->getNumParams()) args.push_back(makeNil());
                        return builder->CreateCall(ctor_fn, args);
                    }
                }
            }

            if (is_var) {
                auto* varObj = loadVar(modName);
                if (fnName == "push" || fnName == "add") {
                    if (!getArgs(expr).empty()) {
                        // SIMD-1: raw arrays — unbox and push via runtime
                        auto ntype_it2 = namedTypes.find(sanitize(modName));
                        if (ntype_it2 != namedTypes.end() && ntype_it2->second &&
                            ntype_it2->second->kind == TypeKind::RAW_ARRAY) {
                            auto* val = cg(getArgs(expr)[0]);
                            return callRtByName("__ang_raw_array_push", {varObj, getI64(val)});
                        }
                        return callRtByName("__ang_list_push", {varObj, cg(getArgs(expr)[0])});
                    }
                    return makeNil();
                }
                if (fnName == "get" || fnName == "at") {
                    if (!getArgs(expr).empty())
                        return callRtByName("__ang_list_get", {varObj, cg(getArgs(expr)[0])});
                    return makeNil();
                }
                if (fnName == "set") {
                    if (getArgs(expr).size() >= 2)
                        return callRtByName("__ang_list_set", {varObj, cg(getArgs(expr)[0]), cg(getArgs(expr)[1])});
                    return makeNil();
                }
                if (fnName == "length" || fnName == "len" || fnName == "size" || fnName == "count") {
                    // SIMD-1: raw arrays get inline len() via GEP+load
                    {
                        auto ntype_it = namedTypes.find(sanitize(modName));
                        if (ntype_it != namedTypes.end() && ntype_it->second &&
                            ntype_it->second->kind == TypeKind::RAW_ARRAY) {
                            auto* payload = builder->CreateExtractValue(varObj, {1}, "arr_payload");
                            auto* arr_ptr = builder->CreateIntToPtr(payload,
                                llvm::PointerType::get(*ctx, 0), "arr_ptr");
                            auto* count = builder->CreateLoad(llvm::Type::getInt64Ty(*ctx),
                                builder->CreateStructGEP(rt->getRawArrayType(), arr_ptr, 1), "count");
                            return makeI64(count);
                        }
                    }
                    return callRtByName("__ang_len", {varObj});
                }
                if (fnName == "lock") {
                    callRtByName("__ang_mutex_lock", {varObj});
                    return makeNil();
                }
                if (fnName == "unlock") {
                    callRtByName("__ang_mutex_unlock", {varObj});
                    return makeNil();
                }
                if (fnName == "join") {
                    return callRtByName("__ang_thread_join", {varObj});
                }
                if (fnName == "clone" || fnName == "deep_clone") {
                    return callRtByName("__ang_deep_clone", {varObj});
                }
                if (fnName == "remove_at") {
                    if (!getArgs(expr).empty())
                        return callRtByName("__ang_list_remove_at", {varObj, cg(getArgs(expr)[0])});
                    return makeNil();
                }
                if (fnName == "remove") {
                    if (!getArgs(expr).empty()) {
                        auto ntype_it = namedTypes.find(sanitize(modName));
                        if (ntype_it != namedTypes.end() && ntype_it->second->kind == TypeKind::RECORD) {
                            auto* key_obj = cg(getArgs(expr)[0]);
                            auto* fn_as_cstr = this->mod->getFunction("__ang_api_as_cstr");
                            if (fn_as_cstr) {
                                auto* key_cstr = builder->CreateCall(fn_as_cstr, {key_obj});
                                return callRtByName("__ang_record_remove", {varObj, key_cstr});
                            }
                        } else {
                            return callRtByName("__ang_list_remove", {varObj, cg(getArgs(expr)[0])});
                        }
                    }
                    return makeNil();
                }
                if (fnName == "keys") {
                    return callRtByName("__ang_record_keys", {varObj});
                }
            }
            if (modName=="io") {
                if (fnName=="println"||fnName=="print") {
                    if (getArgs(expr).size() >= 2) {
                        if (fnName=="println") callRtByName("__ang_io_println", {cg(getArgs(expr)[0]), cg(getArgs(expr)[1])});
                        else callRtByName("__ang_io_print", {cg(getArgs(expr)[0]), cg(getArgs(expr)[1])});
                    } else if (!getArgs(expr).empty()) {
                        if (fnName=="println") callRtByName("__ang_io_println", {cg(getArgs(expr)[0])});
                        else callRtByName("__ang_io_print", {cg(getArgs(expr)[0])});
                    }
                    return makeNil();
                }
                if (fnName=="write") {
                    if (getArgs(expr).size() >= 2)
                        callRtByName("__ang_io_write", {cg(getArgs(expr)[0]), cg(getArgs(expr)[1])});
                    return makeNil();
                }
                if (fnName=="flush") {
                    if (!getArgs(expr).empty()) callRtByName("__ang_io_flush", {cg(getArgs(expr)[0])});
                    return makeNil();
                }
                if (fnName=="read_line") return callRtByName("__ang_io_read_line", {});
                if (fnName=="read_all") return callRtByName("__ang_io_read_all", {});
            }
            // Check for variadic foreign function
            auto vfit = m_variadic_foreign_funcs.find(fnName);
            if (vfit != m_variadic_foreign_funcs.end()) {
                return callVariadicForeignFn(fnName, vfit->second, getArgs(expr));
            }
            // If modName is a variable whose type is a native class, use the
            // class's home module name so the mangled symbol matches the .so
            // export (e.g. "Loop" → Angara_Loop_set_timeout, not ang_loop_set_timeout).
            // Also prepend `self` (the receiver) to the argument list, since
            // native class methods expect it as args[0].
            std::string resolved_mod = modName;
            auto nt_it = namedTypes.find(modName);
            bool is_native_method = false;
            if (nt_it != namedTypes.end() && nt_it->second) {
                auto& t = nt_it->second;
                std::shared_ptr<ClassType> cls;
                if (t->kind == TypeKind::INSTANCE) {
                    auto inst = std::dynamic_pointer_cast<InstanceType>(t);
                    if (inst && inst->class_type) cls = inst->class_type;
                } else if (t->kind == TypeKind::CLASS) {
                    cls = std::dynamic_pointer_cast<ClassType>(t);
                }
                if (cls && cls->is_native) {
                    resolved_mod = cls->name;
                    is_native_method = true;
                }
            }
            if (is_native_method) {
                // Prepend self to the args for native method calling convention
                std::vector<llvm::Value*> llvmArgs;
                llvmArgs.push_back(loadVar(modName));  // self
                for (auto& a : getArgs(expr)) llvmArgs.push_back(cg(a));
                auto cnt = (unsigned)llvmArgs.size();
                std::string native_name = "Angara_" + resolved_mod + "_" + fnName;
                auto* native_ft = llvm::FunctionType::get(objType,
                    {llvm::Type::getInt32Ty(*ctx), llvm::PointerType::get(*ctx, 0)}, false);
                llvm::Function* native_f = llvm::Function::Create(native_ft, llvm::Function::ExternalLinkage, native_name, this->mod.get());
                std::vector<llvm::Value*> call_args;
                call_args.push_back(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), cnt));
                if (cnt > 0) {
                    auto* aa = builder->CreateAlloca(llvm::ArrayType::get(objType, cnt));
                    for (unsigned i = 0; i < cnt; i++) {
                        auto* ep = builder->CreateGEP(llvm::ArrayType::get(objType, cnt), aa,
                            {llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), 0),
                             llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), i)});
                        builder->CreateStore(llvmArgs[i], ep);
                    }
                    call_args.push_back(builder->CreateBitCast(aa, llvm::PointerType::get(*ctx, 0)));
                } else {
                    call_args.push_back(llvm::ConstantPointerNull::get(llvm::PointerType::get(*ctx, 0)));
                }
                return builder->CreateCall(native_f, call_args);
            }
            return callModuleFn(resolved_mod, fnName, getArgs(expr));
        }
        // Handle chained property access on arbitrary expressions:
        // e.g., this.mtx.lock(), this.mtx.unlock(), some_expr.join()
        std::string fnName = get->name.lexeme;
        if (fnName == "lock" || fnName == "unlock" || fnName == "join") {
            auto* obj = cg(get->object);
            if (fnName == "lock") {
                callRtByName("__ang_mutex_lock", {obj});
                return makeNil();
            }
            if (fnName == "unlock") {
                callRtByName("__ang_mutex_unlock", {obj});
                return makeNil();
            }
            if (fnName == "join") {
                return callRtByName("__ang_thread_join", {obj});
            }
        }
        return cg(get->object);
    }

    // Handle super(args...) constructor calls and super.method(args...) calls
    if (auto* super_expr = dynamic_cast<const SuperExpr*>(expr.callee.get())) {
        if (!m_current_superclass.empty()) {
            std::string method_name = super_expr->method.has_value()
                ? mangleMethod(m_current_superclass, super_expr->method->lexeme)
                : mangleMethod(m_current_superclass, "init");
            llvm::Function* super_fn = this->mod->getFunction(method_name);
            if (super_fn) {
                std::vector<llvm::Value*> args;
                args.push_back(loadVar("this"));
                for (auto& a : getArgs(expr)) args.push_back(cg(a));
                auto* ft = super_fn->getFunctionType();
                while (args.size() < ft->getNumParams()) args.push_back(makeNil());
                return builder->CreateCall(super_fn, args);
            }
        }
        return makeNil();
    }

    if (auto* var = dynamic_cast<const VarExpr*>(expr.callee.get())) {
        std::string fn = var->name.lexeme;

        if (fn == "peek8" || fn == "peek16" || fn == "peek32" || fn == "peek64") {
            if (!getArgs(expr).empty()) {
                auto* addr = getI64(cg(getArgs(expr)[0]));
                auto* ptr = builder->CreateIntToPtr(addr, llvm::PointerType::get(*ctx, 0));
                llvm::Type* loadTy = (fn == "peek8")  ? llvm::Type::getInt8Ty(*ctx)
                                   : (fn == "peek16") ? llvm::Type::getInt16Ty(*ctx)
                                   : (fn == "peek32") ? llvm::Type::getInt32Ty(*ctx)
                                   :                      llvm::Type::getInt64Ty(*ctx);
                auto* val = builder->CreateLoad(loadTy, ptr, "peek");
                val->setVolatile(true);
                return makeI64(builder->CreateZExt(val, llvm::Type::getInt64Ty(*ctx)));
            }
            return makeI64((int64_t)0);
        }
        if (fn == "poke8" || fn == "poke16" || fn == "poke32" || fn == "poke64") {
            if (getArgs(expr).size() >= 2) {
                auto* addr = getI64(cg(getArgs(expr)[0]));
                auto* val  = getI64(cg(getArgs(expr)[1]));
                auto* ptr = builder->CreateIntToPtr(addr, llvm::PointerType::get(*ctx, 0));
                llvm::Type* storeTy = (fn == "poke8")  ? llvm::Type::getInt8Ty(*ctx)
                                    : (fn == "poke16") ? llvm::Type::getInt16Ty(*ctx)
                                    : (fn == "poke32") ? llvm::Type::getInt32Ty(*ctx)
                                    :                      llvm::Type::getInt64Ty(*ctx);
                auto* trunc = builder->CreateTrunc(val, storeTy, "poke_val");
                auto* store = builder->CreateStore(trunc, ptr);
                store->setVolatile(true);
            }
            return makeNil();
        }
        if (fn == "halt") {
            auto* trap = llvm::Intrinsic::getOrInsertDeclaration(mod.get(), llvm::Intrinsic::trap);
            builder->CreateCall(trap, {});
            auto* parentFn = builder->GetInsertBlock()->getParent();
            auto* haltBB = llvm::BasicBlock::Create(*ctx, "halt", parentFn);
            builder->CreateBr(haltBB);
            builder->SetInsertPoint(haltBB);
            builder->CreateBr(haltBB);
            return makeNil();
        }
        if (fn == "nop") {
            auto* donothing = llvm::Intrinsic::getOrInsertDeclaration(mod.get(), llvm::Intrinsic::donothing);
            builder->CreateCall(donothing, {});
            return makeNil();
        }
        // TS-2: builtin hash(x) -> i64. Hashes any value via __ang_obj_hash.
        if (fn == "hash") {
            if (!getArgs(expr).empty()) {
                return makeI64(callRtByName("__ang_obj_hash", {cg(getArgs(expr)[0])}));
            }
            return makeI64((int64_t)0);
        }

        // SIMD-5: vector constructors — vec2(...), vec3(...), vec4(...), vec8(...)
        if (fn == "vec2" || fn == "vec3" || fn == "vec4" || fn == "vec8") {
            int size = std::stoi(fn.substr(3));
            // Get the result type from the type checker to determine element type
            auto type_it = m_type_checker.getExpressionTypes().find(&expr);
            if (type_it != m_type_checker.getExpressionTypes().end() &&
                type_it->second->kind == TypeKind::VECTOR) {
                auto vec_type = std::dynamic_pointer_cast<VectorType>(type_it->second);
                if (vec_type) {
                    // Determine LLVM vector type
                    auto* vec_ty = llvmTypeForVector(*vec_type);
                    // Build the vector from scalar arguments
                    auto* undef_vec = llvm::UndefValue::get(vec_ty);
                    llvm::Value* vec_val = undef_vec;
                    for (size_t i = 0; i < getArgs(expr).size() && i < (size_t)size; i++) {
                        auto* arg = cg(getArgs(expr)[i]);
                        llvm::Value* scalar;
                        if (isFloat(vec_type->element_type)) {
                            scalar = getF64(arg);
                        } else {
                            scalar = getI64(arg);
                        }
                        vec_val = builder->CreateInsertElement(vec_val, scalar, (uint64_t)i, "vecins");
                    }
                    return makeVector(vec_val, *vec_type);
                }
            }
            // Fallback: return nil if we couldn't determine the type
            return makeNil();
        }

        if (fn=="string") {
            // LANG-4: type-aware — a char argument renders as the glyph.
            if (!getArgs(expr).empty()) return toStrTyped(getArgs(expr)[0]);
            return makeStr("");
        }
        if (fn=="char") {
            // LANG-4: char(x) — at runtime char is a TAG_I64 integer, so the
            // conversion is just the integer coercion __ang_to_i64 wrapped back
            // into the boxed value (the type checker records the char type).
            if (!getArgs(expr).empty()) return callRtByName("__ang_to_i64",{cg(getArgs(expr)[0])});
            return makeI64((int64_t)0);
        }
        if (fn=="i64" || fn=="int") {
            if (!getArgs(expr).empty()) return callRtByName("__ang_to_i64",{cg(getArgs(expr)[0])});
            return makeI64((int64_t)0);
        }
        if (fn=="f64" || fn=="float") {
            if (!getArgs(expr).empty()) return callRtByName("__ang_to_f64",{cg(getArgs(expr)[0])});
            return makeF64(0.0);
        }
        if (fn=="bool") {
            if (!getArgs(expr).empty()) return callRtByName("__ang_to_bool",{cg(getArgs(expr)[0])});
            return makeBool(false);
        }

        // Global println/print — rewrite to __ang_io_println/__ang_io_print with stdout
        if (fn=="println" || fn=="print") {
            const char* rt = (fn=="println") ? "__ang_io_println" : "__ang_io_print";
            if (!getArgs(expr).empty()) {
                // Build a string by concatenating all arguments. LANG-4: each
                // arg is converted type-aware (char args render as glyphs).
                auto* concat_fn = this->mod->getFunction("__ang_string_concat");
                llvm::Value* result = nullptr;
                for (auto& a : getArgs(expr)) {
                    auto* str_val = toStrTyped(a);
                    if (!result) {
                        result = str_val;
                    } else {
                        result = builder->CreateCall(concat_fn, {result, str_val}, "cat");
                    }
                }
                // Call with stdout stream ID (1)
                auto* stream_id = makeI64(1);
                callRtByName(rt, {stream_id, result});
            }
            return makeNil();
        }
        if (namedVals.find(sanitize(fn)) != namedVals.end()) {
            auto type_it = m_type_checker.getExpressionTypes().find(expr.callee.get());
            bool is_callable = false;
            if (type_it != m_type_checker.getExpressionTypes().end()) {
                auto kind = type_it->second->kind;
                if (kind == TypeKind::FUNCTION || kind == TypeKind::ANY) {
                    is_callable = true;
                }
            }
            if (is_callable) {
                auto* callee = loadVar(fn);
                std::vector<llvm::Value*> llvmArgs;
                for (auto& a : getArgs(expr)) llvmArgs.push_back(cg(a));
                return cgClosureCall(callee, llvmArgs);
            }
        }

        if (fn == "Exception") {
            std::vector<llvm::Value*> args;
            for (auto& a : getArgs(expr)) args.push_back(cg(a));
            if (args.empty()) args.push_back(makeNil());
            return callRtByName("__ang_exception_new", args);
        }

        if (fn == "Mutex") {
            return callRtByName("__ang_mutex_new", {});
        }

        if (fn == "spawn") {
            if (!getArgs(expr).empty()) {
                auto* closure = cg(getArgs(expr)[0]);
                int n_args = (int)getArgs(expr).size() - 1;
                if (n_args > 0) {
                    // Allocate heap array for the extra arguments
                    auto* malloc_fn = this->mod->getFunction("malloc");
                    auto* arr_size = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx),
                        (uint64_t)n_args * 16); // sizeof(AngaraObject) = 16
                    auto* args_mem = builder->CreateCall(malloc_fn, {arr_size}, "spawn_args");
                    auto* arr_type = llvm::ArrayType::get(objType, n_args);
                    for (int i = 0; i < n_args; i++) {
                        auto* elem_ptr = builder->CreateGEP(arr_type, args_mem,
                            {llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), 0),
                             llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), i)});
                        builder->CreateStore(cg(getArgs(expr)[i + 1]), elem_ptr);
                    }
                    return callRtByName("__ang_spawn_thread", {
                        closure,
                        llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), n_args),
                        args_mem
                    });
                }
                return callRtByName("__ang_spawn_thread", {
                    closure,
                    llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), 0),
                    llvm::ConstantPointerNull::get(llvm::PointerType::get(*ctx, 0))
                });
            }
            return makeNil();
        }

        auto cit = constructorLookup.find(fn);
        if (cit != constructorLookup.end()) {
            llvm::Function* ctor = this->mod->getFunction(cit->second);
            if (ctor) {
                std::vector<llvm::Value*> args;
                for (auto& a : getArgs(expr)) args.push_back(cg(a));
                auto* ft = ctor->getFunctionType();
                while (args.size() < ft->getNumParams()) args.push_back(makeNil());
                return builder->CreateCall(ctor, args);
            }
        }
        // Check for variadic foreign function
        auto vfit = m_variadic_foreign_funcs.find(fn);
        if (vfit != m_variadic_foreign_funcs.end()) {
            return callVariadicForeignFn(fn, vfit->second, getArgs(expr));
        }
        // TS-1: gather param types so callModuleFn can box trait/contract args.
        const std::vector<std::shared_ptr<Type>>* param_types = nullptr;
        auto ctit = m_type_checker.getExpressionTypes().find(expr.callee.get());
        if (ctit != m_type_checker.getExpressionTypes().end() &&
            ctit->second && ctit->second->kind == TypeKind::FUNCTION) {
            param_types = &std::dynamic_pointer_cast<FunctionType>(ctit->second)->param_types;
        }
        // TS-1/C4: arg indices to box into trait objects (generic fn with bounds).
        const std::vector<std::pair<size_t, std::shared_ptr<TraitType>>>* boxed_idx = nullptr;
        auto bit = m_type_checker.getGenericBoxedArgs().find(&expr);
        if (bit != m_type_checker.getGenericBoxedArgs().end()) boxed_idx = &bit->second;
        return callModuleFn(moduleName, fn, getArgs(expr), param_types, boxed_idx);
    }
    return makeNil();
}

llvm::Value* LLVMBackend::callModuleFn(const std::string& mod, const std::string& fn,
                                        const std::vector<std::shared_ptr<Expr>>& args,
                                        const std::vector<std::shared_ptr<Type>>* param_types,
                                        const std::vector<std::pair<size_t, std::shared_ptr<TraitType>>>* boxed_idx) {
    std::string mangled = mangle(mod, fn);
    llvm::Function* f = this->mod->getFunction(mangled);
    if (!f) f = this->mod->getFunction("__ang_"+sanitize(fn));

    // If no fixed-arity wrapper exists, check for the native (argc, ptr)
    // convention — this is how variadic module functions are called.
    if (!f) {
        std::string native_name = "Angara_" + mod + "_" + fn;
        // Native functions live in .so files, not in the LLVM IR module.
        // Declare them as external with the native calling convention.
        auto* native_ft = llvm::FunctionType::get(objType,
            {llvm::Type::getInt32Ty(*ctx), llvm::PointerType::get(*ctx, 0)}, false);
        f = llvm::Function::Create(native_ft, llvm::Function::ExternalLinkage, native_name, this->mod.get());
    }

    if (!f) {
        // Check if we know this is a raw-signature function
        auto raw_it = m_raw_functions.find(mangled);
        if (raw_it != m_raw_functions.end()) {
            auto& info = raw_it->second;
            std::vector<llvm::Type*> ptypes;
            for (auto& k : info.param_kinds) ptypes.push_back(llvmTypeForLocalKind(k));
            auto* fnTy = llvm::FunctionType::get(llvmTypeForLocalKind(info.return_kind), ptypes, false);
            f = llvm::Function::Create(fnTy, llvm::Function::ExternalLinkage, mangled, this->mod.get());
        } else {
            std::string declName = mangled;
            auto* fnTy = llvm::FunctionType::get(objType,
                std::vector<llvm::Type*>(args.size(), objType), false);
            f = llvm::Function::Create(fnTy, llvm::Function::ExternalLinkage, declName, this->mod.get());
        }
    }

    auto ft = f->getFunctionType();

    // Check for native module calling convention (i32 argc, void* args)
    if (ft->getNumParams() == 2 &&
        ft->getParamType(0)->isIntegerTy(32) &&
        ft->getParamType(1)->isPointerTy()) {
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

    // RT-3: capture the tail-position signal now and clear the member, so a
    // call nested inside argument evaluation doesn't consume it. Only the
    // outermost return-position call (the one cgReturn flagged) is marked.
    auto pending_tail = m_pending_tail;
    m_pending_tail.reset();

    // Check if this is a raw-signature function
    auto raw_it = m_raw_functions.find(mangled);
    if (raw_it != m_raw_functions.end()) {
        auto& info = raw_it->second;
        std::vector<llvm::Value*> llvmArgs;
        for (size_t i = 0; i < args.size() && i < info.param_kinds.size(); i++) {
            auto* boxed = cg(args[i]);
            // FFI marshalling: if this is a foreign func carrying the param's
            // semantic type, marshal string→char* and pointers via marshalAngaraToC
            // (unboxToRaw only handles numeric primitives).
            if (!info.param_types.empty() && i < info.param_types.size()
                && info.param_kinds[i] == LocalKind::RAW_PTR) {
                llvmArgs.push_back(marshalAngaraToC(boxed, info.param_types[i]));
            } else {
                llvmArgs.push_back(unboxToRaw(boxed, info.param_kinds[i]));
            }
        }
        while (llvmArgs.size() < ft->getNumParams()) {
            auto kind = llvmArgs.size() < info.param_kinds.size()
                ? info.param_kinds[llvmArgs.size()] : LocalKind::RAW_I64;
            llvmArgs.push_back(llvm::ConstantInt::get(llvmTypeForLocalKind(kind), 0));
        }
        auto* raw_result = builder->CreateCall(f, llvmArgs);
        // RT-3: best-effort tail hint on the raw-signature path (musttail is
        // illegal here — boxRaw intervenes between call and ret).
        if (pending_tail) raw_result->setTailCallKind(llvm::CallInst::TCK_Tail);
        return boxRaw(raw_result, info.return_kind);
    }

    // Standard boxed call
    std::vector<llvm::Value*> llvmArgs;
    for (size_t i = 0; i < args.size(); i++) {
        auto* v = cg(args[i]);
        // TS-1: box into a trait object if this parameter is trait/contract-typed.
        if (param_types && i < param_types->size()) {
            v = maybeBoxTraitObject(v, args[i].get(), (*param_types)[i]);
        }
        // TS-1/C4: box a generic-fn bounded arg into a trait object (the bound
        // trait is the boxing target) so the body receives a trait object.
        if (boxed_idx) {
            for (const auto& [idx, trait] : *boxed_idx) {
                if (idx == i && trait) {
                    v = maybeBoxTraitObject(v, args[i].get(), trait);
                    break;
                }
            }
        }
        llvmArgs.push_back(v);
    }
    // RT-3: if this call is in tail position, decide the tail kind before
    // padding (musttail requires exact arity — no makeNil() padding). Promote
    // to TCK_MustTail (guaranteed TCO) only when: callee is on the boxed ABI
    // (not a raw-signature fn), the call supplies exactly ft->getNumParams()
    // args, and the callee returns the boxed objType. Otherwise TCK_Tail.
    llvm::CallInst::TailCallKind tck = llvm::CallInst::TCK_None;
    if (pending_tail) {
        bool exact_arity = (llvmArgs.size() == ft->getNumParams());
        bool callee_boxed = (m_raw_functions.find(mangled) == m_raw_functions.end());
        bool callee_returns_obj = ft->getReturnType() == objType;
        tck = (exact_arity && callee_boxed && callee_returns_obj && !m_current_raw_return_kind)
            ? llvm::CallInst::TCK_MustTail
            : llvm::CallInst::TCK_Tail;
    }
    while (llvmArgs.size() < ft->getNumParams()) llvmArgs.push_back(makeNil());
    auto* call = builder->CreateCall(f, llvmArgs);
    if (tck != llvm::CallInst::TCK_None) call->setTailCallKind(tck);
    return call;
}

llvm::Value* LLVMBackend::cgGet(const GetExpr& e) {
    if (auto* var = dynamic_cast<const VarExpr*>(e.object.get())) {
        auto type_it = m_type_checker.getExpressionTypes().find(e.object.get());
        if (type_it != m_type_checker.getExpressionTypes().end()) {
            std::shared_ptr<EnumType> enum_type;
            if (type_it->second->kind == TypeKind::ENUM) {
                enum_type = std::dynamic_pointer_cast<EnumType>(type_it->second);
            } else if (type_it->second->kind == TypeKind::GENERIC_INSTANCE) {
                // LANG-8: unwrap GenericInstanceType to get base EnumType
                auto gi = std::dynamic_pointer_cast<GenericInstanceType>(type_it->second);
                if (gi && gi->base_type->kind == TypeKind::ENUM) {
                    enum_type = std::dynamic_pointer_cast<EnumType>(gi->base_type);
                }
            }
            if (enum_type) {
                std::string global_name = "Angara_enum_" + enum_type->name + "_" + e.name.lexeme;
                auto* global = mod->getGlobalVariable(global_name, true);
                if (global) {
                    return builder->CreateLoad(objType, global);
                }
            }
        }
    }

    auto* obj = cg(e.object);

    // Optional chaining (?.): short-circuit to nil if the object is nil
    if (e.op.type == TokenType::QUESTION_DOT) {
        auto* fn = builder->GetInsertBlock()->getParent();
        auto* entryBB = builder->GetInsertBlock();
        auto* accessBB = llvm::BasicBlock::Create(*ctx, "opt_access", fn);
        auto* mergeBB = llvm::BasicBlock::Create(*ctx, "opt_merge", fn);

        auto* tag = getTag(obj);
        auto* is_nil = builder->CreateICmpEQ(tag, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), 0));
        builder->CreateCondBr(is_nil, mergeBB, accessBB);

        builder->SetInsertPoint(accessBB);
        llvm::Value* fieldResult;
        if (e.name.lexeme == "message") {
            fieldResult = callRtByName("__ang_exception_get_message", {obj});
        } else {
            fieldResult = callRtByName("__ang_record_get", {obj, builder->CreateGlobalString(e.name.lexeme)});
        }
        auto* accessEndBB = builder->GetInsertBlock();
        builder->CreateBr(mergeBB);

        builder->SetInsertPoint(mergeBB);
        auto* phi = builder->CreatePHI(objType, 2);
        phi->addIncoming(makeNil(), entryBB);
        phi->addIncoming(fieldResult, accessEndBB);
        return phi;
    }

    if (e.name.lexeme == "message") {
        return callRtByName("__ang_exception_get_message", {obj});
    }

    // Check if the object is a foreign data type — use GEP-based field access
    auto type_it = m_type_checker.getExpressionTypes().find(e.object.get());
    if (type_it != m_type_checker.getExpressionTypes().end() && type_it->second->kind == TypeKind::DATA) {
        auto dt = std::dynamic_pointer_cast<DataType>(type_it->second);
        if (dt && dt->is_foreign && !dt->is_opaque) {
            return cgForeignFieldAccess(e, dt);
        }
    }

    return callRtByName("__ang_record_get", {obj, builder->CreateGlobalString(e.name.lexeme)});
}

llvm::Value* LLVMBackend::cgList(const ListExpr& e) {
    // SIMD-1: if this list literal has a raw array type (e.g., expected f64[]),
    // create an unboxed raw array instead of a boxed list.
    auto lt = m_type_checker.getExpressionTypes().find(&e);
    if (lt != m_type_checker.getExpressionTypes().end() && lt->second &&
        lt->second->kind == TypeKind::RAW_ARRAY) {
        auto raw_arr_type = std::dynamic_pointer_cast<RawArrayType>(lt->second);
        if (raw_arr_type && raw_arr_type->element_type) {
            auto elem_kind = localKindForType(raw_arr_type->element_type);
            if (elem_kind != LocalKind::BOXED) {
                auto elem_size = (int64_t)getIntBitWidth(raw_arr_type->element_type) / 8;
                if (elem_size == 0) elem_size = 8;  // f64 default
                auto count = (int64_t)e.elements.size();

                auto* arr = callRtByName("__ang_raw_array_new", {
                    llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), elem_size),
                    llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), count)
                });
                for (auto& el : e.elements) {
                    auto* v = cg(el);
                    // Pass the raw i64 payload (bits) — __ang_raw_array_push uses
                    // memcpy with elem_size to store the right number of bytes.
                    auto* raw_bits = getI64(v);
                    callRtByName("__ang_raw_array_push", {arr, raw_bits});
                }
                return arr;
            }
        }
    }

    auto* l = callRtByName("__ang_list_new",{});
    // TS-1: if this list's element type is a trait/contract, box each element
    // into a trait object as it's pushed (so list<Drawable> holds trait objects).
    // Prefer the downward-flowing expected element type (set when the list is
    // assigned to a typed slot); fall back to the list's own inferred element.
    std::shared_ptr<Type> elem_type = m_expected_list_elem_type;
    if (!elem_type) {
        if (lt != m_type_checker.getExpressionTypes().end() && lt->second &&
            lt->second->kind == TypeKind::LIST) {
            elem_type = std::dynamic_pointer_cast<ListType>(lt->second)->element_type;
        }
    }
    for (auto& el : e.elements) {
        auto* v = cg(el);
        if (elem_type) v = maybeBoxTraitObject(v, el.get(), elem_type);
        callRtByName("__ang_list_push",{l, v});
    }
    return l;
}

// LANG-10: tuple literal — reuses list runtime (__ang_list_new + push each element).
llvm::Value* LLVMBackend::cgTuple(const TupleExpr& e) {
    auto* t = callRtByName("__ang_list_new",{});
    for (auto& el : e.elements) {
        auto* v = cg(el);
        callRtByName("__ang_list_push",{t, v});
    }
    return t;
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
    // Nil coalescing (??): if left is nil, use right; otherwise use left
    if (e.op.type == TokenType::QUESTION_QUESTION) {
        auto* leftBB = builder->GetInsertBlock();
        auto* rhsBB = llvm::BasicBlock::Create(*ctx, "ncoalesce_r", fn);
        auto* mergeBB = llvm::BasicBlock::Create(*ctx, "ncoalesce_m", fn);
        auto* tag = getTag(l);
        auto* is_nil = builder->CreateICmpEQ(tag, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), 0));
        builder->CreateCondBr(is_nil, rhsBB, mergeBB);
        builder->SetInsertPoint(rhsBB);
        auto* r = cg(e.right);
        auto* rhsEndBB = builder->GetInsertBlock();
        builder->CreateBr(mergeBB);
        builder->SetInsertPoint(mergeBB);
        auto* phi = builder->CreatePHI(objType, 2);
        phi->addIncoming(l, leftBB);
        phi->addIncoming(r, rhsEndBB);
        return phi;
    }
    return l;
}

llvm::Value* LLVMBackend::cgSubscript(const SubscriptExpr& e) {
    auto* obj = cg(e.object);
    if (auto* lit = dynamic_cast<const Literal*>(e.index.get())) {
        if (lit->token.type == TokenType::STRING) {
            return callRtByName("__ang_record_get", {obj, builder->CreateGlobalString(lit->token.lexeme)});
        }
    }
    // Check if the object is a record type to dispatch correctly for variable keys
    auto type_it = m_type_checker.getExpressionTypes().find(e.object.get());
    bool is_record = type_it != m_type_checker.getExpressionTypes().end() &&
        (type_it->second->kind == TypeKind::RECORD ||
         type_it->second->kind == TypeKind::INSTANCE ||
         type_it->second->kind == TypeKind::GENERIC_INSTANCE);
    if (is_record) {
        // For record with non-literal key, extract C string from boxed key
        auto* key_obj = cg(e.index);
        auto* fn_as_cstr = this->mod->getFunction("__ang_api_as_cstr");
        if (fn_as_cstr) {
            auto* key_cstr = builder->CreateCall(fn_as_cstr, {key_obj});
            return callRtByName("__ang_record_get", {obj, key_cstr});
        }
        // Fallback: treat as list get
    }

    // SIMD-1: unboxed dynamic array — emit direct GEP + load instead of
    // __ang_list_get. This produces straight-line pointer arithmetic over
    // contiguous memory that LLVM's auto-vectorizer can optimize.
    if (type_it != m_type_checker.getExpressionTypes().end() &&
        type_it->second->kind == TypeKind::RAW_ARRAY) {
        auto raw_arr_type = std::dynamic_pointer_cast<RawArrayType>(type_it->second);
        if (!raw_arr_type || !raw_arr_type->element_type) goto fallback_list;

        auto elem_kind = localKindForType(raw_arr_type->element_type);
        if (elem_kind == LocalKind::BOXED) goto fallback_list;  // unsupported element type

        auto* elem_llvm_ty = llvmTypeForLocalKind(elem_kind);

        // Unbox index: extract i64 payload
        auto* idx_obj = cg(e.index);
        auto* idx_val = getI64(idx_obj);

        // Unbox array: extract heap pointer from AngaraObject payload
        auto* payload = builder->CreateExtractValue(obj, {1}, "arr_payload");
        auto* arr_ptr = builder->CreateIntToPtr(payload,
            llvm::PointerType::get(*ctx, 0), "arr_ptr");

        // Load the element buffer pointer (field 4 of AngaraRawArray)
        auto* buf_ptr = builder->CreateLoad(llvm::PointerType::get(*ctx, 0),
            builder->CreateStructGEP(rt->getRawArrayType(), arr_ptr, 4), "buf_ptr");

        // GEP into the typed buffer: &buf[idx]
        auto* elem_ptr = builder->CreateGEP(elem_llvm_ty, buf_ptr, {idx_val}, "elem_ptr");

        // Load the raw element value
        auto* raw_val = builder->CreateLoad(elem_llvm_ty, elem_ptr, "elem_val");

        // Box back into AngaraObject
        return boxRaw(raw_val, elem_kind);
    }

    // SIMD-5: vector subscript — extract a single element from the vector
    if (type_it != m_type_checker.getExpressionTypes().end() &&
        type_it->second->kind == TypeKind::VECTOR) {
        auto vec_type = std::dynamic_pointer_cast<VectorType>(type_it->second);
        if (!vec_type || !vec_type->element_type) goto fallback_list;

        // Extract the raw LLVM vector from the boxed object
        auto* raw_vec = extractVector(obj, *vec_type);

        // Get the index as i64
        auto* idx_obj = cg(e.index);
        auto* idx_val = getI64(idx_obj);
        // Truncate to i32 for extractelement (LLVM requires i32 index)
        auto* idx_i32 = builder->CreateTrunc(idx_val,
            llvm::Type::getInt32Ty(*ctx), "idx_i32");

        // Extract the scalar element
        auto* scalar = builder->CreateExtractElement(raw_vec, idx_i32, "vec_elem");

        // Box the scalar back into AngaraObject
        auto elem_kind = localKindForType(vec_type->element_type);
        return boxRaw(scalar, elem_kind);
    }

    fallback_list:
    return callRtByName("__ang_list_get", {obj, cg(e.index)});
}

llvm::Value* LLVMBackend::cgRecord(const RecordExpr& e) {
    auto* r = callRtByName("__ang_record_new",{});
    for (size_t i=0; i<e.keys.size(); i++)
        callRtByName("__ang_record_set", {r, builder->CreateGlobalString(e.keys[i].lexeme), cg(e.values[i])});
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
    // Resolve the target type name from the AST
    std::string type_name;
    if (auto* simple = dynamic_cast<const SimpleType*>(e.type.get())) {
        type_name = simple->name.lexeme;
    }

    auto* obj = cg(e.object);
    auto* tag = getTag(obj);
    auto* i32_ty = llvm::Type::getInt32Ty(*ctx);

    // Primitive types: check the top-level tag directly
    if (type_name == "nil") {
        return makeBool(builder->CreateICmpEQ(tag, llvm::ConstantInt::get(i32_ty, TAG_NIL)));
    }
    if (type_name == "bool") {
        return makeBool(builder->CreateICmpEQ(tag, llvm::ConstantInt::get(i32_ty, TAG_BOOL)));
    }
    if (type_name == "i64" || type_name == "i32" || type_name == "i16" || type_name == "i8" ||
        type_name == "u64" || type_name == "u32" || type_name == "u16" || type_name == "u8") {
        return makeBool(builder->CreateICmpEQ(tag, llvm::ConstantInt::get(i32_ty, TAG_I64)));
    }
    if (type_name == "f64" || type_name == "f32") {
        return makeBool(builder->CreateICmpEQ(tag, llvm::ConstantInt::get(i32_ty, TAG_F64)));
    }

    // Object types: must be TAG_OBJ, then check the ObjHeader sub-type.
    // We need conditional branches to avoid dereferencing non-pointer payloads.
    auto* is_obj = builder->CreateICmpEQ(tag, llvm::ConstantInt::get(i32_ty, TAG_OBJ));

    // Helper: check if an object type matches a specific OBJ_* subtype.
    // Generates: if (is_obj) { check subtype } else { false }
    auto checkObjSubtype = [&](int expected_subtype) -> llvm::Value* {
        auto* fn = builder->GetInsertBlock()->getParent();
        auto* check_bb = llvm::BasicBlock::Create(*ctx, "is_check", fn);
        auto* merge_bb = llvm::BasicBlock::Create(*ctx, "is_merge", fn);
        auto* cont_bb = llvm::BasicBlock::Create(*ctx, "is_cont", fn);

        builder->CreateCondBr(is_obj, check_bb, cont_bb);

        builder->SetInsertPoint(check_bb);
        auto* payload = builder->CreateExtractValue(obj, {1});
        auto* ptr = builder->CreateIntToPtr(payload, llvm::PointerType::get(*ctx, 0));
        auto* obj_type = builder->CreateLoad(i32_ty, ptr, "obj_subtype");
        auto* subtype_match = builder->CreateICmpEQ(obj_type,
            llvm::ConstantInt::get(i32_ty, expected_subtype));
        builder->CreateBr(merge_bb);

        builder->SetInsertPoint(cont_bb);
        builder->CreateBr(merge_bb);

        builder->SetInsertPoint(merge_bb);
        auto* phi = builder->CreatePHI(llvm::Type::getInt1Ty(*ctx), 2, "is_result");
        phi->addIncoming(subtype_match, check_bb);
        phi->addIncoming(llvm::ConstantInt::getFalse(*ctx), cont_bb);
        return makeBool(phi);
    };

    if (type_name == "string") {
        return checkObjSubtype(OBJ_STRING);
    }
    if (type_name == "list") {
        return checkObjSubtype(OBJ_LIST);
    }
    if (type_name == "record") {
        return checkObjSubtype(OBJ_RECORD);
    }

    // TS-1: trait/contract target. A value `is Drawable` iff it is a trait
    // object (OBJ_TRAIT_OBJECT) — i.e. it has been boxed into a trait object
    // (the boxing happens at coercion sites per Phase C). Raw class instances
    // are OBJ_RECORDs with no runtime class-id, so a precise check on an
    // unboxed instance isn't possible without a class-id runtime change (which
    // accompanies the fat-pointer perf work). For trait objects this is exact.
    // Resolve whether the named type is a trait/contract.
    auto target_sym = const_cast<SymbolTable&>(m_type_checker.getSymbolTable()).resolve(type_name);
    if (target_sym && target_sym->type &&
        (target_sym->type->kind == TypeKind::TRAIT ||
         target_sym->type->kind == TypeKind::CONTRACT)) {
        return checkObjSubtype(OBJ_TRAIT_OBJECT);
    }

    // For unknown/class types, just check TAG_OBJ
    return makeBool(is_obj);
}

llvm::Value* LLVMBackend::cgCast(const CastExpr& e) {
    auto* val = cg(e.object);
    auto target_type_it = m_type_checker.getExpressionTypes().find(&e);
    auto source_type_it = m_type_checker.getExpressionTypes().find(e.object.get());

    if (target_type_it == m_type_checker.getExpressionTypes().end() ||
        source_type_it == m_type_checker.getExpressionTypes().end()) {
        return val;
    }

    auto& target = target_type_it->second;
    auto& source = source_type_it->second;

    // Pointer-to-pointer cast: reinterpret the i64 payload
    if (source->kind == TypeKind::POINTER && target->kind == TypeKind::POINTER) {
        return val; // same representation (i64 payload with TAG_I64)
    }
    // Integer-to-pointer cast
    if (isNumeric(source) && target->kind == TypeKind::POINTER) {
        return val; // already stored as i64 payload
    }
    // Pointer-to-integer cast: extract raw ptr from NativeInstance → ptrtoint
    if (source->kind == TypeKind::POINTER && isNumeric(target)) {
        auto* data_ptr = callRtByName("__ang_api_native_instance_data", {val});
        return makeI64(builder->CreatePtrToInt(data_ptr, llvm::Type::getInt64Ty(*ctx)));
    }
    // Foreign data → pointer (extract raw pointer, wrap as borrowed NativeInstance)
    if (source->kind == TypeKind::DATA && target->kind == TypeKind::POINTER) {
        auto* data_ptr = callRtByName("__ang_api_native_instance_data", {val});
        auto* name_str = builder->CreateGlobalString("borrowed_ptr");
        auto* null_fin = llvm::ConstantPointerNull::get(llvm::PointerType::get(*ctx, 0));
        return callRtByName("__ang_api_native_instance_new", {data_ptr, null_fin, name_str});
    }
    // Numeric conversion: f64 → i64 (fptosi) and i64 → f64 (sitofp)
    if (isFloat(source) && isInteger(target)) {
        return callRtByName("__ang_to_i64", {val});
    }
    if (isInteger(source) && isFloat(target)) {
        return callRtByName("__ang_to_f64", {val});
    }
    // Same-type numeric: truncation/extension within i64 is a no-op
    // (the value is already stored as i64; the type checker records the narrower type)
    return val;
}

llvm::Value* LLVMBackend::cgDeref(const DerefExpr& e) {
    auto* ptr_val = cg(e.right);

    // Get the pointee type from the type checker
    auto type_it = m_type_checker.getExpressionTypes().find(&e);
    if (type_it == m_type_checker.getExpressionTypes().end()) {
        return makeNil();
    }
    auto& pointee_type = type_it->second;

    // Extract the raw pointer from the NativeInstance wrapper
    auto* raw_ptr = callRtByName("__ang_api_native_instance_data", {ptr_val});

    // Load and wrap in AngaraObject based on pointee type
    auto* c_type = resolveCFieldType(pointee_type);
    auto* loaded = builder->CreateLoad(c_type, raw_ptr, "deref");

    // Dereferenced pointers are borrowed (not owned) — use null finalizer
    if (pointee_type->kind == TypeKind::POINTER) {
        auto* name_str = builder->CreateGlobalString("borrowed_ptr");
        auto* null_fin = llvm::ConstantPointerNull::get(llvm::PointerType::get(*ctx, 0));
        return callRtByName("__ang_api_native_instance_new", {loaded, null_fin, name_str});
    }

    return marshalCToAngara(loaded, pointee_type);
}

llvm::Value* LLVMBackend::cgMatch(const MatchExpr& e) {
    auto* subj = cg(e.condition);
    auto* fn = builder->GetInsertBlock()->getParent();
    auto* mg = llvm::BasicBlock::Create(*ctx,"me",fn);
    std::vector<std::pair<llvm::BasicBlock*,llvm::Value*>> inc;

    // Look up the condition type
    auto type_it = m_type_checker.getExpressionTypes().find(e.condition.get());
    std::shared_ptr<Type> cond_type;
    std::shared_ptr<EnumType> enum_type;
    std::shared_ptr<TupleType> tuple_type;  // LANG-10
    bool is_value_type = false;
    bool is_tuple_type = false;             // LANG-10
    if (type_it != m_type_checker.getExpressionTypes().end()) {
        cond_type = type_it->second;
        if (cond_type->kind == TypeKind::ENUM) {
            enum_type = std::dynamic_pointer_cast<EnumType>(cond_type);
        } else if (cond_type->kind == TypeKind::GENERIC_INSTANCE) {
            // LANG-8: unwrap GenericInstanceType to get base EnumType
            auto gi = std::dynamic_pointer_cast<GenericInstanceType>(cond_type);
            if (gi && gi->base_type->kind == TypeKind::ENUM) {
                enum_type = std::dynamic_pointer_cast<EnumType>(gi->base_type);
            } else {
                is_value_type = true;
            }
        } else if (cond_type->kind == TypeKind::TUPLE) {
            // LANG-10: tuple match
            tuple_type = std::dynamic_pointer_cast<TupleType>(cond_type);
            is_tuple_type = true;
        } else {
            is_value_type = true;
        }
    }

    for (auto& c : e.cases) {
        auto* match_bb = llvm::BasicBlock::Create(*ctx,"mb",fn);
        auto* next_bb = llvm::BasicBlock::Create(*ctx,"mn",fn);

        // Check if any pattern in the or-group is a wildcard
        bool is_wildcard = false;
        for (const auto& pat : c.patterns) {
            if (auto ve = std::dynamic_pointer_cast<const VarExpr>(pat)) {
                if (ve->name.lexeme == "_") {
                    is_wildcard = true;
                    break;
                }
            }
        }

        if (is_wildcard) {
            // Wildcard always matches
            builder->CreateBr(match_bb);
            builder->SetInsertPoint(match_bb);

            // Bind variables if present (wildcard with bindings)
            for (size_t vi = 0; vi < c.variables.size(); ++vi) {
                auto* alloca = allocLocal(fn, sanitize(c.variables[vi].lexeme));
                if (enum_type && vi == 0 && c.patterns.size() == 1) {
                    // For wildcard on enum, bind the whole scrutinee
                    builder->CreateStore(subj, alloca);
                } else {
                    builder->CreateStore(subj, alloca);
                }
                namedVals[sanitize(c.variables[vi].lexeme)] = alloca;
            }

            cgBodyWithGuard(c, fn, mg, next_bb, inc);
            builder->SetInsertPoint(next_bb);
        } else if (is_value_type) {
            // --- Value type match (int/string/bool/char) ---
            llvm::Value* matches = nullptr;

            for (size_t pi = 0; pi < c.patterns.size(); ++pi) {
                const auto& pat = c.patterns[pi];
                llvm::Value* pat_matches = nullptr;

                if (auto lit = std::dynamic_pointer_cast<const Literal>(pat)) {
                    // Compare condition value with literal value
                    if (lit->token.type == TokenType::STRING ||
                        lit->token.type == TokenType::RAW_STRING ||
                        lit->token.type == TokenType::BYTE_STRING) {
                        // String comparison via __ang_equals
                        auto* lit_val = cgLiteral(*lit);
                        auto* eq_obj = callRtByName("__ang_equals", {subj, lit_val});
                        pat_matches = getBool(eq_obj);
                    } else if (lit->token.type == TokenType::TRUE ||
                               lit->token.type == TokenType::FALSE) {
                        auto* subj_bool = getBool(subj);
                        bool target = (lit->token.type == TokenType::TRUE);
                        pat_matches = builder->CreateICmpEQ(subj_bool,
                            llvm::ConstantInt::get(llvm::Type::getInt1Ty(*ctx), target ? 1 : 0));
                    } else if (lit->token.type == TokenType::NUMBER_INT ||
                               lit->token.type == TokenType::CHAR) {
                        // INTEGER or CHAR: extract i64 payload, compare
                        auto* subj_i64 = getI64(subj);
                        auto* lit_val = cgLiteral(*lit);
                        auto* lit_i64 = getI64(lit_val);
                        pat_matches = builder->CreateICmpEQ(subj_i64, lit_i64);
                    } else {
                        // Fallback: use __ang_equals
                        auto* lit_val = cgLiteral(*lit);
                        auto* eq_obj = callRtByName("__ang_equals", {subj, lit_val});
                        pat_matches = getBool(eq_obj);
                    }
                }

                // Combine or-patterns: any match succeeds
                if (pat_matches) {
                    matches = matches
                        ? builder->CreateOr(matches, pat_matches)
                        : pat_matches;
                }
            }

            if (matches) {
                builder->CreateCondBr(matches, match_bb, next_bb);
            } else {
                builder->CreateBr(next_bb);
            }

            builder->SetInsertPoint(match_bb);
            cgBodyWithGuard(c, fn, mg, next_bb, inc);
            builder->SetInsertPoint(next_bb);
        } else if (is_tuple_type && tuple_type) {
            // LANG-10: tuple pattern match — extract elements and compare
            llvm::Value* all_match = nullptr;

            for (size_t pi = 0; pi < c.patterns.size(); ++pi) {
                const auto& pat = c.patterns[pi];
                auto tup_pat = std::dynamic_pointer_cast<const TupleExpr>(pat);
                if (!tup_pat) continue;

                llvm::Value* pat_all = nullptr;
                for (size_t ei = 0; ei < tup_pat->elements.size() && ei < tuple_type->element_types.size(); ++ei) {
                    const auto& sub_pat = tup_pat->elements[ei];

                    // Extract element from scrutinee tuple
                    auto* idx_val = makeI64(llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), ei));
                    auto* elem_val = callRtByName("__ang_list_get", {subj, idx_val});

                    llvm::Value* elem_match = nullptr;

                    if (auto sub_ve = std::dynamic_pointer_cast<const VarExpr>(sub_pat)) {
                        if (sub_ve->name.lexeme == "_") {
                            // Wildcard always matches
                            elem_match = llvm::ConstantInt::get(llvm::Type::getInt1Ty(*ctx), 1);
                        }
                        // Variable binding — always matches (type checked already)
                        // The binding will be handled after the comparison
                        if (!elem_match) {
                            elem_match = llvm::ConstantInt::get(llvm::Type::getInt1Ty(*ctx), 1);
                        }
                    } else if (auto sub_lit = std::dynamic_pointer_cast<const Literal>(sub_pat)) {
                        // Literal comparison
                        if (sub_lit->token.type == TokenType::STRING ||
                            sub_lit->token.type == TokenType::RAW_STRING ||
                            sub_lit->token.type == TokenType::BYTE_STRING) {
                            auto* lit_val = cgLiteral(*sub_lit);
                            auto* eq_obj = callRtByName("__ang_equals", {elem_val, lit_val});
                            elem_match = getBool(eq_obj);
                        } else if (sub_lit->token.type == TokenType::TRUE ||
                                   sub_lit->token.type == TokenType::FALSE) {
                            auto* elem_bool = getBool(elem_val);
                            bool target = (sub_lit->token.type == TokenType::TRUE);
                            elem_match = builder->CreateICmpEQ(elem_bool,
                                llvm::ConstantInt::get(llvm::Type::getInt1Ty(*ctx), target ? 1 : 0));
                        } else if (sub_lit->token.type == TokenType::NUMBER_INT ||
                                   sub_lit->token.type == TokenType::CHAR) {
                            auto* elem_i64 = getI64(elem_val);
                            auto* lit_val = cgLiteral(*sub_lit);
                            auto* lit_i64 = getI64(lit_val);
                            elem_match = builder->CreateICmpEQ(elem_i64, lit_i64);
                        } else {
                            auto* lit_val = cgLiteral(*sub_lit);
                            auto* eq_obj = callRtByName("__ang_equals", {elem_val, lit_val});
                            elem_match = getBool(eq_obj);
                        }
                    }

                    if (elem_match) {
                        pat_all = pat_all
                            ? builder->CreateAnd(pat_all, elem_match)
                            : elem_match;
                    }
                }

                // Combine or-patterns
                if (pat_all) {
                    all_match = all_match
                        ? builder->CreateOr(all_match, pat_all)
                        : pat_all;
                }
            }

            if (all_match) {
                builder->CreateCondBr(all_match, match_bb, next_bb);
            } else {
                builder->CreateBr(next_bb);
            }

            // Matched case — bind variables from tuple elements
            builder->SetInsertPoint(match_bb);
            if (!c.variables.empty() && !c.patterns.empty()) {
                auto first_tup = std::dynamic_pointer_cast<const TupleExpr>(c.patterns[0]);
                if (first_tup) {
                    // Recursively bind variables by walking the pattern tree
                    std::function<void(const std::shared_ptr<const Expr>&, llvm::Value*, size_t)> bindTupleVars;
                    bindTupleVars = [&](const std::shared_ptr<const Expr>& pat, llvm::Value* val, size_t depth) {
                        if (auto* ve = dynamic_cast<const VarExpr*>(pat.get())) {
                            if (ve->name.lexeme != "_") {
                                auto* alloca = allocLocal(fn, sanitize(ve->name.lexeme));
                                builder->CreateStore(val, alloca);
                                namedVals[sanitize(ve->name.lexeme)] = alloca;
                            }
                        } else if (auto* tp = dynamic_cast<const TupleExpr*>(pat.get())) {
                            for (size_t i = 0; i < tp->elements.size(); ++i) {
                                auto* idx = makeI64(llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), i));
                                auto* sub_val = callRtByName("__ang_list_get", {val, idx});
                                bindTupleVars(tp->elements[i], sub_val, depth + 1);
                            }
                        }
                    };
                    bindTupleVars(first_tup, subj, 0);
                }
            }

            cgBodyWithGuard(c, fn, mg, next_bb, inc);
            builder->SetInsertPoint(next_bb);
        } else if (enum_type) {
            // --- Enum type match ---
            llvm::Value* or_matches = nullptr;

            for (size_t pi = 0; pi < c.patterns.size(); ++pi) {
                const auto& pat = c.patterns[pi];

                // Extract variant name and index
                std::string variant_name;
                std::string enum_name;
                // For nested patterns, the outer constructor holds the variant
                const std::shared_ptr<Expr>* pat_ptr = &pat;
                if (auto np = std::dynamic_pointer_cast<const NestedPattern>(pat)) {
                    pat_ptr = &np->constructor;
                }
                if (auto get_expr = std::dynamic_pointer_cast<const GetExpr>(*pat_ptr)) {
                    variant_name = get_expr->name.lexeme;
                    if (auto lhs = std::dynamic_pointer_cast<const VarExpr>(get_expr->object)) {
                        enum_name = lhs->name.lexeme;
                    }
                } else if (auto ve = std::dynamic_pointer_cast<const VarExpr>(*pat_ptr)) {
                    // Bare variant name (for enums defined in same module)
                    variant_name = ve->name.lexeme;
                } else {
                    continue; // skip non-variant patterns in enum match
                }

                std::string qualified = enum_name.empty()
                    ? variant_name
                    : enum_name + "." + variant_name;

                auto it = enumVariantIndex.find(qualified);
                if (it == enumVariantIndex.end()) {
                    // Try looking up variant directly from enum type
                    auto vit = enum_type->variants.find(variant_name);
                    if (vit != enum_type->variants.end()) {
                        // We don't know the index, skip for now
                        // (should not happen since codegenEnumDecl populates enumVariantIndex)
                    }
                    continue;
                }
                int variant_index = it->second;

                // Generate discriminant comparison (same as before)
                auto* subj_tag = getTag(subj);
                auto* tag_is_obj = builder->CreateICmpEQ(subj_tag,
                    llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), TAG_OBJ));

                auto* obj_path_bb = llvm::BasicBlock::Create(*ctx,"mop",fn);
                auto* i64_path_bb = llvm::BasicBlock::Create(*ctx,"mip",fn);
                auto* cmp_bb = llvm::BasicBlock::Create(*ctx,"mc",fn);
                builder->CreateCondBr(tag_is_obj, obj_path_bb, i64_path_bb);

                builder->SetInsertPoint(obj_path_bb);
                auto* tag_field = callRtByName("__ang_record_get",
                    {subj, builder->CreateGlobalString("__tag")});
                auto* obj_disc = getI64(tag_field);
                builder->CreateBr(cmp_bb);

                builder->SetInsertPoint(i64_path_bb);
                auto* i64_disc = getI64(subj);
                builder->CreateBr(cmp_bb);

                builder->SetInsertPoint(cmp_bb);
                auto* disc_phi = builder->CreatePHI(llvm::Type::getInt64Ty(*ctx), 2);
                disc_phi->addIncoming(obj_disc, obj_path_bb);
                disc_phi->addIncoming(i64_disc, i64_path_bb);

                auto* target_index = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), variant_index);
                auto* pat_matches = builder->CreateICmpEQ(disc_phi, target_index);

                or_matches = or_matches
                    ? builder->CreateOr(or_matches, pat_matches)
                    : pat_matches;
            }

            if (or_matches) {
                builder->CreateCondBr(or_matches, match_bb, next_bb);
            } else {
                builder->CreateBr(next_bb);
            }

            // Matched case — bind payload variables
            builder->SetInsertPoint(match_bb);

            if (!c.variables.empty()) {
                // Check if any pattern is a nested pattern
                bool has_nested = false;
                for (const auto& pat : c.patterns) {
                    if (dynamic_cast<const NestedPattern*>(pat.get())) {
                        has_nested = true;
                        break;
                    }
                }

                if (has_nested) {
                    // Nested pattern: recursively extract payload through the
                    // nested enum layers, then bind the leaf variables.
                    for (const auto& pat : c.patterns) {
                        if (auto* np = dynamic_cast<const NestedPattern*>(pat.get())) {
                            // Walk the nested pattern, extracting payload at each level
                            llvm::Value* cur_subj = subj;
                            const NestedPattern* cur_np = np;
                            while (cur_np && !cur_np->subpatterns.empty()) {
                                // Extract the first payload field from the current subject
                                auto* payload_val = callRtByName("__ang_record_get",
                                    {cur_subj, builder->CreateGlobalString("_0")});
                                cur_subj = payload_val;

                                // Move to the next nested level
                                auto& next = cur_np->subpatterns.back();
                                cur_np = dynamic_cast<const NestedPattern*>(next.get());
                            }
                            // After extraction, cur_subj holds the innermost payload.
                            // If the leaf is a single variable, bind it directly.
                            // Otherwise, extract fields from the innermost enum.
                            bool leaf_is_single_var = cur_np == nullptr &&
                                c.variables.size() == 1;
                            if (leaf_is_single_var) {
                                // Bind the single variable directly to cur_subj
                                auto* alloca = allocLocal(fn, sanitize(c.variables[0].lexeme));
                                builder->CreateStore(cur_subj, alloca);
                                namedVals[sanitize(c.variables[0].lexeme)] = alloca;
                            } else {
                                // Bind variables from the innermost record fields
                                for (size_t vi = 0; vi < c.variables.size(); ++vi) {
                                    std::string field_name = "_" + std::to_string(vi);
                                    auto* payload_val = callRtByName("__ang_record_get",
                                        {cur_subj, builder->CreateGlobalString(field_name)});
                                    auto* alloca = allocLocal(fn, sanitize(c.variables[vi].lexeme));
                                    builder->CreateStore(payload_val, alloca);
                                    namedVals[sanitize(c.variables[vi].lexeme)] = alloca;
                                }
                            }
                            break;  // use first nested pattern
                        }
                    }
                } else {
                    // Flat pattern: bind variables directly from scrutinee
                    for (size_t vi = 0; vi < c.variables.size(); ++vi) {
                        std::string field_name = "_" + std::to_string(vi);
                        auto* payload_val = callRtByName("__ang_record_get",
                            {subj, builder->CreateGlobalString(field_name)});
                        auto* alloca = allocLocal(fn, sanitize(c.variables[vi].lexeme));
                        builder->CreateStore(payload_val, alloca);
                        namedVals[sanitize(c.variables[vi].lexeme)] = alloca;
                    }
                }
            }

            cgBodyWithGuard(c, fn, mg, next_bb, inc);
            builder->SetInsertPoint(next_bb);
        }
    }

    // Default fallthrough (no case matched)
    builder->CreateBr(mg);
    inc.push_back({builder->GetInsertBlock(), makeNil()});
    builder->SetInsertPoint(mg);
    auto* phi = builder->CreatePHI(objType, inc.size());
    for (auto& [b,v] : inc) phi->addIncoming(v,b);
    return phi;
}

// Helper: generate guard check + body for a matched case
void LLVMBackend::cgBodyWithGuard(
    const MatchCase& c,
    llvm::Function* fn,
    llvm::BasicBlock* mg,
    llvm::BasicBlock* next_bb,
    std::vector<std::pair<llvm::BasicBlock*, llvm::Value*>>& inc)
{
    if (c.guard) {
        // Evaluate guard; if false, jump to next_bb (try next case)
        auto* guard_bb = llvm::BasicBlock::Create(*ctx, "mgd", fn);
        builder->CreateBr(guard_bb);
        builder->SetInsertPoint(guard_bb);

        auto* guard_val = cg(*c.guard);
        auto* guard_true = isTruthy(guard_val);

        auto* body_bb = llvm::BasicBlock::Create(*ctx, "mbd", fn);
        builder->CreateCondBr(guard_true, body_bb, next_bb);

        builder->SetInsertPoint(body_bb);
        llvm::Value* r = cg(c.body);
        llvm::BasicBlock* body_end_bb = builder->GetInsertBlock();
        builder->CreateBr(mg);
        inc.push_back({body_end_bb, r});
    } else {
        llvm::Value* r = cg(c.body);
        llvm::BasicBlock* body_end_bb = builder->GetInsertBlock();
        builder->CreateBr(mg);
        inc.push_back({body_end_bb, r});
    }
}


llvm::Value* LLVMBackend::cgLambda(const LambdaExpr& e) {
    std::string lambda_fn_name = "__ang_lambda_" + std::to_string(m_lambda_counter++);

    auto* i32_ty = llvm::Type::getInt32Ty(*ctx);
    auto* ptr_ty = llvm::PointerType::get(*ctx, 0);
    auto* i64_ty = llvm::Type::getInt64Ty(*ctx);
    auto* fn_type = llvm::FunctionType::get(objType, {i32_ty, ptr_ty, ptr_ty}, false);
    auto* lambda_fn = llvm::Function::Create(fn_type, llvm::Function::PrivateLinkage,
                                              lambda_fn_name, mod.get());

    auto* saved_insert_block = builder->GetInsertBlock();

    // Collect captured variable names and their current allocas
    std::vector<std::pair<std::string, llvm::AllocaInst*>> captures;
    for (const auto& [name, alloca] : namedVals) {
        captures.emplace_back(name, alloca);
    }
    int capture_count = (int)captures.size();

    // At the call site: malloc a per-instance array and store captured values into it
    auto* capture_arr_type = llvm::ArrayType::get(objType, std::max(capture_count, 1));
    llvm::Value* env_ptr;
    if (capture_count > 0) {
        auto* capture_size = llvm::ConstantInt::get(i64_ty,
            mod->getDataLayout().getTypeAllocSize(capture_arr_type));
        auto* malloc_fn = mod->getFunction("malloc");
        if (!malloc_fn) {
            auto* malloc_type = llvm::FunctionType::get(ptr_ty, {i64_ty}, false);
            malloc_fn = llvm::Function::Create(malloc_type, llvm::Function::ExternalLinkage,
                                               "malloc", mod.get());
        }
        auto* env_mem = builder->CreateCall(malloc_fn, {capture_size});
        env_ptr = builder->CreateBitCast(env_mem, ptr_ty);

        for (int i = 0; i < capture_count; i++) {
            auto& [name, alloca] = captures[i];
            auto* elem_ptr = builder->CreateGEP(capture_arr_type, env_ptr,
                {llvm::ConstantInt::get(i64_ty, 0), llvm::ConstantInt::get(i64_ty, i)});
            auto* val = builder->CreateLoad(objType, alloca, name);
            builder->CreateStore(val, elem_ptr);
        }
    } else {
        env_ptr = llvm::ConstantPointerNull::get(ptr_ty);
    }

    // Generate the lambda function body
    auto* entry = llvm::BasicBlock::Create(*ctx, "entry", lambda_fn);
    builder->SetInsertPoint(entry);

    auto saved_values = std::move(namedVals);
    auto saved_types = std::move(namedTypes);
    auto saved_kinds = std::move(namedKinds);
    auto* saved_ret_alloca = m_inlined_main_ret_alloca;
    auto* saved_cleanup_bb = m_inlined_main_cleanup_bb;
    auto* saved_exc_chain = m_exc_chain_save;
    m_inlined_main_ret_alloca = nullptr;
    m_inlined_main_cleanup_bb = nullptr;
    namedVals.clear();
    namedTypes.clear();
    namedKinds.clear();

    // Load captured variables from the env pointer (3rd arg)
    auto* env_arg = lambda_fn->arg_begin() + 2;
    for (int i = 0; i < capture_count; i++) {
        std::string sname = sanitize(captures[i].first);
        auto* alloca = allocLocal(lambda_fn, sname);
        auto* elem_ptr = builder->CreateGEP(capture_arr_type, env_arg,
            {llvm::ConstantInt::get(i64_ty, 0), llvm::ConstantInt::get(i64_ty, i)});
        auto* val = builder->CreateLoad(objType, elem_ptr, sname);
        builder->CreateStore(val, alloca);
        namedVals[sname] = alloca;
    }

    auto* argc_arg = lambda_fn->arg_begin();
    auto* args_arg = lambda_fn->arg_begin() + 1;
    auto* lambda_arr_type = llvm::ArrayType::get(objType, e.param_names.size());

    for (size_t i = 0; i < e.param_names.size(); ++i) {
        std::string pname = sanitize(e.param_names[i].lexeme);
        auto* alloca = allocLocal(lambda_fn, pname);

        auto* elem_ptr = builder->CreateGEP(lambda_arr_type, args_arg,
            {llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), 0),
             llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), i)});
        auto* val = builder->CreateLoad(objType, elem_ptr, pname);
        builder->CreateStore(val, alloca);
        namedVals[pname] = alloca;
    }

    emitGcPushFrame(lambda_fn, 256);

    for (const auto& stmt : e.body) {
        cgStmt(stmt);
    }

    if (!builder->GetInsertBlock()->getTerminator()) {
        if (m_exc_chain_save) emitGcPopFrame();
        builder->CreateRet(makeNil());
    }

    namedVals = std::move(saved_values);
    namedTypes = std::move(saved_types);
    namedKinds = std::move(saved_kinds);
    m_inlined_main_ret_alloca = saved_ret_alloca;
    m_inlined_main_cleanup_bb = saved_cleanup_bb;
    m_exc_chain_save = saved_exc_chain;

    if (saved_insert_block) {
        builder->SetInsertPoint(saved_insert_block);
    }

    int arity = (int)e.param_names.size();
    return callRtByName("__ang_closure_new", {
        lambda_fn,
        llvm::ConstantInt::get(i32_ty, arity),
        llvm::ConstantInt::get(llvm::Type::getInt1Ty(*ctx), 0),
        env_ptr,
        llvm::ConstantInt::get(i32_ty, capture_count)
    });
}

llvm::Value* LLVMBackend::cgClosureCall(llvm::Value* callee, const std::vector<llvm::Value*>& args) {
    auto* i32_ty = llvm::Type::getInt32Ty(*ctx);
    int argc = (int)args.size();

    if (argc > 0) {
        auto* arr_type = llvm::ArrayType::get(objType, argc);
        auto* arr_alloca = builder->CreateAlloca(arr_type);

        for (int i = 0; i < argc; i++) {
            auto* elem_ptr = builder->CreateGEP(arr_type, arr_alloca,
                {llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), 0),
                 llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), i)});
            builder->CreateStore(args[i], elem_ptr);
        }

        return callRtByName("__ang_call", {
            callee,
            llvm::ConstantInt::get(i32_ty, argc),
            builder->CreateBitCast(arr_alloca, llvm::PointerType::get(*ctx, 0))
        });
    }

    return callRtByName("__ang_call", {
        callee,
        llvm::ConstantInt::get(i32_ty, 0),
        llvm::ConstantPointerNull::get(llvm::PointerType::get(*ctx, 0))
    });
}

// LANG-1: eager range materialization — builds a list<i64> from start to end.
// Used when a range appears outside a for-in (e.g. `let xs = 0..5`).
// For-in detects RangeExpr directly and skips this (zero-allocation C loop).
llvm::Value* LLVMBackend::cgRange(const RangeExpr& e) {
    auto* start = getI64(cg(e.left));
    auto* end = getI64(cg(e.right));
    bool inclusive = (e.op.type == TokenType::DOT_DOT_DOT);

    auto* fn = builder->GetInsertBlock()->getParent();
    auto* list = callRtByName("__ang_list_new", {});

    // for (i = start; i < end; i++) { push(makeI64(i)); }
    // if inclusive: i <= end
    auto* i_alloca = builder->CreateAlloca(llvm::Type::getInt64Ty(*ctx), nullptr, "range_i");
    builder->CreateStore(start, i_alloca);

    auto* cond_bb = llvm::BasicBlock::Create(*ctx, "range_cond", fn);
    auto* body_bb = llvm::BasicBlock::Create(*ctx, "range_body", fn);
    auto* done_bb = llvm::BasicBlock::Create(*ctx, "range_done", fn);
    builder->CreateBr(cond_bb);

    builder->SetInsertPoint(cond_bb);
    auto* i_val = builder->CreateLoad(llvm::Type::getInt64Ty(*ctx), i_alloca, "range_i_val");
    auto* cond = inclusive
        ? builder->CreateICmpSLE(i_val, end)
        : builder->CreateICmpSLT(i_val, end);
    builder->CreateCondBr(cond, body_bb, done_bb);

    builder->SetInsertPoint(body_bb);
    callRtByName("__ang_list_push", {list, makeI64(i_val)});
    auto* next = builder->CreateAdd(i_val, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), 1), "", false, true);
    builder->CreateStore(next, i_alloca);
    builder->CreateBr(cond_bb);

    builder->SetInsertPoint(done_bb);
    return list;
}

// LANG-3: lower an interpolated string to a left-fold concat chain using the
// existing __ang_to_string + __ang_string_concat runtime functions.
llvm::Value* LLVMBackend::cgInterpString(const InterpStringExpr& e) {
    // Start with the first literal segment.
    llvm::Value* acc = nullptr;
    for (const auto& [lit, sub_expr] : e.segments) {
        if (!sub_expr) {
            // Pure literal segment — concat or initialize.
            if (lit.empty() && !acc) {
                // Leading empty literal before an expression — skip.
                continue;
            }
            if (!acc) {
                acc = makeStr(lit);
            } else if (!lit.empty()) {
                acc = callRtByName("__ang_string_concat", {acc, makeStr(lit)});
            }
        } else {
            // Expression hole — convert to string and concat. LANG-4: type-aware
            // (a char hole renders as the glyph, not the code-point number).
            if (!acc) {
                // No preceding literal — start with the converted expression.
                acc = toStrTyped(sub_expr);
            } else {
                acc = callRtByName("__ang_string_concat",
                    {acc, toStrTyped(sub_expr)});
            }
        }
    }
    // If all segments were empty (e.g. $"") return an empty string.
    return acc ? acc : makeStr("");
}

// LIB-4 Stage S: await expression — state machine suspension point.
// On initial run: checks if child is resolved. If yes, extracts result and
// continues. If no, registers waker and suspends.
// On resume: the dispatch switch jumps to a retry block that re-extracts
// the now-resolved result and continues.
llvm::Value* LLVMBackend::cgAwait(const AwaitExpr& e) {
    if (!m_in_async_function) {
        return cg(e.future);
    }

    int cur_idx = m_async_await_idx;
    int resume_state = cur_idx + 1;
    auto* fn = builder->GetInsertBlock()->getParent();

    // Evaluate the future expression
    auto* future_val = cg(e.future);

    // Extract the child's frame via native_instance_data
    auto* child_frame_ptr = callRtByName("__ang_api_native_instance_data", {future_val});
    auto* child_typed = builder->CreateBitCast(child_frame_ptr,
        llvm::PointerType::get(*ctx, 0));

    // ---- Create all basic blocks ----
    auto* resolved_bb = llvm::BasicBlock::Create(*ctx, "await_resolved", fn);
    auto* pending_bb  = llvm::BasicBlock::Create(*ctx, "await_pending", fn);
    auto* retry_bb    = llvm::BasicBlock::Create(*ctx, "await_retry", fn);
    auto* cont_bb     = llvm::BasicBlock::Create(*ctx, "await_cont", fn);

    // ---- Check if child is resolved ----
    auto* child_state_ptr = builder->CreateStructGEP(m_current_async_frame_type,
        child_typed, 0, "child_state_p");
    auto* child_state = builder->CreateLoad(m_async_state_ty, child_state_ptr, "child_state");
    auto* is_resolved = builder->CreateICmpEQ(child_state,
        llvm::ConstantInt::get(m_async_state_ty, -1), "is_resolved");
    builder->CreateCondBr(is_resolved, resolved_bb, pending_bb);

    // ---- Resolved path (initial run, child already done) ----
    builder->SetInsertPoint(resolved_bb);
    auto* child_result_ptr = builder->CreateStructGEP(m_current_async_frame_type,
        child_typed, 1, "child_result_p");
    auto* result = builder->CreateLoad(objType, child_result_ptr, "await_result");
    builder->CreateBr(cont_bb);

    // ---- Retry path (resume after suspension, child now resolved) ----
    builder->SetInsertPoint(retry_bb);
    // Re-evaluate the future expression and extract the result.
    // The child is now resolved, so no state check needed.
    auto* retry_future_val = cg(e.future);
    auto* retry_frame_ptr = callRtByName("__ang_api_native_instance_data", {retry_future_val});
    auto* retry_typed = builder->CreateBitCast(retry_frame_ptr,
        llvm::PointerType::get(*ctx, 0));
    auto* retry_result_ptr = builder->CreateStructGEP(m_current_async_frame_type,
        retry_typed, 1, "retry_result_p");
    auto* retry_result = builder->CreateLoad(objType, retry_result_ptr, "retry_result");
    builder->CreateBr(cont_bb);

    // ---- Pending path: register waker, then suspend ----
    builder->SetInsertPoint(pending_bb);
    // Store the resume state for the dispatch switch
    builder->CreateStore(llvm::ConstantInt::get(m_async_state_ty, resume_state),
                         m_current_async_state_ptr);
    // Store awaited future in own frame (field 2)
    auto* own_typed = builder->CreateBitCast(m_current_async_frame,
        llvm::PointerType::get(*ctx, 0));
    auto* own_awaited_ptr = builder->CreateStructGEP(m_current_async_frame_type,
        own_typed, 2, "own_awaited_p");
    builder->CreateStore(future_val, own_awaited_ptr);
    // Register {&resume_fn, own_frame} as waker on child's frame (fields 3, 4)
    auto* resume_fn_val = builder->CreateBitCast(m_current_async_resume_fn,
        llvm::PointerType::get(*ctx, 0));
    auto* child_waker_fn_ptr = builder->CreateStructGEP(m_current_async_frame_type,
        child_typed, 3, "child_wfn_p");
    builder->CreateStore(resume_fn_val, child_waker_fn_ptr);
    auto* child_waker_ctx_ptr = builder->CreateStructGEP(m_current_async_frame_type,
        child_typed, 4, "child_wctx_p");
    builder->CreateStore(m_current_async_frame, child_waker_ctx_ptr);
    builder->CreateBr(m_async_suspend_bb);

    // ---- Wire up dispatch ----
    // Store the retry block as the dispatch target for resume_state
    if (cur_idx < (int)m_async_await_cont_bbs.size()) {
        m_async_await_cont_bbs[cur_idx] = retry_bb;
    }
    // Update the dispatch switch to target the retry block for this state
    if (m_async_dispatch_switch) {
        auto* case_val = llvm::ConstantInt::get(m_async_state_ty, resume_state);
        for (auto it = m_async_dispatch_switch->case_begin();
             it != m_async_dispatch_switch->case_end(); ++it) {
            if (it->getCaseValue() == case_val) {
                it->setSuccessor(retry_bb);
                break;
            }
        }
    }

    // ---- Continuation block with PHI node ----
    // Both resolved_bb and retry_bb branch here with a result value.
    // Use a PHI to merge the two SSA values so subsequent code sees one result.
    builder->SetInsertPoint(cont_bb);
    auto* phi = builder->CreatePHI(objType, 2, "await_val");
    phi->addIncoming(result, resolved_bb);
    phi->addIncoming(retry_result, retry_bb);

    m_async_await_idx = resume_state;

    // Return the PHI value — this is the result of the await expression
    return phi;
}

}
