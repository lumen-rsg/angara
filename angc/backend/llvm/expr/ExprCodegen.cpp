// Angara LLVM Backend — Expression Codegen
#include "LLVMBackend.h"
#include "RuntimeBuilder.h"
#include <llvm/IR/Intrinsics.h>

namespace angara {

// ============================================================================
// Expression dispatch
// ============================================================================
llvm::Value* LLVMBackend::cg(const std::shared_ptr<Expr>& e) {
    if (!e) return makeNil();
    if (auto* p = dynamic_cast<const Literal*>(e.get())) return cgLiteral(*p);
    if (auto* p = dynamic_cast<const Binary*>(e.get())) return cgBinary(*p);
    if (auto* p = dynamic_cast<const Unary*>(e.get())) return cgUnary(*p);
    if (auto* p = dynamic_cast<const Grouping*>(e.get())) return cg(p->expression);
    if (auto* p = dynamic_cast<const VarExpr*>(e.get())) {
        // First-class function reference: if VarExpr resolves to a function type,
        // wrap the named function in a closure object.
        auto type_it = m_type_checker.m_expression_types.find(e.get());
        if (type_it != m_type_checker.m_expression_types.end() &&
            type_it->second->kind == TypeKind::FUNCTION) {
            auto func_type = std::dynamic_pointer_cast<FunctionType>(type_it->second);
            // Check if this is already a closure variable (in namedVals)
            if (namedVals.find(sanitize(p->name.lexeme)) != namedVals.end()) {
                return loadVar(p->name.lexeme);
            }
            // It's a top-level function reference — generate a closure wrapper
            std::string mangled = mangle(moduleName, p->name.lexeme);
            int arity = (int)func_type->param_types.size();

            // Generate a unique wrapper function that adapts the closure
            // calling convention (i32, ptr) to the direct calling convention
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

            // Extract arguments from the args array and call the real function
            std::vector<llvm::Value*> direct_args;
            auto* wrap_arr_type = llvm::ArrayType::get(objType, arity);
            for (int i = 0; i < arity; i++) {
                auto* elem_ptr = builder->CreateGEP(wrap_arr_type, args_ptr,
                    {llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), 0),
                     llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), i)});
                direct_args.push_back(builder->CreateLoad(objType, elem_ptr));
            }

            // Call the actual named function via callModuleFn
            // We need to look it up directly
            llvm::Function* target_fn = this->mod->getFunction(mangled);
            if (!target_fn) target_fn = this->mod->getFunction("__ang_" + sanitize(p->name.lexeme));

            if (target_fn) {
                auto* result = builder->CreateCall(target_fn, direct_args);
                builder->CreateRet(result);
            } else {
                // Forward declaration — insert it and call it
                auto* direct_fn_type = llvm::FunctionType::get(objType,
                    std::vector<llvm::Type*>(arity, objType), false);
                auto* forward_fn = llvm::Function::Create(direct_fn_type, llvm::Function::ExternalLinkage,
                                                           mangled, mod.get());
                auto* result = builder->CreateCall(forward_fn, direct_args);
                builder->CreateRet(result);
            }

            // Restore builder
            if (saved_insert_block) builder->SetInsertPoint(saved_insert_block);

            // Create a closure object wrapping the wrapper
            return callRt(rt->getFuncClosureNew(), {
                wrapper_fn,
                llvm::ConstantInt::get(i32_ty, arity),
                llvm::ConstantInt::get(llvm::Type::getInt1Ty(*ctx), 0)
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
    if (auto* p = dynamic_cast<const MatchExpr*>(e.get())) return cgMatch(*p);
    if (auto* p = dynamic_cast<const SizeofExpr*>(e.get())) return makeI64((int64_t)16);
    if (auto* p = dynamic_cast<const RetypeExpr*>(e.get())) return cgRetype(*p);
    if (auto* p = dynamic_cast<const LambdaExpr*>(e.get())) return cgLambda(*p);
    return makeNil();
}

llvm::Value* LLVMBackend::cgLiteral(const Literal& e) {
    const auto& tok = e.token;
    if (tok.type == TokenType::NIL) return makeNil();
    if (tok.type == TokenType::TRUE) return makeBool(true);
    if (tok.type == TokenType::FALSE) return makeBool(false);
    if (tok.type == TokenType::NUMBER_INT) return makeI64(std::stoll(tok.lexeme, nullptr, 0));
    if (tok.type == TokenType::NUMBER_FLOAT) return makeF64(std::stod(tok.lexeme));
    if (tok.type == TokenType::STRING) return makeStr(tok.lexeme);
    return makeNil();
}

llvm::Value* LLVMBackend::cgBinary(const Binary& e) {
    auto* l = cg(e.left), *r = cg(e.right);
    if (!l||!r) return makeNil();
    switch (e.op.type) {
        case TokenType::PLUS: {
            auto* lTag = getTag(l);
            auto* rTag = getTag(r);
            auto* bothI64 = builder->CreateAnd(
                builder->CreateICmpEQ(lTag, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), TAG_I64)),
                builder->CreateICmpEQ(rTag, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), TAG_I64)));
            auto* bothF64 = builder->CreateAnd(
                builder->CreateICmpEQ(lTag, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), TAG_F64)),
                builder->CreateICmpEQ(rTag, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), TAG_F64)));
            auto* fn = builder->GetInsertBlock()->getParent();
            auto* iaddBB = llvm::BasicBlock::Create(*ctx,"iadd",fn);
            auto* faddBB = llvm::BasicBlock::Create(*ctx,"fadd",fn);
            auto* saddBB = llvm::BasicBlock::Create(*ctx,"sadd",fn);
            auto* maddBB = llvm::BasicBlock::Create(*ctx,"madd",fn);
            builder->CreateCondBr(bothI64, iaddBB, faddBB);
            builder->SetInsertPoint(iaddBB);
            auto* ia = makeI64(builder->CreateAdd(getI64(l),getI64(r)));
            iaddBB = builder->GetInsertBlock();
            builder->CreateBr(maddBB);
            builder->SetInsertPoint(faddBB);
            auto* isF64 = builder->CreateAnd(
                builder->CreateICmpEQ(lTag, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), TAG_F64)),
                builder->CreateICmpEQ(rTag, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), TAG_F64)));
            auto* faddCont = llvm::BasicBlock::Create(*ctx,"fadd_cont",fn);
            builder->CreateCondBr(isF64, faddCont, saddBB);
            builder->SetInsertPoint(faddCont);
            auto* fa = makeF64(builder->CreateFAdd(getF64(l),getF64(r)));
            faddCont = builder->GetInsertBlock();
            builder->CreateBr(maddBB);
            builder->SetInsertPoint(saddBB);
            auto* sa = callRt(rt->getFuncStringConcat(),{l,r});
            saddBB = builder->GetInsertBlock();
            builder->CreateBr(maddBB);
            builder->SetInsertPoint(maddBB);
            auto* phi = builder->CreatePHI(objType,3);
            phi->addIncoming(ia,iaddBB); phi->addIncoming(fa,faddCont); phi->addIncoming(sa,saddBB);
            return phi;
        }
        case TokenType::MINUS: return makeI64(builder->CreateSub(getI64(l),getI64(r)));
        case TokenType::STAR: {
            // Check if this is string * number repetition
            {
                auto lt = m_type_checker.m_expression_types.find(e.left.get());
                if (lt != m_type_checker.m_expression_types.end() && lt->second->toString() == "string") {
                    return callRt(rt->getFuncStringRepeat(), {l, r});
                }
            }
            return makeI64(builder->CreateMul(getI64(l),getI64(r)));
        }
        case TokenType::SLASH: {
            auto lt = m_type_checker.m_expression_types.find(e.left.get());
            auto rt2 = m_type_checker.m_expression_types.find(e.right.get());
            bool unsigned_div = (lt != m_type_checker.m_expression_types.end() && isUnsignedIntType(lt->second)) ||
                                (rt2 != m_type_checker.m_expression_types.end() && isUnsignedIntType(rt2->second));
            return makeI64(unsigned_div ? builder->CreateUDiv(getI64(l),getI64(r))
                                        : builder->CreateSDiv(getI64(l),getI64(r)));
        }
        case TokenType::PERCENT: {
            auto lt = m_type_checker.m_expression_types.find(e.left.get());
            auto rt2 = m_type_checker.m_expression_types.find(e.right.get());
            bool unsigned_mod = (lt != m_type_checker.m_expression_types.end() && isUnsignedIntType(lt->second)) ||
                                (rt2 != m_type_checker.m_expression_types.end() && isUnsignedIntType(rt2->second));
            return makeI64(unsigned_mod ? builder->CreateURem(getI64(l),getI64(r))
                                        : builder->CreateSRem(getI64(l),getI64(r)));
        }
        case TokenType::AMPERSAND: return makeI64(builder->CreateAnd(getI64(l),getI64(r)));
        case TokenType::PIPE:      return makeI64(builder->CreateOr(getI64(l),getI64(r)));
        case TokenType::CARET:     return makeI64(builder->CreateXor(getI64(l),getI64(r)));
        case TokenType::LESS: {
            auto lt = m_type_checker.m_expression_types.find(e.left.get());
            auto rt2 = m_type_checker.m_expression_types.find(e.right.get());
            bool unsigned_cmp = (lt != m_type_checker.m_expression_types.end() && isUnsignedIntType(lt->second)) ||
                                (rt2 != m_type_checker.m_expression_types.end() && isUnsignedIntType(rt2->second));
            return makeBool(unsigned_cmp ? builder->CreateICmpULT(getI64(l),getI64(r))
                                        : builder->CreateICmpSLT(getI64(l),getI64(r)));
        }
        case TokenType::LESS_EQUAL: {
            auto lt = m_type_checker.m_expression_types.find(e.left.get());
            auto rt2 = m_type_checker.m_expression_types.find(e.right.get());
            bool unsigned_cmp = (lt != m_type_checker.m_expression_types.end() && isUnsignedIntType(lt->second)) ||
                                (rt2 != m_type_checker.m_expression_types.end() && isUnsignedIntType(rt2->second));
            return makeBool(unsigned_cmp ? builder->CreateICmpULE(getI64(l),getI64(r))
                                        : builder->CreateICmpSLE(getI64(l),getI64(r)));
        }
        case TokenType::GREATER: {
            auto lt = m_type_checker.m_expression_types.find(e.left.get());
            auto rt2 = m_type_checker.m_expression_types.find(e.right.get());
            bool unsigned_cmp = (lt != m_type_checker.m_expression_types.end() && isUnsignedIntType(lt->second)) ||
                                (rt2 != m_type_checker.m_expression_types.end() && isUnsignedIntType(rt2->second));
            return makeBool(unsigned_cmp ? builder->CreateICmpUGT(getI64(l),getI64(r))
                                        : builder->CreateICmpSGT(getI64(l),getI64(r)));
        }
        case TokenType::GREATER_EQUAL: {
            auto lt = m_type_checker.m_expression_types.find(e.left.get());
            auto rt2 = m_type_checker.m_expression_types.find(e.right.get());
            bool unsigned_cmp = (lt != m_type_checker.m_expression_types.end() && isUnsignedIntType(lt->second)) ||
                                (rt2 != m_type_checker.m_expression_types.end() && isUnsignedIntType(rt2->second));
            return makeBool(unsigned_cmp ? builder->CreateICmpUGE(getI64(l),getI64(r))
                                        : builder->CreateICmpSGE(getI64(l),getI64(r)));
        }
        case TokenType::EQUAL_EQUAL: return callRt(rt->getFuncEquals(),{l,r});
        case TokenType::BANG_EQUAL: { auto* eq=callRt(rt->getFuncEquals(),{l,r}); return makeBool(builder->CreateNot(getBool(eq))); }
        default: return makeNil();
    }
}

