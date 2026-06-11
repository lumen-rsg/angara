#include "RuntimeBuilder.h"
#include "MarkSweepGC.h"

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
        // TAG_OBJ: inspect the heap object subtype via ObjHeader.type
        auto* payload = bo.CreateExtractValue(val, {1}, "payload");
        auto* obj_ptr = bo.CreateIntToPtr(payload, PointerType::get(m_ctx, 0), "obj_ptr");
        auto* header_ptr = bo.CreateBitCast(obj_ptr, PointerType::get(m_ctx, 0));
        auto* subtype = bo.CreateLoad(i32_ty, header_ptr, "subtype");

        auto* str_bb = BasicBlock::Create(m_ctx, "ty_str", fn);
        auto* lst_bb = BasicBlock::Create(m_ctx, "ty_lst", fn);
        auto* rec_bb = BasicBlock::Create(m_ctx, "ty_rec", fn);
        auto* exc_bb = BasicBlock::Create(m_ctx, "ty_exc", fn);
        auto* thr_bb = BasicBlock::Create(m_ctx, "ty_thr", fn);
        auto* mtx_bb = BasicBlock::Create(m_ctx, "ty_mtx", fn);
        auto* cls_bb = BasicBlock::Create(m_ctx, "ty_cls", fn);
        auto* clos_bb = BasicBlock::Create(m_ctx, "ty_clos", fn);
        auto* bm_bb = BasicBlock::Create(m_ctx, "ty_bm", fn);
        auto* obj_other_bb = BasicBlock::Create(m_ctx, "ty_obj_other", fn);

        auto* sub_sw = bo.CreateSwitch(subtype, obj_other_bb, 9);
        sub_sw->addCase(ConstantInt::get(i32_ty, OBJ_STRING), str_bb);
        sub_sw->addCase(ConstantInt::get(i32_ty, OBJ_LIST), lst_bb);
        sub_sw->addCase(ConstantInt::get(i32_ty, OBJ_RECORD), rec_bb);
        sub_sw->addCase(ConstantInt::get(i32_ty, OBJ_EXCEPTION), exc_bb);
        sub_sw->addCase(ConstantInt::get(i32_ty, OBJ_THREAD), thr_bb);
        sub_sw->addCase(ConstantInt::get(i32_ty, OBJ_MUTEX), mtx_bb);
        sub_sw->addCase(ConstantInt::get(i32_ty, OBJ_CLOSURE), clos_bb);
        sub_sw->addCase(ConstantInt::get(i32_ty, OBJ_BOUND_METHOD), bm_bb);
        sub_sw->addCase(ConstantInt::get(i32_ty, OBJ_CLASS), cls_bb);

        IRBuilder<> bs2(str_bb);
        bs2.CreateRet(bs2.CreateCall(str_from_c, {bs2.CreateGlobalString("string")}));

        IRBuilder<> bl2(lst_bb);
        bl2.CreateRet(bl2.CreateCall(str_from_c, {bl2.CreateGlobalString("list")}));

        IRBuilder<> br2(rec_bb);
        br2.CreateRet(br2.CreateCall(str_from_c, {br2.CreateGlobalString("record")}));

        IRBuilder<> be2(exc_bb);
        be2.CreateRet(be2.CreateCall(str_from_c, {be2.CreateGlobalString("exception")}));

        IRBuilder<> bt2(thr_bb);
        bt2.CreateRet(bt2.CreateCall(str_from_c, {bt2.CreateGlobalString("thread")}));

        IRBuilder<> bm2(mtx_bb);
        bm2.CreateRet(bm2.CreateCall(str_from_c, {bm2.CreateGlobalString("mutex")}));

        IRBuilder<> bc2(clos_bb);
        bc2.CreateRet(bc2.CreateCall(str_from_c, {bc2.CreateGlobalString("function")}));

        IRBuilder<> bbm(bm_bb);
        bbm.CreateRet(bbm.CreateCall(str_from_c, {bbm.CreateGlobalString("method")}));

        IRBuilder<> bcls(cls_bb);
        bcls.CreateRet(bcls.CreateCall(str_from_c, {bcls.CreateGlobalString("class")}));

        IRBuilder<> bof(obj_other_bb);
        bof.CreateRet(bof.CreateCall(str_from_c, {bof.CreateGlobalString("object")}));
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
    auto* i32_ty = Type::getInt32Ty(m_ctx);
    auto* i64_ty = Type::getInt64Ty(m_ctx);
    auto* i8_ptr = PointerType::get(m_ctx, 0);
    auto* obj_ty = m_angara_obj_type;

    auto* malloc_fn = m_module.getFunction("malloc");
    auto* memcpy_fn = m_module.getFunction("memcpy");
    auto* strdup_fn = m_module.getFunction("strdup");

    auto pack_obj = [&](IRBuilder<>& b, Value* raw_ptr) -> Value* {
        auto* ptr_i8 = b.CreateBitCast(raw_ptr, i8_ptr);
        auto* ptr_i64 = b.CreatePtrToInt(ptr_i8, i64_ty);
        Value* result = UndefValue::get(obj_ty);
        result = b.CreateInsertValue(result, ConstantInt::get(i32_ty, TAG_OBJ), {0});
        result = b.CreateInsertValue(result, ptr_i64, {1});
        return result;
    };

    auto* fn_ty = FunctionType::get(obj_ty, {obj_ty}, false);
    auto* fn = createRuntimeFunc("__ang_deep_clone", fn_ty);
    m_fn_deep_clone = FunctionCallee(fn);

    auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
    auto* is_obj_bb = BasicBlock::Create(m_ctx, "is_obj", fn);
    auto* not_obj_bb = BasicBlock::Create(m_ctx, "not_obj", fn);

    // Type dispatch blocks
    auto* is_string_bb = BasicBlock::Create(m_ctx, "dc_string", fn);
    auto* is_list_bb = BasicBlock::Create(m_ctx, "dc_list", fn);
    auto* is_record_bb = BasicBlock::Create(m_ctx, "dc_record", fn);
    auto* is_closure_bb = BasicBlock::Create(m_ctx, "dc_closure", fn);
    auto* is_bound_bb = BasicBlock::Create(m_ctx, "dc_bound", fn);
    auto* is_exception_bb = BasicBlock::Create(m_ctx, "dc_exception", fn);
    auto* is_native_bb = BasicBlock::Create(m_ctx, "dc_native", fn);
    auto* is_thread_bb = BasicBlock::Create(m_ctx, "dc_thread", fn);
    auto* is_mutex_bb = BasicBlock::Create(m_ctx, "dc_mutex", fn);
    auto* fallback_bb = BasicBlock::Create(m_ctx, "dc_fallback", fn);

    IRBuilder<> b(entry);
    auto* val = fn->arg_begin();
    auto* tag = b.CreateExtractValue(val, {0}, "tag");
    auto* is_obj = b.CreateICmpEQ(tag, ConstantInt::get(i32_ty, TAG_OBJ));
    b.CreateCondBr(is_obj, is_obj_bb, not_obj_bb);

    // Non-object: return as-is (i64, f64, bool, nil are all value types)
    IRBuilder<> bn(not_obj_bb);
    bn.CreateRet(val);

    // Object: extract pointer and dispatch on object type
    IRBuilder<> bo(is_obj_bb);
    auto* payload = bo.CreateExtractValue(val, {1}, "payload");
    auto* ptr = bo.CreateIntToPtr(payload, i8_ptr, "ptr");
    auto* type_addr = bo.CreateBitCast(ptr, PointerType::get(m_ctx, 0));
    auto* obj_type = bo.CreateLoad(i32_ty, type_addr, "obj_type");
    auto* sw = bo.CreateSwitch(obj_type, fallback_bb, 9);
    sw->addCase(ConstantInt::get(i32_ty, OBJ_STRING), is_string_bb);
    sw->addCase(ConstantInt::get(i32_ty, OBJ_LIST), is_list_bb);
    sw->addCase(ConstantInt::get(i32_ty, OBJ_RECORD), is_record_bb);
    sw->addCase(ConstantInt::get(i32_ty, OBJ_CLOSURE), is_closure_bb);
    sw->addCase(ConstantInt::get(i32_ty, OBJ_BOUND_METHOD), is_bound_bb);
    sw->addCase(ConstantInt::get(i32_ty, OBJ_EXCEPTION), is_exception_bb);
    sw->addCase(ConstantInt::get(i32_ty, OBJ_NATIVE_INSTANCE), is_native_bb);
    sw->addCase(ConstantInt::get(i32_ty, OBJ_THREAD), is_thread_bb);
    sw->addCase(ConstantInt::get(i32_ty, OBJ_MUTEX), is_mutex_bb);

    // --- Clone string ---
    {
        IRBuilder<> bs(is_string_bb);
        auto* str_ptr = bs.CreateBitCast(ptr, PointerType::get(m_ctx, 0));
        auto* len = bs.CreateLoad(i64_ty, bs.CreateStructGEP(m_string_type, str_ptr, 1), "len");
        auto* chars = bs.CreateLoad(i8_ptr, bs.CreateStructGEP(m_string_type, str_ptr, 3), "chars");

        auto* str_size = ConstantInt::get(i64_ty,
            m_module.getDataLayout().getTypeAllocSize(m_string_type));
        auto* mem = bs.CreateCall(malloc_fn, {str_size}, "mem");
        auto* new_ptr = bs.CreateBitCast(mem, PointerType::get(m_ctx, 0), "new_str");

        // Init header: type=OBJ_STRING, refcount=1
        auto* header = bs.CreateStructGEP(m_string_type, new_ptr, 0);
        bs.CreateStore(ConstantInt::get(i32_ty, OBJ_STRING),
            bs.CreateStructGEP(m_obj_header_type, header, 0));
        bs.CreateStore(ConstantInt::get(i32_ty, MarkSweepGC::packMeta(MarkSweepGC::COLOR_WHITE, true)),
            bs.CreateStructGEP(m_obj_header_type, header, 1));
        bs.CreateStore(ConstantPointerNull::get(PointerType::get(m_ctx, 0)),
            bs.CreateStructGEP(m_obj_header_type, header, 2));
        bs.CreateStore(len, bs.CreateStructGEP(m_string_type, new_ptr, 1));
        bs.CreateStore(len, bs.CreateStructGEP(m_string_type, new_ptr, 2));
        bs.CreateStore(bs.CreateCall(strdup_fn, {chars}, "copied_chars"),
            bs.CreateStructGEP(m_string_type, new_ptr, 3));

        bs.CreateRet(pack_obj(bs, new_ptr));
    }

    // --- Clone list ---
    {
        IRBuilder<> bl(is_list_bb);
        auto* list_ptr = bl.CreateBitCast(ptr, PointerType::get(m_ctx, 0));
        auto* count = bl.CreateLoad(i64_ty, bl.CreateStructGEP(m_list_type, list_ptr, 1), "count");
        auto* cap = bl.CreateLoad(i64_ty, bl.CreateStructGEP(m_list_type, list_ptr, 2), "cap");
        auto* elems = bl.CreateLoad(PointerType::get(m_ctx, 0),
            bl.CreateStructGEP(m_list_type, list_ptr, 3), "elems");

        auto* list_size = ConstantInt::get(i64_ty,
            m_module.getDataLayout().getTypeAllocSize(m_list_type));
        auto* mem = bl.CreateCall(malloc_fn, {list_size}, "mem");
        auto* new_ptr = bl.CreateBitCast(mem, PointerType::get(m_ctx, 0), "new_list");

        auto* header = bl.CreateStructGEP(m_list_type, new_ptr, 0);
        bl.CreateStore(ConstantInt::get(i32_ty, OBJ_LIST),
            bl.CreateStructGEP(m_obj_header_type, header, 0));
        bl.CreateStore(ConstantInt::get(i32_ty, MarkSweepGC::packMeta(MarkSweepGC::COLOR_WHITE, true)),
            bl.CreateStructGEP(m_obj_header_type, header, 1));
        bl.CreateStore(ConstantPointerNull::get(PointerType::get(m_ctx, 0)),
            bl.CreateStructGEP(m_obj_header_type, header, 2));
        bl.CreateStore(count, bl.CreateStructGEP(m_list_type, new_ptr, 1));
        bl.CreateStore(cap, bl.CreateStructGEP(m_list_type, new_ptr, 2));

        // Allocate element array
        auto* elem_size = ConstantInt::get(i64_ty,
            m_module.getDataLayout().getTypeAllocSize(obj_ty));
        auto* total = bl.CreateMul(count, elem_size);
        auto* elems_mem = bl.CreateCall(malloc_fn, {total}, "elems_mem");
        bl.CreateCall(memcpy_fn, {elems_mem, bl.CreateBitCast(elems, i8_ptr), total});
        bl.CreateStore(bl.CreateBitCast(elems_mem, PointerType::get(m_ctx, 0)),
            bl.CreateStructGEP(m_list_type, new_ptr, 3));

        // Deep-clone each element
        auto* loop_bb = BasicBlock::Create(m_ctx, "dc_list_loop", fn);
        auto* body_bb = BasicBlock::Create(m_ctx, "dc_list_body", fn);
        auto* done_bb = BasicBlock::Create(m_ctx, "dc_list_done", fn);
        bl.CreateBr(loop_bb);

        IRBuilder<> bl2(loop_bb);
        auto* i_phi = bl2.CreatePHI(i64_ty, 2, "i");
        i_phi->addIncoming(ConstantInt::get(i64_ty, 0), is_list_bb);
        bl2.CreateCondBr(bl2.CreateICmpSLT(i_phi, count), body_bb, done_bb);

        IRBuilder<> bb(body_bb);
        auto* new_elems = bl.CreateBitCast(elems_mem, PointerType::get(m_ctx, 0));
        auto* elem_ptr = bb.CreateGEP(obj_ty, new_elems, {i_phi});
        auto* elem = bb.CreateLoad(obj_ty, elem_ptr, "elem");

        // Recursively deep-clone the element
        auto* clone_fn = m_module.getFunction("__ang_deep_clone");
        auto* cloned = bb.CreateCall(clone_fn, {elem}, "cloned");
        bb.CreateStore(cloned, elem_ptr);

        auto* next = bb.CreateAdd(i_phi, ConstantInt::get(i64_ty, 1));
        bb.CreateBr(loop_bb);
        i_phi->addIncoming(next, body_bb);

        IRBuilder<> bd(done_bb);
        bd.CreateRet(pack_obj(bd, new_ptr));
    }

    // --- Clone record ---
    {
        IRBuilder<> br(is_record_bb);
        auto* rec_ptr = br.CreateBitCast(ptr, PointerType::get(m_ctx, 0));
        auto* count = br.CreateLoad(i64_ty, br.CreateStructGEP(m_record_type, rec_ptr, 1), "count");
        auto* cap = br.CreateLoad(i64_ty, br.CreateStructGEP(m_record_type, rec_ptr, 2), "cap");
        auto* entries = br.CreateLoad(PointerType::get(m_ctx, 0),
            br.CreateStructGEP(m_record_type, rec_ptr, 3), "entries");

        auto* rec_size = ConstantInt::get(i64_ty,
            m_module.getDataLayout().getTypeAllocSize(m_record_type));
        auto* mem = br.CreateCall(malloc_fn, {rec_size}, "mem");
        auto* new_ptr = br.CreateBitCast(mem, PointerType::get(m_ctx, 0), "new_rec");

        auto* header = br.CreateStructGEP(m_record_type, new_ptr, 0);
        br.CreateStore(ConstantInt::get(i32_ty, OBJ_RECORD),
            br.CreateStructGEP(m_obj_header_type, header, 0));
        br.CreateStore(ConstantInt::get(i32_ty, MarkSweepGC::packMeta(MarkSweepGC::COLOR_WHITE, true)),
            br.CreateStructGEP(m_obj_header_type, header, 1));
        br.CreateStore(ConstantPointerNull::get(PointerType::get(m_ctx, 0)),
            br.CreateStructGEP(m_obj_header_type, header, 2));
        br.CreateStore(count, br.CreateStructGEP(m_record_type, new_ptr, 1));
        br.CreateStore(cap, br.CreateStructGEP(m_record_type, new_ptr, 2));

        // Allocate entry array
        auto* entry_size = ConstantInt::get(i64_ty,
            m_module.getDataLayout().getTypeAllocSize(m_record_entry_type));
        auto* total = br.CreateMul(cap, entry_size);
        auto* entries_mem = br.CreateCall(malloc_fn, {total}, "entries_mem");
        br.CreateCall(memcpy_fn, {entries_mem, br.CreateBitCast(entries, i8_ptr), total});
        br.CreateStore(br.CreateBitCast(entries_mem, PointerType::get(m_ctx, 0)),
            br.CreateStructGEP(m_record_type, new_ptr, 3));

        // Deep-clone each entry: strdup the key, deep-clone the value
        auto* loop_bb = BasicBlock::Create(m_ctx, "dc_rec_loop", fn);
        auto* body_bb = BasicBlock::Create(m_ctx, "dc_rec_body", fn);
        auto* done_bb = BasicBlock::Create(m_ctx, "dc_rec_done", fn);
        br.CreateBr(loop_bb);

        IRBuilder<> br2(loop_bb);
        auto* i_phi = br2.CreatePHI(i64_ty, 2, "i");
        i_phi->addIncoming(ConstantInt::get(i64_ty, 0), is_record_bb);
        br2.CreateCondBr(br2.CreateICmpSLT(i_phi, count), body_bb, done_bb);

        IRBuilder<> bb(body_bb);
        auto* new_entries = br.CreateBitCast(entries_mem, PointerType::get(m_ctx, 0));
        auto* entry_ptr = bb.CreateGEP(m_record_entry_type, new_entries, {i_phi});
        auto* key_ptr = bb.CreateStructGEP(m_record_entry_type, entry_ptr, 0);
        auto* old_key = bb.CreateLoad(i8_ptr, key_ptr, "old_key");
        // strdup the key
        bb.CreateStore(bb.CreateCall(strdup_fn, {old_key}, "new_key"), key_ptr);

        // Deep-clone the value
        auto* val_ptr = bb.CreateStructGEP(m_record_entry_type, entry_ptr, 1);
        auto* old_val = bb.CreateLoad(obj_ty, val_ptr, "old_val");
        auto* clone_fn = m_module.getFunction("__ang_deep_clone");
        bb.CreateStore(bb.CreateCall(clone_fn, {old_val}, "cloned_val"), val_ptr);

        auto* next = bb.CreateAdd(i_phi, ConstantInt::get(i64_ty, 1));
        bb.CreateBr(loop_bb);
        i_phi->addIncoming(next, body_bb);

        IRBuilder<> bd(done_bb);
        bd.CreateRet(pack_obj(bd, new_ptr));
    }

    // --- Clone closure: shallow copy (closures share code, captures are read-only) ---
    {
        IRBuilder<> bc(is_closure_bb);
        auto* closure_ptr = bc.CreateBitCast(ptr, PointerType::get(m_ctx, 0));
        auto* closure_size = ConstantInt::get(i64_ty,
            m_module.getDataLayout().getTypeAllocSize(m_closure_type));
        auto* mem = bc.CreateCall(malloc_fn, {closure_size}, "mem");
        auto* new_ptr = bc.CreateBitCast(mem, PointerType::get(m_ctx, 0), "new_closure");
        bc.CreateCall(memcpy_fn, {new_ptr, bc.CreateBitCast(closure_ptr, i8_ptr), closure_size});
        // Reset refcount to 1
        auto* header = bc.CreateStructGEP(m_closure_type, new_ptr, 0);
        bc.CreateStore(ConstantInt::get(i32_ty, MarkSweepGC::packMeta(MarkSweepGC::COLOR_WHITE, true)),
            bc.CreateStructGEP(m_obj_header_type, header, 1));
        bc.CreateStore(ConstantPointerNull::get(PointerType::get(m_ctx, 0)),
            bc.CreateStructGEP(m_obj_header_type, header, 2));
        bc.CreateRet(pack_obj(bc, new_ptr));
    }

    // --- Clone bound method: deep-clone receiver and method ---
    {
        IRBuilder<> bb(is_bound_bb);
        auto* bm_ptr = bb.CreateBitCast(ptr, PointerType::get(m_ctx, 0));
        auto* recv = bb.CreateLoad(obj_ty, bb.CreateStructGEP(m_bound_method_type, bm_ptr, 1), "recv");
        auto* meth = bb.CreateLoad(obj_ty, bb.CreateStructGEP(m_bound_method_type, bm_ptr, 2), "meth");

        auto* bm_size = ConstantInt::get(i64_ty,
            m_module.getDataLayout().getTypeAllocSize(m_bound_method_type));
        auto* mem = bb.CreateCall(malloc_fn, {bm_size}, "mem");
        auto* new_ptr = bb.CreateBitCast(mem, PointerType::get(m_ctx, 0), "new_bm");

        auto* header = bb.CreateStructGEP(m_bound_method_type, new_ptr, 0);
        bb.CreateStore(ConstantInt::get(i32_ty, OBJ_BOUND_METHOD),
            bb.CreateStructGEP(m_obj_header_type, header, 0));
        bb.CreateStore(ConstantInt::get(i32_ty, MarkSweepGC::packMeta(MarkSweepGC::COLOR_WHITE, true)),
            bb.CreateStructGEP(m_obj_header_type, header, 1));
        bb.CreateStore(ConstantPointerNull::get(PointerType::get(m_ctx, 0)),
            bb.CreateStructGEP(m_obj_header_type, header, 2));

        auto* clone_fn = m_module.getFunction("__ang_deep_clone");
        bb.CreateStore(bb.CreateCall(clone_fn, {recv}, "cloned_recv"),
            bb.CreateStructGEP(m_bound_method_type, new_ptr, 1));
        bb.CreateStore(bb.CreateCall(clone_fn, {meth}, "cloned_meth"),
            bb.CreateStructGEP(m_bound_method_type, new_ptr, 2));

        bb.CreateRet(pack_obj(bb, new_ptr));
    }

    // --- Clone exception: deep-clone the message ---
    {
        IRBuilder<> be(is_exception_bb);
        auto* exc_ptr = be.CreateBitCast(ptr, PointerType::get(m_ctx, 0));
        auto* msg = be.CreateLoad(obj_ty, be.CreateStructGEP(m_exception_type, exc_ptr, 1), "msg");

        auto* exc_size = ConstantInt::get(i64_ty,
            m_module.getDataLayout().getTypeAllocSize(m_exception_type));
        auto* mem = be.CreateCall(malloc_fn, {exc_size}, "mem");
        auto* new_ptr = be.CreateBitCast(mem, PointerType::get(m_ctx, 0), "new_exc");

        auto* header = be.CreateStructGEP(m_exception_type, new_ptr, 0);
        be.CreateStore(ConstantInt::get(i32_ty, OBJ_EXCEPTION),
            be.CreateStructGEP(m_obj_header_type, header, 0));
        be.CreateStore(ConstantInt::get(i32_ty, MarkSweepGC::packMeta(MarkSweepGC::COLOR_WHITE, true)),
            be.CreateStructGEP(m_obj_header_type, header, 1));
        be.CreateStore(ConstantPointerNull::get(PointerType::get(m_ctx, 0)),
            be.CreateStructGEP(m_obj_header_type, header, 2));

        auto* clone_fn = m_module.getFunction("__ang_deep_clone");
        be.CreateStore(be.CreateCall(clone_fn, {msg}, "cloned_msg"),
            be.CreateStructGEP(m_exception_type, new_ptr, 1));

        be.CreateRet(pack_obj(be, new_ptr));
    }

    // --- Clone native instance: shallow copy (opaque to runtime) ---
    {
        IRBuilder<> bn2(is_native_bb);
        auto* ni_ptr = bn2.CreateBitCast(ptr, PointerType::get(m_ctx, 0));
        auto* ni_size = ConstantInt::get(i64_ty,
            m_module.getDataLayout().getTypeAllocSize(m_native_instance_type));
        auto* mem = bn2.CreateCall(malloc_fn, {ni_size}, "mem");
        auto* new_ptr = bn2.CreateBitCast(mem, PointerType::get(m_ctx, 0), "new_ni");
        bn2.CreateCall(memcpy_fn, {new_ptr, bn2.CreateBitCast(ni_ptr, i8_ptr), ni_size});
        // Reset refcount to 1
        auto* header = bn2.CreateStructGEP(m_native_instance_type, new_ptr, 0);
        bn2.CreateStore(ConstantInt::get(i32_ty, MarkSweepGC::packMeta(MarkSweepGC::COLOR_WHITE, true)),
            bn2.CreateStructGEP(m_obj_header_type, header, 1));
        bn2.CreateStore(ConstantPointerNull::get(PointerType::get(m_ctx, 0)),
            bn2.CreateStructGEP(m_obj_header_type, header, 2));
        bn2.CreateRet(pack_obj(bn2, new_ptr));
    }

    // --- Clone thread: shallow copy (thread handle is opaque) ---
    {
        IRBuilder<> bt(is_thread_bb);
        auto* thr_ptr = bt.CreateBitCast(ptr, PointerType::get(m_ctx, 0));
        auto* thr_size = ConstantInt::get(i64_ty,
            m_module.getDataLayout().getTypeAllocSize(m_thread_type));
        auto* mem = bt.CreateCall(malloc_fn, {thr_size}, "mem");
        auto* new_ptr = bt.CreateBitCast(mem, PointerType::get(m_ctx, 0), "new_thr");
        bt.CreateCall(memcpy_fn, {new_ptr, bt.CreateBitCast(thr_ptr, i8_ptr), thr_size});
        auto* header = bt.CreateStructGEP(m_thread_type, new_ptr, 0);
        bt.CreateStore(ConstantInt::get(i32_ty, MarkSweepGC::packMeta(MarkSweepGC::COLOR_WHITE, true)),
            bt.CreateStructGEP(m_obj_header_type, header, 1));
        bt.CreateStore(ConstantPointerNull::get(PointerType::get(m_ctx, 0)),
            bt.CreateStructGEP(m_obj_header_type, header, 2));
        bt.CreateRet(pack_obj(bt, new_ptr));
    }

    // --- Clone mutex: shallow copy (mutex handle is opaque) ---
    {
        IRBuilder<> bm(is_mutex_bb);
        auto* mtx_ptr = bm.CreateBitCast(ptr, PointerType::get(m_ctx, 0));
        auto* mtx_size = ConstantInt::get(i64_ty,
            m_module.getDataLayout().getTypeAllocSize(m_mutex_type));
        auto* mem = bm.CreateCall(malloc_fn, {mtx_size}, "mem");
        auto* new_ptr = bm.CreateBitCast(mem, PointerType::get(m_ctx, 0), "new_mtx");
        bm.CreateCall(memcpy_fn, {new_ptr, bm.CreateBitCast(mtx_ptr, i8_ptr), mtx_size});
        auto* header = bm.CreateStructGEP(m_mutex_type, new_ptr, 0);
        bm.CreateStore(ConstantInt::get(i32_ty, MarkSweepGC::packMeta(MarkSweepGC::COLOR_WHITE, true)),
            bm.CreateStructGEP(m_obj_header_type, header, 1));
        bm.CreateStore(ConstantPointerNull::get(PointerType::get(m_ctx, 0)),
            bm.CreateStructGEP(m_obj_header_type, header, 2));
        bm.CreateRet(pack_obj(bm, new_ptr));
    }

    // --- Fallback: shallow incref for unknown types ---
    {
        IRBuilder<> bf(fallback_bb);
        // Can't clone unknown type — just incref and return the same object
        auto* incref_fn = m_module.getFunction("__ang_incref");
        bf.CreateCall(incref_fn, {val});
        bf.CreateRet(val);
    }
}
} // namespace angara
