#include "RuntimeBuilder.h"

using namespace llvm;

namespace angara {
void RuntimeBuilder::generateConversions() {
    auto* i8_ty = Type::getInt8Ty(m_ctx);
    auto* i32_ty = Type::getInt32Ty(m_ctx);
    auto* i64_ty = Type::getInt64Ty(m_ctx);
    auto* f64_ty = Type::getDoubleTy(m_ctx);
    auto* i8_ptr = PointerType::get(m_ctx, 0);
    auto* obj_ty = m_angara_obj_type;

    auto make_nil = [&](IRBuilder<>& b) -> Value* {
        Value* nil_val = UndefValue::get(obj_ty);
        nil_val = b.CreateInsertValue(nil_val, ConstantInt::get(i32_ty, TAG_NIL), {0});
        nil_val = b.CreateInsertValue(nil_val,
            ConstantInt::get(i64_ty, 0), {1});
        return nil_val;
    };

    {
        auto* fn_ty = FunctionType::get(obj_ty, {obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_to_i64", fn_ty);
        m_fn_to_i64 = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* val = fn->arg_begin();
        auto* tag = b.CreateExtractValue(val, {0}, "tag");

        auto* is_i64_bb = BasicBlock::Create(m_ctx, "is_i64", fn);
        auto* is_f64_bb = BasicBlock::Create(m_ctx, "is_f64", fn);
        auto* is_bool_bb = BasicBlock::Create(m_ctx, "is_bool", fn);
        auto* default_bb = BasicBlock::Create(m_ctx, "default", fn);

        auto* sw = b.CreateSwitch(tag, default_bb, 3);
        sw->addCase(ConstantInt::get(i32_ty, TAG_I64), is_i64_bb);
        sw->addCase(ConstantInt::get(i32_ty, TAG_F64), is_f64_bb);
        sw->addCase(ConstantInt::get(i32_ty, TAG_BOOL), is_bool_bb);

        IRBuilder<> bi(is_i64_bb);
        bi.CreateRet(val);

        IRBuilder<> bf(is_f64_bb);
        auto* fval = bf.CreateBitCast(bf.CreateExtractValue(val, {1}), f64_ty);
        auto* ival = bf.CreateFPToSI(fval, i64_ty);
        Value* result = UndefValue::get(obj_ty);
        result = bf.CreateInsertValue(result, ConstantInt::get(i32_ty, TAG_I64), {0});
        result = bf.CreateInsertValue(result, ival, {1});
        bf.CreateRet(result);

        IRBuilder<> bb(is_bool_bb);
        auto* bval = bb.CreateTrunc(bb.CreateBitCast(bb.CreateExtractValue(val, {1}), i64_ty), Type::getInt1Ty(m_ctx));
        auto* extended = bb.CreateZExt(bval, i64_ty);
        Value* result2 = UndefValue::get(obj_ty);
        result2 = bb.CreateInsertValue(result2, ConstantInt::get(i32_ty, TAG_I64), {0});
        result2 = bb.CreateInsertValue(result2, extended, {1});
        bb.CreateRet(result2);

        IRBuilder<> bd(default_bb);
        bd.CreateRet(make_nil(bd));
    }

    {
        auto* fn_ty = FunctionType::get(obj_ty, {obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_to_f64", fn_ty);
        m_fn_to_f64 = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* val = fn->arg_begin();
        auto* tag = b.CreateExtractValue(val, {0});

        auto* is_f64_bb = BasicBlock::Create(m_ctx, "is_f64", fn);
        auto* is_i64_bb = BasicBlock::Create(m_ctx, "is_i64", fn);
        auto* default_bb = BasicBlock::Create(m_ctx, "default", fn);

        auto* sw = b.CreateSwitch(tag, default_bb, 2);
        sw->addCase(ConstantInt::get(i32_ty, TAG_F64), is_f64_bb);
        sw->addCase(ConstantInt::get(i32_ty, TAG_I64), is_i64_bb);

        IRBuilder<> bf(is_f64_bb);
        bf.CreateRet(val);

        IRBuilder<> bi(is_i64_bb);
        auto* ival = bi.CreateBitCast(bi.CreateExtractValue(val, {1}), i64_ty);
        auto* fval = bi.CreateSIToFP(ival, f64_ty);
        Value* result = UndefValue::get(obj_ty);
        result = bi.CreateInsertValue(result, ConstantInt::get(i32_ty, TAG_F64), {0});
        result = bi.CreateInsertValue(result, bi.CreateBitCast(fval, i64_ty), {1});
        bi.CreateRet(result);

        IRBuilder<> bd(default_bb);
        bd.CreateRet(make_nil(bd));
    }

    {
        auto* fn_ty = FunctionType::get(obj_ty, {obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_to_bool", fn_ty);
        m_fn_to_bool = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* val = fn->arg_begin();
        auto* tag = b.CreateExtractValue(val, {0}, "tag");
        auto* payload = b.CreateExtractValue(val, {1}, "payload");

        auto* nil_bb = BasicBlock::Create(m_ctx, "nil", fn);
        auto* bool_bb = BasicBlock::Create(m_ctx, "bool", fn);
        auto* i64_bb = BasicBlock::Create(m_ctx, "i64", fn);
        auto* f64_bb = BasicBlock::Create(m_ctx, "f64", fn);
        auto* obj_bb = BasicBlock::Create(m_ctx, "obj", fn);

        auto* sw = b.CreateSwitch(tag, nil_bb, 5);
        sw->addCase(ConstantInt::get(i32_ty, TAG_NIL), nil_bb);
        sw->addCase(ConstantInt::get(i32_ty, TAG_BOOL), bool_bb);
        sw->addCase(ConstantInt::get(i32_ty, TAG_I64), i64_bb);
        sw->addCase(ConstantInt::get(i32_ty, TAG_F64), f64_bb);
        sw->addCase(ConstantInt::get(i32_ty, TAG_OBJ), obj_bb);

        auto pack_bool = [&](IRBuilder<>& b, Value* bool_val) -> Value* {
            auto* extended = b.CreateZExt(bool_val, i64_ty);
            Value* result = UndefValue::get(obj_ty);
            result = b.CreateInsertValue(result, ConstantInt::get(i32_ty, TAG_BOOL), {0});
            result = b.CreateInsertValue(result, extended, {1});
            return result;
        };

        IRBuilder<> bn(nil_bb);
        bn.CreateRet(pack_bool(bn, ConstantInt::get(Type::getInt1Ty(m_ctx), 0)));

        IRBuilder<> bb(bool_bb);
        auto* bval = bb.CreateTrunc(bb.CreateBitCast(payload, i64_ty), Type::getInt1Ty(m_ctx));
        bb.CreateRet(pack_bool(bb, bval));

        IRBuilder<> bi(i64_bb);
        auto* ival = bi.CreateBitCast(payload, i64_ty);
        auto* nonzero = bi.CreateICmpNE(ival, ConstantInt::get(i64_ty, 0));
        bi.CreateRet(pack_bool(bi, nonzero));

        IRBuilder<> bf(f64_bb);
        auto* dval = bf.CreateBitCast(payload, f64_ty);
        auto* nonzero_f = bf.CreateFCmpUNE(dval, ConstantFP::get(f64_ty, 0.0));
        bf.CreateRet(pack_bool(bf, nonzero_f));

        IRBuilder<> bo(obj_bb);
        bo.CreateRet(pack_bool(bo, ConstantInt::get(Type::getInt1Ty(m_ctx), 1)));
    }

    {
        auto* fn_ty = FunctionType::get(obj_ty, {obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_typeof", fn_ty);
        m_fn_typeof = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* val = fn->arg_begin();
        auto* tag = b.CreateExtractValue(val, {0});

        auto* nil_bb = BasicBlock::Create(m_ctx, "nil", fn);
        auto* bool_bb = BasicBlock::Create(m_ctx, "bool", fn);
        auto* i64_bb = BasicBlock::Create(m_ctx, "i64", fn);
        auto* f64_bb = BasicBlock::Create(m_ctx, "f64", fn);
        auto* obj_bb = BasicBlock::Create(m_ctx, "obj", fn);

        {
            auto* sw = b.CreateSwitch(tag, nil_bb, 5);
            sw->addCase(ConstantInt::get(i32_ty, TAG_NIL), nil_bb);
            sw->addCase(ConstantInt::get(i32_ty, TAG_BOOL), bool_bb);
            sw->addCase(ConstantInt::get(i32_ty, TAG_I64), i64_bb);
            sw->addCase(ConstantInt::get(i32_ty, TAG_F64), f64_bb);
            sw->addCase(ConstantInt::get(i32_ty, TAG_OBJ), obj_bb);
        }

        auto* str_from_c = m_module.getFunction("__ang_string_from_c");

        IRBuilder<> bn(nil_bb);
        bn.CreateRet(bn.CreateCall(str_from_c, {bn.CreateGlobalString("nil")}));

        IRBuilder<> bb(bool_bb);
        bb.CreateRet(bb.CreateCall(str_from_c, {bb.CreateGlobalString("bool")}));

        IRBuilder<> bi(i64_bb);
        bi.CreateRet(bi.CreateCall(str_from_c, {bi.CreateGlobalString("i64")}));

        IRBuilder<> bf(f64_bb);
        bf.CreateRet(bf.CreateCall(str_from_c, {bf.CreateGlobalString("f64")}));

        IRBuilder<> bo(obj_bb);
        bo.CreateRet(bo.CreateCall(str_from_c, {bo.CreateGlobalString("object")}));
    }
}

void RuntimeBuilder::generateEquality() {
    auto* i32_ty = Type::getInt32Ty(m_ctx);
    auto* i64_ty = Type::getInt64Ty(m_ctx);
    auto* f64_ty = Type::getDoubleTy(m_ctx);
    auto* i8_ptr = PointerType::get(m_ctx, 0);
    auto* obj_ty = m_angara_obj_type;

    auto pack_bool = [&](IRBuilder<>& b, Value* bool_val) -> Value* {
        auto* extended = b.CreateZExt(bool_val, i64_ty);
        Value* result = UndefValue::get(obj_ty);
        result = b.CreateInsertValue(result, ConstantInt::get(i32_ty, TAG_BOOL), {0});
        result = b.CreateInsertValue(result, extended, {1});
        return result;
    };

    {
        auto* fn_ty = FunctionType::get(obj_ty, {obj_ty, obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_equals", fn_ty);
        m_fn_equals = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> bb(entry);
        auto* a = fn->arg_begin();
        auto* b_arg = fn->arg_begin() + 1;

        auto* tag_a = bb.CreateExtractValue(a, {0}, "tag_a");
        auto* tag_b = bb.CreateExtractValue(b_arg, {0}, "tag_b");

        auto* tags_eq = bb.CreateICmpEQ(tag_a, tag_b, "tags_eq");
        auto* both_num = bb.CreateOr(
            bb.CreateAnd(
                bb.CreateICmpEQ(tag_a, ConstantInt::get(i32_ty, TAG_I64)),
                bb.CreateICmpEQ(tag_b, ConstantInt::get(i32_ty, TAG_F64))),
            bb.CreateAnd(
                bb.CreateICmpEQ(tag_a, ConstantInt::get(i32_ty, TAG_F64)),
                bb.CreateICmpEQ(tag_b, ConstantInt::get(i32_ty, TAG_I64)))
        );
        auto* can_compare = bb.CreateOr(tags_eq, both_num, "can_compare");
        auto* not_eq_bb = BasicBlock::Create(m_ctx, "not_eq", fn);
        auto* check_same_bb = BasicBlock::Create(m_ctx, "check_same", fn);
        bb.CreateCondBr(can_compare, check_same_bb, not_eq_bb);

        IRBuilder<> bns(not_eq_bb);
        bns.CreateRet(pack_bool(bns, ConstantInt::get(Type::getInt1Ty(m_ctx), 0)));

        IRBuilder<> bcs(check_same_bb);
        auto* payload_a = bcs.CreateExtractValue(a, {1});
        auto* payload_b = bcs.CreateExtractValue(b_arg, {1});

        auto* same_tag_bb = BasicBlock::Create(m_ctx, "same_tag", fn);
        auto* cross_num_bb = BasicBlock::Create(m_ctx, "cross_num", fn);
        bcs.CreateCondBr(tags_eq, same_tag_bb, cross_num_bb);

        IRBuilder<> bst(same_tag_bb);
        auto* nil_eq_bb = BasicBlock::Create(m_ctx, "nil_eq", fn);
        auto* sw = bst.CreateSwitch(tag_a, nil_eq_bb, 5);

        IRBuilder<> bne(nil_eq_bb);
        bne.CreateRet(pack_bool(bne, ConstantInt::get(Type::getInt1Ty(m_ctx), 1)));

        auto* bool_eq_bb = BasicBlock::Create(m_ctx, "bool_eq", fn);
        sw->addCase(ConstantInt::get(i32_ty, TAG_BOOL), bool_eq_bb);
        {
            IRBuilder<> bbe(bool_eq_bb);
            auto* ba = bbe.CreateTrunc(bbe.CreateBitCast(payload_a, i64_ty), Type::getInt1Ty(m_ctx));
            auto* bb2 = bbe.CreateTrunc(bbe.CreateBitCast(payload_b, i64_ty), Type::getInt1Ty(m_ctx));
            bbe.CreateRet(pack_bool(bbe, bbe.CreateICmpEQ(ba, bb2)));
        }

        auto* i64_eq_bb = BasicBlock::Create(m_ctx, "i64_eq", fn);
        sw->addCase(ConstantInt::get(i32_ty, TAG_I64), i64_eq_bb);
        {
            IRBuilder<> bie(i64_eq_bb);
            auto* ia = bie.CreateBitCast(payload_a, i64_ty);
            auto* ib = bie.CreateBitCast(payload_b, i64_ty);
            bie.CreateRet(pack_bool(bie, bie.CreateICmpEQ(ia, ib)));
        }

        auto* f64_eq_bb = BasicBlock::Create(m_ctx, "f64_eq", fn);
        sw->addCase(ConstantInt::get(i32_ty, TAG_F64), f64_eq_bb);
        {
            IRBuilder<> bfe(f64_eq_bb);
            auto* da = bfe.CreateBitCast(payload_a, f64_ty);
            auto* db = bfe.CreateBitCast(payload_b, f64_ty);
            bfe.CreateRet(pack_bool(bfe, bfe.CreateFCmpOEQ(da, db)));
        }

        auto* obj_eq_bb = BasicBlock::Create(m_ctx, "obj_eq", fn);
        sw->addCase(ConstantInt::get(i32_ty, TAG_OBJ), obj_eq_bb);
        {
            IRBuilder<> boe(obj_eq_bb);
            auto* pa = boe.CreateBitCast(payload_a, i64_ty);
            auto* pb = boe.CreateBitCast(payload_b, i64_ty);
            auto* is_string_bb = BasicBlock::Create(m_ctx, "is_str_eq", fn);
            auto* ptr_eq_bb = BasicBlock::Create(m_ctx, "ptr_eq", fn);
            auto* ptr_a = boe.CreateIntToPtr(pa, PointerType::get(m_ctx, 0));
            auto* obj_type_a = boe.CreateLoad(i32_ty, boe.CreateStructGEP(m_obj_header_type, ptr_a, 0));
            auto* is_str_a = boe.CreateICmpEQ(obj_type_a, ConstantInt::get(i32_ty, OBJ_STRING));
            auto* ptr_b = boe.CreateIntToPtr(pb, PointerType::get(m_ctx, 0));
            auto* obj_type_b = boe.CreateLoad(i32_ty, boe.CreateStructGEP(m_obj_header_type, ptr_b, 0));
            auto* is_str_b = boe.CreateICmpEQ(obj_type_b, ConstantInt::get(i32_ty, OBJ_STRING));
            auto* both_str = boe.CreateAnd(is_str_a, is_str_b);
            boe.CreateCondBr(both_str, is_string_bb, ptr_eq_bb);

            IRBuilder<> bse(is_string_bb);
            auto* str_a = bse.CreateIntToPtr(pa, PointerType::get(m_ctx, 0));
            auto* str_b = bse.CreateIntToPtr(pb, PointerType::get(m_ctx, 0));
            auto* chars_a = bse.CreateLoad(i8_ptr, bse.CreateStructGEP(m_string_type, str_a, 3));
            auto* chars_b = bse.CreateLoad(i8_ptr, bse.CreateStructGEP(m_string_type, str_b, 3));
            auto* strcmp_fn = m_module.getFunction("strcmp");
            auto* cmp = bse.CreateCall(strcmp_fn, {chars_a, chars_b});
            auto* eq = bse.CreateICmpEQ(cmp, ConstantInt::get(i32_ty, 0));
            bse.CreateRet(pack_bool(bse, eq));

            IRBuilder<> bpe(ptr_eq_bb);
            bpe.CreateRet(pack_bool(bpe, bpe.CreateICmpEQ(pa, pb)));
        }

        IRBuilder<> bcn(cross_num_bb);
        auto* ia = bcn.CreateBitCast(payload_a, i64_ty);
        auto* da = bcn.CreateSIToFP(ia, f64_ty);
        auto* db = bcn.CreateBitCast(payload_b, f64_ty);
        auto* a_is_i64 = bcn.CreateICmpEQ(tag_a, ConstantInt::get(i32_ty, TAG_I64));
        auto* a_as_f64 = bcn.CreateSelect(a_is_i64, da, bcn.CreateBitCast(payload_a, f64_ty));
        auto* b_as_f64 = bcn.CreateSelect(a_is_i64, db, bcn.CreateBitCast(payload_b, f64_ty));
        bcn.CreateRet(pack_bool(bcn, bcn.CreateFCmpOEQ(a_as_f64, b_as_f64)));
    }
}

void RuntimeBuilder::generateDeepClone() {
    auto* obj_ty = m_angara_obj_type;
    auto* fn_ty = FunctionType::get(obj_ty, {obj_ty}, false);
    auto* fn = createRuntimeFunc("__ang_deep_clone", fn_ty);
    m_fn_deep_clone = FunctionCallee(fn);

    auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
    IRBuilder<> b(entry);
    auto* val = fn->arg_begin();

    auto* incref_fn = m_module.getFunction("__ang_incref");
    b.CreateCall(incref_fn, {val});
    b.CreateRet(val);
}
} // namespace angara