llvm::Value* LLVMBackend::cgUnary(const Unary& e) {
    auto* o=cg(e.right); if(!o) return makeNil();
    if (e.op.type==TokenType::MINUS) return makeI64(builder->CreateNeg(getI64(o)));
    if (e.op.type==TokenType::TILDE) return makeI64(builder->CreateNot(getI64(o)));
    if (e.op.type==TokenType::BANG) return makeBool(builder->CreateNot(isTruthy(o)));
    return o;
}

llvm::Value* LLVMBackend::cgAssign(const AssignExpr& e) {
    auto* v = cg(e.value);
    if (auto* var = dynamic_cast<const VarExpr*>(e.target.get())) {
        // Apply semantic narrowing for typed integer variables
        auto type_it = namedTypes.find(var->name.lexeme);
        if (type_it != namedTypes.end() && isSizedIntType(type_it->second)) {
            v = truncateForType(v, type_it->second);
        }
        storeVar(var->name.lexeme, v);
        return v;
    }
    if (auto* get = dynamic_cast<const GetExpr*>(e.target.get())) {
        auto* obj = cg(get->object);
        callRt(rt->getFuncRecordSet(), {obj, builder->CreateGlobalString(get->name.lexeme), v});
        return v;
    }
    if (auto* sub = dynamic_cast<const SubscriptExpr*>(e.target.get())) {
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
            {
                auto mit = methodLookup.find(fnName);
                if (mit != methodLookup.end()) {
                    llvm::Function* mf = this->mod->getFunction(mit->second);
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
            // Check if modName is a local variable (list/record) rather than a module
            if (namedVals.find(sanitize(modName)) != namedVals.end()) {
                auto* varObj = loadVar(modName);
                // Built-in list methods
                if (fnName == "push" || fnName == "add") {
                    if (!expr.arguments.empty())
                        return callRt(rt->getFuncListPush(), {varObj, cg(expr.arguments[0])});
                    return makeNil();
                }
                if (fnName == "get" || fnName == "at") {
                    if (!expr.arguments.empty())
                        return callRt(rt->getFuncListGet(), {varObj, cg(expr.arguments[0])});
                    return makeNil();
                }
                if (fnName == "set") {
                    if (expr.arguments.size() >= 2)
                        return callRt(rt->getFuncListSet(), {varObj, cg(expr.arguments[0]), cg(expr.arguments[1])});
                    return makeNil();
                }
                if (fnName == "length" || fnName == "len" || fnName == "size" || fnName == "count") {
                    return callRt(rt->getFuncLen(), {varObj});
                }
                // Not a recognized built-in — fall through to module function lookup
            }
            if (modName=="io") {
                if (fnName=="println"||fnName=="print") {
                    if (expr.arguments.size() >= 2) {
                        if (fnName=="println") callRt(rt->getFuncIOPrintln(), {cg(expr.arguments[0]), cg(expr.arguments[1])});
                        else callRt(rt->getFuncIOPrint(), {cg(expr.arguments[0]), cg(expr.arguments[1])});
                    } else if (!expr.arguments.empty()) {
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

        // Intrinsics: inline volatile IR generation
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
            if (!expr.arguments.empty()) return callRt(rt->getFuncToString(),{cg(expr.arguments[0])});
            return makeStr("");
        }
        if (fn=="i64" || fn=="int") {
            if (!expr.arguments.empty()) return callRt(rt->getFuncToI64(),{cg(expr.arguments[0])});
            return makeI64((int64_t)0);
        }
        if (fn=="f64" || fn=="float") {
            if (!expr.arguments.empty()) return callRt(rt->getFuncToF64(),{cg(expr.arguments[0])});
            return makeF64(0.0);
        }
        if (fn=="bool") {
            if (!expr.arguments.empty()) return callRt(rt->getFuncToBool(),{cg(expr.arguments[0])});
            return makeBool(false);
        }
        // Check if it's a local variable holding a closure (first-class function call)
        if (namedVals.find(sanitize(fn)) != namedVals.end()) {
            auto type_it = m_type_checker.m_expression_types.find(expr.callee.get());
            bool is_callable = false;
            if (type_it != m_type_checker.m_expression_types.end()) {
                auto kind = type_it->second->kind;
                // Explicit function type, or 'any' (which may be a closure passed as param)
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

        // Built-in Exception constructor: use runtime's exception_new
        if (fn == "Exception") {
            std::vector<llvm::Value*> args;
            for (auto& a : expr.arguments) args.push_back(cg(a));
            if (args.empty()) args.push_back(makeNil());
            return callRt(rt->getFuncExceptionNew(), args);
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
        return callModuleFn(moduleName, fn, expr.arguments);
    }
    return makeNil();
}

llvm::Value* LLVMBackend::callModuleFn(const std::string& mod, const std::string& fn,
                                        const std::vector<std::shared_ptr<Expr>>& args) {
    std::string mangled = mangle(mod, fn);
    llvm::Function* f = this->mod->getFunction(mangled);
    if (!f) f = this->mod->getFunction("__ang_"+sanitize(fn));

    // If not found, create a forward declaration for the cross-module function
    // All Angara functions return %AngaraObject and take %AngaraObject params
    if (!f) {
        // Try the mangled name first, then the sanitized name
        std::string declName = mangled;
        auto* fnTy = llvm::FunctionType::get(objType,
            std::vector<llvm::Type*>(args.size(), objType), false);
        f = llvm::Function::Create(fnTy, llvm::Function::ExternalLinkage, declName, this->mod.get());
    }

    auto ft = f->getFunctionType();
    bool direct_call = true;
    if (ft->getNumParams() < 1) direct_call = false;
    else {
        if (ft->getNumParams() == 2 &&
            ft->getParamType(0)->isIntegerTy(32) &&
            ft->getParamType(1)->isPointerTy()) {
            direct_call = false;
        }
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
    // Special case: enum variant access (e.g., Color.Red)
    // Check if the object is a VarExpr whose type is an enum type
    if (auto* var = dynamic_cast<const VarExpr*>(e.object.get())) {
        auto type_it = m_type_checker.m_expression_types.find(e.object.get());
        if (type_it != m_type_checker.m_expression_types.end() &&
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
    // Special case: .message property uses exception_get_message runtime function
    if (e.name.lexeme == "message") {
        return callRt(rt->getFuncExceptionGetMessage(), {obj});
    }
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
// Lambda (anonymous function) codegen
// ============================================================================

llvm::Value* LLVMBackend::cgLambda(const LambdaExpr& e) {
    // Generate a unique name for the lambda's LLVM function
    std::string lambda_fn_name = "__ang_lambda_" + std::to_string(m_lambda_counter++);

    // The lambda function follows the closure calling convention:
    //   define AngaraObject @lambda_fn(i32 %argc, AngaraObject* %args)
    auto* i32_ty = llvm::Type::getInt32Ty(*ctx);
    auto* ptr_ty = llvm::PointerType::get(*ctx, 0);
    auto* fn_type = llvm::FunctionType::get(objType, {i32_ty, ptr_ty}, false);
    auto* lambda_fn = llvm::Function::Create(fn_type, llvm::Function::PrivateLinkage,
                                              lambda_fn_name, mod.get());

    // Save current builder state
    auto* saved_insert_block = builder->GetInsertBlock();

    // Capture parent scope variables: store their current values into globals
    // so the lambda function (which is a separate LLVM function) can access them.
    std::map<std::string, llvm::GlobalVariable*> capture_globals;
    for (const auto& [name, alloca] : namedVals) {
        std::string gname = "__ang_cap_" + lambda_fn_name + "_" + name;
        auto* global = mod->getGlobalVariable(gname);
        if (!global) {
            global = new llvm::GlobalVariable(
                *mod, objType, false, llvm::GlobalValue::PrivateLinkage,
                llvm::ConstantAggregateZero::get(objType), gname);
        }
        // Store the current value of the variable into the global
        auto* val = builder->CreateLoad(objType, alloca, name);
        builder->CreateStore(val, global);
        capture_globals[name] = global;
    }

    // Generate the lambda body
    auto* entry = llvm::BasicBlock::Create(*ctx, "entry", lambda_fn);
    builder->SetInsertPoint(entry);

    // Save and swap named values (the lambda has its own scope)
    auto saved_values = std::move(namedVals);
    auto saved_types = std::move(namedTypes);
    namedVals.clear();
    namedTypes.clear();

    // Restore captured variables from globals into local allocas
    for (const auto& [name, global] : capture_globals) {
        std::string sname = sanitize(name);
        auto* alloca = allocLocal(lambda_fn, sname);
        auto* val = builder->CreateLoad(objType, global, sname);
        builder->CreateStore(val, alloca);
        namedVals[sname] = alloca;
    }

    // Bind lambda parameters from the args array
    // The closure calling convention: args[0] = first param, args[1] = second, etc.
    auto* argc_arg = lambda_fn->arg_begin();
    auto* args_arg = lambda_fn->arg_begin() + 1;
    auto* lambda_arr_type = llvm::ArrayType::get(objType, e.param_names.size());

    for (size_t i = 0; i < e.param_names.size(); ++i) {
        std::string pname = sanitize(e.param_names[i].lexeme);
        auto* alloca = allocLocal(lambda_fn, pname);

        // Load args[i] from the args array
        auto* elem_ptr = builder->CreateGEP(lambda_arr_type, args_arg,
            {llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), 0),
             llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), i)});
        auto* val = builder->CreateLoad(objType, elem_ptr, pname);
        builder->CreateStore(val, alloca);
        namedVals[pname] = alloca;
    }

    // Generate the body statements
    for (const auto& stmt : e.body) {
        cgStmt(stmt);
    }

    // Ensure the lambda function has a terminator
    if (!builder->GetInsertBlock()->getTerminator()) {
        builder->CreateRet(makeNil());
    }

    // Restore named values
    namedVals = std::move(saved_values);
    namedTypes = std::move(saved_types);

    // Restore builder to the caller's insertion point
    if (saved_insert_block) {
        builder->SetInsertPoint(saved_insert_block);
    }

    // Create a closure object wrapping this function
    int arity = (int)e.param_names.size();
    return callRt(rt->getFuncClosureNew(), {
        lambda_fn,
        llvm::ConstantInt::get(i32_ty, arity),
        llvm::ConstantInt::get(llvm::Type::getInt1Ty(*ctx), 0) // not native
    });
}

// ============================================================================
// Closure call — calls an AngaraObject that is a closure
// ============================================================================

llvm::Value* LLVMBackend::cgClosureCall(llvm::Value* callee, const std::vector<llvm::Value*>& args) {
    auto* i32_ty = llvm::Type::getInt32Ty(*ctx);
    int argc = (int)args.size();

    // Allocate an array for the arguments
    if (argc > 0) {
        auto* arr_type = llvm::ArrayType::get(objType, argc);
        auto* arr_alloca = builder->CreateAlloca(arr_type);

        for (int i = 0; i < argc; i++) {
            auto* elem_ptr = builder->CreateGEP(arr_type, arr_alloca,
                {llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), 0),
                 llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), i)});
            builder->CreateStore(args[i], elem_ptr);
        }

        return callRt(rt->getFuncCall(), {
            callee,
            llvm::ConstantInt::get(i32_ty, argc),
            builder->CreateBitCast(arr_alloca, llvm::PointerType::get(*ctx, 0))
        });
    }

    return callRt(rt->getFuncCall(), {
        callee,
        llvm::ConstantInt::get(i32_ty, 0),
        llvm::ConstantPointerNull::get(llvm::PointerType::get(*ctx, 0))
    });
}

} // namespace angara
