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

        auto* zero_i64 = ConstantInt::get(i64_ty, 0);

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
            auto* ptr_a = boe.CreateIntToPtr(pa, PointerType::get(m_ctx, 0));
            auto* obj_type_a = boe.CreateLoad(i32_ty, boe.CreateStructGEP(m_obj_header_type, ptr_a, 0));
            auto* ptr_b = boe.CreateIntToPtr(pb, PointerType::get(m_ctx, 0));
            auto* obj_type_b = boe.CreateLoad(i32_ty, boe.CreateStructGEP(m_obj_header_type, ptr_b, 0));
            auto* same_obj_type = boe.CreateICmpEQ(obj_type_a, obj_type_b);

            auto* dispatch_bb = BasicBlock::Create(m_ctx, "obj_dispatch", fn);
            auto* ptr_eq_bb = BasicBlock::Create(m_ctx, "ptr_eq", fn);
            boe.CreateCondBr(same_obj_type, dispatch_bb, ptr_eq_bb);

            // Dispatch on the (shared) obj subtype: string/list/record -> deep;
            // everything else -> pointer identity.
            IRBuilder<> bd(dispatch_bb);
            auto* str_eq_bb = BasicBlock::Create(m_ctx, "str_eq", fn);
            auto* list_eq_bb = BasicBlock::Create(m_ctx, "list_eq", fn);
            auto* record_eq_bb = BasicBlock::Create(m_ctx, "record_eq", fn);
            auto* obj_fallback_bb = BasicBlock::Create(m_ctx, "obj_fallback", fn);
            auto* od_sw = bd.CreateSwitch(obj_type_a, obj_fallback_bb, 3);
            od_sw->addCase(ConstantInt::get(i32_ty, OBJ_STRING), str_eq_bb);
            od_sw->addCase(ConstantInt::get(i32_ty, OBJ_LIST), list_eq_bb);
            od_sw->addCase(ConstantInt::get(i32_ty, OBJ_RECORD), record_eq_bb);

            // String: deep strcmp.
            IRBuilder<> bse(str_eq_bb);
            {
                auto* str_a = bse.CreateIntToPtr(pa, PointerType::get(m_ctx, 0));
                auto* str_b = bse.CreateIntToPtr(pb, PointerType::get(m_ctx, 0));
                auto* chars_a = bse.CreateLoad(i8_ptr, bse.CreateStructGEP(m_string_type, str_a, 3));
                auto* chars_b = bse.CreateLoad(i8_ptr, bse.CreateStructGEP(m_string_type, str_b, 3));
                auto* strcmp_fn = m_module.getFunction("strcmp");
                auto* cmp = bse.CreateCall(strcmp_fn, {chars_a, chars_b});
                bse.CreateRet(pack_bool(bse, bse.CreateICmpEQ(cmp, ConstantInt::get(i32_ty, 0))));
            }

            // List: equal length, then pairwise __ang_equals recursion.
            IRBuilder<> ble(list_eq_bb);
            {
                auto* list_a = ble.CreateIntToPtr(pa, m_list_type->getPointerTo());
                auto* list_b = ble.CreateIntToPtr(pb, m_list_type->getPointerTo());
                auto* cnt_a = ble.CreateLoad(i64_ty, ble.CreateStructGEP(m_list_type, list_a, 1), "cnt_a");
                auto* cnt_b = ble.CreateLoad(i64_ty, ble.CreateStructGEP(m_list_type, list_b, 1), "cnt_b");
                auto* len_eq = ble.CreateICmpEQ(cnt_a, cnt_b);
                auto* list_neq_bb = BasicBlock::Create(m_ctx, "list_neq", fn);
                auto* list_loop_pre_bb = BasicBlock::Create(m_ctx, "list_loop_pre", fn);
                ble.CreateCondBr(len_eq, list_loop_pre_bb, list_neq_bb);

                IRBuilder<> blne(list_neq_bb);
                blne.CreateRet(pack_bool(blne, ConstantInt::get(Type::getInt1Ty(m_ctx), 0)));

                // Loop: i = 0; while i < cnt_a { eq = __ang_equals(elems_a[i], elems_b[i]); if (!eq) ret false; i++ } ret true
                IRBuilder<> blp(list_loop_pre_bb);
                auto* elems_a = blp.CreateLoad(m_angara_obj_type->getPointerTo(),
                    blp.CreateStructGEP(m_list_type, list_a, 3), "elems_a");
                auto* elems_b = blp.CreateLoad(m_angara_obj_type->getPointerTo(),
                    blp.CreateStructGEP(m_list_type, list_b, 3), "elems_b");
                auto* list_loop_bb = BasicBlock::Create(m_ctx, "list_loop", fn);
                auto* list_done_bb = BasicBlock::Create(m_ctx, "list_done", fn);
                blp.CreateBr(list_loop_bb);

                IRBuilder<> bll(list_loop_bb);
                auto* i = bll.CreatePHI(i64_ty, 2, "i");
                i->addIncoming(zero_i64, list_loop_pre_bb);
                auto* cont = bll.CreateICmpSLT(i, cnt_a);
                auto* list_body_bb = BasicBlock::Create(m_ctx, "list_body", fn);
                bll.CreateCondBr(cont, list_body_bb, list_done_bb);

                IRBuilder<> blb(list_body_bb);
                auto* ea = blb.CreateLoad(m_angara_obj_type, blb.CreateGEP(m_angara_obj_type, elems_a, {i}));
                auto* eb = blb.CreateLoad(m_angara_obj_type, blb.CreateGEP(m_angara_obj_type, elems_b, {i}));
                auto* el_eq = blb.CreateCall(m_fn_equals, {ea, eb});
                auto* el_eq_bool = blb.CreateICmpNE(
                    blb.CreateExtractValue(el_eq, {1}), zero_i64);
                auto* list_next_bb = BasicBlock::Create(m_ctx, "list_next", fn);
                auto* list_early_false_bb = BasicBlock::Create(m_ctx, "list_ef", fn);
                blb.CreateCondBr(el_eq_bool, list_next_bb, list_early_false_bb);

                IRBuilder<> blef(list_early_false_bb);
                blef.CreateRet(pack_bool(blef, ConstantInt::get(Type::getInt1Ty(m_ctx), 0)));

                IRBuilder<> blnx(list_next_bb);
                auto* i_next = blnx.CreateAdd(i, ConstantInt::get(i64_ty, 1));
                i->addIncoming(i_next, list_next_bb);
                blnx.CreateBr(list_loop_bb);

                IRBuilder<> bld(list_done_bb);
                bld.CreateRet(pack_bool(bld, ConstantInt::get(Type::getInt1Ty(m_ctx), 1)));
            }

            // Record: equal count, then each entry's value compared by __ang_equals.
            // (Keys are strdup'd strings; compare keys with strcmp, values recursively.)
            IRBuilder<> bre(record_eq_bb);
            {
                auto* rec_a = bre.CreateIntToPtr(pa, m_record_type->getPointerTo());
                auto* rec_b = bre.CreateIntToPtr(pb, m_record_type->getPointerTo());
                auto* cnt_a = bre.CreateLoad(i64_ty, bre.CreateStructGEP(m_record_type, rec_a, 1), "rcnt_a");
                auto* cnt_b = bre.CreateLoad(i64_ty, bre.CreateStructGEP(m_record_type, rec_b, 1), "rcnt_b");
                auto* len_eq = bre.CreateICmpEQ(cnt_a, cnt_b);
                auto* rec_neq_bb = BasicBlock::Create(m_ctx, "rec_neq", fn);
                auto* rec_loop_pre_bb = BasicBlock::Create(m_ctx, "rec_loop_pre", fn);
                bre.CreateCondBr(len_eq, rec_loop_pre_bb, rec_neq_bb);

                IRBuilder<> brne(rec_neq_bb);
                brne.CreateRet(pack_bool(brne, ConstantInt::get(Type::getInt1Ty(m_ctx), 0)));

                IRBuilder<> brp(rec_loop_pre_bb);
                auto* entries_a = brp.CreateLoad(m_record_entry_type->getPointerTo(),
                    brp.CreateStructGEP(m_record_type, rec_a, 3), "entries_a");
                auto* rec_loop_bb = BasicBlock::Create(m_ctx, "rec_loop", fn);
                auto* rec_done_bb = BasicBlock::Create(m_ctx, "rec_done", fn);
                brp.CreateBr(rec_loop_bb);

                IRBuilder<> brl(rec_loop_bb);
                auto* ri = brl.CreatePHI(i64_ty, 2, "ri");
                ri->addIncoming(zero_i64, rec_loop_pre_bb);
                auto* rcont = brl.CreateICmpSLT(ri, cnt_a);
                auto* rec_body_bb = BasicBlock::Create(m_ctx, "rec_body", fn);
                brl.CreateCondBr(rcont, rec_body_bb, rec_done_bb);

                IRBuilder<> brb(rec_body_bb);
                // entry_a = entries_a[ri]; find a matching key in entries_b by strcmp,
                // then compare values. For simplicity (records are usually small and
                // unordered), linear-scan entries_b for the same key.
                auto* entry_a = brb.CreateGEP(m_record_entry_type, entries_a, {ri});
                auto* key_a = brb.CreateLoad(i8_ptr, brb.CreateStructGEP(m_record_entry_type, entry_a, 0), "rkey_a");
                auto* val_a = brb.CreateLoad(m_angara_obj_type, brb.CreateStructGEP(m_record_entry_type, entry_a, 1), "rval_a");
                // Inner scan over entries_b.
                auto* inner_pre_bb = BasicBlock::Create(m_ctx, "rec_inner_pre", fn);
                brb.CreateBr(inner_pre_bb);
                IRBuilder<> bip(inner_pre_bb);
                // entries_b loaded lazily; need it from rec_b.
                auto* entries_b = bip.CreateLoad(m_record_entry_type->getPointerTo(),
                    bip.CreateStructGEP(m_record_type, rec_b, 3), "entries_b");
                auto* inner_loop_bb = BasicBlock::Create(m_ctx, "rec_inner", fn);
                auto* inner_none_bb = BasicBlock::Create(m_ctx, "rec_inner_none", fn);
                bip.CreateBr(inner_loop_bb);

                IRBuilder<> bil(inner_loop_bb);
                auto* j = bil.CreatePHI(i64_ty, 2, "j");
                j->addIncoming(zero_i64, inner_pre_bb);
                auto* jcont = bil.CreateICmpSLT(j, cnt_b);
                auto* inner_body_bb = BasicBlock::Create(m_ctx, "rec_inner_body", fn);
                bil.CreateCondBr(jcont, inner_body_bb, inner_none_bb);

                IRBuilder<> bib(inner_body_bb);
                auto* entry_b = bib.CreateGEP(m_record_entry_type, entries_b, {j});
                auto* key_b = bib.CreateLoad(i8_ptr, bib.CreateStructGEP(m_record_entry_type, entry_b, 0));
                auto* strcmp_fn2 = m_module.getFunction("strcmp");
                auto* kcmp = bib.CreateCall(strcmp_fn2, {key_a, key_b});
                auto* kmatch = bib.CreateICmpEQ(kcmp, ConstantInt::get(i32_ty, 0));
                auto* inner_cmp_val_bb = BasicBlock::Create(m_ctx, "rec_inner_cmp", fn);
                auto* inner_next_bb = BasicBlock::Create(m_ctx, "rec_inner_next", fn);
                bib.CreateCondBr(kmatch, inner_cmp_val_bb, inner_next_bb);

                IRBuilder<> bicv(inner_cmp_val_bb);
                auto* val_b = bicv.CreateLoad(m_angara_obj_type, bicv.CreateStructGEP(m_record_entry_type, entry_b, 1));
                auto* v_eq = bicv.CreateCall(m_fn_equals, {val_a, val_b});
                auto* v_eq_bool = bicv.CreateICmpNE(bicv.CreateExtractValue(v_eq, {1}), zero_i64);
                auto* rec_outer_next_bb = BasicBlock::Create(m_ctx, "rec_outer_next", fn);
                auto* rec_ef_bb = BasicBlock::Create(m_ctx, "rec_ef", fn);
                bicv.CreateCondBr(v_eq_bool, rec_outer_next_bb, rec_ef_bb);

                IRBuilder<> bref(rec_ef_bb);
                bref.CreateRet(pack_bool(bref, ConstantInt::get(Type::getInt1Ty(m_ctx), 0)));

                // key not found yet -> advance inner j.
                IRBuilder<> binx(inner_next_bb);
                auto* j_next = binx.CreateAdd(j, ConstantInt::get(i64_ty, 1));
                j->addIncoming(j_next, inner_next_bb);
                binx.CreateBr(inner_loop_bb);

                // No matching key in b -> not equal.
                IRBuilder<> binn(inner_none_bb);
                binn.CreateRet(pack_bool(binn, ConstantInt::get(Type::getInt1Ty(m_ctx), 0)));

                // Found-and-equal entry -> advance outer i.
                IRBuilder<> bronx(rec_outer_next_bb);
                auto* ri_next = bronx.CreateAdd(ri, ConstantInt::get(i64_ty, 1));
                ri->addIncoming(ri_next, rec_outer_next_bb);
                bronx.CreateBr(rec_loop_bb);

                IRBuilder<> brd(rec_done_bb);
                brd.CreateRet(pack_bool(brd, ConstantInt::get(Type::getInt1Ty(m_ctx), 1)));
            }

            // Other object subtypes (closure/instance/...) or mismatched subtypes -> pointer eq.
            IRBuilder<> bofb(obj_fallback_bb);
            bofb.CreateBr(ptr_eq_bb);

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

