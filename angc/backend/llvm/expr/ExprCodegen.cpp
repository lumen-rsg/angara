#include "LLVMBackend.h"
#include "RuntimeBuilder.h"
#include <llvm/IR/Intrinsics.h>

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
                auto* result = builder->CreateCall(target_fn, direct_args);
                builder->CreateRet(result);
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
    return makeNil();
}

llvm::Value* LLVMBackend::cgLiteral(const Literal& e) {
    const auto& tok = e.token;
    if (tok.type == TokenType::NIL) return makeNil();
    if (tok.type == TokenType::TRUE) return makeBool(true);
    if (tok.type == TokenType::FALSE) return makeBool(false);
    if (tok.type == TokenType::NUMBER_INT) {
        try { return makeI64(std::stoll(tok.lexeme, nullptr, 0)); }
        catch (const std::exception&) { return makeI64(static_cast<int64_t>(0)); }
    }
    if (tok.type == TokenType::NUMBER_FLOAT) {
        try { return makeF64(std::stod(tok.lexeme)); }
        catch (const std::exception&) { return makeF64(0.0); }
    }
    if (tok.type == TokenType::STRING) return makeStr(tok.lexeme);
    return makeNil();
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

    switch (e.op.type) {
        case TokenType::PLUS: {
            // Fast path: typed primitives — skip tag dispatch entirely
            if (types_known) {
                auto& ltype = lt_it->second;
                auto& rtype = rt_it->second;
                if (isInteger(ltype) && isInteger(rtype))
                    return makeI64(builder->CreateAdd(getI64(l), getI64(r)));
                if (isFloat(ltype) || isFloat(rtype)) {
                    auto* ld = isFloat(ltype) ? getF64(l) : builder->CreateSIToFP(getI64(l), f64_ty);
                    auto* rd = isFloat(rtype) ? getF64(r) : builder->CreateSIToFP(getI64(r), f64_ty);
                    return makeF64(builder->CreateFAdd(ld, rd));
                }
            }
            // If the type checker knows either operand is a string, skip the
            // runtime tag dispatch and call __ang_string_concat directly.
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
            auto* ia = makeI64(builder->CreateAdd(getI64(l),getI64(r)));
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
            // Fast path: typed primitives
            if (types_known) {
                auto& ltype = lt_it->second;
                auto& rtype = rt_it->second;
                if (isInteger(ltype) && isInteger(rtype))
                    return makeI64(builder->CreateSub(getI64(l), getI64(r)));
                if (isFloat(ltype) || isFloat(rtype)) {
                    auto* ld = isFloat(ltype) ? getF64(l) : builder->CreateSIToFP(getI64(l), f64_ty);
                    auto* rd = isFloat(rtype) ? getF64(r) : builder->CreateSIToFP(getI64(r), f64_ty);
                    return makeF64(builder->CreateFSub(ld, rd));
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
            auto* ia = makeI64(builder->CreateSub(getI64(l),getI64(r)));
            isubBB = builder->GetInsertBlock();
            builder->CreateBr(msubBB);
            builder->SetInsertPoint(msubBB);
            auto* phi = builder->CreatePHI(objType,2);
            phi->addIncoming(fa,fsubBB); phi->addIncoming(ia,isubBB);
            return phi;
        }
        case TokenType::STAR: {
            // Fast path: typed primitives
            if (types_known) {
                auto& ltype = lt_it->second;
                auto& rtype = rt_it->second;
                if (isInteger(ltype) && isInteger(rtype))
                    return makeI64(builder->CreateMul(getI64(l), getI64(r)));
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
            auto* ia = makeI64(builder->CreateMul(getI64(l),getI64(r)));
            imulBB = builder->GetInsertBlock();
            builder->CreateBr(mmulBB);
            builder->SetInsertPoint(mmulBB);
            auto* phi = builder->CreatePHI(objType,2);
            phi->addIncoming(fa,fmulBB); phi->addIncoming(ia,imulBB);
            return phi;
        }
        case TokenType::SLASH: {
            // Fast path: typed primitives
            if (types_known) {
                auto& ltype = lt_it->second;
                auto& rtype = rt_it->second;
                if (isInteger(ltype) && isInteger(rtype)) {
                    bool unsign = isUnsignedIntType(ltype) || isUnsignedIntType(rtype);
                    return makeI64(unsign ? builder->CreateUDiv(getI64(l), getI64(r))
                                          : builder->CreateSDiv(getI64(l), getI64(r)));
                }
                if (isFloat(ltype) || isFloat(rtype)) {
                    auto* ld = isFloat(ltype) ? getF64(l) : builder->CreateSIToFP(getI64(l), f64_ty);
                    auto* rd = isFloat(rtype) ? getF64(r) : builder->CreateSIToFP(getI64(r), f64_ty);
                    return makeF64(builder->CreateFDiv(ld, rd));
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
                    return makeI64(unsign ? builder->CreateURem(getI64(l), getI64(r))
                                          : builder->CreateSRem(getI64(l), getI64(r)));
                }
                if (isFloat(ltype) || isFloat(rtype)) {
                    auto* ld = isFloat(ltype) ? getF64(l) : builder->CreateSIToFP(getI64(l), f64_ty);
                    auto* rd = isFloat(rtype) ? getF64(r) : builder->CreateSIToFP(getI64(r), f64_ty);
                    return makeF64(builder->CreateFRem(ld, rd));
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
        case TokenType::LSHIFT:    return makeI64(builder->CreateShl(getI64(l),getI64(r)));
        case TokenType::RSHIFT:    return makeI64(builder->CreateAShr(getI64(l),getI64(r)));
        case TokenType::LESS:
        case TokenType::LESS_EQUAL:
        case TokenType::GREATER:
        case TokenType::GREATER_EQUAL: {
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
        case TokenType::EQUAL_EQUAL: return callRtByName("__ang_equals",{l,r});
        case TokenType::BANG_EQUAL: { auto* eq=callRtByName("__ang_equals",{l,r}); return makeBool(builder->CreateNot(getBool(eq))); }
        default: return makeNil();
    }
}

llvm::Value* LLVMBackend::cgUnary(const Unary& e) {
    auto* o=cg(e.right); if(!o) return makeNil();
    if (e.op.type==TokenType::MINUS) {
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

llvm::Value* LLVMBackend::cgAssign(const AssignExpr& e) {
    auto* v = cg(e.value);
    if (auto* var = dynamic_cast<const VarExpr*>(e.target.get())) {
        // Under GC, assignment just overwrites the alloca. The old value's
        // lifetime is determined by reachability — no manual decref needed.
        auto type_it = namedTypes.find(var->name.lexeme);
        if (type_it != namedTypes.end() && isSizedIntType(type_it->second)) {
            v = truncateForType(v, type_it->second);
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

    // Handle immediate lambda invocation: (func(...) -> T { ... })(args)
    if (dynamic_cast<const LambdaExpr*>(expr.callee.get())) {
        auto* callee = cg(expr.callee);
        std::vector<llvm::Value*> llvmArgs;
        for (auto& a : expr.arguments) llvmArgs.push_back(cg(a));
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
                    for (auto& a : expr.arguments) args.push_back(cg(a));
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
                                for (auto& a : expr.arguments) args.push_back(cg(a));
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
                    for (auto& a : expr.arguments) args.push_back(cg(a));
                    auto* ft = mf->getFunctionType();
                    while (args.size() < ft->getNumParams()) args.push_back(makeNil());
                    return builder->CreateCall(mf, args);
                }
            }
        }
        if (auto* obj = dynamic_cast<const VarExpr*>(get->object.get())) {
            std::string modName = obj->name.lexeme, fnName = get->name.lexeme;
            {
                // Type-aware method dispatch: use the variable's class type to
                // look up the qualified key (e.g. "Animal.speak"), walking the
                // superclass chain if the method isn't found on the exact class.
                std::string resolved_method;
                auto type_it = m_type_checker.getExpressionTypes().find(obj);
                if (type_it != m_type_checker.getExpressionTypes().end() &&
                    type_it->second->kind == TypeKind::INSTANCE) {
                    auto inst = std::dynamic_pointer_cast<InstanceType>(type_it->second);
                    if (inst && inst->class_type) {
                        auto cls = inst->class_type;
                        while (cls) {
                            auto qit = methodLookup.find(cls->name + "." + fnName);
                            if (qit != methodLookup.end()) {
                                resolved_method = qit->second;
                                break;
                            }
                            cls = cls->superclass;
                        }
                    }
                }
                // Fall back to unqualified lookup if no qualified match
                if (resolved_method.empty()) {
                    auto mit = methodLookup.find(fnName);
                    if (mit != methodLookup.end())
                        resolved_method = mit->second;
                }
                if (!resolved_method.empty()) {
                    llvm::Function* mf = this->mod->getFunction(resolved_method);
                    if (mf) {
                        std::vector<llvm::Value*> args;
                        args.push_back(loadVar(modName));
                        for (auto& a : expr.arguments) args.push_back(cg(a));
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
                        for (auto& a : expr.arguments) args.push_back(cg(a));
                        auto* ft = ctor_fn->getFunctionType();
                        while (args.size() < ft->getNumParams()) args.push_back(makeNil());
                        return builder->CreateCall(ctor_fn, args);
                    }
                }
            }

            if (is_var) {
                auto* varObj = loadVar(modName);
                if (fnName == "push" || fnName == "add") {
                    if (!expr.arguments.empty())
                        return callRtByName("__ang_list_push", {varObj, cg(expr.arguments[0])});
                    return makeNil();
                }
                if (fnName == "get" || fnName == "at") {
                    if (!expr.arguments.empty())
                        return callRtByName("__ang_list_get", {varObj, cg(expr.arguments[0])});
                    return makeNil();
                }
                if (fnName == "set") {
                    if (expr.arguments.size() >= 2)
                        return callRtByName("__ang_list_set", {varObj, cg(expr.arguments[0]), cg(expr.arguments[1])});
                    return makeNil();
                }
                if (fnName == "length" || fnName == "len" || fnName == "size" || fnName == "count") {
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
                    if (!expr.arguments.empty())
                        return callRtByName("__ang_list_remove_at", {varObj, cg(expr.arguments[0])});
                    return makeNil();
                }
                if (fnName == "remove") {
                    if (!expr.arguments.empty()) {
                        auto ntype_it = namedTypes.find(sanitize(modName));
                        if (ntype_it != namedTypes.end() && ntype_it->second->kind == TypeKind::RECORD) {
                            auto* key_obj = cg(expr.arguments[0]);
                            auto* fn_as_cstr = this->mod->getFunction("__ang_api_as_cstr");
                            if (fn_as_cstr) {
                                auto* key_cstr = builder->CreateCall(fn_as_cstr, {key_obj});
                                return callRtByName("__ang_record_remove", {varObj, key_cstr});
                            }
                        } else {
                            return callRtByName("__ang_list_remove", {varObj, cg(expr.arguments[0])});
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
                    if (expr.arguments.size() >= 2) {
                        if (fnName=="println") callRtByName("__ang_io_println", {cg(expr.arguments[0]), cg(expr.arguments[1])});
                        else callRtByName("__ang_io_print", {cg(expr.arguments[0]), cg(expr.arguments[1])});
                    } else if (!expr.arguments.empty()) {
                        if (fnName=="println") callRtByName("__ang_io_println", {cg(expr.arguments[0])});
                        else callRtByName("__ang_io_print", {cg(expr.arguments[0])});
                    }
                    return makeNil();
                }
                if (fnName=="write") {
                    if (expr.arguments.size() >= 2)
                        callRtByName("__ang_io_write", {cg(expr.arguments[0]), cg(expr.arguments[1])});
                    return makeNil();
                }
                if (fnName=="flush") {
                    if (!expr.arguments.empty()) callRtByName("__ang_io_flush", {cg(expr.arguments[0])});
                    return makeNil();
                }
                if (fnName=="read_line") return callRtByName("__ang_io_read_line", {});
                if (fnName=="read_all") return callRtByName("__ang_io_read_all", {});
            }
            // Check for variadic foreign function
            auto vfit = m_variadic_foreign_funcs.find(fnName);
            if (vfit != m_variadic_foreign_funcs.end()) {
                return callVariadicForeignFn(fnName, vfit->second, expr.arguments);
            }
            return callModuleFn(modName, fnName, expr.arguments);
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
                for (auto& a : expr.arguments) args.push_back(cg(a));
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
            if (!expr.arguments.empty()) {
                auto* addr = getI64(cg(expr.arguments[0]));
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
            if (expr.arguments.size() >= 2) {
                auto* addr = getI64(cg(expr.arguments[0]));
                auto* val  = getI64(cg(expr.arguments[1]));
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

        if (fn=="string") {
            if (!expr.arguments.empty()) return callRtByName("__ang_to_string", {cg(expr.arguments[0])});
            return makeStr("");
        }
        if (fn=="i64" || fn=="int") {
            if (!expr.arguments.empty()) return callRtByName("__ang_to_i64",{cg(expr.arguments[0])});
            return makeI64((int64_t)0);
        }
        if (fn=="f64" || fn=="float") {
            if (!expr.arguments.empty()) return callRtByName("__ang_to_f64",{cg(expr.arguments[0])});
            return makeF64(0.0);
        }
        if (fn=="bool") {
            if (!expr.arguments.empty()) return callRtByName("__ang_to_bool",{cg(expr.arguments[0])});
            return makeBool(false);
        }

        // Global println/print — rewrite to __ang_io_println/__ang_io_print with stdout
        if (fn=="println" || fn=="print") {
            const char* rt = (fn=="println") ? "__ang_io_println" : "__ang_io_print";
            if (!expr.arguments.empty()) {
                // Build a string by concatenating all arguments
                auto* to_str_fn = this->mod->getFunction("__ang_to_string");
                auto* concat_fn = this->mod->getFunction("__ang_string_concat");
                llvm::Value* result = nullptr;
                for (auto& a : expr.arguments) {
                    auto* val = cg(a);
                    auto* str_val = builder->CreateCall(to_str_fn, {val}, "str");
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
                for (auto& a : expr.arguments) llvmArgs.push_back(cg(a));
                return cgClosureCall(callee, llvmArgs);
            }
        }

        if (fn == "Exception") {
            std::vector<llvm::Value*> args;
            for (auto& a : expr.arguments) args.push_back(cg(a));
            if (args.empty()) args.push_back(makeNil());
            return callRtByName("__ang_exception_new", args);
        }

        if (fn == "Mutex") {
            return callRtByName("__ang_mutex_new", {});
        }

        if (fn == "spawn") {
            if (!expr.arguments.empty()) {
                auto* closure = cg(expr.arguments[0]);
                int n_args = (int)expr.arguments.size() - 1;
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
                        builder->CreateStore(cg(expr.arguments[i + 1]), elem_ptr);
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
                for (auto& a : expr.arguments) args.push_back(cg(a));
                auto* ft = ctor->getFunctionType();
                while (args.size() < ft->getNumParams()) args.push_back(makeNil());
                return builder->CreateCall(ctor, args);
            }
        }
        // Check for variadic foreign function
        auto vfit = m_variadic_foreign_funcs.find(fn);
        if (vfit != m_variadic_foreign_funcs.end()) {
            return callVariadicForeignFn(fn, vfit->second, expr.arguments);
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

    if (!f) {
        std::string declName = mangled;
        auto* fnTy = llvm::FunctionType::get(objType,
            std::vector<llvm::Type*>(args.size(), objType), false);
        f = llvm::Function::Create(fnTy, llvm::Function::ExternalLinkage, declName, this->mod.get());
    }

    auto ft = f->getFunctionType();
    bool direct_call = true;
    if (ft->getNumParams() == 2 &&
        ft->getParamType(0)->isIntegerTy(32) &&
        ft->getParamType(1)->isPointerTy()) {
        direct_call = false;
    }

    if (direct_call) {
        std::vector<llvm::Value*> llvmArgs;
        for (auto& a : args) llvmArgs.push_back(cg(a));
        while (llvmArgs.size() < ft->getNumParams()) llvmArgs.push_back(makeNil());
        return builder->CreateCall(f, llvmArgs);
    }

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
    if (auto* var = dynamic_cast<const VarExpr*>(e.object.get())) {
        auto type_it = m_type_checker.getExpressionTypes().find(e.object.get());
        if (type_it != m_type_checker.getExpressionTypes().end() &&
            type_it->second->kind == TypeKind::ENUM) {
            auto enum_type = std::dynamic_pointer_cast<EnumType>(type_it->second);
            std::string global_name = "Angara_enum_" + enum_type->name + "_" + e.name.lexeme;
            auto* global = mod->getGlobalVariable(global_name, true);
            if (global) {
                return builder->CreateLoad(objType, global);
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
    auto* l = callRtByName("__ang_list_new",{});
    for (auto& el : e.elements) callRtByName("__ang_list_push",{l, cg(el)});
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
    // Numeric truncation/extension: for now just return as-is
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

    // Look up the enum type to find variant indices
    auto type_it = m_type_checker.getExpressionTypes().find(e.condition.get());
    std::shared_ptr<EnumType> enum_type;
    if (type_it != m_type_checker.getExpressionTypes().end() && type_it->second->kind == TypeKind::ENUM) {
        enum_type = std::dynamic_pointer_cast<EnumType>(type_it->second);
    }

    for (auto& c : e.cases) {
        auto* match_bb = llvm::BasicBlock::Create(*ctx,"mb",fn);
        auto* next_bb = llvm::BasicBlock::Create(*ctx,"mn",fn);

        // Check for wildcard pattern '_'
        bool is_wildcard = false;
        if (auto var_expr = std::dynamic_pointer_cast<const VarExpr>(c.pattern)) {
            if (var_expr->name.lexeme == "_") {
                is_wildcard = true;
            }
        }

        if (is_wildcard) {
            // Wildcard always matches
            builder->CreateBr(match_bb);
            builder->SetInsertPoint(match_bb);

            // Bind variable if present
            if (c.variable) {
                auto* alloca = allocLocal(fn, sanitize(c.variable->lexeme));
                builder->CreateStore(subj, alloca);
                namedVals[sanitize(c.variable->lexeme)] = alloca;
            }

            auto* r = cg(c.body);
            match_bb = builder->GetInsertBlock();
            builder->CreateBr(mg);
            inc.push_back({match_bb, r});
            builder->SetInsertPoint(next_bb);
        } else {
            // Named variant — extract variant name and index
            std::string variant_name;
            if (auto get_expr = std::dynamic_pointer_cast<const GetExpr>(c.pattern)) {
                variant_name = get_expr->name.lexeme;
            }

            // Find the variant index from declaration order
            int variant_index = -1;
            bool has_payload = false;
            // Look up the enum name prefix to build the qualified key
            std::string enum_name;
            if (auto get_expr = std::dynamic_pointer_cast<const GetExpr>(c.pattern)) {
                if (auto lhs = std::dynamic_pointer_cast<const VarExpr>(get_expr->object)) {
                    enum_name = lhs->name.lexeme;
                }
            }
            std::string qualified = enum_name + "." + variant_name;
            auto it = enumVariantIndex.find(qualified);
            if (it != enumVariantIndex.end()) {
                variant_index = it->second;
            }
            // Check if variant has payload from the enum type
            if (enum_type) {
                auto vit = enum_type->variants.find(variant_name);
                if (vit != enum_type->variants.end()) {
                    has_payload = !vit->second->param_types.empty();
                }
            }

            // Generate discriminant comparison
            auto* subj_tag = getTag(subj);
            auto* tag_is_obj = builder->CreateICmpEQ(subj_tag,
                llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), TAG_OBJ));

            // For payload-carrying variants (TAG_OBJ): extract __tag from heap record
            // For simple variants (TAG_I64): compare the i64 payload directly
            auto* obj_path_bb = llvm::BasicBlock::Create(*ctx,"mop",fn);
            auto* i64_path_bb = llvm::BasicBlock::Create(*ctx,"mip",fn);
            auto* cmp_bb = llvm::BasicBlock::Create(*ctx,"mc",fn);
            builder->CreateCondBr(tag_is_obj, obj_path_bb, i64_path_bb);

            // Object path: extract __tag field from the heap record
            builder->SetInsertPoint(obj_path_bb);
            auto* tag_field = callRtByName("__ang_record_get",
                {subj, builder->CreateGlobalString("__tag")});
            auto* obj_disc = getI64(tag_field);
            builder->CreateBr(cmp_bb);

            // i64 path: the payload IS the discriminant
            builder->SetInsertPoint(i64_path_bb);
            auto* i64_disc = getI64(subj);
            builder->CreateBr(cmp_bb);

            // Merge discriminant values
            builder->SetInsertPoint(cmp_bb);
            auto* disc_phi = builder->CreatePHI(llvm::Type::getInt64Ty(*ctx), 2);
            disc_phi->addIncoming(obj_disc, obj_path_bb);
            disc_phi->addIncoming(i64_disc, i64_path_bb);

            auto* target_index = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), variant_index);
            auto* matches = builder->CreateICmpEQ(disc_phi, target_index);

            builder->CreateCondBr(matches, match_bb, next_bb);

            // Matched case — bind variable if needed
            builder->SetInsertPoint(match_bb);

            if (c.variable && has_payload) {
                // Extract the payload field "_0" from the enum record
                auto* payload_val = callRtByName("__ang_record_get",
                    {subj, builder->CreateGlobalString("_0")});
                auto* alloca = allocLocal(fn, sanitize(c.variable->lexeme));
                builder->CreateStore(payload_val, alloca);
                namedVals[sanitize(c.variable->lexeme)] = alloca;
            }

            auto* r = cg(c.body);
            match_bb = builder->GetInsertBlock();
            builder->CreateBr(mg);
            inc.push_back({match_bb, r});
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
    auto* saved_gc_frame = m_gc_current_frame;
    int saved_gc_slot_idx = m_gc_frame_slot_idx;
    int saved_gc_max_slots = m_gc_frame_max_slots;
    m_inlined_main_ret_alloca = nullptr;
    m_inlined_main_cleanup_bb = nullptr;
    m_gc_current_frame = nullptr;
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
        if (m_gc_current_frame) emitGcPopFrame();
        builder->CreateRet(makeNil());
    }

    namedVals = std::move(saved_values);
    namedTypes = std::move(saved_types);
    namedKinds = std::move(saved_kinds);
    m_inlined_main_ret_alloca = saved_ret_alloca;
    m_inlined_main_cleanup_bb = saved_cleanup_bb;
    m_gc_current_frame = saved_gc_frame;
    m_gc_frame_slot_idx = saved_gc_slot_idx;
    m_gc_frame_max_slots = saved_gc_max_slots;

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

}