// TS-2: general object hash. Hashes any AngaraObject to an i64 for use as a
// map/set key. Scalars hash by their payload; strings via FNV-1a over their
// bytes; lists and records by folding each element/entry's hash. Other object
// subtypes hash by their pointer (identity). The function is recursive
// (__ang_obj_hash calls itself for list/record contents).
void RuntimeBuilder::generateObjectHash() {
    auto* i32_ty = Type::getInt32Ty(m_ctx);
    auto* i64_ty = Type::getInt64Ty(m_ctx);
    auto* f64_ty = Type::getDoubleTy(m_ctx);
    auto* i8_ptr = PointerType::get(m_ctx, 0);
    auto* obj_ty = m_angara_obj_type;

    auto* fn_ty = FunctionType::get(i64_ty, {obj_ty}, false);
    auto* fn = createRuntimeFunc("__ang_obj_hash", fn_ty);
    m_fn_obj_hash = FunctionCallee(fn);

    auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
    IRBuilder<> bb(entry);
    auto* arg = fn->arg_begin();
    auto* tag = bb.CreateExtractValue(arg, {0}, "tag");
    auto* payload = bb.CreateExtractValue(arg, {1}, "payload");

    // FNV-1a constants.
    auto* fnv_offset = ConstantInt::get(i64_ty, 0xcbf29ce484222325ULL);
    auto* fnv_prime = ConstantInt::get(i64_ty, 0x100000001b3ULL);

    auto* nil_h_bb = BasicBlock::Create(m_ctx, "nil_h", fn);
    auto* sw = bb.CreateSwitch(tag, nil_h_bb, 4);

    // default / nil -> 0
    IRBuilder<> bnh(nil_h_bb);
    bnh.CreateRet(ConstantInt::get(i64_ty, 0));

    // bool/i64 -> payload (as i64)
    auto* int_h_bb = BasicBlock::Create(m_ctx, "int_h", fn);
    sw->addCase(ConstantInt::get(i32_ty, TAG_BOOL), int_h_bb);
    sw->addCase(ConstantInt::get(i32_ty, TAG_I64), int_h_bb);
    {
        IRBuilder<> bih(int_h_bb);
        bih.CreateRet(bih.CreateBitCast(payload, i64_ty));
    }

    // f64 -> bitcast to i64
    auto* f64_h_bb = BasicBlock::Create(m_ctx, "f64_h", fn);
    sw->addCase(ConstantInt::get(i32_ty, TAG_F64), f64_h_bb);
    {
        IRBuilder<> bfh(f64_h_bb);
        bfh.CreateRet(bfh.CreateBitCast(payload, i64_ty));
    }

    // obj -> dispatch on subtype
    auto* obj_h_bb = BasicBlock::Create(m_ctx, "obj_h", fn);
    sw->addCase(ConstantInt::get(i32_ty, TAG_OBJ), obj_h_bb);
    {
        IRBuilder<> boh(obj_h_bb);
        auto* ptr = boh.CreateIntToPtr(boh.CreateBitCast(payload, i64_ty), PointerType::get(m_ctx, 0));
        auto* obj_type = boh.CreateLoad(i32_ty, boh.CreateStructGEP(m_obj_header_type, ptr, 0));

        auto* str_h_bb = BasicBlock::Create(m_ctx, "str_h", fn);
        auto* list_h_bb = BasicBlock::Create(m_ctx, "list_h", fn);
        auto* record_h_bb = BasicBlock::Create(m_ctx, "record_h", fn);
        auto* ident_h_bb = BasicBlock::Create(m_ctx, "ident_h", fn);
        auto* h_sw = boh.CreateSwitch(obj_type, ident_h_bb, 3);
        h_sw->addCase(ConstantInt::get(i32_ty, OBJ_STRING), str_h_bb);
        h_sw->addCase(ConstantInt::get(i32_ty, OBJ_LIST), list_h_bb);
        h_sw->addCase(ConstantInt::get(i32_ty, OBJ_RECORD), record_h_bb);

        // String: FNV-1a over bytes.
        IRBuilder<> bsh(str_h_bb);
        {
            auto* str = bsh.CreateIntToPtr(bsh.CreateBitCast(payload, i64_ty), m_string_type->getPointerTo());
            auto* chars = bsh.CreateLoad(i8_ptr, bsh.CreateStructGEP(m_string_type, str, 3), "chars");
            auto* slen = bsh.CreateLoad(i64_ty, bsh.CreateStructGEP(m_string_type, str, 1), "slen");
            auto* loop_bb = BasicBlock::Create(m_ctx, "str_h_loop", fn);
            auto* done_bb = BasicBlock::Create(m_ctx, "str_h_done", fn);
            bsh.CreateBr(loop_bb);

            IRBuilder<> bl(loop_bb);
            auto* i = bl.CreatePHI(i64_ty, 2, "si");
            auto* h = bl.CreatePHI(i64_ty, 2, "sh");
            i->addIncoming(ConstantInt::get(i64_ty, 0), str_h_bb);
            h->addIncoming(fnv_offset, str_h_bb);
            auto* cont = bl.CreateICmpSLT(i, slen);
            auto* body_bb = BasicBlock::Create(m_ctx, "str_h_body", fn);
            bl.CreateCondBr(cont, body_bb, done_bb);

            IRBuilder<> bb2(body_bb);
            auto* byte_ptr = bb2.CreateGEP(Type::getInt8Ty(m_ctx), chars, {i});
            auto* byte = bb2.CreateLoad(Type::getInt8Ty(m_ctx), byte_ptr);
            auto* byte_zext = bb2.CreateZExt(byte, i64_ty);
            auto* h_xor = bb2.CreateXor(h, byte_zext);
            auto* h_mul = bb2.CreateMul(h_xor, fnv_prime);
            auto* i_next = bb2.CreateAdd(i, ConstantInt::get(i64_ty, 1));
            i->addIncoming(i_next, body_bb);
            h->addIncoming(h_mul, body_bb);
            bb2.CreateBr(loop_bb);

            IRBuilder<> bd2(done_bb);
            bd2.CreateRet(h);
        }

        // List: fold hash of each element via __ang_obj_hash, XOR-mixed.
        IRBuilder<> blh(list_h_bb);
        {
            auto* list = blh.CreateIntToPtr(blh.CreateBitCast(payload, i64_ty), m_list_type->getPointerTo());
            auto* cnt = blh.CreateLoad(i64_ty, blh.CreateStructGEP(m_list_type, list, 1), "lhcnt");
            auto* elems = blh.CreateLoad(obj_ty->getPointerTo(),
                blh.CreateStructGEP(m_list_type, list, 3), "lhelems");
            auto* loop_bb = BasicBlock::Create(m_ctx, "list_h_loop", fn);
            auto* done_bb = BasicBlock::Create(m_ctx, "list_h_done", fn);
            blh.CreateBr(loop_bb);

            IRBuilder<> bl(loop_bb);
            auto* i = bl.CreatePHI(i64_ty, 2, "li");
            auto* h = bl.CreatePHI(i64_ty, 2, "lh");
            i->addIncoming(ConstantInt::get(i64_ty, 0), list_h_bb);
            h->addIncoming(fnv_offset, list_h_bb);
            auto* cont = bl.CreateICmpSLT(i, cnt);
            auto* body_bb = BasicBlock::Create(m_ctx, "list_h_body", fn);
            bl.CreateCondBr(cont, body_bb, done_bb);

            IRBuilder<> bb2(body_bb);
            auto* elem = bb2.CreateLoad(obj_ty, bb2.CreateGEP(obj_ty, elems, {i}));
            auto* elem_h = bb2.CreateCall(m_fn_obj_hash, {elem});
            auto* h_xor = bb2.CreateXor(
                bb2.CreateMul(h, fnv_prime), elem_h);
            auto* i_next = bb2.CreateAdd(i, ConstantInt::get(i64_ty, 1));
            i->addIncoming(i_next, body_bb);
            h->addIncoming(h_xor, body_bb);
            bb2.CreateBr(loop_bb);

            IRBuilder<> bd2(done_bb);
            bd2.CreateRet(h);
        }

        // Record: fold each entry's value hash (keys are strings already folded
        // into the element hash via the value comparison semantics; here we mix
        // each value's hash).
        IRBuilder<> brh(record_h_bb);
        {
            auto* rec = brh.CreateIntToPtr(brh.CreateBitCast(payload, i64_ty), m_record_type->getPointerTo());
            auto* cnt = brh.CreateLoad(i64_ty, brh.CreateStructGEP(m_record_type, rec, 1), "rhcnt");
            auto* entries = brh.CreateLoad(m_record_entry_type->getPointerTo(),
                brh.CreateStructGEP(m_record_type, rec, 3), "rhentries");
            auto* loop_bb = BasicBlock::Create(m_ctx, "record_h_loop", fn);
            auto* done_bb = BasicBlock::Create(m_ctx, "record_h_done", fn);
            brh.CreateBr(loop_bb);

            IRBuilder<> bl(loop_bb);
            auto* i = bl.CreatePHI(i64_ty, 2, "ri");
            auto* h = bl.CreatePHI(i64_ty, 2, "rh");
            i->addIncoming(ConstantInt::get(i64_ty, 0), record_h_bb);
            h->addIncoming(fnv_offset, record_h_bb);
            auto* cont = bl.CreateICmpSLT(i, cnt);
            auto* body_bb = BasicBlock::Create(m_ctx, "record_h_body", fn);
            bl.CreateCondBr(cont, body_bb, done_bb);

            IRBuilder<> bb2(body_bb);
            auto* entry_e = bb2.CreateGEP(m_record_entry_type, entries, {i});
            auto* val = bb2.CreateLoad(obj_ty, bb2.CreateStructGEP(m_record_entry_type, entry_e, 1));
            auto* val_h = bb2.CreateCall(m_fn_obj_hash, {val});
            auto* h_xor = bb2.CreateXor(bb2.CreateMul(h, fnv_prime), val_h);
            auto* i_next = bb2.CreateAdd(i, ConstantInt::get(i64_ty, 1));
            i->addIncoming(i_next, body_bb);
            h->addIncoming(h_xor, body_bb);
            bb2.CreateBr(loop_bb);

            IRBuilder<> bd2(done_bb);
            bd2.CreateRet(h);
        }

        // Other object subtypes -> hash by pointer identity.
        IRBuilder<> bih(ident_h_bb);
        bih.CreateRet(bih.CreateBitCast(payload, i64_ty));
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
        // BUG-6: route through __ang_gc_alloc so the clone is tracked/swept/
        // finalized (raw malloc leaked clones forever). gc_alloc inits the header.
        auto* new_ptr = bs.CreateCall(m_module.getFunction("__ang_gc_alloc"),
            {str_size, ConstantInt::get(i32_ty, OBJ_STRING)}, "new_str");
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
        auto* elems = bl.CreateLoad(PointerType::get(m_ctx, 0),
            bl.CreateStructGEP(m_list_type, list_ptr, 3), "elems");

        auto* list_size = ConstantInt::get(i64_ty,
            m_module.getDataLayout().getTypeAllocSize(m_list_type));
        // BUG-6: route through __ang_gc_alloc (see clone string).
        auto* new_ptr = bl.CreateCall(m_module.getFunction("__ang_gc_alloc"),
            {list_size, ConstantInt::get(i32_ty, OBJ_LIST)}, "new_list");
        // BUG-4: stored cap must match the allocation. The element buffer below
        // is sized count*elem_size; copying the source's cap left cap>count, so
        // the next push (which grows only when count==cap) skipped the grow and
        // wrote past the buffer.
        bl.CreateStore(count, bl.CreateStructGEP(m_list_type, new_ptr, 1));
        bl.CreateStore(count, bl.CreateStructGEP(m_list_type, new_ptr, 2));

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
        // BUG-6: route through __ang_gc_alloc (see clone string).
        auto* new_ptr = br.CreateCall(m_module.getFunction("__ang_gc_alloc"),
            {rec_size, ConstantInt::get(i32_ty, OBJ_RECORD)}, "new_rec");
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
        bc.CreateStore(getGcInitialMeta(),
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
        // BUG-6: route through __ang_gc_alloc (see clone string).
        auto* new_ptr = bb.CreateCall(m_module.getFunction("__ang_gc_alloc"),
            {bm_size, ConstantInt::get(i32_ty, OBJ_BOUND_METHOD)}, "new_bm");

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
        // BUG-6: route through __ang_gc_alloc (see clone string).
        auto* new_ptr = be.CreateCall(m_module.getFunction("__ang_gc_alloc"),
            {exc_size, ConstantInt::get(i32_ty, OBJ_EXCEPTION)}, "new_exc");

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
        bn2.CreateStore(getGcInitialMeta(),
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
        bt.CreateStore(getGcInitialMeta(),
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
        bm.CreateStore(getGcInitialMeta(),
            bm.CreateStructGEP(m_obj_header_type, header, 1));
        bm.CreateStore(ConstantPointerNull::get(PointerType::get(m_ctx, 0)),
            bm.CreateStructGEP(m_obj_header_type, header, 2));
        bm.CreateRet(pack_obj(bm, new_ptr));
    }

    // --- Fallback: return same object for unknown types ---
    {
        IRBuilder<> bf(fallback_bb);
        bf.CreateRet(val);
    }
}
} // namespace angara
